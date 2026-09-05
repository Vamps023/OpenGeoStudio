// ─────────────────────────────────────────────────────────────────────
// OSM Building Builder (pure engine): parse Overpass API building
// footprints, resolve Simple-3D-Buildings attributes (height, levels,
// roof shape), project them into the project's world frame, and extrude
// selectable 3D volumes. No DOM / THREE dependencies — node-testable.
// Building data © OpenStreetMap contributors (ODbL 1.0).
// ─────────────────────────────────────────────────────────────────────
import type { MeshData } from './mesh'

export interface GeoRef {
  lng: number
  lat: number
  scale: number
}

/** building footprint as returned by Overpass (lng/lat ring) */
export interface OsmBuildingRaw {
  id: string
  tags: Record<string, string>
  ring: { lng: number; lat: number }[]
}

/** projected building stored on the project (world meters, project frame) */
export interface OsmBuildingData {
  id: string
  name: string
  buildingType: string
  height: number
  levels: number | null
  roofShape: string
  ring: { x: number; y: number }[]
  tags: Record<string, string>
}

const FLOOR_HEIGHT = 3.2
const DEFAULT_HEIGHT = 6

/** Parse an Overpass JSON response into raw building footprints.
 *  Handles both `out geom` (inline geometry) and plain `out body` (node map).
 *  Skips `building=no` and non-closed ways. */
export function parseOverpassBuildings(json: unknown): OsmBuildingRaw[] {
  const root = json as { elements?: unknown[] } | null
  const elements = Array.isArray(root?.elements) ? root.elements : []
  const nodes = new Map<number, { lng: number; lat: number }>()
  for (const element of elements) {
    const e = element as { type?: string; id?: number; lat?: number; lon?: number }
    if (e.type === 'node' && typeof e.id === 'number') {
      nodes.set(e.id, { lng: e.lon ?? 0, lat: e.lat ?? 0 })
    }
  }
  const out: OsmBuildingRaw[] = []
  for (const element of elements) {
    const e = element as {
      type?: string
      id?: number
      tags?: Record<string, string>
      geometry?: { lat?: number; lon?: number }[]
      nodes?: number[]
    }
    if (e.type !== 'way' || typeof e.id !== 'number' || !e.tags) continue
    if (!e.tags.building || e.tags.building === 'no') continue
    let ring: { lng: number; lat: number }[] = []
    if (Array.isArray(e.geometry) && e.geometry.length >= 4) {
      ring = e.geometry.map((point) => ({ lng: point.lon ?? 0, lat: point.lat ?? 0 }))
    } else if (Array.isArray(e.nodes) && e.nodes.length >= 4) {
      ring = e.nodes.map((ref) => nodes.get(ref)).map((p) => p ?? { lng: 0, lat: 0 })
    }
    if (ring.length < 4) continue
    // drop the duplicated closing vertex
    const first = ring[0]
    const last = ring[ring.length - 1]
    if (Math.abs(first.lng - last.lng) < 1e-9 && Math.abs(first.lat - last.lat) < 1e-9) ring = ring.slice(0, -1)
    if (ring.length < 3) continue
    out.push({ id: `way:${e.id}`, tags: e.tags, ring })
  }
  return out
}

/** lng/lat → world meters in the project frame (same convention as the viewport). */
export function projectRing(ring: { lng: number; lat: number }[], geoRef: GeoRef): { x: number; y: number }[] {
  const latRad = (geoRef.lat * Math.PI) / 180
  const metersPerDegLat = 111320
  const metersPerDegLng = 111320 * Math.cos(latRad)
  return ring.map((point) => ({
    x: ((point.lng - geoRef.lng) * metersPerDegLng) / geoRef.scale,
    y: ((point.lat - geoRef.lat) * metersPerDegLat) / geoRef.scale,
  }))
}

