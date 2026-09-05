// ─────────────────────────────────────────────────────────────────────
// Track operations for function-based roads (SCANeR "Functions,
// portions and tracks"): chain XY functions into a FittedPath, add
// functions with radius continuity, invert orientation, split
// track/function, merge functions, link tracks, bind tracks and stick
// to background terrain.
// ─────────────────────────────────────────────────────────────────────
import type { FittedPath, PathElement, Vec2 } from './types'
import type { PathSample } from './geometry'
import {
  bezierConnector,
  functionEndFrame,
  functionLength,
  functionRadiusOut,
  invertFunction,
  mergeFunctions as mergeFn,
  sampleFunction,
  splitFunction as splitFn,
  type Frame,
  type XYFunction,
} from './xyFunctions'
import { evaluateElevation, normalizeElevationProfile, type ElevationPoint } from './elevation'

export interface TrackLike {
  id: string
  points: Vec2[]                  // legacy control points (unused when functions set)
  functions?: XYFunction[]
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  elevationProfile?: ElevationPoint[]
}

/** Start frame of a track (position + heading of the first function). */
export function trackStartFrame(track: TrackLike): Frame | null {
  if (track.functions && track.functions.length > 0) {
    const first = track.functions[0]
    if (first.kind === 'polyline' || first.kind === 'clothoidSpline') {
      const pts = first.points
      if (!pts || pts.length < 2) return null
      const heading = first.kind === 'clothoidSpline'
        ? Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x)
        : Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x)
      return { x: pts[0].x, y: pts[0].y, heading }
    }
    // intrinsic functions need explicit start: stored in track.points[0..1]
    const pts = track.points
    if (!pts || pts.length < 2) return null
    return { x: pts[0].x, y: pts[0].y, heading: Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) }
  }
  const pts = track.points
  if (!pts || pts.length < 2) return null
  return { x: pts[0].x, y: pts[0].y, heading: Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) }
}

export function trackFunctions(track: TrackLike): XYFunction[] | null {
  return track.functions && track.functions.length > 0 ? track.functions : null
}

export function trackTotalLength(track: TrackLike): number {
  const fns = trackFunctions(track)
  const start = trackStartFrame(track)
  if (!fns || !start) return 0
  let total = 0
  let frame: Frame = start
  for (const fn of fns) {
    total += functionLength(fn, frame)
    frame = functionEndFrame(frame, fn)
  }
  return total
}

/** Per-function start frames + lengths for selection/editing. */
export interface FunctionSlice {
  index: number
  fn: XYFunction
  start: Frame
  length: number
  /** cumulative station of the function start along the track */
  offset: number
}

export function trackSlices(track: TrackLike): FunctionSlice[] | null {
  const fns = trackFunctions(track)
  const start = trackStartFrame(track)
  if (!fns || !start) return null
  const slices: FunctionSlice[] = []
  let frame: Frame = start
  let offset = 0
  for (let i = 0; i < fns.length; i++) {
    const length = functionLength(fns[i], frame)
    slices.push({ index: i, fn: fns[i], start: { ...frame }, length, offset })
    offset += length
    frame = functionEndFrame(frame, fns[i])
  }
  return slices
}

/** Build a FittedPath from the function chain so all downstream machinery works. */
export function fitTrackPath(track: TrackLike): FittedPath | null {
  const fns = trackFunctions(track)
  const start = trackStartFrame(track)
  if (!fns || !start || fns.length === 0) return null
  const elements: PathElement[] = []
  let frame: Frame = start
  let total = 0
  for (const fn of fns) {
    const length = functionLength(fn, frame)
    if (length <= 1e-6) continue
    switch (fn.kind) {
      case 'segment':
        elements.push({ type: 'line', x: frame.x, y: frame.y, heading: frame.heading, length, curvature: 0 })
        break
      case 'arc':
        elements.push({
          type: 'arc',
          x: frame.x,
          y: frame.y,
          heading: frame.heading,
          length,
          curvature: Math.sign(fn.angle) / fn.radius,
        })
        break
      case 'clothoid': {
        const kIn = fn.radiusIn !== 0 ? 1 / fn.radiusIn : 0
        const kOut = fn.radiusOut !== 0 ? 1 / fn.radiusOut : 0
        elements.push({ type: 'clothoid', x: frame.x, y: frame.y, heading: frame.heading, length, curvature: kIn, curvatureOut: kOut })
        break
      }
      case 'polyline':
        elements.push({ type: 'polyline', x: frame.x, y: frame.y, heading: frame.heading, length, curvature: 0, points: [...fn.points] })
        break
      case 'bezier':
        elements.push({ type: 'bezier', x: frame.x, y: frame.y, heading: frame.heading, length, curvature: 0, p1: fn.p1, p2: fn.p2, points: [fn.p3] })
        break
      case 'clothoidSpline':
        elements.push({ type: 'spline', x: frame.x, y: frame.y, heading: frame.heading, length, curvature: 0, points: [...fn.points] })
        break
    }
    total += length
    frame = functionEndFrame(frame, fn)
  }
  if (elements.length === 0) return null
  return { elements, length: total }
}

