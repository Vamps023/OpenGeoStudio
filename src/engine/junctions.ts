import { evaluatePath, samplePath } from './geometry'
import type { PathSample } from './geometry'
import { fitRoadGeometry } from './roadGeometry'
import type { FittedPath, Vec2 } from './types'

export interface JunctionRoad {
  id: string
  points: Vec2[]
  geometryType?: 'straight' | 'polyline' | 'arc'
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
}

export interface RoadCut {
  roadId: string
  sStart: number
  sEnd: number
}

export type ContactPoint = 'start' | 'end'
export type TurningSemantics = 'straight' | 'left' | 'right' | 'uturn'

export interface LaneLink {
  fromRoadId: string
  fromLaneId: number
  toRoadId: string
  toLaneId: number
}

export interface ConnectingRoad {
  id: string
  junctionId: string
  turn: TurningSemantics
  laneCount: number
  laneWidth: number
  samples: PathSample[]
  laneLinks: LaneLink[]
}

export interface RoadApproach {
  roadId: string
  contact: ContactPoint
  station: number
  position: Vec2
  heading: number
  incomingLaneCount: number
  outgoingLaneCount: number
}

export interface LaneMakerJunction {
  id: string
  key: string
  position: Vec2
  approaches: RoadApproach[]
  connectingRoads: ConnectingRoad[]
  suppressed: boolean
}

export type ElevationSampler = (station: number) => number

export interface JunctionNetwork {
  paths: Map<string, FittedPath>
  cuts: RoadCut[]
  junctions: LaneMakerJunction[]
}

interface PathRoad {
  road: JunctionRoad
  path: FittedPath
  halfWidth: number
  samples: PathSample[]
}

interface DirectedEndpoint {
  roadId: string
  contact: ContactPoint
  station: number
  origin: Vec2
  forward: Vec2
  laneCount: number
  laneSign: 1 | -1
}

interface TurningGroup {
  from: DirectedEndpoint
  to: DirectedEndpoint
  turn: TurningSemantics
  fromBase: number
  toBase: number
  laneCount: number
}

const ROAD_MIN_LENGTH = 5
const JUNCTION_TRIM_MIN = 2
const JUNCTION_TRIM_MAX = 6

export function buildJunctionNetwork(
  roads: JunctionRoad[],
  suppressedKeys: string[] = [],
  elevationSamplers: Map<string, ElevationSampler> = new Map(),
): JunctionNetwork {
  const pathRoads: PathRoad[] = []
  const paths = new Map<string, FittedPath>()
  for (const road of roads) {
    const path = fitRoadGeometry(road)
    if (!path) continue
    paths.set(road.id, path)
    pathRoads.push({
      road,
      path,
      halfWidth: ((road.lanesLeft + road.lanesRight) * road.laneWidth) / 2,
      samples: samplePath(path, 1.5),
    })
  }

  const candidates: { a: number; b: number; sA: number; sB: number; point: Vec2; angle: number }[] = []
  for (let a = 0; a < pathRoads.length; a++) {
    for (let b = a + 1; b < pathRoads.length; b++) {
      const hits = findIntersections(pathRoads[a].samples, pathRoads[b].samples)
      for (const hit of hits) {
        if (!candidates.some((item) => distance(item.point, hit.point) < 3)) {
          candidates.push({ a, b, ...hit })
        }
      }
    }
  }

  const cuts: RoadCut[] = []
  const junctions: LaneMakerJunction[] = []

  for (const candidate of candidates) {
    const roadA = pathRoads[candidate.a]
    const roadB = pathRoads[candidate.b]
    const sinAngle = Math.max(0.2, Math.abs(Math.sin(candidate.angle)))
    const overlapA = roadB.halfWidth / sinAngle
    const overlapB = roadA.halfWidth / sinAngle
    const cutA = makeCut(roadA, candidate.sA, overlapA)
    const cutB = makeCut(roadB, candidate.sB, overlapB)
    if (!cutA || !cutB) continue
    const approaches = [...makeApproaches(roadA, cutA), ...makeApproaches(roadB, cutB)]
    if (approaches.length < 3) continue
    const key = [roadA.road.id, roadB.road.id].sort().join('|')
    const suppressed = suppressedKeys.includes(key)
    if (!suppressed) {
      if (cuts.some((cut) => cut.roadId === cutA.roadId && rangesOverlap(cut, cutA))) continue
      if (cuts.some((cut) => cut.roadId === cutB.roadId && rangesOverlap(cut, cutB))) continue
      cuts.push(cutA, cutB)
    }
    const id = `junction-${junctions.length + 1}`
    junctions.push({
      id,
      key,
      position: candidate.point,
      approaches,
      connectingRoads: suppressed ? [] : generateConnectingRoads(id, approaches, pathRoads, elevationSamplers),
      suppressed,
    })
  }

  return { paths, cuts, junctions }
}

