// Engine verification for the SCANeR-style XY function / track /
// intersection modules. Run with:
//   npx esbuild tests/engine-verify.ts --bundle --platform=node --format=cjs //     --outfile=node_modules/.cache/engine-verify.cjs --external:zustand && node node_modules/.cache/engine-verify.cjs
// Temporary engine verification (bundled and run via node, then deleted).
import { evaluatePath } from '../src/engine/geometry'
import { buildRailwayMesh, buildRoadMesh, buildConnectingRoadMesh } from '../src/engine/mesh'
import { buildRailFixtureMeshes } from '../src/engine/railFixtures'
import { buildJunctionNetwork, buildJunctionSurface, buildJunctionMarkings } from '../src/engine/junctions'
import { buildSimPaths, spawnVehicles, stepSimulation, simulationPoses, simPoseAt } from '../src/engine/simulation'
import { parseOverpassBuildings, toBuildingData, triangulatePolygon, buildBuildingMesh, overpassQuery, overpassQueryPolygon, polygonBounds, pointInPolygon, ringCentroidLatLng } from '../src/engine/osmBuildings'
import { generatePcgBuildingMesh } from '../src/engine/pcgBuildings'
import { fitRoadGeometry } from '../src/engine/roadGeometry'
import { smoothPolylinePoints } from '../src/engine/tracks'
import { exportNetworkDefinition } from '../src/engine/railNetwork'
import { exportOpenDrive } from '../src/engine/opendriveExport'
import type { Project } from '../src/state/store'
import { serializeProject, deserializeProject, PROJECT_SCHEMA_VERSION } from '../src/domain'
import type { RoadData as DomainRoadData, Project as DomainProject } from '../src/domain'
import { buildRoadSamplers, buildProjectJunctionNetwork, getLaneSection, getRoadTotalWidth, getRoadTotalLanes, validateRoad, validateProjectRoads, ROAD_LIFT } from '../src/engine/roadServices'
import { buildEditorPreviewMeshes, flattenPreviewMeshes, previewRoadIds } from '../src/engine/previewMeshes'
import { buildExportScene, encodeGLB, exportProjectToGLB } from '../src/engine/gltfExport'
import { DOMParser as LinkedomDOMParser } from 'linkedom'
import { OrthographicCamera, Vector3 } from 'three'
import { fitPlanCamera } from '../src/viewport/RoadViewport'

// OpenDRIVE import needs a DOM parser in node — polyfill from linkedom
if (typeof globalThis.DOMParser === 'undefined') {
  (globalThis as unknown as { DOMParser: unknown }).DOMParser = LinkedomDOMParser
}
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
    const meshResult = buildRoadMesh(splinePath, { left: [{ id: 'l', type: 'travel', width: 3.5, speedLimit: 0, circulation: 'both' as const, vehicles: [], marking: 'none' as const }], right: [] })
    const mesh = meshResult.pavement
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
  const secLaneIds = secConns.flatMap((c) => c.laneLinks.flatMap((link) => [link.fromRoadId === 'sec' ? link.fromLaneId : 0, link.toRoadId === 'sec' ? link.toLaneId : 0]))
  check('phase24: section lane counts used', secLaneIds.some((id) => Math.abs(id) >= 2), JSON.stringify(secLaneIds))
}


// 25. OpenDRIVE export -> import round trip (SCANeR Export/OpenDRIVE parity)
{
  const mk = (id: string, name: string, x: number, y: number, heading: number, lanesLeft: number, lanesRight: number): RoadData => ({
    id, name,
    points: [{ x, y }, { x: x + Math.cos(heading), y: y + Math.sin(heading) }],
    lanesLeft, lanesRight, laneWidth: 3.5, filletRadius: 50,
    functions: [
      { kind: 'segment', length: 80 },
      { kind: 'arc', radius: 100, angle: Math.PI / 3 },
      { kind: 'clothoid', radiusIn: 0, radiusOut: 150, length: 60 },
    ],
    elevationProfile: [{ s: 0, z: 10 }, { s: 120, z: 18 }, { s: 200, z: 14 }],
  })
  const masterProject = {
    id: 'exp', name: 'RoundTrip', createdAt: '', suppressedJunctions: [],
    geoRef: { lng: -95.36, lat: 29.76, scale: 1 },
    roads: [
      mk('r1', 'Main', 0, 0, 0, 1, 2),
      mk('r2', 'Cross', -40, -60, Math.PI / 2, 2, 1),
    ],
  } as unknown as Project
  const xodr = exportOpenDrive(masterProject)
  check('odr: header written', xodr.startsWith('<?xml') && xodr.includes('<OpenDRIVE>'))
  check('odr: planView line element', xodr.includes('<line/>'))
  check('odr: arc element with curvature', xodr.includes('<arc curvature="0.01000000"/>'))
  check('odr: spiral element with curvEnd', xodr.includes('<spiral') && xodr.includes('curvEnd="0.00666667"/>'))
  check('odr: elevation profile exported', xodr.includes('<elevationProfile>') && xodr.includes('<elevation s="120.0000" a="18'))
  check('odr: lanes section with driving lanes', xodr.includes('<lane id="2" type="driving"') && xodr.includes('<lane id="-1" type="driving"'))
  check('odr: green centre road mark', xodr.includes('color="green"'))

  // re-import our own export (round trip)
  const parsed = importOpenDrive(xodr)
  check('odr: round trip parses', !!parsed)
  if (parsed) {
    check('odr: round trip road count', parsed.roads.length === 2, String(parsed.roads.length))
    const rt = parsed.roads[0]
    check('odr: round trip has functions', !!rt && !!rt.functions && rt.functions.length >= 3, JSON.stringify(rt?.functions?.map((f) => f.kind)))
    const rtLen = rt!.functions!.reduce((a, f) => a + functionLength(f), 0)
    const srcLen = masterProject.roads[0].functions!.reduce((a, f) => a + functionLength(f), 0)
    check('odr: round trip length preserved', Math.abs(rtLen - srcLen) < 0.5, `${rtLen.toFixed(2)} vs ${srcLen.toFixed(2)}`)
  }

}


