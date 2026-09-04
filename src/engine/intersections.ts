// ─────────────────────────────────────────────────────────────────────
// Explicit intersections (SCANeR "Create and edit intersection" +
// "Bifurcations"): insert / detect / link / unlink / align / delete,
// contours, authorizations, implicit & explicit ways, extract ways and
// create interchange.
// ─────────────────────────────────────────────────────────────────────
import type { Vec2 } from './types'
import type { PathSample } from './geometry'
import type { XYFunction } from './xyFunctions'
import type { TrackLike } from './tracks'
import { fitTrackPath, trackStartFrame } from './tracks'

export interface IntersectionTrackEnd {
  trackId: string
  contact: 'start' | 'end'
}

export interface IntersectionData {
  id: string
  position: Vec2
  trackEnds: IntersectionTrackEnd[]
  /** key `${fromTrackId}:${fromContact}->${toTrackId}:${toContact}` → authorized */
  authorizations: Record<string, boolean>
  /** two privileged tracks (Main Path / PatchGridWithMainPath) */
  mainPath?: [string, string]
  /** inserted contour handle points */
  contourHandles: Vec2[]
  contourInterpolation: 'linear' | 'smooth'
  /** Draw markings inside the intersection */
  markings: boolean
  groundName: string
  setSpecificMaterial: boolean
  materialName: string
  uv: { x: number; y: number; offsetX: number; offsetY: number; heading: number }
  /**
   * Explicit ways imported from a road network file (OpenDRIVE). They are
   * stored verbatim and invalidated when connected elements are edited.
   */
  explicitWays?: IntersectionWay[]
}

export function makeIntersectionData(id: string, position: Vec2): IntersectionData {
  return {
    id,
    position: { ...position },
    trackEnds: [],
    authorizations: {},
    contourHandles: [],
    contourInterpolation: 'smooth',
    markings: false,
    groundName: 'Default',
    setSpecificMaterial: false,
    materialName: '',
    uv: { x: 1, y: 1, offsetX: 0, offsetY: 0, heading: 0 },
  }
}

// ─── Track resolver ──────────────────────────────────────────────────
export interface ResolvedTrack {
  track: TrackLike
  path: NonNullable<ReturnType<typeof fitTrackPath>>
  halfWidth: number
}

export function resolveTracks(tracks: TrackLike[]): Map<string, ResolvedTrack> {
  const map = new Map<string, ResolvedTrack>()
  for (const track of tracks) {
    const path = fitTrackPath(track)
    if (!path) continue
    map.set(track.id, {
      track,
      path,
      halfWidth: totalSectionWidth(track) / 2,
    })
  }
  return map
}

function totalSectionWidth(track: TrackLike): number {
  const section = (track as { laneSection?: { left: { width: number }[]; right: { width: number }[] } }).laneSection
  if (section) {
    return section.left.reduce((a, l) => a + l.width, 0) + section.right.reduce((a, l) => a + l.width, 0)
  }
  return (track.lanesLeft + track.lanesRight) * track.laneWidth
}

// ─── Approaches & ways ───────────────────────────────────────────────
export interface IntersectionApproach {
  end: IntersectionTrackEnd
  position: Vec2
  /** heading pointing INTO the track interior (away from the node) */
  heading: number
  incomingLaneCount: number
  outgoingLaneCount: number
  halfWidth: number
}

export interface IntersectionWay {
  key: string
  from: IntersectionTrackEnd
  to: IntersectionTrackEnd
  turn: 'straight' | 'left' | 'right' | 'uturn'
  samples: PathSample[]
  laneCount: number
  laneWidth: number
  authorized: boolean
  /** imported (OpenDRIVE) ways are explicit; computed ways are implicit */
  explicit: boolean
}