export function visibleRoadRanges(path: FittedPath, cuts: RoadCut[]): { sStart: number; sEnd: number }[] {
  if (cuts.length === 0) return [{ sStart: 0, sEnd: path.length }]
  const ordered = cuts
    .map((cut) => ({ sStart: Math.max(0, cut.sStart), sEnd: Math.min(path.length, cut.sEnd) }))
    .sort((a, b) => a.sStart - b.sStart)
  const ranges: { sStart: number; sEnd: number }[] = []
  let cursor = 0
  for (const cut of ordered) {
    if (cut.sStart > cursor) ranges.push({ sStart: cursor, sEnd: cut.sStart })
    cursor = Math.max(cursor, cut.sEnd)
  }
  if (cursor < path.length) ranges.push({ sStart: cursor, sEnd: path.length })
  return ranges.filter((range) => range.sEnd - range.sStart > 0.01)
}

function makeCut(pathRoad: PathRoad, s: number, overlapHalf: number): RoadCut | null {
  const availableBefore = s
  const availableAfter = pathRoad.path.length - s
  const trimBefore = clamp(availableBefore - overlapHalf - ROAD_MIN_LENGTH, JUNCTION_TRIM_MIN, JUNCTION_TRIM_MAX)
  const trimAfter = clamp(availableAfter - overlapHalf - ROAD_MIN_LENGTH, JUNCTION_TRIM_MIN, JUNCTION_TRIM_MAX)
  let sStart = s - overlapHalf - trimBefore
  let sEnd = s + overlapHalf + trimAfter
  if (sStart < ROAD_MIN_LENGTH) sStart = 0
  if (pathRoad.path.length - sEnd < ROAD_MIN_LENGTH) sEnd = pathRoad.path.length
  sStart = clamp(sStart, 0, pathRoad.path.length)
  sEnd = clamp(sEnd, 0, pathRoad.path.length)
  if (sStart === 0 && sEnd === pathRoad.path.length) return null
  return { roadId: pathRoad.road.id, sStart, sEnd }
}

function makeApproaches(pathRoad: PathRoad, cut: RoadCut): RoadApproach[] {
  const approaches: RoadApproach[] = []
  if (cut.sStart > 0) {
    const sample = evaluatePath(pathRoad.path, cut.sStart)
    approaches.push({
      roadId: pathRoad.road.id,
      contact: 'end',
      station: cut.sStart,
      position: { x: sample.x, y: sample.y },
      heading: sample.heading,
      incomingLaneCount: pathRoad.road.lanesRight,
      outgoingLaneCount: pathRoad.road.lanesLeft,
    })
  }
  if (cut.sEnd < pathRoad.path.length) {
    const sample = evaluatePath(pathRoad.path, cut.sEnd)
    approaches.push({
      roadId: pathRoad.road.id,
      contact: 'start',
      station: cut.sEnd,
      position: { x: sample.x, y: sample.y },
      heading: sample.heading,
      incomingLaneCount: pathRoad.road.lanesLeft,
      outgoingLaneCount: pathRoad.road.lanesRight,
    })
  }
  return approaches
}

