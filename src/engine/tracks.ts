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
  if (!fns) return 0
  return fns.reduce((sum, fn) => sum + functionLength(fn), 0)
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
    const length = functionLength(fns[i])
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
    const length = functionLength(fn)
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

// ─── Orientation (doc 5.5.4.3.6) ─────────────────────────────────────

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
    functions: [...fns].reverse().map(invertFunction),
    // reversed traversal starts at the old end, pointing back into the track
    startFrame: { ...end, heading: end.heading + Math.PI },
  }
}

/** Function-only inversion (chain start frame unchanged afterwards). */
export function invertTrackFunctions(track: TrackLike): XYFunction[] | null {
  const fns = trackFunctions(track)
  if (!fns) return null
  return [...fns].reverse().map(invertFunction)
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
    const length = functionLength(fns[i])
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
      const t = el.length > 0 ? (el.length * i) / steps : 0
      const frame = elementFrame(el, t)
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
