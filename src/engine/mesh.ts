import { samplePath, samplePathRange, evaluatePath } from './geometry'
import { laneLayout, LANE_TYPE_META } from './lanes'
import type { PathSample } from './geometry'
import type { FittedPath } from './types'
import type { LaneDef, LaneSectionDef } from './laneTypes'
import type { LaneStrip } from './laneLayout'
import type { LaneTaper, RailwayConfig } from '../state/store'

export interface MeshData {
  positions: Float32Array
  colors: Float32Array
  indices: Uint32Array
  /** optional per-vertex UVs (u = lateral meters, v = station meters, ÷ 6) for textured materials */
  uvs?: Float32Array
}

/** Semantic surface type — controls which material the renderer applies.
 *  Pavement gets the dark asphalt PBR set; markings get a plain vertex-colored
 *  material so white/yellow/green strips stay clean and are not affected by
 *  the asphalt texture. */
export type SurfaceType = 'pavement' | 'marking' | 'shoulder' | 'sidewalk' | 'rail' | 'misc'

/** A mesh tagged with its semantic surface type so the renderer can pick the
 *  right material. Returned by the road/junction mesh builders. */
export interface TypedMesh {
  mesh: MeshData
  surface: SurfaceType
}

/** A road/junction build result: pavement (asphalt-textured) + markings
 *  (plain vertex-colored) as separate meshes so the asphalt texture never
 *  contaminates lane markings. */
export interface RoadMeshResult {
  pavement: MeshData | null
  markings: MeshData | null
}

type Rgb = [number, number, number]

const ASPHALT_A: Rgb = [0.16, 0.18, 0.21]
const ASPHALT_B: Rgb = [0.19, 0.21, 0.25]
const WHITE: Rgb = [0.88, 0.9, 0.93]
const YELLOW: Rgb = [0.91, 0.7, 0.1]
const SIDEWALK: Rgb = [0.64, 0.65, 0.66]
const SHOULDER: Rgb = [0.36, 0.40, 0.46]
const SOFT_SHOULDER: Rgb = [0.55, 0.62, 0.50]
const MEDIAN: Rgb = [0.40, 0.45, 0.50]
const BIKE: Rgb = [0.85, 0.78, 0.55]
const BUS: Rgb = [0.78, 0.50, 0.40]
const CURB: Rgb = [0.55, 0.65, 0.65]
const PARKING: Rgb = [0.70, 0.40, 0.40]
const GRASS: Rgb = [0.40, 0.55, 0.35]
const DIRT: Rgb = [0.55, 0.42, 0.32]
const RUNWAY: Rgb = [0.20, 0.22, 0.25]
const STEEL: Rgb = [0.62, 0.64, 0.68]
const CENTER_GREEN: Rgb = [0.2, 0.85, 0.35]
const BALLAST: Rgb = [0.44, 0.41, 0.37]
const SLEEPER: Rgb = [0.35, 0.28, 0.20]

function hexToRgb(hex: string): Rgb {
  const m = hex.replace('#', '').match(/.{2}/g)
  if (!m) return ASPHALT_A
  return [parseInt(m[0], 16) / 255, parseInt(m[1], 16) / 255, parseInt(m[2], 16) / 255]
}

function colorForLaneType(type: LaneDef['type']): Rgb {
  switch (type) {
    case 'travel':        return ASPHALT_A
    case 'paved_major':   return ASPHALT_B
    case 'shoulder':      return SHOULDER
    case 'hard_shoulder': return SHOULDER
    case 'soft_shoulder': return SOFT_SHOULDER
    case 'curb':          return CURB
    case 'sidewalk':      return SIDEWALK
    case 'bike':          return BIKE
    case 'bus':           return BUS
    case 'median':        return MEDIAN
    case 'parking':       return PARKING
    case 'embankment':    return GRASS
    case 'ditch':         return [0.40, 0.50, 0.60]
    case 'barrier':       return [0.18, 0.20, 0.25]
    case 'land':          return GRASS
    case 'lane_out':      return YELLOW
    case 'runway':        return RUNWAY
    case 'taxiway':       return [0.26, 0.30, 0.36]
    default:              return ASPHALT_A
  }
}

