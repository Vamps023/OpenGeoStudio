// ─────────────────────────────────────────────────────────────────────
// Domain Model — Core Primitive Types
//
// Canonical, UI-independent primitives shared by Editor, 3D Studio, and
// the export pipeline. No React, Three.js, or viewport imports here.
// ─────────────────────────────────────────────────────────────────────

/** 2D point in world/project coordinates (meters). */
export interface Vec2 {
  x: number
  y: number
}

/** 3D point with elevation. */
export interface Vec3 {
  x: number
  y: number
  z: number
}

/** Geographic bounds (WGS84 degrees). */
export interface GeoBounds {
  west: number
  south: number
  east: number
  north: number
}

/** Stable object identifier — unique within a project. */
export type ObjectId = string

/** Type alias for clarity: a station (abscissa along a path) in meters. */
export type Station = number

/** Type alias for clarity: a heading angle in radians. */
export type Heading = number

/** Type alias for clarity: a curvature value in radians per meter. */
export type Curvature = number