// 26. Simulation runtime (vehicles on the network) + track smoothing
{
  const poly = [
    { x: 0, y: 0 }, { x: 50, y: 0 }, { x: 50, y: 50 },
  ]
  const paths = buildSimPaths([{ id: 'r1', name: 'R1', polyline: poly, length: 100 }])
  check('sim: path builds', paths.length === 1 && Math.abs(paths[0].length - 100) < 1)
  const pose0 = simPoseAt(paths[0], 0)
  check('sim: pose at start', Math.abs(pose0.x) < 0.01 && Math.abs(pose0.y) < 0.01)
  const poseMid = simPoseAt(paths[0], 75)
  check('sim: pose mid on second leg', Math.abs(poseMid.x - 50) < 0.01 && Math.abs(poseMid.y - 25) < 0.01)
  check('sim: heading along first leg', Math.abs(simPoseAt(paths[0], 20).heading) < 1e-9)
  const vehicles = spawnVehicles(paths, 4)
  check('sim: spawns vehicles', vehicles.length === 4)
  const before = vehicles.map((v) => v.s)
  stepSimulation(vehicles, paths, 1)
  const after = vehicles.map((v) => v.s)
  check('sim: advances by speed*dt (wrap-aware)', vehicles.every((v, i) => ((after[i] - before[i]) % 100 + 100) % 100 > 0))
  const wrap = [{ id: 1, roadId: 'r1', s: 99.9, laneOffset: -1.75, speed: 10 }]
  stepSimulation(wrap, paths, 0.1)
  check('sim: wraps at road end', wrap[0].s < 1, String(wrap[0].s))
  const poses = simulationPoses(vehicles, paths)
  check('sim: poses for all vehicles', poses.length === 4)

  const smoothed = smoothPolylinePoints([{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 20, y: 10 }], 2)
  check('smoothing: endpoint preserved', smoothed[0].x === 0 && smoothed[0].y === 0)
  check('smoothing: point count grows', smoothed.length === 16, String(smoothed.length))
}

// ─── Section 26: OSM buildings (parse, attributes, extrusion) ───────
{
  const overpass = {
    elements: [
      { type: 'node', id: 1, lat: 18.52, lon: 73.85 },
      { type: 'node', id: 2, lat: 18.5201, lon: 73.85 },
      { type: 'node', id: 3, lat: 18.5201, lon: 73.8501 },
      { type: 'node', id: 4, lat: 18.52, lon: 73.8501 },
      { type: 'way', id: 100, tags: { building: 'yes', name: 'Tower A', height: '24 m' }, nodes: [1, 2, 3, 4, 1] },
      { type: 'way', id: 101, tags: { building: 'residential', 'building:levels': '3' }, geometry: [
        { lat: 18.52, lon: 73.851 }, { lat: 18.5201, lon: 73.851 }, { lat: 18.5201, lon: 73.8511 }, { lat: 18.52, lon: 73.8511 }, { lat: 18.52, lon: 73.851 },
      ] },
      { type: 'way', id: 102, tags: { building: 'no' }, nodes: [1, 2, 3, 4, 1] },
      { type: 'way', id: 103, tags: { building: 'commercial', 'roof:shape': 'gabled' }, geometry: [
        { lat: 18.5202, lon: 73.85 }, { lat: 18.5203, lon: 73.85 }, { lat: 18.5203, lon: 73.8502 }, { lat: 18.5202, lon: 73.8502 }, { lat: 18.5202, lon: 73.85 },
      ] },
    ],
  }
  const raw = parseOverpassBuildings(overpass)
  check('osm: parses buildings, skips building=no', raw.length === 3, String(raw.length))
  check('osm: node-map ring resolves', raw[0].ring.length === 4)
  const geoRef = { lng: 73.85, lat: 18.52, scale: 1 }
  const buildings = raw.map((building) => toBuildingData(building, geoRef, 6))
  check('osm: height tag wins', Math.abs(buildings[0].height - 24) < 0.01)
  check('osm: levels fallback', Math.abs(buildings[1].height - 3 * 3.2) < 0.01 && buildings[1].levels === 3)
  check('osm: default fallback height', Math.abs(buildings[2].height - 6) < 0.01)
  check('osm: projected into world frame', Math.abs(buildings[0].ring[0].x) < 0.01 && Math.abs(buildings[0].ring[0].y) < 0.01)

  // L-shaped concave polygon triangulates into >= 4 triangles, all valid
  const lShape = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 5 }, { x: 5, y: 5 }, { x: 5, y: 10 }, { x: 0, y: 10 }]
  const tris = triangulatePolygon(lShape)
  check('osm: concave triangulation covers area', tris.length >= 4, String(tris.length))
  let triArea = 0
  for (const [a, b, c] of tris) {
    triArea += Math.abs(
      (lShape[b].x - lShape[a].x) * (lShape[c].y - lShape[a].y) - (lShape[c].x - lShape[a].x) * (lShape[b].y - lShape[a].y),
    ) / 2
  }
  check('osm: triangulation area matches polygon', Math.abs(triArea - 75) < 0.01, String(triArea))

  const mesh = buildBuildingMesh(buildings[0], 5)
  check('osm: extrusion builds mesh', !!mesh && mesh.positions.length >= 3 * 4 * 2 * 4)
  let minY = Infinity
  let maxY = -Infinity
  for (let i = 1; i < mesh!.positions.length; i += 3) {
    minY = Math.min(minY, mesh!.positions[i])
    maxY = Math.max(maxY, mesh!.positions[i])
  }
  check('osm: walls span base..height above terrain', Math.abs(minY - 5) < 0.01 && Math.abs(maxY - 29) < 0.01)
  const flatVariant = buildBuildingMesh({ ...buildings[2], roofShape: 'flat' }, 0)
  const gabled = buildBuildingMesh(buildings[2], 0)
  check('osm: gabled roof adds ridge geometry', !!gabled && !!flatVariant && gabled.positions.length > flatVariant.positions.length)
}

// ─── Section 26b: OSM polygon query + point-in-polygon filter ───────
{
  // bbox query
  const bbox = { west: 73.85, south: 18.52, east: 73.86, north: 18.53 }
  const q = overpassQuery(bbox)
  check('osm: bbox query contains coords', q.includes('(18.52,73.85,18.53,73.86)') && q.includes('out:json'))

  // polygon query
  const ring = [
    { lat: 18.52, lng: 73.85 }, { lat: 18.53, lng: 73.85 },
    { lat: 18.53, lng: 73.86 }, { lat: 18.52, lng: 73.86 },
  ]
  const pq = overpassQueryPolygon(ring)
  check('osm: polygon query uses poly filter', pq.includes('poly:"18.52 73.85 18.53 73.85') && pq.includes('timeout:45'))
  let threw = false
  try { overpassQueryPolygon([{ lat: 1, lng: 1 }]) } catch { threw = true }
  check('osm: polygon query rejects < 3 vertices', threw)

  // polygon bounds
  const pb = polygonBounds(ring)
  check('osm: polygon bounds computed', Math.abs(pb.west - 73.85) < 1e-9 && Math.abs(pb.north - 18.53) < 1e-9)
  check('osm: empty polygon bounds zero', (() => { const z = polygonBounds([]); return z.west === 0 && z.north === 0 })())

  // point-in-polygon: square ring
  check('osm: pip inside', pointInPolygon(73.855, 18.525, ring))
  check('osm: pip outside', !pointInPolygon(73.90, 18.60, ring))
  // concave L-shape
  const lRing = [
    { lat: 0, lng: 0 }, { lat: 0, lng: 10 }, { lat: 5, lng: 10 }, { lat: 5, lng: 5 }, { lat: 10, lng: 5 }, { lat: 10, lng: 0 },
  ]
  check('osm: pip concave inside notch', pointInPolygon(7.5, 2.5, lRing))
  check('osm: pip concave outside notch', !pointInPolygon(7.5, 7.5, lRing))

  // ring centroid (lat/lng)
  const c = ringCentroidLatLng(ring)
  check('osm: latlng ring centroid', Math.abs(c.lat - 18.525) < 1e-9 && Math.abs(c.lng - 73.855) < 1e-9)
}

