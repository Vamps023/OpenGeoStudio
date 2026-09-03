export interface ElevationPoint {
  s: number
  z: number
}

export function normalizeElevationProfile(
  profile: ElevationPoint[] | undefined,
  roadLength: number,
): ElevationPoint[] {
  if (roadLength <= 0) return []
  const source = profile?.length ? profile : [{ s: 0, z: 0 }, { s: roadLength, z: 0 }]
  const sorted = source
    .map((point) => ({ s: clamp(point.s, 0, roadLength), z: finite(point.z, 0) }))
    .sort((a, b) => a.s - b.s)
  const unique: ElevationPoint[] = []
  for (const point of sorted) {
    const previous = unique[unique.length - 1]
    if (previous && Math.abs(previous.s - point.s) < 1e-4) previous.z = point.z
    else unique.push(point)
  }
  if (unique[0].s > 1e-4) unique.unshift({ s: 0, z: unique[0].z })
  else unique[0].s = 0
  const last = unique[unique.length - 1]
  if (last.s < roadLength - 1e-4) unique.push({ s: roadLength, z: last.z })
  else last.s = roadLength
  if (unique.length === 1) unique.push({ s: roadLength, z: unique[0].z })
  return unique
}

export function evaluateElevation(profile: ElevationPoint[], station: number): number {
  if (profile.length === 0) return 0
  if (profile.length === 1) return profile[0].z
  const points = [...profile].sort((a, b) => a.s - b.s)
  const s = clamp(station, points[0].s, points[points.length - 1].s)
  const segment = findSegment(points, s)
  const a = points[segment]
  const b = points[segment + 1]
  const h = Math.max(1e-9, b.s - a.s)
  const t = (s - a.s) / h
  const tangents = monotoneTangents(points)
  const h00 = 2 * t * t * t - 3 * t * t + 1
  const h10 = t * t * t - 2 * t * t + t
  const h01 = -2 * t * t * t + 3 * t * t
  const h11 = t * t * t - t * t
  return h00 * a.z + h10 * h * tangents[segment] + h01 * b.z + h11 * h * tangents[segment + 1]
}

export function evaluateGrade(profile: ElevationPoint[], station: number): number {
  if (profile.length < 2) return 0
  const points = [...profile].sort((a, b) => a.s - b.s)
  const s = clamp(station, points[0].s, points[points.length - 1].s)
  const segment = findSegment(points, s)
  const a = points[segment]
  const b = points[segment + 1]
  const h = Math.max(1e-9, b.s - a.s)
  const t = (s - a.s) / h
  const tangents = monotoneTangents(points)
  const dh00 = (6 * t * t - 6 * t) / h
  const dh10 = 3 * t * t - 4 * t + 1
  const dh01 = (-6 * t * t + 6 * t) / h
  const dh11 = 3 * t * t - 2 * t
  return dh00 * a.z + dh10 * tangents[segment] + dh01 * b.z + dh11 * tangents[segment + 1]
}

export function maxAbsGrade(profile: ElevationPoint[], roadLength: number): number {
  if (profile.length < 2 || roadLength <= 0) return 0
  const steps = Math.max(20, Math.ceil(roadLength))
  let maximum = 0
  for (let index = 0; index <= steps; index++) {
    maximum = Math.max(maximum, Math.abs(evaluateGrade(profile, (roadLength * index) / steps)))
  }
  return maximum
}

function monotoneTangents(points: ElevationPoint[]): number[] {
  const count = points.length
  if (count === 2) {
    const slope = (points[1].z - points[0].z) / Math.max(1e-9, points[1].s - points[0].s)
    return [slope, slope]
  }
  const h: number[] = []
  const delta: number[] = []
  for (let index = 0; index < count - 1; index++) {
    h.push(Math.max(1e-9, points[index + 1].s - points[index].s))
    delta.push((points[index + 1].z - points[index].z) / h[index])
  }
  const tangents = new Array<number>(count).fill(0)
  tangents[0] = endpointTangent(h[0], h[1], delta[0], delta[1])
  tangents[count - 1] = endpointTangent(h[count - 2], h[count - 3], delta[count - 2], delta[count - 3])
  for (let index = 1; index < count - 1; index++) {
    if (delta[index - 1] === 0 || delta[index] === 0 || Math.sign(delta[index - 1]) !== Math.sign(delta[index])) {
      tangents[index] = 0
    } else {
      const w1 = 2 * h[index] + h[index - 1]
      const w2 = h[index] + 2 * h[index - 1]
      tangents[index] = (w1 + w2) / (w1 / delta[index - 1] + w2 / delta[index])
    }
  }
  return tangents
}

function endpointTangent(h0: number, h1: number, d0: number, d1: number): number {
  let tangent = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1)
  if (Math.sign(tangent) !== Math.sign(d0)) tangent = 0
  else if (Math.sign(d0) !== Math.sign(d1) && Math.abs(tangent) > Math.abs(3 * d0)) tangent = 3 * d0
  return tangent
}

function findSegment(points: ElevationPoint[], station: number): number {
  for (let index = 0; index < points.length - 1; index++) {
    if (station <= points[index + 1].s) return index
  }
  return points.length - 2
}

function finite(value: number, fallback: number): number {
  return Number.isFinite(value) ? value : fallback
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value))
}
