// Reproduce the app's overlay data pipeline: detect intersection on two
// crossing segment roads, then buildOverlays → count way lines/markers.
import { splitTrackFunctions, fitTrackPath, trackSlices, trackStartFrame } from '../src/engine/tracks'
import { findTrackCrossing, makeIntersectionData, resolveTracks, allWays } from '../src/engine/intersections'
import { buildOverlays } from '../src/roads/overlays'
import { evaluatePath } from '../src/engine/geometry'
import { functionEndFrame } from '../src/engine/xyFunctions'
import type { Frame } from '../src/engine/xyFunctions'
import type { RoadData } from '../src/state/store'

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

console.log(failures === 0 ? '\nALL PASSED' : `\n${failures} FAILURES`)
process.exit(failures === 0 ? 0 : 1)
