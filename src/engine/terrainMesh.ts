/**
 * Terrain mesh generation from elevation grids.
 * Produces Three.js-compatible BufferGeometry data for the viewport.
 */

export interface TerrainData {
  elevations: Float32Array
  width: number
  height: number
  bounds: { west: number; south: number; east: number; north: number }
  minElevation: number
  maxElevation: number
}

export interface TerrainMeshData {
  positions: Float32Array
  normals: Float32Array
  colors: Float32Array
  indices: Uint32Array
  width: number
  height: number
}

/**
 * Build a terrain mesh from an elevation grid.
 *
 * The mesh is centered at origin in the XZ plane, with Y as elevation.
 * The terrain is scaled to fit a configurable world size.
 *
 * @param terrain — elevation grid data
 * @param worldSize — size of the terrain in world units (meters)
 * @param heightScale — multiplier for elevation (to exaggerate relief)
 */
export function buildTerrainMesh(
  terrain: TerrainData,
  worldSize = 400,
  heightScale = 1,
): TerrainMeshData {
  const { elevations, width, height, minElevation, maxElevation } = terrain
  const vertexCount = width * height
  const positions = new Float32Array(vertexCount * 3)
  const normals = new Float32Array(vertexCount * 3)
  const colors = new Float32Array(vertexCount * 3)

  const elevationRange = Math.max(1e-6, maxElevation - minElevation)
  const halfWorld = worldSize / 2
  const dx = worldSize / (width - 1)
  const dz = worldSize / (height - 1)

  // Positions and colors
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = y * width + x
      const elevation = elevations[i]
      const normalized = (elevation - minElevation) / elevationRange
      positions[i * 3] = -halfWorld + x * dx
      positions[i * 3 + 1] = elevation * heightScale
      positions[i * 3 + 2] = -halfWorld + y * dz
      // Color: green-brown gradient based on elevation
      colors[i * 3] = 0.2 + normalized * 0.3
      colors[i * 3 + 1] = 0.35 + normalized * 0.15
      colors[i * 3 + 2] = 0.15 + normalized * 0.05
    }
  }

  // Indices
  const indexCount = (width - 1) * (height - 1) * 6
  const indices = new Uint32Array(indexCount)
  let idx = 0
  for (let y = 0; y < height - 1; y++) {
    for (let x = 0; x < width - 1; x++) {
      const a = y * width + x
      const b = y * width + x + 1
      const c = (y + 1) * width + x
      const d = (y + 1) * width + x + 1
      indices[idx++] = a
      indices[idx++] = c
      indices[idx++] = b
      indices[idx++] = b
      indices[idx++] = c
      indices[idx++] = d
    }
  }

  // Compute normals
  computeNormals(positions, indices, normals)

  return { positions, normals, colors, indices, width, height }
}

/**
 * Build a terrain mesh in the roads' world space so the 3D Studio shows
 * terrain and roads aligned: world (x, y) maps to lng/lat through the
 * project geo reference (origin at lng/lat, scale meters per world unit,
 * north = +y → three.js -z). The grid is decimated to maxCells per side.
 */