export function computeApproaches(intersection: IntersectionData, resolved: Map<string, ResolvedTrack>): IntersectionApproach[] {
  const approaches: IntersectionApproach[] = []
  for (const end of intersection.trackEnds) {
    const item = resolved.get(end.trackId)
    if (!item) continue
    const s = end.contact === 'start' ? 0 : item.path.length
    const first = item.path.elements[0]
    const last = item.path.elements[item.path.elements.length - 1]
    const atStart = end.contact === 'start'
    const el = atStart ? first : last
    if (atStart) {
      approaches.push({
        end,
        position: { x: el.x, y: el.y },
        heading: el.heading,
        incomingLaneCount: laneCountAt(item.track, 'left', atStart),
        outgoingLaneCount: laneCountAt(item.track, 'right', atStart),
        halfWidth: item.halfWidth,
      })
    } else {
      const frame = endFrameOf(item)
      void s
      approaches.push({
        end,
        position: { x: frame.x, y: frame.y },
        heading: frame.heading,
        incomingLaneCount: laneCountAt(item.track, 'right', atStart),
        outgoingLaneCount: laneCountAt(item.track, 'left', atStart),
        halfWidth: item.halfWidth,
      })
    }
  }
  return approaches
}

function endFrameOf(item: ResolvedTrack): { x: number; y: number; heading: number } {
  let el = item.path.elements[item.path.elements.length - 1]
  const sLocal = el.length
  if (el.type === 'line' || el.type === 'arc') {
    if (el.type === 'line') return { x: el.x + Math.cos(el.heading) * sLocal, y: el.y + Math.sin(el.heading) * sLocal, heading: el.heading }
    const h = el.heading + el.curvature * sLocal
    return {
      x: el.x + (Math.sin(h) - Math.sin(el.heading)) / el.curvature,
      y: el.y + (-Math.cos(h) + Math.cos(el.heading)) / el.curvature,
      heading: h,
    }
  }
  // fallback: sample near end via last polyline/spline point
  if (el.points && el.points.length > 0) {
    const p = el.points[el.points.length - 1]
    const prev = el.points.length > 1 ? el.points[el.points.length - 2] : { x: el.x, y: el.y }
    return { x: p.x, y: p.y, heading: Math.atan2(p.y - prev.y, p.x - prev.x) }
  }
  return { x: el.x, y: el.y, heading: el.heading }
}

function laneCountAt(track: TrackLike, side: 'left' | 'right', atStart: boolean): number {
  const section = (track as { laneSection?: { left: { width: number }[]; right: { width: number }[] } }).laneSection
  if (section) return section[side].length
  return side === 'left' ? track.lanesLeft : track.lanesRight
}

export function authorizationKey(from: IntersectionTrackEnd, to: IntersectionTrackEnd): string {
  return `${from.trackId}:${from.contact}->${to.trackId}:${to.contact}`
}

/** Effective authorization of a way, honoring the intersection overrides. */
export function wayAuthorized(way: IntersectionWay, intersection: IntersectionData): boolean {
  return intersection.authorizations[way.key] ?? way.authorized
}

/** All ways of an intersection: imported (explicit) ones first, then implicit. */
export function allWays(intersection: IntersectionData, resolved: Map<string, ResolvedTrack>): IntersectionWay[] {
  const implicit = computeWays(intersection, resolved)
  const explicit = (intersection.explicitWays ?? []).map((way) => ({
    ...way,
    authorized: wayAuthorized(way, intersection),
  }))
  const implicitKeys = new Set(implicit.map((w) => w.key))
  return [...explicit.filter((w) => !implicitKeys.has(w.key)), ...implicit]
}

/** Implicit ways: one per entering/leaving passageway pair (doc 5.5.4.4.12). */
export function computeWays(intersection: IntersectionData, resolved: Map<string, ResolvedTrack>): IntersectionWay[] {
  const approaches = computeApproaches(intersection, resolved)
  if (approaches.length < 2) return []
  const width = averageLaneWidth(resolved, intersection.trackEnds)
  const ways: IntersectionWay[] = []
  for (const from of approaches) {
    if (from.incomingLaneCount === 0) continue
    const fromEnd: IntersectionTrackEnd = from.end
    const fromForward = headingTowardNode(from.position, intersection.position)
    for (const to of approaches) {
      if (to.outgoingLaneCount === 0) continue
      if (from.end.trackId === to.end.trackId && from.end.contact === to.end.contact) continue
      const toForward = headingAwayFromNode(to.position, intersection.position)
      const turn = classifyWayTurn(fromForward, toForward, sameEnd(fromEnd, to.end) && from.end.trackId === to.end.trackId)
      const key = authorizationKey(fromEnd, to.end)
      const authorized = intersection.authorizations[key] ?? true
      const laneCount = Math.max(1, Math.min(from.incomingLaneCount, to.outgoingLaneCount))
      ways.push({
        key,
        from: fromEnd,
        to: to.end,
        turn,
        samples: waySamples(from.position, fromForward, to.position, toForward, intersection.position),
        laneCount,
        laneWidth: width,
        authorized,
        explicit: false,
      })
    }
  }
  return ways
}

