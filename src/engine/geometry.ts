import type { FittedPath, PathElement, Vec2 } from './types'

const EPS = 1e-9

export interface PathSample {
  s: number
  x: number
  y: number
  z?: number
  heading: number
}

function sub(a: Vec2, b: Vec2): Vec2 {
  return { x: a.x - b.x, y: a.y - b.y }
}

function length(a: Vec2): number {
  return Math.hypot(a.x, a.y)
}

function normalize(a: Vec2): Vec2 {
  const l = length(a)
  return l < EPS ? { x: 0, y: 0 } : { x: a.x / l, y: a.y / l }
}

export function dedupePoints(points: Vec2[], minDist = 0.01): Vec2[] {
  const out: Vec2[] = []
  for (const p of points) {
    const last = out[out.length - 1]
    if (!last || Math.hypot(p.x - last.x, p.y - last.y) > minDist) out.push({ ...p })
  }
  return out
}

export function evaluateElement(el: PathElement, sLocal: number): { x: number; y: number; heading: number } {
  if (el.type === 'line' || (el.type !== 'arc' && el.type !== 'clothoid' && el.type !== 'bezier' && el.type !== 'polyline' && el.type !== 'spline') || (el.type === 'arc' && Math.abs(el.curvature) < EPS)) {
    return {
      x: el.x + Math.cos(el.heading) * sLocal,
      y: el.y + Math.sin(el.heading) * sLocal,
      heading: el.heading,
    }
  }
  if (el.type === 'arc') {
    const k = el.curvature
    const h = el.heading + k * sLocal
    return {
      x: el.x + (Math.sin(h) - Math.sin(el.heading)) / k,
      y: el.y + (-Math.cos(h) + Math.cos(el.heading)) / k,
      heading: h,
    }
  }
  if (el.type === 'clothoid') {
    const L = Math.max(1e-9, el.length)
    const kIn = el.curvature
    const kOut = el.curvatureOut ?? kIn
    const k1 = (kOut - kIn) / L
    const steps = Math.max(4, Math.ceil(sLocal / 0.5))
    let x = el.x
    let y = el.y
    const h = sLocal / steps
    let u = 0
    for (let i = 0; i < steps; i++) {
      const th1 = el.heading + kIn * u + (k1 * u * u) / 2
      const th2 = el.heading + kIn * (u + h) + (k1 * (u + h) * (u + h)) / 2
      x += (h / 2) * (Math.cos(th1) + Math.cos(th2))
      y += (h / 2) * (Math.sin(th1) + Math.sin(th2))
      u += h
    }
    return { x, y, heading: el.heading + kIn * sLocal + (k1 * sLocal * sLocal) / 2 }
  }
  if (el.type === 'bezier') {
    const p1 = el.p1 ?? { x: el.x, y: el.y }
    const p2 = el.p2 ?? p1
    const p0 = { x: el.x, y: el.y }
    const t = Math.max(0, Math.min(1, sLocal / Math.max(1e-9, el.length)))
    // Arc-length parameterization is approximated by the stored uniform length.
    const u = 1 - t
    const p3 = polylineEnd(el)
    const x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x
    const y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y
    const dx = 3 * u * u * (p1.x - p0.x) + 6 * u * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x)
    const dy = 3 * u * u * (p1.y - p0.y) + 6 * u * t * (p2.y - p1.y) + 3 * t * t * (p3.y - p2.y)
    return { x, y, heading: Math.atan2(dy, dx) }
  }
  if (el.type === 'polyline') {
    const pts = el.points ?? [{ x: el.x, y: el.y }]
    let acc = 0
    for (let i = 1; i < pts.length; i++) {
      const a = pts[i - 1]
      const b = pts[i]
      const seg = Math.hypot(b.x - a.x, b.y - a.y)
      if (acc + seg >= sLocal || i === pts.length - 1) {
        const t = seg > EPS ? (sLocal - acc) / seg : 0
        return {
          x: a.x + (b.x - a.x) * t,
          y: a.y + (b.y - a.y) * t,
          heading: Math.atan2(b.y - a.y, b.x - a.x),
        }
      }
      acc += seg
    }
    const last = pts[pts.length - 1]
    return { x: last.x, y: last.y, heading: el.heading }
  }
  // spline: Catmull-Rom style clamped curve through points
  {
    const pts = el.points ?? [{ x: el.x, y: el.y }]
    if (pts.length < 2) return { x: el.x, y: el.y, heading: el.heading }
    const tangents: number[] = []
    for (let i = 0; i < pts.length; i++) {
      if (i === 0) tangents.push(Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x))
      else if (i === pts.length - 1) tangents.push(Math.atan2(pts[i].y - pts[i - 1].y, pts[i].x - pts[i - 1].x))
      else {
        const a = Math.atan2(pts[i].y - pts[i - 1].y, pts[i].x - pts[i - 1].x)
        const b = Math.atan2(pts[i + 1].y - pts[i].y, pts[i + 1].x - pts[i].x)
        let d = b - a
        while (d > Math.PI) d -= Math.PI * 2
        while (d < -Math.PI) d += Math.PI * 2
        tangents.push(a + d / 2)
      }
    }
    // locate segment containing sLocal by chord lengths
    let acc = 0
    for (let i = 0; i < pts.length - 1; i++) {
      const chord = Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y)
      if (acc + chord >= sLocal || i === pts.length - 2) {
        const t = chord > EPS ? (sLocal - acc) / chord : 0
        const u = 1 - t
        const h00 = 2 * t * t * t - 3 * t * t + 1
        const h10 = t * t * t - 2 * t * t + t
        const h01 = -2 * t * t * t + 3 * t * t
        const h11 = t * t * t - t * t
        const x = h00 * pts[i].x + h10 * chord * Math.cos(tangents[i]) + h01 * pts[i + 1].x + h11 * chord * Math.cos(tangents[i + 1])
        const y = h00 * pts[i].y + h10 * chord * Math.sin(tangents[i]) + h01 * pts[i + 1].y + h11 * chord * Math.sin(tangents[i + 1])
        const e = 1e-3
        const herm = (tt: number) => {
          const uu = 1 - tt
          const a0 = 2 * tt * tt * tt - 3 * tt * tt + 1
          const a1 = tt * tt * tt - 2 * tt * tt + tt
          const a2 = -2 * tt * tt * tt + 3 * tt * tt
          const a3 = tt * tt * tt - tt * tt
          return {
            x: a0 * pts[i].x + a1 * chord * Math.cos(tangents[i]) + a2 * pts[i + 1].x + a3 * chord * Math.cos(tangents[i + 1]),
            y: a0 * pts[i].y + a1 * chord * Math.sin(tangents[i]) + a2 * pts[i + 1].y + a3 * chord * Math.sin(tangents[i + 1]),
          }
        }
        const pa = herm(Math.max(0, t - e))
        const pb = herm(Math.min(1, t + e))
        return { x, y, heading: Math.atan2(pb.y - pa.y, pb.x - pa.x) }
      }
      acc += chord
    }
    const lastP = pts[pts.length - 1]
    return { x: lastP.x, y: lastP.y, heading: el.heading }
  }
}

