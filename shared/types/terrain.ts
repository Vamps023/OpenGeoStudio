/**
 * Shared TypeScript type definitions for GeoTerrain Studio.
 * Mirrors the C++ domain model and the Terrain Package manifest.
 */

export interface ApiKeys {
  opentopography?: string;
  mapbox?: string;
  maptiler?: string;
  gpxz?: string;
  stadia?: string;
}

export interface ElectronAPI {
  native: {
    getVersion: () => Promise<string>;
    planGeneration: (bounds: GeoBounds, profile: TerrainProfile) => Promise<GenerationPlan>;
    startGeneration: (sessionId: string, plan: GenerationPlan) => Promise<string>;
    cancelGeneration: (jobId: string) => Promise<void>;
    cancelExport: (sessionId: string) => Promise<boolean>;
    getProgress: (jobId: string) => Promise<JobProgress>;
    exportPackage: (
      sessionId: string,
      outputPath: string,
      preset: string,
      bounds: GeoBounds,
      heightmapFormat: HeightmapFormat,
      albedoFormat: AlbedoFormat,
      heightmapResolution?: number,
      albedoResolution?: number,
      imageryZoom?: number,
      demSource?: string,
      imagerySource?: string,
      apiKeys?: ApiKeys,
      tileRow?: number,
      tileCol?: number,
      maskSettings?: MaskSettings,
      downloadDem?: boolean,
      crsSource?: string,
      gladArdInterval?: number,
    ) => Promise<string>;
  };
  settings: {
    getApiKeys: () => Promise<ApiKeys>;
    setApiKeys: (apiKeys: ApiKeys) => Promise<boolean>;
  };
  dialog: {
    selectFolder: () => Promise<string | null>;
    selectPackage: () => Promise<string | null>;
    saveProject: () => Promise<string | null>;
    loadProject: () => Promise<string | null>;
  };
  fs: {
    readManifest: (packagePath: string) => Promise<unknown>;
    writeManifest: (packagePath: string, manifest: object) => Promise<boolean>;
    saveProject: (filePath: string, data: ProjectData) => Promise<boolean>;
    loadProject: (filePath: string) => Promise<ProjectData | null>;
    readFileBinary: (filePath: string) => Promise<ArrayBuffer>;
  };
  onProgressUpdate: (callback: (progress: JobProgress) => void) => () => void;
  /** Generic IPC access for core services (workspace, project, commands, etc.) */
  ipc: {
    invoke: (channel: string, ...args: any[]) => Promise<any>;
    on: (channel: string, listener: (event: unknown, ...args: any[]) => void) => () => void;
    removeListener: (channel: string, listener: (...args: any[]) => void) => void;
  };
}

export interface GeoBounds {
  west: number;
  south: number;
  east: number;
  north: number;
}

export interface TileCoords {
  row: number;
  col: number;
}

export interface WorldOffset {
  x: number;
  y: number;
  z: number;
}

export interface TileFileSet {
  heightmap?: string;
  albedo?: string;
  roadMask?: string;
  waterMask?: string;
  vegetationMask?: string;
  buildingMask?: string;
  cliffMask?: string;
  splat?: string;
  buildings3D?: string;
  roads3D?: string;
  railways3D?: string;
  trafficSigns3D?: string;
}

export interface ElevationRange {
  min: number;
  max: number;
  units: 'meters' | 'feet';
}

export interface TerrainTile {
  row: number;
  col: number;
  bounds: GeoBounds;
  worldOffset: WorldOffset;
  files: TileFileSet;
  elevation: ElevationRange;
}

export interface TileGridConfig {
  rows: number;
  cols: number;
  chunkSizeM: number;
  tileWidthM?: number;
  tileHeightM?: number;
  heightmapResolution: number;
  albedoResolution: number;
  pixelSizeM?: number;
}

export interface DataSourceInfo {
  id: string;
  name: string;
  attribution: string;
  queryDate?: string;
}

export interface ProcessingOptions {
  normalizeHeights: boolean;
  heightScale: number;
  seamStitching: boolean;
  fillNodata: boolean;
  generateRoadMasks: boolean;
  generateWaterMasks: boolean;
  generateVegetationMasks: boolean;
  generateBuildingMasks: boolean;
  generateCliffMasks: boolean;
  cliffThresholdDegrees: number;
}

export type ExportPreset = 'unigine' | 'unreal' | 'blender' | 'generic' | 'babylon';

