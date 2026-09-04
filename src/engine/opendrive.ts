// ─────────────────────────────────────────────────────────────────────
// Minimal OpenDRIVE (.xodr) importer — source of the doc's "Explicits
// Ways: built from an importation of logical description of roads
// networks files" (SCANeR Roads tab, ways visualisation chapter).
//
// Supported subset:
//  - planView geometries: <line>, <arc>, <spiral> (→ clothoid), <paramPoly3>
//    (sampled to a polyline); everything else falls back to a segment
//  - road elevationProfile polynomials (sampled to a profile)
//  - junctions: nodes with explicit ways built from the connecting roads
// Lanes, objects and signals are ignored (default section is applied).
// ─────────────────────────────────────────────────────────────────────
import type { ElevationPoint, } from './elevation'
import type { PathSample } from './geometry'
import type { Vec2 } from './types'
import type { XYFunction } from './xyFunctions'
import type { IntersectionData, IntersectionWay } from './intersections'
import { makeIntersectionData } from './intersections'
import type { RoadData } from '../state/store'
import { makeDefaultSection } from './laneLayout'

export interface OdrImportResult {
  roads: RoadData[]
  intersections: IntersectionData[]
  warnings: string[]
}

interface OdrGeometry {
  s: number
  x: number
  y: number
  hdg: number
  length: number
  type: 'line' | 'arc' | 'spiral' | 'paramPoly3' | 'other'
  curvature?: number
  curvStart?: number
  curvEnd?: number
  poly?: { aU: number; bU: number; cU: number; dU: number; aV: number; bV: number; cV: number; dV: number; normalized: boolean }
}

function num(el: Element, attr: string, fallback = 0): number {
  const v = el.getAttribute(attr)
  if (v === null || v === '') return fallback
  const parsed = Number.parseFloat(v)
  return Number.isFinite(parsed) ? parsed : fallback
}

function parseGeometry(geo: Element): OdrGeometry {
  const base: OdrGeometry = {
    s: num(geo, 's'),
    x: num(geo, 'x'),
    y: num(geo, 'y'),
    hdg: num(geo, 'hdg'),
    length: num(geo, 'length'),
    type: 'other',
  }
  const line = geo.querySelector('line')
  if (line) return { ...base, type: 'line' }
  const arc = geo.querySelector('arc')
  if (arc) return { ...base, type: 'arc', curvature: num(arc, 'curvature') }
  const spiral = geo.querySelector('spiral')
  if (spiral) return { ...base, type: 'spiral', curvStart: num(spiral, 'curvStart'), curvEnd: num(spiral, 'curvEnd') }
  const poly = geo.querySelector('paramPoly3')
  if (poly) {
    return {
      ...base,
      type: 'paramPoly3',
      poly: {
        aU: num(poly, 'aU'), bU: num(poly, 'bU'), cU: num(poly, 'cU'), dU: num(poly, 'dU'),
        aV: num(poly, 'aV'), bV: num(poly, 'bV'), cV: num(poly, 'cV'), dV: num(poly, 'dV'),
        normalized: poly.getAttribute('pRange') === 'normalized',
      },
    }
  }
  return base
}

