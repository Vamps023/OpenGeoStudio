// ─────────────────────────────────────────────────────────────────────
// Rail fixture mesh generation (Train section): turnout points (tapered
// switch blades), frogs and diamonds at track crossings with wing and
// guard rails, and catch points — the Track Mesh Builder component set
// (points, frogs, diamonds, guard rails, catch points) simplified to
// strip geometry on top of the railway track meshes.
// ─────────────────────────────────────────────────────────────────────
import { evaluatePath, samplePathRange } from './geometry'
import type { PathSample } from './geometry'
import { fitTrackPath } from './tracks'
import type { FittedPath } from './types'
import type { MeshData } from './mesh'
import { DEFAULT_RAILWAY } from '../state/store'
import type { CatchPoint, RailCrossing, RailPoint, RailwayConfig, RoadData } from '../state/store'

type Rgb = [number, number, number]

const BLADE: Rgb = [0.45, 0.62, 0.82]     // switch blades (steel blue)
const FROG: Rgb = [0.92, 0.62, 0.18]      // frog + wing rails (orange)
const GUARD: Rgb = [0.85, 0.32, 0.28]     // guard rails (red)
const CATCH: Rgb = [0.7, 0.4, 0.85]       // catch points (purple)

interface FixtureContext {
  path: FittedPath
  railway: RailwayConfig
  elevation?: (s: number) => number
  length: number
}

function contextFor(road: RoadData | undefined): FixtureContext | null {
  if (!road) return null
  const path = fitTrackPath(road)
  if (!path) return null
  const elevation = road.elevationProfile?.length
    ? (() => {
        const profile = road.elevationProfile!
        return (s: number) => {
          // piecewise-linear elevation lookup (same convention as engine/elevation)
          if (profile.length === 0) return 0
          if (s <= profile[0].s) return profile[0].z
          for (let i = 1; i < profile.length; i++) {
            if (s <= profile[i].s) {
              const a = profile[i - 1]
              const b = profile[i]
              const t = (s - a.s) / Math.max(1e-9, b.s - a.s)
              return a.z + (b.z - a.z) * t
            }
          }
          return profile[profile.length - 1].z
        }
      })()
    : undefined
  return { path, railway: road.railway ?? DEFAULT_RAILWAY, elevation, length: path.length }
}

/**
 * Build a tapered strip mesh: for each sample t∈[0,1] along `samples`,
 * the strip spans lateral [innerAt(t), outerAt(t)] at height heightAt(t).
 */
function buildTaperedStrip(
  samples: PathSample[],
  innerAt: (t: number) => number,
  outerAt: (t: number) => number,
  heightAt: (t: number) => number,
  color: Rgb,
  elevation?: (s: number) => number,
): MeshData | null {
  const n = samples.length
  if (n < 2) return null
  const positions: number[] = []
  const colors: number[] = []
  const indices: number[] = []
  for (let i = 0; i < n; i++) {
    const sample = samples[i]
    const t = i / (n - 1)
    const nx = -Math.sin(sample.heading)
    const ny = Math.cos(sample.heading)
    const base = (sample.z ?? elevation?.(sample.s) ?? 0)
    const inner = innerAt(t)
    const outer = outerAt(t)
    const h = heightAt(t)
    positions.push(
      sample.x + nx * inner, base + h, -(sample.y + ny * inner),
      sample.x + nx * outer, base + h, -(sample.y + ny * outer),
    )
    colors.push(color[0], color[1], color[2], color[0], color[1], color[2])
  }
  for (let i = 0; i < n - 1; i++) {
    const inner0 = i * 2
    const outer0 = inner0 + 1
    const inner1 = inner0 + 2
    const outer1 = inner0 + 3
    indices.push(inner0, inner1, outer0, outer0, inner1, outer1)
  }
  return { positions: new Float32Array(positions), colors: new Float32Array(colors), indices: new Uint32Array(indices) }
}

/** Constant strip between two stations (simple wing/guard rails). */
function buildStripBetween(ctx: FixtureContext, sStart: number, sEnd: number, inner: number, outer: number, height: number, color: Rgb): MeshData | null {
  if (sEnd - sStart < 0.2) return null
  const samples = samplePathRange(ctx.path, Math.max(0, sStart), Math.min(ctx.length, sEnd), 0.5)
  return buildTaperedStrip(samples, () => inner, () => outer, () => height, color, ctx.elevation)
}

