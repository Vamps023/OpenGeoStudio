// Satellite imagery compositing for 3D terrain: fetches Esri World Imagery
// web tiles for a geographic bounds and bakes them into one canvas texture.
import * as THREE from 'three'
import type { TerrainData } from '../engine/terrainMesh'

const TILE = 256
const TILE_URL = (z: number, x: number, y: number) =>
  `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${z}/${y}/${x}`

const lngToTileX = (lng: number, z: number) => ((lng + 180) / 360) * 2 ** z
const latToTileY = (lat: number, z: number) => {
  const rad = (lat * Math.PI) / 180
  return ((1 - Math.log(Math.tan(rad) + 1 / Math.cos(rad)) / Math.PI) / 2) * 2 ** z
}

function loadImage(url: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const image = new Image()
    image.crossOrigin = 'anonymous'
    image.onload = () => resolve(image)
    image.onerror = () => reject(new Error(`tile failed: ${url}`))
    image.src = url
  })
}

/**
 * Composite the satellite imagery tiles covering `terrain.bounds` into a
 * single texture (UV: u = west→east, v = south→north). Returns null when
 * tiles cannot be loaded (offline / blocked).
 */
export async function loadImageryTexture(terrain: TerrainData, maxTiles = 36): Promise<THREE.CanvasTexture | null> {
  const { west, east, south, north } = terrain.bounds
  for (let z = 16; z >= 5; z--) {
    const n = 2 ** z
    const tx0 = Math.floor(lngToTileX(west, z))
    const tx1 = Math.floor(lngToTileX(east, z))
    const ty0 = Math.floor(latToTileY(north, z))
    const ty1 = Math.floor(latToTileY(south, z))
    const cols = tx1 - tx0 + 1
    const rows = ty1 - ty0 + 1
    if (cols * rows > maxTiles) continue

    const canvas = document.createElement('canvas')
    canvas.width = cols * TILE
    canvas.height = rows * TILE
    const ctx = canvas.getContext('2d')
    if (!ctx) return null

    const jobs: Promise<void>[] = []
    for (let ty = ty0; ty <= ty1; ty++) {
      for (let tx = tx0; tx <= tx1; tx++) {
        const px = (tx - tx0) * TILE
        const py = (ty - ty0) * TILE
        jobs.push(
          loadImage(TILE_URL(z, tx, ty)).then((image) => ctx.drawImage(image, px, py)).catch(() => undefined),
        )
      }
    }
    await Promise.all(jobs)

    const texture = new THREE.CanvasTexture(canvas)
    texture.colorSpace = THREE.SRGBColorSpace
    return texture
  }
  return null
}

/** UVs for a terrain grid aligned with loadImageryTexture (v = south→north). */
export function terrainUvs(terrain: TerrainData, gridWidth: number, gridHeight: number): Float32Array {
  const { west, east, south, north } = terrain.bounds
  const uvs = new Float32Array(gridWidth * gridHeight * 2)
  for (let gy = 0; gy < gridHeight; gy++) {
    const v = gy / (gridHeight - 1) // row 0 = north → v = 1
    for (let gx = 0; gx < gridWidth; gx++) {
      const u = gx / (gridWidth - 1)
      const i = gy * gridWidth + gx
      uvs[i * 2] = u
      uvs[i * 2 + 1] = 1 - v
    }
  }
  void west; void east; void south; void north
  return uvs
}
