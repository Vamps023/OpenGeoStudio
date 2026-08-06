/**
 * ProjectContext — Central project state service.
 *
 * This is the SINGLE SOURCE OF TRUTH for project-level data in the main process.
 * All modules (GIS, Scene, Simulation, Export) read from ProjectContext.
 *
 * The renderer pushes state changes here via IPC sync channels:
 *   - projectContext:syncTerrain  (after terrain generation/export)
 *   - projectContext:syncGIS      (after OSM data fetch)
 *   - projectContext:syncScene    (after scene generation)
 *   - projectContext:syncViewport (camera, zoom, map position)
 *   - projectContext:syncAssets   (placed props/set dressing)
 *
 * Commands retrieve data from ProjectContext via the service registry:
 *   const ctx = projectContext.getTerrain();
 *   if (!ctx) { warn('Generate terrain first'); return; }
 *
 * This eliminates the anti-pattern of the renderer passing state as command args.
 * Every caller (toolbar, palette, hotkey, script, plugin) gets the same data
 * from the same place.
 *
 * PERSISTENCE: ProjectContext state is saved to `project-context.json` in the
 * project folder on every sync and on project save. When a project is opened,
 * the state is loaded from this file, restoring terrain, GIS, scene, viewport,
 * and assets automatically — no manual imports required.
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';

// ─── Types ────────────────────────────────────────────────────────

export interface TerrainMetadata {
  /** Bounding box of the generated terrain */
  bounds: { north: number; south: number; east: number; west: number };
  /** CRS string (e.g., 'EPSG:4326') */
  crs: string;
  /** Tile size in kilometers */
  tileSizeKm: number;
  /** Heightmap resolution */
  heightmapResolution: number;
  /** Albedo resolution */
  albedoResolution: number;
  /** Path to the exported terrain package (manifest.json folder) */
  packagePath: string | null;
  /** Path to the manifest.json file */
  manifestPath: string | null;
  /** DEM source used */
  demSource: string;
  /** Imagery source used */
  imagerySource: string;
  /** When the terrain was generated (ISO timestamp) */
  generatedAt: string;
}

export interface GISData {
  /** OSM features keyed by type */
  roads: unknown[] | null;
  buildings: unknown[] | null;
  water: unknown[] | null;
  vegetation: unknown[] | null;
  railways: unknown[] | null;
  /** Built road network (from RoadNetworkBuilder) */
  roadNetwork: unknown | null;
  /** Built railway network (from RoadNetworkBuilder) */
  railwayNetwork: unknown | null;
  /** Bounding box used for the GIS fetch */
  bounds: { north: number; south: number; east: number; west: number } | null;
  /** When the GIS data was fetched (ISO timestamp) */
  fetchedAt: string | null;
}

export interface SceneState {
  /** Whether the 3D scene has been generated */
  generated: boolean;
  /** Path to the scene file (if saved) */
  scenePath: string | null;
  /** Path to the exported GLB file (in project's Scene folder) */
  glbPath: string | null;
  /** When the scene was generated (ISO timestamp) */
  generatedAt: string | null;
  /** Full scene data from SceneBuilder (buildings, roads, railways, trafficSigns) */
  sceneData: unknown | null;
}

export interface ViewportState {
  /** Map center latitude */
  mapLat: number | null;
  /** Map center longitude */
  mapLon: number | null;
  /** Map zoom level */
  mapZoom: number | null;
  /** 3D camera position */
  cameraPosition: { x: number; y: number; z: number } | null;
  /** 3D camera target */
  cameraTarget: { x: number; y: number; z: number } | null;
  /** Active workspace ID */
  activeWorkspace: string | null;
}

export interface AssetPlacement {
  id: string;
  type: string;
  name: string;
  position: { x: number; y: number; z: number };
  rotation: { x: number; y: number; z: number };
  scale: { x: number; y: number; z: number };
  visible: boolean;
}

export interface LayerVisibilityState {
  buildings: boolean;
  roads: boolean;
  railways: boolean;
  trafficSigns: boolean;
  satellite: boolean;
  dem: boolean;
  water: boolean;
  vegetation: boolean;
}

export interface ProjectContextState {
  /** Active project ID */
  projectId: string | null;
  /** Active project name */
  projectName: string | null;
  /** Active project base path */
  basePath: string | null;
  /** Terrain metadata — the foundation of all downstream work */
  terrain: TerrainMetadata | null;
  /** GIS data (OSM features) */
  gis: GISData;
  /** 3D scene state */
  scene: SceneState;
  /** Viewport state (map position, camera, workspace) */
  viewport: ViewportState;
  /** Placed assets (set dressing) */
  assets: AssetPlacement[];
  /** Layer visibility toggles */
  layerVisibility: LayerVisibilityState;
}

// ─── Events ───────────────────────────────────────────────────────