function geometryToFunction(g: OdrGeometry): XYFunction | null {
  switch (g.type) {
    case 'line':
      return { kind: 'segment', length: Math.max(0.01, g.length) }
    case 'arc': {
      const curvature = g.curvature ?? 0
      if (Math.abs(curvature) < 1e-9) return { kind: 'segment', length: Math.max(0.01, g.length) }
      return { kind: 'arc', radius: 1 / Math.abs(curvature), angle: curvature * g.length }
    }
    case 'spiral': {
      const rIn = (g.curvStart ?? 0) !== 0 ? 1 / (g.curvStart ?? 0) : 0
      const rOut = (g.curvEnd ?? 0) !== 0 ? 1 / (g.curvEnd ?? 0) : 0
      return { kind: 'clothoid', radiusIn: rIn, radiusOut: rOut, length: Math.max(0.01, g.length) }
    }
    case 'paramPoly3': {
      const p = g.poly
      if (!p) return null
      const steps = Math.max(8, Math.ceil(g.length / 2))
      const points: Vec2[] = []
      for (let i = 0; i <= steps; i++) {
        const t = i / steps
        // pRange="arcLength" → u is arc length; "normalized" → u ∈ [0,1] and
        // the resulting local (u,v) is scaled by the geometry length
        const uVal = p.normalized ? t : g.length * t
        const scale = p.normalized ? g.length : 1
        const along = (p.aU + p.bU * uVal + p.cU * uVal * uVal + p.dU * uVal * uVal * uVal) * scale
        const lat = (p.aV + p.bV * uVal + p.cV * uVal * uVal + p.dV * uVal * uVal * uVal) * scale
        points.push({
          x: g.x + Math.cos(g.hdg) * along - Math.sin(g.hdg) * lat,
          y: g.y + Math.sin(g.hdg) * along + Math.cos(g.hdg) * lat,
        })
      }
      return points.length >= 2 ? { kind: 'polyline', points, splineType: 'segment' } : null
    }
    default:
      return { kind: 'segment', length: Math.max(0.01, g.length) }
  }
}

function parseElevations(road: Element): ElevationPoint[] | undefined {
  const elevations = [...road.querySelectorAll('elevationProfile > elevation')]
  if (elevations.length === 0) return undefined
  const points: ElevationPoint[] = []
  for (const el of elevations) {
    const s0 = num(el, 's')
    const a = num(el, 'a')
    const b = num(el, 'b')
    const c = num(el, 'c')
    const d = num(el, 'd')
    const s1 = elevations.indexOf(el) + 1 < elevations.length ? num(elevations[elevations.indexOf(el) + 1], 's') : s0 + 50
    const span = Math.max(0.01, s1 - s0)
    const steps = Math.max(2, Math.ceil(span / 10))
    for (let i = 0; i < steps; i++) {
      const ds = (span * i) / steps
      points.push({ s: s0 + ds, z: a + b * ds + c * ds * ds + d * ds * ds * ds })
    }
  }
  return points.length >= 2 ? points : undefined
}

function samplePolyline(functions: XYFunction[], start: { x: number; y: number; heading: number }, ds = 3): PathSample[] {
  // walk the function chain with the shared sampling helpers
  const samples: PathSample[] = [{ s: 0, x: start.x, y: start.y, heading: start.heading }]
  let frame = start
  let s = 0
  for (const fn of functions) {
    const length = fn.kind === 'segment' ? fn.length
      : fn.kind === 'arc' ? Math.abs(fn.radius * fn.angle)
      : fn.kind === 'clothoid' ? fn.length
      : fn.kind === 'polyline' ? polyLength(fn.points)
      : 10
    const steps = Math.max(1, Math.ceil(length / ds))
    for (let i = 1; i <= steps; i++) {
      const t = i / steps
      const p = stepFunction(frame, fn, t, length)
      s += Math.hypot(p.x - frame.x, p.y - frame.y)
      samples.push({ s, x: p.x, y: p.y, heading: p.heading })
      frame = p
    }
    if (steps === 0) {
      const p = stepFunction(frame, fn, 1, length)
      frame = p
    }
  }
  return samples
}

function polyLength(points: Vec2[]): number {
  let total = 0
  for (let i = 1; i < points.length; i++) total += Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y)
  return total
}

