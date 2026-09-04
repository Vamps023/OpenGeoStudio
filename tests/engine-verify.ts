// Engine verification for the SCANeR-style XY function / track /
// intersection modules. Run with:
//   npx esbuild tests/engine-verify.ts --bundle --platform=node --format=cjs //     --outfile=node_modules/.cache/engine-verify.cjs --external:zustand && node node_modules/.cache/engine-verify.cjs
// Temporary engine verification (bundled and run via node, then deleted).
import { evaluatePath } from '../src/engine/geometry'
import { buildRoadMesh } from '../src/engine/mesh'
import { importOpenDrive } from '../src/engine/opendrive'
import { fitTrackPath, trackSlices, splitTrackFunctions, mergeFunctionPair, invertTrack, linkTrackFunctions, bindTrackFunctions, trackTotalLength } from '../src/engine/tracks'
import { makeIntersectionData, computeWays, resolveTracks, authorizationKey } from '../src/engine/intersections'
import { sampleFunction, functionLength, splitFunction, mergeFunctions, convertSplineToFunctions, bezierConnector, bezierAt, functionEndFrame, FUNCTION_COLORS } from '../src/engine/xyFunctions'
import { stickTrackToTerrain } from '../src/engine/tracks'
import type { XYFunction, Frame } from '../src/engine/xyFunctions'
import type { Vec2 } from '../src/engine/types'
import type { RoadData } from '../src/state/store'

let failures = 0
function check(name: string, cond: boolean, detail = '') {
  if (!cond) { failures++; console.log('FAIL', name, detail) } else console.log('ok  ', name)
}

const frame: Frame = { x: 0, y: 0, heading: 0 }

// 1. Segment
const seg: XYFunction = { kind: 'segment', length: 100 }
check('segment length', Math.abs(functionLength(seg) - 100) < 1e-9)
const segEnd = functionEndFrame(frame, seg)
check('segment end', Math.abs(segEnd.x - 100) < 1e-9 && Math.abs(segEnd.heading) < 1e-9)

// 2. Arc
const arc: XYFunction = { kind: 'arc', radius: 50, angle: Math.PI / 2 }
check('arc length', Math.abs(functionLength(arc) - 78.54) < 0.01)
const arcEnd = functionEndFrame(frame, arc)
check('arc end pos', Math.abs(arcEnd.x - 50) < 1e-6 && Math.abs(arcEnd.y - 50) < 1e-6, `${arcEnd.x},${arcEnd.y}`)
check('arc end heading', Math.abs(arcEnd.heading - Math.PI / 2) < 1e-6)

// 3. Clothoid: infinite → r=100, left turn
const clo: XYFunction = { kind: 'clothoid', radiusIn: 0, radiusOut: 100, length: 60 }
const cloSamples = sampleFunction(frame, clo, 2)
check('clothoid samples start heading 0', Math.abs(cloSamples[0].heading) < 1e-9)
const cloEnd = functionEndFrame(frame, clo)
check('clothoid bends toward r=100', cloEnd.y > 5 && cloEnd.y < 25, `${cloEnd.x.toFixed(1)},${cloEnd.y.toFixed(1)}`)
check('clothoid end heading positive', cloEnd.heading > 0.2 && cloEnd.heading < 0.4, `${cloEnd.heading}`)
const cloSplit = splitFunction(frame, clo, 30)
check('clothoid split', !!cloSplit && cloSplit[0].kind === 'clothoid' && cloSplit[1].kind === 'clothoid')
if (cloSplit && cloSplit[0].kind === 'clothoid' && cloSplit[1].kind === 'clothoid') {
  const a = cloSplit[0] as { radiusOut: number }, b = cloSplit[1] as { radiusIn: number }
  check('clothoid split radius continuity', Math.abs(a.radiusOut - b.radiusIn) < 1e-6)
}

