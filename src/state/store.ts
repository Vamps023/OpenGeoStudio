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
}

const STORAGE_KEY = 'ogs.projects.v2'

function loadProjects(): Project[] {
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

function persist(projects: Project[]) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(projects))
}

function updateActiveProject(
  get: () => OgsState,
  set: (patch: Partial<OgsState>) => void,
  update: (project: Project) => Project,
) {
  const { projects, activeProjectId } = get()
  if (!activeProjectId) return
  const next = projects.map((project) => (project.id === activeProjectId ? update(project) : project))
  persist(next)
  set({ projects: next })
}

export function uuid(): string {
  return crypto.randomUUID ? crypto.randomUUID() : `${Date.now()}-${Math.random().toString(36).slice(2)}`
}

export const useStore = create<OgsState>((set, get) => ({
  projects: loadProjects(),
  activeProjectId: null,
  tool: 'draw-straight',
  config: { lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 50 },
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
    persist(projects)
    set({ projects, activeProjectId: id })
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
}))
