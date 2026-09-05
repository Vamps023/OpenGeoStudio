// ─────────────────────────────────────────────────────────────────────
// Procedural asphalt PBR material (3D Studio road surfaces).
//
// Generates a seamless, tileable asphalt texture set (albedo, roughness,
// normal) on canvas — no external assets, so no licensing questions and
// it works offline. The albedo is authored near-white because three.js
// multiplies map × vertexColor: the road mesh's vertex colors (asphalt
// grey + lane markings) provide the actual tones, the texture adds
// aggregate speckle, mottling and wear. UVs come from the mesh builders
// at a constant 6 m per tile, so texel density never stretches.
// ─────────────────────────────────────────────────────────────────────
import * as THREE from 'three'

export type RoadWear = 'fresh' | 'normal' | 'worn'

const SIZE = 512

/** deterministic PRNG so every session renders identical roads */
function mulberry32(seed: number): () => number {
  let a = seed
  return () => {
    a |= 0
    a = (a + 0x6d2b79f5) | 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

/** periodic value noise wrapping at the canvas edge — cellsX/cellsY set the
 *  anisotropy (unequal values give streaks along the road direction) */
function valueNoise(cellsX: number, cellsY: number, rng: () => number): Float32Array {
  const lattice = new Float32Array(cellsX * cellsY)
  for (let i = 0; i < lattice.length; i++) lattice[i] = rng()
  const out = new Float32Array(SIZE * SIZE)
  const cellW = SIZE / cellsX
  const cellH = SIZE / cellsY
  const smooth = (t: number) => t * t * (3 - 2 * t)
  for (let y = 0; y < SIZE; y++) {
    const gy = y / cellH
    const y0 = Math.floor(gy)
    const fy = smooth(gy - y0)
    const ya = y0 % cellsY
    const yb = (y0 + 1) % cellsY
    for (let x = 0; x < SIZE; x++) {
      const gx = x / cellW
      const x0 = Math.floor(gx)
      const fx = smooth(gx - x0)
      const xa = x0 % cellsX
      const xb = (x0 + 1) % cellsX
      const v00 = lattice[ya * cellsX + xa]
      const v10 = lattice[ya * cellsX + xb]
      const v01 = lattice[yb * cellsX + xa]
      const v11 = lattice[yb * cellsX + xb]
      out[y * SIZE + x] =
        (v00 * (1 - fx) + v10 * fx) * (1 - fy) + (v01 * (1 - fx) + v11 * fx) * fy
    }
  }
  return out
}

function grayCanvas(gray: Float32Array): HTMLCanvasElement {
  const canvas = document.createElement('canvas')
  canvas.width = SIZE
  canvas.height = SIZE
  const ctx = canvas.getContext('2d')!
  const image = ctx.createImageData(SIZE, SIZE)
  for (let i = 0; i < gray.length; i++) {
    const v = Math.max(0, Math.min(255, Math.round(gray[i] * 255)))
    image.data[i * 4] = v
    image.data[i * 4 + 1] = v
    image.data[i * 4 + 2] = v
    image.data[i * 4 + 3] = 255
  }
  ctx.putImageData(image, 0, 0)
  return canvas
}

/** tangent-space normal map from a height field (central differences) */
function normalCanvas(height: Float32Array, strength: number): HTMLCanvasElement {
  const canvas = document.createElement('canvas')
  canvas.width = SIZE
  canvas.height = SIZE
  const ctx = canvas.getContext('2d')!
  const image = ctx.createImageData(SIZE, SIZE)
  const at = (x: number, y: number) => height[((y + SIZE) % SIZE) * SIZE + ((x + SIZE) % SIZE)]
  for (let y = 0; y < SIZE; y++) {
    for (let x = 0; x < SIZE; x++) {
      const dx = (at(x + 1, y) - at(x - 1, y)) * strength
      const dy = (at(x, y + 1) - at(x, y - 1)) * strength
      // normalize (dx, dy, 1)
      const len = Math.sqrt(dx * dx + dy * dy + 1)
      const i = (y * SIZE + x) * 4
      image.data[i] = Math.round(((-dx / len) * 0.5 + 0.5) * 255)
      image.data[i + 1] = Math.round(((-dy / len) * 0.5 + 0.5) * 255)
      image.data[i + 2] = Math.round((1 / len * 0.5 + 0.5) * 255)
      image.data[i + 3] = 255
    }
  }
  ctx.putImageData(image, 0, 0)
  return canvas
}

interface AsphaltSet {
  map: HTMLCanvasElement
  roughnessMap: HTMLCanvasElement
  normalMap: HTMLCanvasElement
}

const WEAR_CONFIG: Record<RoadWear, { base: number; mottle: number; speckle: number; crack: number; rough: number; roughAmp: number }> = {
  fresh: { base: 0.99, mottle: 0.03, speckle: 0.04, crack: 0.0, rough: 0.78, roughAmp: 0.06 },
  normal: { base: 0.96, mottle: 0.05, speckle: 0.05, crack: 0.03, rough: 0.86, roughAmp: 0.08 },
  worn: { base: 0.93, mottle: 0.07, speckle: 0.06, crack: 0.07, rough: 0.92, roughAmp: 0.09 },
}

function buildAsphaltSet(wear: RoadWear): AsphaltSet {
  const cfg = WEAR_CONFIG[wear]
  const rng = mulberry32(wear === 'fresh' ? 101 : wear === 'normal' ? 202 : 303)
  const mottle = valueNoise(6, 6, rng)
  const streaks = valueNoise(28, 5, rng) // tire-polish streaks along the road
  const aggregate = valueNoise(96, 96, rng)
  const cracks = valueNoise(14, 14, rng)
  const fine = valueNoise(192, 192, rng)

  const albedo = new Float32Array(SIZE * SIZE)
  const rough = new Float32Array(SIZE * SIZE)
  const height = new Float32Array(SIZE * SIZE)
  for (let i = 0; i < albedo.length; i++) {
    // near-white albedo: vertex colors carry the tone, this adds variation
    let v = cfg.base
    v += (mottle[i] - 0.5) * 2 * cfg.mottle
    v += (streaks[i] - 0.5) * 2 * cfg.mottle * 0.6
    v += (aggregate[i] - 0.5) * 2 * cfg.speckle
    v += (fine[i] - 0.5) * 2 * cfg.speckle * 0.5
    if (cfg.crack > 0 && cracks[i] < 0.09) v -= cfg.crack // dark fatigue patches
    albedo[i] = v
    rough[i] = cfg.rough + (aggregate[i] - 0.5) * 2 * cfg.roughAmp
    height[i] = aggregate[i] * 0.7 + fine[i] * 0.3
  }
  return {
    map: grayCanvas(albedo),
    roughnessMap: grayCanvas(rough),
    normalMap: normalCanvas(height, 2.2),
  }
}

function toTexture(canvas: HTMLCanvasElement, srgb: boolean): THREE.CanvasTexture {
  const texture = new THREE.CanvasTexture(canvas)
  texture.wrapS = THREE.RepeatWrapping
  texture.wrapT = THREE.RepeatWrapping
  texture.anisotropy = 8
  if (srgb) texture.colorSpace = THREE.SRGBColorSpace
  return texture
}

export interface AsphaltMaterialTextures {
  map: THREE.CanvasTexture
  roughnessMap: THREE.CanvasTexture
  normalMap: THREE.CanvasTexture
}

const cache = new Map<RoadWear, AsphaltMaterialTextures>()

/** Asphalt texture set for a wear variant (cached — generation is ~ms but free to reuse). */
export function getAsphaltTextures(wear: RoadWear): AsphaltMaterialTextures {
  const hit = cache.get(wear)
  if (hit) return hit
  const set = buildAsphaltSet(wear)
  const textures: AsphaltMaterialTextures = {
    map: toTexture(set.map, true),
    roughnessMap: toTexture(set.roughnessMap, false),
    normalMap: toTexture(set.normalMap, false),
  }
  cache.set(wear, textures)
  return textures
}