function averageLaneWidth(resolved: Map<string, ResolvedTrack>, ends: IntersectionTrackEnd[]): number {
  let total = 0
  let count = 0
  for (const end of ends) {
    const item = resolved.get(end.trackId)
    if (!item) continue
    const section = (item.track as { laneSection?: { left: { width: number }[]; right: { width: number }[] } }).laneSection
    if (section) {
      const lanes = [...section.left, ...section.right]
      if (lanes.length > 0) {
        total += lanes.reduce((a, l) => a + l.width, 0) / lanes.length
        count++
      }
    } else {
      total += item.track.laneWidth
      count++
    }
  }
  return count > 0 ? total / count : 3.5
}

function headingTowardNode(position: Vec2, node: Vec2): number {
  return Math.atan2(node.y - position.y, node.x - position.x)
}

function headingAwayFromNode(position: Vec2, node: Vec2): number {
  return Math.atan2(position.y - node.y, position.x - node.x)
}

function sameEnd(a: IntersectionTrackEnd, b: IntersectionTrackEnd): boolean {
  return a.trackId === b.trackId && a.contact === b.contact
}

function classifyWayTurn(fromForward: number, toForward: number, uturn: boolean): IntersectionWay['turn'] {
  if (uturn) return 'uturn'
  let angle = toForward - fromForward
  while (angle > Math.PI) angle -= Math.PI * 2
  while (angle < -Math.PI) angle += Math.PI * 2
  if (angle > Math.PI / 4) return 'left'
  if (angle < -Math.PI / 4) return 'right'
  return 'straight'
}

/** Cubic bezier path for a way, hugging the node center. */
function waySamples(fromPos: Vec2, fromHeading: number, toPos: Vec2, toHeading: number, node: Vec2): PathSample[] {
  const chord = Math.hypot(toPos.x - fromPos.x, toPos.y - fromPos.y)
  if (chord < 0.05) return []
  const pull = Math.min(0.5, 12 / Math.max(12, chord))
  const c1 = { x: fromPos.x + Math.cos(fromHeading) * chord * pull, y: fromPos.y + Math.sin(fromHeading) * chord * pull }
  const c2 = { x: toPos.x + Math.cos(toHeading) * chord * pull, y: toPos.y + Math.sin(toHeading) * chord * pull }
  const bias = { x: (c1.x + c2.x) / 2 + (node.x - (c1.x + c2.x) / 2) * 0.35, y: (c1.y + c2.y) / 2 + (node.y - (c1.y + c2.y) / 2) * 0.35 }
  const steps = Math.max(8, Math.ceil(chord / 1))
  const samples: PathSample[] = []
  let s = 0
  let prev = fromPos
  for (let i = 0; i <= steps; i++) {
    const t = i / steps
    const u = 1 - t
    const x = u * u * u * fromPos.x + 3 * u * u * t * c1.x + 3 * u * t * t * bias.x + t * t * t * toPos.x
    const y = u * u * u * fromPos.y + 3 * u * u * t * c1.y + 3 * u * t * t * bias.y + t * t * t * toPos.y
    if (i > 0) s += Math.hypot(x - prev.x, y - prev.y)
    const e = 0.01
    const at = (tt: number) => {
      const uu = 1 - tt
      return {
        x: uu * uu * uu * fromPos.x + 3 * uu * uu * tt * c1.x + 3 * uu * tt * tt * bias.x + tt * tt * tt * toPos.x,
        y: uu * uu * uu * fromPos.y + 3 * uu * uu * tt * c1.y + 3 * uu * tt * tt * bias.y + tt * tt * tt * toPos.y,
      }
    }
    const pa = at(Math.max(0, t - e))
    const pb = at(Math.min(1, t + e))
    samples.push({ s, x, y, heading: Math.atan2(pb.y - pa.y, pb.x - pa.x) })
    prev = { x, y }
  }
  return samples
}

