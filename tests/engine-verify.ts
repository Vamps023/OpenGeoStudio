// Engine verification for the SCANeR-style XY function / track /
// intersection modules. Run with:
//   npx esbuild tests/engine-verify.ts --bundle --platform=node --format=cjs //     --outfile=node_modules/.cache/engine-verify.cjs --external:zustand && node node_modules/.cache/engine-verify.cjs
// Temporary engine verification (bundled and run via node, then deleted).
import { evaluatePath } from '../src/engine/geometry'
import { buildRailwayMesh, buildRoadMesh } from '../src/engine/mesh'
import { buildRailFixtureMeshes } from '../src/engine/railFixtures'
import { buildJunctionNetwork } from '../src/engine/junctions'
import { exportNetworkDefinition } from '../src/engine/railNetwork'
import type { Project } from '../src/state/store'
import { importOpenDrive } from '../src/engine/opendrive'
import { fitTrackPath, trackSlices, splitTrackFunctions, mergeFunctionPair, invertTrack, linkTrackFunctions, bindTrackFunctions, trackTotalLength } from '../src/engine/tracks'
import { makeIntersectionData, computeWays, resolveTracks, authorizationKey } from '../src/engine/intersections'
import { buildTerrainMeshWorld } from '../src/engine/terrainMesh'
import { makeTerrainSampler, setActiveTerrain } from '../src/terrain/terrainRegistry'
import { evaluateElevation } from '../src/engine/elevation'
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
    id: 'spline-1', points: [{ x: splinePoints[0].x, y: splinePoints[0].y }],
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
    id: 'bz-1', points: [{ x: farFrame.x, y: farFrame.y }, { x: farFrame.x + 1, y: farFrame.y }],
    lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 50,
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

// 20. Railway mesh (Train section): rails at ±(gauge/2 + railSize/2),
// sleepers at the configured spacing, ballast as the widest strip.
{
  const trackPath = fitTrackPath({
    id: 'rw',
    points: [{ x: 0, y: 0 }, { x: 1, y: 0 }],
    lanesLeft: 0, lanesRight: 0, laneWidth: 3.5,
    functions: [{ kind: 'segment', length: 50 }],
  })
  check('railway path fits', !!trackPath)
  if (trackPath) {
    const cfg = { gauge: 1.435, railSize: 0.075, trackbedWidth: 3, sleeperSpacing: 0.65 }
    const mesh = buildRailwayMesh(trackPath, cfg)
    check('railway mesh builds', !!mesh)
    if (mesh) {
      // sleeper count for spacing 0.65 starting at spacing/2 over 50 m
      const expectedSleepers = 77
      // 3 strips (ballast + 2 rails) over 201 samples → 3*200*6 base indices
      const sleeperQuads = (mesh.indices.length - 3 * 200 * 6) / 6
      check('railway sleepers at spacing', Math.abs(sleeperQuads - expectedSleepers) <= 1, `got ${sleeperQuads}`)
      // rail head vertices near ±(gauge/2 + railSize/2), raised above the ballast top
      const railOffset = cfg.gauge / 2 + cfg.railSize / 2
      let minRailDelta = Number.POSITIVE_INFINITY
      for (let i = 0; i < mesh.positions.length; i += 3) {
        const lateral = Math.abs(mesh.positions[i])
        const height = mesh.positions[i + 1]
        if (height > 0.3) minRailDelta = Math.min(minRailDelta, Math.abs(lateral - railOffset))
      }
      check('railway rails at gauge offset', minRailDelta < 0.02, `min delta ${minRailDelta.toFixed(4)}`)
    }
  }
}