function polylineEnd(el: PathElement): Vec2 {
  if (el.points && el.points.length > 0) return el.points[el.points.length - 1]
  return { x: el.x + Math.cos(el.heading) * el.length, y: el.y + Math.sin(el.heading) * el.length }
}

export function fitPath(points: Vec2[], radius: number): FittedPath | null {
  const pts = dedupePoints(points)
  if (pts.length < 2) return null

  const tangents: number[] = []
  const deflections: number[] = []
  for (let i = 1; i < pts.length - 1; i++) {
    const inDir = normalize(sub(pts[i], pts[i - 1]))
    const outDir = normalize(sub(pts[i + 1], pts[i]))
    const cross = inDir.x * outDir.y - inDir.y * outDir.x
    const dot = clamp(inDir.x * outDir.x + inDir.y * outDir.y, -1, 1)
    const deflection = Math.atan2(cross, dot)
    const inLen = length(sub(pts[i], pts[i - 1]))
    const outLen = length(sub(pts[i + 1], pts[i]))
    let tangent = 0
    if (Math.abs(deflection) > 1e-6) {
      const maxTangent = Math.min(inLen, outLen) * 0.5
      tangent = Math.min(radius * Math.tan(Math.abs(deflection) / 2), maxTangent)
    }
    tangents.push(tangent)
    deflections.push(deflection)
  }

  const elements: PathElement[] = []
  let cursor = { ...pts[0] }
  let total = 0

  const pushLine = (to: Vec2, heading: number) => {
    const l = Math.hypot(to.x - cursor.x, to.y - cursor.y)
    if (l > EPS) {
      elements.push({ type: 'line', x: cursor.x, y: cursor.y, heading, length: l, curvature: 0 })
      cursor = { ...to }
      total += l
    }
  }

  const pushArc = (heading: number, curvature: number, arcLength: number) => {
    elements.push({ type: 'arc', x: cursor.x, y: cursor.y, heading, length: arcLength, curvature })
    const k = curvature
    const endHeading = heading + k * arcLength
    cursor = {
      x: cursor.x + (Math.sin(endHeading) - Math.sin(heading)) / k,
      y: cursor.y + (-Math.cos(endHeading) + Math.cos(heading)) / k,
    }
    total += arcLength
  }

  for (let i = 1; i < pts.length - 1; i++) {
    const inDir = normalize(sub(pts[i], pts[i - 1]))
    const outDir = normalize(sub(pts[i + 1], pts[i]))
    const heading = Math.atan2(inDir.y, inDir.x)
    const t = tangents[i - 1]
    pushLine({ x: pts[i].x - inDir.x * t, y: pts[i].y - inDir.y * t }, heading)
    if (t > EPS) {
      const rEff = t / Math.tan(Math.abs(deflections[i - 1]) / 2)
      pushArc(heading, Math.sign(deflections[i - 1]) / rEff, rEff * Math.abs(deflections[i - 1]))
    }
  }

  const lastDir = normalize(sub(pts[pts.length - 1], pts[pts.length - 2]))
  pushLine({ ...pts[pts.length - 1] }, Math.atan2(lastDir.y, lastDir.x))

  if (elements.length === 0) return null
  return { elements, length: total }
}

