// ─────────────────────────────────────────────────────────────────────
// Domain Model — Junction and Intersection Types
//
// Canonical junction/intersection definitions. Re-exports the existing
// engine intersection types and defines the canonical intersection data.
// No React or Three.js imports.
// ─────────────────────────────────────────────────────────────────────

import type { Vec2, ObjectId } from './types'

export type {
  IntersectionTrackEnd,
  IntersectionData,
  IntersectionWay,
} from '../engine/intersections'

/** Junction suppression key — `${roadAId}|${roadBId}` (sorted). */
export type JunctionKey = string
