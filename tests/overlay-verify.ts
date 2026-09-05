// Reproduce the app's overlay data pipeline: detect intersection on two
// crossing segment roads, then buildOverlays → count way lines/markers.
import { splitTrackFunctions, fitTrackPath, trackSlices, trackStartFrame, attachTrackFunction, retargetTrackEnd, trackEndpointRadius } from '../src/engine/tracks'
import { findTrackCrossing, makeIntersectionData, resolveTracks, allWays } from '../src/engine/intersections'
import { buildOverlays } from '../src/roads/overlays'
import { evaluatePath } from '../src/engine/geometry'
import { functionEndFrame, functionRadiusOut } from '../src/engine/xyFunctions'
import type { Frame } from '../src/engine/xyFunctions'
import type { RoadData } from '../src/state/store'
import { buildRoadMeshFromSamples } from '../src/engine/mesh'
import type { MeshData } from '../src/engine/mesh'
import { defaultTravelLane } from '../src/engine/laneLayout'
import type { LaneMarking, MarkingStyle } from '../src/engine/laneTypes'

let failures = 0
function check(name: string, cond: boolean, detail = '') {
  if (!cond) { failures++; console.log('FAIL', name, detail) } else console.log('ok  ', name)
}

function makeSeg(id: string, from: [number, number], to: [number, number]): RoadData {
  const heading = Math.atan2(to[1] - from[1], to[0] - from[0])
  return {
    id, name: id,
    points: [{ x: from[0], y: from[1] }, { x: from[0] + Math.cos(heading), y: from[1] + Math.sin(heading) }],
    functions: [{ kind: 'segment', length: Math.hypot(to[0] - from[0], to[1] - from[1]) }],
    lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0,
  }
}

function chainEndFrame(road: RoadData, functions: import('../src/engine/xyFunctions').XYFunction[]): Frame | null {
  const start = trackStartFrame(road)
  if (!start) return null
  let frame: Frame = start
  for (const fn of functions) frame = functionEndFrame(frame, fn)
  return frame
}

const roadA = makeSeg('A', [0, 0], [100, 0])
const roadB = makeSeg('B', [50, -60], [50, 60])
const resolved0 = resolveTracks([roadA, roadB])
const crossing = findTrackCrossing(resolved0.get('A')!, resolved0.get('B')!)
check('crossing found', !!crossing)

const piecesA = splitTrackFunctions(roadA, crossing!.sA)!
const piecesB = splitTrackFunctions(roadB, crossing!.sB)!
const frameA = chainEndFrame(roadA, piecesA.functionsA)!
const frameB = chainEndFrame(roadB, piecesB.functionsA)!
// trim the arms back from the crossing like detectIntersectionFromSelection
function trim(road: RoadData, contact: 'start' | 'end', t: number): RoadData {
  const len = fitTrackPath(road)!.length
  if (contact === 'end') return { ...road, functions: splitTrackFunctions(road, len - t)!.functionsA }
  const pieces = splitTrackFunctions(road, t)!
  let frame: Frame = trackStartFrame(road)!
  for (const fn of pieces.functionsA) frame = functionEndFrame(frame, fn)
  return { ...road, functions: pieces.functionsB, points: [{ x: frame.x, y: frame.y }, { x: frame.x + Math.cos(frame.heading), y: frame.y + Math.sin(frame.heading) }] }
}
const sinAngle = Math.max(0.2, crossing!.angle)
const trimA = 3.5 / sinAngle + 3
const a1 = trim({ ...roadA, id: 'A1', name: 'A1', functions: piecesA.functionsA }, 'end', trimA)
const a2 = trim({ ...roadA, id: 'A2', name: 'A2', points: [{ x: frameA.x, y: frameA.y }, { x: frameA.x + Math.cos(frameA.heading), y: frameA.y + Math.sin(frameA.heading) }], functions: piecesA.functionsB }, 'start', trimA)
const b1 = trim({ ...roadB, id: 'B1', name: 'B1', functions: piecesB.functionsA }, 'end', trimA)
const b2 = trim({ ...roadB, id: 'B2', name: 'B2', points: [{ x: frameB.x, y: frameB.y }, { x: frameB.x + Math.cos(frameB.heading), y: frameB.y + Math.sin(frameB.heading) }], functions: piecesB.functionsB }, 'start', trimA)

