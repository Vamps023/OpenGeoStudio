import { useEffect, useRef, useState, useCallback } from 'react'
import { Map as MapLibreMap, NavigationControl, ScaleControl, Point, setWorkerUrl } from 'maplibre-gl'
import 'maplibre-gl/dist/maplibre-gl.css'
import maplibreWorkerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?url'
import type { GeoBounds } from '../engine/crs'
import type { TileGrid } from '../engine/tileGrid'

// MapLibre auto-resolves its worker script only on http(s) origins — the
// custom app:// protocol used in production returns "" and the map never
// boots. Point it at the bundled worker explicitly (works in dev too).
setWorkerUrl(new URL(maplibreWorkerUrl, import.meta.url).href)

interface Props {
  onBoundsSelected: (bounds: GeoBounds) => void
  selectedBounds: GeoBounds | null
  tileGrid: TileGrid | null
  selectedTiles: Set<string>
  onToggleTile: (row: number, col: number) => void
  gridVisible: boolean
  /** move the map camera to a location (location search / set coordinates) */
  flyTo?: { lng: number; lat: number; zoom?: number } | null
}

interface PixelRect {
  left: number
  top: number
  width: number
  height: number
}

export default function TerrainMap({
  onBoundsSelected,
  selectedBounds,
  tileGrid,
  selectedTiles,
  onToggleTile,
  gridVisible,
  flyTo,
}: Props) {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef = useRef<MapLibreMap | null>(null)
  const isDraggingRef = useRef(false)
  const dragStartRef = useRef<{ x: number; y: number } | null>(null)

  // Camera moves requested from outside (location search / coordinates)
  useEffect(() => {
    const map = mapRef.current
    if (!flyTo || !map) return
    map.flyTo({ center: [flyTo.lng, flyTo.lat], zoom: flyTo.zoom ?? 13, essential: true })
  }, [flyTo])
  const liveBoundsRef = useRef<GeoBounds | null>(null)
  const [liveRectPx, setLiveRectPx] = useState<{ x: number; y: number; size: number } | null>(null)
  const [mapReady, setMapReady] = useState(false)
  const [mapError, setMapError] = useState<string | null>(null)
  // Pixel rects for each tile, recomputed on every map move/zoom
  const [tileRects, setTileRects] = useState<{ key: string; row: number; col: number; rect: PixelRect }[]>([])
  // Selection rectangle in pixels (for the confirmed selection)
  const [selectionRect, setSelectionRect] = useState<PixelRect | null>(null)

  const onBoundsSelectedRef = useRef(onBoundsSelected)
  onBoundsSelectedRef.current = onBoundsSelected

  const onToggleTileRef = useRef(onToggleTile)
  onToggleTileRef.current = onToggleTile

  // ─── Map creation (runs once) ────────────────────────────────
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
      zoom: 11,
      minZoom: 2,
      maxZoom: 22,
      maxPitch: 0,        // top-down only — no tilt
      dragRotate: false,  // no right-click drag rotation
      touchPitch: false,  // no touch tilt   // no drag tilt
      attributionControl: false,
    })

    map.boxZoom.disable()
    map.addControl(new NavigationControl({ visualizePitch: false }), 'top-right')
    map.addControl(new ScaleControl(), 'bottom-left')
    map.on('load', () => {
      setMapReady(true)
      ;(window as unknown as Record<string, unknown>).__ogsMapReady = true
    })
    map.on('error', (event) => {
      const message = (event as { error?: { message?: string } }).error?.message
      setMapError(message || 'The map failed to load')
    })

    const resizeTimer = window.setTimeout(() => {
      if (mapRef.current === map) map.resize()
    }, 150)

    ;(window as unknown as Record<string, unknown>).__ogsMap = map

    const canvas = map.getCanvas()
    const container = containerRef.current!

    const canvasPoint = (clientX: number, clientY: number) => {
      const r = canvas.getBoundingClientRect()
      return { x: clientX - r.left, y: clientY - r.top }
    }

    // ── Shift+drag square selection ──
    const onMouseDown = (e: MouseEvent) => {
      if (!e.shiftKey) return
      e.preventDefault()
      e.stopPropagation()
      map.dragPan.disable()
      const p = canvasPoint(e.clientX, e.clientY)
      isDraggingRef.current = true
      dragStartRef.current = p
      canvas.style.cursor = 'crosshair'
      setLiveRectPx({ x: p.x, y: p.y, size: 0 })
    }

    const onMouseMove = (e: MouseEvent) => {
      if (!isDraggingRef.current || !dragStartRef.current) return
      e.preventDefault()
      const start = dragStartRef.current
      const cur = canvasPoint(e.clientX, e.clientY)
      const dX = cur.x - start.x
      const dY = cur.y - start.y
      const size = Math.max(Math.abs(dX), Math.abs(dY))
      const signX = dX >= 0 ? 1 : -1
      const signY = dY >= 0 ? 1 : -1
      const endX = start.x + signX * size
      const endY = start.y + signY * size

      const startLngLat = map.unproject(new Point(start.x, start.y))
      const endLngLat = map.unproject(new Point(endX, endY))

      const bounds: GeoBounds = {
        west: Math.min(startLngLat.lng, endLngLat.lng),
        south: Math.min(startLngLat.lat, endLngLat.lat),
        east: Math.max(startLngLat.lng, endLngLat.lng),
        north: Math.max(startLngLat.lat, endLngLat.lat),
      }
      liveBoundsRef.current = bounds
      const left = Math.min(start.x, endX)
      const top = Math.min(start.y, endY)
      setLiveRectPx({ x: left, y: top, size })
    }

    const onMouseUp = (e: MouseEvent) => {
      if (!isDraggingRef.current) return
      e.preventDefault()
      isDraggingRef.current = false
      canvas.style.cursor = ''
      map.dragPan.enable()
      if (liveBoundsRef.current) {
        onBoundsSelectedRef.current(liveBoundsRef.current)
      }
      dragStartRef.current = null
      liveBoundsRef.current = null
      setLiveRectPx(null)
    }

    container.addEventListener('mousedown', onMouseDown)
    container.addEventListener('mousemove', onMouseMove)
    container.addEventListener('mouseup', onMouseUp)
    window.addEventListener('mouseup', onMouseUp)

    mapRef.current = map

    const resizeObserver = new ResizeObserver(() => map.resize())
    resizeObserver.observe(containerRef.current)

    return () => {
      resizeObserver.disconnect()
      container.removeEventListener('mousedown', onMouseDown)
      container.removeEventListener('mousemove', onMouseMove)
      container.removeEventListener('mouseup', onMouseUp)
      window.removeEventListener('mouseup', onMouseUp)
      clearTimeout(resizeTimer)
      map.remove()
      mapRef.current = null
    }
  }, [])

  // ─── Recompute pixel rects on map move/zoom ──────────────────
  const recomputeRects = useCallback(() => {
    const map = mapRef.current
    if (!map) return
    // Don't try to project before the map is ready
    if (!map.getCanvas().width) return

    // Selection rectangle
    if (selectedBounds) {
      const nw = map.project([selectedBounds.west, selectedBounds.north])
      const se = map.project([selectedBounds.east, selectedBounds.south])
      setSelectionRect({
        left: nw.x,
        top: nw.y,
        width: Math.abs(se.x - nw.x),
        height: Math.abs(se.y - nw.y),
      })
    } else {
      setSelectionRect(null)
    }

    // Tile grid rectangles
    if (tileGrid && gridVisible) {
      const rects: { key: string; row: number; col: number; rect: PixelRect }[] = []
      for (const tile of tileGrid.tiles) {
        const nw = map.project([tile.bounds.west, tile.bounds.north])
        const se = map.project([tile.bounds.east, tile.bounds.south])
        rects.push({
          key: `${tile.row},${tile.col}`,
          row: tile.row,
          col: tile.col,
          rect: {
            left: nw.x,
            top: nw.y,
            width: Math.abs(se.x - nw.x),
            height: Math.abs(se.y - nw.y),
          },
        })
      }
      setTileRects(rects)
    } else {
      setTileRects([])
    }
  }, [selectedBounds, tileGrid, gridVisible])

  // Recompute on load and when bounds/grid/visibility change (not on every move frame)
  useEffect(() => {
    const map = mapRef.current
    if (!map) return

    const onLoad = () => recomputeRects()
    const onMoveEnd = () => recomputeRects()
    // Use 'render' for smooth updates during zoom — it fires after each frame
    // but is less noisy than 'move'
    const onRender = () => {
      if (map.isMoving() || map.isZooming()) recomputeRects()
    }

    map.on('load', onLoad)
    map.on('moveend', onMoveEnd)
    map.on('render', onRender)

    // Try immediately
    recomputeRects()

    return () => {
      map.off('load', onLoad)
      map.off('moveend', onMoveEnd)
      map.off('render', onRender)
    }
  }, [recomputeRects])

  return (
    <div className="terrain-map-container">
      <div ref={containerRef} className="terrain-map-canvas" />

      {/* ─── Confirmed selection rectangle (DOM overlay) ─── */}
      {selectionRect && !liveRectPx && (
        <div
          className="pointer-events-none absolute border-2 border-green-400 bg-green-400/10"
          style={{
            left: selectionRect.left,
            top: selectionRect.top,
            width: selectionRect.width,
            height: selectionRect.height,
            zIndex: 5,
            willChange: 'left, top, width, height',
          }}
        />
      )}

      {/* ─── Live drag selection rectangle (DOM overlay) ─── */}
      {liveRectPx && (
        <div
          className="pointer-events-none absolute border-2 border-green-400 bg-green-400/15"
          style={{
            left: liveRectPx.x,
            top: liveRectPx.y,
            width: liveRectPx.size,
            height: liveRectPx.size,
            zIndex: 20,
          }}
        />
      )}

      {/* ─── Tile grid (DOM overlay) ─── */}
      {gridVisible && tileRects.map(({ key, row, col, rect }) => {
        const isSelected = selectedTiles.has(key)
        return (
          <div
            key={key}
            className="absolute cursor-pointer"
            style={{
              left: rect.left,
              top: rect.top,
              width: rect.width,
              height: rect.height,
              border: `${isSelected ? 2 : 1}px solid ${isSelected ? '#06b6d4' : 'rgba(102, 102, 102, 0.7)'}`,
              backgroundColor: isSelected ? 'rgba(6, 182, 212, 0.25)' : 'rgba(6, 182, 212, 0.04)',
              zIndex: 10,
              willChange: 'left, top, width, height',
            }}
            onClick={(e) => {
              e.stopPropagation()
              onToggleTileRef.current(row, col)
            }}
            title={`Tile ${row},${col}`}
          >
            <span
              className="pointer-events-none absolute left-1 top-0.5 text-[10px] font-medium select-none"
              style={{
                color: isSelected ? '#06b6d4' : 'rgba(136, 136, 136, 0.9)',
                textShadow: '0 0 3px #000, 0 0 3px #000',
              }}
            >
              {row},{col}
            </span>
          </div>
        )
      })}

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
      <div className="terrain-map-hint">Hold Shift + Drag to draw a square selection</div>
    </div>
  )
}
