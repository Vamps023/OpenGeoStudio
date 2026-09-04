// ─────────────────────────────────────────────────────────────────────
// SCANeR-compatible XY functions: segment, circle arc, clothoid arc,
// polyline, Bezier and ClothoidSpline.
//
// A "track" (RoadData with functions) is a sequence of XY functions
// chained end-to-end. Intrinsic functions (segment, arc, clothoid) are
// defined by parameters only; point-based functions (polyline, bezier,
// clothoidSpline) store absolute points with points[0]/start equal to
// the chain cursor when the track is built.
// ─────────────────────────────────────────────────────────────────────
import type { PathSample } from './geometry'
import type { Vec2 } from './types'

export type XYFunctionKind = 'segment' | 'arc' | 'clothoid' | 'polyline' | 'bezier' | 'clothoidSpline'

export interface SegmentFunction {
  kind: 'segment'
  length: number
}
export interface ArcFunction {
  kind: 'arc'
  radius: number   // > 0, always equal in/out per SCANeR
  angle: number    // signed sweep [rad], positive = left turn
}
export interface ClothoidFunction {
  kind: 'clothoid'
  radiusIn: number   // 0 = infinite radius (straight)
  radiusOut: number  // 0 = infinite radius (straight)
  length: number
}
export interface PolylineFunction {
  kind: 'polyline'
  points: Vec2[]   // absolute, points[0] = chain start
  splineType: 'segment' | 'spline' | 'bezier' // "Polyline Spline" dropdown
}
export interface BezierFunction {
  kind: 'bezier'
  p0?: Vec2        // absolute start point; control points are absolute, so the
  p1: Vec2         // chain frame alone cannot measure or invert a bezier
  p2: Vec2
  p3: Vec2
}
export interface ClothoidSplineFunction {
  kind: 'clothoidSpline'
  points: Vec2[]          // control points (spline does NOT pass through them)
  tolerance: number       // approximation accuracy for conversions
  symmetryThreshold: number
}
export type XYFunction = SegmentFunction | ArcFunction | ClothoidFunction | PolylineFunction | BezierFunction | ClothoidSplineFunction

export interface Frame {
  x: number
  y: number
  heading: number
}

export const INFINITE_RADIUS = 0 // SCANeR: infinite radius value is 0

// ─── Colors used by the doc tool table ───────────────────────────────
export const FUNCTION_COLORS: Record<XYFunctionKind, string> = {
  segment: '#4ade80',          // green
  arc: '#b87333',              // brown
  clothoid: '#3b82f6',         // blue
  polyline: '#a855f7',         // purple
  bezier: '#a855f7',           // handle-based like polyline (purple)
  clothoidSpline: '#1d4ed8',   // dark blue (+green in doc)
}

export const FUNCTION_LABELS: Record<XYFunctionKind, string> = {
  segment: 'Segment',
  arc: 'Circle Arc',
  clothoid: 'Clothoid Arc',
  polyline: 'Polyline',
  bezier: 'Bezier',
  clothoidSpline: 'ClothoidSpline',
}

// ─── Length ──────────────────────────────────────────────────────────
export function functionLength(fn: XYFunction, frame?: Frame): number {
  switch (fn.kind) {
    case 'segment': return Math.max(0, fn.length)
    case 'arc': return Math.abs(fn.radius * fn.angle)
    case 'clothoid': return Math.max(0, fn.length)
    case 'polyline': return polylinieLength(fn.points)
    case 'bezier': {
      // control points are absolute: measure from the chain frame when the
      // caller knows it, else from the stored p0 (origin fallback is legacy-only)
      const p0 = frame ?? (fn.p0 ? { x: fn.p0.x, y: fn.p0.y, heading: 0 } : { x: 0, y: 0, heading: 0 })
      return bezierLength(p0, fn)
    }
    case 'clothoidSpline': return splineLength(fn.points)
  }
}

function polylinieLength(points: Vec2[]): number {
  let total = 0
  for (let i = 1; i < points.length; i++) total += Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)
  return total
}