// ─── Contours (doc 5.5.4.4.6 / 5.5.4.4.13) ───────────────────────────
export interface IntersectionContour {
  /** polyline between the borders of two adjacent tracks, through the corner */
  points: Vec2[]
  /** corner point (handle attach target) */
  corner: Vec2
  ends: [IntersectionTrackEnd, IntersectionTrackEnd]
}

export function computeContours(intersection: IntersectionData, resolved: Map<string, ResolvedTrack>): IntersectionContour[] {
  const approaches = computeApproaches(intersection, resolved)
  if (approaches.length < 2) return []
  const sorted = [...approaches].sort((a, b) => a.heading - b.heading)
  const contours: IntersectionContour[] = []
  for (let i = 0; i < sorted.length; i++) {
    const a = sorted[i]
    const b = sorted[(i + 1) % sorted.length]
    if (a.end.trackId === b.end.trackId && a.end.contact === b.end.contact) continue
    // outer borders: offset each approach end laterally on the side facing the other
    const corner = borderCorner(a, b, intersection.position)
    if (!corner) continue
    const outerA = offsetEnd(a, b, intersection.position)
    const outerB = offsetEnd(b, a, intersection.position)
    contours.push({
      points: [outerA, corner, outerB],
      corner,
      ends: [a.end, b.end],
    })
  }
  // apply user handles: replace the nearest corner
  if (intersection.contourHandles.length > 0) {
    for (const handle of intersection.contourHandles) {
      let best: IntersectionContour | null = null
      let bestDistance = Number.POSITIVE_INFINITY
      for (const contour of contours) {
        const d = Math.hypot(contour.corner.x - handle.x, contour.corner.y - handle.y)
        if (d < bestDistance) {
          best = contour
          bestDistance = d
        }
      }
      if (best && bestDistance < 30) {
        best.corner = { ...handle }
        best.points[1] = { ...handle }
      }
    }
  }
  return contours
}

function lateralSides(a: IntersectionApproach, b: IntersectionApproach, node: Vec2): [number, number] {
  // +1 = left of forward, -1 = right of forward (facing away from node)
  const cross = relativeCross(a.position, node, b.position)
  const sideA = cross > 0 ? -1 : 1
  const sideB = cross > 0 ? 1 : -1
  return [sideA, sideB]
}

function relativeCross(a: Vec2, node: Vec2, b: Vec2): number {
  const ua: Vec2 = { x: node.x - a.x, y: node.y - a.y }
  const ub: Vec2 = { x: node.x - b.x, y: node.y - b.y }
  return ua.x * ub.y - ua.y * ub.x
}

function offsetEnd(approach: IntersectionApproach, other: IntersectionApproach, node: Vec2): Vec2 {
  const [sideA] = lateralSides(approach, other, node)
  const nx = -Math.sin(approach.heading) * sideA
  const ny = Math.cos(approach.heading) * sideA
  return { x: approach.position.x + nx * approach.halfWidth, y: approach.position.y + ny * approach.halfWidth }
}

