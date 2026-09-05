// ─────────────────────────────────────────────────────────────────────
// PCG building generation: converts OSM footprints (engine/osmBuildings)
// into varied, deterministic 3D buildings — per-floor facades with
// windows, per-building color palettes, parapets, rooftop units and
// roof shapes. Styles are presets; every random draw comes from a seed
// derived from the building id + global seed, so the same input and
// settings always reproduce the same model.
//
// All "assets" (window glass tones, facade palettes, roof units) are
// generated procedurally — deliberately no external files, so nothing
// to license and nothing to ship. The generator reads only MeshData
// inputs and is node-testable; swapping in real modular glTF parts
// later means replacing this layer, not the callers.
// ─────────────────────────────────────────────────────────────────────
import type { MeshData } from './mesh'
import type { OsmBuildingData } from './osmBuildings'
import { triangulatePolygon } from './osmBuildings'

export type PcgStyle = 'residential' | 'commercial' | 'industrial' | 'generic'
export type PcgDetail = 'low' | 'medium' | 'high'

/** Generation settings persisted on the project. */
export interface PcgProjectConfig {
  mode: 'extrusion' | 'pcg'
  style: PcgStyle
  seed: number
  detail: PcgDetail
  /** per-building regeneration counters (Regenerate button bumps the hash) */
  overrides: Record<string, number>
}

export const DEFAULT_PCG_CONFIG: PcgProjectConfig = {
  mode: 'pcg',
  style: 'generic',
  seed: 1,
  detail: 'medium',
  overrides: {},
}

export interface PcgParams {
  style: PcgStyle
  seed: number
  detail: PcgDetail
  /** per-building regeneration offset (Regenerate button) */
  override?: number
}