// ─── Adding functions (doc 5.5.4.2.10–5.5.4.2.13) ────────────────────

/**
 * Append a function to a track. For clothoids the input radius defaults
 * to the previous function's output radius (radius continuity warning
 * from the doc is honored automatically).
 */
export function appendFunction(track: TrackLike, fn: XYFunction): XYFunction[] {
  const fns = trackFunctions(track) ?? []
  if (fn.kind === 'clothoid' && fn.radiusIn === -1) {
    return [...fns, { ...fn, radiusIn: functionRadiusOut(fns[fns.length - 1]) }]
  }
  return [...fns, fn]
}

export function trackEndpointRadius(track: TrackLike, contact: 'start' | 'end'): number {
  const fns = trackFunctions(track)
  if (!fns) return 0
  return functionRadiusOut(contact === 'start' ? invertFunction(fns[0]) : fns[fns.length - 1])
}

function framePoints(frame: Frame): Vec2[] {
  return [{ x: frame.x, y: frame.y }, { x: frame.x + Math.cos(frame.heading), y: frame.y + Math.sin(frame.heading) }]
}

export function attachTrackFunction(track: TrackLike, fn: XYFunction, contact: 'start' | 'end'): { functions: XYFunction[]; startFrame: Frame } | null {
  const startFrame = trackStartFrame(track)
  if (!trackFunctions(track) || !startFrame) return null
  const attached = fn.kind === 'clothoid' && fn.radiusIn === 0
    ? { ...fn, radiusIn: trackEndpointRadius(track, contact) }
    : fn
  if (contact === 'end') return { functions: appendFunction(track, attached), startFrame }
  // attach at start: invert road, append, invert back
  const inverted = invertTrack(track)
  if (!inverted) return null
  return invertTrack({ ...track, points: framePoints(inverted.startFrame), functions: [...inverted.functions, attached] })
}

export function retargetTrackEnd(
  track: TrackLike,
  contact: 'start' | 'end',
  point: Vec2,
  constraint: string,
  allowHeading: boolean,
): { functions: XYFunction[]; startFrame: Frame } | null {
  const startFrame = trackStartFrame(track)
  const slices = trackSlices(track)
  if (!startFrame || !slices?.length) return null
  if (contact === 'end') {
    const slice = slices[slices.length - 1]
    const functions = [...track.functions!]
    if (functions.length === 1 && slice.fn.kind === 'segment' && allowHeading) {
      return {
        functions: [{ ...slice.fn, length: Math.hypot(point.x - startFrame.x, point.y - startFrame.y) }],
        startFrame: { ...startFrame, heading: Math.atan2(point.y - startFrame.y, point.x - startFrame.x) },
      }
    }
    functions[slice.index] = retargetFunction(slice, point, constraint, allowHeading)
    return { functions, startFrame }
  }
  // contact 'start': work in inverted space, then invert back. The
  // inverted chain starts at the original end frame.
  const inverted = invertTrack(track)
  if (!inverted) return null
  const invTrack = { ...track, points: framePoints(inverted.startFrame), functions: inverted.functions }
  const slicesInv = trackSlices(invTrack)
  if (!slicesInv?.length) return null
  const slice = slicesInv[slicesInv.length - 1]
  const functions = [...inverted.functions]
  if (slice.fn.kind === 'segment' && allowHeading && functions.length > 1) {
    const heading = Math.atan2(point.y - slice.start.y, point.x - slice.start.x)
    functions[slice.index] = bezierConnector(slice.start, { ...point, heading })
  } else if (slice.fn.kind === 'segment' && allowHeading) {
    invTrack.points = framePoints({ ...inverted.startFrame, heading: Math.atan2(point.y - slice.start.y, point.x - slice.start.x) })
    functions[slice.index] = { ...slice.fn, length: Math.hypot(point.x - slice.start.x, point.y - slice.start.y) }
  } else {
    functions[slice.index] = retargetFunction(slice, point, constraint, allowHeading)
  }
  // walk the adjusted inverted chain to find the restored chain's start
  return invertTrack({ ...invTrack, functions })
}

