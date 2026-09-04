// ─────────────────────────────────────────────────────────────────────
// Network-definition XML export (Train section): segments (Straight /
// Circle / Spiral elements with start frame and superelevation),
// connections (shared track extremities), points (turnouts), crossings
// and catch points — the input format the Track Mesh Builder consumes.
// ─────────────────────────────────────────────────────────────────────
import { evaluatePath } from './geometry'
import { fitTrackPath, trackSlices, trackStartFrame } from './tracks'
import { functionLength } from './xyFunctions'
import type { Project, RailCrossing, RailPoint, RoadData } from '../state/store'
import type { Vec2 } from './types'

const esc = (value: string) =>
  value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;')

const num = (value: number, digits = 4) => Number.isFinite(value) ? value.toFixed(digits) : '0'

interface Extremity {
  trackId: string
  contact: 'start' | 'end'
  at: Vec2
}

function extremities(road: RoadData): Extremity[] {
  const path = fitTrackPath(road)
  if (!path) return []
  const first = path.elements[0]
  const startSample = { x: first.x, y: first.y }
  const end = evaluatePath(path, path.length)
  return [
    { trackId: road.id, contact: 'start', at: startSample },
    { trackId: road.id, contact: 'end', at: { x: end.x, y: end.y } },
  ]
}

function segmentXml(road: RoadData): string {
  const start = trackStartFrame(road)
  const slices = trackSlices(road) ?? []
  const elements = slices.map((slice) => {
    const fn = slice.fn
    switch (fn.kind) {
      case 'segment':
        return `        <Element type="Straight" length="${num(fn.length)}"/>`
      case 'arc':
        return `        <Element type="Circle" radius="${num(fn.radius)}" angle="${num(fn.angle, 6)}"/>`
      case 'clothoid':
        return `        <Element type="Spiral" length="${num(fn.length)}" radiusStart="${num(fn.radiusIn)}" radiusEnd="${num(fn.radiusOut)}"/>`
      default:
        return `        <Element type="Spline" kind="${fn.kind}" length="${num(functionLength(fn, slice.start))}"/>`
    }
  }).join('\n')
  const superelevation = (road.bankingProfile ?? [])
    .map((point) => `        <SuperelevationPoint s="${num(point.s)}" degrees="${num((point.z * 180) / Math.PI, 4)}"/>`)
    .join('\n')
  return [
    `      <Segment id="${road.id}" name="${esc(road.name)}">`,
    `        <Start x="${num(start?.x ?? 0)}" y="${num(start?.y ?? 0)}" heading="${num(start?.heading ?? 0, 6)}"/>`,
    `        <Geometry>`,
    elements,
    `        </Geometry>`,
    superelevation ? `        <Superelevation>\n${superelevation}\n        </Superelevation>` : null,
    `      </Segment>`,
  ].filter(Boolean).join('\n')
}

function pointXml(point: RailPoint): string {
  return [
    `      <Point id="${point.id}" name="${esc(point.name)}"`,
    `             facingSegment="${point.facingTrackId}" facingAtAlpha="${point.facingContact === 'start'}"`,
    `             trailingSegment="${point.trailingTrackId}" branchSegment="${point.branchTrackId}"/>`,
  ].join('\n')
}

function crossingXml(crossing: RailCrossing): string {
  return `      <Crossing trackA="${crossing.trackAId}" stationA="${num(crossing.sA)}" trackB="${crossing.trackBId}" stationB="${num(crossing.sB)}" angle="${num(crossing.angle, 6)}" kind="${crossing.kind}"/>`
}

/** Build the network-definition XML for a project's railway tracks. */
export function exportNetworkDefinition(project: Project): string {
  const tracks = project.roads.filter((road) => road.railway)

  const segmentBlocks = tracks.map(segmentXml).join('\n')

  // connections: track extremities that coincide (within 1 m)
  const connectionBlocks: string[] = []
  const all: Extremity[] = tracks.flatMap(extremities)
  for (let i = 0; i < all.length; i++) {
    for (let j = i + 1; j < all.length; j++) {
      const a = all[i]
      const b = all[j]
      if (a.trackId === b.trackId) continue
      if (Math.hypot(a.at.x - b.at.x, a.at.y - b.at.y) <= 1) {
        connectionBlocks.push(`      <Connection fromSegment="${a.trackId}" fromEnd="${a.contact === 'start' ? 'Alpha' : 'Beta'}" toSegment="${b.trackId}" toEnd="${b.contact === 'start' ? 'Alpha' : 'Beta'}"/>`)
      }
    }
  }

  const pointBlocks = (project.railPoints ?? []).map(pointXml).join('\n')
  const crossingBlocks = (project.railCrossings ?? []).map(crossingXml).join('\n')
  const catchBlocks = (project.catchPoints ?? [])
    .map((cp) => `      <CatchPoint trackId="${cp.trackId}" contact="${cp.contact === 'start' ? 'Alpha' : 'Beta'}" side="${cp.side === 'left' ? 'Left' : 'Right'}"/>`)
    .join('\n')

  return [
    `<?xml version="1.0" encoding="utf-8"?>`,
    `<NetworkDefinition>`,
    `  <Segments>`,
    segmentBlocks,
    `  </Segments>`,
    connectionBlocks.length ? `  <Connections>\n${connectionBlocks.join('\n')}\n  </Connections>` : `  <Connections/>`,
    pointBlocks ? `  <Points>\n${pointBlocks}\n  </Points>` : `  <Points/>`,
    crossingBlocks ? `  <Crossings>\n${crossingBlocks}\n  </Crossings>` : `  <Crossings/>`,
    catchBlocks ? `  <CatchPoints>\n${catchBlocks}\n  </CatchPoints>` : `  <CatchPoints/>`,
    `</NetworkDefinition>`,
  ].join('\n')
}
