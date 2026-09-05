import { create } from 'zustand'
import type { ElevationPoint } from '../engine/elevation'
import type { Vec2 } from '../engine/types'
import type { LaneSectionDef } from '../engine/laneTypes'
import { makeDefaultSection, totalLanes, totalWidth } from '../engine/laneLayout'
import type { XYFunction } from '../engine/xyFunctions'
import type { IntersectionData } from '../engine/intersections'
import { decodeTerrain, encodeTerrain, type StoredTerrain } from '../terrain/terrainCodec'
import { setActiveTerrain } from '../terrain/terrainRegistry'
import type { TerrainData } from '../engine/terrainMesh'

// ─── Canonical domain model ─────────────────────────────────────────
// The domain layer (src/domain/) is the single source of truth for road,
// rail, terrain, and project types. Import here for local use and
// re-export so existing imports from state/store continue to work.
import type { RoadGeometryType, RoadType, LaneTaper, PortionDef, RailwayConfig, RoadData } from '../domain/road'
import { DEFAULT_RAILWAY } from '../domain/road'
import type { RailPoint, RailCrossing, CatchPoint } from '../domain/rail'
import type { GeoReference, WorkingArea } from '../domain/terrain'
import type { Project, SerializedProject } from '../domain/project'
import { PROJECT_SCHEMA_VERSION, serializeProject, deserializeProject } from '../domain/project'
import { getLaneSection, getRoadTotalWidth, getRoadTotalLanes } from '../engine/roadServices'

export type { RoadGeometryType, RoadType, LaneTaper, PortionDef, RailwayConfig, RoadData } from '../domain/road'
export { DEFAULT_RAILWAY } from '../domain/road'
export type { RailPoint, RailCrossing, CatchPoint } from '../domain/rail'
export type { GeoReference, WorkingArea } from '../domain/terrain'
export type { Project, SerializedProject } from '../domain/project'
export { PROJECT_SCHEMA_VERSION, serializeProject, deserializeProject } from '../domain/project'

export type Tool =
  | 'select' | 'draw-straight' | 'draw-polyline' | 'draw-arc'
  | 'draw-clothoid' | 'draw-bezier' | 'draw-spline'
  | 'move' | 'extend' | 'split' | 'delete' | 'junction'
  | 'insert-intersection'
  | 'lane-begin' | 'lane-end' | 'lane-insert' | 'lane-remove' | 'lane-border' | 'lane-sidewalk'
  // Train section rail fixtures
  | 'rail-point' | 'rail-crossing' | 'catch-point'

export type EditionConstraint = 'free' | 'fixedRadius' | 'fixedLength'

export interface LayerFlags {
  roadLogicalContent: boolean
  road3dGeneration: boolean
  intersectionLogicalContent: boolean
  intersection3dGeneration: boolean
  wayAxis: boolean
  wayLogicalContents: boolean
  otherSubNetworks: boolean
}

export interface InsertOptions {
  /** "Stick to Background Terrain" — auto altitude/banking picking for new tracks */
  stickToTerrain: boolean
  /** "Default Profile" used by the next new track */
  defaultProfile: 'travel' | 'highway' | 'rural' | 'urban'
}

export interface EditorSelection {
  trackIds: string[]
  intersectionId: string | null
  /** station on the selected track where it was last clicked (function picking) */
  trackStation: number | null
}

export interface EditorConfig {
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
  /** clothoid drawing: output radius (0 = infinite) */
  clothoidRadiusOut: number
  /** clothoid drawing: turn direction */
  clothoidTurn: 'left' | 'right'
}

