import { evaluatePath, samplePath } from './geometry'
import type { PathSample } from './geometry'
import { fitRoadGeometry } from './roadGeometry'
import type { LaneDef } from './laneTypes'
import type { LaneTaper } from '../domain/road'
import type { FittedPath, Vec2 } from './types'
import type { MeshData } from './mesh'

export interface JunctionRoad {
  id: string
  points: Vec2[]
  geometryType?: 'straight' | 'polyline' | 'arc'
  lanesLeft: number
  lanesRight: number
  laneWidth: number
  filletRadius: number
  /** rich lane section: when present it overrides the legacy counts */
  laneSection?: import('./laneTypes').LaneSectionDef
  /** lane expansion/reduction tapers: change the pavement width actually
   *  reached at the junction cut stations */
  tapers?: LaneTaper[]
}

/** Real per-side lane counts + total widths from the road's lane section. */
export function sectionSides(road: JunctionRoad): {
  leftCount: number; rightCount: number; leftWidth: number; rightWidth: number
} {
  const section = road.laneSection
  if (section) {
    return {
      leftCount: section.left.length,
      rightCount: section.right.length,
      leftWidth: section.left.reduce((a, l) => a + (Number.isFinite(l.width) && l.width > 0 ? l.width : 0), 0),
      rightWidth: section.right.reduce((a, l) => a + (Number.isFinite(l.width) && l.width > 0 ? l.width : 0), 0),
    }
  }
  return {
    leftCount: road.lanesLeft,
    rightCount: road.lanesRight,
    leftWidth: road.lanesLeft * road.laneWidth,
    rightWidth: road.lanesRight * road.laneWidth,
  }
}

export interface RoadCut {
  roadId: string
  sStart: number
  sEnd: number
}

export type ContactPoint = 'start' | 'end'
export type TurningSemantics = 'straight' | 'left' | 'right' | 'uturn'

export interface JunctionLaneConnection {
  fromRoadId: string
  fromContact: ContactPoint
  fromLaneId: number
  toRoadId: string
  toContact: ContactPoint
  toLaneId: number
  enabled: boolean
}

export interface JunctionConfiguration {
  name?: string
  markings?: boolean
  connections?: JunctionLaneConnection[]
}

export interface JunctionLaneEndpoint {
  laneId: number
  name: string
  width: number
  offset: number
}

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
  fromContact?: ContactPoint
  toContact?: ContactPoint
  authorized?: boolean
}

export interface RoadApproach {
  roadId: string
  contact: ContactPoint
  station: number
  position: Vec2
  heading: number
  incomingLaneCount: number
  outgoingLaneCount: number
  /** mean lane width of the incoming/outgoing side (from the lane section) */
  incomingLaneWidth?: number
  outgoingLaneWidth?: number
  incomingLanes?: JunctionLaneEndpoint[]
  outgoingLanes?: JunctionLaneEndpoint[]
  leftWidth?: number
  rightWidth?: number
}

