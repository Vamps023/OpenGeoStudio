import { create } from 'zustand'
import type { ElevationPoint } from '../engine/elevation'
import type { Vec2 } from '../engine/types'

export type RoadGeometryType = 'straight' | 'polyline' | 'arc'

export interface RoadData {
  id: string
  name: string
  points: Vec2[]
  geometryType?: RoadGeometryType
  elevationProfile?: ElevationPoint[]
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
}

export interface Project {
  id: string
  name: string
  createdAt: string
  roads: RoadData[]
  suppressedJunctions: string[]
}

export type Tool = 'select' | 'draw-straight' | 'draw-polyline' | 'draw-arc' | 'move' | 'extend' | 'split' | 'delete' | 'junction'

export interface EditorConfig {
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
}

interface OgsState {
  projects: Project[]
  activeProjectId: string | null
  tool: Tool
  config: EditorConfig
  workspacePath: string | null
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
  refreshProjects: () => Promise<void>
  saveCurrentProject: () => Promise<void>
  deleteProject: (id: string) => Promise<void>
}

const STORAGE_KEY = 'ogs.projects.v2'

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
          ? project.roads.map((road) => ({
              ...road,
              points: Array.isArray(road.points) ? road.points : [],
              elevationProfile: Array.isArray(road.elevationProfile) ? road.elevationProfile : undefined,
            }))
          : [],
        suppressedJunctions: Array.isArray(project.suppressedJunctions) ? project.suppressedJunctions : [],
      }
    })
  } catch {
    return []
  }
}

function persistLocal(projects: Project[]) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(projects))
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
    return result.projects.map((p) => {
      const { dirName, projectDir, ...projectData } = p
      void dirName
      void projectDir
      return {
        id: projectData.id as string,
        name: projectData.name as string,
        createdAt: projectData.createdAt as string,
        roads: Array.isArray(projectData.roads) ? projectData.roads as RoadData[] : [],
        suppressedJunctions: Array.isArray(projectData.suppressedJunctions) ? projectData.suppressedJunctions as string[] : [],
      }
    })
  } catch {
    return loadProjectsLocal()
  }
}

function updateActiveProject(
  get: () => OgsState,
  set: (patch: Partial<OgsState>) => void,
  update: (project: Project) => Project,
) {
  const { projects, activeProjectId } = get()
  if (!activeProjectId) return
  const next = projects.map((project) => (project.id === activeProjectId ? update(project) : project))
  persistLocal(next)
  set({ projects: next })
  // Also save to file (async, non-blocking)
  const active = next.find((p) => p.id === activeProjectId)
  if (active) void saveProjectToFile(active)
}

export function uuid(): string {
  return crypto.randomUUID ? crypto.randomUUID() : `${Date.now()}-${Math.random().toString(36).slice(2)}`
}

export const useStore = create<OgsState>((set, get) => ({
  projects: loadProjectsLocal(), // Load from localStorage immediately, refresh from disk on mount
  activeProjectId: null,
  tool: 'draw-straight',
  config: { lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 50 },
  workspacePath: null,

  createProject: (name) => {
    const id = uuid()
    const project: Project = {
      id,
      name: name.trim() || 'Untitled',
      createdAt: new Date().toISOString(),
      roads: [],
      suppressedJunctions: [],
    }
    const projects = [project, ...get().projects]
    persistLocal(projects)
    set({ projects, activeProjectId: id })
    // Save to C:\OpenGeoStudio
    void saveProjectToFile(project)
  },

  openProject: (id) => set({ activeProjectId: id }),
  closeProject: () => set({ activeProjectId: null }),

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
    return { ...project, roads }
  }),
  deleteRoad: (roadId) => updateActiveProject(get, set, (project) => ({
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

  refreshProjects: async () => {
    const projects = await loadProjectsFromDisk()
    persistLocal(projects)
    set({ projects })
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