function stepFunction(frame: { x: number; y: number; heading: number }, fn: XYFunction, t: number, length: number): { x: number; y: number; heading: number } {
  const sLocal = length * t
  switch (fn.kind) {
    case 'segment':
      return { x: frame.x + Math.cos(frame.heading) * sLocal, y: frame.y + Math.sin(frame.heading) * sLocal, heading: frame.heading }
    case 'arc': {
      const k = Math.sign(fn.angle) / fn.radius
      const h = frame.heading + fn.angle * t
      return {
        x: frame.x + (Math.sin(h) - Math.sin(frame.heading)) / k,
        y: frame.y + (-Math.cos(h) + Math.cos(frame.heading)) / k,
        heading: h,
      }
    }
    case 'clothoid': {
      const kIn = fn.radiusIn !== 0 ? 1 / fn.radiusIn : 0
      const kOut = fn.radiusOut !== 0 ? 1 / fn.radiusOut : 0
      const k1 = (kOut - kIn) / Math.max(1e-9, fn.length)
      const u = sLocal
      const heading = frame.heading + kIn * u + (k1 * u * u) / 2
      // small-step integration
      const steps = Math.max(2, Math.ceil(sLocal / 1))
      let x = frame.x
      let y = frame.y
      const hStep = sLocal / steps
      let acc = 0
      for (let i = 0; i < steps; i++) {
        const th1 = frame.heading + kIn * acc + (k1 * acc * acc) / 2
        const th2 = frame.heading + kIn * (acc + hStep) + (k1 * (acc + hStep) * (acc + hStep)) / 2
        x += (hStep / 2) * (Math.cos(th1) + Math.cos(th2))
        y += (hStep / 2) * (Math.sin(th1) + Math.sin(th2))
        acc += hStep
      }
      return { x, y, heading }
    }
    case 'polyline': {
      const pts = fn.points
      const target = sLocal
      let acc = 0
      for (let i = 1; i < pts.length; i++) {
        const seg = Math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y)
        if (acc + seg >= target) {
          const f = seg > 1e-9 ? (target - acc) / seg : 0
          return {
            x: pts[i - 1].x + (pts[i].x - pts[i - 1].x) * f,
            y: pts[i - 1].y + (pts[i].y - pts[i - 1].y) * f,
            heading: Math.atan2(pts[i].y - pts[i - 1].y, pts[i].x - pts[i - 1].x),
          }
        }
        acc += seg
      }
      const last = pts[pts.length - 1]
      return { x: last.x, y: last.y, heading: frame.heading }
    }
    default:
      return { ...frame }
  }
}

