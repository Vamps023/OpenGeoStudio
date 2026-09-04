// Builds viewport overlay specs (axis lines, arrows, intersection nodes,
// links, contours, ways, exit arrows, handles) from project state —
// the visual layer system of the SCANeR Roads tab.
import type { OverlayLine, OverlayMarker } from '../viewport/RoadViewport'
import type { Project, RoadData, LayerFlags } from '../state/store'
import type { IntersectionData, IntersectionWay } from '../engine/intersections'
import { allWays, computeContours, computeWays, resolveTracks } from '../engine/intersections'
import { FUNCTION_COLORS } from '../engine/xyFunctions'
import { trackSlices, trackStartFrame, fitTrackPath } from '../engine/tracks'
import { samplePath, samplePathRange } from '../engine/geometry'
import type { Vec2 } from '../engine/types'

export interface ExitEndpointInfo {
  roadId: string
  contact: 'start' | 'end'
  position: Vec2
}

export interface OverlayInput {
  project: Project
  layers: LayerFlags
  selection: { trackIds: string[]; intersectionId: string | null }
  lockedPassageways: string[]
  selectedTrackStation: number | null
}

export function buildOverlays(input: OverlayInput): { lines: OverlayLine[]; markers: OverlayMarker[]; exits: ExitEndpointInfo[] } {
  const { project, layers, selection, lockedPassageways } = input
  const lines: OverlayLine[] = []
  const markers: OverlayMarker[] = []
  const exits: ExitEndpointInfo[] = []
  if (!project) return { lines, markers, exits }

  const roads = project.roads
  const resolved = resolveTracks(roads)

  // ── Road logical content: per-function colored axes + orientation arrows ──
  if (layers.roadLogicalContent) {
    for (const road of roads) {
      if (!road.functions || road.functions.length === 0) continue
      const slices = trackSlices(road)
      if (!slices) continue
      const selected = selection.trackIds.includes(road.id)
      let sCursor = 0
      for (const slice of slices) {
        const path = fitTrackPath(road)
        if (!path) break
        const points = samplePathRange(path, sCursor, sCursor + slice.length, 2).map((s) => ({ x: s.x, y: s.y }))
        sCursor += slice.length
        if (points.length < 2) continue
        if (selected) {
          // selected track axis turns white (doc 5.5.4.2.2)
          lines.push({ points, color: '#ffffff', width: 0.7, opacity: 0.95 })
        } else {
          lines.push({ points, color: FUNCTION_COLORS[slice.fn.kind], width: 0.45, opacity: 0.9 })
        }
      }
      // orientation arrows at each function start of the selected track
      if (selected) {
        for (const slice of slices) {
          markers.push({
            id: `orient:${road.id}:${slice.index}`,
            point: { x: slice.start.x, y: slice.start.y },
            color: '#e2e8f0',
            shape: 'arrow',
            heading: slice.start.heading,
            size: 2.2,
          })
        }
      }
    }
  }

  // ── Sub-network exits (doc 5.5.4.2.16) ──
  for (const road of roads) {
    for (const contact of road.subNetworkExits ?? []) {
      const info = exitInfo(road, contact)
      if (!info) continue
      exits.push({ roadId: road.id, contact, position: info.position })
      markers.push({
        id: `exit:${road.id}:${contact}`,
        point: info.position,
        color: '#38bdf8',
        shape: 'arrow',
        heading: info.heading,
        size: 2.6,
      })
    }
  }

  // ── Intersections ──
  const intersections = project.intersections ?? []
  for (const intersection of intersections) {
    const selected = selection.intersectionId === intersection.id
    if (layers.intersectionLogicalContent) {
      // yellow node (doc: single yellow point)
      markers.push({
        id: `node:${intersection.id}`,
        point: intersection.position,
        color: selected ? '#fde047' : '#eab308',
        shape: 'circle',
        size: selected ? 3.2 : 2.4,
        draggable: true,
      })
      // yellow links node → each connected track end
      for (const end of intersection.trackEnds) {
        const item = resolved.get(end.trackId)
        if (!item) continue
        const endPos = trackEndPosition(item, end.contact)
        if (!endPos) continue
        lines.push({ points: [intersection.position, endPos], color: '#eab308', width: 0.35, opacity: 0.85 })
      }
      // yellow contours between borders
      const contours = computeContours(intersection, resolved)
      for (const contour of contours) {
        lines.push({ points: contour.points, color: '#facc15', width: 0.5, opacity: 0.9 })
        markers.push({ point: contour.corner, color: '#fde68a', shape: 'circle', size: 1 })
      }
      // main path: fuchsia contour indicator (doc 5.5.4.4.3)
      if (intersection.mainPath && selected) {
        for (const trackId of intersection.mainPath) {
          const item = resolved.get(trackId)
          if (!item) continue
          const pts = samplePath(item.path, 4).map((s) => ({ x: s.x, y: s.y }))
          if (pts.length >= 2) lines.push({ points: pts, color: '#e879f9', width: 1.1, opacity: 0.85 })
        }
      }
      // contour handles: draggable squares
      intersection.contourHandles.forEach((handle, index) => {
        markers.push({
          id: `contour:${intersection.id}:${index}`,
          point: handle,
          color: '#f97316',
          shape: 'square',
          size: 1.6,
          draggable: true,
        })
      })
    }
    // ways visualisation (doc 5.5.4.4.12) — implicit (dark green) and
    // imported explicit (purple) ways
    if ((layers.wayAxis || layers.wayLogicalContents) && intersection.trackEnds.length >= 2) {
      const ways = allWays(intersection, resolved)
      const locks = lockedPassageways
      for (const way of ways) {
        const matchesLock = locks.length === 0
          || (locks.length === 2 ? locks.includes(endKey(way.from)) && locks.includes(endKey(way.to)) : locks.includes(endKey(way.from)) || locks.includes(endKey(way.to)))
        if (!matchesLock && locks.length > 0) continue
        const points = way.samples.map((s) => ({ x: s.x, y: s.y }))
        if (points.length < 2) continue
        const color = !way.authorized ? '#ef4444' : way.explicit ? '#c084fc' : '#1f9d55'
        lines.push({
          points,
          color,
          width: layers.wayLogicalContents ? Math.max(0.6, way.laneCount * way.laneWidth * 0.12) : 0.3,
          opacity: 0.85,
          wayKey: way.key,
        })
        // incoming/outgoing authorization arrows (green/red, doc 5.5.4.4.11.1)
        if (selected) {
          const mid = points[Math.floor(points.length / 2)]
          markers.push({
            id: `way:${intersection.id}:${way.key}`,
            point: mid,
            color: way.authorized ? '#22c55e' : '#dc2626',
            shape: 'arrow',
            heading: way.samples[Math.floor(way.samples.length / 2)].heading,
            size: 2.4,
          })
        }
        // way axis: circulation arrows along the way (doc ways visualisation)
        if (layers.wayAxis) {
          const step = Math.max(1, Math.floor(way.samples.length / 6))
          for (let i = step; i < way.samples.length - 1; i += step) {
            const sample = way.samples[i]
            markers.push({
              point: { x: sample.x, y: sample.y },
              color: way.authorized ? '#facc15' : '#f87171',
              shape: 'arrow',
              heading: sample.heading,
              size: 1.6,
            })
          }
        }
      }
    }
  }

  return { lines, markers, exits }
}