// 21. Rail fixtures (turnout blade, diamond wings/guards, catch point)
// and network-definition XML export.
{
  const mk = (id: string, x: number, y: number, heading: number, len: number): RoadData => ({
    id, name: id,
    points: [{ x, y }, { x: x + Math.cos(heading), y: y + Math.sin(heading) }],
    lanesLeft: 0, lanesRight: 0, laneWidth: 3.5, filletRadius: 50,
    functions: [{ kind: 'segment', length: len }],
    railway: { gauge: 1.435, railSize: 0.075, trackbedWidth: 3, sleeperSpacing: 0.65 },
  })
  // turnout: facing track ends at (50,0); main continues, branch diverges
  const facing = mk('f', 0, 0, 0, 50)
  const main = mk('m', 50, 0, 0, 40)
  const branch = mk('b', 50, 0, 0.5, 30)
  const fixtureMeshes = buildRailFixtureMeshes({
    roads: [facing, main, branch],
    railPoints: [{ id: 'p1', name: 'P1', facingTrackId: 'f', facingContact: 'end', trailingTrackId: 'm', branchTrackId: 'b' }],
    catchPoints: [{ id: 'c1', trackId: 'f', contact: 'start', side: 'left' }],
  })
  check('rail fixtures build meshes', fixtureMeshes.length >= 2, `got ${fixtureMeshes.length}`)
  const blade = fixtureMeshes[0]
  check('turnout blade is non-degenerate', !!blade && blade.indices.length >= 60)

  // diamond crossing of two tracks through (50, 0)
  const a = mk('a', 0, 0, 0, 100)
  const b = mk('b2', 50, -50, Math.PI / 2, 100)
  const crossingMeshes = buildRailFixtureMeshes({
    roads: [a, b],
    railCrossings: [{ id: 'x1', trackAId: 'a', trackBId: 'b2', sA: 50, sB: 50, position: { x: 50, y: 0 }, angle: Math.PI / 2, kind: 'diamond' }],
  })
  check('diamond builds wings + guards on both tracks', crossingMeshes.length >= 8, `got ${crossingMeshes.length}`)

  const project = {
    id: 'p', name: 'Test', createdAt: '', roads: [facing, main, branch], suppressedJunctions: [],
    railPoints: [{ id: 'p1', name: 'P1', facingTrackId: 'f', facingContact: 'end' as const, trailingTrackId: 'm', branchTrackId: 'b' }],
  } as unknown as Project
  const xml = exportNetworkDefinition(project)
  check('network xml has straight segments', xml.includes('<Segment id="f"') && xml.includes('type="Straight"'))
  check('network xml has turnout point', xml.includes('facingSegment="f"') && xml.includes('branchSegment="b"'))
  check('network xml has connection', xml.includes('<Connection fromSegment="f" fromEnd="Beta"'))
}


// 22. GIS alignment: the world-geo transforms used by buildTerrainMeshWorld
// (3D terrain) and makeTerrainSampler (road draping) must agree: a road
// stuck with the sampler must sit exactly on the rendered terrain.
{
  const west = -95.38, east = -95.34, south = 29.74, north = 29.78
  const width = 5, height = 5
  const elevations = new Float32Array(width * height)
  for (let gy = 0; gy < height; gy++) {
    const lat = north + ((south - north) * gy) / (height - 1)
    for (let gx = 0; gx < width; gx++) {
      const lng = west + ((east - west) * gx) / (width - 1)
      elevations[gy * width + gx] = 100 + (lng - west) * 1000 // known ramp
    }
  }
  const terrain = { elevations, width, height, bounds: { west, east, south, north }, minElevation: 100, maxElevation: 140 }
  const geoRef = { lng: -95.36, lat: 29.76, scale: 1 }
  const mesh = buildTerrainMeshWorld(terrain as never, geoRef)
  check('gis: terrain mesh builds', !!mesh)
  if (mesh) {
    // bounds center == geoRef origin, so the grid center vertex sits at world (0,0)
    const cxi = Math.floor(mesh.width / 2)
    const cyi = Math.floor(mesh.height / 2)
    const cx = mesh.positions[(cyi * mesh.width + cxi) * 3]
    const cz = mesh.positions[(cyi * mesh.width + cxi) * 3 + 2]
    check('gis: geoRef origin is terrain center', Math.abs(cx) < 0.5 && Math.abs(cz) < 0.5, `${cx.toFixed(2)}, ${cz.toFixed(2)}`)
    setActiveTerrain(terrain as never)
    const sampler = makeTerrainSampler(geoRef)
    const gxi = 3, gyi = 1
    const lng = west + ((east - west) * gxi) / (width - 1)
    const lat = north + ((south - north) * gyi) / (height - 1)
    const latRad = (geoRef.lat * Math.PI) / 180
    const wx = ((lng - geoRef.lng) * 111320 * Math.cos(latRad)) / geoRef.scale
    const wy = ((lat - geoRef.lat) * 111320) / geoRef.scale
    const expected = 100 + (lng - west) * 1000
    const got = sampler(wx, wy)
    check('gis: sampler matches DEM at mesh node', got !== null && Math.abs(got - expected) < 1e-6, `${got} vs ${expected}`)
    const vy = mesh.positions[(gyi * mesh.width + gxi) * 3 + 1]
    check('gis: mesh vertex carries DEM elevation', Math.abs(vy - expected) < 1e-6, `${vy} vs ${expected}`)
  }
}