// ─── Clothoid (Euler spiral) math ────────────────────────────────────
// Curvature varies linearly from kIn to kOut over length L.
function clothoidCurvature(fn: ClothoidFunction, s: number): number {
  const kIn = fn.radiusIn !== 0 ? 1 / fn.radiusIn : 0
  const kOut = fn.radiusOut !== 0 ? 1 / fn.radiusOut : 0
  const L = Math.max(1e-9, fn.length)
  return kIn + ((kOut - kIn) * s) / L
}

/** Point/heading at local station s along a clothoid starting at `frame`. */
export function clothoidAt(frame: Frame, fn: ClothoidFunction, s: number): { x: number; y: number; heading: number } {
  const L = Math.max(1e-9, fn.length)
  const t = Math.max(0, Math.min(L, s))
  const kIn = fn.radiusIn !== 0 ? 1 / fn.radiusIn : 0
  const kOut = fn.radiusOut !== 0 ? 1 / fn.radiusOut : 0
  const k1 = (kOut - kIn) / L
  // θ(t) = h0 + kIn·t + k1·t²/2 ; position = ∫cos/sin θ — Simpson integration
  const steps = Math.max(4, Math.ceil(t / 0.5))
  let x = frame.x
  let y = frame.y
  const h = t / steps
  let u = 0
  for (let i = 0; i < steps; i++) {
    const th1 = frame.heading + kIn * u + (k1 * u * u) / 2
    const th2 = frame.heading + kIn * (u + h) + (k1 * (u + h) * (u + h)) / 2
    x += (h / 2) * (Math.cos(th1) + Math.cos(th2))
    y += (h / 2) * (Math.sin(th1) + Math.sin(th2))
    u += h
  }
  return { x, y, heading: frame.heading + kIn * t + (k1 * t * t) / 2 }
}

// ─── Bezier math ─────────────────────────────────────────────────────
export function bezierAt(frame: Frame, p1: Vec2, p2: Vec2, p3: Vec2, t: number): { x: number; y: number; heading: number } {
  const p0 = { x: frame.x, y: frame.y }
  const u = 1 - t
  const x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x
  const y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y
  const dx = 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x)
  const dy = 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y)
  return { x, y, heading: Math.atan2(dy, dx) }
}

export function bezierLength(frame: Frame, fn: BezierFunction): number {
  const steps = 32
  let total = 0
  let prev = { x: frame.x, y: frame.y }
  for (let i = 1; i <= steps; i++) {
    const p = bezierAt(frame, fn.p1, fn.p2, fn.p3, i / steps)
    total += Math.hypot(p.x - prev.x, p.y - prev.y)
    prev = p
  }
  return total
}

// ─── ClothoidSpline: clamped spline through control points ───────────
// (clamped = tangent continuity at junctions; see doc warnings)
function splineTangents(points: Vec2[]): number[] {
  const n = points.length
  if (n < 2) return []
  const tangents: number[] = []
  for (let i = 0; i < n; i++) {
    if (i === 0) tangents.push(Math.atan2(points[1].y - points[0].y, points[1].x - points[0].x))
    else if (i === n - 1) tangents.push(Math.atan2(points[n - 1].y - points[n - 2].y, points[n - 1].x - points[n - 2].x))
    else {
      const a = Math.atan2(points[i].y - points[i - 1].y, points[i].x - points[i - 1].x)
      const b = Math.atan2(points[i + 1].y - points[i].y, points[i + 1].x - points[i].x)
      tangents.push(angleLerp(a, b))
    }
  }
  return tangents
}

function angleLerp(a: number, b: number): number {
  let d = b - a
  while (d > Math.PI) d -= Math.PI * 2
  while (d < -Math.PI) d += Math.PI * 2
  return a + d / 2
}

function hermiteAt(p0: Vec2, p1: Vec2, m0: number, m1: number, scale0: number, scale1: number, t: number): Vec2 {
  const u = 1 - t
  const h00 = 2 * t * t * t - 3 * t * t + 1
  const h10 = t * t * t - 2 * t * t + t
  const h01 = -2 * t * t * t + 3 * t * t
  const h11 = t * t * t - t * t
  return {
    x: h00 * p0.x + h10 * scale0 * Math.cos(m0) + h01 * p1.x + h11 * scale1 * Math.cos(m1),
    y: h00 * p0.y + h10 * scale0 * Math.sin(m0) + h01 * p1.y + h11 * scale1 * Math.sin(m1),
  }
}