interface StripSpec {
  inner: number
  outer: number
  color: Rgb
  height?: number
  /** semantic surface type — defaults to 'pavement'. Markings (thin white/
   *  yellow / green strips) are tagged 'marking' so the renderer gives them
   *  a plain vertex-colored material instead of the asphalt texture. */
  surface?: SurfaceType
}

export type { StripSpec }
export type { Rgb }

/** Classify a strip as marking vs pavement based on its color.
 *  Markings are the thin white/yellow/green strips (edge lines, center
 *  line, lane dividers). Everything else is pavement (asphalt, shoulder,
 *  sidewalk, median, etc.). */
function isMarkingColor(color: Rgb): boolean {
  // white markings
  if (color[0] > 0.8 && color[1] > 0.8 && color[2] > 0.8) return true
  // yellow markings
  if (color[0] > 0.85 && color[1] > 0.6 && color[2] < 0.2) return true
  // green center line (SCANeR style)
  if (color[1] > 0.7 && color[0] < 0.4 && color[2] < 0.5) return true
  return false
}

type ElevationSampler = (station: number) => number

function isLaneDefArray(arr: unknown): arr is LaneDef[] {
  return Array.isArray(arr) && (arr.length === 0 || typeof (arr[0] as LaneDef)?.width === 'number')
}

export function buildRoadMesh(
  path: FittedPath | null,
  section: LaneSectionDef,
  ds = 1,
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
): RoadMeshResult {
  if (!path || path.elements.length === 0) return { pavement: null, markings: null }
  return buildRoadMeshFromSamples(samplePath(path, ds), section, elevation, banking)
}

export function buildRoadMeshRange(
  path: FittedPath,
  section: LaneSectionDef,
  sStart: number,
  sEnd: number,
  ds = 1,
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
  tapers?: LaneTaper[],
): RoadMeshResult {
  return buildRoadMeshFromSamples(samplePathRange(path, sStart, sEnd, ds), section, elevation, banking, tapers, path.length)
}

export function buildRoadMeshFromSamples(
  samples: PathSample[],
  section: LaneSectionDef,
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
  tapers?: LaneTaper[],
  roadLength = 0,
): RoadMeshResult {
  if (samples.length < 2) return { pavement: null, markings: null }
  const layout = laneLayout(section)
  const strips: StripSpec[] = []

  // Determine if we have rich lane defs or just width arrays
  const leftAreDefs = isLaneDefArray(section.left)
  const rightAreDefs = isLaneDefArray(section.right)

  // Lane strips (colored by lane type if available) — these are pavement
  if (leftAreDefs) {
    section.left.forEach((lane, i) => {
      const strip = layout.left[i]
      const baseColor = colorForLaneType(lane.type)
      const color: Rgb = i % 2 ? [baseColor[0] + 0.03, baseColor[1] + 0.03, baseColor[2] + 0.04] : baseColor
      strips.push({ ...strip, color, height: lane.borderLeftHeight ?? 0, surface: 'pavement' })
    })
  } else {
    layout.left.forEach((strip, i) => strips.push({ ...strip, color: i % 2 ? ASPHALT_B : ASPHALT_A, surface: 'pavement' }))
  }
  if (rightAreDefs) {
    section.right.forEach((lane, i) => {
      const strip = layout.right[i]
      const baseColor = colorForLaneType(lane.type)
      const color: Rgb = i % 2 ? [baseColor[0] + 0.03, baseColor[1] + 0.03, baseColor[2] + 0.04] : baseColor
      strips.push({ ...strip, color, height: lane.borderRightHeight ?? 0, surface: 'pavement' })
    })
  } else {
    layout.right.forEach((strip, i) => strips.push({ ...strip, color: i % 2 ? ASPHALT_B : ASPHALT_A, surface: 'pavement' }))
  }

  // Express-lane tapers: per-sample strip layout for tapered lanes
  let layoutAt: ((s: number) => { left: LaneStrip[]; right: LaneStrip[] }) | undefined
  if (tapers && tapers.length > 0 && roadLength > 0) {
    const widths = {
      left: section.left.map((l) => l.width),
      right: section.right.map((l) => l.width),
    }
    layoutAt = (s: number) => {
      const scaled = {
        left: widths.left.slice(),
        right: widths.right.slice(),
      }
      for (const taper of tapers) {
        // express lane: absent before startS / after endS, growing/shrinking
        // smoothly over the taper length (SPEED_LIMIT × REACTION_TIME)
        const factor = taper.mode === 'in'
          ? (() => {
              const start = taper.startS ?? 0
              return s <= start ? 0 : Math.max(0, Math.min(1, (s - start) / Math.max(0.01, taper.length)))
            })()
          : (() => {
              const end = taper.endS ?? roadLength
              return s >= end ? 0 : Math.max(0, Math.min(1, (end - s) / Math.max(0.01, taper.length)))
            })()
        const list = scaled[taper.side]
        if (taper.index < list.length) list[taper.index] = widths[taper.side][taper.index] * factor
      }
      return taperedLayout(scaled.left, scaled.right)
    }
  }

  // Edge lines (white) at outermost borders — markings
  const boundaryMarkings = buildLaneMarkings(samples, section, layoutAt ?? (() => layout), elevation, banking)
  // SCANeR default style: green centre line between the two traffic directions — marking
  if (layout.totalLeft > 0 && layout.totalRight > 0) strips.push({ inner: -0.075, outer: 0.075, color: CENTER_GREEN, height: 0.025, surface: 'marking' })

  // Lane dividers (per-lane markings) — markings
  const laneStripCount = section.left.length + section.right.length
  const result = buildStripsSplit(samples, strips, elevation, banking, layoutAt ? { layoutAt, laneStripCount } : undefined)
  result.markings = mergeMarkings(result.markings, boundaryMarkings)
  return result
}

