import { samplePath, samplePathRange } from './geometry'
import { laneLayout } from './lanes'
import type { PathSample } from './geometry'
import type { FittedPath, LaneSectionDef } from './types'

export interface MeshData {
  positions: Float32Array
  colors: Float32Array
  indices: Uint32Array
}

type Rgb = [number, number, number]

const ASPHALT_A: Rgb = [0.16, 0.18, 0.21]
const ASPHALT_B: Rgb = [0.19, 0.21, 0.25]
const WHITE: Rgb = [0.88, 0.9, 0.93]
const YELLOW: Rgb = [0.91, 0.7, 0.1]

interface StripSpec {
  inner: number
  outer: number
  color: Rgb
  height?: number
}

type ElevationSampler = (station: number) => number

export function buildRoadMesh(
  path: FittedPath | null,
  section: LaneSectionDef,
  ds = 1,
  elevation?: ElevationSampler,
): MeshData | null {
  if (!path || path.elements.length === 0) return null
  return buildRoadMeshFromSamples(samplePath(path, ds), section, elevation)
}

export function buildRoadMeshRange(
  path: FittedPath,
  section: LaneSectionDef,
  sStart: number,
  sEnd: number,
  ds = 1,
  elevation?: ElevationSampler,
): MeshData | null {
  return buildRoadMeshFromSamples(samplePathRange(path, sStart, sEnd, ds), section, elevation)
}

export function buildRoadMeshFromSamples(
  samples: PathSample[],
  section: LaneSectionDef,
  elevation?: ElevationSampler,
): MeshData | null {
  if (samples.length < 2) return null
  const layout = laneLayout(section)
  const strips: StripSpec[] = []

  layout.left.forEach((strip, i) => strips.push({ ...strip, color: i % 2 ? ASPHALT_B : ASPHALT_A }))
  layout.right.forEach((strip, i) => strips.push({ ...strip, color: i % 2 ? ASPHALT_B : ASPHALT_A }))

  if (layout.totalLeft > 0.3) strips.push({ inner: layout.totalLeft - 0.2, outer: layout.totalLeft - 0.05, color: WHITE, height: 0.025 })
  if (layout.totalRight > 0.3) strips.push({ inner: -layout.totalRight + 0.05, outer: -layout.totalRight + 0.2, color: WHITE, height: 0.025 })
  if (layout.totalLeft > 0 && layout.totalRight > 0) strips.push({ inner: -0.075, outer: 0.075, color: YELLOW, height: 0.025 })

  for (const side of [layout.left, layout.right]) {
    for (let i = 1; i < side.length; i++) {
      strips.push({ inner: side[i].inner - 0.06, outer: side[i].inner + 0.06, color: WHITE, height: 0.025 })
    }
  }

  return buildStrips(samples, strips, elevation)
}

export function buildConnectingRoadMesh(samples: PathSample[], laneCount: number, laneWidth: number): MeshData | null {
  if (samples.length < 2 || laneCount < 1) return null
  const total = laneCount * laneWidth
  const strips: StripSpec[] = []
  for (let lane = 0; lane < laneCount; lane++) {
    strips.push({
      inner: -total / 2 + lane * laneWidth,
      outer: -total / 2 + (lane + 1) * laneWidth,
      color: lane % 2 ? ASPHALT_B : ASPHALT_A,
      height: 0.015,
    })
  }
  for (let lane = 1; lane < laneCount; lane++) {
    const offset = -total / 2 + lane * laneWidth
    strips.push({ inner: offset - 0.06, outer: offset + 0.06, color: WHITE, height: 0.04 })
  }
  return buildStrips(samples, strips)
}

function buildStrips(samples: PathSample[], strips: StripSpec[], elevation?: ElevationSampler): MeshData | null {
  if (samples.length < 2 || strips.length === 0) return null
  const n = samples.length
  const positions = new Float32Array(strips.length * n * 6)
  const colors = new Float32Array(strips.length * n * 6)
  const indices: number[] = []

  strips.forEach((strip, stripIndex) => {
    samples.forEach((sample, i) => {
      const nx = -Math.sin(sample.heading)
      const ny = Math.cos(sample.heading)
      const base = (stripIndex * n + i) * 6
      const height = (sample.z ?? elevation?.(sample.s) ?? 0) + (strip.height ?? 0)
      positions[base] = sample.x + nx * strip.inner
      positions[base + 1] = height
      positions[base + 2] = -(sample.y + ny * strip.inner)
      positions[base + 3] = sample.x + nx * strip.outer
      positions[base + 4] = height
      positions[base + 5] = -(sample.y + ny * strip.outer)
      for (let vertex = 0; vertex < 2; vertex++) {
        const colorBase = base + vertex * 3
        colors[colorBase] = strip.color[0]
        colors[colorBase + 1] = strip.color[1]
        colors[colorBase + 2] = strip.color[2]
      }
    })
    for (let i = 0; i < n - 1; i++) {
      const inner0 = (stripIndex * n + i) * 2
      const outer0 = inner0 + 1
      const inner1 = inner0 + 2
      const outer1 = inner0 + 3
      indices.push(inner0, inner1, outer0, outer0, inner1, outer1)
    }
  })

  return { positions, colors, indices: new Uint32Array(indices) }
}
