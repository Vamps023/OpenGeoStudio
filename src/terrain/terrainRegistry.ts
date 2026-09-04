// Cross-page registry for loaded background terrain elevation.
// TerrainPage stores the loaded DEM grid here; the Roads editor samples
// it for "Stick to Background Terrain".
import type { TerrainData } from '../engine/terrainMesh'

let activeTerrain: TerrainData | null = null

export function setActiveTerrain(terrain: TerrainData | null) {
  activeTerrain = terrain
}

export function getActiveTerrain(): TerrainData | null {
  return activeTerrain
}

/**
 * Sample terrain height at geographic coordinates. Returns null when no
 * terrain is loaded or the point lies outside the grid.
 */
export function terrainHeightAtGeo(lng: number, lat: number): number | null {
  const terrain = activeTerrain
  if (!terrain) return null
  const { west, east, south, north } = terrain.bounds
  if (lng < west || lng > east || lat < south || lat > north) return null
  const fx = ((lng - west) / (east - west)) * (terrain.width - 1)
  const fy = ((north - lat) / (north - south)) * (terrain.height - 1)
  const x0 = Math.max(0, Math.min(terrain.width - 2, Math.floor(fx)))
  const y0 = Math.max(0, Math.min(terrain.height - 2, Math.floor(fy)))
  const tx = fx - x0
  const ty = fy - y0
  const at = (x: number, y: number) => terrain.elevations[y * terrain.width + x]
  const h =
    at(x0, y0) * (1 - tx) * (1 - ty) +
    at(x0 + 1, y0) * tx * (1 - ty) +
    at(x0, y0 + 1) * (1 - tx) * ty +
    at(x0 + 1, y0 + 1) * tx * ty
  return Number.isFinite(h) ? h : null
}