interface OgsState {
  projects: Project[]
  activeProjectId: string | null
  tool: Tool
  config: EditorConfig
  workspacePath: string | null
  // Selection within the lanes tab (for the Lanes dock widget)
  selectedLaneKey: string | null // e.g. "left:0", "right:2"
  // Border selection: a key like "border:left:outer" or "border:right:inner"
  selectedBorderKey: string | null
  // ── Roads tab (SCANeR) state ──
  selection: EditorSelection
  editionConstraint: EditionConstraint
  layers: LayerFlags
  insertOptions: InsertOptions
  /** locked entering/leaving passageways for ways visualization (max 2) */
  lockedPassageways: string[]
  setSelection: (selection: EditorSelection) => void
  toggleTrackSelection: (trackId: string, additive: boolean) => void
  setEditionConstraint: (constraint: EditionConstraint) => void
  setLayer: (layer: keyof LayerFlags, value: boolean) => void
  setInsertOptions: (patch: Partial<InsertOptions>) => void
  setLockedPassageways: (keys: string[]) => void
  addIntersection: (intersection: IntersectionData) => void
  updateIntersection: (id: string, patch: Partial<IntersectionData>) => void
  deleteIntersectionById: (id: string) => void
  createProject: (name: string) => void
  openProject: (id: string) => void
  closeProject: () => void
  addRoad: (road: RoadData) => void
  updateRoad: (roadId: string, patch: Partial<RoadData>) => void
  replaceRoad: (roadId: string, replacements: RoadData[]) => void
  deleteRoad: (roadId: string) => void
  suppressJunction: (key: string) => void
  restoreJunction: (key: string) => void
  regenerateJunctions: () => void
  setTool: (tool: Tool) => void
  setConfig: (patch: Partial<EditorConfig>) => void
  setGeoRef: (geoRef: GeoReference) => void
  setSelectedLane: (key: string | null) => void
  setSelectedBorder: (key: string | null) => void
  // Rail fixtures (Train section turnout pipeline)
  addRailPoint: (point: RailPoint) => void
  removeRailPointById: (id: string) => void
  addRailCrossing: (crossing: RailCrossing) => void
  removeRailCrossingById: (id: string) => void
  addCatchPoint: (catchPoint: CatchPoint) => void
  removeCatchPointById: (id: string) => void
  /** persist (or clear) the project's background terrain and activate it */
  setProjectTerrain: (terrain: TerrainData | null) => void
  // Lane operations (mutate the active road's laneSection)
  insertLaneAt: (roadId: string, side: 'left' | 'right', index: number, lane: import('../engine/laneTypes').LaneDef) => void
  removeLaneAt: (roadId: string, side: 'left' | 'right', index: number) => void
  updateLaneAt: (roadId: string, side: 'left' | 'right', index: number, patch: Partial<import('../engine/laneTypes').LaneDef>) => void
  moveLaneAt: (roadId: string, side: 'left' | 'right', from: number, to: number) => void
  setLaneBorder: (roadId: string, side: 'left' | 'right', index: number, edge: 'inner' | 'outer', height: number, offset?: number) => void
  refreshProjects: () => Promise<void>
  markHydrated: () => void
  setWorkingArea: (area: { bounds: { west: number; south: number; east: number; north: number }; tileSizeKm: number }) => void
  setSelectedTiles: (keys: string[]) => void
  setOsmBuildings: (buildings: import('../engine/osmBuildings').OsmBuildingData[], meta: { area: { west: number; south: number; east: number; north: number }; fetchedAt: string; total: number }) => void
  deleteOsmBuilding: (id: string) => void
  clearOsmBuildings: () => void
  setPcgConfig: (config: import('../engine/pcgBuildings').PcgProjectConfig) => void
  regeneratePcgBuilding: (id: string) => void
  saveCurrentProject: () => Promise<void>
  deleteProject: (id: string) => Promise<void>
  /** Undo/redo of project mutations (roads, intersections, junctions, geoRef). */
  history: { past: Project[]; future: Project[] }
  /** true once refreshProjects has loaded disk state — blocks stale saves before that */
  hydrated: boolean
  undo: () => void
  redo: () => void
}

const STORAGE_KEY = 'ogs.projects.v2'

// getLaneSection, getRoadTotalWidth, and getRoadTotalLanes are now
// canonical in engine/roadServices.ts. Re-export here for backwards
// compatibility with existing imports from state/store.
export { getLaneSection, getRoadTotalWidth, getRoadTotalLanes } from '../engine/roadServices'

// ─── localStorage fallback (used until Electron bridge is available) ───
function loadProjectsLocal(): Project[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    const parsed = raw ? JSON.parse(raw) : []
    if (!Array.isArray(parsed)) return []
    return parsed.map((project) => {
      const { terrain, ...rest } = project
      void terrain
      return {
        ...rest,
        roads: Array.isArray(project.roads)
          ? project.roads.map((road: RoadData) => ({
            ...road,
            points: Array.isArray(road.points) ? road.points : [],
            elevationProfile: Array.isArray(road.elevationProfile) ? road.elevationProfile : undefined,
            // Materialize laneSection from legacy counts if missing.
            laneSection: road.laneSection ?? makeDefaultSection(road.lanesLeft ?? 1, road.lanesRight ?? 1, road.laneWidth ?? 3.5),
          }))
          : [],
        suppressedJunctions: Array.isArray(project.suppressedJunctions) ? project.suppressedJunctions : [],
        intersections: Array.isArray(project.intersections) ? project.intersections : [],
      }
    })
  } catch {
    return []
  }
}