export const PROJECT_CONTEXT_EVENTS = {
  TERRAIN_SYNCED: 'projectContext:terrain-synced',
  GIS_SYNCED: 'projectContext:gis-synced',
  SCENE_SYNCED: 'projectContext:scene-synced',
  VIEWPORT_SYNCED: 'projectContext:viewport-synced',
  ASSETS_SYNCED: 'projectContext:assets-synced',
  LAYER_VISIBILITY_SYNCED: 'projectContext:layer-visibility-synced',
  CLEARED: 'projectContext:cleared',
  RESTORED: 'projectContext:restored',
} as const;

// ─── Service Token ────────────────────────────────────────────────

export const PROJECT_CONTEXT_TOKEN = 'projectContext' as const;

// ─── Default State ────────────────────────────────────────────────

function createDefaultState(): ProjectContextState {
  return {
    projectId: null,
    projectName: null,
    basePath: null,
    terrain: null,
    gis: {
      roads: null,
      buildings: null,
      water: null,
      vegetation: null,
      railways: null,
      roadNetwork: null,
      railwayNetwork: null,
      bounds: null,
      fetchedAt: null,
    },
    scene: {
      generated: false,
      scenePath: null,
      glbPath: null,
      generatedAt: null,
      sceneData: null,
    },
    viewport: {
      mapLat: null,
      mapLon: null,
      mapZoom: null,
      cameraPosition: null,
      cameraTarget: null,
      activeWorkspace: null,
    },
    assets: [],
    layerVisibility: {
      buildings: true,
      roads: true,
      railways: true,
      trafficSigns: true,
      satellite: true,
      dem: true,
      water: true,
      vegetation: true,
    },
  };
}

// ─── Persistence File Name ────────────────────────────────────────

const CONTEXT_FILE_NAME = 'project-context.json';

// ─── Implementation ───────────────────────────────────────────────

export class ProjectContextImpl {
  private state: ProjectContextState = createDefaultState();

  constructor(
    private events: EventBus,
    private logger: Logger,
  ) {}

  // ─── Project ──────────────────────────────────────────────────

  setProject(id: string, name: string, basePath: string): void {
    this.state.projectId = id;
    this.state.projectName = name;
    this.state.basePath = basePath;
    this.logger.info(`ProjectContext: project set to "${name}" (${id})`);
  }

  clearProject(): void {
    this.state = createDefaultState();
    this.events.emit(PROJECT_CONTEXT_EVENTS.CLEARED, {});
    this.logger.info('ProjectContext: cleared');
  }

  // ─── Terrain ──────────────────────────────────────────────────

  syncTerrain(metadata: Partial<TerrainMetadata> & { bounds: TerrainMetadata['bounds'] }): void {
    // Merge with existing terrain state so partial updates (e.g. just bounds)
    // don't overwrite packagePath/manifestPath from a previous export
    this.state.terrain = { ...(this.state.terrain ?? {}), ...metadata } as TerrainMetadata;
    this.events.emit(PROJECT_CONTEXT_EVENTS.TERRAIN_SYNCED, this.state.terrain);
    this.logger.info(
      `ProjectContext: terrain synced — bounds=[${this.state.terrain.bounds.west.toFixed(3)},${this.state.terrain.bounds.south.toFixed(3)} → ${this.state.terrain.bounds.east.toFixed(3)},${this.state.terrain.bounds.north.toFixed(3)}], CRS=${this.state.terrain.crs}`,
    );
    this.persist();
  }

  getTerrain(): TerrainMetadata | null {
    return this.state.terrain;
  }

  getTerrainBounds(): { north: number; south: number; east: number; west: number } | null {
    return this.state.terrain?.bounds ?? null;
  }

  hasTerrain(): boolean {
    return this.state.terrain !== null;
  }

  // ─── GIS ──────────────────────────────────────────────────────

  syncGIS(data: Partial<GISData>): void {
    this.state.gis = { ...this.state.gis, ...data };
    this.events.emit(PROJECT_CONTEXT_EVENTS.GIS_SYNCED, this.state.gis);
    this.logger.info('ProjectContext: GIS data synced');
    this.persist();
  }

  getGIS(): GISData {
    return this.state.gis;
  }

  hasGIS(): boolean {
    return this.state.gis.roads !== null || this.state.gis.buildings !== null;
  }

  // ─── Scene ────────────────────────────────────────────────────

  syncScene(state: Partial<SceneState>): void {
    this.state.scene = { ...this.state.scene, ...state };
    this.events.emit(PROJECT_CONTEXT_EVENTS.SCENE_SYNCED, this.state.scene);
    this.logger.info('ProjectContext: scene state synced');
    this.persist();
  }

  getScene(): SceneState {
    return this.state.scene;
  }

  hasScene(): boolean {
    return this.state.scene.generated;
  }

  getSceneData(): unknown | null {
    return this.state.scene.sceneData;
  }

  // ─── Viewport ────────────────────────────────────────────────

  syncViewport(state: Partial<ViewportState>): void {
    this.state.viewport = { ...this.state.viewport, ...state };
    this.events.emit(PROJECT_CONTEXT_EVENTS.VIEWPORT_SYNCED, this.state.viewport);
  }