function retargetFunction(
  slice: { fn: XYFunction; start: Frame; length: number },
  point: Vec2,
  constraint: string,
  allowHeading: boolean,
): XYFunction {
  const fn = slice.fn
  const d = Math.hypot(point.x - slice.start.x, point.y - slice.start.y)
  switch (fn.kind) {
    case 'segment': {
      if (!allowHeading) return { ...fn, length: Math.max(0.01, d) } // heading changes need a new direction: keep length
      return { kind: 'polyline', points: [{ x: slice.start.x, y: slice.start.y }, point], splineType: 'segment' }
    }
    case 'arc': {
      const headingToPoint = Math.atan2(point.y - slice.start.y, point.x - slice.start.x)
      let deflection = headingToPoint - slice.start.heading
      while (deflection > Math.PI) deflection -= Math.PI * 2
      while (deflection < -Math.PI) deflection += Math.PI * 2
      if (constraint === 'fixedLength') {
        // solve radius so that chord matches with fixed arc length,
        // then recompute angle so arc length is preserved
        const len = fn.radius * Math.abs(fn.angle)
        const radius = solveRadiusForChord(d, len) ?? fn.radius
        const safeRadius = Math.max(0.01, radius)
        const angle = Math.sign(fn.angle || deflection) * (len / safeRadius)
        return { ...fn, radius: safeRadius, angle }
      }
      // free / fixed radius: solve radius from chord and deflection so
      // the arc endpoint follows the cursor. chord = 2 * r * |sin(deflection)|
      const sinDef = Math.abs(Math.sin(deflection))
      if (sinDef < 1e-4) {
        // nearly straight: fall back to a segment
        return { kind: 'segment', length: Math.max(0.01, d) }
      }
      const radius = Math.max(0.01, d / (2 * sinDef))
      const angle = 2 * deflection
      return { ...fn, radius, angle }
    }
    case 'clothoid': {
      const curLen = fn.length
      const scale = Math.max(0.05, d / Math.max(1, curLen))
      return { ...fn, length: Math.max(0.5, curLen * scale) }
    }
    case 'polyline':
      return { ...fn, points: [...fn.points.slice(0, -1), point] }
    case 'clothoidSpline':
      return { ...fn, points: [...fn.points.slice(0, -1), point] }
    case 'bezier':
      return { ...fn, p3: point }
  }
}

function solveRadiusForChord(chord: number, arcLength: number): number | null {
  // chord = 2 r sin(L / 2r) → solve for r by bisection
  let lo = chord / 2
  let hi = Math.max(chord, arcLength) * 4
  for (let i = 0; i < 60; i++) {
    const mid = (lo + hi) / 2
    const c = 2 * mid * Math.sin(arcLength / (2 * mid))
    if (c < chord) lo = mid
    else hi = mid
  }
  return (lo + hi) / 2
}

// ─── Orientation (doc 5.5.4.3.6) ─────────────────────────────────────

/**
 * Reverse a function chain (flip order, invert each) and re-anchor bezier
 * control points. Bezier control points are absolute coordinates, so an
 * inverted chain would otherwise keep stale p0/p3: re-anchor each bezier's
 * p0 to the walked frame of the new chain and p3 to the original start.
 */
function invertFunctionsAnchored(fns: XYFunction[], start: Frame): XYFunction[] {
  const starts: Frame[] = []
  let frame: Frame = start
  for (const fn of fns) {
    starts.push({ ...frame })
    frame = functionEndFrame(frame, fn)
  }
  const reversed = [...fns].reverse().map(invertFunction)
  let anchor: Frame = { ...frame, heading: frame.heading + Math.PI }
  for (let k = 0; k < reversed.length; k++) {
    const fn = reversed[k]
    if (fn.kind === 'bezier') {
      const originalStart = starts[fns.length - 1 - k]
      reversed[k] = { ...fn, p0: { x: anchor.x, y: anchor.y }, p3: { x: originalStart.x, y: originalStart.y } }
    }
    anchor = functionEndFrame(anchor, reversed[k])
  }
  return reversed
}