/** Resolve building height from OSM tags: height tag → levels × floor → fallback. */
export function resolveHeight(tags: Record<string, string>, fallback = DEFAULT_HEIGHT): { height: number; levels: number | null } {
  const rawHeight = tags.height ?? tags['building:height']
  if (rawHeight) {
    const meters = Number.parseFloat(rawHeight.replace(',', '.'))
    if (Number.isFinite(meters) && meters > 0 && meters < 1000) return { height: meters, levels: null }
  }
  const rawLevels = tags['building:levels']
  if (rawLevels) {
    const levels = Number.parseFloat(rawLevels.replace(',', '.'))
    if (Number.isFinite(levels) && levels > 0) return { height: levels * FLOOR_HEIGHT, levels }
  }
  return { height: fallback, levels: null }
}

export function toBuildingData(raw: OsmBuildingRaw, geoRef: GeoRef, fallbackHeight = DEFAULT_HEIGHT): OsmBuildingData {
  const { height, levels } = resolveHeight(raw.tags, fallbackHeight)
  return {
    id: raw.id,
    name: raw.tags.name || raw.tags['building:use'] || raw.tags.building || 'Building',
    buildingType: raw.tags.building || 'yes',
    height,
    levels,
    roofShape: raw.tags['roof:shape'] || 'flat',
    ring: projectRing(raw.ring, geoRef),
    tags: raw.tags,
  }
}

// ─── Triangulation (ear clipping, concave-safe, no holes) ──────────

type Point2 = { x: number; y: number }

function signedArea(ring: Point2[]): number {
  let area = 0
  for (let i = 0; i < ring.length; i++) {
    const a = ring[i]
    const b = ring[(i + 1) % ring.length]
    area += a.x * b.y - b.x * a.y
  }
  return area / 2
}

function cross(o: Point2, a: Point2, b: Point2): number {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x)
}

function pointInTriangle(p: Point2, a: Point2, b: Point2, c: Point2): boolean {
  const d1 = cross(a, b, p)
  const d2 = cross(b, c, p)
  const d3 = cross(c, a, p)
  const hasNeg = d1 < 0 || d2 < 0 || d3 < 0
  const hasPos = d1 > 0 || d2 > 0 || d3 > 0
  return !(hasNeg && hasPos)
}

/** Triangulate a simple polygon (CCW or CW) into vertex-index triangles.
 *  Returns [] for degenerate rings. */
export function triangulatePolygon(ring: Point2[]): [number, number, number][] {
  const n = ring.length
  if (n < 3) return []
  // work in CCW order; map back to original indices at the end
  const reversed = signedArea(ring) < 0
  const pts = reversed ? [...ring].reverse() : ring
  const original = (i: number) => (reversed ? n - 1 - i : i)

  const indices = pts.map((_, i) => i)
  const triangles: [number, number, number][] = []
  let guard = 0
  while (indices.length > 3 && guard++ < n * n) {
    let clipped = false
    for (let i = 0; i < indices.length; i++) {
      const i0 = indices[(i + indices.length - 1) % indices.length]
      const i1 = indices[i]
      const i2 = indices[(i + 1) % indices.length]
      const a = pts[i0]
      const b = pts[i1]
      const c = pts[i2]
      if (cross(a, b, c) <= 1e-12) continue // reflex or collinear
      let isEar = true
      for (const j of indices) {
        if (j === i0 || j === i1 || j === i2) continue
        if (pointInTriangle(pts[j], a, b, c)) {
          isEar = false
          break
        }
      }
      if (isEar) {
        triangles.push([original(i0), original(i1), original(i2)])
        indices.splice(i, 1)
        clipped = true
        break
      }
    }
    if (!clipped) break // degenerate ring — keep what we have
  }
  if (indices.length === 3) triangles.push([original(indices[0]), original(indices[1]), original(indices[2])])
  return triangles
}

// ─── Mesh extrusion ────────────────────────────────────────────────