function borderCorner(a: IntersectionApproach, b: IntersectionApproach, node: Vec2): Vec2 | null {
  const [sideA, sideB] = lateralSides(a, b, node)
  const na: Vec2 = { x: -Math.sin(a.heading) * sideA, y: Math.cos(a.heading) * sideA }
  const nb: Vec2 = { x: -Math.sin(b.heading) * sideB, y: Math.cos(b.heading) * sideB }
  const p1 = { x: a.position.x + na.x * a.halfWidth, y: a.position.y + na.y * a.halfWidth }
  const p2 = { x: b.position.x + nb.x * b.halfWidth, y: b.position.y + nb.y * b.halfWidth }
  // border lines run from each offset end toward the node
  const d1: Vec2 = { x: node.x - p1.x, y: node.y - p1.y }
  const d2: Vec2 = { x: node.x - p2.x, y: node.y - p2.y }
  const denom = d1.x * d2.y - d1.y * d2.x
  if (Math.abs(denom) < 1e-9) {
    return { x: (p1.x + p2.x) / 2, y: (p1.y + p2.y) / 2 }
  }
  // intersect line p1+d1 with p2+d2
  const t = ((p2.x - p1.x) * d2.y - (p2.y - p1.y) * d2.x) / denom
  return { x: p1.x + d1.x * t, y: p1.y + d1.y * t }
}

// ─── Detect intersection (doc 5.5.4.4.4) ─────────────────────────────
export interface TrackCrossing {
  sA: number
  sB: number
  point: Vec2
  angle: number
}

/** Find where two track axes cross (for Detect Intersection). */
export function findTrackCrossing(a: ResolvedTrack, b: ResolvedTrack): TrackCrossing | null {
  const samplesA = samplePathAxis(a.path, 1.5)
  const samplesB = samplePathAxis(b.path, 1.5)
  let best: TrackCrossing | null = null
  for (let i = 0; i < samplesA.length - 1; i++) {
    for (let j = 0; j < samplesB.length - 1; j++) {
      const hit = segmentHit(samplesA[i], samplesA[i + 1], samplesB[j], samplesB[j + 1])
      if (!hit) continue
      const sA = samplesA[i].s + (samplesA[i + 1].s - samplesA[i].s) * hit.t
      const sB = samplesB[j].s + (samplesB[j + 1].s - samplesB[j].s) * hit.u
      if (sA < 2 || sA > a.path.length - 2 || sB < 2 || sB > b.path.length - 2) continue
      const angle = Math.abs(samplesA[i].heading - samplesB[j].heading)
      if (!best || angle > best.angle) {
        best = { sA, sB, point: hit.point, angle: Math.sin(angle) }
      }
    }
  }
  return best
}

function segmentHit(p1: PathSample, p2: PathSample, p3: PathSample, p4: PathSample) {
  const ax = p2.x - p1.x
  const ay = p2.y - p1.y
  const bx = p4.x - p3.x
  const by = p4.y - p3.y
  const denom = ax * by - ay * bx
  if (Math.abs(denom) < 1e-9) return null
  const t = ((p3.x - p1.x) * by - (p3.y - p1.y) * bx) / denom
  const u = ((p3.x - p1.x) * ay - (p3.y - p1.y) * ax) / denom
  if (t < 0 || t > 1 || u < 0 || u > 1) return null
  return { t, u, point: { x: p1.x + ax * t, y: p1.y + ay * t } }
}

// ─── Extract ways from intersection (doc 5.5.4.5.2.1) ────────────────
export interface ExtractedWayTrack {
  id: string
  name: string
  functions: XYFunction[]
}

export interface ExtractWaysResult {
  wayTracks: ExtractedWayTrack[]
  extremityIntersections: IntersectionData[]
}

/**
 * Create a track from each implicit way of the intersection, plus small
 * extremity intersections between the new tracks and the connected tracks.
 */
export function extractWaysFromIntersection(
  intersection: IntersectionData,
  resolved: Map<string, ResolvedTrack>,
  startIndex: number,
): ExtractWaysResult | null {
  const ways = computeWays(intersection, resolved).filter((way) => way.authorized)
  if (ways.length === 0) return null
  const wayTracks: ExtractedWayTrack[] = []
  const extremityIntersections: IntersectionData[] = []
  let index = startIndex
  for (const way of ways) {
    if (way.samples.length < 2) continue
    const points = way.samples.map((s) => ({ x: s.x, y: s.y }))
    const id = `way-${intersection.id}-${index}`
    wayTracks.push({
      id,
      name: `Way ${index}`,
      functions: [{ kind: 'polyline', points, splineType: 'segment' }],
    })
    // extremity intersections: connect the way track to both source tracks
    for (const [i, end] of [way.from, way.to].entries()) {
      const item = resolved.get(end.trackId)
      if (!item) continue
      const nodePos = i === 0 ? points[0] : points[points.length - 1]
      const existing = extremityIntersections.find(
        (node) => node.trackEnds.some((e) => e.trackId === end.trackId) && Math.hypot(node.position.x - nodePos.x, node.position.y - nodePos.y) < 8,
      )
      if (existing) {
        existing.trackEnds.push({ trackId: id, contact: i === 0 ? 'start' : 'end' })
      } else {
        const node = makeIntersectionData(`${intersection.id}-ext-${index}-${i}`, nodePos)
        node.trackEnds = [
          { trackId: end.trackId, contact: end.contact },
          { trackId: id, contact: i === 0 ? 'start' : 'end' },
        ]
        extremityIntersections.push(node)
      }
    }
    index++
  }
  return { wayTracks, extremityIntersections }
}

