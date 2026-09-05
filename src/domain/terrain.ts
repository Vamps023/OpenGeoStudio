// ─────────────────────────────────────────────────────────────────────
// Domain Model — Terrain Reference
//
// Canonical terrain/geo-reference definitions. Re-exports the existing
// terrain codec types. No React or Three.js imports.
// ─────────────────────────────────────────────────────────────────────

import type { GeoBounds } from './types'

/** Geographic reference (origin + scale for world↔geo conversion). */
export interface GeoReference {
  lng: number
  lat: number
  scale: number // meters per world unit
}

/** Persisted DEM terrain (base64 Float32 elevations). */
export type { StoredTerrain } from '../terrain/terrainCodec'

/** Working area selection (Terrain workspace). */
export interface WorkingArea {
  bounds: GeoBounds
  tileSizeKm: number
}