const FACADE: [number, number, number] = [0.72, 0.7, 0.66]
const FACADE_BAND: [number, number, number] = [0.6, 0.58, 0.55]
const ROOF: [number, number, number] = [0.42, 0.4, 0.38]
const ROOF_GABLE: [number, number, number] = [0.5, 0.3, 0.26]

/** Push one quad (two triangles, both sides shaded via DoubleSide material). */
class MeshBuilder {
  positions: number[] = []
  colors: number[] = []

  quad(p0: [number, number, number], p1: [number, number, number], p2: [number, number, number], p3: [number, number, number], color: [number, number, number]) {
    this.tri(p0, p1, p2, color)
    this.tri(p0, p2, p3, color)
  }

  tri(p0: [number, number, number], p1: [number, number, number], p2: [number, number, number], color: [number, number, number]) {
    for (const p of [p0, p1, p2]) {
      this.positions.push(p[0], p[1], p[2])
      this.colors.push(color[0], color[1], color[2])
    }
  }

  build(): MeshData {
    return {
      positions: new Float32Array(this.positions),
      colors: new Float32Array(this.colors),
      indices: new Uint32Array(this.positions.length / 3),
    }
  }
}

/** world-space vertex from a ground point (x, y) at elevation z (three.js: y up, -z north) */
function vertex(p: Point2, z: number): [number, number, number] {
  return [p.x, z, -p.y]
}

/** Extrude a building footprint into walls + roof. `baseZ` is the terrain
 *  elevation at the building; roofShape 'gabled' adds a simple ridge roof. */
export function buildBuildingMesh(
  building: OsmBuildingData,
  baseZ: number,
): MeshData | null {
  const ring = building.ring
  if (ring.length < 3) return null
  const triangles = triangulatePolygon(ring)
  if (triangles.length === 0) return null

  const builder = new MeshBuilder()
  const top = baseZ + building.height

  // walls: one quad per edge, floor bands every 3.2 m via slight color banding
  for (let i = 0; i < ring.length; i++) {
    const a = ring[i]
    const b = ring[(i + 1) % ring.length]
    const edgeLength = Math.hypot(b.x - a.x, b.y - a.y)
    if (edgeLength < 0.05) continue
    const floors = Math.max(1, Math.round(building.height / FLOOR_HEIGHT))
    let z0 = baseZ
    for (let f = 0; f < floors; f++) {
      const z1 = baseZ + (building.height * (f + 1)) / floors
      const color = f % 2 ? FACADE_BAND : FACADE
      builder.quad(vertex(a, z0), vertex(b, z0), vertex(b, z1), vertex(a, z1), color)
      z0 = z1
    }
  }

  // flat roof
  for (const [i0, i1, i2] of triangles) {
    builder.tri(vertex(ring[i0], top), vertex(ring[i2], top), vertex(ring[i1], top), ROOF)
  }

  // simple gable roof: ridge along the bounding box's longer axis
  if (building.roofShape === 'gabled' || building.roofShape === 'hipped') {
    let minX = Infinity
    let maxX = -Infinity
    let minY = Infinity
    let maxY = -Infinity
    for (const p of ring) {
      minX = Math.min(minX, p.x)
      maxX = Math.max(maxX, p.x)
      minY = Math.min(minY, p.y)
      maxY = Math.max(maxY, p.y)
    }
    const spanX = maxX - minX
    const spanY = maxY - minY
    if (spanX > 1 && spanY > 1) {
      const ridgeHeight = Math.min(4, building.height * 0.3)
      const alongX = spanX >= spanY
      const r0: Point2 = alongX ? { x: minX, y: (minY + maxY) / 2 } : { x: (minX + maxX) / 2, y: minY }
      const r1: Point2 = alongX ? { x: maxX, y: (minY + maxY) / 2 } : { x: (minX + maxX) / 2, y: maxY }
      const e0: Point2 = alongX ? { x: minX, y: minY } : { x: minX, y: maxY }
      const e1: Point2 = alongX ? { x: maxX, y: minY } : { x: maxX, y: maxY }
      const r0v = vertex(r0, top + ridgeHeight)
      const r1v = vertex(r1, top + ridgeHeight)
      const e0v = vertex(e0, top)
      const e1v = vertex(e1, top)
      // two slopes over the bbox (visual approximation of a gable roof)
      const off = alongX ? { x: 0, y: spanY } : { x: spanX, y: 0 }
      const f0v = vertex({ x: e0.x + off.x, y: e0.y + off.y }, top)
      const f1v = vertex({ x: e1.x + off.x, y: e1.y + off.y }, top)
      builder.quad(e0v, e1v, r1v, r0v, ROOF_GABLE)
      builder.quad(f1v, f0v, r0v, r1v, ROOF_GABLE)
      builder.tri(e0v, r0v, f0v, ROOF_GABLE)
      builder.tri(e1v, f1v, r1v, ROOF_GABLE)
    }
  }

  const mesh = builder.build()
  return mesh.positions.length ? mesh : null
}

