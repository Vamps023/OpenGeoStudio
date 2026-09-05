// ─────────────────────────────────────────────────────────────────────
// Domain Model — Road Data
//
// Canonical road definition. This is the authoritative type consumed by
// the Editor (authoring), 3D Studio (rendering), and the export pipeline.
// No React or Three.js imports.
//
// The existing `RoadData` in `state/store.ts` is re-exported from here
// so all consumers reference the same canonical definition.
// ─────────────────────────────────────────────────────────────────────

import type { Vec2, ObjectId } from './types'
import type { ElevationPoint } from './elevation'
import type { LaneSectionDef } from './lane'
import type { XYFunction } from '../engine/xyFunctions'

/** Road geometry type (how control points are interpreted). */
export type RoadGeometryType = 'straight' | 'polyline' | 'arc'

/** Road classification (bridge, tunnel, or normal road). */
export type RoadType = 'none' | 'bridge' | 'tunnel'

/** Lane taper (express lane growth/reduction). */
export interface LaneTaper {
  side: 'left' | 'right'
  index: number
  mode: 'in' | 'out'
  length: number
  startS?: number
  endS?: number
}

/** Road portion (SCANeR portion editing). */
export interface PortionDef {
  id: string
  name: string
  sEnd: number
  roadType: RoadType
}

/** Railway track parameters (Train section). */
export interface RailwayConfig {
  gauge: number
  railSize: number
  trackbedWidth: number
  sleeperSpacing: number
}

export const DEFAULT_RAILWAY: RailwayConfig = {
  gauge: 1.435,
  railSize: 0.075,
  trackbedWidth: 3.0,
  sleeperSpacing: 0.65,
}

/** Canonical road definition — the authoritative road data model. */
export interface RoadData {
  id: ObjectId
  name: string
  points: Vec2[]
  geometryType?: RoadGeometryType
  elevationProfile?: ElevationPoint[]
  /** SCANeR-style XY function chain. When present, defines the road axis
   *  instead of `points`. */
  functions?: XYFunction[]
  /** Lane expansion/reduction tapers (express lanes). */
  tapers?: LaneTaper[]
  /** Road portions (SCANeR portion editing). */
  portions?: PortionDef[]
  /** Profile (road style) name. */
  profileName?: string
  /** Sub-network exits marked on track extremities. */
  subNetworkExits?: ('start' | 'end')[]
  /** Banking/cant profile (station → signed radians). */
  bankingProfile?: ElevationPoint[]
  /** Legacy simple lane counts (backwards compatibility). */
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
  /** Rich per-lane definition (SCANeR-compatible). When undefined, derived
   *  from {lanesLeft, lanesRight, laneWidth}. */
  laneSection?: LaneSectionDef
  /** Railway track config (Train section). When present, renders as rails. */
  railway?: RailwayConfig
}