const node = makeIntersectionData('n1', crossing!.point)
node.trackEnds = [
  { trackId: 'A1', contact: 'end' },
  { trackId: 'A2', contact: 'start' },
  { trackId: 'B1', contact: 'end' },
  { trackId: 'B2', contact: 'start' },
]

const roads = [a1, a2, b1, b2]
const resolved = resolveTracks(roads)
check('4 arms resolve', resolved.size === 4)

const ways = allWays(node, resolved)
check('ways computed', ways.length >= 4, `${ways.length}`)
check('way samples non-empty', ways.every((w) => w.samples.length >= 2), ways.map((w) => w.samples.length).join(','))
const sampleLen = ways[0].samples[ways[0].samples.length - 1].s
check('way paths span the trimmed interior', sampleLen > 5, `${sampleLen.toFixed(1)} m`)

const overlays = buildOverlays({
  project: {
    id: 'p', name: 'p', createdAt: '', roads,
    suppressedJunctions: [], intersections: [node],
  },
  layers: {
    roadLogicalContent: true, road3dGeneration: true,
    intersectionLogicalContent: true, intersection3dGeneration: true,
    wayAxis: true, wayLogicalContents: true, otherSubNetworks: true,
  },
  selection: { trackIds: [], intersectionId: 'n1' },
  lockedPassageways: [],
  selectedTrackStation: null,
})
const wayLines = overlays.lines.filter((l) => l.wayKey)
check('overlay way lines rendered', wayLines.length === ways.length, `${wayLines.length} vs ${ways.length}`)
check('overlay arrows rendered (auth + circulation)', overlays.markers.filter((m) => m.shape === 'arrow').length >= ways.length, `${overlays.markers.length}`)

check('right-turn arc reports signed output radius', functionRadiusOut({ kind: 'arc', radius: 40, angle: -0.7 }) === -40)
check('left-turn arc reports signed output radius', functionRadiusOut({ kind: 'arc', radius: 40, angle: 0.7 }) === 40)

function samePoint(a: { x: number; y: number }, b: { x: number; y: number }, tolerance = 1e-6) {
  return Math.hypot(a.x - b.x, a.y - b.y) < tolerance
}

function endpointRoad(road: RoadData, result: { functions: import('../src/engine/xyFunctions').XYFunction[]; startFrame: Frame }): RoadData {
  const frame = result.startFrame
  return { ...road, functions: result.functions, points: [{ x: frame.x, y: frame.y }, { x: frame.x + Math.cos(frame.heading), y: frame.y + Math.sin(frame.heading) }] }
}

const endpointBase = makeSeg('endpoint', [10, 20], [110, 20])
const beforeEndpoint = JSON.stringify(endpointBase)
const prepended = endpointRoad(endpointBase, attachTrackFunction(endpointBase, { kind: 'segment', length: 25 }, 'start')!)
check('prepend moves start anchor outward', samePoint(trackStartFrame(prepended)!, { x: -15, y: 20 }))
check('prepend keeps original end', samePoint(chainEndFrame(prepended, prepended.functions!)!, { x: 110, y: 20 }))
check('prepend leaves original geometry anchored', samePoint(trackSlices(prepended)![1].start, trackStartFrame(endpointBase)!))
const appended = endpointRoad(endpointBase, attachTrackFunction(endpointBase, { kind: 'segment', length: 25 }, 'end')!)
check('append keeps start and extends end', samePoint(trackStartFrame(appended)!, { x: 10, y: 20 }) && samePoint(chainEndFrame(appended, appended.functions!)!, { x: 135, y: 20 }))