/**
 * Invert track orientation: reverse function order and geometry.
 * Returns the new start frame (the original end) so callers can update
 * the track's stored start points.
 */
export function invertTrack(track: TrackLike): { functions: XYFunction[]; startFrame: Frame } | null {
  const fns = trackFunctions(track)
  const start = trackStartFrame(track)
  if (!fns || !start) return null
  const end = walkToEnd(start, fns)
  return {
    functions: invertFunctionsAnchored(fns, start),
    // reversed traversal starts at the old end, pointing back into the track
    startFrame: { ...end, heading: end.heading + Math.PI },
  }
}

/** Function-only inversion (chain start frame unchanged afterwards). */
export function invertTrackFunctions(track: TrackLike): XYFunction[] | null {
  const fns = trackFunctions(track)
  const start = trackStartFrame(track)
  if (!fns || !start) return null
  return invertFunctionsAnchored(fns, start)
}

// ─── Split (doc 5.5.4.3.5 split track / 5.5.4.3.7 split function) ────

export interface TrackSplitResult {
  functionsA: XYFunction[]
  functionsB: XYFunction[]
}

/** Split a track at global station s into two function lists. */
export function splitTrackFunctions(track: TrackLike, s: number): TrackSplitResult | null {
  const fns = trackFunctions(track)
  const start = trackStartFrame(track)
  if (!fns || !start) return null
  let frame = start
  let cursor = 0
  for (let i = 0; i < fns.length; i++) {
    const length = functionLength(fns[i], frame)
    if (s < cursor + length) {
      const local = s - cursor
      const pieces = splitFn(frame, fns[i], local)
      if (!pieces) return null
      return {
        functionsA: [...fns.slice(0, i), pieces[0]],
        functionsB: [pieces[1], ...fns.slice(i + 1)],
      }
    }
    cursor += length
    frame = functionEndFrame(frame, fns[i])
  }
  return null
}

// ─── Merge functions (doc 5.5.4.3.1) ─────────────────────────────────

/** Merge functions[i] and functions[i+1] into one. */
export function mergeFunctionPair(fns: XYFunction[], index: number): XYFunction[] | null {
  if (index < 0 || index + 1 >= fns.length) return null
  const merged = mergeFn(fns[index], fns[index + 1])
  if (!merged) return null
  return [...fns.slice(0, index), merged, ...fns.slice(index + 2)]
}

// ─── Link tracks (doc 5.5.4.3.3) ─────────────────────────────────────

export interface LinkOptions {
  /** which half of A is selected ('start' | 'end' half that should connect) */
  contactA: 'start' | 'end'
  contactB: 'start' | 'end'
}

/**
 * Link two tracks into one: functions of A + bezier connector + functions
 * of B. The first track imposes its direction (doc tip): if A's start half
 * was selected, A is re-oriented so the result runs from A's far end to
 * the connector. B is re-oriented when its end half was selected.
 * Returns the new chain plus its start frame (for the stored points).
 */
export function linkTrackFunctions(
  trackA: TrackLike,
  trackB: TrackLike,
  options: LinkOptions,
): { functions: XYFunction[]; startFrame: Frame } | null {
  const fnsA = trackFunctions(trackA)
  const fnsB = trackFunctions(trackB)
  if (!fnsA || !fnsB) return null
  const startA = trackStartFrame(trackA)
  const startB = trackStartFrame(trackB)
  if (!startA || !startB) return null

  const frameEndA = walkToEnd(startA, fnsA)
  const frameEndB = walkToEnd(startB, fnsB)

  // Contact frames on A: the connector leaves from the selected half.
  const leaveFrame: Frame = options.contactA === 'end'
    ? frameEndA
    : { ...startA, heading: startA.heading + Math.PI }
  // Contact frames on B: the connector enters the selected half; heading
  // must point back into B.
  const enterFrame: Frame = options.contactB === 'start'
    ? startB
    : { ...frameEndB, heading: frameEndB.heading + Math.PI }

  const prefix = options.contactA === 'end' ? fnsA : invertTrackFunctions({ ...trackA, functions: fnsA })!
  const suffix = options.contactB === 'start' ? fnsB : invertTrackFunctions({ ...trackB, functions: fnsB })!
  const connector = bezierConnector(leaveFrame, enterFrame)
  const functions = [...prefix, connector, ...suffix]
  const startFrame = options.contactA === 'end' ? startA : frameEndA
  return { functions, startFrame }
}

