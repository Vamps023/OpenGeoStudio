import type { LaneDef, LaneSectionDef, LaneType } from './laneTypes'
import { LANE_TYPE_META } from './laneMetadata'

export type { LaneDef, LaneSectionDef } from './laneTypes'

export interface LaneStrip {
  inner: number
  outer: number
}

export interface LaneLayout {
  left: LaneStrip[]
  right: LaneStrip[]
  totalLeft: number
  totalRight: number
}

export function laneLayout(section: LaneSectionDef): LaneLayout {
  const build = (lanes: LaneDef[], sign: 1 | -1): LaneStrip[] => {
    const strips: LaneStrip[] = []
    let acc = 0
    for (const lane of lanes) {
      strips.push({ inner: acc * sign, outer: (acc + lane.width) * sign })
      acc += lane.width
    }
    return strips
  }
  return {
    left: build(section.left, 1),
    right: build(section.right, -1),
    totalLeft: section.left.reduce((a, l) => a + l.width, 0),
    totalRight: section.right.reduce((a, l) => a + l.width, 0),
  }
}

function uid(): string {
  if (typeof crypto !== 'undefined' && 'randomUUID' in crypto) return crypto.randomUUID()
  return `lane-${Math.random().toString(36).slice(2, 10)}`
}

export function defaultTravelLane(width = 3.5): LaneDef {
  return {
    id: uid(),
    type: 'travel',
    width,
    speedLimit: 50,
    circulation: 'forward',
    vehicles: ['car', 'truck', 'motorcycle', 'emergency'],
    marking: 'solid',
    name: 'Travel',
  }
}

export function defaultSidewalkLane(width = 1.5): LaneDef {
  return {
    id: uid(),
    type: 'sidewalk',
    width,
    speedLimit: 0,
    circulation: 'both',
    vehicles: ['pedestrian'],
    marking: 'solid',
    borderLeftHeight: 0.15,
    name: 'Sidewalk',
  }
}

export function defaultShoulderLane(width = 1.0, hard = false): LaneDef {
  return {
    id: uid(),
    type: hard ? 'hard_shoulder' : 'soft_shoulder',
    width,
    speedLimit: 0,
    circulation: 'forward',
    vehicles: [],
    marking: 'solid',
    name: hard ? 'Hard Shoulder' : 'Soft Shoulder',
  }
}

export function defaultBikeLane(width = 1.5): LaneDef {
  return {
    id: uid(),
    type: 'bike',
    width,
    speedLimit: 25,
    circulation: 'forward',
    vehicles: ['bike'],
    marking: 'dashed',
    name: 'Bike',
  }
}

export function defaultBusLane(width = 3.5): LaneDef {
  return {
    id: uid(),
    type: 'bus',
    width,
    speedLimit: 50,
    circulation: 'forward',
    vehicles: ['bus'],
    marking: 'solid',
    name: 'Bus',
  }
}

export function defaultMedianLane(width = 0.5): LaneDef {
  return {
    id: uid(),
    type: 'median',
    width,
    speedLimit: 0,
    circulation: 'both',
    vehicles: [],
    marking: 'none',
    name: 'Median',
  }
}

export function defaultCurbLane(width = 0.3): LaneDef {
  return {
    id: uid(),
    type: 'curb',
    width,
    speedLimit: 0,
    circulation: 'both',
    vehicles: [],
    marking: 'none',
    borderLeftHeight: 0.12,
    name: 'Curb',
  }
}

export function defaultLaneByType(type: LaneType, width?: number): LaneDef {
  const w = width ?? LANE_TYPE_META[type]?.defaultWidth ?? 3.5
  switch (type) {
    case 'travel':        return defaultTravelLane(w)
    case 'shoulder':      return defaultShoulderLane(w, false)
    case 'hard_shoulder': return defaultShoulderLane(w, true)
    case 'soft_shoulder': return defaultShoulderLane(w, false)
    case 'sidewalk':      return defaultSidewalkLane(w)
    case 'bike':          return defaultBikeLane(w)
    case 'bus':           return defaultBusLane(w)
    case 'median':        return defaultMedianLane(w)
    case 'curb':          return defaultCurbLane(w)
    default:              return { id: uid(), type, width: w, speedLimit: 0, circulation: 'both', vehicles: [], marking: 'none' }
  }
}

export function makeDefaultSection(
  lanesLeft: number,
  lanesRight: number,
  laneWidth: number,
): LaneSectionDef {
  return {
    left: Array.from({ length: lanesLeft }, () => defaultTravelLane(laneWidth)),
    right: Array.from({ length: lanesRight }, () => defaultTravelLane(laneWidth)),
  }
}

export function insertLane(section: LaneSectionDef, side: 'left' | 'right', index: number, lane: LaneDef): LaneSectionDef {
  const next = { left: [...section.left], right: [...section.right] }
  next[side].splice(index, 0, lane)
  return next
}

export function removeLane(section: LaneSectionDef, side: 'left' | 'right', index: number): LaneSectionDef {
  const next = { left: [...section.left], right: [...section.right] }
  next[side].splice(index, 1)
  return next
}

export function moveLane(section: LaneSectionDef, side: 'left' | 'right', from: number, to: number): LaneSectionDef {
  const list = [...section[side]]
  const [item] = list.splice(from, 1)
  list.splice(to, 0, item)
  return { ...section, left: side === 'left' ? list : section.left, right: side === 'right' ? list : section.right }
}

export function updateLane(section: LaneSectionDef, side: 'left' | 'right', index: number, patch: Partial<LaneDef>): LaneSectionDef {
  const list = [...section[side]]
  list[index] = { ...list[index], ...patch }
  return { ...section, left: side === 'left' ? list : section.left, right: side === 'right' ? list : section.right }
}

export function totalLanes(section: LaneSectionDef): number {
  return section.left.length + section.right.length
}

export function totalWidth(section: LaneSectionDef): number {
  return section.left.reduce((a, l) => a + l.width, 0) + section.right.reduce((a, l) => a + l.width, 0)
}