export type HeightmapFormat = 'dem' | 'geotiff' | 'r16' | 'png' | 'float32' | 'none';
export type AlbedoFormat = 'png' | 'geotiff';

export interface ExportPresetConfig {
  id: ExportPreset;
  name: string;
  description: string;
  fileFormats: {
    heightmap: string;
    albedo: string;
    masks: string;
  };
  heightmapBitDepth: 16 | 32;
  // Engine-specific options
  unigineOptions?: {
    lmapResolution: number;
    enableStreaming: boolean;
  };
  unrealOptions?: {
    zScale: number;
  };
}

export interface TerrainManifest {
  version: string;
  createdBy: string;
  createdAt: string;
  terrainName: string;
  description?: string;
  bounds: GeoBounds;
  crs: string;
  tileGrid: TileGridConfig;
  tiles: TerrainTile[];
  sources: {
    dem: DataSourceInfo;
    imagery: DataSourceInfo;
    osm?: DataSourceInfo;
  };
  exportPreset: ExportPreset;
  processing: ProcessingOptions;
  features3D?: {
    enabled: boolean;
    extractionTimeMs?: number;
    counts?: { buildings: number; roads: number; trafficSigns: number };
    trafficSigns?: Array<{ lat: number; lon: number; type: string; value?: string }>;
  };
}

export interface TerrainProfile {
  id: string;
  name: string;
  description: string;
  resolution: {
    heightmapSize: number;
    albedoSize: number;
    pixelSizeM: number;
  };
  sources: {
    demSource: string;
    imagerySource: string;
    enableOSM: boolean;
  };
  processing: ProcessingOptions;
}

export type DEMSource =
  | 'aws-terrarium'
  | 'mapzen'
  | 'mapbox-terrain-rgb'
  | 'opentopo-srtmgl1'
  | 'opentopo-srtmgl3'
  | 'opentopo-aw3d30'
  | 'opentopo-cop30'
  | 'opentopo-nasadem'
  | 'opentopo-usgs10m'
  | 'nasa-earthdata'
  | 'gpxz'
  | 'glad-srtm'
  | 'local-file';
export type ImagerySource = 'arcgis' | 'mapbox' | 'maptiler' | 'google' | 'glad-ard' | 'local-file';

/** Supported Coordinate Reference Systems for export */
export type CRSSource =
  | 'EPSG:4326'
  | 'EPSG:32643' | 'EPSG:32644' | 'EPSG:32645' | 'EPSG:32646'
  | 'EPSG:32647' | 'EPSG:32648' | 'EPSG:32649' | 'EPSG:32650'
  | 'EPSG:3857';

/** CRS configuration with display names and descriptions */
export const CRS_CONFIG: Record<CRSSource, { name: string; description: string; utmZone?: number }> = {
  'EPSG:4326': { name: 'WGS 84', description: 'Global geographic CRS (default)' },
  'EPSG:32643': { name: 'UTM Zone 43N', description: 'India/SE Asia (69°E-75°E)', utmZone: 43 },
  'EPSG:32644': { name: 'UTM Zone 44N', description: 'India (75°E-81°E)', utmZone: 44 },
  'EPSG:32645': { name: 'UTM Zone 45N', description: 'India (81°E-87°E)', utmZone: 45 },
  'EPSG:32646': { name: 'UTM Zone 46N', description: 'India/Myanmar (87°E-93°E)', utmZone: 46 },
  'EPSG:32647': { name: 'UTM Zone 47N', description: 'SE Asia (93°E-99°E)', utmZone: 47 },
  'EPSG:32648': { name: 'UTM Zone 48N', description: 'SE Asia/China (99°E-105°E)', utmZone: 48 },
  'EPSG:32649': { name: 'UTM Zone 49N', description: 'China/Vietnam (105°E-111°E)', utmZone: 49 },
  'EPSG:32650': { name: 'UTM Zone 50N', description: 'China (111°E-117°E)', utmZone: 50 },
  'EPSG:3857': { name: 'Web Mercator', description: 'Spherical Mercator (Google Maps)' },
};

export interface GenerationPlan {
  zoom: number;
  tiles: Array<{
    row: number;
    col: number;
    bounds: GeoBounds;
  }>;
  estimatedMemoryMb: number;
  estimatedDurationSec: number;
}

export interface JobProgress {
  jobId: string;
  state: 'idle' | 'planning' | 'downloading' | 'processing' | 'exporting' | 'complete' | 'cancelled' | 'error';
  overallProgress: number;
  currentTile?: string;
  tileProgress?: number;
  message?: string;
  error?: string;
}

