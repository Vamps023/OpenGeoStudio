import { evaluatePath, fitPath, samplePath } from './geometry'
import { fitTrackPath } from './tracks'
import type { FittedPath, Vec2 } from './types'
import type { XYFunction } from './xyFunctions'

export interface RoadGeometryInput {
  points: Vec2[]
  geometryType?: 'straight' | 'polyline' | 'arc'
  filletRadius: number
  // SCANeR XY function chain: takes precedence over points when present
  functions?: XYFunction[]
}

export interface NearestPathPoint {
  point: Vec2
  s: number
  distance: number
}

export function fitRoadGeometry(road: RoadGeometryInput): FittedPath | null {
  if (road.functions && road.functions.length > 0) {
    return fitTrackPath({
      id: 'road',
      points: road.points,
      functions: road.functions,
      lanesLeft: 0,
      lanesRight: 0,
      laneWidth: 0,
    })
  }
  if (road.geometryType === 'arc' && road.points.length >= 3) {
    return fitArcThroughPoints(road.points[0], road.points[1], road.points[2])
  }
  return fitPath(road.points, road.geometryType === 'straight' ? 0 : road.filletRadius)
}

export function fitArcThroughPoints(start: Vec2, through: Vec2, end: Vec2): FittedPath | null {
  const determinant = 2 * (
    start.x * (through.y - end.y) +
    through.x * (end.y - start.y) +
    end.x * (start.y - through.y)
  )
  if (Math.abs(determinant) < 1e-7) return fitPath([start, end], 0)

  const startSq = start.x * start.x + start.y * start.y
  const throughSq = through.x * through.x + through.y * through.y
  const endSq = end.x * end.x + end.y * end.y
  const center = {
    x: (startSq * (through.y - end.y) + throughSq * (end.y - start.y) + endSq * (start.y - through.y)) / determinant,
    y: (startSq * (end.x - through.x) + throughSq * (start.x - end.x) + endSq * (through.x - start.x)) / determinant,
  }
  const radius = Math.hypot(start.x - center.x, start.y - center.y)
  if (!Number.isFinite(radius) || radius < 0.01) return null

  const startAngle = Math.atan2(start.y - center.y, start.x - center.x)
  const throughAngle = Math.atan2(through.y - center.y, through.x - center.x)
  const endAngle = Math.atan2(end.y - center.y, end.x - center.x)
  const ccwSweep = normalizePositive(endAngle - startAngle)
  const throughSweep = normalizePositive(throughAngle - startAngle)
  const direction = throughSweep <= ccwSweep ? 1 : -1
  const sweep = direction > 0 ? ccwSweep : normalizePositive(startAngle - endAngle)
  const heading = startAngle + direction * Math.PI / 2

  return {
    length: radius * sweep,
    elements: [{
      type: 'arc',
      x: start.x,
      y: start.y,
      heading,
      length: radius * sweep,
      curvature: direction / radius,
    }],
  }
}

export function nearestPointOnPath(path: FittedPath, target: Vec2, spacing = 0.75): NearestPathPoint {
  const samples = samplePath(path, spacing)
  let best = { point: { x: samples[0].x, y: samples[0].y }, s: 0, distance: Number.POSITIVE_INFINITY }
  for (let i = 0; i < samples.length - 1; i++) {
    const a = samples[i]
    const b = samples[i + 1]
    const dx = b.x - a.x
    const dy = b.y - a.y
    const lengthSq = dx * dx + dy * dy
    const t = lengthSq > 0
      ? Math.max(0, Math.min(1, ((target.x - a.x) * dx + (target.y - a.y) * dy) / lengthSq))
      : 0
    const point = { x: a.x + dx * t, y: a.y + dy * t }
    const distance = Math.hypot(target.x - point.x, target.y - point.y)
    if (distance < best.distance) {
      best = { point, s: a.s + (b.s - a.s) * t, distance }
    }
  }
  return best
}

export function sampledControlPoints(path: FittedPath, sStart: number, sEnd: number, spacing = 8): Vec2[] {
  const length = Math.max(0, sEnd - sStart)
  const steps = Math.max(1, Math.ceil(length / spacing))
  return Array.from({ length: steps + 1 }, (_, index) => {
    const sample = evaluatePath(path, sStart + (length * index) / steps)
    return { x: sample.x, y: sample.y }
  })
}

function normalizePositive(angle: number): number {
  const full = Math.PI * 2
  return ((angle % full) + full) % full
}
