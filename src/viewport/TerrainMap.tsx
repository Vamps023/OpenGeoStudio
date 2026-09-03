import { useEffect, useRef, useState } from 'react'
import { Map as MapLibreMap, NavigationControl, ScaleControl, Point, GeoJSONSource, setWorkerUrl } from 'maplibre-gl'
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
}

const TILEGRID_SOURCE = 'tilegrid-source'
const TILEGRID_FILL = 'tilegrid-fill'
const TILEGRID_OUTLINE = 'tilegrid-outline'
const TILEGRID_LABEL = 'tilegrid-label'

export default function TerrainMap({
  onBoundsSelected,
  selectedBounds,
  tileGrid,
  selectedTiles,
  onToggleTile,
  gridVisible,
}: Props) {
  const containerRef = useRef<HTMLDivElement>(null)
  const mapRef = useRef<MapLibreMap | null>(null)
  const isDraggingRef = useRef(false)
  const dragStartRef = useRef<{ x: number; y: number } | null>(null)
  const liveBoundsRef = useRef<GeoBounds | null>(null)
  // Pixel rect of the in-progress drag — rendered as a DOM overlay so the
  // selection is always visible, regardless of MapLibre source/layer timing.
  const [liveRectPx, setLiveRectPx] = useState<{ x: number; y: number; size: number } | null>(null)
  const [mapReady, setMapReady] = useState(false)
  const [mapError, setMapError] = useState<string | null>(null)
  const onBoundsSelectedRef = useRef(onBoundsSelected)
  onBoundsSelectedRef.current = onBoundsSelected

  // Keep latest tile toggle callback in a ref so the click handler doesn't need to re-bind
  const onToggleTileRef = useRef(onToggleTile)
  onToggleTileRef.current = onToggleTile

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
      maxZoom: 22,
      attributionControl: false,
    })

    map.boxZoom.disable()
    map.addControl(new NavigationControl(), 'top-right')
    map.addControl(new ScaleControl(), 'bottom-left')
    map.on('load', () => {
      setMapReady(true)
      ;(window as unknown as Record<string, unknown>).__ogsMapReady = true
    })
    map.on('error', (event) => {
      const message = (event as { error?: { message?: string } }).error?.message
      setMapError(message || 'The map failed to load')
    })
    // If the container settled its layout after mount, make sure the canvas matches
    const resizeTimer = window.setTimeout(() => {
      if (mapRef.current === map) map.resize()
    }, 150)

    // Debug handle for diagnostics (used by OGS_TEST end-to-end runs)
    ;(window as unknown as Record<string, unknown>).__ogsMap = map

    const canvas = map.getCanvas()
    const container = containerRef.current!

    // Coords helper — works regardless of pointer events captured by MapLibre
    const canvasPoint = (clientX: number, clientY: number) => {
      const r = canvas.getBoundingClientRect()
      return { x: clientX - r.left, y: clientY - r.top }
    }

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
      // Live pixel rect for the DOM overlay (drawn at the smallest enclosing square)
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

    // Bind to the container, not the canvas — MapLibre's pointer handling
    // can swallow events on the canvas itself, but the wrapper div always
    // receives them and we can still derive canvas-local coords above.
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
      map.remove()
      mapRef.current = null
    }
  }, [])

  // ─── Selection rectangle overlay ─────────────────────────────
  useEffect(() => {
    const map = mapRef.current
    if (!map) return

    function ensureSelectionLayer() {
      if (map.getSource('selection')) return
      map.addSource('selection', {
        type: 'geojson',
        data: { type: 'Feature', geometry: { type: 'Polygon', coordinates: [] }, properties: {} },
      })
      map.addLayer({
        id: 'selection',
        type: 'fill',
        source: 'selection',
        paint: { 'fill-color': '#4ade80', 'fill-opacity': 0.08 },
      })
      map.addLayer({
        id: 'selection-outline',
        type: 'line',
        source: 'selection',
        paint: { 'line-color': '#4ade80', 'line-width': 2 },
      })
    }

    function updateSelection() {
      const bounds = selectedBounds
      if (!map.loaded()) return
      try {
        ensureSelectionLayer()
      } catch {
        return
      }
      if (!bounds) {
        map.setLayoutProperty('selection', 'visibility', 'none')
        map.setLayoutProperty('selection-outline', 'visibility', 'none')
        return
      }
      map.setLayoutProperty('selection', 'visibility', 'visible')
      map.setLayoutProperty('selection-outline', 'visibility', 'visible')
      const coords = [
        [
          [bounds.west, bounds.south],
          [bounds.east, bounds.south],
          [bounds.east, bounds.north],
          [bounds.west, bounds.north],
          [bounds.west, bounds.south],
        ],
      ]
      ;(map.getSource('selection') as GeoJSONSource).setData({
        type: 'Feature',
        geometry: { type: 'Polygon', coordinates: coords },
        properties: {},
      })
    }

    if (map.loaded()) {
      updateSelection()
    } else {
      map.once('load', updateSelection)
    }
  }, [selectedBounds])

  // ─── Tile grid overlay ───────────────────────────────────────
  useEffect(() => {
    const map = mapRef.current
    if (!map) return

    function applyTileGrid() {
      if (!map.loaded()) return

      // No grid → remove layers if they exist
      if (!tileGrid) {
        try {
          if (map.getLayer(TILEGRID_FILL)) map.removeLayer(TILEGRID_FILL)
          if (map.getLayer(TILEGRID_OUTLINE)) map.removeLayer(TILEGRID_OUTLINE)
          if (map.getLayer(TILEGRID_LABEL)) map.removeLayer(TILEGRID_LABEL)
          if (map.getSource(TILEGRID_SOURCE)) map.removeSource(TILEGRID_SOURCE)
        } catch {
          // ignore
        }
        return
      }

      const features = tileGrid.tiles.map((tile) => {
        const key = `${tile.row},${tile.col}`
        const isSelected = selectedTiles.has(key)
        return {
          type: 'Feature' as const,
          geometry: {
            type: 'Polygon' as const,
            coordinates: [
              [
                [tile.bounds.west, tile.bounds.south],
                [tile.bounds.east, tile.bounds.south],
                [tile.bounds.east, tile.bounds.north],
                [tile.bounds.west, tile.bounds.north],
                [tile.bounds.west, tile.bounds.south],
              ],
            ],
          },
          properties: {
            row: tile.row,
            col: tile.col,
            selected: isSelected ? 1 : 0,
            label: `${tile.row},${tile.col}`,
          },
        }
      })

      const geojson = { type: 'FeatureCollection' as const, features }

      if (!map.getSource(TILEGRID_SOURCE)) {
        map.addSource(TILEGRID_SOURCE, { type: 'geojson', data: geojson })
        map.addLayer({
          id: TILEGRID_FILL,
          type: 'fill',
          source: TILEGRID_SOURCE,
          paint: {
            'fill-color': '#06b6d4',
            'fill-opacity': ['case', ['==', ['get', 'selected'], 1], 0.25, 0.03],
          },
        })
        map.addLayer({
          id: TILEGRID_OUTLINE,
          type: 'line',
          source: TILEGRID_SOURCE,
          paint: {
            'line-color': ['case', ['==', ['get', 'selected'], 1], '#06b6d4', '#666'],
            'line-width': ['case', ['==', ['get', 'selected'], 1], 2, 1],
            'line-dasharray': [4, 2],
          },
        })
        map.addLayer({
          id: TILEGRID_LABEL,
          type: 'symbol',
          source: TILEGRID_SOURCE,
          layout: {
            'text-field': ['get', 'label'],
            'text-size': 11,
            'text-anchor': 'center',
          },
          paint: {
            'text-color': ['case', ['==', ['get', 'selected'], 1], '#06b6d4', '#888'],
            'text-halo-color': '#000',
            'text-halo-width': 1,
          },
        })

        // Click to toggle tile selection
        const handleClick = (e: { features?: Array<{ properties?: Record<string, unknown> }> }) => {
          if (!e.features || e.features.length === 0) return
          const props = e.features[0].properties
          if (!props) return
          const row = props.row as number
          const col = props.col as number
          onToggleTileRef.current(row, col)
        }
        const handleMouseEnter = () => {
          map.getCanvas().style.cursor = 'pointer'
        }
        const handleMouseLeave = () => {
          map.getCanvas().style.cursor = ''
        }
        map.on('click', TILEGRID_FILL, handleClick)
        map.on('mouseenter', TILEGRID_FILL, handleMouseEnter)
        map.on('mouseleave', TILEGRID_FILL, handleMouseLeave)
      } else {
        ;(map.getSource(TILEGRID_SOURCE) as GeoJSONSource).setData(geojson)
      }
    }

    if (map.loaded()) {
      applyTileGrid()
    } else {
      map.once('load', applyTileGrid)
    }
  }, [tileGrid, selectedTiles])

  // ─── Tile grid visibility toggle ─────────────────────────────
  useEffect(() => {
    const map = mapRef.current
    if (!map) return
    const visibility = gridVisible ? 'visible' : 'none'
    function applyVis() {
      try {
        if (map.getLayer(TILEGRID_FILL)) map.setLayoutProperty(TILEGRID_FILL, 'visibility', visibility)
        if (map.getLayer(TILEGRID_OUTLINE)) map.setLayoutProperty(TILEGRID_OUTLINE, 'visibility', visibility)
        if (map.getLayer(TILEGRID_LABEL)) map.setLayoutProperty(TILEGRID_LABEL, 'visibility', visibility)
      } catch {
        // ignore
      }
    }
    if (map.loaded()) {
      applyVis()
    } else {
      map.once('load', applyVis)
    }
  }, [gridVisible])

  return (
    <div className="terrain-map-container">
      <div ref={containerRef} className="terrain-map-canvas" />
      {/* Live drag selection rectangle (DOM overlay) */}
      {liveRectPx && (
        <div
          className="pointer-events-none absolute z-20 border-2 border-green-400 bg-green-400/15"
          style={{
            left: liveRectPx.x,
            top: liveRectPx.y,
            width: liveRectPx.size,
            height: liveRectPx.size,
          }}
        />
      )}
      {!mapReady && !mapError && (
        <div className="pointer-events-none absolute left-1/2 top-4 z-10 -translate-x-1/2 rounded-full border border-border bg-card/85 px-4 py-1.5 text-xs font-medium text-muted-foreground shadow-lg backdrop-blur">
          Loading map…
        </div>
      )}
      {mapError && (
        <div className="absolute left-1/2 top-4 z-10 -translate-x-1/2 rounded-md border border-destructive/40 bg-destructive/10 px-3 py-2 text-xs text-destructive shadow-lg backdrop-blur">
          Map error: {mapError}
        </div>
      )}
      <div className="terrain-map-hint">Hold Shift + Drag to draw a square selection</div>
    </div>
  )
}