export interface ToastAction {
  label: string;
  onClick: () => void;
}

export interface ToastNotification {
  id: string;
  type: 'success' | 'error' | 'info' | 'warning';
  title?: string;
  message: string;
  /** Optional action buttons (e.g. "Undo", "View") */
  actions?: ToastAction[];
  /** Auto-dismiss timeout in ms (0 = manual dismiss only). Default: 5000 for non-error, 0 for error */
  timeout?: number;
}

export interface ExportProgress {
  stage: string;
  current: number;
  total: number;
  message: string;
  percent: number;
}

export interface AppState {
  // Selection
  selectedBounds: GeoBounds | null;

  // Profile
  activeProfile: TerrainProfile;

  // Generation
  generationPlan: GenerationPlan | null;
  activeJobId: string | null;
  jobProgress: JobProgress | null;

  // Export
  outputPath: string | null;
  selectedPreset: ExportPreset;
  heightmapFormat: HeightmapFormat;
  albedoFormat: AlbedoFormat;
  exportedManifest: TerrainManifest | null;
  exportedPackagePath: string | null;

  // Quality settings
  demSource: DEMSource;
  imagerySource: ImagerySource;
  imageryZoom: number; // 0 = auto, or 10-19
  heightmapResolution: number;
  albedoResolution: number;

  // CRS settings
  crsSource: CRSSource;

  // GLAD ARD settings
  gladArdInterval: number; // 16-day interval ID for GLAD ARD imagery

  // Tile-based export (new)
  tileSizeKm: number; // Size of each tile in km (1, 2, 4, 8)
  tileGrid: TileGrid | null; // Computed tile grid from selected bounds
  selectedTiles: Set<string>; // Set of "row,col" strings for selected tiles

  // Mask generation settings
  maskSettings: MaskSettings;

  // UI
  activeTab: 'map' | 'layers' | 'jobs' | 'export' | 'view3d';

  // Layer visibility
  buildingsVisible: boolean;
  roadsVisible: boolean;
  signsVisible: boolean;
  satelliteVisible: boolean;
  demVisible: boolean;

  // Notifications (toasts)
  notifications: ToastNotification[];
}

export interface TileGrid {
  rows: number;
  cols: number;
  tileSizeKm: number;
  tiles: TileDefinition[];
}

export interface TileDefinition {
  row: number;
  col: number;
  bounds: GeoBounds;
  center: { lng: number; lat: number };
  selected: boolean;
}

// ─── Mask Generation ──────────────────────────────────────────

export interface MaskSettings {
  generateRoadMask: boolean;
  generateWaterMask: boolean;
  generateVegetationMask: boolean;
  generateBuildingMask: boolean;
  generateCliffMask: boolean;
  cliffThresholdDegrees: number;  // 0-90, default 45
  roadLineWidthPx: number;        // 1-10, default 3
}

export interface MaskGenerationOptions {
  bounds: GeoBounds;
  resolution: number;
  outputPath: string;
  tilePrefix: string;
  generateRoadMask: boolean;
  generateWaterMask: boolean;
  generateVegetationMask: boolean;
  generateBuildingMask: boolean;
  generateCliffMask: boolean;
  cliffThresholdDegrees: number;
  roadLineWidthPx?: number;       // Default: 3
  /** Target heightmap width in pixels. Used by resolution-sensitive masks (vegetation) to ensure output matches heightmap dimensions. */
  heightmapWidth?: number;
  /** Target heightmap height in pixels. Used by resolution-sensitive masks (vegetation) to ensure output matches heightmap dimensions. */
  heightmapHeight?: number;
}

export interface MaskResult {
  roadMask?: string;
  waterMask?: string;
  vegetationMask?: string;
  buildingMask?: string;
  cliffMask?: string;
  generationTimeMs: number;
}

export interface OverpassFeature {
  type: 'way' | 'relation';
  id: number;
  geometry: Array<{ lat: number; lon: number }>;
  tags: Record<string, string>;
}

export interface OverpassQueryResult {
  features: OverpassFeature[];
  queryTimeMs: number;
  featureCount: number;
}

// ─── Project Save/Load ────────────────────────────────────────

