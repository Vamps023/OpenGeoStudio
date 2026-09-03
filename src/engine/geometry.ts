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
  if (el.type === 'line' || Math.abs(el.curvature) < EPS) {
    return {
      x: el.x + Math.cos(el.heading) * sLocal,
      y: el.y + Math.sin(el.heading) * sLocal,
      heading: el.heading,
    }
  }
  const k = el.curvature
  const h = el.heading + k * sLocal
  return {
    x: el.x + (Math.sin(h) - Math.sin(el.heading)) / k,
    y: el.y + (-Math.cos(h) + Math.cos(el.heading)) / k,
    heading: h,
  }
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
