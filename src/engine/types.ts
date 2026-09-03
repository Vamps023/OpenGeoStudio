export interface Vec2 {
  x: number
  y: number
}

export interface PathElement {
  type: 'line' | 'arc'
  x: number
  y: number
  heading: number
  length: number
  curvature: number
}

export interface FittedPath {
  elements: PathElement[]
  length: number
}

export interface LaneSectionDef {
  left: number[]
  right: number[]
}

export interface RoadSpec {
  name: string
  points: Vec2[]
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
}
