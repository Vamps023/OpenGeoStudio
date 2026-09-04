// Terrain persistence: encode a DEM grid as base64 Float32 so a project can
// be saved to disk (and localStorage) and reopened with its terrain intact.
import type { TerrainData } from '../engine/terrainMesh'

export interface StoredTerrain {
  /** little-endian Float32 elevations, row-major from north (west→east per row) */
  data: string
  width: number
  height: number
  bounds: { west: number; south: number; east: number; north: number }
  minElevation: number
  maxElevation: number
}

function float32ToBase64(values: Float32Array): string {
  const bytes = new Uint8Array(values.buffer, values.byteOffset, values.byteLength)
  let binary = ''
  const chunk = 0x8000
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk))
  }
  return btoa(binary)
}

function base64ToFloat32(base64: string): Float32Array {
  const binary = atob(base64)
  const bytes = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i)
  return new Float32Array(bytes.buffer)
}

export function encodeTerrain(terrain: TerrainData): StoredTerrain {
  return {
    data: float32ToBase64(terrain.elevations),
    width: terrain.width,
    height: terrain.height,
    bounds: { ...terrain.bounds },
    minElevation: terrain.minElevation,
    maxElevation: terrain.maxElevation,
  }
}

export function decodeTerrain(stored: StoredTerrain): TerrainData | null {
  try {
    const elevations = base64ToFloat32(stored.data)
    if (elevations.length !== stored.width * stored.height) return null
    return {
      elevations,
      width: stored.width,
      height: stored.height,
      bounds: { ...stored.bounds },
      minElevation: stored.minElevation,
      maxElevation: stored.maxElevation,
    }
  } catch {
    return null
  }
}
