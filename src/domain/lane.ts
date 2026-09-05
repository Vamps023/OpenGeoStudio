// ─────────────────────────────────────────────────────────────────────
// Domain Model — Lane Section Definitions
//
// Canonical lane type system (SCANeR-compatible). Re-exports the
// existing lane types from the engine so there is one source of truth.
// No React or Three.js imports.
// ─────────────────────────────────────────────────────────────────────

export type {
  LaneType,
  CirculationWay,
  VehicleCategory,
  LaneMarking,
  MarkingBehaviour,
} from '../engine/laneTypes'

export type { LaneDef, MarkingStyle, LaneSectionDef } from '../engine/laneTypes'