function endKey(end: { trackId: string; contact: 'start' | 'end' }): string {
  return `${end.trackId}:${end.contact}`
}

function exitInfo(road: RoadData, contact: 'start' | 'end'): { position: Vec2; heading: number } | null {
  const pts = road.points
  if (road.functions && road.functions.length > 0) {
    const item = resolveTracks([road]).get(road.id)
    if (item) {
      if (contact === 'start') {
        const start = trackStartFrame(road)
        if (start) return { position: { x: start.x, y: start.y }, heading: start.heading }
      } else {
        const last = item.path.elements[item.path.elements.length - 1]
        if (last.points && last.points.length > 0) {
          const p = last.points[last.points.length - 1]
          const prev = last.points.length > 1 ? last.points[last.points.length - 2] : { x: last.x, y: last.y }
          return { position: p, heading: Math.atan2(p.y - prev.y, p.x - prev.x) }
        }
      }
    }
  }
  if (!pts || pts.length < 2) return null
  return contact === 'start'
    ? { position: pts[0], heading: Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) }
    : { position: pts[pts.length - 1], heading: Math.atan2(pts[pts.length - 1].y - pts[pts.length - 2].y, pts[pts.length - 1].x - pts[pts.length - 2].x) }
}

function trackEndPosition(item: { path: { elements: { points?: Vec2[]; x: number; y: number; heading: number; length: number; curvature: number; type: string }[] } }, contact: 'start' | 'end'): Vec2 | null {
  const els = item.path.elements
  if (els.length === 0) return null
  const el = contact === 'end' ? els[els.length - 1] : els[0]
  if (contact === 'start') {
    if (el.points && el.points.length > 0) return el.points[0]
    return { x: el.x, y: el.y }
  }
  if (el.points && el.points.length > 0) return el.points[el.points.length - 1]
  if (el.type === 'line') return { x: el.x + Math.cos(el.heading) * el.length, y: el.y + Math.sin(el.heading) * el.length }
  if (el.type === 'arc') {
    const h = el.heading + el.curvature * el.length
    return { x: el.x + (Math.sin(h) - Math.sin(el.heading)) / el.curvature, y: el.y + (-Math.cos(h) + Math.cos(el.heading)) / el.curvature }
  }
  return { x: el.x, y: el.y }
}

export function computeWaysForIntersection(intersection: IntersectionData, project: Project): IntersectionWay[] {
  return computeWays(intersection, resolveTracks(project.roads))
}

export function contourCornersFor(intersection: IntersectionData, project: Project) {
  return computeContours(intersection, resolveTracks(project.roads))
}
