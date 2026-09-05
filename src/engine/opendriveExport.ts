// ─────────────────────────────────────────────────────────────────────
// OpenDRIVE (.xodr) export — revMajor 1 / revMinor 4.
// Each road's XY-function chain maps onto planView geometry: segment →
// <line>, arc → <arc>, clothoid → <spiral>; point-based functions
// (polyline, ClothoidSpline, Bezier) are flattened to <line> elements.
// The cross section is written as one laneSection from the road's lane
// section, with a green centre road mark and white lane marks, plus an
// elevationProfile from the altitude handles.
// ─────────────────────────────────────────────────────────────────────
import type { Project, RoadData } from '../state/store'
import { getLaneSection } from '../state/store'
import type { XYFunction, Frame } from './xyFunctions'
import { functionLength, functionEndFrame, sampleFunction } from './xyFunctions'
import { trackStartFrame } from './tracks'
import type { Vec2 } from './types'

const esc = (v: string) =>
  v.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')
const num = (v: number, d = 4) => (Number.isFinite(v) ? v.toFixed(d) : '0')

const OPEN_DRIVE_HEADER = '<?xml version="1.0" encoding="utf-8"?>'

/** Intrinsic functions map 1:1 onto OpenDRIVE geometry elements. */
function intrinsicXml(fn: XYFunction, s: number, frame: Frame, length: number): string | null {
  const pos = `s="${num(s)}" x="${num(frame.x)}" y="${num(frame.y)}" hdg="${num(frame.heading, 6)}" length="${num(length)}"`
  switch (fn.kind) {
    case 'segment':
      return `      <geometry ${pos}>\n        <line/>\n      </geometry>`
    case 'arc':
      return `      <geometry ${pos}>\n        <arc curvature="${num(Math.sign(fn.angle) / fn.radius, 8)}"/>\n      </geometry>`
    case 'clothoid':
      return `      <geometry ${pos}>\n        <spiral curvStart="${num(fn.radiusIn !== 0 ? 1 / fn.radiusIn : 0, 8)}" curvEnd="${num(fn.radiusOut !== 0 ? 1 / fn.radiusOut : 0, 8)}"/>\n      </geometry>`
    default:
      return null
  }
}

/** Point-based functions are flattened into short line elements. */
function flattenedXml(frame: Frame, fn: XYFunction, s: number, length: number, ds = 2): string[] {
  const samples = sampleFunction(frame, fn, ds)
  const out: string[] = []
  for (let i = 1; i < samples.length; i++) {
    const a = samples[i - 1]
    const b = samples[i]
    const seg = Math.hypot(b.x - a.x, b.y - a.y)
    if (seg < 1e-3) continue
    out.push(
      `      <geometry s="${num(s + a.s)}" x="${num(a.x)}" y="${num(a.y)}" hdg="${num(b.heading, 6)}" length="${num(seg)}">\n        <line/>\n      </geometry>`,
    )
  }
  void length
  return out
}

function planViewXml(road: RoadData): string | null {
  const fns = road.functions ?? []
  const start = trackStartFrame(road)
  if (fns.length === 0 || !start) return null
  const geometry: string[] = []
  let s = 0
  let frame: Frame = start
  for (const fn of fns) {
    const length = functionLength(fn, frame)
    if (length <= 1e-6) continue
    const intrinsic = intrinsicXml(fn, s, frame, length)
    if (intrinsic) geometry.push(intrinsic)
    else geometry.push(...flattenedXml(frame, fn, s, length))
    s += length
    frame = functionEndFrame(frame, fn)
  }
  if (geometry.length === 0) return null
  return `      <planView>\n${geometry.join('\n')}\n      </planView>`
}

const laneType = (type: string): string =>
  type === 'sidewalk' ? 'sidewalk'
  : type === 'bike' ? 'biking'
  : type === 'curb' ? 'curb'
  : type === 'shoulder' || type === 'soft_shoulder' || type === 'hard_shoulder' ? 'shoulder'
  : 'driving'