// ─── Turnout point: tapered switch blade on the branch track ────────

function buildPointBlade(branch: FixtureContext, contact: 'start' | 'end', throughSide: 'left' | 'right'): MeshData | null {
  const { railway } = branch
  const bladeLength = Math.min(12, branch.length / 3)
  if (bladeLength < 2) return null
  const sTip = contact === 'start' ? 0 : branch.length
  const sBack = contact === 'start' ? bladeLength : branch.length - bladeLength
  const samples = samplePathRange(branch.path, Math.min(sTip, sBack), Math.max(sTip, sBack), 0.5)
  if (samples.length < 2) return null
  // normalize sample order so t=0 is the tip
  const ordered = contact === 'start' ? samples : [...samples].reverse()
  const sideSign = throughSide === 'left' ? -1 : 1
  const railOffset = railway.gauge / 2 + railway.railSize / 2
  const centerAt = (t: number) => sideSign * (railOffset - (railOffset - 0.12) * (1 - t))
  const halfWidth = (t: number) => railway.railSize * (0.2 + 0.8 * t) / 2
  return buildTaperedStrip(
    ordered,
    (t) => centerAt(t) - halfWidth(t),
    (t) => centerAt(t) + halfWidth(t),
    () => 0.32,
    BLADE,
    branch.elevation,
  )
}

// ─── Crossing: frog (one track) / diamond (both tracks) ─────────────

function buildCrossingWings(ctx: FixtureContext, s: number): MeshData[] {
  const out: MeshData[] = []
  const gaugeHalf = ctx.railway.gauge / 2
  // wing rails inside each running rail, flanking the crossing V
  for (const side of [-1, 1] as const) {
    const inner = side * (gaugeHalf - 0.16)
    const outer = side * (gaugeHalf)
    for (const [a, b] of [[s - 2.6, s - 0.9], [s + 0.9, s + 2.6]] as const) {
      const mesh = buildStripBetween(ctx, a, b, Math.min(inner, outer), Math.max(inner, outer), 0.3, FROG)
      if (mesh) out.push(mesh)
    }
  }
  return out
}

function buildGuardRails(ctx: FixtureContext, s: number): MeshData[] {
  const out: MeshData[] = []
  const gaugeHalf = ctx.railway.gauge / 2
  // check rails inside both running rails, further out along the track
  for (const side of [-1, 1] as const) {
    const inner = side * (gaugeHalf - 0.3)
    const outer = side * (gaugeHalf - 0.16)
    for (const [a, b] of [[s - 5.2, s - 2.8], [s + 2.8, s + 5.2]] as const) {
      const mesh = buildStripBetween(ctx, a, b, Math.min(inner, outer), Math.max(inner, outer), 0.28, GUARD)
      if (mesh) out.push(mesh)
    }
  }
  return out
}

// ─── Catch point: blade at the extremity + stop block ────────────────

function buildCatchPoint(ctx: FixtureContext, contact: 'start' | 'end', side: 'left' | 'right'): MeshData[] {
  const out: MeshData[] = []
  const bladeLength = Math.min(7, ctx.length / 3)
  if (bladeLength < 2) return out
  const sTip = contact === 'start' ? 0 : ctx.length
  const sBack = contact === 'start' ? bladeLength : ctx.length - bladeLength
  const samples = samplePathRange(ctx.path, Math.min(sTip, sBack), Math.max(sTip, sBack), 0.5)
  if (samples.length >= 2) {
    const ordered = contact === 'start' ? samples : [...samples].reverse()
    const sideSign = side === 'left' ? 1 : -1
    const gaugeHalf = ctx.railway.gauge / 2
    const mesh = buildTaperedStrip(
      ordered,
      (t) => sideSign * gaugeHalf * (1 - (1 - t) * 0.85) - ctx.railway.railSize / 2,
      (t) => sideSign * gaugeHalf * (1 - (1 - t) * 0.85) + ctx.railway.railSize / 2,
      () => 0.34,
      CATCH,
      ctx.elevation,
    )
    if (mesh) out.push(mesh)
  }
  // stop block across the gauge just behind the blade tip
  const sBlock = contact === 'start' ? bladeLength + 0.3 : ctx.length - bladeLength - 0.3
  const block = buildStripBetween(ctx, sBlock - 0.2, sBlock + 0.2, -ctx.railway.gauge / 2, ctx.railway.gauge / 2, 0.45, CATCH)
  if (block) out.push(block)
  return out
}