function persistLocal(projects: Project[]) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(projects))
  } catch {
    // localStorage quota exceeded (large DEM payloads) — the Electron file
    // save is the source of truth in the desktop app
  }
}

// ─── File-based persistence via Electron IPC ───
async function saveProjectToFile(project: Project): Promise<void> {
  if (!window.ogs?.saveProject) return
  const result = await window.ogs.saveProject(project)
  if (!result.success) {
    console.error('[store] Failed to save project to file:', result.error)
  }
}

async function loadProjectsFromDisk(): Promise<Project[]> {
  if (!window.ogs?.listProjects) return loadProjectsLocal()
  try {
    const result = await window.ogs.listProjects()
    if (!result.success || !result.projects) return loadProjectsLocal()
    // keep every persisted field (geoRef, terrain, rail fixtures, portions,
    // tapers, ...) — only normalize the structural ones below
    return result.projects.map((p) => {
      const { dirName, projectDir, ...projectData } = p
      void dirName
      void projectDir
      const data = projectData as unknown as Project
      return {
        ...data,
        roads: Array.isArray(data.roads)
          ? (data.roads as RoadData[]).map((road) => ({
            ...road,
            laneSection: road.laneSection ?? makeDefaultSection(road.lanesLeft ?? 1, road.lanesRight ?? 1, road.laneWidth ?? 3.5),
          }))
          : [],
        suppressedJunctions: Array.isArray(data.suppressedJunctions) ? data.suppressedJunctions : [],
        intersections: Array.isArray(data.intersections) ? data.intersections : [],
      }
    })
  } catch {
    return loadProjectsLocal()
  }
}

const HISTORY_LIMIT = 100

/** Drop rail fixtures whose tracks no longer exist (split/replace/delete). */
function scrubRailFixtures(project: Project): Project {
  const ids = new Set(project.roads.map((road) => road.id))
  return {
    ...project,
    railPoints: project.railPoints?.filter((p) => ids.has(p.facingTrackId) && ids.has(p.trailingTrackId) && ids.has(p.branchTrackId)),
    railCrossings: project.railCrossings?.filter((c) => ids.has(c.trackAId) && ids.has(c.trackBId)),
    catchPoints: project.catchPoints?.filter((c) => ids.has(c.trackId)),
  }
}

function updateActiveProject(
  get: () => OgsState,
  set: (patch: Partial<OgsState>) => void,
  update: (project: Project) => Project,
) {
  const { projects, activeProjectId, history } = get()
  if (!activeProjectId) return
  const current = projects.find((project) => project.id === activeProjectId)
  if (!current) return
  const nextProjects = projects.map((project) => (project.id === activeProjectId ? update(project) : project))
  // pre-mutation snapshot for undo; any new mutation clears the redo stack
  const past = [...history.past, current].slice(-HISTORY_LIMIT)
  set({ projects: nextProjects, history: { past, future: [] } })
  // Block stale saves until disk state has been loaded: without this, the
  // renderer's localStorage snapshot could clobber newer project files
  if (!get().hydrated) return
  persistLocal(nextProjects)
  // Also save to file (async, non-blocking)
  const active = nextProjects.find((p) => p.id === activeProjectId)
  if (active) void saveProjectToFile(active)
}

/** Persist a workspace preference (e.g. terrain tile selection) without
 *  pushing an undo step — clicking tiles is not a content edit. */
function updateActiveProjectSilent(
  get: () => OgsState,
  set: (patch: Partial<OgsState>) => void,
  update: (project: Project) => Project,
) {
  const { projects, activeProjectId } = get()
  if (!activeProjectId) return
  const current = projects.find((project) => project.id === activeProjectId)
  if (!current) return
  const nextProjects = projects.map((project) => (project.id === activeProjectId ? update(project) : project))
  set({ projects: nextProjects })
  if (!get().hydrated) return
  persistLocal(nextProjects)
  const active = nextProjects.find((p) => p.id === activeProjectId)
  if (active) void saveProjectToFile(active)
}

export function uuid(): string {
  return crypto.randomUUID ? crypto.randomUUID() : `${Date.now()}-${Math.random().toString(36).slice(2)}`
}

