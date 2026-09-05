import { useCallback, useEffect, useRef, useState } from 'react'
import { Map as MapLibreMap, NavigationControl, ScaleControl, Point, setWorkerUrl } from 'maplibre-gl'
import 'maplibre-gl/dist/maplibre-gl.css'
import maplibreWorkerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?url'
import type { LatLngRing } from '../engine/osmBuildings'
import type { GeoBounds } from '../engine/crs'

// Reuse the same worker setup as TerrainMap (app:// protocol safe).
setWorkerUrl(new URL(maplibreWorkerUrl, import.meta.url).href)

interface Props {
  /** polygon vertices in lng/lat (no closing duplicate) */
  polygon: LatLngRing[]
  onPolygonChange: (ring: LatLngRing[]) => void
  /** imported building footprints (lng/lat rings) drawn as an overlay */
  buildings?: { id: string; ring: [number, number][] }[]
  /** move the map camera to a location (location search) */
  flyTo?: { lng: number; lat: number; zoom?: number } | null
  /** disable drawing interactions (e.g. while fetching) */
  drawingDisabled?: boolean
  /** terrain working-area bounds shown as a dashed reference rectangle so
   *  the user can see where their Terrain selection is and draw around it */
  terrainBounds?: GeoBounds | null
}

interface PixelPt {
  x: number
  y: number
}

interface PixelRect {
  left: number
  top: number
  width: number
  height: number
}