export function buildTerrainMeshWorld(
  terrain: TerrainData,
  geoRef?: { lng?: number; lat?: number; scale?: number },
  maxCells = 400,
): TerrainMeshData | null {
  const { elevations, width, height, bounds, minElevation, maxElevation } = terrain
  if (width < 2 || height < 2) return null

  const lng0 = geoRef?.lng ?? -95.36
  const lat0 = geoRef?.lat ?? 29.76
  const scale = geoRef?.scale ?? 1
  const latRad = (lat0 * Math.PI) / 180
  const metersPerDegLng = 111320 * Math.cos(latRad)
  const worldX = (lng: number) => ((lng - lng0) * metersPerDegLng) / scale
  const worldY = (lat: number) => ((lat - lat0) * 111320) / scale

  // decimate so large DEMs stay interactive
  const stride = Math.max(1, Math.ceil(Math.max(width - 1, height - 1) / maxCells))
  const gw = Math.floor((width - 1) / stride) + 1
  const gh = Math.floor((height - 1) / stride) + 1
  const elevationRange = Math.max(1e-6, maxElevation - minElevation)

  const positions = new Float32Array(gw * gh * 3)
  const normals = new Float32Array(gw * gh * 3)
  const colors = new Float32Array(gw * gh * 3)

  for (let gy = 0; gy < gh; gy++) {
    const lat = bounds.north + ((bounds.south - bounds.north) * (gy * stride)) / (height - 1)
    for (let gx = 0; gx < gw; gx++) {
      const lng = bounds.west + ((bounds.east - bounds.west) * (gx * stride)) / (width - 1)
      const elevation = elevations[gy * stride * width + gx * stride]
      const normalized = (elevation - minElevation) / elevationRange
      const i = gy * gw + gx
      positions[i * 3] = worldX(lng)
      positions[i * 3 + 1] = elevation
      positions[i * 3 + 2] = -worldY(lat)
      // green→brown→gray elevation gradient
      colors[i * 3] = 0.18 + normalized * 0.34
      colors[i * 3 + 1] = 0.32 + normalized * 0.14
      colors[i * 3 + 2] = 0.14 + normalized * 0.16
    }
  }

  const indices = new Uint32Array((gw - 1) * (gh - 1) * 6)
  let idx = 0
  for (let gy = 0; gy < gh - 1; gy++) {
    for (let gx = 0; gx < gw - 1; gx++) {
      const a = gy * gw + gx
      const b = a + 1
      const c = a + gw
      const d = c + 1
      indices[idx++] = a
      indices[idx++] = c
      indices[idx++] = b
      indices[idx++] = b
      indices[idx++] = c
      indices[idx++] = d
    }
  }

  computeNormals(positions, indices, normals)
  return { positions, normals, colors, indices, width: gw, height: gh }
}

function computeNormals(positions: Float32Array, indices: Uint32Array, normals: Float32Array) {
  normals.fill(0)
  const v0 = [0, 0, 0]
  const v1 = [0, 0, 0]
  const v2 = [0, 0, 0]
  const e1 = [0, 0, 0]
  const e2 = [0, 0, 0]
  const n = [0, 0, 0]

  for (let i = 0; i < indices.length; i += 3) {
    const a = indices[i]
    const b = indices[i + 1]
    const c = indices[i + 2]
    v0[0] = positions[a * 3]; v0[1] = positions[a * 3 + 1]; v0[2] = positions[a * 3 + 2]
    v1[0] = positions[b * 3]; v1[1] = positions[b * 3 + 1]; v1[2] = positions[b * 3 + 2]
    v2[0] = positions[c * 3]; v2[1] = positions[c * 3 + 1]; v2[2] = positions[c * 3 + 2]
    e1[0] = v1[0] - v0[0]; e1[1] = v1[1] - v0[1]; e1[2] = v1[2] - v0[2]
    e2[0] = v2[0] - v0[0]; e2[1] = v2[1] - v0[1]; e2[2] = v2[2] - v0[2]
    n[0] = e1[1] * e2[2] - e1[2] * e2[1]
    n[1] = e1[2] * e2[0] - e1[0] * e2[2]
    n[2] = e1[0] * e2[1] - e1[1] * e2[0]
    normals[a * 3] += n[0]; normals[a * 3 + 1] += n[1]; normals[a * 3 + 2] += n[2]
    normals[b * 3] += n[0]; normals[b * 3 + 1] += n[1]; normals[b * 3 + 2] += n[2]
    normals[c * 3] += n[0]; normals[c * 3 + 1] += n[1]; normals[c * 3 + 2] += n[2]
  }

  for (let i = 0; i < normals.length; i += 3) {
    const len = Math.hypot(normals[i], normals[i + 1], normals[i + 2])
    if (len > 1e-9) {
      normals[i] /= len
      normals[i + 1] /= len
      normals[i + 2] /= len
    } else {
      normals[i] = 0; normals[i + 1] = 1; normals[i + 2] = 0
    }
  }
}