function walkToEnd(start: Frame, fns: XYFunction[]): Frame {
  let frame = start
  for (const fn of fns) frame = functionEndFrame(frame, fn)
  return frame
}

// ─── Bind tracks (doc 5.5.4.3.4) ─────────────────────────────────────

export interface BindResult {
  /** transformed functions for the SECOND (moved) track */
  functions: XYFunction[]
  /** transformed start frame for the SECOND track (update its stored points) */
  startFrame: Frame
  translation: Vec2
  rotation: number
}

/**
 * Bind two tracks: rigidly move/rotate the second track so the selected
 * half of its end meets the selected half of the first track.
 * The first track stays at its position (doc tip).
 */
export function bindTrackFunctions(
  trackA: TrackLike,
  trackB: TrackLike,
  options: LinkOptions,
): BindResult | null {
  const fnsB = trackFunctions(trackB)
  const startB = trackStartFrame(trackB)
  if (!fnsB || !startB) return null
  const fnsA = trackFunctions(trackA)
  const startA = trackStartFrame(trackA)
  if (!fnsA || !startA) return null

  const frameEndA = walkToEnd(startA, fnsA)
  const frameEndB = walkToEnd(startB, fnsB)
  const target: Frame = options.contactA === 'end'
    ? frameEndA
    : startA
  const source: Frame = options.contactB === 'end'
    ? frameEndB
    : startB

  const rotation = target.heading - source.heading
  const cos = Math.cos(rotation)
  const sin = Math.sin(rotation)
  // p' = R·(p - source) + target  (rotate B around its contact end, land on target)
  const transform = (p: Vec2): Vec2 => {
    const dx = p.x - source.x
    const dy = p.y - source.y
    return { x: target.x + dx * cos - dy * sin, y: target.y + dx * sin + dy * cos }
  }
  const transformed = fnsB.map((fn) => transformFunctionPoints(fn, transform))
  const startFrame: Frame = {
    x: transform({ x: startB.x, y: startB.y }).x,
    y: transform({ x: startB.x, y: startB.y }).y,
    heading: startB.heading + rotation,
  }
  return { functions: transformed, startFrame, translation: { x: target.x - source.x, y: target.y - source.y }, rotation }
}

function transformFunctionPoints(fn: XYFunction, f: (p: Vec2) => Vec2, rotation = 0): XYFunction {
  switch (fn.kind) {
    case 'polyline':
    case 'clothoidSpline':
      return { ...fn, points: fn.points.map(f) }
    case 'bezier':
      return { ...fn, p1: f(fn.p1), p2: f(fn.p2), p3: f(fn.p3) }
    default:
      void rotation
      return fn // intrinsic functions unchanged (position comes from the chain)
  }
}

// ─── Smoothing (doc 5.5.2.1.13) ──────────────────────────────────────

/**
 * Track smoothing for legacy polyline tracks: one Chaikin corner-cutting
 * pass doubles the control points and rounds every interior corner while
 * keeping the two endpoints fixed (the doc's "track smoothing").
 */
export function smoothPolylinePoints(points: Vec2[], passes = 2): Vec2[] {
  let current = points.map((p) => ({ x: p.x, y: p.y }))
  for (let pass = 0; pass < passes; pass++) {
    if (current.length < 3) return current
    const next: Vec2[] = [current[0]]
    for (let i = 0; i < current.length - 1; i++) {
      const a = current[i]
      const b = current[i + 1]
      next.push({ x: a.x * 0.75 + b.x * 0.25, y: a.y * 0.75 + b.y * 0.25 })
      next.push({ x: a.x * 0.25 + b.x * 0.75, y: a.y * 0.25 + b.y * 0.75 })
    }
    next.push(current[current.length - 1])
    current = next
  }
  return current
}

// ─── Stick to background terrain (doc 5.5.4.3.2) ─────────────────────

export interface StickResult {
  /** altitude profile (station → z in meters) */
  elevation: ElevationPoint[]
  /** banking/cant profile (station → signed angle in radians, positive = left side raised) */
  banking: ElevationPoint[]
}

/**
 * Stick track to Background Terrain: sample the terrain height along the
 * track axis (altitude picking) and across it (banking picking) — the
 * "automatic handles (Altitude and Banking) picking" of the doc.
 */