// 4. Track with intrinsic kinds + fit + slices
const road: RoadData = {
  id: 'r1', name: 'R1',
  points: [{ x: 0, y: 0 }, { x: 1, y: 0 }],
  functions: [
    { kind: 'segment', length: 50 },
    { kind: 'clothoid', radiusIn: 0, radiusOut: 100, length: 40 },
    { kind: 'arc', radius: 100, angle: Math.PI / 3 },
    { kind: 'segment', length: 30 },
  ],
  lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0,
}
const path = fitTrackPath(road)
check('track path fitted', !!path && path.elements.length === 4)
check('track total length', Math.abs(trackTotalLength(road) - (50 + 40 + 104.72 + 30)) < 0.1, `${trackTotalLength(road)}`)
const slices = trackSlices(road)!
check('slices offsets cumulative', Math.abs(slices[2].offset - 90) < 1e-6, `${slices[2].offset}`)

// 5. Split track at station
const pieces = splitTrackFunctions(road, 100)
check('split track', !!pieces)
if (pieces) {
  const lenA = pieces.functionsA.reduce((s, f) => s + functionLength(f), 0)
  const lenB = pieces.functionsB.reduce((s, f) => s + functionLength(f), 0)
  check('split lengths preserved', Math.abs(lenA - 100) < 0.01 && Math.abs(lenA + lenB - trackTotalLength(road)) < 0.01, `${lenA} + ${lenB}`)
}

// 6. Merge mismatched kinds fails
if (pieces) {
  const merged = mergeFunctionPair(pieces.functionsA, pieces.functionsA.length - 2)
  check('merge needs same kind (segment+clothoid fails)', merged === null)
}

// 7. Invert track (start frame moves to the original end)
const inverted = invertTrack(road)!
const invPath = fitTrackPath({ ...road, functions: inverted.functions, points: [
  { x: inverted.startFrame.x, y: inverted.startFrame.y },
  { x: inverted.startFrame.x + Math.cos(inverted.startFrame.heading), y: inverted.startFrame.y + Math.sin(inverted.startFrame.heading) },
] })
check('invert produces path', !!invPath)
const origEnd = evaluatePath(path!, path!.length)
const invEnd = evaluatePath(invPath!, invPath!.length)
check('original end is end of chain', origEnd.x > 100)
check('invert: end reaches original start', Math.abs(invEnd.x - 0) < 0.5 && Math.abs(invEnd.y - 0) < 0.5, `${invEnd.x.toFixed(2)},${invEnd.y.toFixed(2)}`)
check('invert: end heading ~ reversed', Math.abs(Math.abs(invEnd.heading) - Math.PI) < 0.02, `${invEnd.heading}`)

// 8. Polyline track
const plRoad: RoadData = {
  id: 'r2', name: 'R2',
  points: [],
  functions: [{ kind: 'polyline', points: [{ x: 0, y: 0 }, { x: 30, y: 0 }, { x: 30, y: 40 }], splineType: 'segment' }],
  lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0,
}
const plPath = fitTrackPath(plRoad)
check('polyline path', !!plPath && Math.abs(plPath.length - 70) < 0.01)
const plEnd = evaluatePath(plPath!, plPath!.length)
check('polyline end', Math.abs(plEnd.x - 30) < 1e-6 && Math.abs(plEnd.y - 40) < 1e-6 && Math.abs(plEnd.heading - Math.PI / 2) < 1e-6)

// 9. Bezier connector
const conn = bezierConnector({ x: 30, y: 40, heading: Math.PI / 2 }, { x: 60, y: 40, heading: 0 })
const connEnd = functionEndFrame({ x: 30, y: 40, heading: Math.PI / 2 }, conn)
check('bezier connector lands on target', Math.abs(connEnd.x - 60) < 1e-6 && Math.abs(connEnd.y - 40) < 1e-6)

// 10. Spline → clothoid+circle+segment conversion
const splineFn: XYFunction = { kind: 'clothoidSpline', points: [{ x: 0, y: 0 }, { x: 40, y: 10 }, { x: 80, y: 40 }], tolerance: 0.5, symmetryThreshold: 1 }
const conv = convertSplineToFunctions(frame, splineFn)
check('spline conversion produces functions', conv.length >= 3 && conv[0].kind === 'segment' && conv.some(f => f.kind === 'arc'))