// ─── Public entry ────────────────────────────────────────────────────

export interface RailFixtureProject {
  roads: RoadData[]
  railPoints?: RailPoint[]
  railCrossings?: RailCrossing[]
  catchPoints?: CatchPoint[]
}

/** Which contact of `track` lies nearest to `at`. */
export function nearestContact(track: RoadData, at: { x: number; y: number }): 'start' | 'end' {
  const path = fitTrackPath(track)
  if (!path) return 'start'
  const start = { x: path.elements[0].x, y: path.elements[0].y }
  const end = pointAtLength(path, path.length)
  const dStart = Math.hypot(start.x - at.x, start.y - at.y)
  const dEnd = Math.hypot(end.x - at.x, end.y - at.y)
  return dStart <= dEnd ? 'start' : 'end'
}

function pointAtLength(path: FittedPath, length: number): { x: number; y: number } {
  const sample = evaluatePath(path, length)
  return { x: sample.x, y: sample.y }
}

function sampleAt(path: FittedPath, s: number): { x: number; y: number; heading: number } {
  return evaluatePath(path, s)
}

/** Build meshes for all rail fixtures of a project (Train section). */
export function buildRailFixtureMeshes(project: RailFixtureProject): MeshData[] {
  const byId = new Map(project.roads.map((road) => [road.id, road]))
  const ctx = (id: string) => contextFor(byId.get(id))
  const out: MeshData[] = []

  for (const point of project.railPoints ?? []) {
    const facing = ctx(point.facingTrackId)
    const trailing = ctx(point.trailingTrackId)
    const branch = ctx(point.branchTrackId)
    if (!facing || !trailing || !branch) continue
    // facing extremity position
    const facingStart = facing.path.elements[0]
    const facingTip = point.facingContact === 'start'
      ? { x: facingStart.x, y: facingStart.y }
      : pointAtLength(facing.path, facing.length)
    // branch contact at the point
    const branchStart = branch.path.elements[0]
    const branchEnd = pointAtLength(branch.path, branch.length)
    const dStart = Math.hypot(branchStart.x - facingTip.x, branchStart.y - facingTip.y)
    const dEnd = Math.hypot(branchEnd.x - facingTip.x, branchEnd.y - facingTip.y)
    const branchContact: 'start' | 'end' = dStart <= dEnd ? 'start' : 'end'
    // which side of the branch the through (trailing) line lies on
    const tipSample = sampleAt(branch.path, branchContact === 'start' ? 0 : branch.length)
    const trailingStart = trailing.path.elements[0]
    const trailingEnd = pointAtLength(trailing.path, trailing.length)
    const throughPoint = Math.hypot(trailingStart.x - facingTip.x, trailingStart.y - facingTip.y)
      <= Math.hypot(trailingEnd.x - facingTip.x, trailingEnd.y - facingTip.y) ? trailingStart : trailingEnd
    const nx = -Math.sin(tipSample.heading)
    const ny = Math.cos(tipSample.heading)
    const throughSide: 'left' | 'right' = (throughPoint.x - tipSample.x) * nx + (throughPoint.y - tipSample.y) * ny >= 0 ? 'left' : 'right'
    const blade = buildPointBlade(branch, branchContact, throughSide)
    if (blade) out.push(blade)
  }

  for (const crossing of project.railCrossings ?? []) {
    const a = ctx(crossing.trackAId)
    const b = ctx(crossing.trackBId)
    if (!a || !b) continue
    out.push(...buildCrossingWings(a, crossing.sA))
    out.push(...buildGuardRails(a, crossing.sA))
    if (crossing.kind === 'diamond') {
      // plain crossing: both tracks get wings + guards; a frog only has them on one
      out.push(...buildCrossingWings(b, crossing.sB))
      out.push(...buildGuardRails(b, crossing.sB))
    }
  }

  for (const catchPoint of project.catchPoints ?? []) {
    const track = ctx(catchPoint.trackId)
    if (!track) continue
    out.push(...buildCatchPoint(track, catchPoint.contact, catchPoint.side))
  }

  return out
}