/** Lane strips recomputed with per-lane (tapered) widths. */
function taperedLayout(leftWidths: number[], rightWidths: number[]): { left: LaneStrip[]; right: LaneStrip[] } {
  const build = (widths: number[], sign: 1 | -1): LaneStrip[] => {
    const strips: LaneStrip[] = []
    let acc = 0
    for (const width of widths) {
      strips.push({ inner: acc * sign, outer: (acc + width) * sign })
      acc += width
    }
    return strips
  }
  return { left: build(leftWidths, 1), right: build(rightWidths, -1) }
}

function mergeMarkings(a: MeshData | null, b: MeshData | null): MeshData | null {
  if (!a) return b
  if (!b) return a
  const positions = new Float32Array(a.positions.length + b.positions.length)
  positions.set(a.positions)
  positions.set(b.positions, a.positions.length)
  const colors = new Float32Array(a.colors.length + b.colors.length)
  colors.set(a.colors)
  colors.set(b.colors, a.colors.length)
  const indices = new Uint32Array(a.indices.length + b.indices.length)
  indices.set(a.indices)
  indices.set(b.indices.map((i) => i + a.positions.length / 3), a.indices.length)
  return { positions, colors, indices }
}

function buildLaneMarkings(
  samples: PathSample[],
  section: LaneSectionDef,
  layoutAt: (s: number) => { left: LaneStrip[]; right: LaneStrip[] },
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
): MeshData | null {
  const positions: number[] = []
  const colors: number[] = []
  const indices: number[] = []
  const safeLength = (value: number | undefined, fallback: number, min: number, max: number) =>
    Number.isFinite(value) && value! > 0 ? Math.max(min, Math.min(max, value!)) : fallback
  const span = samples[samples.length - 1].s - samples[0].s
  const interpolate = (a: PathSample, b: PathSample, s: number): PathSample => {
    const t = (s - a.s) / (b.s - a.s)
    const turn = Math.atan2(Math.sin(b.heading - a.heading), Math.cos(b.heading - a.heading))
    return {
      s, x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t,
      heading: a.heading + turn * t,
      z: a.z !== undefined && b.z !== undefined ? a.z + (b.z - a.z) * t : undefined,
    }
  }
  for (const side of ['left', 'right'] as const) {
    const sign = side === 'left' ? 1 : -1
    section[side].forEach((lane, laneIndex) => {
      const marking = lane.marking ?? 'solid'
      if (marking === 'none') return
      const style = lane.markingStyle ?? {}
      const width = safeLength(style.width, 0.15, 0.01, 2)
      const period = Math.max(safeLength(style.totalLength, 9, 0.1, 1e6), span / 20000)
      const dot = Math.min(period, safeLength(style.dotLength, 3, 0.01, 1e6))
      const patterns = marking === 'double-solid' ? [false, false]
        : marking === 'solid-dashed' ? [false, true]
          : marking === 'dashed-solid' ? [true, false] : [marking === 'dashed']
      patterns.forEach((dashed, stripeIndex) => {
        const emit = (a: PathSample, b: PathSample) => {
          if (!(b.s - a.s > 1e-8)) return
          const corners: number[] = []
          for (const sample of [a, b]) {
            const strip = layoutAt(sample.s)[side][laneIndex]
            const laneWidth = Math.abs(strip.outer - strip.inner)
            if (!Number.isFinite(laneWidth)) return
            const inset = Math.min(0.05, laneWidth / 10)
            const lineWidth = Math.min(width, (laneWidth - inset) / (patterns.length === 2 ? 3 : 1))
            const center = strip.outer - sign * (inset + lineWidth / 2 + (patterns.length - 1 - stripeIndex) * 2 * lineWidth)
            const height = (sample.z ?? elevation?.(sample.s) ?? 0)
              + (side === 'left' ? lane.borderLeftHeight ?? 0 : lane.borderRightHeight ?? 0) + 0.025
            const bank = Math.tan(banking?.(sample.s) ?? 0)
            for (const offset of [center - lineWidth / 2, center + lineWidth / 2]) {
              corners.push(Math.fround(sample.x - Math.sin(sample.heading) * offset),
                Math.fround(height + bank * offset), Math.fround(-(sample.y + Math.cos(sample.heading) * offset)))
            }
          }
          if (!corners.every(Number.isFinite)) return
          const triangles: number[] = []
          for (const [a, b, c] of [[0, 1, 2], [1, 3, 2]]) {
            const up = (corners[c * 3] - corners[a * 3]) * (corners[b * 3 + 2] - corners[a * 3 + 2])
              - (corners[b * 3] - corners[a * 3]) * (corners[c * 3 + 2] - corners[a * 3 + 2])
            if (Math.abs(up) <= 1e-12) continue
            triangles.push(...(up < 0 ? [a, c, b] : [a, b, c]))
          }
          if (!triangles.length) return
          const base = positions.length / 3
          positions.push(...corners)
          for (let v = 0; v < 4; v++) colors.push(...WHITE)
          indices.push(...triangles.map((i) => i + base))
        }
        for (let i = 1; i < samples.length; i++) {
          const a = samples[i - 1], b = samples[i]
          if (!(b.s > a.s) || !Number.isFinite(a.s) || !Number.isFinite(b.s)) continue
          if (!dashed || dot >= period) {
            emit(a, b)
            continue
          }
          const first = Math.floor(a.s / period)
          const last = Math.floor(b.s / period)
          for (let cycle = first; cycle <= last; cycle++) {
            const start = Math.max(a.s, cycle * period)
            const end = Math.min(b.s, cycle * period + dot)
            if (end - start > 1e-8) emit(interpolate(a, b, start), interpolate(a, b, end))
          }
        }
      })
    })
  }
  return indices.length ? { positions: new Float32Array(positions), colors: new Float32Array(colors), indices: new Uint32Array(indices) } : null
}