/** Samples of a clamped spline through `points` (pinning ends to given headings). */
export function splineSamples(points: Vec2[], endHeadingIn: number | null, endHeadingOut: number | null, ds = 1): PathSample[] {
  if (points.length < 2) return []
  const tangents = splineTangents(points)
  if (endHeadingIn !== null) tangents[0] = endHeadingIn
  if (endHeadingOut !== null) tangents[tangents.length - 1] = endHeadingOut
  const samples: PathSample[] = []
  let s = 0
  for (let i = 0; i < points.length - 1; i++) {
    const p0 = points[i]
    const p1 = points[i + 1]
    const chord = Math.hypot(p1.x - p0.x, p1.y - p0.y)
    const scale0 = chord
    const scale1 = chord
    const steps = Math.max(4, Math.ceil(chord / ds))
    for (let k = 0; k < steps; k++) {
      const t = k / steps
      const u = 1 - t
      const p = hermiteAt(p0, p1, tangents[i], tangents[i + 1], scale0, scale1, t)
      // tangent by finite difference of the hermite
      const e = 1e-3
      const pa = hermiteAt(p0, p1, tangents[i], tangents[i + 1], scale0, scale1, Math.max(0, t - e))
      const pb = hermiteAt(p0, p1, tangents[i], tangents[i + 1], scale0, scale1, Math.min(1, t + e))
      const heading = Math.atan2(pb.y - pa.y, pb.x - pa.x)
      samples.push({ s: s + t * chord, x: p.x, y: p.y, heading })
    }
    s += chord
  }
  const last = points[points.length - 1]
  samples.push({ s, x: last.x, y: last.y, heading: tangents[tangents.length - 1] })
  return samples
}

function splineLength(points: Vec2[]): number {
  // Chord-sum domain: evaluateElement / splineSamples parameterize the curve
  // by chord length, so the function length must match it exactly. Padding
  // here made meshes extrapolate straight past the last control point.
  let total = 0
  for (let i = 1; i < points.length; i++) total += Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)
  return total
}

// ─── Sampling a function from a frame ────────────────────────────────
export function sampleFunction(frame: Frame, fn: XYFunction, ds = 1): PathSample[] {
  const samples: PathSample[] = [{ s: 0, x: frame.x, y: frame.y, heading: frame.heading }]
  const length = functionLength(fn)
  switch (fn.kind) {
    case 'segment': {
      const steps = Math.max(1, Math.ceil(length / ds))
      for (let i = 1; i <= steps; i++) {
        const s = (length * i) / steps
        samples.push({ s, x: frame.x + Math.cos(frame.heading) * s, y: frame.y + Math.sin(frame.heading) * s, heading: frame.heading })
      }
      return samples
    }
    case 'arc': {
      const steps = Math.max(2, Math.ceil(length / ds))
      const k = Math.sign(fn.angle) / Math.max(1e-9, fn.radius)
      for (let i = 1; i <= steps; i++) {
        const s = (length * i) / steps
        const h = frame.heading + k * s
        samples.push({
          s,
          x: frame.x + (Math.sin(h) - Math.sin(frame.heading)) / k,
          y: frame.y + (-Math.cos(h) + Math.cos(frame.heading)) / k,
          heading: h,
        })
      }
      return samples
    }
    case 'clothoid': {
      const steps = Math.max(4, Math.ceil(length / Math.min(ds, 1)))
      for (let i = 1; i <= steps; i++) {
        const s = (length * i) / steps
        const p = clothoidAt(frame, fn, s)
        samples.push({ s, x: p.x, y: p.y, heading: p.heading })
      }
      return samples
    }
    case 'polyline': {
      let s = 0
      for (let i = 1; i < fn.points.length; i++) {
        const a = fn.points[i - 1]
        const b = fn.points[i]
        const seg = Math.hypot(b.x - a.x, b.y - a.y)
        if (seg < 1e-9) continue
        const heading = Math.atan2(b.y - a.y, b.x - a.x)
        samples.push({ s: s + seg, x: b.x, y: b.y, heading })
        s += seg
      }
      return samples
    }
    case 'bezier': {
      const steps = Math.max(8, Math.ceil(length / ds))
      for (let i = 1; i <= steps; i++) {
        const t = i / steps
        const p = bezierAt(frame, fn.p1, fn.p2, fn.p3, t)
        samples.push({ s: length * t, x: p.x, y: p.y, heading: p.heading })
      }
      return samples
    }
    case 'clothoidSpline': {
      const inner = splineSamples(fn.points, null, null, ds)
      for (const p of inner) samples.push({ ...p, s: p.s })
      return samples
    }
  }
}