/** Import an OpenDRIVE XML document into app roads + intersections with explicit ways. */
export function importOpenDrive(xml: string): OdrImportResult | null {
  const doc = new DOMParser().parseFromString(xml, 'application/xml')
  if (doc.querySelector('parsererror')) return null
  const warnings: string[] = []
  const roads: RoadData[] = []
  const section = makeDefaultSection(1, 1, 3.5)
  const odrRoads = [...doc.querySelectorAll('road')]

  for (const roadEl of odrRoads) {
    const id = `odr-${roadEl.getAttribute('id') ?? roads.length}`
    const name = roadEl.getAttribute('name') || `ODR Road ${roadEl.getAttribute('id') ?? ''}`
    const geometries = [...roadEl.querySelectorAll('planView > geometry')].map(parseGeometry)
    if (geometries.length === 0) {
      warnings.push(`Road ${name}: no planView geometries, skipped`)
      continue
    }
    const functions: XYFunction[] = []
    let skipped = 0
    for (const g of geometries) {
      const fn = geometryToFunction(g)
      if (fn) functions.push(fn)
      else skipped++
    }
    if (functions.length === 0) {
      warnings.push(`Road ${name}: no supported geometries, skipped`)
      continue
    }
    if (skipped > 0) warnings.push(`Road ${name}: ${skipped} unsupported geometry element(s) skipped`)
    const first = geometries[0]
    const elevationProfile = parseElevations(roadEl)
    roads.push({
      id,
      name,
      points: [
        { x: first.x, y: first.y },
        { x: first.x + Math.cos(first.hdg), y: first.y + Math.sin(first.hdg) },
      ],
      functions,
      lanesLeft: 1,
      lanesRight: 1,
      laneWidth: 3.5,
      filletRadius: 0,
      laneSection: section,
      elevationProfile,
    })
  }

  // Junctions: node at the centroid of connected road ends; explicit ways
  // built from the connecting roads' imported axes (purple in the view).
  const roadById = new Map(roads.map((r) => [r.id, r]))
  const intersections: IntersectionData[] = []
  for (const junctionEl of doc.querySelectorAll('junction')) {
    const jid = junctionEl.getAttribute('id') ?? `${intersections.length}`
    const jname = junctionEl.getAttribute('name') || `ODR Junction ${jid}`
    const connections = [...junctionEl.querySelectorAll('connection')]
    if (connections.length === 0) continue
    const ends = new Map<string, { trackId: string; contact: 'start' | 'end' }>()
    const ways: IntersectionWay[] = []
    const positions: Vec2[] = []
    for (const conn of connections) {
      const incomingId = `odr-${conn.getAttribute('incomingRoad')}`
      const contact = conn.getAttribute('contactPoint') === 'end' ? 'end' : 'start'
      const connectingId = `odr-${conn.getAttribute('connectingRoad')}`
      const incoming = roadById.get(incomingId)
      const connecting = roadById.get(connectingId)
      if (!incoming) continue
      ends.set(`${incomingId}:${contact}`, { trackId: incomingId, contact })
      // node position: endpoints of the incoming roads at the junction
      const endPos = endpointOf(incoming, contact)
      if (endPos) positions.push(endPos)
      if (connecting) {
        const fromPos = endpointOf(connecting, 'start')
        const toPos = endpointOf(connecting, 'end')
        if (fromPos && toPos) {
          positions.push(fromPos)
          const samples: PathSample[] = samplePolyline(connecting.functions ?? [], startFrameOf(connecting), 2)
          const laneLinks = [...conn.querySelectorAll('laneLink')]
          ways.push({
            key: `${incomingId}:${contact}->${connectingId}:start`,
            from: { trackId: incomingId, contact },
            to: { trackId: connectingId, contact: 'start' },
            turn: 'straight',
            samples,
            laneCount: Math.max(1, laneLinks.length),
            laneWidth: 3.5,
            authorized: true,
            explicit: true,
          })
          void toPos
        }
      }
    }
    if (ends.size === 0) continue
    const node = makeIntersectionData(`odr-junction-${jid}-${intersections.length}`, centroid(positions))
    node.trackEnds = [...ends.values()]
    node.explicitWays = ways
    node.groundName = jname
    intersections.push(node)
  }

  return { roads, intersections, warnings }
}

function centroid(points: Vec2[]): Vec2 {
  if (points.length === 0) return { x: 0, y: 0 }
  const sum = points.reduce((acc, p) => ({ x: acc.x + p.x, y: acc.y + p.y }), { x: 0, y: 0 })
  return { x: sum.x / points.length, y: sum.y / points.length }
}

function startFrameOf(road: RoadData): { x: number; y: number; heading: number } {
  const pts = road.points
  if (pts.length >= 2) return { x: pts[0].x, y: pts[0].y, heading: Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) }
  return { x: 0, y: 0, heading: 0 }
}

function endpointOf(road: RoadData, contact: 'start' | 'end'): Vec2 | null {
  // approximate endpoints from the stored start frame + function chain length
  if (road.functions && road.functions.length > 0) {
    const samples = samplePolyline(road.functions, startFrameOf(road), 4)
    if (samples.length === 0) return null
    return contact === 'start' ? { x: samples[0].x, y: samples[0].y } : { x: samples[samples.length - 1].x, y: samples[samples.length - 1].y }
  }
  const pts = road.points
  if (!pts || pts.length < 1) return null
  return contact === 'start' ? pts[0] : pts[pts.length - 1]
}