export default function OsmBuilderMap({
  polygon,
  onPolygonChange,
  buildings,
  flyTo,
  drawingDisabled,
  terrainBounds,
}: Props) {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef = useRef<MapLibreMap | null>(null)
  const [mapReady, setMapReady] = useState(false)
  const [mapError, setMapError] = useState<string | null>(null)
  // polygon vertices + building footprints projected to pixels
  const [polyPx, setPolyPx] = useState<PixelPt[]>([])
  const [buildingPolys, setBuildingPolys] = useState<{ id: string; points: string }[]>([])
  // terrain working-area rectangle in pixels (reference overlay)
  const [terrainRectPx, setTerrainRectPx] = useState<PixelRect | null>(null)
  // vertex being dragged (index), or null
  const dragVertexRef = useRef<number | null>(null)
  // hover state for cursor feedback
  const [hoverVertex, setHoverVertex] = useState<number | null>(null)

  const onPolygonChangeRef = useRef(onPolygonChange)
  onPolygonChangeRef.current = onPolygonChange
  const polygonRef = useRef(polygon)
  polygonRef.current = polygon
  const drawingDisabledRef = useRef(drawingDisabled)
  drawingDisabledRef.current = drawingDisabled

  // Camera moves from outside
  useEffect(() => {
    const map = mapRef.current
    if (!flyTo || !map) return
    map.flyTo({ center: [flyTo.lng, flyTo.lat], zoom: flyTo.zoom ?? 14, essential: true })
  }, [flyTo])

  // ─── Map creation (once) ──────────────────────────────────────
  useEffect(() => {
    if (!containerRef.current || mapRef.current) return

    const map = new MapLibreMap({
      container: containerRef.current,
      style: {
        version: 8,
        sources: {
          'esri-imagery': {
            type: 'raster',
            tiles: [
              'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
            ],
            tileSize: 256,
            maxzoom: 19,
            attribution: 'Esri',
          },
        },
        layers: [
          {
            id: 'esri-imagery',
            type: 'raster',
            source: 'esri-imagery',
            minzoom: 0,
            maxzoom: 22,
          },
        ],
      },
      center: [-112.1129, 36.1069],
      zoom: 12,
      minZoom: 2,
      maxZoom: 22,
      maxPitch: 0,
      dragRotate: false,
      touchPitch: false,
      attributionControl: false,
    })

    map.boxZoom.disable()
    map.doubleClickZoom.disable()
    map.addControl(new NavigationControl({ visualizePitch: false }), 'top-right')
    map.addControl(new ScaleControl(), 'bottom-left')
    map.on('load', () => setMapReady(true))
    map.on('error', (event) => {
      const message = (event as { error?: { message?: string } }).error?.message
      setMapError(message || 'The map failed to load')
    })

    const resizeTimer = window.setTimeout(() => {
      if (mapRef.current === map) map.resize()
    }, 150)

    const canvas = map.getCanvas()
    const container = containerRef.current!

    const canvasPoint = (clientX: number, clientY: number): PixelPt => {
      const r = canvas.getBoundingClientRect()
      return { x: clientX - r.left, y: clientY - r.top }
    }

    // ── MapLibre native click: add a vertex ──
    // Using map.on('click') instead of a DOM listener on the container because
    // MapLibre's canvas can consume/stopPropagation on DOM click events, and
    // the SVG overlay (pointer-events) would block container clicks once
    // vertices exist. map.on('click') fires after MapLibre has processed the
    // interaction and gives us e.lngLat directly.
    const onMapClick = (e: { lngLat: { lng: number; lat: number }; point: { x: number; y: number } }) => {
      if (drawingDisabledRef.current) return
      if (dragVertexRef.current !== null) return
      const ring = polygonRef.current
      // If clicking near the first vertex and we have ≥3 vertices, close the polygon
      if (ring.length >= 3) {
        const firstPx = map.project([ring[0].lng, ring[0].lat])
        if (Math.hypot(firstPx.x - e.point.x, firstPx.y - e.point.y) < 10) {
          return
        }
      }
      onPolygonChangeRef.current([...ring, { lng: e.lngLat.lng, lat: e.lngLat.lat }])
    }

    // ── MapLibre native dblclick: finish/close the polygon ──
    // Double-click also fires a click event first (adding a vertex). Remove
    // the stray duplicate if the last vertex is within a few px of the click.
    const onMapDblClick = (e: { point: { x: number; y: number } }) => {
      const ring = polygonRef.current
      if (ring.length >= 2) {
        const last = ring[ring.length - 1]
        const lastPx = map.project([last.lng, last.lat])
        if (Math.hypot(lastPx.x - e.point.x, lastPx.y - e.point.y) < 6) {
          onPolygonChangeRef.current(ring.slice(0, -1))
        }
      }
    }

    map.on('click', onMapClick)
    map.on('dblclick', onMapDblClick)

    // ── Vertex drag: mousedown on a vertex marker starts a drag ──
    // Vertex markers are SVG <circle> elements with data-vertex-index.
    // These are DOM events on the SVG circles (which opt into pointer-events),
    // bubbling to the container.
    const onVertexMouseDown = (e: MouseEvent) => {
      if (drawingDisabledRef.current) return
      const target = e.target as SVGElement | null
      const idxAttr = target?.getAttribute('data-vertex-index')
      if (idxAttr === null || idxAttr === undefined) return
      e.preventDefault()
      e.stopPropagation()
      const idx = Number(idxAttr)
      dragVertexRef.current = idx
      map.dragPan.disable()
      canvas.style.cursor = 'grabbing'
    }

    const onMouseMove = (e: MouseEvent) => {
      if (dragVertexRef.current === null) return
      e.preventDefault()
      const idx = dragVertexRef.current
      const p = canvasPoint(e.clientX, e.clientY)
      const lngLat = map.unproject(new Point(p.x, p.y))
      const ring = polygonRef.current.slice()
      ring[idx] = { lng: lngLat.lng, lat: lngLat.lat }
      onPolygonChangeRef.current(ring)
    }

    const onMouseUp = (e: MouseEvent) => {
      if (dragVertexRef.current === null) return
      e.preventDefault()
      dragVertexRef.current = null
      canvas.style.cursor = ''
      map.dragPan.enable()
    }

    // Vertex drag listeners go on the container — they check e.target for the
    // data-vertex-index attribute, so only vertex circle mousedowns trigger a drag.
    container.addEventListener('mousedown', onVertexMouseDown)
    container.addEventListener('mousemove', onMouseMove)
    window.addEventListener('mouseup', onMouseUp)

    mapRef.current = map

    const resizeObserver = new ResizeObserver(() => map.resize())
    resizeObserver.observe(containerRef.current)

    return () => {
      resizeObserver.disconnect()
      map.off('click', onMapClick)
      map.off('dblclick', onMapDblClick)
      container.removeEventListener('mousedown', onVertexMouseDown)
      container.removeEventListener('mousemove', onMouseMove)
      window.removeEventListener('mouseup', onMouseUp)
      clearTimeout(resizeTimer)
      map.remove()
      mapRef.current = null
    }
  }, [])

  // ─── Recompute pixel projections on map move/zoom ─────────────
  const recompute = useCallback(() => {
    const map = mapRef.current
    if (!map || !map.getCanvas().width) return

    // polygon vertices
    if (polygon.length > 0) {
      setPolyPx(polygon.map((v) => {
        const p = map.project([v.lng, v.lat])
        return { x: p.x, y: p.y }
      }))
    } else {
      setPolyPx([])
    }

    // building footprints
    if (buildings && buildings.length > 0) {
      const polys: { id: string; points: string }[] = []
      for (const building of buildings) {
        let path = ''
        let offScreen = 0
        for (const [lng, lat] of building.ring) {
          const p = map.project([lng, lat])
          path += `${Math.round(p.x)},${Math.round(p.y)} `
          if (p.x < -50 || p.y < -50 || p.x > map.getCanvas().clientWidth + 50 || p.y > map.getCanvas().clientHeight + 50) offScreen++
        }
        if (offScreen === building.ring.length) continue
        polys.push({ id: building.id, points: path.trim() })
      }
      setBuildingPolys(polys)
    } else {
      setBuildingPolys([])
    }

    // terrain working-area reference rectangle
    if (terrainBounds) {
      const nw = map.project([terrainBounds.west, terrainBounds.north])
      const se = map.project([terrainBounds.east, terrainBounds.south])
      setTerrainRectPx({
        left: nw.x,
        top: nw.y,
        width: Math.abs(se.x - nw.x),
        height: Math.abs(se.y - nw.y),
      })
    } else {
      setTerrainRectPx(null)
    }
  }, [polygon, buildings, terrainBounds])

  useEffect(() => {
    const map = mapRef.current
    if (!map) return
    const onLoad = () => recompute()
    const onMoveEnd = () => recompute()
    const onRender = () => {
      if (map.isMoving() || map.isZooming()) recompute()
    }
    map.on('load', onLoad)
    map.on('moveend', onMoveEnd)
    map.on('render', onRender)
    recompute()
    return () => {
      map.off('load', onLoad)
      map.off('moveend', onMoveEnd)
      map.off('render', onRender)
    }
  }, [recompute])

  // Build the SVG polygon path string (closed)
  const polyPath = polyPx.length > 0
    ? polyPx.map((p) => `${Math.round(p.x)},${Math.round(p.y)}`).join(' ')
    : ''
  const isClosed = polygon.length >= 3

  return (
    <div className="terrain-map-container">
      <div ref={containerRef} className="terrain-map-canvas" />

      {/* ─── Terrain working-area reference rectangle (DOM overlay) ─── */}
      {terrainRectPx && (
        <div
          className="pointer-events-none absolute border-2 border-dashed border-cyan-400/70 bg-cyan-400/5"
          style={{
            left: terrainRectPx.left,
            top: terrainRectPx.top,
            width: terrainRectPx.width,
            height: terrainRectPx.height,
            zIndex: 6,
          }}
        >
          <span
            className="absolute left-1 top-0.5 text-[10px] font-medium select-none text-cyan-300"
            style={{ textShadow: '0 0 3px #000, 0 0 3px #000' }}
          >
            Terrain area
          </span>
        </div>
      )}

      {/* ─── Building footprints (SVG overlay) ─── */}
      {buildingPolys.length > 0 && (
        <svg className="pointer-events-none absolute inset-0" style={{ zIndex: 8 }} width="100%" height="100%">
          {buildingPolys.map(({ id, points }) => (
            <polygon
              key={id}
              points={points}
              fill="rgba(251, 146, 60, 0.25)"
              stroke="#fb923c"
              strokeWidth={1.5}
            />
          ))}
        </svg>
      )}

      {/* ─── Drawn polygon (SVG overlay) ─── */}
      {/* pointer-events-none on the SVG so map clicks pass through; vertex
          hit circles opt back in with pointer-events: auto for dragging. */}
      {polyPx.length > 0 && (
        <svg className="pointer-events-none absolute inset-0" style={{ zIndex: 15 }} width="100%" height="100%">
          {/* polygon fill + outline */}
          {isClosed && polyPath && (
            <polygon
              points={polyPath}
              fill="rgba(34, 197, 94, 0.15)"
              stroke="#22c55e"
              strokeWidth={2}
              className="pointer-events-none"
            />
          )}
          {/* polyline (when not yet closed) */}
          {!isClosed && polyPath && (
            <polyline
              points={polyPath}
              fill="none"
              stroke="#22c55e"
              strokeWidth={2}
              strokeDasharray="6 4"
              className="pointer-events-none"
            />
          )}
          {/* vertex markers */}
          {polyPx.map((p, i) => (
            <g key={i}>
              {/* larger transparent hit area for dragging — opts into
                  pointer-events so the SVG's pointer-events:none doesn't
                  block vertex interaction */}
              <circle
                cx={p.x}
                cy={p.y}
                r={12}
                fill="transparent"
                style={{ cursor: drawingDisabled ? 'default' : 'grab', pointerEvents: 'auto' }}
                data-vertex-index={i}
                onMouseEnter={() => setHoverVertex(i)}
                onMouseLeave={() => setHoverVertex(null)}
              />
              <circle
                cx={p.x}
                cy={p.y}
                r={hoverVertex === i ? 6 : 5}
                fill={i === 0 && isClosed ? '#22c55e' : '#0b1220'}
                stroke="#22c55e"
                strokeWidth={2}
                className="pointer-events-none"
              />
              {/* close-polygon hint on first vertex */}
              {i === 0 && isClosed && hoverVertex === i && (
                <text x={p.x + 10} y={p.y - 10} fill="#22c55e" fontSize="11" className="pointer-events-none select-none">
                  close
                </text>
              )}
            </g>
          ))}
        </svg>
      )}

      {/* ─── Status overlays ─── */}
      {!mapReady && !mapError && (
        <div className="pointer-events-none absolute left-1/2 top-4 -translate-x-1/2 rounded-full border border-border bg-card/85 px-4 py-1.5 text-xs font-medium text-muted-foreground shadow-lg backdrop-blur" style={{ zIndex: 30 }}>
          Loading map…
        </div>
      )}
      {mapError && (
        <div className="absolute left-1/2 top-4 -translate-x-1/2 rounded-md border border-destructive/40 bg-destructive/10 px-3 py-2 text-xs text-destructive shadow-lg backdrop-blur" style={{ zIndex: 30 }}>
          Map error: {mapError}
        </div>
      )}
      <div className="terrain-map-hint">
        {drawingDisabled
          ? 'Fetching buildings…'
          : isClosed
            ? 'Drag vertices to edit · Double-click to finish · Click first vertex to close'
            : 'Click to add polygon vertices · Double-click to finish'}
      </div>
    </div>
  )
}