const curvedBase: RoadData = {
  ...makeSeg('curved', [10, 20], [11, 20]),
  functions: [{ kind: 'arc', radius: 30, angle: -0.6 }],
}
const arcEnd = chainEndFrame(curvedBase, curvedBase.functions!)!
curvedBase.functions!.push({
  kind: 'bezier', p0: { x: -999, y: -999 },
  p1: { x: arcEnd.x + 15 * Math.cos(arcEnd.heading), y: arcEnd.y + 15 * Math.sin(arcEnd.heading) },
  p2: { x: 65, y: 8 }, p3: { x: 80, y: 8 },
}, { kind: 'bezier', p1: { x: 95, y: 8 }, p2: { x: 105, y: 12 }, p3: { x: 120, y: 12 } }, { kind: 'segment', length: 20 })
const curvedBefore = JSON.stringify(curvedBase)
const curvedPrepended = endpointRoad(curvedBase, attachTrackFunction(curvedBase, { kind: 'arc', radius: 25, angle: 0.4 }, 'start')!)
const originalSlices = trackSlices(curvedBase)!
const restoredSlices = trackSlices(curvedPrepended)!.slice(1)
check('curved and legacy Bezier prepend preserves original end', samePoint(chainEndFrame(curvedPrepended, curvedPrepended.functions!)!, chainEndFrame(curvedBase, curvedBase.functions!)!))
check('curved and Bezier prepend preserves every original slice anchor', restoredSlices.every((slice, index) => samePoint(slice.start, originalSlices[index].start)))
check('curved and Bezier prepend preserves sampled geometry', originalSlices.every((slice, index) => {
  const before = fitTrackPath(endpointRoad(curvedBase, { functions: [slice.fn], startFrame: slice.start }))!
  const after = fitTrackPath(endpointRoad(curvedBase, { functions: [restoredSlices[index].fn], startFrame: restoredSlices[index].start }))!
  return [0, 0.25, 0.5, 0.75, 1].every((t) => samePoint(evaluatePath(before, before.length * t), evaluatePath(after, after.length * t)))
}))

