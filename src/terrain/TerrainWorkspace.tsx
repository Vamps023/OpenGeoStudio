import { useEffect, useMemo, useState } from 'react'
import { Download, Grid3x3, Globe, Image as ImageIcon, Layers, Mountain } from 'lucide-react'
import { toast } from 'sonner'
import { resolveCRS } from '../engine/crs'
import type { GeoBounds } from '../engine/crs'
import { buildTerrainMesh } from '../engine/terrainMesh'
import type { TerrainData } from '../engine/terrainMesh'
import { setActiveTerrain } from './terrainRegistry'
import { computeTileGrid, tileKey } from '../engine/tileGrid'
import type { TileGrid } from '../engine/tileGrid'
import { useStore } from '../state/store'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'
import TerrainMap from '../viewport/TerrainMap'
import TerrainViewport from '../viewport/TerrainViewport'

type DEMProvider =
  | 'aws-terrarium'
  | 'mapbox-terrain-rgb'
  | 'opentopo-srtmgl1'
  | 'opentopo-srtmgl3'
  | 'opentopo-aw3d30'
  | 'opentopo-cop30'
  | 'opentopo-nasadem'
  | 'opentopo-usgs10m'
  | 'nasa-earthdata'
  | 'gpxz'

type ImagerySource = 'arcgis' | 'google' | 'mapbox' | 'maptiler'
type HeightmapFormat = 'png' | 'r16' | 'geotiff' | 'dem' | 'float32' | 'none'
type AlbedoFormat = 'png' | 'geotiff' | 'none'

const TILE_SIZES = [1, 2, 4, 8, 16] as const

const DEM_PROVIDERS: { value: DEMProvider; label: string; needsKey: boolean; keyLabel?: string; keyHint?: string }[] = [
  { value: 'aws-terrarium', label: 'AWS Terrarium (Free)', needsKey: false },
  { value: 'nasa-earthdata', label: 'NASA Copernicus DEM 30m (Free)', needsKey: false },
  { value: 'opentopo-srtmgl1', label: 'OpenTopo SRTM GL1 30m', needsKey: true, keyLabel: 'OpenTopography API Key', keyHint: 'https://portal.opentopography.org/myopentopo' },
  { value: 'opentopo-srtmgl3', label: 'OpenTopo SRTM GL3 90m', needsKey: true, keyLabel: 'OpenTopography API Key' },
  { value: 'opentopo-aw3d30', label: 'OpenTopo ALOS World 3D 30m', needsKey: true, keyLabel: 'OpenTopography API Key' },
  { value: 'opentopo-cop30', label: 'OpenTopo Copernicus GLO-30', needsKey: true, keyLabel: 'OpenTopography API Key' },
  { value: 'opentopo-nasadem', label: 'OpenTopo NASADEM', needsKey: true, keyLabel: 'OpenTopography API Key' },
  { value: 'opentopo-usgs10m', label: 'OpenTopo USGS 3DEP 10m', needsKey: true, keyLabel: 'OpenTopography API Key' },
  { value: 'gpxz', label: 'GPXZ (High-res 5m)', needsKey: true, keyLabel: 'GPXZ API Key', keyHint: 'https://www.gpxz.io/' },
  { value: 'mapbox-terrain-rgb', label: 'Mapbox Terrain RGB', needsKey: true, keyLabel: 'Mapbox Access Token', keyHint: 'https://account.mapbox.com/' },
]

const IMAGERY_SOURCES: { value: ImagerySource; label: string; needsKey: boolean; keyLabel?: string; keyHint?: string }[] = [
  { value: 'arcgis', label: 'Esri World Imagery (Free)', needsKey: false },
  { value: 'google', label: 'Google Satellite (Free)', needsKey: false },
  { value: 'mapbox', label: 'Mapbox Satellite', needsKey: true, keyLabel: 'Mapbox Access Token', keyHint: 'https://account.mapbox.com/' },
  { value: 'maptiler', label: 'MapTiler Satellite', needsKey: true, keyLabel: 'MapTiler API Key', keyHint: 'https://www.maptiler.com/' },
]

const HEIGHTMAP_FORMATS: { value: HeightmapFormat; label: string }[] = [
  { value: 'geotiff', label: 'GeoTIFF UInt16 (normalized)' },
  { value: 'dem', label: 'GeoTIFF Int16 (meters)' },
  { value: 'float32', label: 'GeoTIFF Float32 (meters)' },
  { value: 'png', label: 'PNG 16-bit grayscale' },
  { value: 'r16', label: 'R16 Raw 16-bit' },
  { value: 'none', label: 'No heightmap' },
]