export interface ProjectData {
  version: string;
  savedAt: string;
  // Selection bounds
  selectedBounds: GeoBounds | null;
  // Tile grid config
  tileSizeKm: number;
  tileGrid: TileGrid | null;
  selectedTiles: string[]; // Array of "row,col" strings (Set can't be JSON serialized)
  // Shapefile data (GeoJSON FeatureCollection)
  shapefileGeoJSON: {
    type: 'FeatureCollection';
    features: Array<{
      type: 'Feature';
      geometry: {
        type: 'Polygon';
        /** Rings of [x, y] points: [ring][point][x|y] */
        coordinates: number[][][];
      };
      properties: Record<string, unknown>;
    }>;
  } | null;
  shapefileBounds: { minX: number; maxX: number; minY: number; maxY: number } | null;
  // Map view state
  mapCenter: { lng: number; lat: number };
  mapZoom: number;
}

// ─── 3D Geometry Extraction ───────────────────────────────────

export interface BuildingGeometry {
  footprint: Array<{ lat: number; lon: number }>;
  height: number;       // meters, >= 0
  floors: number;       // integer, >= 0
  minLevel: number;     // integer, >= 0
  roofShape?: string;   // 'flat' | 'gabled' | 'hipped' | 'pyramidal' | undefined
  tags: Record<string, string>;
}

export interface RoadGeometry {
  centerline: Array<{ lat: number; lon: number }>;
  width: number;        // meters, >= 0
  surface: string;      // raw surface tag or ''
  highway: string;      // highway classification tag
  bridge?: string;      // raw bridge tag value: 'yes', 'viaduct', etc.
  layer: number;        // parsed layer tag, default 0
  tags: Record<string, string>;
}

export interface TrafficSignGeometry {
  lat: number;
  lon: number;
  type: string;      // e.g. 'traffic_signals', 'stop', 'give_way', 'speed_limit', etc.
  value?: string;    // e.g. speed limit value
  tags: Record<string, string>;
}

export interface Extraction3DResult {
  buildings: BuildingGeometry[];
  roads: RoadGeometry[];
  trafficSigns: TrafficSignGeometry[];
  extractionTimeMs: number;
}

// ─── Store Actions (Zustand) ──────────────────────────────────

export interface StoreActions {
  setSelectedBounds: (bounds: GeoBounds | null) => void;
  setActiveProfile: (profile: TerrainProfile) => void;
  setGenerationPlan: (plan: GenerationPlan | null) => void;
  setActiveJobId: (jobId: string | null) => void;
  setJobProgress: (progress: JobProgress | null) => void;
  setOutputPath: (path: string | null) => void;
  setSelectedPreset: (preset: ExportPreset) => void;
  setHeightmapFormat: (format: HeightmapFormat) => void;
  setAlbedoFormat: (format: AlbedoFormat) => void;
  setDEMSource: (source: DEMSource) => void;
  setImagerySource: (source: ImagerySource) => void;
  setImageryZoom: (zoom: number) => void;
  setHeightmapResolution: (res: number) => void;
  setAlbedoResolution: (res: number) => void;
  setCRSSource: (crs: CRSSource) => void;
  setGladArdInterval: (interval: number) => void;
  setTileSizeKm: (size: number) => void;
  setTileGrid: (grid: TileGrid | null) => void;
  toggleTileSelection: (row: number, col: number) => void;
  selectAllTiles: () => void;
  deselectAllTiles: () => void;
  setSelectedTiles: (tiles: Set<string>) => void;
  setMaskSettings: (settings: Partial<MaskSettings>) => void;
  setBuildingsVisible: (visible: boolean) => void;
  setRoadsVisible: (visible: boolean) => void;
  setSignsVisible: (visible: boolean) => void;
  setSatelliteVisible: (visible: boolean) => void;
  setDemVisible: (visible: boolean) => void;
  setActiveTab: (tab: AppState['activeTab']) => void;
  setExportedData: (manifest: TerrainManifest | null, packagePath: string | null) => void;
  resetGeneration: () => void;
  resetAll: () => void;
  /** Serialize the terrain editing session for project persistence. */
  captureTerrainState: () => Record<string, unknown>;
  /** Restore a previously captured terrain editing session. */
  restoreTerrainState: (state: Record<string, unknown> | undefined) => void;
  exportProgress: ExportProgress | null;
  exportResult: string | null;
  exportStartTime: number | null;
  setExportProgress: (p: ExportProgress | null) => void;
  setExportResult: (r: string | null) => void;
  setExportStartTime: (t: number | null) => void;
  addNotification: (n: Omit<ToastNotification, 'id'>) => void;
  removeNotification: (id: string) => void;
}

export type StoreState = AppState & StoreActions;