function generateConnectingRoads(
  junctionId: string,
  approaches: RoadApproach[],
  pathRoads: PathRoad[],
  elevationSamplers: Map<string, ElevationSampler>,
): ConnectingRoad[] {
  const roadMap = new Map(pathRoads.map((item) => [item.road.id, item.road]))
  const incoming = approaches
    .filter((approach) => approach.incomingLaneCount > 0)
    .map((approach) => toEndpoint(approach, 'incoming'))
  const outgoing = approaches
    .filter((approach) => approach.outgoingLaneCount > 0)
    .map((approach) => toEndpoint(approach, 'outgoing'))
  const groups: TurningGroup[] = []

  for (const from of incoming) {
    const destinations = outgoing
      .map((to) => ({ to, turn: classifyTurn(from, to) }))
      .sort((a, b) => turnOrder(a.turn) - turnOrder(b.turn))
    const allocations = allocateIncomingLanes(from.laneCount, destinations)
    destinations.forEach((destination, index) => {
      const allocation = allocations[index]
      if (allocation.count > 0) {
        groups.push({
          from,
          to: destination.to,
          turn: destination.turn,
          fromBase: allocation.base,
          toBase: 0,
          laneCount: allocation.count,
        })
      }
    })
  }

  for (const to of outgoing) {
    const arriving = groups.filter((group) => sameEndpoint(group.to, to))
    assignOutgoingLanes(arriving, to.laneCount)
  }

  const connectingRoads: ConnectingRoad[] = []
  for (const group of groups) {
    const laneWidth = Math.min(
      roadMap.get(group.from.roadId)?.laneWidth ?? 3.5,
      roadMap.get(group.to.roadId)?.laneWidth ?? 3.5,
    )
    const start = laneGroupCenter(group.from, group.fromBase, group.laneCount, laneWidth)
    const end = laneGroupCenter(group.to, group.toBase, group.laneCount, laneWidth)
    const fromSampler = elevationSamplers.get(group.from.roadId)
    const toSampler = elevationSamplers.get(group.to.roadId)
    const zStart = fromSampler ? fromSampler(group.from.station) : 0
    const zEnd = toSampler ? toSampler(group.to.station) : 0
    const samples = connectRays(start, group.from.forward, end, group.to.forward, zStart, zEnd)
    if (samples.length < 2) continue
    const laneLinks: LaneLink[] = []
    for (let lane = 0; lane < group.laneCount; lane++) {
      laneLinks.push({
        fromRoadId: group.from.roadId,
        fromLaneId: group.from.laneSign * (group.fromBase + lane + 1),
        toRoadId: group.to.roadId,
        toLaneId: group.to.laneSign * (group.toBase + lane + 1),
      })
    }
    connectingRoads.push({
      id: `${junctionId}-connection-${connectingRoads.length + 1}`,
      junctionId,
      turn: group.turn,
      laneCount: group.laneCount,
      laneWidth,
      samples,
      laneLinks,
    })
  }
  return connectingRoads
}

function toEndpoint(approach: RoadApproach, direction: 'incoming' | 'outgoing'): DirectedEndpoint {
  const atStart = approach.contact === 'start'
  const incoming = direction === 'incoming'
  const forwardHeading = incoming === atStart ? approach.heading + Math.PI : approach.heading
  return {
    roadId: approach.roadId,
    contact: approach.contact,
    station: approach.station,
    origin: approach.position,
    forward: { x: Math.cos(forwardHeading), y: Math.sin(forwardHeading) },
    laneCount: incoming ? approach.incomingLaneCount : approach.outgoingLaneCount,
    laneSign: atStart ? (incoming ? 1 : -1) : incoming ? -1 : 1,
  }
}