for (const contact of ['start', 'end'] as const) {
  const target = contact === 'start' ? { x: -15, y: 45 } : { x: 145, y: 65 }
  const moved = endpointRoad(endpointBase, retargetTrackEnd(endpointBase, contact, target, 'free', true)!)
  check(`move ${contact} reaches off-axis target`, samePoint(contact === 'start' ? trackStartFrame(moved)! : chainEndFrame(moved, moved.functions!)!, target))
  check(`move ${contact} preserves opposite endpoint`, samePoint(contact === 'start' ? chainEndFrame(moved, moved.functions!)! : trackStartFrame(moved)!, contact === 'start' ? { x: 110, y: 20 } : { x: 10, y: 20 }))
  const extended = endpointRoad(endpointBase, retargetTrackEnd(endpointBase, contact, contact === 'start' ? { x: -15, y: 20 } : { x: 135, y: 20 }, 'free', false)!)
  check(`extend ${contact} keeps opposite endpoint`, samePoint(contact === 'start' ? chainEndFrame(extended, extended.functions!)! : trackStartFrame(extended)!, contact === 'start' ? { x: 110, y: 20 } : { x: 10, y: 20 }))
}
const multiBase: RoadData = { ...endpointBase, functions: [{ kind: 'segment', length: 40 }, { kind: 'arc', radius: 30, angle: -0.6 }, { kind: 'segment', length: 20 }] }
for (const contact of ['start', 'end'] as const) {
  const target = contact === 'start' ? { x: -20, y: 35 } : { x: 125, y: 55 }
  const moved = endpointRoad(multiBase, retargetTrackEnd(multiBase, contact, target, 'free', true)!)
  check(`multi-function move ${contact} reaches target`, samePoint(contact === 'start' ? trackStartFrame(moved)! : chainEndFrame(moved, moved.functions!)!, target))
  const oldSlices = trackSlices(multiBase)!
  const newSlices = trackSlices(moved)!
  check(`multi-function move ${contact} preserves untouched curve`, samePoint(oldSlices[1].start, newSlices[1].start) && samePoint(functionEndFrame(oldSlices[1].start, oldSlices[1].fn), functionEndFrame(newSlices[1].start, newSlices[1].fn)))
  check(`multi-function move ${contact} keeps opposite endpoint`, samePoint(contact === 'start' ? chainEndFrame(moved, moved.functions!)! : trackStartFrame(moved)!, contact === 'start' ? chainEndFrame(multiBase, multiBase.functions!)! : trackStartFrame(multiBase)!))
}
const bezierMoved = endpointRoad(curvedBase, retargetTrackEnd(curvedBase, 'start', { x: -5, y: 20 }, 'free', false)!)
check('start retarget preserves distant Bezier end', samePoint(chainEndFrame(bezierMoved, bezierMoved.functions!)!, chainEndFrame(curvedBase, curvedBase.functions!)!))
const radiiBase: RoadData = { ...endpointBase, functions: [{ kind: 'arc', radius: 40, angle: -0.5 }, { kind: 'clothoid', radiusIn: -40, radiusOut: 80, length: 15 }] }
check('start continuity uses reversed first radius, not last function', trackEndpointRadius(radiiBase, 'start') === 40)
check('end continuity uses last output radius', trackEndpointRadius(radiiBase, 'end') === 80)
check('left-turn start reverses curvature sign', trackEndpointRadius({ ...endpointBase, functions: [{ kind: 'arc', radius: 40, angle: 0.5 }] }, 'start') === -40)
check('clothoid start uses negated input rather than output radius', trackEndpointRadius({ ...endpointBase, functions: [{ kind: 'clothoid', radiusIn: -35, radiusOut: 75, length: 20 }] }, 'start') === 35)
const rightArcContinuation = attachTrackFunction({ ...endpointBase, functions: [{ kind: 'arc', radius: 40, angle: -0.5 }] }, { kind: 'clothoid', radiusIn: 0, radiusOut: 0, length: 12 }, 'end')!.functions[1]
check('clothoid appended to right arc keeps right-turn input curvature', rightArcContinuation.kind === 'clothoid' && rightArcContinuation.radiusIn === -40)
for (const contact of ['start', 'end'] as const) {
  const continued = endpointRoad(radiiBase, attachTrackFunction(radiiBase, { kind: 'clothoid', radiusIn: 0, radiusOut: -60, length: 12 }, contact)!)
  const fn = contact === 'start' ? continued.functions![0] : continued.functions![continued.functions!.length - 1]
  check(`clothoid ${contact} attachment preserves signed curvature continuity`, fn.kind === 'clothoid' && (contact === 'start' ? fn.radiusOut === -40 : fn.radiusIn === 80))
}
check('endpoint operations do not mutate original roads', JSON.stringify(endpointBase) === beforeEndpoint && JSON.stringify(curvedBase) === curvedBefore)