// 23. Phase 11 regression: roads AND railways must follow hilly terrain
// exactly when stuck (the "rail floating above terrain" class of bug),
// and the network XML must carry every fixture type.
{
  // hilly synthetic DEM: 200 m square, smooth sinusoidal relief
  const tw = 64, th = 64
  const terr = {
    elevations: new Float32Array(tw * th),
    width: tw, height: th,
    bounds: { west: -95.37, east: -95.35, south: 29.75, north: 29.77 },
    minElevation: 0, maxElevation: 0,
  }
  let elMin = Infinity, elMax = -Infinity
  for (let gy = 0; gy < th; gy++) {
    const lat = terr.bounds.north + ((terr.bounds.south - terr.bounds.north) * gy) / (th - 1)
    for (let gx = 0; gx < tw; gx++) {
      const lng = terr.bounds.west + ((terr.bounds.east - terr.bounds.west) * gx) / (tw - 1)
      const h = 20 + 8 * Math.sin(((lng + 95.36) * 111320) / 30) + 5 * Math.cos(((lat - 29.76) * 111320) / 25)
      terr.elevations[gy * tw + gx] = h
      if (h < elMin) elMin = h
      if (h > elMax) elMax = h
    }
  }
  terr.minElevation = elMin
  terr.maxElevation = elMax
  setActiveTerrain(terr as never)
  const sampler = makeTerrainSampler({ lng: -95.36, lat: 29.76, scale: 1 })

  const stickAndCheck = (name: string, halfWidth: number) => {
    // S-curve track through the hills
    const track = {
      id: `stk-${name}`, points: [{ x: -60, y: -60 }, { x: -59, y: -60 }],
      lanesLeft: 0, lanesRight: 0, laneWidth: 3.5,
      functions: [
        { kind: 'segment', length: 60 },
        { kind: 'arc', radius: 60, angle: Math.PI / 2 },
        { kind: 'segment', length: 60 },
      ] as never,
    }
    const result = stickTrackToTerrain(track, sampler, halfWidth)
    check(`phase11: ${name} stick succeeds`, !!result)
    if (!result) return
    const path = fitTrackPath(track)!
    const profile = result.elevation
    let worst = 0
    for (let s = 5; s <= path.length - 5; s += 5) {
      const at = evaluatePath(path, s)
      const terrainZ = sampler(at.x, at.y) ?? 0
      const roadZ = evaluateElevation(profile, s)
      worst = Math.max(worst, Math.abs(roadZ - terrainZ))
    }
    check(`phase11: ${name} follows hilly terrain`, worst < 0.75, `max delta ${worst.toFixed(3)} m`)
    // banking was picked from the cross-slope
    check(`phase11: ${name} picks cant`, (result.banking?.length ?? 0) > 0)
  }
  evaluateElevation
  stickAndCheck('road', 3.5)
  stickAndCheck('rail', 1.5)

  // XML export carries every fixture type
  const road = (id: string, x: number, y: number, heading: number, len: number): RoadData => ({
    id, name: id,
    points: [{ x, y }, { x: x + Math.cos(heading), y: y + Math.sin(heading) }],
    lanesLeft: 0, lanesRight: 0, laneWidth: 3.5, filletRadius: 50,
    functions: [{ kind: 'segment', length: len }],
    railway: { gauge: 1.435, railSize: 0.075, trackbedWidth: 3, sleeperSpacing: 0.65 },
  })
  const fullProject = {
    id: 'p2', name: 'Fixtures', createdAt: '', suppressedJunctions: [],
    roads: [road('t1', 0, 0, 0, 100), road('t2', 20, -60, Math.PI / 2, 120)],
    railCrossings: [{ id: 'x9', trackAId: 't1', trackBId: 't2', sA: 20, sB: 20, position: { x: 20, y: 0 }, angle: Math.PI / 2, kind: 'diamond' as const }],
    catchPoints: [{ id: 'c9', trackId: 't1', contact: 'start' as const, side: 'left' as const }],
  } as unknown as Project
  const xml2 = exportNetworkDefinition(fullProject)
  check('phase11: xml has diamond crossing', xml2.includes('kind="diamond"'))
  check('phase11: xml has catch point', xml2.includes('<CatchPoint'))
  const crossingMeshes2 = buildRailFixtureMeshes(fullProject)
  check('phase11: diamond + catch meshes build', crossingMeshes2.length >= 9, `got ${crossingMeshes2.length}`)
}