/** End frame after walking `fn` from `frame`. */
export function functionEndFrame(frame: Frame, fn: XYFunction): Frame {
  const length = functionLength(fn)
  switch (fn.kind) {
    case 'segment':
      return { x: frame.x + Math.cos(frame.heading) * length, y: frame.y + Math.sin(frame.heading) * length, heading: frame.heading }
    case 'arc': {
      const k = Math.sign(fn.angle) / Math.max(1e-9, fn.radius)
      const h = frame.heading + fn.angle
      return {
        x: frame.x + (Math.sin(h) - Math.sin(frame.heading)) / k,
        y: frame.y + (-Math.cos(h) + Math.cos(frame.heading)) / k,
        heading: h,
      }
    }
    case 'clothoid':
      return clothoidAt(frame, fn, length)
    case 'polyline': {
      const end = fn.points[fn.points.length - 1]
      const prev = fn.points.length > 1 ? fn.points[fn.points.length - 2] : { x: frame.x, y: frame.y }
      return { x: end.x, y: end.y, heading: Math.atan2(end.y - prev.y, end.x - prev.x) }
    }
    case 'bezier':
      return bezierAt(frame, fn.p1, fn.p2, fn.p3, 1)
    case 'clothoidSpline': {
      const samples = splineSamples(fn.points, null, null, 2)
      const last = samples[samples.length - 1]
      return { x: last.x, y: last.y, heading: last.heading }
    }
  }
}

// ─── Function parameter helpers ──────────────────────────────────────
export function setFunctionLength(fn: XYFunction, length: number): XYFunction {
  switch (fn.kind) {
    case 'segment': return { ...fn, length: Math.max(0.01, length) }
    case 'arc': {
      const angle = fn.radius > 1e-9 ? Math.sign(fn.angle || 1) * (length / fn.radius) : fn.angle
      return { ...fn, angle }
    }
    case 'clothoid': return { ...fn, length: Math.max(0.01, length) }
    default: return fn
  }
}

export function setFunctionRadius(fn: XYFunction, radius: number, at: 'in' | 'out'): XYFunction {
  if (fn.kind === 'arc') return { ...fn, radius: Math.max(0.01, radius) }
  // clothoid radii are signed (negative = right turn); 0 = infinite
  if (fn.kind === 'clothoid') return at === 'in' ? { ...fn, radiusIn: radius } : { ...fn, radiusOut: radius }
  return fn
}

/** Whether a function kind supports Free / Fixed Radius / Fixed Length constraints. */
export function supportsConstraints(fn: XYFunction): boolean {
  return fn.kind === 'arc' || fn.kind === 'clothoid'
}

/** Suggested radius continuity: the output radius of a function (0 = infinite). */
export function functionRadiusOut(fn: XYFunction | undefined): number {
  if (!fn) return INFINITE_RADIUS
  if (fn.kind === 'segment') return INFINITE_RADIUS
  if (fn.kind === 'arc') return fn.radius
  if (fn.kind === 'clothoid') return fn.radiusOut
  return INFINITE_RADIUS
}

// ─── Split / merge ───────────────────────────────────────────────────