const markingSamples = [0, 20].map((s) => ({ s, x: s, y: 0, heading: 0 }))
const markingLane = defaultTravelLane()
function markedRoad(marking: LaneMarking, markingStyle: MarkingStyle = {}, samples = markingSamples) {
  return buildRoadMeshFromSamples(samples, { left: [], right: [{ ...markingLane, marking, markingStyle }] })
}
function markingArea(mesh: MeshData | null) {
  if (!mesh) return 0
  let area = 0
  for (let i = 0; i < mesh.indices.length; i += 3) {
    const [a, b, c] = Array.from(mesh.indices.slice(i, i + 3), (v) => v * 3)
    area += Math.abs((mesh.positions[b] - mesh.positions[a]) * (mesh.positions[c + 2] - mesh.positions[a + 2])
      - (mesh.positions[c] - mesh.positions[a]) * (mesh.positions[b + 2] - mesh.positions[a + 2])) / 2
  }
  return area
}
function validMarking(mesh: MeshData | null) {
  if (!mesh) return true
  return [mesh.positions, mesh.colors, mesh.uvs ?? []].every((values) => Array.from(values).every(Number.isFinite))
    && Array.from(mesh.indices).every((i) => i < mesh.positions.length / 3)
    && Array.from({ length: mesh.indices.length / 3 }, (_, i) => {
      const [a, b, c] = Array.from(mesh.indices.slice(i * 3, i * 3 + 3), (v) => v * 3)
      return Math.abs((mesh.positions[b] - mesh.positions[a]) * (mesh.positions[c + 2] - mesh.positions[a + 2])
        - (mesh.positions[c] - mesh.positions[a]) * (mesh.positions[b + 2] - mesh.positions[a + 2])) > 1e-10
    }).every(Boolean)
}
const noMarking = markedRoad('none')
const solidMarking = markedRoad('solid')
const dashedMarking = markedRoad('dashed')
const doubleMarking = markedRoad('double-solid')
const solidDashedMarking = markedRoad('solid-dashed')
const dashedSolidMarking = markedRoad('dashed-solid')
check('none removes configured outer boundary without a hardcoded edge', noMarking.markings === null)
check('solid has continuous paint', Math.abs(markingArea(solidMarking.markings) - 20 * 0.15) < 1e-5)
check('dashed has station-clipped paint and gaps even with coarse samples', Math.abs(markingArea(dashedMarking.markings) - 8 * 0.15) < 1e-5)
check('dash endpoints inserted at absolute stations', !!dashedMarking.markings && [3, 9, 12, 18].every((s) => Array.from(dashedMarking.markings!.positions).some((v, i) => i % 3 === 0 && Math.abs(v - s) < 1e-5)))
check('double solid has two continuous stripes', Math.abs(markingArea(doubleMarking.markings) - 2 * markingArea(solidMarking.markings)) < 1e-5)
check('mixed markings combine one solid and one dashed stripe', [solidDashedMarking, dashedSolidMarking].every((result) => Math.abs(markingArea(result.markings) - markingArea(solidMarking.markings) - markingArea(dashedMarking.markings)) < 1e-5))
check('solid-dashed and dashed-solid paint opposite stripe positions', JSON.stringify(solidDashedMarking.markings?.positions) !== JSON.stringify(dashedSolidMarking.markings?.positions))
check('line width scales painted area', Math.abs(markingArea(markedRoad('solid', { width: 0.3 }).markings) - 2 * markingArea(solidMarking.markings)) < 1e-5)
check('dot length changes paint coverage', markingArea(markedRoad('dashed', { dotLength: 1 }).markings) < markingArea(dashedMarking.markings))
check('total length changes dash spacing', markingArea(markedRoad('dashed', { totalLength: 6 }).markings) > markingArea(dashedMarking.markings))
check('range sampling keeps absolute dash phase', markedRoad('dashed', {}, [4, 8].map((s) => ({ s, x: s, y: 0, heading: 0 }))).markings === null)
for (const value of [0, -1, NaN, Infinity, 1e-12, 1e20]) {
  const result = markedRoad('solid-dashed', { width: value, dotLength: value, totalLength: value })
  check(`pathological marking lengths ${value} remain finite and nondegenerate`, validMarking(result.markings))
}
check('short road marking remains nondegenerate', validMarking(markedRoad('double-solid', {}, [0, 0.001].map((s) => ({ s, x: s, y: 0, heading: 0 }))).markings))
check('marking settings leave pavement byte-for-byte unchanged', [solidMarking, dashedMarking, doubleMarking, solidDashedMarking, dashedSolidMarking, markedRoad('dashed', { width: 0.4, dotLength: 1, totalLength: 4 })].every((result) => JSON.stringify(result.pavement) === JSON.stringify(noMarking.pavement)))
const centerOnly = buildRoadMeshFromSamples(markingSamples, { left: [{ ...markingLane, marking: 'none' }], right: [{ ...markingLane, marking: 'none' }] })
check('legacy green center line retained with no center config', !!centerOnly.markings && Math.abs(markingArea(centerOnly.markings) - 3) < 1e-5 && centerOnly.markings.colors[1] > centerOnly.markings.colors[0])

console.log(failures === 0 ? '\nALL PASSED' : `\n${failures} FAILURES`)
process.exit(failures === 0 ? 0 : 1)