// 24. Junctions must follow the lane SECTION (not the legacy lane counts):
// after adding a lane in the Lanes tab the junction cuts, connections and
// lane allocations have to adapt.
{
  const crossRoad = (id: string, x: number, y: number, heading: number, len: number, lanesLeft: number, lanesRight: number, laneSection?: never) => ({
    id, name: id,
    points: [{ x, y }, { x: x + Math.cos(heading), y: y + Math.sin(heading) }],
    lanesLeft, lanesRight, laneWidth: 3.5, filletRadius: 50,
    functions: [{ kind: 'segment', length: len }],
  })
  // stale legacy counts (1/1) but a real section with 2 left + 3 right lanes
  const sectionRoad = {
    ...crossRoad('sec', -80, 0, 0, 160, 1, 1),
    laneSection: {
      left: [
        { id: 'l0', type: 'travel', width: 3.5, speedLimit: 50, circulation: 'both', vehicles: [], marking: 'none' },
        { id: 'l1', type: 'travel', width: 3.2, speedLimit: 50, circulation: 'both', vehicles: [], marking: 'none' },
      ],
      right: [
        { id: 'r0', type: 'travel', width: 3.5, speedLimit: 50, circulation: 'both', vehicles: [], marking: 'none' },
        { id: 'r1', type: 'travel', width: 3.5, speedLimit: 50, circulation: 'both', vehicles: [], marking: 'none' },
        { id: 'r2', type: 'travel', width: 3.0, speedLimit: 50, circulation: 'both', vehicles: [], marking: 'none' },
      ],
    },
  }
  const main = crossRoad('mj', 0, -80, Math.PI / 2, 160, 1, 1)
  const network = buildJunctionNetwork([sectionRoad, main], [])
  const junctions = network.junctions.filter((j) => !j.suppressed)
  check('phase24: crossing junction detected', junctions.length >= 1)
  const conns = junctions.flatMap((j) => j.connectingRoads)
  check('phase24: junction builds connecting roads', conns.length >= 2, `got ${conns.length}`)
  // connections on the section side must use the SECTION count (2/3), not the stale 1
  const secConns = conns.filter((c) => c.laneLinks.some((l) => l.fromRoadId === 'sec' || l.toRoadId === 'sec'))
  check('phase24: section-side connections exist', secConns.length >= 2, `got ${secConns.length}`)
  const secCounts = secConns.map((c) => (c.laneLinks.filter((l) => l.fromRoadId === 'sec' || l.toRoadId === 'sec')).length)
  check('phase24: section lane counts used', secCounts.some((n) => n >= 2), JSON.stringify(secCounts))
}

console.log(failures === 0 ? '\nALL PASSED' : `\n${failures} FAILURES`)
process.exit(failures === 0 ? 0 : 1)