export function evaluatePath(path: FittedPath, s: number): PathSample {
  const clamped = Math.max(0, Math.min(path.length, s))
  let cursor = 0
  for (const element of path.elements) {
    if (clamped <= cursor + element.length) {
      const value = evaluateElement(element, clamped - cursor)
      return { s: clamped, ...value }
    }
    cursor += element.length
  }
  const last = path.elements[path.elements.length - 1]
  const value = evaluateElement(last, last.length)
  return { s: path.length, ...value }
}

export function samplePathRange(path: FittedPath, sStart: number, sEnd: number, ds: number): PathSample[] {
  const begin = Math.max(0, Math.min(path.length, sStart))
  const end = Math.max(begin, Math.min(path.length, sEnd))
  if (end - begin < EPS) return []
  const steps = Math.max(1, Math.ceil((end - begin) / ds))
  return Array.from({ length: steps + 1 }, (_, i) => evaluatePath(path, begin + ((end - begin) * i) / steps))
}

export function samplePath(path: FittedPath, ds: number): PathSample[] {
  const samples: PathSample[] = []
  let s = 0
  for (const el of path.elements) {
    const steps = Math.max(1, Math.ceil(el.length / ds))
    for (let i = 0; i < steps; i++) {
      const sLocal = (el.length * i) / steps
      const e = evaluateElement(el, sLocal)
      samples.push({ s: s + sLocal, x: e.x, y: e.y, heading: e.heading })
    }
    s += el.length
  }
  const last = path.elements[path.elements.length - 1]
  const end = evaluateElement(last, last.length)
  samples.push({ s: path.length, x: end.x, y: end.y, heading: end.heading })
  return samples
}

function clamp(v: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, v))
}