// deterministic PRNG (mulberry32)
function mulberry32(seed: number): () => number {
  let a = seed | 0
  return () => {
    a = (a + 0x6d2b79f5) | 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

/** stable string hash → 32-bit seed component */
export function hashId(id: string): number {
  let h = 2166136261
  for (let i = 0; i < id.length; i++) {
    h ^= id.charCodeAt(i)
    h = Math.imul(h, 16777619)
  }
  return h >>> 0
}

interface StyleConfig {
  wallPalettes: [number, number, number][]
  glass: [number, number, number]
  windowDensity: number      // 0..1 share of facade cells that get windows
  parapetChance: number
  rooftopUnits: [number, number] // min/max count
  bandColor: [number, number, number] | null // floor separation band
}

const GLASS_LIGHT: [number, number, number] = [0.55, 0.68, 0.78]
const GLASS_DARK: [number, number, number] = [0.22, 0.3, 0.38]

const STYLES: Record<PcgStyle, StyleConfig> = {
  residential: {
    wallPalettes: [
      [0.85, 0.78, 0.66], [0.8, 0.72, 0.62], [0.72, 0.76, 0.68],
      [0.82, 0.7, 0.6], [0.78, 0.74, 0.8],
    ],
    glass: GLASS_LIGHT,
    windowDensity: 0.55,
    parapetChance: 0.5,
    rooftopUnits: [0, 1],
    bandColor: [0.6, 0.56, 0.5],
  },
  commercial: {
    wallPalettes: [
      [0.72, 0.73, 0.76], [0.62, 0.66, 0.7], [0.55, 0.58, 0.62], [0.78, 0.76, 0.72],
    ],
    glass: GLASS_DARK,
    windowDensity: 0.75,
    parapetChance: 0.8,
    rooftopUnits: [1, 3],
    bandColor: null,
  },
  industrial: {
    wallPalettes: [
      [0.62, 0.6, 0.56], [0.55, 0.55, 0.52], [0.68, 0.64, 0.58],
    ],
    glass: GLASS_DARK,
    windowDensity: 0.25,
    parapetChance: 0.3,
    rooftopUnits: [2, 4],
    bandColor: [0.45, 0.44, 0.42],
  },
  generic: {
    wallPalettes: [
      [0.78, 0.75, 0.7], [0.68, 0.68, 0.66], [0.73, 0.7, 0.64],
    ],
    glass: GLASS_LIGHT,
    windowDensity: 0.5,
    parapetChance: 0.5,
    rooftopUnits: [0, 2],
    bandColor: [0.58, 0.55, 0.5],
  },
}

const ROOF: [number, number, number] = [0.38, 0.37, 0.36]
const ROOF_UNIT: [number, number, number] = [0.52, 0.52, 0.54]
const PARAPET: [number, number, number] = [0.66, 0.64, 0.6]

const FLOOR_HEIGHT = 3.2
const WINDOW_HEIGHT = 1.4
const WINDOW_SILL = 0.9

class MeshBuilder {
  positions: number[] = []
  colors: number[] = []

  quad(p0: [number, number, number], p1: [number, number, number], p2: [number, number, number], p3: [number, number, number], color: [number, number, number]) {
    this.tri(p0, p1, p2, color)
    this.tri(p0, p2, p3, color)
  }

  tri(p0: [number, number, number], p1: [number, number, number], p2: [number, number, number], color: [number, number, number]) {
    for (const p of [p0, p1, p2]) {
      this.positions.push(p[0], p[1], p[2])
      this.colors.push(color[0], color[1], color[2])
    }
  }

  build(): MeshData {
    return {
      positions: new Float32Array(this.positions),
      colors: new Float32Array(this.colors),
      indices: new Uint32Array(this.positions.length / 3),
    }
  }
}

type V3 = [number, number, number]

/** Generate a varied building mesh from an OSM footprint. Deterministic for
 *  (building.id, params). Returns null for degenerate footprints. */
export function generatePcgBuildingMesh(
  building: OsmBuildingData,
  baseZ: number,
  params: PcgParams,
): MeshData | null {
  const ring = building.ring
  if (ring.length < 3) return null
  const triangles = triangulatePolygon(ring)
  if (triangles.length === 0) return null

  const style = STYLES[params.style] ?? STYLES.generic
  const rng = mulberry32((hashId(building.id) ^ (params.seed * 2654435761)) >>> 0)
  const overrideRng = params.override ? mulberry32((hashId(building.id) + params.override * 7919) >>> 0) : null

  const palette = style.wallPalettes[Math.floor(rng() * style.wallPalettes.length) % style.wallPalettes.length]
  // gentle per-building tint variation
  const tint = 0.92 + rng() * 0.16
  const wall: [number, number, number] = [palette[0] * tint, palette[1] * tint, palette[2] * tint]
  const glass = style.glass
  const detail = params.detail

  // facade orientation override: Regenerate shuffles window pattern + tint accents
  const facadeRng = overrideRng ?? rng

  const builder = new MeshBuilder()
  const top = baseZ + building.height
  const floors = Math.max(1, Math.round(building.height / FLOOR_HEIGHT))

  // outward edge normals (ring is closed; normal = rotate edge dir by -90° in ground plane)
  const normal = (i: number) => {
    const a = ring[i]
    const b = ring[(i + 1) % ring.length]
    const dx = b.x - a.x
    const dy = b.y - a.y
    const len = Math.hypot(dx, dy) || 1
    return { x: dy / len, y: -dx / len }
  }

  // ── walls + windows, per floor per edge ──
  for (let i = 0; i < ring.length; i++) {
    const a = ring[i]
    const b = ring[(i + 1) % ring.length]
    const edgeLength = Math.hypot(b.x - a.x, b.y - a.y)
    if (edgeLength < 0.2) continue
    const n = normal(i)
    const outX = n.x * 0.02
    const outY = n.y * 0.02

    for (let f = 0; f < floors; f++) {
      const fz0 = baseZ + (building.height * f) / floors
      const fz1 = baseZ + (building.height * (f + 1)) / floors
      builder.quad(
        [a.x, fz0, -a.y], [b.x, fz0, -b.y], [b.x, fz1, -b.y], [a.x, fz1, -a.y],
        wall,
      )

      if (detail === 'low') continue
      // windows: cells across the edge within this floor's band
      const cellWidth = detail === 'high' ? 1.6 : 2.4
      const cells = Math.floor(edgeLength / cellWidth)
      const z0 = fz0 + WINDOW_SILL
      const z1 = Math.min(fz1 - 0.3, z0 + WINDOW_HEIGHT)
      if (z1 - z0 < 0.4) continue
      for (let c = 0; c < cells; c++) {
        if (facadeRng() > style.windowDensity) continue
        const t0 = (c + 0.18) / cells
        const t1 = (c + 0.82) / cells
        const ax = a.x + (b.x - a.x) * t0
        const ay = a.y + (b.y - a.y) * t0
        const bx = a.x + (b.x - a.x) * t1
        const by = a.y + (b.y - a.y) * t1
        const w0: V3 = [ax + outX, z0, -(ay + outY)]
        const w1: V3 = [bx + outX, z0, -(by + outY)]
        const w2: V3 = [bx + outX, z1, -(by + outY)]
        const w3: V3 = [ax + outX, z1, -(ay + outY)]
        builder.quad(w0, w1, w2, w3, glass)
      }
    }
  }

  // ── flat roof + parapet + rooftop units ──
  for (const [i0, i1, i2] of triangles) {
    builder.tri(
      [ring[i0].x, top, -ring[i0].y],
      [ring[i2].x, top, -ring[i2].y],
      [ring[i1].x, top, -ring[i1].y],
      ROOF,
    )
  }

  const wantsParapet = facadeRng() < style.parapetChance
  if (wantsParapet) {
    const ph = Math.min(0.9, 0.4 + facadeRng() * 0.5)
    for (let i = 0; i < ring.length; i++) {
      const a = ring[i]
      const b = ring[(i + 1) % ring.length]
      const n = normal(i)
      const inX = -n.x * 0.12
      const inY = -n.y * 0.12
      builder.quad(
        [a.x, top, -a.y], [b.x, top, -b.y],
        [b.x + inX, top + ph, -(b.y + inY)], [a.x + inX, top + ph, -(a.y + inY)],
        PARAPET,
      )
    }
  }

  if (detail !== 'low') {
    const [minUnits, maxUnits] = style.rooftopUnits
    const unitCount = minUnits + Math.floor(facadeRng() * (maxUnits - minUnits + 1))
    let minX = Infinity
    let maxX = -Infinity
    let minY = Infinity
    let maxY = -Infinity
    for (const p of ring) {
      minX = Math.min(minX, p.x)
      maxX = Math.max(maxX, p.x)
      minY = Math.min(minY, p.y)
      maxY = Math.max(maxY, p.y)
    }
    const roofZ = top + (wantsParapet ? 0.5 : 0)
    for (let u = 0; u < unitCount && maxX - minX > 4 && maxY - minY > 4; u++) {
      const cx = minX + 1.5 + facadeRng() * (maxX - minX - 3)
      const cy = minY + 1.5 + facadeRng() * (maxY - minY - 3)
      const w = 0.8 + facadeRng() * 1.2
      const d = 0.8 + facadeRng() * 1.2
      const h = 0.6 + facadeRng() * 0.8
      const c00: V3 = [cx - w, roofZ, -(cy - d)]
      const c10: V3 = [cx + w, roofZ, -(cy - d)]
      const c11: V3 = [cx + w, roofZ, -(cy + d)]
      const c01: V3 = [cx - w, roofZ, -(cy + d)]
      const t00: V3 = [cx - w, roofZ + h, -(cy - d)]
      const t10: V3 = [cx + w, roofZ + h, -(cy - d)]
      const t11: V3 = [cx + w, roofZ + h, -(cy + d)]
      const t01: V3 = [cx - w, roofZ + h, -(cy + d)]
      builder.quad(c00, c10, t10, t00, ROOF_UNIT)
      builder.quad(c10, c11, t11, t10, ROOF_UNIT)
      builder.quad(c11, c01, t01, t11, ROOF_UNIT)
      builder.quad(c01, c00, t00, t01, ROOF_UNIT)
      builder.quad(t00, t10, t11, t01, ROOF_UNIT)
    }
  }

  // ── gable ridge when tagged ──
  if (building.roofShape === 'gabled' || building.roofShape === 'hipped') {
    let minX = Infinity
    let maxX = -Infinity
    let minY = Infinity
    let maxY = -Infinity
    for (const p of ring) {
      minX = Math.min(minX, p.x)
      maxX = Math.max(maxX, p.x)
      minY = Math.min(minY, p.y)
      maxY = Math.max(maxY, p.y)
    }
    const spanX = maxX - minX
    const spanY = maxY - minY
    if (spanX > 1 && spanY > 1) {
      const ridgeHeight = Math.min(4, building.height * 0.3)
      const alongX = spanX >= spanY
      const r0 = alongX ? { x: minX, y: (minY + maxY) / 2 } : { x: (minX + maxX) / 2, y: minY }
      const r1 = alongX ? { x: maxX, y: (minY + maxY) / 2 } : { x: (minX + maxX) / 2, y: maxY }
      const r0v: V3 = [r0.x, top + ridgeHeight, -r0.y]
      const r1v: V3 = [r1.x, top + ridgeHeight, -r1.y]
      const gableColor: [number, number, number] = [wall[0] * 0.8, wall[1] * 0.8, wall[2] * 0.8]
      const e0v: V3 = [minX, top, -minY]
      const e1v: V3 = [maxX, top, -minY]
      const f0v: V3 = [minX, top, -maxY]
      const f1v: V3 = [maxX, top, -maxY]
      if (alongX) {
        builder.quad(e0v, e1v, r1v, r0v, gableColor)
        builder.quad(f1v, f0v, r0v, r1v, gableColor)
        builder.tri(e0v, r0v, f0v, gableColor)
        builder.tri(e1v, f1v, r1v, gableColor)
      } else {
        builder.quad(e0v, f0v, r0v, r0v, gableColor)
        builder.quad(e1v, r1v, f1v, e1v, gableColor)
      }
    }
  }

  const mesh = builder.build()
  return mesh.positions.length ? mesh : null
}