export interface LaneMakerJunction {
  id: string
  key: string
  position: Vec2
  approaches: RoadApproach[]
  connectingRoads: ConnectingRoad[]
  configurationKey?: string
  configuration?: JunctionConfiguration
  connectionOptions?: ConnectingRoad[]
  configurationWarnings?: string[]
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
  lanes: JunctionLaneEndpoint[]
  normal: Vec2
  /** mean lane width of this side (from the lane section) */
  laneWidth: number
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
  configurations: Record<string, JunctionConfiguration> = {},
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
      halfWidth: Math.max(0.5, (() => { const sd = sectionSides(road); return (sd.leftWidth + sd.rightWidth) / 2 })()),
      samples: samplePath(path, 1.5),
    })
  }

  const candidates: { a: number; b: number; sA: number; sB: number; point: Vec2; angle: number; configurationKey: string }[] = []
  for (let a = 0; a < pathRoads.length; a++) {
    for (let b = a + 1; b < pathRoads.length; b++) {
      const ids = [pathRoads[a].road.id, pathRoads[b].road.id].sort()
      const hits = findIntersections(pathRoads[a].samples, pathRoads[b].samples)
        .sort((x, y) => ids[0] === pathRoads[a].road.id ? x.sA - y.sA : x.sB - y.sB)
      const distinct = hits.filter((hit, index) => !hits.slice(0, index).some((item) => distance(item.point, hit.point) < 3))
      distinct.forEach((hit, ordinal) => {
        if (!candidates.some((item) => distance(item.point, hit.point) < 3)) {
          candidates.push({ a, b, ...hit, configurationKey: `${JSON.stringify(ids)}:${ordinal}` })
        }
      })
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
    const configurationKey = candidate.configurationKey
    const configuration = configurations[configurationKey]
    const configurationWarnings: string[] = []
    const connectionOptions = generateConnectingRoads(id, approaches, elevationSamplers, configuration, configurationWarnings)
    junctions.push({
      id,
      key,
      configurationKey,
      configuration,
      configurationWarnings,
      connectionOptions,
      position: candidate.point,
      approaches,
      connectingRoads: suppressed ? [] : connectionOptions.filter((connection) => connection.authorized !== false),
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

function approachLanes(road: JunctionRoad, contact: ContactPoint, incoming: boolean, station: number, roadLength: number): JunctionLaneEndpoint[] {
  const lanes: JunctionLaneEndpoint[] = []
  for (const side of ['left', 'right'] as const) {
    const sign = side === 'left' ? 1 : -1
    const definitions = road.laneSection?.[side]
    const count = definitions?.length ?? (side === 'left' ? road.lanesLeft : road.lanesRight)
    // tapered widths: a lane absent at the junction gets no endpoint and
    // no turning connections, matching the pavement that is actually there
    const tapered = taperedLaneWidths(road, side, station, roadLength)
    let offset = 0
    for (let index = 0; index < count; index++) {
      const lane = definitions?.[index]
      const width = tapered[index] ?? 0
      const validWidth = width > 0.05
      const center = offset + (validWidth ? width / 2 : 0)
      offset += validWidth ? width : 0
      const circulation = lane?.circulation ?? 'forward'
      const normalIncoming = (contact === 'end') === (side === 'right')
      const allowed = circulation === 'both' || (circulation === 'forward' ? normalIncoming === incoming : circulation === 'backward' && normalIncoming !== incoming)
      if (!validWidth || !allowed || (lane && !isConnectionLane(lane))) continue
      lanes.push({ laneId: sign * (index + 1), name: lane?.name || `${side === 'left' ? 'Left' : 'Right'} ${index + 1}`, width, offset: sign * center })
    }
  }
  return lanes.sort((a, b) => Math.abs(a.offset) - Math.abs(b.offset) || a.laneId - b.laneId)
}

function isConnectionLane(lane: LaneDef): boolean {
  return ['travel', 'bus', 'bike', 'paved_major'].includes(lane.type)
}

/** Taper factor per lane index on one side at station s — mirrors the road
 *  mesh exactly: the last taper targeting a lane wins, 0 = lane absent. */
function laneTaperFactors(road: JunctionRoad, side: 'left' | 'right', count: number, s: number, roadLength: number): number[] {
  const factors = Array.from({ length: count }, () => 1)
  for (const taper of road.tapers ?? []) {
    if (taper.side !== side || taper.index >= count) continue
    if (taper.mode === 'in') {
      const start = taper.startS ?? 0
      factors[taper.index] = s <= start ? 0 : clamp((s - start) / Math.max(0.01, taper.length), 0, 1)
    } else {
      const end = taper.endS ?? roadLength
      factors[taper.index] = s >= end ? 0 : clamp((end - s) / Math.max(0.01, taper.length), 0, 1)
    }
  }
  return factors
}

/** Per-lane widths on one side with tapers applied at station s. Lanes
 *  absent at s (fully tapered out) get width 0. */
function taperedLaneWidths(road: JunctionRoad, side: 'left' | 'right', s: number, roadLength: number): number[] {
  const definitions = road.laneSection?.[side]
  const count = definitions?.length ?? (side === 'left' ? road.lanesLeft : road.lanesRight)
  const factors = laneTaperFactors(road, side, count, s, roadLength)
  const widths: number[] = []
  for (let index = 0; index < count; index++) {
    const raw = definitions?.[index]?.width ?? road.laneWidth
    widths.push(Number.isFinite(raw) && raw > 0 ? raw * factors[index] : 0)
  }
  return widths
}

function makeApproaches(pathRoad: PathRoad, cut: RoadCut): RoadApproach[] {
  const approaches: RoadApproach[] = []
  const roadLength = pathRoad.path.length
  for (const contact of ['end', 'start'] as const) {
    const station = contact === 'end' ? cut.sStart : cut.sEnd
    if (contact === 'end' ? station <= 0 : station >= roadLength) continue
    const sample = evaluatePath(pathRoad.path, station)
    const incomingLanes = approachLanes(pathRoad.road, contact, true, station, roadLength)
    const outgoingLanes = approachLanes(pathRoad.road, contact, false, station, roadLength)
    // widths must match the tapered pavement the road mesh actually
    // renders at the cut, so the junction surface meets the road edges
    const leftWidth = taperedLaneWidths(pathRoad.road, 'left', station, roadLength).reduce((a, w) => a + w, 0)
    const rightWidth = taperedLaneWidths(pathRoad.road, 'right', station, roadLength).reduce((a, w) => a + w, 0)
    approaches.push({
      roadId: pathRoad.road.id,
      contact,
      station,
      position: { x: sample.x, y: sample.y },
      heading: sample.heading,
      incomingLanes,
      outgoingLanes,
      incomingLaneCount: incomingLanes.length,
      outgoingLaneCount: outgoingLanes.length,
      incomingLaneWidth: incomingLanes.reduce((sum, lane) => sum + lane.width, 0) / (incomingLanes.length || 1),
      outgoingLaneWidth: outgoingLanes.reduce((sum, lane) => sum + lane.width, 0) / (outgoingLanes.length || 1),
      leftWidth,
      rightWidth,
    })
  }
  return approaches
}

function generateConnectingRoads(
  junctionId: string,
  approaches: RoadApproach[],
  elevationSamplers: Map<string, ElevationSampler>,
  configuration: JunctionConfiguration | undefined,
  warnings: string[],
): ConnectingRoad[] {
  const incoming = approaches
    .filter((approach) => approach.incomingLaneCount > 0)
    .map((approach) => toEndpoint(approach, 'incoming'))
  const outgoing = approaches
    .filter((approach) => approach.outgoingLaneCount > 0)
    .map((approach) => toEndpoint(approach, 'outgoing'))
  if (configuration?.connections !== undefined) {
    const connections: ConnectingRoad[] = []
    const seen = new Set<string>()
    configuration.connections.forEach((row, index) => {
      const label = `Connection ${index + 1}`
      const key = JSON.stringify([row.fromRoadId, row.fromContact, row.fromLaneId, row.toRoadId, row.toContact, row.toLaneId])
      if (seen.has(key)) { warnings.push(`${label}: duplicate lane connection.`); return }
      seen.add(key)
      const from = incoming.find((endpoint) => endpoint.roadId === row.fromRoadId && endpoint.contact === row.fromContact)
      const to = outgoing.find((endpoint) => endpoint.roadId === row.toRoadId && endpoint.contact === row.toContact)
      const fromLane = from?.lanes.find((lane) => lane.laneId === row.fromLaneId)
      const toLane = to?.lanes.find((lane) => lane.laneId === row.toLaneId)
      if (!from || !to || !fromLane || !toLane) {
        warnings.push(`${label}: missing approach or lane, or lane type, circulation or width does not permit this movement.`)
        return
      }
      if (typeof row.enabled !== 'boolean') { warnings.push(`${label}: enabled must be a boolean.`); return }
      const connection = makeLaneConnection(junctionId, index, from, to, fromLane, toLane, row.enabled, elevationSamplers)
      if (connection) connections.push(connection)
      else warnings.push(`${label}: lane endpoints are coincident; no connection geometry could be built.`)
    })
    return connections
  }
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
    for (let lane = 0; lane < group.laneCount; lane++) {
      const fromLane = group.from.lanes[group.fromBase + lane]
      const toLane = group.to.lanes[group.toBase + lane]
      if (!fromLane || !toLane) continue
      const connection = makeLaneConnection(junctionId, connectingRoads.length, group.from, group.to, fromLane, toLane, true, elevationSamplers)
      if (connection) connectingRoads.push(connection)
    }
  }
  return connectingRoads
}

function makeLaneConnection(
  junctionId: string, index: number, from: DirectedEndpoint, to: DirectedEndpoint,
  fromLane: JunctionLaneEndpoint, toLane: JunctionLaneEndpoint, authorized: boolean,
  elevationSamplers: Map<string, ElevationSampler>,
): ConnectingRoad | null {
  const start = laneCenter(from, fromLane)
  const end = laneCenter(to, toLane)
  const zStart = elevationSamplers.get(from.roadId)?.(from.station) ?? 0
  const zEnd = elevationSamplers.get(to.roadId)?.(to.station) ?? 0
  const samples = connectRays(start, from.forward, end, to.forward, zStart, zEnd)
  if (samples.length < 2) return null
  return {
    id: `${junctionId}-connection-${index + 1}`,
    junctionId,
    turn: classifyTurn(from, to),
    laneCount: 1,
    laneWidth: Math.min(fromLane.width, toLane.width),
    samples,
    laneLinks: [{ fromRoadId: from.roadId, fromLaneId: fromLane.laneId, toRoadId: to.roadId, toLaneId: toLane.laneId }],
    fromContact: from.contact,
    toContact: to.contact,
    authorized,
  }
}

function toEndpoint(approach: RoadApproach, direction: 'incoming' | 'outgoing'): DirectedEndpoint {
  const atStart = approach.contact === 'start'
  const incoming = direction === 'incoming'
  const forwardHeading = incoming === atStart ? approach.heading + Math.PI : approach.heading
  const laneSign = atStart ? (incoming ? 1 : -1) : incoming ? -1 : 1
  const laneWidth = (incoming ? approach.incomingLaneWidth : approach.outgoingLaneWidth) ?? 3.5
  const lanes = (incoming ? approach.incomingLanes : approach.outgoingLanes) ?? Array.from(
    { length: incoming ? approach.incomingLaneCount : approach.outgoingLaneCount },
    (_, index) => ({ laneId: laneSign * (index + 1), name: `Lane ${laneSign * (index + 1)}`, width: laneWidth, offset: laneSign * (index + 0.5) * laneWidth }),
  )
  return {
    roadId: approach.roadId,
    contact: approach.contact,
    station: approach.station,
    origin: approach.position,
    forward: { x: Math.cos(forwardHeading), y: Math.sin(forwardHeading) },
    laneCount: lanes.length,
    lanes,
    normal: { x: -Math.sin(approach.heading), y: Math.cos(approach.heading) },
    laneWidth,
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

function laneCenter(endpoint: DirectedEndpoint, lane: JunctionLaneEndpoint): Vec2 {
  return { x: endpoint.origin.x + endpoint.normal.x * lane.offset, y: endpoint.origin.y + endpoint.normal.y * lane.offset }
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

// ─── Unified junction surface ───────────────────────────────────────
//
// Instead of rendering each turning connection as a standalone full-width
// road strip (which produces overlapping star-shaped surfaces in a 4-arm
// junction), build ONE continuous pavement polygon from the approach
// boundaries and triangulate it. Turning centerlines are kept only for
// lane connectivity — they are not rendered as pavement.

/** A 2D point in world space (x, y) with an elevation z. */
interface SurfacePoint {
  x: number
  y: number
  z: number
}

/** Default curb-return radius for junction corners (metres). */
const JUNCTION_CORNER_RADIUS = 6

/** Build the pavement boundary polygon for a junction with rounded
 *  curb-return corners. The polygon traces each approach's road edges
 *  from the cut stations toward the centre; wherever the edges of two
 *  different arms would meet in a sharp point, the corner is extended to
 *  the true curb line intersection and replaced with a tangent arc.
 *  Shared by the 3D pavement mesh and the 2D overlay outline. */
export function junctionSurfaceBoundary(
  junction: LaneMakerJunction,
  cornerRadius = JUNCTION_CORNER_RADIUS,
): Vec2[] {
  if (junction.approaches.length < 2) return []

  // Approach corner points: left (+leftWidth) and right (-rightWidth)
  // road edges at the cut station.
  const corners: { approach: RoadApproach; point: Vec2 }[] = []
  for (const approach of junction.approaches) {
    const leftWidth = approach.leftWidth ?? (approach.outgoingLaneWidth ?? 3.5) * approach.outgoingLaneCount
    const rightWidth = approach.rightWidth ?? (approach.incomingLaneWidth ?? 3.5) * approach.incomingLaneCount
    const px = -Math.sin(approach.heading)
    const py = Math.cos(approach.heading)
    corners.push(
      { approach, point: { x: approach.position.x + px * leftWidth, y: approach.position.y + py * leftWidth } },
      { approach, point: { x: approach.position.x - px * rightWidth, y: approach.position.y - py * rightWidth } },
    )
  }
  if (corners.length < 3) return corners.map((c) => c.point)

  // Order the corners by angle around the junction centre. The two
  // corners of one arm stay adjacent in this order, so a consecutive
  // pair from DIFFERENT arms is exactly a curb corner to round.
  let cx0 = 0, cy0 = 0
  for (const c of corners) { cx0 += c.point.x; cy0 += c.point.y }
  cx0 /= corners.length
  cy0 /= corners.length
  const ordered = corners
    .map((c) => ({ c, angle: Math.atan2(c.point.y - cy0, c.point.x - cx0) }))
    .sort((a, b) => a.angle - b.angle)
    .map((item) => item.c)

  // Remove near-duplicate points (corners from different approaches that
  // land at the same position) to avoid degenerate triangles.
  const deduped: { approach: RoadApproach; point: Vec2 }[] = []
  for (const c of ordered) {
    const last = deduped[deduped.length - 1]
    if (!last || distance(c.point, last.point) > 0.5) deduped.push(c)
  }
  if (deduped.length >= 3 && distance(deduped[0].point, deduped[deduped.length - 1].point) < 0.5) {
    deduped.pop()
  }
  if (deduped.length < 3) return deduped.map((c) => c.point)

  // Trace the boundary, rounding each corner between two different arms.
  const boundary: Vec2[] = []
  const n = deduped.length
  for (let i = 0; i < n; i++) {
    const cur = deduped[i]
    const next = deduped[(i + 1) % n]
    boundary.push(cur.point)
    if (cur.approach === next.approach) continue // arm mouth: straight cap where the road pavement stops
    boundary.push(...cornerFillet(cur.point, cur.approach.heading, next.point, next.approach.heading, cornerRadius))
  }
  return boundary
}

/** Tangent arc points rounding the corner between two road edges. The
 *  edges (defined by an endpoint at the cut station plus the road
 *  heading there) are extended, intersected at the sharp curb corner and
 *  the corner is replaced with a quadratic Bézier tangent to both edges.
 *  Returns [] when the edges are parallel, degenerate or the round-off
 *  would be invisible. */
function cornerFillet(aFrom: Vec2, aHeading: number, bFrom: Vec2, bHeading: number, radius: number): Vec2[] {
  const aDir = { x: Math.cos(aHeading), y: Math.sin(aHeading) }
  const bDir = { x: Math.cos(bHeading), y: Math.sin(bHeading) }
  const corner = lineIntersection(aFrom, aDir, bFrom, bDir)
  if (!corner) return []
  const da = distance(aFrom, corner)
  const db = distance(corner, bFrom)
  if (da < 0.1 || db < 0.1) return []
  const inDir = normalize2({ x: corner.x - aFrom.x, y: corner.y - aFrom.y })
  const outDir = normalize2({ x: bFrom.x - corner.x, y: bFrom.y - corner.y })
  // Deflection angle at the corner; nearly straight corners need no arc
  const cosTurn = clamp(-dot(inDir, outDir), -1, 1)
  const turn = Math.acos(cosTurn)
  if (turn < 0.15) return []
  // Keep the tangent points on the edge segments we actually have
  const tangent = Math.min(radius * Math.tan(turn / 2), 0.8 * da, 0.8 * db)
  if (tangent < 0.25) return []
  const tA = { x: corner.x - inDir.x * tangent, y: corner.y - inDir.y * tangent }
  const tB = { x: corner.x + outDir.x * tangent, y: corner.y + outDir.y * tangent }
  const steps = Math.max(2, Math.min(8, Math.ceil(turn / (Math.PI / 12))))
  const points: Vec2[] = []
  for (let i = 0; i <= steps; i++) {
    points.push(quadBezier(tA, corner, tB, i / steps))
  }
  return points
}

function lineIntersection(pA: Vec2, dirA: Vec2, pB: Vec2, dirB: Vec2): Vec2 | null {
  const denom = dirA.x * dirB.y - dirA.y * dirB.x
  if (Math.abs(denom) < 1e-9) return null
  const dx = pB.x - pA.x
  const dy = pB.y - pA.y
  const t = (dx * dirB.y - dy * dirB.x) / denom
  return { x: pA.x + dirA.x * t, y: pA.y + dirA.y * t }
}

function quadBezier(p0: Vec2, control: Vec2, p1: Vec2, t: number): Vec2 {
  const u = 1 - t
  return {
    x: u * u * p0.x + 2 * u * t * control.x + t * t * p1.x,
    y: u * u * p0.y + 2 * u * t * control.y + t * t * p1.y,
  }
}

function dot(a: Vec2, b: Vec2): number {
  return a.x * b.x + a.y * b.y
}

function normalize2(v: Vec2): Vec2 {
  const len = Math.hypot(v.x, v.y) || 1
  return { x: v.x / len, y: v.y / len }
}

/** Build a unified pavement surface for a junction from its approaches.
 *  Returns a triangulated mesh (positions + indices + colors + UVs) that
 *  covers the junction area as one continuous surface, plus the boundary
 *  polygon for marking placement. */
export function buildJunctionSurface(
  junction: LaneMakerJunction,
  elevationSamplers: Map<string, ElevationSampler>,
): { mesh: MeshData | null; boundary: Vec2[] } {
  if (junction.approaches.length < 2) return { mesh: null, boundary: [] }

  const deduped = junctionSurfaceBoundary(junction)
  if (deduped.length < 3) return { mesh: null, boundary: deduped }

  // Triangulate the hull polygon (fan triangulation for convex polygons)
  const positions: number[] = []
  const colors: number[] = []
  const uvs: number[] = []
  const indices: number[] = []

  // Compute centroid for UV origin and elevation
  let cx = 0
  let cy = 0
  for (const p of deduped) { cx += p.x; cy += p.y }
  cx /= deduped.length
  cy /= deduped.length

  // Sample elevation at the centroid from any available sampler
  let baseZ = 0
  for (const approach of junction.approaches) {
    const sampler = elevationSamplers.get(approach.roadId)
    if (sampler) {
      baseZ = sampler(approach.station)
      break
    }
  }

  // Asphalt color (slightly varied per-vertex for natural look)
  const asphaltColor: [number, number, number] = [0.17, 0.19, 0.22]

  // Add vertices
  for (let i = 0; i < deduped.length; i++) {
    const p = deduped[i]
    // interpolate z toward the centroid (smooth, flat junction surface)
    const z = baseZ + 0.02 // tiny lift above terrain to avoid z-fighting
    positions.push(p.x, z, -p.y)
    colors.push(asphaltColor[0], asphaltColor[1], asphaltColor[2])
    // UV: local coordinates relative to centroid, 6 m per tile
    uvs.push((p.x - cx) / 6, (p.y - cy) / 6)
  }

  // Ear-clipping triangulation (handles non-convex polygons)
  const triIndices = earClip2D(deduped)
  indices.push(...triIndices)

  const mesh: MeshData = {
    positions: new Float32Array(positions),
    colors: new Float32Array(colors),
    indices: new Uint32Array(indices),
    uvs: new Float32Array(uvs),
  }

  return { mesh, boundary: deduped }
}

/** Ear-clipping triangulation for a 2D simple polygon.
 *  Points are in world (x, y) coordinates. Returns triangle indices. */
function earClip2D(points: Vec2[]): number[] {
  const n = points.length
  if (n < 3) return []
  if (n === 3) return [0, 1, 2]

  // Determine winding order (signed area)
  let area = 0
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n
    area += points[i].x * points[j].y - points[j].x * points[i].y
  }
  const ccw = area > 0

  // Build index list
  const indices: number[] = []
  const remaining: number[] = points.map((_, i) => i)

  let guard = 0
  while (remaining.length > 2 && guard < n * n) {
    guard++
    let clipped = false
    for (let i = 0; i < remaining.length; i++) {
      const prev = remaining[(i - 1 + remaining.length) % remaining.length]
      const curr = remaining[i]
      const next = remaining[(i + 1) % remaining.length]

      const a = points[prev]
      const b = points[curr]
      const c = points[next]

      // Cross product to check if curr is a convex vertex
      const cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
      const isConvex = ccw ? cross > 0 : cross < 0
      if (!isConvex) continue

      // Check no other point is inside this triangle
      let inside = false
      for (const idx of remaining) {
        if (idx === prev || idx === curr || idx === next) continue
        if (pointInTriangle(points[idx], a, b, c)) {
          inside = true
          break
        }
      }
      if (inside) continue

      // Clip this ear
      if (ccw) {
        indices.push(prev, curr, next)
      } else {
        indices.push(prev, next, curr)
      }
      remaining.splice(i, 1)
      clipped = true
      break
    }
    if (!clipped) {
      // Fallback: fan triangulation from first vertex
      for (let i = 1; i < remaining.length - 1; i++) {
        if (ccw) indices.push(remaining[0], remaining[i], remaining[i + 1])
        else indices.push(remaining[0], remaining[i + 1], remaining[i])
      }
      break
    }
  }
  return indices
}

function pointInTriangle(p: Vec2, a: Vec2, b: Vec2, c: Vec2): boolean {
  const d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y)
  const d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y)
  const d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y)
  const hasNeg = d1 < 0 || d2 < 0 || d3 < 0
  const hasPos = d1 > 0 || d2 > 0 || d3 > 0
  return !(hasNeg && hasPos)
}

/** Build lane markings for a junction: draw the turning paths as thin
 *  dashed lines on top of the unified surface. Returns a marking mesh
 *  (plain vertex-colored, no asphalt texture). */
export function buildJunctionMarkings(
  junction: LaneMakerJunction,
): MeshData | null {
  if (junction.suppressed || junction.configuration?.markings === false || junction.connectingRoads.length === 0) return null
  const positions: number[] = []
  const colors: number[] = []
  const indices: number[] = []
  let vertexCount = 0

  for (const connection of junction.connectingRoads) {
    // Draw the centerline of each turning path as a thin strip
    if (connection.authorized === false || connection.samples.length < 2) continue
    const lineWidth = 0.1
    const white: [number, number, number] = [0.88, 0.9, 0.93]

    for (let i = 0; i < connection.samples.length - 1; i++) {
      const s0 = connection.samples[i]
      const s1 = connection.samples[i + 1]
      const dx = s1.x - s0.x
      const dy = s1.y - s0.y
      const len = Math.hypot(dx, dy)
      if (len < 0.01) continue
      const nx = -dy / len
      const ny = dx / len
      const z = (s0.z ?? 0) + 0.03 // slightly above pavement

      // quad: s0-left, s0-right, s1-left, s1-right
      positions.push(
        s0.x + nx * lineWidth, z, -(s0.y + ny * lineWidth),
        s0.x - nx * lineWidth, z, -(s0.y - ny * lineWidth),
        s1.x + nx * lineWidth, z, -(s1.y + ny * lineWidth),
        s1.x - nx * lineWidth, z, -(s1.y - ny * lineWidth),
      )
      for (let v = 0; v < 4; v++) colors.push(white[0], white[1], white[2])
      indices.push(vertexCount, vertexCount + 1, vertexCount + 2, vertexCount + 1, vertexCount + 3, vertexCount + 2)
      vertexCount += 4
    }
  }

  if (positions.length === 0) return null
  return {
    positions: new Float32Array(positions),
    colors: new Float32Array(colors),
    indices: new Uint32Array(indices),
  }
}