export function stickTrackToTerrain(
  track: TrackLike,
  terrainHeight: (x: number, y: number) => number | null,
  halfWidth = 2,
): StickResult | null {
  const path = fitTrackPath(track)
  if (!path) return null
  const samples = Math.max(8, Math.ceil(path.length / 5))
  const elevation: ElevationPoint[] = []
  const banking: ElevationPoint[] = []
  let cursor = 0
  let lastZ: number | null = null
  for (const el of path.elements) {
    const steps = Math.max(1, Math.ceil(el.length / (path.length / samples)))
    for (let i = 0; i < steps; i++) {
      const s = cursor + (el.length * i) / steps
      // elementFrame takes a FRACTION of the element, not a station
      const frame = elementFrame(el, el.length > 0 ? i / steps : 0)
      const z = terrainHeight(frame.x, frame.y)
      if (z !== null) {
        lastZ = z
        elevation.push({ s, z })
      }
      // banking picking: cross-slope between the two section edges
      const nx = -Math.sin(frame.heading)
      const ny = Math.cos(frame.heading)
      const zLeft = terrainHeight(frame.x + nx * halfWidth, frame.y + ny * halfWidth)
      const zRight = terrainHeight(frame.x - nx * halfWidth, frame.y - ny * halfWidth)
      if (zLeft !== null && zRight !== null) {
        const width = halfWidth * 2
        banking.push({ s, z: Math.atan2(zLeft - zRight, width) })
      }
    }
    cursor += el.length
  }
  if (lastZ === null || elevation.length < 2) return null
  return {
    elevation: normalizeElevationProfile(elevation, path.length),
    banking: normalizeElevationProfile(banking, path.length),
  }
}

function elementFrame(el: PathElement, t: number): { x: number; y: number; heading: number } {
  if (el.type === 'line' || el.type === 'arc') {
    const s = el.length * t
    if (el.type === 'line') return { x: el.x + Math.cos(el.heading) * s, y: el.y + Math.sin(el.heading) * s, heading: el.heading }
    const k = el.curvature
    const h = el.heading + k * s
    return {
      x: el.x + (Math.sin(h) - Math.sin(el.heading)) / k,
      y: el.y + (-Math.cos(h) + Math.cos(el.heading)) / k,
      heading: h,
    }
  }
  if (el.points && el.points.length > 1) {
    const index = t * (el.points.length - 1)
    const i = Math.min(el.points.length - 2, Math.floor(index))
    const f = index - i
    const x = el.points[i].x + (el.points[i + 1].x - el.points[i].x) * f
    const y = el.points[i].y + (el.points[i + 1].y - el.points[i].y) * f
    const ahead = el.points[Math.min(el.points.length - 1, i + 2)]
    const heading = Math.atan2(ahead.y - el.points[i + 1].y, ahead.x - el.points[i + 1].x)
    return { x, y, heading }
  }
  return { x: el.x, y: el.y, heading: el.heading }
}

/** Evaluate an elevation profile built by stickTrackToTerrain. */
export function trackElevationAt(profile: ElevationPoint[], s: number): number {
  return evaluateElevation(profile, s)
}

// ─── Insert handle (doc tip under 5.5.4.1) ───────────────────────────

/**
 * Insert a handle (control point) into a handle-based function
 * (polyline / clothoidSpline) at global station s.
 */
export function insertHandle(track: TrackLike, functionIndex: number, s: number): XYFunction[] | null {
  const fns = trackFunctions(track)
  if (!fns) return null
  const fn = fns[functionIndex]
  if (!fn || (fn.kind !== 'polyline' && fn.kind !== 'clothoidSpline')) return null
  const split = splitFn({ x: 0, y: 0, heading: 0 }, fn, s)
  if (!split) return null
  // merging the two halves back keeps the inserted vertex as a control point
  if (fn.kind === 'polyline') {
    const points = [...(split[0] as typeof fn).points, ...(split[1] as typeof fn).points.slice(1)]
    const next = [...fns]
    next[functionIndex] = { ...fn, points }
    return next
  }
  const points = [...(split[0] as typeof fn).points, ...(split[1] as typeof fn).points.slice(1)]
  const next = [...fns]
  next[functionIndex] = { ...fn, points }
  return next
}

/** Samples of one function of a track in world space (for axis rendering). */
export function trackFunctionSamples(track: TrackLike, index: number, ds = 1): { samples: PathSample[]; start: Frame } | null {
  const slices = trackSlices(track)
  if (!slices) return null
  const slice = slices[index]
  if (!slice) return null
  return { samples: sampleFunction(slice.start, slice.fn, ds), start: slice.start }
}