/** Split one function at local station `s` (0 < s < length). Returns [a, b]. */
export function splitFunction(frame: Frame, fn: XYFunction, s: number): [XYFunction, XYFunction] | null {
  const length = functionLength(fn)
  if (s <= 0.01 || s >= length - 0.01) return null
  switch (fn.kind) {
    case 'segment':
      return [{ kind: 'segment', length: s }, { kind: 'segment', length: length - s }]
    case 'arc': {
      const a1 = Math.sign(fn.angle) * (s / fn.radius)
      return [
        { kind: 'arc', radius: fn.radius, angle: a1 },
        { kind: 'arc', radius: fn.radius, angle: fn.angle - a1 },
      ]
    }
    case 'clothoid': {
      const midK = clothoidCurvature(fn, s)
      const rMid = midK === 0 ? INFINITE_RADIUS : 1 / midK
      return [
        { kind: 'clothoid', radiusIn: fn.radiusIn, radiusOut: rMid, length: s },
        { kind: 'clothoid', radiusIn: rMid, radiusOut: fn.radiusOut, length: length - s },
      ]
    }
    case 'polyline': {
      const pts = splitPointList(fn.points, s)
      if (!pts) return null
      return [
        { ...fn, points: pts[0] },
        { ...fn, points: pts[1] },
      ]
    }
    case 'bezier': {
      // de Casteljau split with p0 from the frame
      const p0 = { x: frame.x, y: frame.y }
      const t = s / length
      const { left, right } = deCasteljau(p0, fn.p1, fn.p2, fn.p3, t)
      return [
        { kind: 'bezier', p0: left[0], p1: left[1], p2: left[2], p3: left[3] },
        { kind: 'bezier', p0: right[0], p1: right[1], p2: right[2], p3: right[3] },
      ]
    }
    case 'clothoidSpline': {
      const pts = splitPointList(fn.points, s)
      if (!pts) return null
      return [
        { ...fn, points: pts[0] },
        { ...fn, points: pts[1] },
      ]
    }
  }
}

/** Remove interior vertices that lie exactly on the segment between their neighbours. */
function dropCollinear(points: Vec2[]): Vec2[] {
  if (points.length <= 2) return points
  const out = [points[0]]
  for (let i = 1; i < points.length - 1; i++) {
    const a = out[out.length - 1]
    const b = points[i]
    const c = points[i + 1]
    const cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
    const dot = (b.x - a.x) * (c.x - a.x) + (b.y - a.y) * (c.y - a.y)
    // strictly collinear AND between a and c → drop
    if (Math.abs(cross) < 1e-6 && dot > 0) continue
    out.push(b)
  }
  out.push(points[points.length - 1])
  return out
}

function splitPointList(points: Vec2[], s: number): [Vec2[], Vec2[]] | null {  let acc = 0
  for (let i = 1; i < points.length; i++) {
    const seg = Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)
    if (acc + seg >= s) {
      const t = seg > 1e-9 ? (s - acc) / seg : 0
      const mid = { x: points[i - 1].x + (points[i].x - points[i - 1].x) * t, y: points[i - 1].y + (points[i].y - points[i - 1].y) * t }
      return [ [...points.slice(0, i), mid], [mid, ...points.slice(i)] ]
    }
    acc += seg
  }
  return null
}

function deCasteljau(p0: Vec2, p1: Vec2, p2: Vec2, p3: Vec2, t: number) {
  const lerp = (a: Vec2, b: Vec2, t: number): Vec2 => ({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t })
  const ab = lerp(p0, p1, t)
  const bc = lerp(p1, p2, t)
  const cd = lerp(p2, p3, t)
  const abc = lerp(ab, bc, t)
  const bcd = lerp(bc, cd, t)
  const abcd = lerp(abc, bcd, t)
  return { left: [p0, ab, abc, abcd], right: [abcd, bcd, cd, p3] }
}

/** Merge two contiguous same-kind functions. Returns null when not mergeable. */
export function mergeFunctions(a: XYFunction, b: XYFunction): XYFunction | null {
  if (a.kind !== b.kind) return null
  switch (a.kind) {
    case 'segment':
      return { kind: 'segment', length: a.length + (b as SegmentFunction).length }
    case 'arc': {
      const bb = b as ArcFunction
      if (Math.abs(bb.radius - a.radius) > 1e-6 || Math.sign(bb.angle) !== Math.sign(a.angle)) return null
      return { kind: 'arc', radius: a.radius, angle: a.angle + bb.angle }
    }
    case 'clothoid': {
      const bb = b as ClothoidFunction
      const rOutA = a.radiusOut
      const rInB = bb.radiusIn
      const same = (rOutA === 0 && rInB === 0) || Math.abs(rOutA - rInB) < 1e-4
      if (!same) return null
      return { kind: 'clothoid', radiusIn: a.radiusIn, radiusOut: bb.radiusOut, length: a.length + bb.length }
    }
    case 'polyline': {
      const bb = b as PolylineFunction
      const merged = [...a.points, ...bb.points.slice(1)]
      return { kind: 'polyline', points: dropCollinear(merged), splineType: a.splineType }
    }
    default:
      return null
  }
}