// ─── Create interchange / bifurcation (doc 5.5.4.5.2.2) ──────────────
export interface InterchangeResult {
  /** stations where each track must be split */
  mainSplits: number[]
  secondarySplits: number[]
  /** the central intersection node position */
  node: Vec2
  /** authorization defaults: secondary-through is denied */
  deniedKeys: string[]
}

/**
 * Detect the overlapping run of two tracks (entry/exit passageway shape)
 * and compute the split stations + central node for a bifurcation.
 */
export function planInterchange(main: ResolvedTrack, secondary: ResolvedTrack, maxSeparation = 25): InterchangeResult | null {
  const mainSamples = samplePathAxis(main.path, 1)
  const secSamples = samplePathAxis(secondary.path, 1)
  if (mainSamples.length < 4 || secSamples.length < 4) return null
  // for each secondary sample, distance to main axis
  const distances = secSamples.map((sample) => {
    let best = Number.POSITIVE_INFINITY
    let bestIndex = 0
    for (let i = 0; i < mainSamples.length - 1; i++) {
      const a = mainSamples[i]
      const b = mainSamples[i + 1]
      const dx = b.x - a.x
      const dy = b.y - a.y
      const lenSq = dx * dx + dy * dy
      const t = lenSq > 0 ? Math.max(0, Math.min(1, ((sample.x - a.x) * dx + (sample.y - a.y) * dy) / lenSq)) : 0
      const px = a.x + dx * t
      const py = a.y + dy * t
      const d = Math.hypot(sample.x - px, sample.y - py)
      if (d < best) {
        best = d
        bestIndex = i
      }
    }
    return { distance: best, sMain: mainSamples[bestIndex].s + (mainSamples[bestIndex + 1].s - mainSamples[bestIndex].s) * 0.5 }
  })
  // contiguous run where tracks are close
  let runStart = -1
  let runEnd = -1
  let currentStart = -1
  for (let i = 0; i < distances.length; i++) {
    if (distances[i].distance < maxSeparation) {
      if (currentStart < 0) currentStart = i
    } else {
      if (currentStart >= 0 && i - currentStart > (distances.length * 0.2)) {
        runStart = currentStart
        runEnd = i - 1
      }
      currentStart = -1
    }
  }
  if (currentStart >= 0 && distances.length - currentStart > distances.length * 0.2) {
    runStart = currentStart
    runEnd = distances.length - 1
  }
  if (runStart < 0 || runEnd <= runStart + 1) return null
  const secondarySplits = [secSamples[runStart].s, secSamples[runEnd].s]
  const mainSplits = [distances[runStart].sMain, distances[runEnd].sMain]
  const node = {
    x: (secSamples[Math.floor((runStart + runEnd) / 2)].x + mainSamples[Math.floor((runStart + runEnd) / 2) % mainSamples.length].x) / 2,
    y: (secSamples[Math.floor((runStart + runEnd) / 2)].y + mainSamples[Math.floor((runStart + runEnd) / 2) % mainSamples.length].y) / 2,
  }
  return { mainSplits, secondarySplits, node, deniedKeys: [] }
}