/** World-frame centroid of a ring (for terrain elevation sampling). */
export function ringCentroid(ring: Point2[]): Point2 {
  let x = 0
  let y = 0
  for (const p of ring) {
    x += p.x
    y += p.y
  }
  return { x: x / ring.length, y: y / ring.length }
}

/** Build the Overpass QL query for a bounding box. */
export function overpassQuery(bounds: { west: number; south: number; east: number; north: number }): string {
  return `[out:json][timeout:30];way["building"](${bounds.south},${bounds.west},${bounds.north},${bounds.east});out tags geom;`
}

/** A lat/lng polygon ring (no closing duplicate). */
export interface LatLngRing {
  lat: number
  lng: number
}

/** Bounding box of a lat/lng polygon ring. */
export function polygonBounds(ring: LatLngRing[]): { west: number; south: number; east: number; north: number } {
  if (ring.length === 0) return { west: 0, south: 0, east: 0, north: 0 }
  let west = Infinity
  let south = Infinity
  let east = -Infinity
  let north = -Infinity
  for (const p of ring) {
    if (p.lng < west) west = p.lng
    if (p.lng > east) east = p.lng
    if (p.lat < south) south = p.lat
    if (p.lat > north) north = p.lat
  }
  return { west, south, east, north }
}

/** Build the Overpass QL query for a polygon area of interest.
 *  Uses the `poly:"lat lng lat lng …"` filter so only buildings intersecting
 *  the drawn polygon are returned. The polygon must have ≥ 3 vertices. */
export function overpassQueryPolygon(ring: LatLngRing[]): string {
  if (ring.length < 3) throw new Error('Polygon needs at least 3 vertices')
  const poly = ring.map((p) => `${p.lat} ${p.lng}`).join(' ')
  return `[out:json][timeout:45];way["building"](poly:"${poly}");out tags geom;`
}

/** Even-odd point-in-polygon test (ray casting) for lat/lng rings.
 *  Used to filter Overpass results to buildings whose centroid is inside the
 *  drawn polygon (the `poly` filter returns intersecting ways, which can
 *  include buildings that straddle the boundary). */
export function pointInPolygon(lng: number, lat: number, ring: LatLngRing[]): boolean {
  let inside = false
  for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {
    const xi = ring[i].lng
    const yi = ring[i].lat
    const xj = ring[j].lng
    const yj = ring[j].lat
    const intersect = yi > lat !== yj > lat && lng < ((xj - xi) * (lat - yi)) / (yj - yi) + xi
    if (intersect) inside = !inside
  }
  return inside
}

/** Centroid of a lat/lng ring (simple average — fine for the polygon filter). */
export function ringCentroidLatLng(ring: LatLngRing[]): LatLngRing {
  let lat = 0
  let lng = 0
  for (const p of ring) {
    lat += p.lat
    lng += p.lng
  }
  return { lat: lat / ring.length, lng: lng / ring.length }
}