export function buildConnectingRoadMesh(samples: PathSample[], laneCount: number, laneWidth: number): RoadMeshResult {
  if (samples.length < 2 || laneCount < 1) return { pavement: null, markings: null }
  const total = laneCount * laneWidth
  const strips: StripSpec[] = []
  for (let lane = 0; lane < laneCount; lane++) {
    strips.push({
      inner: -total / 2 + lane * laneWidth,
      outer: -total / 2 + (lane + 1) * laneWidth,
      color: lane % 2 ? ASPHALT_B : ASPHALT_A,
      height: 0.015,
      surface: 'pavement',
    })
  }
  for (let lane = 1; lane < laneCount; lane++) {
    const offset = -total / 2 + lane * laneWidth
    strips.push({ inner: offset - 0.06, outer: offset + 0.06, color: WHITE, height: 0.04, surface: 'marking' })
  }
  return buildStripsSplit(samples, strips)
}

/**
 * Railway track mesh (Train section): ballast + two steel rails as
 * along-track strips (banking-aware, rail centers at ±(gauge/2 + railSize/2)
 * like the Track Mesh Builder), plus sleeper quads across the track at the
 * configured spacing.
 */
export function buildRailwayMesh(
  path: FittedPath,
  railway: RailwayConfig,
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
): MeshData | null {
  if (!path || path.elements.length === 0) return null
  const { gauge, railSize, trackbedWidth, sleeperSpacing } = railway
  const ds = Math.min(1, Math.max(0.25, sleeperSpacing / 4))
  const samples = samplePath(path, ds)
  if (samples.length < 2) return null

  const railOffset = gauge / 2 + railSize / 2
  const strips: StripSpec[] = [
    { inner: -trackbedWidth / 2, outer: trackbedWidth / 2, color: BALLAST, height: 0.22 },
    { inner: -railOffset - railSize / 2, outer: -railOffset + railSize / 2, color: STEEL, height: 0.32 },
    { inner: railOffset - railSize / 2, outer: railOffset + railSize / 2, color: STEEL, height: 0.32 },
  ]
  const base = buildStrips(samples, strips, elevation, banking)
  if (!base) return null

  // Sleepers: flat quads across the track on top of the ballast
  const positions: number[] = [...base.positions]
  const colors: number[] = [...base.colors]
  const indices: number[] = [...base.indices]
  let vertexCount = base.positions.length / 3
  const sleeperLen = gauge + railSize * 2 + 0.6
  const halfAlong = 0.12
  const railTop = 0.24
  for (let s = sleeperSpacing / 2; s < path.length; s += sleeperSpacing) {
    const c = evaluatePath(path, s)
    const y = (c.z ?? elevation?.(c.s) ?? 0) + railTop
    const nx = -Math.sin(c.heading)
    const ny = Math.cos(c.heading)
    const tx = Math.cos(c.heading)
    const ty = Math.sin(c.heading)
    const corners: [number, number, number][] = [
      [c.x + tx * halfAlong + nx * sleeperLen / 2, y, -(c.y + ty * halfAlong + ny * sleeperLen / 2)],
      [c.x - tx * halfAlong + nx * sleeperLen / 2, y, -(c.y - ty * halfAlong + ny * sleeperLen / 2)],
      [c.x + tx * halfAlong - nx * sleeperLen / 2, y, -(c.y + ty * halfAlong - ny * sleeperLen / 2)],
      [c.x - tx * halfAlong - nx * sleeperLen / 2, y, -(c.y - ty * halfAlong - ny * sleeperLen / 2)],
    ]
    const start = vertexCount
    for (const [px, py, pz] of corners) {
      positions.push(px, py, pz)
      colors.push(SLEEPER[0], SLEEPER[1], SLEEPER[2])
      vertexCount++
    }
    indices.push(start, start + 2, start + 1, start + 1, start + 2, start + 3)
  }

  return {
    positions: new Float32Array(positions),
    colors: new Float32Array(colors),
    indices: new Uint32Array(indices),
  }
}