function samplePathAxis(path: NonNullable<ReturnType<typeof fitTrackPath>>, ds: number): PathSample[] {
  const out: PathSample[] = []
  let s = 0
  for (const el of path.elements) {
    const steps = Math.max(1, Math.ceil(el.length / ds))
    for (let i = 0; i < steps; i++) {
      const local = (el.length * i) / steps
      out.push({ s: s + local, ...elementSample(el, local) })
    }
    s += el.length
  }
  const lastEl = path.elements[path.elements.length - 1]
  out.push({ s: path.length, ...elementSample(lastEl, lastEl.length) })
  return out
}

function elementSample(el: { type: string; x: number; y: number; heading: number; length: number; curvature: number; points?: Vec2[] }, sLocal: number): { x: number; y: number; heading: number } {
  if (el.type === 'line') return { x: el.x + Math.cos(el.heading) * sLocal, y: el.y + Math.sin(el.heading) * sLocal, heading: el.heading }
  if (el.type === 'arc') {
    const h = el.heading + el.curvature * sLocal
    return {
      x: el.x + (Math.sin(h) - Math.sin(el.heading)) / el.curvature,
      y: el.y + (-Math.cos(h) + Math.cos(el.heading)) / el.curvature,
      heading: h,
    }
  }
  if (el.points && el.points.length > 1) {
    const index = (sLocal / Math.max(1e-9, el.length)) * (el.points.length - 1)
    const i = Math.min(el.points.length - 2, Math.floor(index))
    const f = index - i
    const a = el.points[i]
    const b = el.points[i + 1]
    return { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f, heading: Math.atan2(b.y - a.y, b.x - a.x) }
  }
  return { x: el.x, y: el.y, heading: el.heading }
}

// ─── Sub-network exits (doc 5.5.4.2.16) ──────────────────────────────

/**
 * Build a clothoid spline (or segment+circle) connector between two
 * marked exits. Returns the functions of the connector track.
 */
export function buildExitConnector(
  from: { position: Vec2; heading: number },
  to: { position: Vec2; heading: number },
  method: 'clothoidSpline' | 'segmentCircle',
): XYFunction[] {
  if (method === 'clothoidSpline') {
    const midX = (from.position.x + to.position.x) / 2
    const midY = (from.position.y + to.position.y) / 2
    // control points pulled along the exit headings for tangent continuity
    const chord = Math.hypot(to.position.x - from.position.x, to.position.y - from.position.y)
    const d = chord * 0.4
    const points: Vec2[] = [
      from.position,
      { x: from.position.x + Math.cos(from.heading) * d, y: from.position.y + Math.sin(from.heading) * d },
      { x: midX, y: midY },
      { x: to.position.x + Math.cos(to.heading + Math.PI) * d, y: to.position.y + Math.sin(to.heading + Math.PI) * d },
      to.position,
    ]
    return [{ kind: 'clothoidSpline', points, tolerance: 0.5, symmetryThreshold: 1 }]
  }
  // segment + circle arc: segment along from-heading, circle to to-position
  const toDir = Math.atan2(to.position.y - from.position.y, to.position.x - from.position.x)
  let turn = toDir - from.heading
  while (turn > Math.PI) turn -= Math.PI * 2
  while (turn < -Math.PI) turn += Math.PI * 2
  const dist = Math.hypot(to.position.x - from.position.x, to.position.y - from.position.y)
  const segLen = dist * 0.3
  const chordRest = dist * 0.7
  const radius = Math.abs(chordRest / (2 * Math.sin(Math.abs(turn) / 2) || 1e-6))
  return [
    { kind: 'segment', length: segLen },
    { kind: 'arc', radius: Math.max(5, radius), angle: turn },
  ]
}

/** Collect exit endpoints (position + outward heading) for marked tracks. */
export function exitEndpoint(track: TrackLike, contact: 'start' | 'end'): { position: Vec2; heading: number } | null {
  const start = trackStartFrame(track)
  const path = fitTrackPath(track)
  if (!start || !path) return null
  if (contact === 'start') return { position: { x: start.x, y: start.y }, heading: start.heading }
  const end = endFrameOf({ track, path, halfWidth: 0 })
  return { position: { x: end.x, y: end.y }, heading: end.heading }
}