  getViewport(): ViewportState {
    return this.state.viewport;
  }

  // ─── Assets (Set Dressing) ───────────────────────────────────

  syncAssets(assets: AssetPlacement[]): void {
    this.state.assets = assets;
    this.events.emit(PROJECT_CONTEXT_EVENTS.ASSETS_SYNCED, this.state.assets);
    this.persist();
  }

  addAsset(asset: AssetPlacement): void {
    this.state.assets = [...this.state.assets, asset];
    this.events.emit(PROJECT_CONTEXT_EVENTS.ASSETS_SYNCED, this.state.assets);
    this.persist();
  }

  removeAsset(id: string): void {
    this.state.assets = this.state.assets.filter((a) => a.id !== id);
    this.events.emit(PROJECT_CONTEXT_EVENTS.ASSETS_SYNCED, this.state.assets);
    this.persist();
  }

  getAssets(): AssetPlacement[] {
    return this.state.assets;
  }

  // ─── Layer Visibility ────────────────────────────────────────

  syncLayerVisibility(state: Partial<LayerVisibilityState>): void {
    this.state.layerVisibility = { ...this.state.layerVisibility, ...state };
    this.events.emit(PROJECT_CONTEXT_EVENTS.LAYER_VISIBILITY_SYNCED, this.state.layerVisibility);
  }

  getLayerVisibility(): LayerVisibilityState {
    return this.state.layerVisibility;
  }

  // ─── Full State ───────────────────────────────────────────────

  getState(): ProjectContextState {
    return { ...this.state };
  }

  /**
   * Restore full state from a saved ProjectContextState (loaded from disk).
   * Used when opening a project — restores terrain, GIS, scene, viewport, assets.
   */
  restoreState(saved: Partial<ProjectContextState>): void {
    const defaults = createDefaultState();
    this.state = {
      projectId: this.state.projectId, // keep current project ID/name/basePath
      projectName: this.state.projectName,
      basePath: this.state.basePath,
      terrain: saved.terrain ?? defaults.terrain,
      gis: { ...defaults.gis, ...saved.gis },
      scene: { ...defaults.scene, ...saved.scene },
      viewport: { ...defaults.viewport, ...saved.viewport },
      assets: saved.assets ?? defaults.assets,
      layerVisibility: { ...defaults.layerVisibility, ...saved.layerVisibility },
    };
    this.events.emit(PROJECT_CONTEXT_EVENTS.RESTORED, this.state);
    this.logger.info(
      `ProjectContext: state restored — terrain=${!!this.state.terrain}, gis=${this.hasGIS()}, scene=${this.state.scene.generated}, assets=${this.state.assets.length}`,
    );
  }

  // ─── Persistence ─────────────────────────────────────────────

  /**
   * Save ProjectContext state to `project-context.json` in the project folder.
   * Called automatically on every terrain/gis/scene/assets sync.
   * Silent — does not throw on failure (non-critical persistence).
   */
  async persist(): Promise<void> {
    if (!this.state.basePath) return;
    try {
      const fs = await import('fs/promises');
      const path = await import('path');
      const filePath = path.join(this.state.basePath, CONTEXT_FILE_NAME);
      // Don't persist projectId/projectName/basePath — those come from the .ogproj file
      const toSave = {
        terrain: this.state.terrain,
        gis: this.state.gis,
        scene: this.state.scene,
        viewport: this.state.viewport,
        assets: this.state.assets,
        layerVisibility: this.state.layerVisibility,
      };
      await fs.writeFile(filePath, JSON.stringify(toSave, null, 2), 'utf-8');
    } catch (err) {
      this.logger.debug('ProjectContext: persist failed (non-critical):', err);
    }
  }

  /**
   * Load ProjectContext state from `project-context.json` in the project folder.
   * Called when a project is opened. Restores all saved state.
   */
  async loadFromDisk(basePath: string): Promise<boolean> {
    try {
      const fs = await import('fs/promises');
      const path = await import('path');
      const filePath = path.join(basePath, CONTEXT_FILE_NAME);
      const data = await fs.readFile(filePath, 'utf-8');
      const saved = JSON.parse(data);
      this.restoreState(saved);
      return true;
    } catch {
      // File doesn't exist or is invalid — that's OK, fresh project
      this.logger.debug(`ProjectContext: no saved state found in ${basePath}`);
      return false;
    }
  }

  // ─── Workflow Stage Helpers ───────────────────────────────────

  /** Returns the current workflow stage based on what's been completed */
  getWorkflowStage(): 'project' | 'terrain' | 'gis' | 'scene' | 'simulation' | 'export' {
    if (!this.state.projectId) return 'project';
    if (!this.state.terrain) return 'terrain';
    if (!this.hasGIS()) return 'gis';
    if (!this.state.scene.generated) return 'scene';
    return 'simulation';
  }
}