/** Split strips into pavement and markings, build each as a separate mesh.
 *  Pavement gets the asphalt PBR texture; markings get a plain vertex-colored
 *  material so white/yellow/green strips stay clean. */
function buildStripsSplit(
  samples: PathSample[],
  strips: StripSpec[],
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
  taperLayout?: { layoutAt: (s: number) => { left: LaneStrip[]; right: LaneStrip[] }; laneStripCount: number },
): RoadMeshResult {
  const pavementStrips = strips.filter((s) => s.surface !== 'marking')
  const markingStrips = strips.filter((s) => s.surface === 'marking')
  return {
    pavement: buildStrips(samples, pavementStrips, elevation, banking, taperLayout),
    markings: buildStrips(samples, markingStrips, elevation, banking),
  }
}

function buildStrips(
  samples: PathSample[],
  strips: StripSpec[],
  elevation?: ElevationSampler,
  banking?: ElevationSampler,
  taperLayout?: { layoutAt: (s: number) => { left: LaneStrip[]; right: LaneStrip[] }; laneStripCount: number },
): MeshData | null {
  if (samples.length < 2 || strips.length === 0) return null
  const n = samples.length
  const positions = new Float32Array(strips.length * n * 6)
  const colors = new Float32Array(strips.length * n * 6)
  const uvs = new Float32Array(strips.length * n * 4)
  const indices: number[] = []

  strips.forEach((strip, stripIndex) => {
    samples.forEach((sample, i) => {
      const nx = -Math.sin(sample.heading)
      const ny = Math.cos(sample.heading)
      const base = (stripIndex * n + i) * 6
      const baseHeight = (sample.z ?? elevation?.(sample.s) ?? 0) + (strip.height ?? 0)
      // banking tilts the section: positive cant raises the left side (positive lateral)
      const bank = Math.tan(banking?.(sample.s) ?? 0)
      let inner = strip.inner
      let outer = strip.outer
      if (taperLayout && stripIndex < taperLayout.laneStripCount) {
        const at = taperLayout.layoutAt(sample.s)
        // strips are ordered: left lanes (center→outward), right lanes, then markings
        const laneSide = stripIndex < at.left.length ? at.left[stripIndex] : at.right[stripIndex - at.left.length]
        if (laneSide) {
          inner = laneSide.inner
          outer = laneSide.outer
        }
      }
      const heightInner = baseHeight + bank * inner
      const heightOuter = baseHeight + bank * outer
      positions[base] = sample.x + nx * inner
      positions[base + 1] = heightInner
      positions[base + 2] = -(sample.y + ny * inner)
      positions[base + 3] = sample.x + nx * outer
      positions[base + 4] = heightOuter
      positions[base + 5] = -(sample.y + ny * outer)
      for (let vertex = 0; vertex < 2; vertex++) {
        const colorBase = base + vertex * 3
        colors[colorBase] = strip.color[0]
        colors[colorBase + 1] = strip.color[1]
        colors[colorBase + 2] = strip.color[2]
      }
      // constant texel density: one texture tile spans 6 m both across and along.
      // 4 floats per row (2 vertices × uv) — keep the stride in sync with the
      // 6-float position stride or every UV lands on the wrong vertex.
      const uvBase = (stripIndex * n + i) * 4
      uvs[uvBase] = inner / 6
      uvs[uvBase + 1] = sample.s / 6
      uvs[uvBase + 2] = outer / 6
      uvs[uvBase + 3] = sample.s / 6
    })
    for (let i = 0; i < n - 1; i++) {
      const A = (stripIndex * n + i) * 2
      const B = A + 1
      const C = A + 2
      const D = A + 3
      // Winding must face UP (+y): strips on either side of the centre line
      // (and edge/centre markings) wind in opposite lateral directions, so
      // branch on the quad's actual orientation. Down-facing quads render
      // black under the directional lights (the old "checkerboard" bug).
      const ax = positions[A * 3], az = positions[A * 3 + 2]
      const bx = positions[B * 3], bz = positions[B * 3 + 2]
      const cx = positions[C * 3], cz = positions[C * 3 + 2]
      const ny = (cx - ax) * (bz - az) - (bx - ax) * (cz - az)
      if (ny < 0) indices.push(A, C, B, B, C, D)
      else indices.push(A, B, C, B, D, C)
    }
  })

  return { positions, colors, uvs, indices: new Uint32Array(indices) }
}
