// ─────────────────────────────────────────────────────────────────────
// Domain Model — Barrel Export
//
// Single import point for the canonical domain model. Both Editor and
// 3D Studio should import types from here:
//   import type { Project, RoadData, LaneSectionDef } from '../domain'
//
// No React or Three.js imports in any domain module.
// ─────────────────────────────────────────────────────────────────────

// Core primitives
export type { Vec2, Vec3, GeoBounds, ObjectId, Station, Heading, Curvature } from './types'

// Lane system
export type {
  LaneType,
  CirculationWay,
  VehicleCategory,
  LaneMarking,
  MarkingBehaviour,
  LaneDef,
  MarkingStyle,
  LaneSectionDef,
} from './lane'

// Elevation
export type { ElevationPoint } from './elevation'

// Road
export type {
  RoadGeometryType,
  RoadType,
  LaneTaper,
  PortionDef,
  RailwayConfig,
  RoadData,
} from './road'
export { DEFAULT_RAILWAY } from './road'

// Rail fixtures
export type { RailPoint, RailCrossing, CatchPoint } from './rail'

// Junction / intersection
export type {
  IntersectionTrackEnd,
  IntersectionData,
  IntersectionWay,
  JunctionKey,
} from './junction'

// Terrain
export type { GeoReference, WorkingArea, StoredTerrain } from './terrain'

// Project
export type { Project, SerializedProject } from './project'
export {
  PROJECT_SCHEMA_VERSION,
  serializeProject,
  deserializeProject,
} from './project'