// 11. Intersections: ways + authorizations
const roadA: RoadData = { ...road, id: 'A', points: [{ x: -100, y: 0 }, { x: -99, y: 0 }], functions: [{ kind: 'segment', length: 80 }] }
const roadB: RoadData = { ...road, id: 'B', points: [{ x: 0, y: -100 }, { x: 0, y: -99 }], functions: [{ kind: 'segment', length: 80 }] }
const roadC: RoadData = { ...road, id: 'C', points: [{ x: 100, y: 0 }, { x: 99, y: 0 }], functions: [{ kind: 'segment', length: 80 }] }
const resolved = resolveTracks([roadA, roadB, roadC])
check('resolve tracks', resolved.size === 3)
const node = makeIntersectionData('n1', { x: 0, y: 0 })
node.trackEnds = [
  { trackId: 'A', contact: 'end' },
  { trackId: 'B', contact: 'end' },
  { trackId: 'C', contact: 'end' },
]
const ways = computeWays(node, resolved)
check('ways generated for 3 approaches', ways.length >= 4, `${ways.length}`)
const key = authorizationKey({ trackId: 'A', contact: 'end' }, { trackId: 'B', contact: 'end' })
check('way key format', ways.some(w => w.key === key), key)
check('ways authorized by default', ways.every(w => w.authorized))
node.authorizations[key] = false
const ways2 = computeWays(node, resolved)
check('denied way reflected', ways2.find(w => w.key === key)?.authorized === false)

// 12. Function colors cover all kinds
check('colors for all kinds', Object.keys(FUNCTION_COLORS).length === 6)

// 13. Polyline split preserves vertices
const plFn: XYFunction = { kind: 'polyline', points: [{ x: 0, y: 0 }, { x: 50, y: 0 }, { x: 50, y: 50 }], splineType: 'segment' }
const plSplit = splitFunction(frame, plFn, 60)
check('polyline split at 60', !!plSplit && plSplit[0].kind === 'polyline' && (plSplit[0] as { points: unknown[] }).points.length === 3)

// 14. Polyline merge round trip
const mergedPl = mergeFunctions(plSplit![0], plSplit![1])
check('polyline merge restores', !!mergedPl && (mergedPl as { points: unknown[] }).points.length === 3 && Math.abs(functionLength(mergedPl) - 100) < 1e-6)


// 17. Stick to Background Terrain picks altitude AND banking (cross-slope)
const slopeRoad: RoadData = {
  id: 'slope', name: 'Slope',
  points: [{ x: 0, y: 0 }, { x: 1, y: 0 }],
  functions: [{ kind: 'segment', length: 40 }],
  lanesLeft: 1, lanesRight: 1, laneWidth: 4, filletRadius: 0,
}
// terrain rising to the left of the axis: 8% cross slope
const slopedTerrain = (x: number, y: number) => 5 + 0.08 * y
const stick = stickTrackToTerrain(slopeRoad, slopedTerrain, 2)
check('stick returns altitude + banking', !!stick && stick.elevation.length > 1 && stick.banking.length > 1)
if (stick) {
  const expected = Math.atan2(0.32, 4) // z(+2) - z(-2) = 0.32 over width 4
  const bank = stick.banking[Math.floor(stick.banking.length / 2)].z
  check('banking picks up cross slope', Math.abs(bank - expected) < 0.005, `${bank} vs ${expected}`)
  check('altitude picks center height', Math.abs(stick.elevation[0].z - 5) < 1e-6)
}