const laneXml = (id: number, width: number, type: string): string =>
  [
    `            <lane id="${id}" type="${laneType(type)}" level="false">`,
    `              <width sOffset="0" a="${num(width)}" b="0" c="0" d="0"/>`,
    `              <roadMark sOffset="0" type="solid" weight="standard" color="white" laneChange="both"/>`,
    `            </lane>`,
  ].join('\n')

function lanesXml(road: RoadData): string {
  const section = getLaneSection(road)
  const left = section.left
    .map((lane, i) => laneXml(i + 1, lane.width, lane.type))
    .reverse()
    .join('\n')
  const right = section.right
    .map((lane, i) => laneXml(-(i + 1), lane.width, lane.type))
    .join('\n')
  return [
    `      <lanes>`,
    `        <laneSection s="0">`,
    `          <left>`,
    left,
    `          </left>`,
    `          <center>`,
    `            <lane id="0"/>`,
    `            <roadMark sOffset="0" type="solid" weight="standard" color="green" laneChange="none"/>`,
    `          </center>`,
    `          <right>`,
    right,
    `          </right>`,
    `        </laneSection>`,
    `      </lanes>`,
  ].join('\n')
}

function elevationXml(road: RoadData, length: number): string {
  const profile = road.elevationProfile ?? []
  if (profile.length === 0) return '      <elevationProfile/>'
  const rows: string[] = []
  for (let i = 0; i < profile.length; i++) {
    const a = profile[i]
    const b = profile[i + 1]
    const slope = b && b.s > a.s + 1e-9 ? (b.z - a.z) / (b.s - a.s) : 0
    rows.push(`        <elevation s="${num(a.s)}" a="${num(a.z)}" b="${num(slope, 6)}" c="0" d="0"/>`)
  }
  if (profile[profile.length - 1].s < length - 1e-3) {
    const last = profile[profile.length - 1]
    rows.push(`        <elevation s="${num(length)}" a="${num(last.z)}" b="0" c="0" d="0"/>`)
  }
  return `      <elevationProfile>\n${rows.join('\n')}\n      </elevationProfile>`
}

function roadXml(road: RoadData, id: number): string | null {
  const fns = road.functions ?? []
  const start = trackStartFrame(road)
  if (fns.length === 0 || !start) return null
  const planView = planViewXml(road)
  if (!planView) return null
  const length = fns.reduce((sum, fn) => sum + functionLength(fn, start), 0)
  return [
    `    <road name="${esc(road.name)}" id="${id}" length="${num(length)}" junction="-1">`,
    `      <link/>`,
    planView,
    elevationXml(road, length),
    lanesXml(road),
    `    </road>`,
  ].join('\n')
}

/** Export the project's road network as an OpenDRIVE 1.4 document. */
export function exportOpenDrive(project: Project): string {
  const points: Vec2[] = []
  for (const road of project.roads) {
    const path = (() => {
      const fns = road.functions ?? []
      const start = trackStartFrame(road)
      if (fns.length === 0 || !start) return null
      let frame: Frame = start
      for (const fn of fns) frame = functionEndFrame(frame, fn)
      return { x: frame.x, y: frame.y }
    })()
    if (path) points.push(path)
  }
  const north = points.length ? Math.max(...points.map((p) => p.y)) : 0
  const south = points.length ? Math.min(...points.map((p) => p.y)) : 0
  const west = points.length ? Math.min(...points.map((p) => p.x)) : 0
  const east = points.length ? Math.max(...points.map((p) => p.x)) : 0

  const roadBlocks = project.roads
    .map((road, index) => roadXml(road, index + 1))
    .filter((block): block is string => block !== null)
    .join('\n')

  return [
    OPEN_DRIVE_HEADER,
    `<OpenDRIVE>`,
    `  <header revMajor="1" revMinor="4" name="${esc(project.name)}"`,
    `        north="${num(north, 2)}" south="${num(south, 2)}" east="${num(east, 2)}" west="${num(west, 2)}"`,
    `        vendor="OpenGeoStudio"/>`,
    roadBlocks,
    `</OpenDRIVE>`,
  ].join('\n')
}