export const useStore = create<OgsState>((set, get) => ({
  projects: loadProjectsLocal(), // Load from localStorage immediately, refresh from disk on mount
  activeProjectId: null,
  tool: 'draw-straight',
  config: { lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 50, clothoidRadiusOut: 100, clothoidTurn: 'left' },
  workspacePath: null,
  selectedLaneKey: null,
  selectedBorderKey: null,
  selection: { trackIds: [], intersectionId: null, trackStation: null },
  editionConstraint: 'free',
  layers: {
    roadLogicalContent: true,
    road3dGeneration: true,
    intersectionLogicalContent: true,
    intersection3dGeneration: true,
    wayAxis: false,
    wayLogicalContents: false,
    otherSubNetworks: true,
  },
  insertOptions: { stickToTerrain: false, defaultProfile: 'travel' },
  lockedPassageways: [],
  history: { past: [], future: [] },
  hydrated: false,

  setSelection: (selection) => set({ selection }),
  toggleTrackSelection: (trackId, additive) => {
    const current = get().selection
    if (!additive) {
      set({ selection: { trackIds: [trackId], intersectionId: null, trackStation: current.trackStation } })
      return
    }
    const has = current.trackIds.includes(trackId)
    set({
      selection: {
        ...current,
        trackIds: has ? current.trackIds.filter((id) => id !== trackId) : [...current.trackIds, trackId],
      },
    })
  },
  setEditionConstraint: (constraint) => set({ editionConstraint: constraint }),
  setLayer: (layer, value) => set({ layers: { ...get().layers, [layer]: value } }),
  setInsertOptions: (patch) => set({ insertOptions: { ...get().insertOptions, ...patch } }),
  setLockedPassageways: (keys) => set({ lockedPassageways: keys.slice(0, 2) }),

  addIntersection: (intersection) => updateActiveProject(get, set, (project) => ({
    ...project,
    intersections: [...(project.intersections ?? []), intersection],
  })),
  updateIntersection: (id, patch) => updateActiveProject(get, set, (project) => ({
    ...project,
    intersections: (project.intersections ?? []).map((item) => (item.id === id ? { ...item, ...patch } : item)),
  })),
  deleteIntersectionById: (id) => updateActiveProject(get, set, (project) => ({
    ...project,
    intersections: (project.intersections ?? []).filter((item) => item.id !== id),
  })),

  createProject: (name) => {
    const id = uuid()
    const project: Project = {
      id,
      name: name.trim() || 'Untitled',
      createdAt: new Date().toISOString(),
      roads: [],
      suppressedJunctions: [],
      intersections: [],
    }
    const projects = [project, ...get().projects]
    persistLocal(projects)
    set({ projects, activeProjectId: id, history: { past: [], future: [] } })
    // Save to C:\OpenGeoStudio
    void saveProjectToFile(project)
  },

  openProject: (id) => {
    const project = get().projects.find((p) => p.id === id)
    if (project?.terrain) {
      const terrain = decodeTerrain(project.terrain)
      if (terrain) setActiveTerrain(terrain)
    }
    set({ activeProjectId: id, history: { past: [], future: [] } })
  },
  closeProject: () => set({ activeProjectId: null, history: { past: [], future: [] } }),

  undo: () => {
    const { history, projects, activeProjectId } = get()
    if (history.past.length === 0 || !activeProjectId) return
    const current = projects.find((project) => project.id === activeProjectId)
    if (!current) return
    const previous = history.past[history.past.length - 1]
    const nextProjects = projects.map((project) => (project.id === activeProjectId ? previous : project))
    persistLocal(nextProjects)
    set({
      projects: nextProjects,
      history: { past: history.past.slice(0, -1), future: [...history.future, current].slice(-HISTORY_LIMIT) },
    })
    void saveProjectToFile(previous)
  },

  redo: () => {
    const { history, projects, activeProjectId } = get()
    if (history.future.length === 0 || !activeProjectId) return
    const current = projects.find((project) => project.id === activeProjectId)
    if (!current) return
    const next = history.future[history.future.length - 1]
    const nextProjects = projects.map((project) => (project.id === activeProjectId ? next : project))
    persistLocal(nextProjects)
    set({
      projects: nextProjects,
      history: { past: [...history.past, current].slice(-HISTORY_LIMIT), future: history.future.slice(0, -1) },
    })
    void saveProjectToFile(next)
  },

  addRoad: (road) => updateActiveProject(get, set, (project) => ({ ...project, roads: [...project.roads, road] })),
  updateRoad: (roadId, patch) => updateActiveProject(get, set, (project) => ({
    ...project,
    roads: project.roads.map((road) => (road.id === roadId ? { ...road, ...patch, id: road.id } : road)),
  })),
  replaceRoad: (roadId, replacements) => updateActiveProject(get, set, (project) => {
    const index = project.roads.findIndex((road) => road.id === roadId)
    if (index < 0) return project
    const roads = [...project.roads]
    roads.splice(index, 1, ...replacements)
    return scrubRailFixtures({ ...project, roads })
  }),
  deleteRoad: (roadId) => updateActiveProject(get, set, (project) => scrubRailFixtures({
    ...project,
    roads: project.roads.filter((road) => road.id !== roadId),
    suppressedJunctions: project.suppressedJunctions.filter((key) => !key.split('|').includes(roadId)),
  })),
  suppressJunction: (key) => updateActiveProject(get, set, (project) => ({
    ...project,
    suppressedJunctions: project.suppressedJunctions.includes(key)
      ? project.suppressedJunctions
      : [...project.suppressedJunctions, key],
  })),
  restoreJunction: (key) => updateActiveProject(get, set, (project) => ({
    ...project,
    suppressedJunctions: project.suppressedJunctions.filter((item) => item !== key),
  })),
  regenerateJunctions: () => updateActiveProject(get, set, (project) => ({ ...project, suppressedJunctions: [] })),
  setTool: (tool) => set({ tool }),
  setConfig: (patch) => set({ config: { ...get().config, ...patch } }),
  setGeoRef: (geoRef) => updateActiveProject(get, set, (project) => ({ ...project, geoRef })),
  setSelectedLane: (key) => set({ selectedLaneKey: key }),
  setSelectedBorder: (key) => set({ selectedBorderKey: key }),

  addRailPoint: (point) => updateActiveProject(get, set, (project) => ({
    ...project,
    railPoints: [...(project.railPoints ?? []), point],
  })),
  removeRailPointById: (id) => updateActiveProject(get, set, (project) => ({
    ...project,
    railPoints: (project.railPoints ?? []).filter((item) => item.id !== id),
  })),
  addRailCrossing: (crossing) => updateActiveProject(get, set, (project) => ({
    ...project,
    railCrossings: [...(project.railCrossings ?? []), crossing],
  })),
  removeRailCrossingById: (id) => updateActiveProject(get, set, (project) => ({
    ...project,
    railCrossings: (project.railCrossings ?? []).filter((item) => item.id !== id),
  })),
  addCatchPoint: (catchPoint) => updateActiveProject(get, set, (project) => ({
    ...project,
    catchPoints: [...(project.catchPoints ?? []), catchPoint],
  })),
  removeCatchPointById: (id) => updateActiveProject(get, set, (project) => ({
    ...project,
    catchPoints: (project.catchPoints ?? []).filter((item) => item.id !== id),
  })),
  markHydrated: () => set({ hydrated: true }),
  setWorkingArea: (area) => updateActiveProject(get, set, (project) => ({ ...project, workingArea: area })),
  setSelectedTiles: (keys) => updateActiveProjectSilent(get, set, (project) => ({ ...project, selectedTiles: keys })),
  setOsmBuildings: (buildings, meta) => updateActiveProject(get, set, (project) => ({ ...project, osmBuildings: buildings, osmImport: meta })),
  deleteOsmBuilding: (id) => updateActiveProject(get, set, (project) => ({ ...project, osmBuildings: (project.osmBuildings ?? []).filter((building) => building.id !== id) })),
  clearOsmBuildings: () => updateActiveProject(get, set, (project) => ({ ...project, osmBuildings: [], osmImport: undefined })),
  setPcgConfig: (config) => updateActiveProjectSilent(get, set, (project) => ({ ...project, pcgConfig: config })),
  regeneratePcgBuilding: (id) => updateActiveProjectSilent(get, set, (project) => {
    const config = project.pcgConfig ?? { mode: 'pcg' as const, style: 'generic' as const, seed: 1, detail: 'medium' as const, overrides: {} }
    return { ...project, pcgConfig: { ...config, overrides: { ...config.overrides, [id]: (config.overrides[id] ?? 0) + 1 } } }
  }),
  setProjectTerrain: (terrain) => {
    if (terrain) setActiveTerrain(terrain)
    updateActiveProject(get, set, (project) => ({
      ...project,
      terrain: terrain ? encodeTerrain(terrain) : undefined,
    }))
  },

  // Lane operations - operate on the active road's laneSection.
  // They ensure the section is materialized first, then mutate it.
  insertLaneAt: (roadId, side, index, lane) => updateActiveProject(get, set, (project) => ({
    ...project,
    roads: project.roads.map((road) => {
      if (road.id !== roadId) return road
      const section = getLaneSection(road)
      const next = { left: [...section.left], right: [...section.right] }
      next[side].splice(index, 0, lane)
      const updated = { ...road, laneSection: next }
      // Keep legacy counts in sync.
      updated.lanesLeft = next.left.length
      updated.lanesRight = next.right.length
      return updated
    }),
  })),
  removeLaneAt: (roadId, side, index) => updateActiveProject(get, set, (project) => ({
    ...project,
    roads: project.roads.map((road) => {
      if (road.id !== roadId) return road
      const section = getLaneSection(road)
      const next = { left: [...section.left], right: [...section.right] }
      next[side].splice(index, 1)
      const updated = { ...road, laneSection: next }
      updated.lanesLeft = next.left.length
      updated.lanesRight = next.right.length
      return updated
    }),
  })),
  updateLaneAt: (roadId, side, index, patch) => updateActiveProject(get, set, (project) => ({
    ...project,
    roads: project.roads.map((road) => {
      if (road.id !== roadId) return road
      const section = getLaneSection(road)
      const list = [...section[side]]
      list[index] = { ...list[index], ...patch }
      return { ...road, laneSection: { left: side === 'left' ? list : section.left, right: side === 'right' ? list : section.right } }
    }),
  })),
  moveLaneAt: (roadId, side, from, to) => updateActiveProject(get, set, (project) => ({
    ...project,
    roads: project.roads.map((road) => {
      if (road.id !== roadId) return road
      const section = getLaneSection(road)
      const list = [...section[side]]
      const [item] = list.splice(from, 1)
      list.splice(to, 0, item)
      return { ...road, laneSection: { left: side === 'left' ? list : section.left, right: side === 'right' ? list : section.right } }
    }),
  })),
  setLaneBorder: (roadId, side, index, edge, height, offset) => updateActiveProject(get, set, (project) => ({
    ...project,
    roads: project.roads.map((road) => {
      if (road.id !== roadId) return road
      const section = getLaneSection(road)
      const list = [...section[side]]
      const lane = { ...list[index] }
      if (edge === 'inner') {
        // inner border is owned by the lane on the inside of this one
        // but for simplicity we just expose borderLeftHeight / borderRightHeight
        if (side === 'left') lane.borderRightHeight = height
        else lane.borderLeftHeight = height
        if (offset !== undefined) {
          if (side === 'left') lane.borderRightOffset = offset
          else lane.borderLeftOffset = offset
        }
      } else {
        if (side === 'left') lane.borderLeftHeight = height
        else lane.borderRightHeight = height
        if (offset !== undefined) {
          if (side === 'left') lane.borderLeftOffset = offset
          else lane.borderRightOffset = offset
        }
      }
      list[index] = lane
      return { ...road, laneSection: { left: side === 'left' ? list : section.left, right: side === 'right' ? list : section.right } }
    }),
  })),

  refreshProjects: async () => {
    const projects = await loadProjectsFromDisk()
    persistLocal(projects)
    set({ hydrated: true, projects })
    // Also fetch workspace path
    if (window.ogs?.getWorkspacePath) {
      const ws = await window.ogs.getWorkspacePath()
      if (ws.success) set({ workspacePath: ws.workspacePath ?? null })
    }
  },

  saveCurrentProject: async () => {
    const { projects, activeProjectId } = get()
    if (!activeProjectId) return
    const active = projects.find((p) => p.id === activeProjectId)
    if (active) await saveProjectToFile(active)
  },

  deleteProject: async (id) => {
    const { projects, activeProjectId } = get()
    const project = projects.find((p) => p.id === id)
    if (!project) return
    // Delete from disk
    if (window.ogs?.deleteProject) {
      await window.ogs.deleteProject(project.id, project.name)
    }
    // Remove from state + localStorage
    const next = projects.filter((p) => p.id !== id)
    persistLocal(next)
    set({
      projects: next,
      activeProjectId: activeProjectId === id ? null : activeProjectId,
    })
  },
}))