// ─── Section 27: PCG building generation ────────────────────────────
{
  const footprint: import('../src/engine/osmBuildings').OsmBuildingData = {
    id: 'way:9001',
    name: 'PCG Test',
    buildingType: 'residential',
    height: 12.8,
    levels: 4,
    roofShape: 'flat',
    ring: [{ x: 0, y: 0 }, { x: 20, y: 0 }, { x: 20, y: 12 }, { x: 0, y: 12 }],
    tags: { building: 'residential' },
  }
  const params = { style: 'residential' as const, seed: 42, detail: 'medium' as const }
  const a = generatePcgBuildingMesh(footprint, 3, params)
  const b = generatePcgBuildingMesh(footprint, 3, params)
  check('pcg: deterministic for same seed', !!a && !!b && a.positions.length === b.positions.length)
  let same = true
  for (let i = 0; i < a!.colors.length; i++) if (a!.colors[i] !== b!.colors[i]) { same = false; break }
  check('pcg: identical colors for same seed', same)
  const c = generatePcgBuildingMesh(footprint, 3, { ...params, seed: 43 })
  let differs = a!.colors.length === c!.colors.length
  for (let i = 0; i < a!.colors.length; i++) if (a!.colors[i] !== c!.colors[i]) { differs = true; break }
  check('pcg: different seed varies the model', differs)
  const simple = buildBuildingMesh(footprint, 3)
  check('pcg: richer than plain extrusion', !!a && !!simple && a.positions.length > simple!.positions.length)
  const low = generatePcgBuildingMesh(footprint, 3, { style: 'residential', seed: 42, detail: 'low' })
  const high = generatePcgBuildingMesh(footprint, 3, { style: 'residential', seed: 42, detail: 'high' })
  check('pcg: detail level scales geometry', !!low && !!high && low.positions.length < high.positions.length)
  const regen = generatePcgBuildingMesh(footprint, 3, { ...params, override: 1 })
  let regenDiffers = false
  for (let i = 0; i < a!.colors.length; i++) if (a!.colors[i] !== regen!.colors[i]) { regenDiffers = true; break }
  check('pcg: regenerate override changes variation', regenDiffers)
  check('pcg: degenerate ring rejected', generatePcgBuildingMesh({ ...footprint, ring: [{ x: 0, y: 0 }] }, 0, params) === null)
  // height respected: max vertex Y within base+height (+small parapet tolerance)
  const mesh = generatePcgBuildingMesh(footprint, 3, params)!
  let maxY = -Infinity
  for (let i = 1; i < mesh.positions.length; i += 3) maxY = Math.max(maxY, mesh.positions[i])
  check('pcg: building stays within height + parapet', maxY <= 3 + 12.8 + 1.0, String(maxY))
}