function classifyTurn(from: DirectedEndpoint, to: DirectedEndpoint): TurningSemantics {
  if (from.roadId === to.roadId && from.contact === to.contact) return 'uturn'
  const angle = signedAngle(from.forward, to.forward)
  if (angle > Math.PI / 4) return 'left'
  if (angle < -Math.PI / 4) return 'right'
  return 'straight'
}

function allocateIncomingLanes(
  laneCount: number,
  destinations: { to: DirectedEndpoint; turn: TurningSemantics }[],
): { base: number; count: number }[] {
  if (destinations.length === 0 || laneCount === 0) return []
  const semanticsWeight = { straight: 1, uturn: 0.01, left: 0.99, right: 0.5 }
  const weights = destinations.map((item) => item.to.laneCount * semanticsWeight[item.turn])
  const totalWeight = weights.reduce((sum, value) => sum + value, 0)
  const splitPoints: number[] = []
  let cumulative = 0
  for (let i = 0; i < weights.length - 1; i++) {
    cumulative += (weights[i] / totalWeight) * laneCount
    splitPoints.push(cumulative)
  }
  const boundaries = [0, ...splitPoints, laneCount]
  return destinations.map((destination, index) => {
    const begin = nearInteger(boundaries[index]) ? Math.round(boundaries[index]) : Math.floor(boundaries[index])
    const end = nearInteger(boundaries[index + 1]) ? Math.round(boundaries[index + 1]) : Math.ceil(boundaries[index + 1])
    const count = Math.min(destination.to.laneCount, Math.max(0, end - begin))
    return { base: Math.min(begin, Math.max(0, laneCount - count)), count }
  })
}

function assignOutgoingLanes(groups: TurningGroup[], totalLanes: number) {
  let leftUsed = 0
  let rightUsed = 0
  for (const group of groups) {
    if (group.turn === 'left' || group.turn === 'uturn') leftUsed = Math.max(leftUsed, group.laneCount)
    if (group.turn === 'right') rightUsed = Math.max(rightUsed, group.laneCount)
  }
  for (const group of groups) {
    if (group.turn === 'left' || group.turn === 'uturn') {
      group.toBase = 0
    } else if (group.turn === 'right') {
      group.toBase = Math.max(0, totalLanes - group.laneCount)
    } else if (leftUsed + rightUsed + group.laneCount <= totalLanes) {
      group.toBase = leftUsed
    } else if (leftUsed + group.laneCount >= totalLanes) {
      group.toBase = Math.max(0, totalLanes - group.laneCount)
    } else {
      group.toBase = 0
    }
  }
}

function sameEndpoint(a: DirectedEndpoint, b: DirectedEndpoint): boolean {
  return a.roadId === b.roadId && a.contact === b.contact
}

function nearInteger(value: number): boolean {
  return Math.abs(value - Math.round(value)) < 1e-4
}

function laneGroupCenter(endpoint: DirectedEndpoint, base: number, count: number, width: number): Vec2 {
  const right = { x: endpoint.forward.y, y: -endpoint.forward.x }
  const offset = (base + count / 2) * width
  return { x: endpoint.origin.x + right.x * offset, y: endpoint.origin.y + right.y * offset }
}

function connectRays(start: Vec2, startDir: Vec2, end: Vec2, endDir: Vec2, zStart: number, zEnd: number): PathSample[] {
  const chord = distance(start, end)
  if (chord < 0.05) return []
  const intersection = rayIntersection(start, startDir, end, { x: -endDir.x, y: -endDir.y })
  let c1: Vec2
  let c2: Vec2
  if (intersection && distance(start, intersection) < chord * 5) {
    c1 = intersection
    c2 = intersection
  } else {
    c1 = { x: start.x + startDir.x * chord * 0.5, y: start.y + startDir.y * chord * 0.5 }
    c2 = { x: end.x - endDir.x * chord * 0.5, y: end.y - endDir.y * chord * 0.5 }
  }
  const steps = Math.max(8, Math.ceil(chord / 0.75))
  const samples: PathSample[] = []
  let s = 0
  let previous = start
  for (let i = 0; i <= steps; i++) {
    const t = i / steps
    const point = cubicBezier(start, c1, c2, end, t)
    const tangent = cubicBezierTangent(start, c1, c2, end, t)
    if (i > 0) s += distance(previous, point)
    const z = zStart + (zEnd - zStart) * t
    samples.push({ s, x: point.x, y: point.y, z, heading: Math.atan2(tangent.y, tangent.x) })
    previous = point
  }
  return samples
}