if (typeof DOMParser === 'undefined') {
  // node: use linkedom's DOMParser so the ODR tests run everywhere
  try {
    const { DOMParser: LinkedomParser } = require('linkedom')
    ;(globalThis as { DOMParser?: unknown }).DOMParser = LinkedomParser
  } catch {
    console.log('skip  odr tests (no DOMParser and no linkedom)')
  }
}
if (typeof DOMParser !== 'undefined') {
// 18. OpenDRIVE import: line/arc/spiral geometries + junction explicit ways
const odrXml = '<?xml version="1.0"?><OpenDRIVE>' +
  '<road name="Main" id="1" length="100"><planView>' +
  '<geometry s="0" x="0" y="0" hdg="0" length="60"><line/></geometry>' +
  '<geometry s="60" x="60" y="0" hdg="0" length="40"><arc curvature="0.01"/></geometry>' +
  '</planView><elevationProfile><elevation s="0" a="3" b="0" c="0" d="0"/></elevationProfile></road>' +
  '<road name="Cross" id="2" length="80"><planView>' +
  '<geometry s="0" x="30" y="-40" hdg="1.5707963" length="80"><line/></geometry>' +
  '</planView></road>' +
  '<road name="Ramp" id="3" length="30"><planView>' +
  '<geometry s="0" x="40" y="0" hdg="0" length="30"><spiral curvStart="0" curvEnd="0.02"/></geometry>' +
  '</planView></road>' +
  '<junction id="1" name="J1"><connection id="0" incomingRoad="2" contactPoint="start" connectingRoad="3">' +
  '<laneLink from="-1" to="-1"/></connection></junction>' +
  '</OpenDRIVE>'
const odr = importOpenDrive(odrXml)
check('odr parses', !!odr)
if (odr) {
  check('odr imports 3 roads', odr.roads.length === 3, String(odr.roads.length))
  const main = odr.roads.find((r) => r.id === 'odr-1')!
  check('odr line+arc functions', !!main && main.functions!.length === 2 && main.functions![0].kind === 'segment' && main.functions![1].kind === 'arc')
  const arcFn = main.functions![1] as { kind: 'arc'; radius: number; angle: number }
  check('odr arc radius/angle', Math.abs(arcFn.radius - 100) < 1e-6 && Math.abs(arcFn.angle - 0.4) < 1e-6)
  check('odr elevation profile', !!main.elevationProfile && Math.abs(main.elevationProfile![0].z - 3) < 1e-6)
  const ramp = odr.roads.find((r) => r.id === 'odr-3')!
  check('odr spiral becomes clothoid', ramp.functions![0].kind === 'clothoid' && (ramp.functions![0] as { radiusOut: number }).radiusOut === 50)
  check('odr junction with explicit ways', odr.intersections.length === 1 && (odr.intersections![0].explicitWays?.length ?? 0) >= 1)
  check('odr explicit ways flagged', (odr.intersections![0].explicitWays![0] as { explicit: boolean }).explicit === true)
}
}


