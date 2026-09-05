import { useEffect, useMemo, useState, type FormEvent } from 'react'
import { Building2, Download, Eraser, Globe, Search, Trash2 } from 'lucide-react'
import { toast } from 'sonner'
import {
  parseOverpassBuildings,
  toBuildingData,
  overpassQueryPolygon,
  polygonBounds,
  pointInPolygon,
  ringCentroidLatLng,
  type LatLngRing,
} from '../engine/osmBuildings'
import { useStore } from '../state/store'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Separator } from '@/components/ui/separator'
import OsmBuilderMap from './OsmBuilderMap'

/**
 * Dedicated OSM Builder workspace (issue #49): an interactive map where the
 * user draws a polygon area of interest, then downloads building footprints
 * from OpenStreetMap inside that polygon. Imported buildings are stored on
 * the project and rendered in the 3D Studio.
 *
 * The Overpass request runs through the Electron main process (IPC) to avoid
 * the browser CORS restriction that blocked the renderer-side fetch (issue #46).
 * Building data © OpenStreetMap contributors (ODbL 1.0).
 */
export default function OsmBuilderWorkspace() {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const setGeoRef = useStore((s) => s.setGeoRef)
  const setOsmBuildings = useStore((s) => s.setOsmBuildings)
  const deleteOsmBuilding = useStore((s) => s.deleteOsmBuilding)
  const clearOsmBuildings = useStore((s) => s.clearOsmBuildings)
  const project = projects.find((p) => p.id === activeProjectId)

  // ── Polygon drawing state ──
  const [polygon, setPolygon] = useState<LatLngRing[]>([])
  const [osmStatus, setOsmStatus] = useState<'idle' | 'fetching'>('idle')
  const [osmError, setOsmError] = useState<string | null>(null)
  const osmBuildings = project?.osmBuildings ?? []

  // ── Location search ──
  const [searchText, setSearchText] = useState('')
  const [searching, setSearching] = useState(false)
  const [flyTo, setFlyTo] = useState<{ lng: number; lat: number; zoom?: number } | null>(null)

  const isClosed = polygon.length >= 3
  const polyBounds = useMemo(() => (polygon.length ? polygonBounds(polygon) : null), [polygon])

  /** terrain working-area bounds (from the Terrain workspace) shown as a
   *  reference rectangle so the user can see where their terrain selection is
   *  and draw a polygon around it. */
  const terrainBounds = project?.workingArea?.bounds ?? null

  /** Auto-focus the map on the terrain working area when the workspace opens
   *  (or when the project changes). Falls back to the geoRef origin, then to
   *  the default map location. Runs once per project — the user's manual pan/
   *  zoom after that is preserved. */
  useEffect(() => {
    if (terrainBounds) {
      const lng = (terrainBounds.west + terrainBounds.east) / 2
      const lat = (terrainBounds.south + terrainBounds.north) / 2
      setFlyTo({ lng, lat, zoom: 14 })
    } else if (project?.geoRef) {
      setFlyTo({ lng: project.geoRef.lng, lat: project.geoRef.lat, zoom: 14 })
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [project?.id])

  /** imported buildings as lng/lat rings for the map overlay */
  const mapBuildings = useMemo(() => {
    const geoRef = project?.geoRef
    if (!geoRef || osmBuildings.length === 0) return []
    const latRad = (geoRef.lat * Math.PI) / 180
    const metersPerDegLat = 111320
    const metersPerDegLng = 111320 * Math.cos(latRad)
    return osmBuildings.map((building) => ({
      id: building.id,
      ring: building.ring.map((point) => [
        geoRef.lng + (point.x * geoRef.scale) / metersPerDegLng,
        geoRef.lat + (point.y * geoRef.scale) / metersPerDegLat,
      ] as [number, number]),
    }))
  }, [osmBuildings, project?.geoRef])

  async function handleLocationSearch(event: FormEvent) {
    event.preventDefault()
    const query = searchText.trim()
    if (!query) return
    setSearching(true)
    try {
      const response = await fetch(`https://nominatim.openstreetmap.org/search?format=json&limit=1&q=${encodeURIComponent(query)}`)
      const results = (await response.json()) as { lat: string; lon: string; display_name: string }[]
      if (!results.length) {
        toast.error('Location not found', { description: query })
        return
      }
      const lat = Number.parseFloat(results[0].lat)
      const lng = Number.parseFloat(results[0].lon)
      setFlyTo({ lng, lat, zoom: 15 })
      toast.success('Location found', { description: results[0].display_name })
    } catch {
      toast.error('Location search failed — check the connection, or draw the polygon directly on the map.')
    } finally {
      setSearching(false)
    }
  }

  /** Ensure the project has a geo reference centered on the polygon centroid. */
  function ensureGeoRefForPolygon(ring: LatLngRing[]) {
    if (project?.geoRef) return project.geoRef
    const c = ringCentroidLatLng(ring)
    const geoRef = { lng: c.lng, lat: c.lat, scale: 1 }
    setGeoRef(geoRef)
    return geoRef
  }

  /** Fetch OSM buildings inside the drawn polygon via the main-process IPC
   *  bridge (avoids CORS). Results are filtered to buildings whose centroid
   *  is inside the polygon, then projected into the project world frame. */
  async function handleImport() {
    if (!isClosed) {
      setOsmError('Draw a polygon with at least 3 vertices first.')
      return
    }
    const currentProject = project
    if (!currentProject) {
      setOsmError('No active project.')
      return
    }
    const geoRef = ensureGeoRefForPolygon(polygon)
    setOsmStatus('fetching')
    setOsmError(null)
    try {
      const query = overpassQueryPolygon(polygon)
      let json: unknown
      if (window.ogs?.fetchOsmBuildings) {
        const result = await window.ogs.fetchOsmBuildings(query)
        if (!result.success) throw new Error(result.error || 'Overpass request failed')
        json = result.data
      } else {
        // dev/browser fallback (no CORS bypass)
        const response = await fetch('https://overpass-api.de/api/interpreter', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'data=' + encodeURIComponent(query),
        })
        if (!response.ok) throw new Error(`Overpass API returned ${response.status}`)
        json = await response.json()
      }
      const raw = parseOverpassBuildings(json)
      // Filter to buildings whose centroid is inside the drawn polygon
      const inside = raw.filter((b) => {
        let lng = 0
        let lat = 0
        for (const p of b.ring) { lng += p.lng; lat += p.lat }
        lng /= b.ring.length
        lat /= b.ring.length
        return pointInPolygon(lng, lat, polygon)
      })
      const projected = inside.map((building) => toBuildingData(building, geoRef))
      // area-sync merge: drop existing buildings whose centroid is inside the
      // drawn polygon (precise point-in-polygon test, not just bounding box),
      // keep buildings outside the polygon untouched.
      const bounds = polyBounds!
      const latRad = (geoRef.lat * Math.PI) / 180
      const metersPerDegLat = 111320
      const metersPerDegLng = 111320 * Math.cos(latRad)
      const inPolygon = (building: typeof projected[number]) => {
        const ring = building.ring
        let cx = 0
        let cy = 0
        for (const p of ring) { cx += p.x; cy += p.y }
        cx /= ring.length
        cy /= ring.length
        const lng = geoRef.lng + (cx * geoRef.scale) / metersPerDegLng
        const lat = geoRef.lat + (cy * geoRef.scale) / metersPerDegLat
        return pointInPolygon(lng, lat, polygon)
      }
      const kept = (currentProject.osmBuildings ?? []).filter((b) => !inPolygon(b))
      const merged = [...kept, ...projected]
      setOsmBuildings(merged, { area: bounds, fetchedAt: new Date().toISOString(), total: merged.length })
      setOsmStatus('idle')
      const replaced = (currentProject.osmBuildings ?? []).length - kept.length
      if (projected.length === 0) {
        toast.info('No OSM buildings found in this polygon', {
          description: replaced > 0 ? `Removed ${replaced} old building${replaced === 1 ? '' : 's'} from this area` : undefined,
        })
      } else {
        toast.success(`Imported ${projected.length} building${projected.length === 1 ? '' : 's'}`, {
          description: `${merged.length} total in project${replaced > 0 ? ` · replaced ${replaced} old` : ''} · © OpenStreetMap contributors`,
        })
      }
    } catch (err) {
      setOsmStatus('idle')
      setOsmError((err as Error).message || 'OSM download failed')
      toast.error('OSM import failed', { description: (err as Error).message })
    }
  }

  function handleClearPolygon() {
    setPolygon([])
    setOsmError(null)
  }

  function handleUndoVertex() {
    setPolygon((ring) => ring.slice(0, -1))
  }

  return (
    <div className="flex h-full min-h-0">
      {/* ─── Map area ─── */}
      <div className="relative min-w-0 flex-1">
        <OsmBuilderMap
          polygon={polygon}
          onPolygonChange={setPolygon}
          buildings={mapBuildings}
          flyTo={flyTo}
          drawingDisabled={osmStatus === 'fetching'}
          terrainBounds={terrainBounds}
        />
      </div>

      {/* ─── Sidebar ─── */}
      <ScrollArea className="w-80 shrink-0 border-l border-border bg-card/60">
        <aside className="grid content-start gap-4 p-4">
          {/* ─── Location search ─── */}
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <Globe className="size-3.5" />
              Find Location
            </h3>
            <form className="grid gap-2" onSubmit={handleLocationSearch}>
              <div className="flex gap-2">
                <Input
                  value={searchText}
                  onChange={(e) => setSearchText(e.target.value)}
                  placeholder="Search a place…"
                  className="h-8 text-xs"
                />
                <Button type="submit" size="sm" variant="secondary" className="h-8 shrink-0 px-2" disabled={searching}>
                  <Search className="size-3.5" />
                </Button>
              </div>
            </form>
          </section>

          <Separator />

          {/* ─── Polygon drawing ─── */}
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <Building2 className="size-3.5" />
              Area of Interest
            </h3>
            <p className="text-[11px] leading-snug text-muted-foreground">
              Click on the map to add polygon vertices. Double-click to finish. Drag vertices to edit.
              Buildings will be downloaded only inside the drawn polygon.
            </p>
            <div className="flex flex-wrap items-center gap-1.5">
              <Button
                type="button"
                size="sm"
                variant="outline"
                className="h-7 text-xs"
                onClick={handleUndoVertex}
                disabled={polygon.length === 0 || osmStatus === 'fetching'}
              >
                Undo vertex
              </Button>
              <Button
                type="button"
                size="sm"
                variant="ghost"
                className="h-7 text-xs"
                onClick={handleClearPolygon}
                disabled={polygon.length === 0 || osmStatus === 'fetching'}
              >
                <Eraser className="size-3" />
                Clear
              </Button>
              {terrainBounds && (
                <Button
                  type="button"
                  size="sm"
                  variant="outline"
                  className="h-7 text-xs"
                  onClick={() =>
                    setPolygon([
                      { lat: terrainBounds.north, lng: terrainBounds.west },
                      { lat: terrainBounds.north, lng: terrainBounds.east },
                      { lat: terrainBounds.south, lng: terrainBounds.east },
                      { lat: terrainBounds.south, lng: terrainBounds.west },
                    ])
                  }
                  disabled={osmStatus === 'fetching'}
                  title="Set the polygon to the terrain working-area rectangle"
                >
                  Use terrain area
                </Button>
              )}
            </div>
            {polygon.length > 0 && (
              <Badge variant="muted" className="w-fit">
                {polygon.length} vertex{polygon.length === 1 ? '' : 's'}{isClosed ? ' · closed' : ''}
              </Badge>
            )}
            {polyBounds && (
              <p className="text-[10px] leading-snug text-muted-foreground tabular-nums">
                Bounds: {polyBounds.west.toFixed(4)}, {polyBounds.south.toFixed(4)} → {polyBounds.east.toFixed(4)}, {polyBounds.north.toFixed(4)}
              </p>
            )}
          </section>

          <Separator />

          {/* ─── Import ─── */}
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <Download className="size-3.5" />
              Import Buildings
            </h3>
            <Button
              type="button"
              size="sm"
              variant="secondary"
              className="h-8 text-xs"
              disabled={!isClosed || osmStatus !== 'idle'}
              onClick={() => void handleImport()}
              title={isClosed ? 'Fetch buildings inside the polygon' : 'Draw a closed polygon first'}
            >
              {osmStatus === 'fetching' ? 'Downloading…' : 'Import Buildings'}
            </Button>
            {osmError && (
              <p className="text-[11px] text-destructive">{osmError}</p>
            )}
            {osmBuildings.length > 0 && (
              <>
                <div className="flex items-center gap-2">
                  <Badge variant="muted" className="w-fit">
                    {osmBuildings.length} building{osmBuildings.length === 1 ? '' : 's'} in project
                  </Badge>
                  <Button
                    type="button"
                    size="sm"
                    variant="ghost"
                    className="h-7 px-2 text-xs text-destructive hover:text-destructive"
                    onClick={() => {
                      clearOsmBuildings()
                      toast.success('Cleared all OSM buildings')
                    }}
                    disabled={osmStatus === 'fetching'}
                    title="Remove all imported buildings from the project"
                  >
                    <Trash2 className="size-3" />
                    Clear all
                  </Button>
                </div>
                {project?.osmImport && (
                  <p className="text-[10px] leading-snug text-muted-foreground tabular-nums">
                    Last import: {new Date(project.osmImport.fetchedAt).toLocaleString()}
                  </p>
                )}
                <div className="grid max-h-60 gap-0.5 overflow-y-auto">
                  {osmBuildings.slice(0, 200).map((building) => (
                    <div
                      key={building.id}
                      className="group flex items-baseline justify-between gap-2 rounded px-1.5 py-0.5 text-[11px] hover:bg-muted/60"
                    >
                      <span className="min-w-0 truncate text-foreground" title={`${building.name} (${building.id})`}>
                        {building.name}
                      </span>
                      <span className="shrink-0 tabular-nums text-muted-foreground">
                        {Math.round(building.height)}m{building.levels ? ` · ${building.levels}fl` : ''}
                      </span>
                      <button
                        type="button"
                        className="shrink-0 text-muted-foreground opacity-0 transition-opacity hover:text-destructive group-hover:opacity-100"
                        onClick={() => deleteOsmBuilding(building.id)}
                        title="Remove building"
                        aria-label={`Remove ${building.name}`}
                      >
                        <Trash2 className="size-3" />
                      </button>
                    </div>
                  ))}
                  {osmBuildings.length > 200 && (
                    <p className="px-1.5 pt-1 text-[11px] text-muted-foreground">+ {osmBuildings.length - 200} more…</p>
                  )}
                </div>
                <p className="text-[10px] leading-snug text-muted-foreground">
                  Building data © OpenStreetMap contributors (ODbL 1.0). Re-importing the same area replaces buildings inside the polygon; buildings outside are kept. Use "Clear all" to start fresh.
                </p>
              </>
            )}
          </section>

          <Separator />

          {/* ─── Geo reference + terrain area info ─── */}
          <section className="grid gap-2">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              Geo Reference
            </h3>
            {project?.geoRef ? (
              <p className="text-[10px] leading-snug text-muted-foreground tabular-nums">
                Origin: {project.geoRef.lng.toFixed(5)}, {project.geoRef.lat.toFixed(5)} · scale {project.geoRef.scale}
              </p>
            ) : (
              <p className="text-[10px] leading-snug text-muted-foreground">
                No geo reference set. One will be created at the polygon centroid on import.
              </p>
            )}
            {terrainBounds ? (
              <p className="text-[10px] leading-snug text-cyan-300/80 tabular-nums">
                Terrain area: {terrainBounds.west.toFixed(4)}, {terrainBounds.south.toFixed(4)} → {terrainBounds.east.toFixed(4)}, {terrainBounds.north.toFixed(4)}
              </p>
            ) : (
              <p className="text-[10px] leading-snug text-muted-foreground">
                No terrain area set. Use the Terrain workspace to select one.
              </p>
            )}
          </section>
        </aside>
      </ScrollArea>
    </div>
  )
}