// ─── Invert (reverse traversal direction) ────────────────────────────
export function invertFunction(fn: XYFunction): XYFunction {
  switch (fn.kind) {
    case 'segment': return fn
    case 'arc': return { ...fn, angle: -fn.angle }
    // reversed clothoid: curvature negates → rIn_rev = -rOut, rOut_rev = -rIn
    case 'clothoid': return { ...fn, radiusIn: -fn.radiusOut, radiusOut: -fn.radiusIn }
    case 'polyline': return { ...fn, points: [...fn.points].reverse() }
    // reversed bezier starts at the old end (p3) and ends at the old start
    // (p0 when known — legacy beziers without p0 keep their old end)
    case 'bezier': return { ...fn, p0: fn.p3, p1: fn.p2, p2: fn.p1, p3: fn.p0 ?? fn.p3 }
    case 'clothoidSpline': return { ...fn, points: [...fn.points].reverse() }
  }
}

// ─── Conversions (doc 5.5.4.2.14 / 5.5.4.2.15) ───────────────────────

/** Convert a segment-type polyline into a smooth Bezier (preserves end tangents). */
export function convertPolylineToBezier(frame: Frame, fn: PolylineFunction): BezierFunction {
  const end = fn.points[fn.points.length - 1]
  const chord = Math.hypot(end.x - frame.x, end.y - frame.y)
  const dirIn: Vec2 = { x: Math.cos(frame.heading), y: Math.sin(frame.heading) }
  const prev = fn.points.length > 1 ? fn.points[fn.points.length - 2] : frame
  const dirOut: Vec2 = normalize({ x: end.x - prev.x, y: end.y - prev.y })
  const d = chord / 2
  return {
    kind: 'bezier',
    p0: { x: frame.x, y: frame.y },
    p1: { x: frame.x + dirIn.x * d, y: frame.y + dirIn.y * d },
    p2: { x: end.x - dirOut.x * d, y: end.y - dirOut.y * d },
    p3: end,
  }
}

/**
 * Convert a segment polyline into a series of clothoid arcs and circle
 * segments: each interior corner becomes clothoid (in) → circle → clothoid (out).
 */
export function convertPolylineToClothoidArcs(fn: PolylineFunction): XYFunction[] {
  const out: XYFunction[] = []
  const pts = fn.points
  if (pts.length < 2) return out
  // initial segment to first vertex handled by corners below; start from first span
  for (let i = 1; i < pts.length - 1; i++) {
    const a = pts[i - 1]
    const b = pts[i]
    const c = pts[i + 1]
    const inLen = Math.hypot(b.x - a.x, b.y - a.y)
    const outLen = Math.hypot(c.x - b.x, c.y - b.y)
    const hIn = Math.atan2(b.y - a.y, b.x - a.x)
    const hOut = Math.atan2(c.y - b.y, c.x - b.x)
    let deflection = hOut - hIn
    while (deflection > Math.PI) deflection -= Math.PI * 2
    while (deflection < -Math.PI) deflection += Math.PI * 2
    if (Math.abs(deflection) < 1e-4) continue
    const radius = Math.max(5, Math.min(inLen, outLen) / 2)
    const tangent = radius * Math.tan(Math.abs(deflection) / 2)
    const clothoidLen = Math.max(1, tangent * 0.5)
    const straight1 = Math.max(0, inLen - tangent)
    if (i === 1 && straight1 > 0.01) out.push({ kind: 'segment', length: straight1 })
    // clothoid infinite→radius
    out.push({ kind: 'clothoid', radiusIn: INFINITE_RADIUS, radiusOut: radius, length: clothoidLen })
    out.push({ kind: 'arc', radius, angle: Math.max(0.01, Math.abs(deflection) - (2 * clothoidLen) / radius) * Math.sign(deflection) })
    out.push({ kind: 'clothoid', radiusIn: radius, radiusOut: INFINITE_RADIUS, length: clothoidLen })
    const straight2 = Math.max(0, outLen - tangent)
    if (i === pts.length - 2 && straight2 > 0.01) out.push({ kind: 'segment', length: straight2 })
  }
  if (out.length === 0) out.push({ kind: 'segment', length: polylinieLength(pts) })
  return out
}