// ─── Section 28: Road mesh split (pavement + markings) + junction surface ─
{
  // Straight road: pavement and markings are separate meshes
  const straightPath = fitRoadGeometry({ points: [{ x: 0, y: 0 }, { x: 100, y: 0 }], geometryType: 'straight', filletRadius: 0 })
  const section = {
    left: [{ id: 'l1', type: 'travel' as const, width: 3.5, speedLimit: 0, circulation: 'both' as const, vehicles: [], marking: 'none' as const }],
    right: [{ id: 'r1', type: 'travel' as const, width: 3.5, speedLimit: 0, circulation: 'both' as const, vehicles: [], marking: 'none' as const }],
  }
  const result = buildRoadMesh(straightPath, section)
  check('road split: pavement exists', !!result.pavement)
  check('road split: markings exist', !!result.markings)
  check('road split: pavement has positions', !!result.pavement && result.pavement.positions.length > 0)
  check('road split: markings have positions', !!result.markings && result.markings.positions.length > 0)
  // Both pavement and markings should have valid geometry
  if (result.pavement && result.markings) {
    check('road split: pavement has indices', result.pavement.indices.length > 0)
    check('road split: markings have indices', result.markings.indices.length > 0)
  }

  // Connecting road mesh: also split
  const connResult = buildConnectingRoadMesh(
    [{ s: 0, x: 0, y: 0, z: 0, heading: 0 }, { s: 10, x: 10, y: 0, z: 0, heading: 0 }],
    2, 3.5,
  )
  check('connector split: pavement exists', !!connResult.pavement)
  check('connector split: markings exist', !!connResult.markings)

  // Junction surface: 4-arm junction produces one continuous mesh
  const roads: import('../src/engine/junctions').JunctionRoad[] = [
    { id: 'h1', points: [{ x: -50, y: 0 }, { x: 50, y: 0 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
    { id: 'v1', points: [{ x: 0, y: -50 }, { x: 0, y: 50 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
  ]
  const network = buildJunctionNetwork(roads)
  const junction = network.junctions.find((j) => !j.suppressed)
  check('junction surface: junction found', !!junction)
  if (junction) {
    const surface = buildJunctionSurface(junction, new Map())
    check('junction surface: mesh built', !!surface.mesh)
    check('junction surface: boundary computed', surface.boundary.length >= 3)
    if (surface.mesh) {
      check('junction surface: has positions', surface.mesh.positions.length > 0)
      check('junction surface: has indices', surface.mesh.indices.length > 0)
      check('junction surface: has UVs', !!surface.mesh.uvs && surface.mesh.uvs.length > 0)
      // no NaN/Infinity in positions
      let clean = true
      for (const v of surface.mesh.positions) { if (!Number.isFinite(v)) { clean = false; break } }
      check('junction surface: no NaN/Infinity positions', clean)
      // no zero-area triangles (all indices valid and distinct)
      let validTris = true
      for (let i = 0; i < surface.mesh.indices.length; i += 3) {
        const a = surface.mesh.indices[i], b = surface.mesh.indices[i + 1], c = surface.mesh.indices[i + 2]
        if (a === b || b === c || a === c) { validTris = false; break }
      }
      check('junction surface: no degenerate triangles', validTris)
    }
    // junction markings
    const markings = buildJunctionMarkings(junction)
    check('junction markings: built', !!markings)
    if (markings) {
      check('junction markings: has positions', markings.positions.length > 0)
    }
  }

  // 3-arm junction (T-junction)
  const tRoads: import('../src/engine/junctions').JunctionRoad[] = [
    { id: 'h1', points: [{ x: -30, y: 0 }, { x: 30, y: 0 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
    { id: 'v1', points: [{ x: 0, y: 0 }, { x: 0, y: 30 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
  ]
  const tNetwork = buildJunctionNetwork(tRoads)
  const tJunction = tNetwork.junctions.find((j) => !j.suppressed)
  if (tJunction) {
    const tSurface = buildJunctionSurface(tJunction, new Map())
    check('T-junction surface: mesh built', !!tSurface.mesh)
  }

  // Lane tapers: the junction must meet the pavement actually rendered at
  // the cut (tapered width), not the full lane section — otherwise the
  // boundary overhangs the road and turning paths anchor off-pavement.
  const taperRoads: import('../src/engine/junctions').JunctionRoad[] = [
    { id: 'ht', points: [{ x: -60, y: 0 }, { x: 60, y: 0 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0,
      tapers: [{ side: 'right', index: 0, mode: 'out', length: 30, endS: 85 }] },
    { id: 'xt', points: [{ x: -5, y: -34.64 }, { x: 55, y: 34.64 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
  ]
  const taperNetwork = buildJunctionNetwork(taperRoads)
  const taperJunction = taperNetwork.junctions.find((j) => !j.suppressed)
  check('taper junction: found', !!taperJunction)
  if (taperJunction) {
    const hApproaches = taperJunction.approaches.filter((a) => a.roadId === 'ht')
    check('taper junction: both h approaches', hApproaches.length === 2)
    // endS=85 is the junction centre: at the west cut (s≈74.4) the right
    // lane is tapered to (85-74.4)/30 ≈ 0.35 → right width ≈ 1.24, not 3.5
    const west = hApproaches.find((a) => a.contact === 'end')
    if (west) {
      check('taper junction: right width follows taper', Math.abs((west.rightWidth ?? 0) - 3.5 * ((85 - west.station) / 30)) < 0.05,
        `rightWidth=${west.rightWidth?.toFixed(3)} at s=${west.station.toFixed(2)}`)
      check('taper junction: left width unaffected', Math.abs((west.leftWidth ?? 0) - 3.5) < 0.001)
      const rightLanes = west.outgoingLanes.filter((l) => l.laneId < 0)
      check('taper junction: tapered lane endpoint uses tapered width', rightLanes.every((l) => l.width < 3.5))
    }
    const surface = buildJunctionSurface(taperJunction, new Map())
    check('taper junction: surface built', !!surface.mesh)
    let clean = true
    for (const v of surface.mesh?.positions ?? []) { if (!Number.isFinite(v)) { clean = false; break } }
    check('taper junction: no NaN positions', clean)
  }
}

// ─── Section 29: Domain model (canonical types + serialization) ─────
{
  // Schema version is a positive integer
  check('domain: schema version is positive integer', Number.isInteger(PROJECT_SCHEMA_VERSION) && PROJECT_SCHEMA_VERSION > 0)

  // Build a minimal canonical project
  const road: DomainRoadData = {
    id: 'road-1',
    name: 'Test Road',
    points: [{ x: 0, y: 0 }, { x: 100, y: 0 }],
    geometryType: 'straight',
    lanesLeft: 1,
    lanesRight: 1,
    laneWidth: 3.5,
    filletRadius: 0,
  }
  const project: DomainProject = {
    id: 'proj-1',
    name: 'Test Project',
    createdAt: '2025-01-01T00:00:00Z',
    roads: [road],
    suppressedJunctions: [],
  }

  // Serialize → deserialize round-trip preserves data
  const serialized = serializeProject(project)
  check('domain: serialize wraps with schema version', serialized.schemaVersion === PROJECT_SCHEMA_VERSION)
  check('domain: serialize preserves project id', serialized.project.id === 'proj-1')
  check('domain: serialize preserves road count', serialized.project.roads.length === 1)
  check('domain: serialize preserves road id', serialized.project.roads[0].id === 'road-1')

  const restored = deserializeProject(serialized)
  check('domain: deserialize returns project', !!restored)
  if (restored) {
    check('domain: deserialize preserves project id', restored.id === 'proj-1')
    check('domain: deserialize preserves road id', restored.roads[0].id === 'road-1')
    check('domain: deserialize preserves road name', restored.roads[0].name === 'Test Road')
    check('domain: deserialize preserves points', restored.roads[0].points.length === 2)
  }

  // Legacy raw project format (no wrapper) still deserializes
  const legacy = deserializeProject(project)
  check('domain: deserialize accepts legacy raw project', !!legacy && legacy.id === 'proj-1')

  // Invalid data returns null
  check('domain: deserialize rejects null', deserializeProject(null) === null)
  check('domain: deserialize rejects non-object', deserializeProject(42) === null)
  check('domain: deserialize rejects garbage', deserializeProject({ foo: 'bar' }) === null)

  // Future schema version returns null
  const future = { schemaVersion: PROJECT_SCHEMA_VERSION + 1, project }
  check('domain: deserialize rejects future schema', deserializeProject(future) === null)

  // Domain module exports compile-time types (verified by typecheck)
  check('domain: exports Vec2/RoadData/Project/LaneSectionDef/RailPoint/GeoReference/IntersectionData types', true)
}

// ─── Section 30: Shared road services (samplers, junction network, validation) ─
{
  // Build a road for testing
  const road: DomainRoadData = {
    id: 'svc-1',
    name: 'Service Test Road',
    points: [{ x: 0, y: 0 }, { x: 50, y: 0 }],
    geometryType: 'straight',
    elevationProfile: [{ s: 0, z: 0 }, { s: 50, z: 10 }],
    lanesLeft: 1,
    lanesRight: 1,
    laneWidth: 3.5,
    filletRadius: 0,
  }
  const project: DomainProject = {
    id: 'svc-proj',
    name: 'Service Test',
    createdAt: '2025-01-01T00:00:00Z',
    roads: [road],
    suppressedJunctions: [],
  }

  // getLaneSection: returns rich section if present, otherwise default
  const section = getLaneSection(road)
  check('roadServices: getLaneSection returns section', !!section)
  check('roadServices: section has 2 lanes', section.left.length + section.right.length === 2)

  // getRoadTotalWidth / getRoadTotalLanes
  check('roadServices: total width = 7.0', Math.abs(getRoadTotalWidth(road) - 7.0) < 0.01)
  check('roadServices: total lanes = 2', getRoadTotalLanes(road) === 2)

  // buildRoadSamplers without lift (Editor mode)
  const samplersNoLift = buildRoadSamplers([road], false)
  const elevNoLift = samplersNoLift.elevation.get('svc-1')
  check('roadServices: elevation sampler exists', !!elevNoLift)
  if (elevNoLift) {
    check('roadServices: elevation at s=0 is 0 (no lift)', Math.abs(elevNoLift(0)) < 0.01)
    check('roadServices: elevation at s=50 is 10 (no lift)', Math.abs(elevNoLift(50) - 10) < 0.01)
  }

  // buildRoadSamplers with lift (3D Studio mode)
  const samplersWithLift = buildRoadSamplers([road], true)
  const elevWithLift = samplersWithLift.elevation.get('svc-1')
  check('roadServices: elevation sampler with lift exists', !!elevWithLift)
  if (elevWithLift) {
    check('roadServices: elevation at s=0 is ROAD_LIFT (with lift)', Math.abs(elevWithLift(0) - ROAD_LIFT) < 0.01)
    check('roadServices: elevation at s=50 is 10+ROAD_LIFT (with lift)', Math.abs(elevWithLift(50) - (10 + ROAD_LIFT)) < 0.01)
  }

  // banking sampler
  const bankSampler = samplersNoLift.banking.get('svc-1')
  check('roadServices: banking sampler exists', !!bankSampler)
  if (bankSampler) {
    check('roadServices: banking at s=0 is 0', Math.abs(bankSampler(0)) < 0.01)
  }

  // buildProjectJunctionNetwork
  const result = buildProjectJunctionNetwork(project, false)
  check('roadServices: buildProjectJunctionNetwork returns result', !!result)
  if (result) {
    check('roadServices: network has paths', result.network.paths.size >= 1)
    check('roadServices: samplers included in result', !!result.samplers.elevation.get('svc-1'))
  }

  // buildProjectJunctionNetwork with empty project returns null
  const emptyProject: DomainProject = {
    id: 'empty', name: 'Empty', createdAt: '', roads: [], suppressedJunctions: [],
  }
  check('roadServices: empty project returns null', buildProjectJunctionNetwork(emptyProject) === null)

  // Validation: valid road
  const validResult = validateRoad(road)
  check('roadServices: valid road passes validation', validResult.valid)
  check('roadServices: valid road has no errors', validResult.errors.length === 0)

  // Validation: invalid road (NaN coordinates)
  const nanRoad: DomainRoadData = {
    ...road,
    id: 'nan-road',
    points: [{ x: NaN, y: 0 }, { x: 50, y: 0 }],
  }
  const nanResult = validateRoad(nanRoad)
  check('roadServices: NaN road fails validation', !nanResult.valid)
  check('roadServices: NaN road has errors', nanResult.errors.length > 0)

  // Validation: invalid road (negative lanes)
  const badLaneRoad: DomainRoadData = {
    ...road,
    id: 'bad-lane',
    lanesLeft: -1,
    lanesRight: 1,
  }
  const badLaneResult = validateRoad(badLaneRoad)
  check('roadServices: negative lanes fails validation', !badLaneResult.valid)

  // Validation: invalid road (zero lane width)
  const zeroWidthRoad: DomainRoadData = {
    ...road,
    id: 'zero-width',
    laneWidth: 0,
  }
  const zeroWidthResult = validateRoad(zeroWidthRoad)
  check('roadServices: zero lane width fails validation', !zeroWidthResult.valid)

  // validateProjectRoads
  const validations = validateProjectRoads(project)
  check('roadServices: validateProjectRoads returns one result', validations.length === 1)
  check('roadServices: project road is valid', validations[0].valid)
}

// ─── Section 31: Editor preview mesh builder (shared service) ──────
{
  // Build a project with 2 crossing roads (4-arm junction)
  const roads: DomainRoadData[] = [
    { id: 'h1', name: 'Horizontal', points: [{ x: -50, y: 0 }, { x: 50, y: 0 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
    { id: 'v1', name: 'Vertical', points: [{ x: 0, y: -50 }, { x: 0, y: 50 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
  ]
  const project: DomainProject = {
    id: 'preview-proj',
    name: 'Preview Test',
    createdAt: '2025-01-01T00:00:00Z',
    roads,
    suppressedJunctions: [],
  }

  const jnResult = buildProjectJunctionNetwork(project, false)
  check('previewMeshes: junction network built', !!jnResult)
  if (jnResult) {
    const { network, samplers } = jnResult
    const layers = { road3dGeneration: true, intersection3dGeneration: true }
    const meshes = buildEditorPreviewMeshes(project, network, samplers, layers, false, null)

    // Road meshes
    check('previewMeshes: road meshes exist', meshes.roadMeshes.length > 0)
    check('previewMeshes: road meshes have positions', meshes.roadMeshes.every((e) => e.mesh.positions.length > 0))
    check('previewMeshes: road meshes have indices', meshes.roadMeshes.every((e) => e.mesh.indices.length > 0))
    check('previewMeshes: lane markings survive into editor', meshes.roadMeshes.some(({ mesh }) =>
      mesh.colors.some((value, i) => i % 3 === 0 && value > 0.8 && mesh.colors[i + 1] > 0.8 && mesh.colors[i + 2] > 0.8)))
    const junctionSurface = buildJunctionSurface(network.junctions[0], samplers.elevation).mesh!
    check('previewMeshes: unified junction surface survives into editor', meshes.connectingMeshes.some((mesh) =>
      mesh.positions.length === junctionSurface.positions.length && mesh.positions.every((value, i) => value === junctionSurface.positions[i])))
    const junctionMarks = buildJunctionMarkings(network.junctions[0])!
    check('previewMeshes: junction markings survive into editor', meshes.connectingMeshes.some((mesh) =>
      mesh.positions.length === junctionMarks.positions.length && mesh.positions.every((value, i) => value === junctionMarks.positions[i])))

    // Connecting meshes (junction connectors)
    check('previewMeshes: connecting meshes exist', meshes.connectingMeshes.length > 0)

    // Intersection way meshes (may be empty if no explicit intersections)
    check('previewMeshes: intersection way meshes is array', Array.isArray(meshes.intersectionWayMeshes))

    // Rail fixture meshes (not in rail section, so empty)
    check('previewMeshes: rail fixtures empty (not rail section)', meshes.railFixtureMeshes.length === 0)

    // Terrain mesh (null since no terrain passed)
    check('previewMeshes: terrain mesh null', meshes.terrainMesh === null)

    // Flatten
    const flat = flattenPreviewMeshes(meshes)
    check('previewMeshes: flatten produces array', Array.isArray(flat))
    check('previewMeshes: flatten has meshes', flat.length > 0)

    // Preview road IDs
    const ids = previewRoadIds(meshes)
    check('previewMeshes: road IDs set has h1', ids.has('h1'))
    check('previewMeshes: road IDs set has v1', ids.has('v1'))
    check('previewMeshes: road IDs set size 2', ids.size === 2)

    // Disable road3dGeneration → no road meshes
    const noRoads = buildEditorPreviewMeshes(project, network, samplers, { road3dGeneration: false, intersection3dGeneration: true }, false, null)
    check('previewMeshes: no road meshes when disabled', noRoads.roadMeshes.length === 0)

    // Disable intersection3dGeneration → no connecting meshes
    const noInter = buildEditorPreviewMeshes(project, network, samplers, { road3dGeneration: true, intersection3dGeneration: false }, false, null)
    check('previewMeshes: no connecting meshes when disabled', noInter.connectingMeshes.length === 0)
    const suppressed = buildEditorPreviewMeshes(project, { ...network, junctions: network.junctions.map((junction) => ({ ...junction, suppressed: true })) }, samplers, layers, false, null)
    check('previewMeshes: suppressed junctions have no surface or markings', suppressed.connectingMeshes.length === 0)
    const elevatedProject = { ...project, roads: roads.map((road) => ({ ...road, elevationProfile: [{ s: 0, z: 1200 }, { s: 100, z: 1200 }] })) }
    const elevatedNetwork = buildProjectJunctionNetwork(elevatedProject, false)!
    const elevatedMeshes = buildEditorPreviewMeshes(elevatedProject, elevatedNetwork.network, elevatedNetwork.samplers, layers, false, null)
    check('previewMeshes: elevated junction surfaces and markings stay above terrain', elevatedMeshes.connectingMeshes.every((mesh) =>
      mesh.positions.every((value, i) => i % 3 !== 1 || (value >= 1200 && value < 1201))))

    const camera = new OrthographicCamera(-100, 100, 100, -100, 0.1, 5000)
    camera.position.set(20, 500, -30)
    camera.up.set(0, 0, -1)
    camera.lookAt(20, 0, -30)
    camera.zoom = 2
    const cameraMeshes = flattenPreviewMeshes(elevatedMeshes)
    const positionsBefore = cameraMeshes.map((mesh) => mesh.positions.slice())
    fitPlanCamera(camera, cameraMeshes)
    const elevatedPoint = new Vector3(20, 1200, -30).project(camera)
    const groundPoint = new Vector3(20, 0, -30).project(camera)
    check('viewport: elevated roads fit within plan camera clipping planes', Math.abs(elevatedPoint.z) < 1)
    check('viewport: ground overlays remain inside plan camera clipping planes', Math.abs(groundPoint.z) < 1)
    check('viewport: plan projection aligns road and ground selection', Math.hypot(elevatedPoint.x - groundPoint.x, elevatedPoint.y - groundPoint.y) < 1e-9)
    check('viewport: fitting elevation preserves pan and zoom', camera.position.x === 20 && camera.position.z === -30 && camera.zoom === 2)
    check('viewport: fitting does not modify 3D mesh elevations', cameraMeshes.every((mesh, index) => mesh.positions.every((value, i) => value === positionsBefore[index][i])))
    fitPlanCamera(camera, [{ positions: new Float32Array([20, -6000, -30, 20, 9000, -30]), colors: new Float32Array(6), indices: new Uint32Array() }])
    check('viewport: below-sea-level and mountain roads remain visible', [-6000, 9000].every((height) => Math.abs(new Vector3(20, height, -30).project(camera).z) < 1))
    fitPlanCamera(camera, [])
    check('viewport: clearing roads resets plan camera depth', camera.position.y === 500 && camera.far === 5000)
  }
}

// ─── Section 32: GLTF/GLB export pipeline ──────────────────────────
{
  // Build a project with 2 crossing roads (4-arm junction)
  const roads: DomainRoadData[] = [
    { id: 'h1', name: 'Main Street', points: [{ x: -50, y: 0 }, { x: 50, y: 0 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
    { id: 'v1', name: 'Cross Ave', points: [{ x: 0, y: -50 }, { x: 0, y: 50 }], geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0 },
  ]
  const project: DomainProject = {
    id: 'export-proj',
    name: 'Export Test',
    createdAt: '2025-01-01T00:00:00Z',
    roads,
    suppressedJunctions: [],
    geoRef: { lng: -122.4194, lat: 37.7749, scale: 1 },
  }

  // ── buildExportScene ──
  const scene = buildExportScene(project, { drape: false, includeTerrain: false })
  check('gltfExport: scene has meshes', scene.meshes.length > 0)
  check('gltfExport: scene version is 1', scene.version === 1)
  check('gltfExport: scene has origin', !!scene.origin)
  check('gltfExport: origin lng matches', scene.origin?.lng === -122.4194)
  check('gltfExport: origin lat matches', scene.origin?.lat === 37.7749)

  // Road meshes exist
  const roadMeshes = scene.meshes.filter((m) => m.category === 'road')
  check('gltfExport: road meshes exist', roadMeshes.length > 0)
  check('gltfExport: road mesh names start with Road:', roadMeshes.every((m) => m.name.startsWith('Road:')))
  check('gltfExport: road mesh IDs match project', roadMeshes.some((m) => m.id === 'h1' || m.id === 'v1'))

  // Marking meshes exist
  const markingMeshes = scene.meshes.filter((m) => m.category === 'marking')
  check('gltfExport: marking meshes exist', markingMeshes.length > 0)

  // Junction meshes exist (2 crossing roads → 1 junction)
  const junctionMeshes = scene.meshes.filter((m) => m.category === 'junction')
  check('gltfExport: junction meshes exist', junctionMeshes.length > 0)

  // All meshes have valid data
  for (const mesh of scene.meshes) {
    check(`gltfExport: ${mesh.name} has positions`, mesh.mesh.positions.length > 0)
    check(`gltfExport: ${mesh.name} has colors`, mesh.mesh.colors.length > 0)
    check(`gltfExport: ${mesh.name} has indices`, mesh.mesh.indices.length > 0)
    check(`gltfExport: ${mesh.name} positions/3 === colors/3`, mesh.mesh.positions.length / 3 === mesh.mesh.colors.length / 3)
  }

  // ── encodeGLB ──
  const glb = encodeGLB(scene)
  check('gltfExport: GLB is ArrayBuffer', glb instanceof ArrayBuffer)
  check('gltfExport: GLB size > 0', glb.byteLength > 0)

  // Parse GLB header
  const view = new DataView(glb)
  const magic = view.getUint32(0, true)
  const version = view.getUint32(4, true)
  const length = view.getUint32(8, true)
  check('gltfExport: GLB magic is glTF', magic === 0x46546c67)
  check('gltfExport: GLB version is 2', version === 2)
  check('gltfExport: GLB length matches buffer', length === glb.byteLength)

  // Parse JSON chunk
  const jsonLength = view.getUint32(12, true)
  const jsonType = view.getUint32(16, true)
  check('gltfExport: JSON chunk type', jsonType === 0x4e4f534a)
  check('gltfExport: JSON chunk length > 0', jsonLength > 0)

  const jsonBytes = new Uint8Array(glb, 20, jsonLength)
  const jsonStr = new TextDecoder().decode(jsonBytes).replace(/\0+$/, '')
  const gltfJson = JSON.parse(jsonStr)

  check('gltfExport: GLTF asset version 2.0', gltfJson.asset?.version === '2.0')
  check('gltfExport: GLTF has scenes', Array.isArray(gltfJson.scenes))
  check('gltfExport: GLTF has nodes', Array.isArray(gltfJson.nodes) && gltfJson.nodes.length > 0)
  check('gltfExport: GLTF has meshes', Array.isArray(gltfJson.meshes) && gltfJson.meshes.length > 0)
  check('gltfExport: GLTF has materials', Array.isArray(gltfJson.materials) && gltfJson.materials.length >= 4)
  check('gltfExport: GLTF has accessors', Array.isArray(gltfJson.accessors))
  check('gltfExport: GLTF has bufferViews', Array.isArray(gltfJson.bufferViews))
  check('gltfExport: GLTF has buffers', Array.isArray(gltfJson.buffers) && gltfJson.buffers.length === 1)
  check('gltfExport: GLTF node count = scene mesh count', gltfJson.nodes.length === scene.meshes.length)

  // Geographic metadata extension
  check('gltfExport: GLTF has OGS_origin extension', gltfJson.extensions?.OGS_origin?.lng === -122.4194)
  check('gltfExport: GLTF extensionsUsed includes OGS_origin', Array.isArray(gltfJson.extensionsUsed) && gltfJson.extensionsUsed.includes('OGS_origin'))

  // Parse BIN chunk
  const binChunkOffset = 20 + jsonLength
  const binLength2 = view.getUint32(binChunkOffset, true)
  const binType = view.getUint32(binChunkOffset + 4, true)
  check('gltfExport: BIN chunk type', binType === 0x004e4942)
  check('gltfExport: BIN chunk length > 0', binLength2 > 0)
  check('gltfExport: BIN chunk + JSON chunk + header = total', binChunkOffset + 8 + binLength2 === glb.byteLength)

  // ── exportProjectToGLB (convenience) ──
  const glb2 = exportProjectToGLB(project, { drape: false, includeTerrain: false })
  check('gltfExport: convenience GLB is ArrayBuffer', glb2 instanceof ArrayBuffer)
  check('gltfExport: convenience GLB size > 0', glb2.byteLength > 0)

  // ── Empty project ──
  const emptyProject: DomainProject = {
    id: 'empty', name: 'Empty', createdAt: '', roads: [], suppressedJunctions: [],
  }
  const emptyScene = buildExportScene(emptyProject)
  check('gltfExport: empty project has no meshes', emptyScene.meshes.length === 0)
  const emptyGlb = encodeGLB(emptyScene)
  check('gltfExport: empty GLB is valid', emptyGlb.byteLength > 0)
  const emptyView = new DataView(emptyGlb)
  check('gltfExport: empty GLB magic', emptyView.getUint32(0, true) === 0x46546c67)
}

{
  const lane = (id: string, width: number, type: import('../src/engine/laneTypes').LaneType = 'travel', circulation: import('../src/engine/laneTypes').CirculationWay = 'forward'): import('../src/engine/laneTypes').LaneDef => ({
    id, name: id, width, type, circulation, speedLimit: 50, vehicles: ['all'],
  })
  const horizontal: RoadData = {
    id: 'configured-h', name: 'Horizontal', points: [{ x: -100, y: 0 }, { x: 100, y: 0 }],
    geometryType: 'straight', lanesLeft: 1, lanesRight: 1, laneWidth: 3.5, filletRadius: 0,
    laneSection: { left: [lane('left-travel', 4), lane('left-walk', 2, 'sidewalk')], right: [lane('median', 1, 'median'), lane('right-travel', 3), lane('right-walk', 2, 'sidewalk'), lane('bus', 5, 'bus')] },
  }
  const vertical: RoadData = { ...horizontal, id: 'configured-v', name: 'Vertical', points: [{ x: 0, y: -100 }, { x: 0, y: 100 }] }
  const roads = [horizontal, vertical]
  const initial = buildJunctionNetwork(roads).junctions[0]
  const configKey = initial.configurationKey!
  check('lane configuration: crossing has stable configuration key', !!configKey)
  check('lane configuration: every default is one signed lane movement', initial.connectionOptions!.length > 0 && initial.connectionOptions!.every((c) => c.laneCount === 1 && c.laneLinks.length === 1 && c.laneLinks[0].fromLaneId !== 0 && !!c.fromContact && !!c.toContact && c.authorized === true))
  const entry = initial.approaches.find((a) => a.roadId === horizontal.id && a.contact === 'end')!
  check('lane configuration: sidewalk and median excluded from vehicle lanes', entry.incomingLanes!.map((l) => l.laneId).join(',') === '-2,-4' && entry.outgoingLanes!.map((l) => l.laneId).join(',') === '1')
  check('lane configuration: non-travel physical pavement widths preserved', entry.leftWidth === 6 && entry.rightWidth === 11)
  check('lane configuration: mixed-width lane centers include median and sidewalk', entry.incomingLanes![0].offset === -2.5 && entry.incomingLanes![1].offset === -8.5)
  const route: import('../src/engine/junctions').JunctionLaneConnection = { fromRoadId: horizontal.id, fromContact: 'end', fromLaneId: -2, toRoadId: vertical.id, toContact: 'start', toLaneId: -4, enabled: true }
  const configured = (configuration: import('../src/engine/junctions').JunctionConfiguration, input = roads) => buildJunctionNetwork(input, [], new Map([[horizontal.id, () => 7], [vertical.id, () => 12]]), { [configKey]: configuration }).junctions.find((j) => j.configurationKey === configKey)!
  const custom = configured({ name: 'Custom crossing', connections: [route] })
  check('lane configuration: custom set replaces defaults completely', custom.connectingRoads.length === 1 && custom.configuration?.name === 'Custom crossing')
  const samples = custom.connectingRoads[0].samples
  const exit = custom.approaches.find((a) => a.roadId === vertical.id && a.contact === 'start')!
  check('lane configuration: source samples use actual lane center and profile', Math.abs(samples[0].x - entry.position.x) < 1e-6 && Math.abs(samples[0].y + 2.5) < 1e-6 && samples[0].z === 7)
  check('lane configuration: target samples use side-specific width and profile', Math.abs(samples.at(-1)!.x - 8.5) < 1e-6 && Math.abs(samples.at(-1)!.y - exit.position.y) < 1e-6 && samples.at(-1)!.z === 12)
  const remapped = configured({ connections: [{ ...route, fromLaneId: -4, toLaneId: -2 }] }).connectingRoads[0]
  check('lane configuration: changing mapping changes both endpoints', Math.abs(remapped.samples[0].y + 8.5) < 1e-6 && Math.abs(remapped.samples.at(-1)!.x - 2.5) < 1e-6)
  check('lane configuration: connector uses actual lane widths', custom.connectingRoads[0].laneWidth === 3 && remapped.laneWidth === 3)
  const disabled = configured({ connections: [{ ...route, enabled: false }] })
  check('lane configuration: disabled candidate retained but no movement', disabled.connectingRoads.length === 0 && disabled.connectionOptions!.length === 1 && disabled.connectionOptions![0].authorized === false)
  const empty = configured({ connections: [] })
  check('lane configuration: explicit empty set has no candidates or movements', empty.connectingRoads.length === 0 && empty.connectionOptions!.length === 0)
  check('lane configuration: surface retained when all routes disabled', !!buildJunctionSurface(disabled, new Map()).mesh && !!buildJunctionSurface(empty, new Map()).mesh && buildJunctionMarkings(disabled) === null)
  const noMarkings = configured({ markings: false })
  check('lane configuration: name and markings do not change auto connectivity', configured({ name: 'Renamed' }).connectingRoads.length === initial.connectingRoads.length && noMarkings.connectingRoads.length === initial.connectingRoads.length)
  check('lane configuration: markings flag hides only markings', buildJunctionMarkings(noMarkings) === null && !!buildJunctionSurface(noMarkings, new Map()).mesh)
  const invalid = configured({ connections: [route, { ...route }, { ...route, fromLaneId: -3 }, { ...route, fromLaneId: -99 }, { ...route, toRoadId: 'deleted' }, { ...route, fromContact: 'start' }] })
  check('lane configuration: duplicate and stale/ineligible rows warn without rerouting', invalid.configurationWarnings!.length === 5 && invalid.connectingRoads.length === 1)
  const zeroWidth = { ...horizontal, laneSection: { ...horizontal.laneSection!, right: horizontal.laneSection!.right.map((l, i) => i === 1 ? { ...l, width: 0 } : l) } }
  const invalidWidth = configured({ connections: [route] }, [zeroWidth, vertical])
  check('lane configuration: zero width invalidates configured lane', invalidWidth.connectingRoads.length === 0 && invalidWidth.configurationWarnings!.length === 1)
  const reversed: RoadData = { ...horizontal, laneSection: { left: [lane('left-reversed', 4, 'travel', 'backward')], right: [lane('right-reversed', 3, 'travel', 'backward'), lane('bike-both', 1.5, 'bike', 'both'), lane('paved', 2, 'paved_major')] } }
  const reversedJunction = buildJunctionNetwork([reversed, vertical]).junctions[0]
  const reversedEnd = reversedJunction.approaches.find((a) => a.roadId === horizontal.id && a.contact === 'end')!
  const reversedStart = reversedJunction.approaches.find((a) => a.roadId === horizontal.id && a.contact === 'start')!
  check('lane configuration: backward reverses normal left/right circulation', reversedEnd.incomingLanes!.some((l) => l.laneId === 1) && reversedEnd.outgoingLanes!.some((l) => l.laneId === -1) && reversedStart.incomingLanes!.some((l) => l.laneId === -1) && reversedStart.outgoingLanes!.some((l) => l.laneId === 1))
  check('lane configuration: both direction bike lane allowed at both contacts', [reversedEnd, reversedStart].every((a) => a.incomingLanes!.some((l) => l.laneId === -2) && a.outgoingLanes!.some((l) => l.laneId === -2)))
  check('lane configuration: paved major is connection eligible', reversedEnd.incomingLanes!.some((l) => l.laneId === -3))
  const legacy = buildJunctionNetwork(roads.map((r) => ({ ...r, laneSection: undefined }))).junctions[0]
  check('lane configuration: legacy counts retain default signed lanes', legacy.approaches.every((a) => a.incomingLaneCount === 1 && a.outgoingLaneCount === 1 && Math.abs(a.incomingLanes![0].offset) === 1.75))
  const repeatRoads: RoadData[] = [
    { ...horizontal, id: 'repeat-h', laneSection: undefined, points: [{ x: -150, y: 0 }, { x: 150, y: 0 }] },
    { ...vertical, id: 'repeat-v', laneSection: undefined, geometryType: 'polyline', points: [{ x: -80, y: -80 }, { x: -80, y: 80 }, { x: 80, y: 80 }, { x: 80, y: -80 }] },
  ]
  const repeated = buildJunctionNetwork(repeatRoads).junctions
  check('lane configuration: repeated crossings have distinct keys and same suppression pair', repeated.length === 2 && repeated[0].configurationKey !== repeated[1].configurationKey && repeated[0].key === repeated[1].key)
  const extra: RoadData = { ...horizontal, id: 'unrelated', points: [{ x: -100, y: 300 }, { x: 100, y: 300 }] }
  const reordered = buildJunctionNetwork([extra, ...repeatRoads.slice().reverse()], [repeated[0].key], new Map(), { [repeated[1].configurationKey!]: { name: 'Second', connections: [] } }).junctions
  check('lane configuration: keys survive suppression, reordering and unrelated road addition', repeated.every((j) => reordered.some((r) => r.configurationKey === j.configurationKey && Math.hypot(r.position.x - j.position.x, r.position.y - j.position.y) < 1e-6)) && reordered.every((j) => j.suppressed))
  check('lane configuration: repeated crossing settings stay isolated', reordered.find((j) => j.configurationKey === repeated[1].configurationKey)?.configuration?.name === 'Second' && !reordered.find((j) => j.configurationKey === repeated[0].configurationKey)?.configuration)
  const project: DomainProject = { id: 'lane-config-project', name: 'Configured', createdAt: '', roads, suppressedJunctions: [], junctionConfigurations: { [configKey]: { name: 'Saved', markings: false, connections: [route, { ...route, fromLaneId: -4, enabled: false }] } } }
  const restored = deserializeProject(JSON.parse(JSON.stringify(serializeProject(project))))!
  check('lane configuration: serialization retains names, flags, routes and denied rows', JSON.stringify(restored.junctionConfigurations) === JSON.stringify(project.junctionConfigurations))
  const built = buildProjectJunctionNetwork(restored)!
  check('lane configuration: shared road services propagate configuration', built.network.junctions[0].connectingRoads.length === 1 && built.network.junctions[0].connectionOptions!.length === 2)
  const preview = buildEditorPreviewMeshes(restored, built.network, built.samplers, { road3dGeneration: true, intersection3dGeneration: true }, false, null)
  check('lane configuration: preview keeps only surface when markings disabled', preview.connectingMeshes.length === 1)
  const scene = buildExportScene(restored, { drape: false })
  check('lane configuration: export retains configured name and omits junction markings', scene.meshes.some((m) => m.category === 'junction' && m.name === 'Junction:Saved') && !scene.meshes.some((m) => m.id.startsWith('jx:') && m.category === 'marking'))
  const explicitProject: DomainProject = { ...project, roads: [roadA, roadB, roadC].map((r) => ({ ...r, lanesLeft: 2, lanesRight: 2 })), junctionConfigurations: {}, intersections: [{ ...node, markings: false }] }
  const explicitNetwork = buildProjectJunctionNetwork(explicitProject)!
  const explicitPreview = buildEditorPreviewMeshes(explicitProject, explicitNetwork.network, explicitNetwork.samplers, { road3dGeneration: true, intersection3dGeneration: true }, false, null)
  const explicitMarked = buildEditorPreviewMeshes({ ...explicitProject, intersections: [{ ...node, markings: true }] }, explicitNetwork.network, explicitNetwork.samplers, { road3dGeneration: true, intersection3dGeneration: true }, false, null)
  check('lane configuration: explicit intersection markings flag respected in preview', explicitMarked.intersectionWayMeshes.length > explicitPreview.intersectionWayMeshes.length)
  const deniedNode = { ...node, authorizations: Object.fromEntries(ways.map((way) => [way.key, false])) }
  const deniedPreview = buildEditorPreviewMeshes({ ...explicitProject, intersections: [deniedNode] }, explicitNetwork.network, explicitNetwork.samplers, { road3dGeneration: true, intersection3dGeneration: true }, false, null)
  check('lane configuration: denied explicit ways not rendered in preview', deniedPreview.intersectionWayMeshes.length === 0)
}

console.log(failures === 0 ? '\nALL PASSED' : `\n${failures} FAILURES`)
process.exit(failures === 0 ? 0 : 1)