function findIntersections(a: PathSample[], b: PathSample[]) {
  const hits: { sA: number; sB: number; point: Vec2; angle: number }[] = []
  for (let i = 0; i < a.length - 1; i++) {
    for (let j = 0; j < b.length - 1; j++) {
      const hit = segmentIntersection(a[i], a[i + 1], b[j], b[j + 1])
      if (!hit) continue
      const sA = a[i].s + (a[i + 1].s - a[i].s) * hit.t
      const sB = b[j].s + (b[j + 1].s - b[j].s) * hit.u
      const angle = signedAngle(
        { x: Math.cos(a[i].heading), y: Math.sin(a[i].heading) },
        { x: Math.cos(b[j].heading), y: Math.sin(b[j].heading) },
      )
      hits.push({ sA, sB, point: hit.point, angle })
    }
  }
  return hits
}

function segmentIntersection(p1: Vec2, p2: Vec2, p3: Vec2, p4: Vec2) {
  const ax = p2.x - p1.x
  const ay = p2.y - p1.y
  const bx = p4.x - p3.x
  const by = p4.y - p3.y
  const denominator = ax * by - ay * bx
  if (Math.abs(denominator) < 1e-9) return null
  const t = ((p3.x - p1.x) * by - (p3.y - p1.y) * bx) / denominator
  const u = ((p3.x - p1.x) * ay - (p3.y - p1.y) * ax) / denominator
  if (t < 0 || t > 1 || u < 0 || u > 1) return null
  return { t, u, point: { x: p1.x + ax * t, y: p1.y + ay * t } }
}

function rayIntersection(originA: Vec2, dirA: Vec2, originB: Vec2, dirB: Vec2): Vec2 | null {
  const denominator = dirA.x * dirB.y - dirA.y * dirB.x
  if (Math.abs(denominator) < 1e-9) return null
  const dx = originB.x - originA.x
  const dy = originB.y - originA.y
  const t = (dx * dirB.y - dy * dirB.x) / denominator
  const u = (dx * dirA.y - dy * dirA.x) / denominator
  if (t < 0 || u < 0) return null
  return { x: originA.x + dirA.x * t, y: originA.y + dirA.y * t }
}

function cubicBezier(p0: Vec2, p1: Vec2, p2: Vec2, p3: Vec2, t: number): Vec2 {
  const u = 1 - t
  return {
    x: u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
    y: u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y,
  }
}

function cubicBezierTangent(p0: Vec2, p1: Vec2, p2: Vec2, p3: Vec2, t: number): Vec2 {
  const u = 1 - t
  const x = 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x)
  const y = 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y)
  const magnitude = Math.hypot(x, y) || 1
  return { x: x / magnitude, y: y / magnitude }
}

function signedAngle(a: Vec2, b: Vec2): number {
  return Math.atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y)
}

function turnOrder(turn: TurningSemantics): number {
  return { uturn: 0, left: 1, straight: 2, right: 3 }[turn]
}

function rangesOverlap(a: RoadCut, b: RoadCut): boolean {
  return a.sStart < b.sEnd && b.sStart < a.sEnd
}

function distance(a: Vec2, b: Vec2): number {
  return Math.hypot(a.x - b.x, a.y - b.y)
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value))
}
