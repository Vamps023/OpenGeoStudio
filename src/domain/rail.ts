// ─────────────────────────────────────────────────────────────────────
// Domain Model — Rail / Track Data
//
// Canonical rail fixture definitions (turnouts, crossings, catch points).
// No React or Three.js imports.
// ─────────────────────────────────────────────────────────────────────

import type { Vec2, ObjectId } from './types'

/** Turnout (switch): facing track extremity splits into trailing + branch. */
export interface RailPoint {
  id: ObjectId
  name: string
  facingTrackId: ObjectId
  facingContact: 'start' | 'end'
  trailingTrackId: ObjectId
  branchTrackId: ObjectId
}

/** Interior crossing of two railway tracks (frog or diamond). */
export interface RailCrossing {
  id: ObjectId
  trackAId: ObjectId
  trackBId: ObjectId
  sA: number
  sB: number
  position: Vec2
  angle: number
  kind: 'frog' | 'diamond'
}

/** Single-blade derail point at a track extremity (catch point). */
export interface CatchPoint {
  id: ObjectId
  trackId: ObjectId
  contact: 'start' | 'end'
  side: 'left' | 'right'
}