const ALBEDO_FORMATS: { value: AlbedoFormat; label: string }[] = [
  { value: 'png', label: 'PNG RGB' },
  { value: 'geotiff', label: 'GeoTIFF RGB' },
  { value: 'none', label: 'No albedo' },
]

/**
 * Full terrain workspace body (map area + settings sidebar): download DEM,
 * tile grid, export. Hosted by both the standalone Terrain page and the
 * Editor's Terrain section — hosts provide their own header.
 */
export default function TerrainWorkspace() {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const project = projects.find((p) => p.id === activeProjectId)

  const [selectedBounds, setSelectedBounds] = useState<GeoBounds | null>(null)
  const [terrain, setTerrain] = useState<TerrainData | null>(null)
  const [loading, setLoading] = useState(false)
  const [exporting, setExporting] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [view, setView] = useState<'map' | '3d'>('map')

  // Tile grid state
  const [tileSizeKm, setTileSizeKm] = useState<number>(4)
  const [tileGrid, setTileGrid] = useState<TileGrid | null>(null)
  const [selectedTiles, setSelectedTiles] = useState<Set<string>>(new Set())
  const [gridVisible, setGridVisible] = useState(true)

  // DEM settings
  const [demProvider, setDemProvider] = useState<DEMProvider>('aws-terrarium')
  const [demApiKey, setDemApiKey] = useState('')
  // Imagery settings
  const [imagerySource, setImagerySource] = useState<ImagerySource>('arcgis')
  const [imageryApiKey, setImageryApiKey] = useState('')
  // Export settings
  const [heightmapFormat, setHeightmapFormat] = useState<HeightmapFormat>('geotiff')
  const [albedoFormat, setAlbedoFormat] = useState<AlbedoFormat>('png')
  const [heightmapSize, setHeightmapSize] = useState(1024)
  const [albedoSize, setAlbedoSize] = useState(1024)
  const [crsSource, setCrsSource] = useState('auto')
  const [compression, setCompression] = useState<'none' | 'deflate'>('deflate')
  // Preview settings
  const [heightScale, setHeightScale] = useState(1)
  // Progress
  const [progress, setProgress] = useState<string | null>(null)

  const terrainMesh = useMemo(
    () => (terrain ? buildTerrainMesh(terrain, 400, 1) : null),
    [terrain],
  )

  const demInfo = DEM_PROVIDERS.find((p) => p.value === demProvider)!
  const imageryInfo = IMAGERY_SOURCES.find((p) => p.value === imagerySource)!

  // Recompute tile grid when bounds or tile size changes
  useEffect(() => {
    if (!selectedBounds) {
      setTileGrid(null)
      setSelectedTiles(new Set())
      return
    }
    const grid = computeTileGrid(selectedBounds, tileSizeKm)
    setTileGrid(grid)
    // Auto-select all tiles when grid is recomputed
    setSelectedTiles(new Set(grid.tiles.map((t) => tileKey(t.row, t.col))))
  }, [selectedBounds, tileSizeKm])

  function toggleTile(row: number, col: number) {
    setSelectedTiles((prev) => {
      const next = new Set(prev)
      const key = tileKey(row, col)
      if (next.has(key)) next.delete(key)
      else next.add(key)
      return next
    })
  }

  function selectAllTiles() {
    if (!tileGrid) return
    setSelectedTiles(new Set(tileGrid.tiles.map((t) => tileKey(t.row, t.col))))
  }

  function deselectAllTiles() {
    setSelectedTiles(new Set())
  }

  // Subscribe to export progress
  useEffect(() => {
    if (!window.ogs?.onExportProgress) return
    const unsub = window.ogs.onExportProgress((p) => setProgress(p.message))
    return () => unsub()
  }, [])

  async function handleDownload() {
    if (!window.ogs) {
      setError('Electron bridge not available. Run via npm start.')
      return
    }
    if (!selectedBounds) {
      setError('Draw a square on the map first (Shift + Drag).')
      return
    }
    setLoading(true)
    setError(null)
    setProgress('Starting download...')
    try {
      const result = await window.ogs.downloadTerrain(selectedBounds, {
        provider: demProvider,
        apiKey: demApiKey,
        targetSize: heightmapSize,
        onProgress: (p) => setProgress(p.message),
      })
      if (!result.success) {
        setError(result.error || 'Download failed')
      } else {
        const data = result.data
        const terrainData = {
          elevations: data.elevations,
          width: data.width,
          height: data.height,
          bounds: data.bounds,
          minElevation: data.minElevation,
          maxElevation: data.maxElevation,
        }
        setTerrain(terrainData)
        // Register for cross-page sampling (Stick to Background Terrain)
        setActiveTerrain(terrainData)
        setProgress(null)
        setView('3d')
      }
    } catch (err) {
      setError((err as Error).message)
    } finally {
      setLoading(false)
    }
  }

  async function handleExport() {
    if (!window.ogs) {
      setError('Electron bridge not available. Run via npm start.')
      return
    }
    if (!selectedBounds) {
      setError('Draw a square on the map first (Shift + Drag).')
      return
    }
    if (selectedTiles.size === 0) {
      setError('Select at least one tile to export.')
      return
    }
    setExporting(true)
    setError(null)
    setProgress('Starting export...')
    try {
      // Collect selected tile bounds
      const tilesToExport = tileGrid
        ? tileGrid.tiles.filter((t) => selectedTiles.has(tileKey(t.row, t.col)))
        : [{ row: 0, col: 0, bounds: selectedBounds, center: { lng: 0, lat: 0 }, selected: true }]

      const result = await window.ogs.exportTerrain({
        bounds: selectedBounds,
        projectId: project?.id,
        projectName: project?.name,
        demSource: demProvider,
        imagerySource,
        heightmapFormat,
        albedoFormat,
        heightmapSize,
        albedoSize,
        demApiKey,
        imageryApiKey,
        crsSource,
        compression,
        downloadDem: heightmapFormat !== 'none',
        downloadImagery: albedoFormat !== 'none',
        tiles: tilesToExport.map((t) => ({ row: t.row, col: t.col, bounds: t.bounds })),
      })
      if (!result.success) {
        setError(result.error || 'Export failed')
        toast.error('Export failed', { description: result.error || undefined })
      } else {
        const manifestPath = result.manifestPath || ''
        const fileList = Object.values(result.files || {}).filter(Boolean)
        setProgress(`Export complete → ${manifestPath}`)
        toast.success('Export complete', {
          description: manifestPath || (fileList.length > 0 ? fileList.join(' · ') : undefined),
        })
      }
    } catch (err) {
      setError((err as Error).message)
    } finally {
      setExporting(false)
    }
  }

  async function handleSaveGeoTIFF() {
    if (!window.ogs || !terrain) return
    setError(null)
    const crs = resolveCRS(crsSource, terrain.bounds)
    const result = await window.ogs.saveGeoTIFF(
      { elevations: terrain.elevations },
      {
        width: terrain.width,
        height: terrain.height,
        bitsPerSample: 32,
        sampleFormat: 3,
        samplesPerPixel: 1,
        photometricInterpretation: 1,
        bounds: terrain.bounds,
        rasterType: 'point',
        crs,
        filename: 'terrain.tif',
      },
    )
    if (!result.success && result.error !== 'Cancelled') {
      setError(result.error)
    }
  }

  if (!project) return null

  const selectedCount = selectedTiles.size
  const totalTiles = tileGrid?.tiles.length ?? 0

  return (
    <div className="flex h-full min-h-0 flex-col bg-background">
      {/* View toggle toolbar */}
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-border bg-card/50 px-3">
        <div className="flex items-center rounded-md border border-border bg-background p-0.5">
          <Button size="sm" variant={view === 'map' ? 'default' : 'ghost'} className="h-6 px-3 text-xs" onClick={() => setView('map')}>
            Map
          </Button>
          <Button size="sm" variant={view === '3d' ? 'default' : 'ghost'} className="h-6 px-3 text-xs" onClick={() => setView('3d')} disabled={!terrain}>
            3D
          </Button>
        </div>
        <span className="ml-auto text-[11px] text-muted-foreground">Shift + Drag on the map to select the terrain area</span>
      </div>

      <div className="flex min-h-0 flex-1">
        <div className="relative flex min-w-0 flex-1">
        {view === 'map' ? (
          <TerrainMap
            onBoundsSelected={setSelectedBounds}
            selectedBounds={selectedBounds}
            tileGrid={tileGrid}
            selectedTiles={selectedTiles}
            onToggleTile={toggleTile}
            gridVisible={gridVisible}
          />
        ) : (
          <TerrainViewport terrainMesh={terrainMesh} heightScale={heightScale} />
        )}
      </div>

        <ScrollArea className="w-80 shrink-0 border-l border-border bg-card/60">
          <aside className="grid content-start gap-4 p-4">
          {/* ─── Tile Grid ──────────────────────────────────── */}
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <Grid3x3 className="size-3.5" />
              Tile Grid
            </h3>
            <div className="flex flex-wrap items-center gap-1.5">
              {TILE_SIZES.map((size) => (
                <Button
                  key={size}
                  type="button"
                  size="sm"
                  variant={tileSizeKm === size ? 'default' : 'outline'}
                  className="h-7 px-2.5 text-xs"
                  onClick={() => setTileSizeKm(size)}
                  title={`${size}km tiles`}
                >
                  {size}km
                </Button>
              ))}
              <Button
                type="button"
                size="sm"
                variant={gridVisible ? 'default' : 'outline'}
                className="ml-auto h-7 px-2.5 text-xs"
                onClick={() => setGridVisible((v) => !v)}
                title="Toggle grid visibility"
              >
                Grid
              </Button>
            </div>

            {tileGrid && (
              <div className="flex items-center gap-2">
                <Button type="button" size="sm" variant="secondary" className="h-7 text-xs" onClick={selectAllTiles}>
                  Select All
                </Button>
                <Button type="button" size="sm" variant="ghost" className="h-7 text-xs" onClick={deselectAllTiles}>
                  Clear
                </Button>
                <Badge variant="muted" className="ml-auto">
                  {selectedCount}/{totalTiles} tiles
                </Badge>
              </div>
            )}
          </section>

          {/* ─── DEM Source ─────────────────────────────────── */}
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <Mountain className="size-3.5" />
              DEM Source (Heightmap)
            </h3>
            <div className="grid gap-1.5">
              <Label>Provider</Label>
              <Select
                value={demProvider}
                onValueChange={(value) => {
                  setDemProvider(value as DEMProvider)
                  setDemApiKey('')
                }}
              >
                <SelectTrigger>
                  <SelectValue placeholder="Select DEM provider" />
                </SelectTrigger>
                <SelectContent>
                  {DEM_PROVIDERS.map((p) => (
                    <SelectItem key={p.value} value={p.value}>
                      {p.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
            {demInfo.needsKey && (
              <div className="grid gap-1.5">
                <Label>{demInfo.keyLabel}</Label>
                <Input
                  type="password"
                  value={demApiKey}
                  onChange={(e) => setDemApiKey(e.target.value)}
                  placeholder={demInfo.keyHint || 'Enter API key'}
                />
              </div>
            )}
          </section>

          {/* ─── Imagery Source ─────────────────────────────── */}
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <ImageIcon className="size-3.5" />
              Imagery Source (Albedo)
            </h3>
            <div className="grid gap-1.5">
              <Label>Provider</Label>
              <Select
                value={imagerySource}
                onValueChange={(value) => {
                  setImagerySource(value as ImagerySource)
                  setImageryApiKey('')
                }}
              >
                <SelectTrigger>
                  <SelectValue placeholder="Select imagery provider" />
                </SelectTrigger>
                <SelectContent>
                  {IMAGERY_SOURCES.map((p) => (
                    <SelectItem key={p.value} value={p.value}>
                      {p.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
            {imageryInfo.needsKey && (
              <div className="grid gap-1.5">
                <Label>{imageryInfo.keyLabel}</Label>
                <Input
                  type="password"
                  value={imageryApiKey}
                  onChange={(e) => setImageryApiKey(e.target.value)}
                  placeholder={imageryInfo.keyHint || 'Enter API key'}
                />
              </div>
            )}
          </section>

          {selectedBounds && (
            <div className="grid gap-1.5 rounded-lg border border-border bg-muted/40 p-3 text-xs">
              <div className="flex items-center justify-between">
                <span className="text-muted-foreground">West</span>
                <b className="font-medium">{selectedBounds.west.toFixed(4)}</b>
              </div>
              <div className="flex items-center justify-between">
                <span className="text-muted-foreground">East</span>
                <b className="font-medium">{selectedBounds.east.toFixed(4)}</b>
              </div>
              <div className="flex items-center justify-between">
                <span className="text-muted-foreground">South</span>
                <b className="font-medium">{selectedBounds.south.toFixed(4)}</b>
              </div>
              <div className="flex items-center justify-between">
                <span className="text-muted-foreground">North</span>
                <b className="font-medium">{selectedBounds.north.toFixed(4)}</b>
              </div>
            </div>
          )}

          <div className="grid gap-2">
            <Button type="button" onClick={handleDownload} disabled={loading || !selectedBounds}>
              <Globe className="size-4" />
              {loading ? 'Downloading...' : 'Preview DEM (3D)'}
            </Button>

            {progress && (
              <p className="rounded-md border border-border bg-muted/40 px-3 py-2 text-xs text-muted-foreground">
                {progress}
              </p>
            )}
            {error && (
              <p className="rounded-md border border-destructive/40 bg-destructive/10 px-3 py-2 text-xs text-destructive">
                {error}
              </p>
            )}
          </div>

          {terrain && (
            <section className="grid gap-3">
              <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
                <Layers className="size-3.5" />
                3D Preview
              </h3>
              <div className="grid gap-1.5">
                <Label htmlFor="height-scale">Height scale</Label>
                <Input
                  id="height-scale"
                  type="number"
                  min={0.1}
                  max={10}
                  step={0.1}
                  value={heightScale}
                  onChange={(e) => setHeightScale(parseFloat(e.target.value) || 1)}
                />
              </div>

              <div className="grid gap-1.5 rounded-lg border border-border bg-muted/40 p-3 text-xs">
                <div className="flex items-center justify-between">
                  <span className="text-muted-foreground">Size</span>
                  <b className="font-medium">{terrain.width} × {terrain.height}</b>
                </div>
                <div className="flex items-center justify-between">
                  <span className="text-muted-foreground">Elevation</span>
                  <b className="font-medium">{terrain.minElevation.toFixed(1)} – {terrain.maxElevation.toFixed(1)} m</b>
                </div>
              </div>

              <Button type="button" variant="outline" onClick={handleSaveGeoTIFF} disabled={loading}>
                Save DEM as GeoTIFF
              </Button>
            </section>
          )}

          {/* ─── Export ─────────────────────────────────────── */}
          <Separator />
          <section className="grid gap-3">
            <h3 className="flex items-center gap-1.5 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
              <Download className="size-3.5" />
              Export
            </h3>
            <div className="grid gap-1.5">
              <Label>Heightmap format</Label>
              <Select value={heightmapFormat} onValueChange={(value) => setHeightmapFormat(value as HeightmapFormat)}>
                <SelectTrigger>
                  <SelectValue placeholder="Select format" />
                </SelectTrigger>
                <SelectContent>
                  {HEIGHTMAP_FORMATS.map((f) => (
                    <SelectItem key={f.value} value={f.value}>
                      {f.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
            <div className="grid gap-1.5">
              <Label>Albedo format</Label>
              <Select value={albedoFormat} onValueChange={(value) => setAlbedoFormat(value as AlbedoFormat)}>
                <SelectTrigger>
                  <SelectValue placeholder="Select format" />
                </SelectTrigger>
                <SelectContent>
                  {ALBEDO_FORMATS.map((f) => (
                    <SelectItem key={f.value} value={f.value}>
                      {f.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
            <div className="grid grid-cols-2 gap-2">
              <div className="grid gap-1.5">
                <Label htmlFor="heightmap-size">Heightmap (px)</Label>
                <Input
                  id="heightmap-size"
                  type="number"
                  min={128}
                  max={4096}
                  step={128}
                  value={heightmapSize}
                  onChange={(e) => setHeightmapSize(parseInt(e.target.value, 10) || 1024)}
                />
              </div>
              <div className="grid gap-1.5">
                <Label htmlFor="albedo-size">Albedo (px)</Label>
                <Input
                  id="albedo-size"
                  type="number"
                  min={128}
                  max={4096}
                  step={128}
                  value={albedoSize}
                  onChange={(e) => setAlbedoSize(parseInt(e.target.value, 10) || 1024)}
                />
              </div>
            </div>
            <div className="grid gap-1.5">
              <Label>CRS</Label>
              <Select value={crsSource} onValueChange={(value) => setCrsSource(value)}>
                <SelectTrigger>
                  <SelectValue placeholder="Select CRS" />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="auto">Auto (UTM from centroid)</SelectItem>
                  <SelectItem value="EPSG:4326">EPSG:4326 (WGS84)</SelectItem>
                  <SelectItem value="EPSG:3857">EPSG:3857 (Web Mercator)</SelectItem>
                </SelectContent>
              </Select>
            </div>
            <div className="grid gap-1.5">
              <Label>Compression</Label>
              <Select value={compression} onValueChange={(value) => setCompression(value as 'none' | 'deflate')}>
                <SelectTrigger>
                  <SelectValue placeholder="Select compression" />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="deflate">Deflate (zlib)</SelectItem>
                  <SelectItem value="none">None</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <Button type="button" onClick={handleExport} disabled={exporting || !selectedBounds || selectedCount === 0}>
              <Download className="size-4" />
              {exporting ? 'Exporting...' : `Export ${selectedCount} tile${selectedCount !== 1 ? 's' : ''}`}
            </Button>
          </section>
        </aside>
        </ScrollArea>
      </div>
    </div>
  )
}
