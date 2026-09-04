export interface Vec2 {
  x: number
  y: number
}

export type ElementType = 'line' | 'arc' | 'clothoid' | 'bezier' | 'polyline' | 'spline'

export interface PathElement {
  type: ElementType
  x: number
  y: number
  heading: number
  length: number
  curvature: number
  // Clothoid: exit curvature (entry in `curvature`).
  curvatureOut?: number
  // Bezier: absolute control points p1/p2 (p0 = x,y; p3 = element end).
  p1?: Vec2
  p2?: Vec2
  // Polyline / spline: absolute vertex list (first = x,y).
  points?: Vec2[]
}

export interface FittedPath {
  elements: PathElement[]
  length: number
}

// Re-export rich LaneSectionDef from laneTypes for legacy imports.
// Legacy code that passed number[] arrays is no longer compatible; the
// codebase has been migrated to use the per-lane definition system.
export type { LaneSectionDef, LaneDef, LaneType, CirculationWay, VehicleCategory, LaneMarking } from './laneTypes'

export interface RoadSpec {
  name: string
  points: Vec2[]
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
}