// 19. ClothoidSpline / Bezier mesh pipeline: lengths and fitted paths must
// stay inside the control polygon — no straight extrapolation tails, and
// bezier length must be measured from the curve's real start, not the origin.
{
  const splinePoints: Vec2[] = [
    { x: 1000, y: 2000 }, { x: 1040, y: 2030 }, { x: 1090, y: 2020 }, { x: 1120, y: 2060 },
  ]
  const splineRoad = {
    id: 'spline-1', name: 'S', points: [{ x: splinePoints[0].x, y: splinePoints[0].y }],
    lanesLeft: 1, lanesRight: 1, laneWidth: 3.5,
    functions: [{ kind: 'clothoidSpline', points: splinePoints, tolerance: 0.5, symmetryThreshold: 1 } as never],
  }
  const splinePath = fitTrackPath(splineRoad)
  check('spline path fits', !!splinePath)
  if (splinePath) {
    const last = splinePoints[splinePoints.length - 1]
    const end = evaluatePath(splinePath, splinePath.length)
    check('spline path ends on last control point', Math.hypot(end.x - last.x, end.y - last.y) < 0.05, `${Math.hypot(end.x - last.x, end.y - last.y).toFixed(3)}`)
    // no mesh sample may overshoot past the last control point (the 1.02 tail bug)
    const mesh = buildRoadMesh(splinePath, { left: [{ id: 'l', type: 'travel', width: 3.5, speedLimit: 0, circulation: 'both' as const, vehicles: [], marking: 'none' as const }], right: [] })
    check('spline mesh builds', !!mesh)
    if (mesh) {
      let maxDist = 0
      for (let i = 0; i < mesh.positions.length; i += 3) {
        const wx = mesh.positions[i]
        const wy = -mesh.positions[i + 2]
        // distance beyond the last control point along the exit direction
        const ex = end.x - last.x, ey = end.y - last.y
        const el = Math.hypot(ex, ey) || 1
        const along = (wx - last.x) * (ex / el) + (wy - last.y) * (ey / el)
        if (along > maxDist) maxDist = along
      }
      check('spline mesh has no straight tail past the end', maxDist < 0.5, `max ${maxDist.toFixed(2)} m`)
    }
    check('spline length is chord domain', Math.abs(splinePath.length - functionLength(splineRoad.functions![0])) < 1e-6)
  }

  // Bezier drawn far from the origin: length must use the real start (p0/frame)
  const farFrame: Frame = { x: 5000, y: 8000, heading: 0.4 }
  const farEnd: Frame = { x: 5040, y: 8035, heading: 1.2 }
  const bz = bezierConnector(farFrame, farEnd)
  const bzLen = functionLength(bz)
  const denseLen = (() => {
    let total = 0
    let prev = bezierAt(farFrame, bz.p1, bz.p2, bz.p3, 0)
    for (let i = 1; i <= 400; i++) {
      const p = bezierAt(farFrame, bz.p1, bz.p2, bz.p3, i / 400)
      total += Math.hypot(p.x - prev.x, p.y - prev.y)
      prev = p
    }
    return total
  })()
  check('bezier length measured from real start', Math.abs(bzLen - denseLen) < 0.1, `${bzLen.toFixed(2)} vs ${denseLen.toFixed(2)}`)
  const bzRoad = {
    id: 'bz-1', name: 'B', points: [{ x: farFrame.x, y: farFrame.y }, { x: farFrame.x + 1, y: farFrame.y }],
    lanesLeft: 1, lanesRight: 1, laneWidth: 3.5,
    functions: [bz],
  }
  const bzPath = fitTrackPath(bzRoad)
  check('bezier path fits', !!bzPath)
  if (bzPath) {
    const end = evaluatePath(bzPath, bzPath.length)
    check('bezier path ends at p3', Math.hypot(end.x - farEnd.x, end.y - farEnd.y) < 0.05)
    check('bezier path length matches curve', Math.abs(bzPath.length - denseLen) < 0.1, `${bzPath.length.toFixed(2)} vs ${denseLen.toFixed(2)}`)
  }
  // Inversion: reversed chain must start at the old end and end at the old start
  const inv = invertTrack(bzRoad)
  check('bezier invert works', !!inv)
  if (inv) {
    const invPath = fitTrackPath({ ...bzRoad, points: [{ x: inv.startFrame.x, y: inv.startFrame.y }, { x: inv.startFrame.x + Math.cos(inv.startFrame.heading), y: inv.startFrame.y + Math.sin(inv.startFrame.heading) }], functions: inv.functions })
    check('bezier inverted path fits', !!invPath)
    if (invPath) {
      const end = evaluatePath(invPath, invPath.length)
      check('bezier inverted ends at old start', Math.hypot(end.x - farFrame.x, end.y - farFrame.y) < 0.05, `${end.x.toFixed(1)},${end.y.toFixed(1)}`)
      check('bezier inverted length preserved', Math.abs(invPath.length - denseLen) < 0.1)
    }
  }
}

console.log(failures === 0 ? '\nALL PASSED' : `\n${failures} FAILURES`)
process.exit(failures === 0 ? 0 : 1)