/** Convert a polyline into a ClothoidSpline through its vertices. */
export function convertPolylineToClothoidSpline(fn: PolylineFunction, tolerance = 0.5): ClothoidSplineFunction {
  return {
    kind: 'clothoidSpline',
    points: [...fn.points],
    tolerance,
    symmetryThreshold: 1,
  }
}

/**
 * Convert a ClothoidSpline into "clothoid + circle + segment" (doc 5.5.4.2.15):
 * best-fit decomposition of the spline into segment → clothoid(0→R) →
 * circle(R) → clothoid(R→0) → segment.
 */
export function convertSplineToFunctions(frame: Frame, fn: ClothoidSplineFunction): XYFunction[] {
  const samples = splineSamples(fn.points, null, null, 1)
  if (samples.length < 2) return []
  const end = samples[samples.length - 1]
  const startHeading = samples[0].heading
  const endHeading = end.heading
  let totalTurn = endHeading - startHeading
  while (totalTurn > Math.PI) totalTurn -= Math.PI * 2
  while (totalTurn < -Math.PI) totalTurn += Math.PI * 2
  const chord = Math.hypot(end.x - frame.x, end.y - frame.y)
  const out: XYFunction[] = []

  if (Math.abs(totalTurn) < 0.02) {
    // essentially straight
    out.push({ kind: 'segment', length: chord })
    return out
  }
  // Best fit circle radius from chord + turn: chord = 2R sin(turn/2)
  const radius = Math.abs(chord / (2 * Math.sin(Math.abs(totalTurn) / 2)))
  const arcAngle = totalTurn
  // Transition lengths from chord geometry (keep 60% for the circular part)
  const transition = Math.max(1, radius * 0.2)
  const arcSweep = Math.max(0.01, Math.abs(totalTurn) - (2 * transition) / radius) * Math.sign(totalTurn)
  // Straight run-in if the spline starts straight-ish relative to chord
  const dirStart: Vec2 = { x: Math.cos(startHeading), y: Math.sin(startHeading) }
  const straightIn = Math.max(0, chord * 0.08)
  if (straightIn > 0.5) out.push({ kind: 'segment', length: straightIn })
  out.push({ kind: 'clothoid', radiusIn: INFINITE_RADIUS, radiusOut: radius, length: transition })
  out.push({ kind: 'arc', radius, angle: arcSweep })
  out.push({ kind: 'clothoid', radiusIn: radius, radiusOut: INFINITE_RADIUS, length: transition })
  return out
}

// ─── Helpers ─────────────────────────────────────────────────────────
export function normalize(v: Vec2): Vec2 {
  const l = Math.hypot(v.x, v.y)
  return l < 1e-9 ? { x: 0, y: 0 } : { x: v.x / l, y: v.y / l }
}

/** Create a bezier connector between two frames (used by Link Tracks). */
export function bezierConnector(from: Frame, to: Frame): BezierFunction {
  const chord = Math.hypot(to.x - from.x, to.y - from.y)
  const d = chord * 0.6
  return {
    kind: 'bezier',
    p0: { x: from.x, y: from.y },
    p1: { x: from.x + Math.cos(from.heading) * d, y: from.y + Math.sin(from.heading) * d },
    p2: { x: to.x - Math.cos(to.heading) * d, y: to.y - Math.sin(to.heading) * d },
    p3: { x: to.x, y: to.y },
  }
}

/** Snap candidate: find closest track endpoint within tolerance. */
export function snapFrame(frames: { frame: Frame; roadId: string; contact: 'start' | 'end' }[], point: Vec2, tolerance = 8) {
  let best: { frame: Frame; roadId: string; contact: 'start' | 'end'; distance: number } | null = null
  for (const item of frames) {
    const distance = Math.hypot(item.frame.x - point.x, item.frame.y - point.y)
    if (!best || distance < best.distance) best = { ...item, distance }
  }
  return best && best.distance <= tolerance ? best : null
}
