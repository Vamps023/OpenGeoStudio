// ─────────────────────────────────────────────────────────────────────
// Simulation runtime (SCANeR Simulation parity, core): vehicles driving
// on the road network. Pure math — paths are sampled once per network,
// vehicles advance by speed·dt along arc length, wrap at the end, and
// report world positions/headings for rendering.
// ─────────────────────────────────────────────────────────────────────
import type { Vec2 } from './types'

export interface SimPathSample {
  s: number
  x: number
  y: number
  heading: number
}

export interface SimPath {
  roadId: string
  name: string
  length: number
  samples: SimPathSample[]
}

export interface SimVehicle {
  id: number
  roadId: string
  /** station along the road [m] */
  s: number
  /** signed lateral offset from the axis [m] (positive = left) */
  laneOffset: number
  /** speed [m/s] */
  speed: number
}

export interface SimPose {
  id: number
  roadId: string
  x: number
  y: number
  heading: number
  speed: number
}

/** Build simulation paths from raw polylines (already sampled per road). */
export function buildSimPaths(
  roads: { id: string; name: string; polyline: Vec2[]; length: number }[],
): SimPath[] {
  const out: SimPath[] = []
  for (const road of roads) {
    if (road.polyline.length < 2 || road.length <= 0) continue
    const samples: SimPathSample[] = []
    // resample the polyline at ~2 m with cumulative s and headings
    const pts = road.polyline
    const dense: Vec2[] = []
    for (let i = 1; i < pts.length; i++) {
      const a = pts[i - 1]
      const b = pts[i]
      const seg = Math.hypot(b.x - a.x, b.y - a.y)
      const steps = Math.max(1, Math.ceil(seg / 2))
      for (let k = 0; k < steps; k++) {
        const t = k / steps
        dense.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t })
      }
    }
    dense.push(pts[pts.length - 1])
    let s = 0
    for (let i = 0; i < dense.length; i++) {
      if (i > 0) {
        s += Math.hypot(dense[i].x - dense[i - 1].x, dense[i].y - dense[i - 1].y)
      }
      const a = dense[Math.max(0, i - 1)]
      const b = dense[Math.min(dense.length - 1, i + 1)]
      samples.push({
        s,
        x: dense[i].x,
        y: dense[i].y,
        heading: Math.atan2(b.y - a.y, b.x - a.x),
      })
    }
    out.push({ roadId: road.id, name: road.name, length: s, samples })
  }
  return out
}

/** Position + heading at station s on a path (linear interpolation). */
export function simPoseAt(path: SimPath, s: number): { x: number; y: number; heading: number } {
  const clamped = Math.max(0, Math.min(path.length, s))
  const samples = path.samples
  // binary search for the segment
  let lo = 0
  let hi = samples.length - 1
  while (lo < hi) {
    const mid = (lo + hi) >> 1
    if (samples[mid].s < clamped) lo = mid + 1
    else hi = mid
  }
  const i = Math.max(1, lo)
  const a = samples[i - 1]
  const b = samples[i]
  const span = b.s - a.s
  const t = span > 1e-9 ? (clamped - a.s) / span : 0
  return {
    x: a.x + (b.x - a.x) * t,
    y: a.y + (b.y - a.y) * t,
    heading: a.heading + shortAngle(b.heading - a.heading) * t,
  }
}

function shortAngle(d: number): number {
  while (d > Math.PI) d -= Math.PI * 2
  while (d < -Math.PI) d += Math.PI * 2
  return d
}

/** Spawn `count` vehicles spread over the longest paths, right-hand traffic. */
export function spawnVehicles(paths: SimPath[], count: number, seed = 1): SimVehicle[] {
  const usable = paths.filter((p) => p.length >= 30)
  if (usable.length === 0 || count <= 0) return []
  const rng = mulberry32(seed)
  const vehicles: SimVehicle[] = []
  for (let i = 0; i < count; i++) {
    const path = usable[i % usable.length]
    const laneOffset = (i % 2 === 0 ? -1 : 1) * 1.75 // right-hand traffic
    vehicles.push({
      id: i + 1,
      roadId: path.roadId,
      s: rng() * path.length,
      laneOffset,
      speed: 8 + rng() * 10, // 29..65 km/h
    })
  }
  return vehicles
}

/** Advance every vehicle by dt seconds, wrapping at the end of its road. */
export function stepSimulation(
  vehicles: SimVehicle[],
  paths: SimPath[],
  dt: number,
): void {
  const byId = new Map(paths.map((p) => [p.roadId, p]))
  for (const vehicle of vehicles) {
    const path = byId.get(vehicle.roadId)
    if (!path) continue
    vehicle.s += vehicle.speed * Math.max(0, dt)
    if (vehicle.s > path.length) vehicle.s = vehicle.s % path.length
  }
}

/** World poses for rendering (interpolated + lateral lane offset). */
export function simulationPoses(vehicles: SimVehicle[], paths: SimPath[]): SimPose[] {
  const byId = new Map(paths.map((p) => [p.roadId, p]))
  const out: SimPose[] = []
  for (const vehicle of vehicles) {
    const path = byId.get(vehicle.roadId)
    if (!path) continue
    const p = simPoseAt(path, vehicle.s)
    const nx = -Math.sin(p.heading)
    const ny = Math.cos(p.heading)
    out.push({
      id: vehicle.id,
      roadId: vehicle.roadId,
      x: p.x + nx * vehicle.laneOffset,
      y: p.y + ny * vehicle.laneOffset,
      heading: p.heading,
      speed: vehicle.speed,
    })
  }
  return out
}

function mulberry32(seed: number): () => number {
  let a = seed >>> 0
  return () => {
    a |= 0
    a = (a + 0x6d2b79f5) | 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}
