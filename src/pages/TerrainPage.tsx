import { useEffect, useMemo, useState } from 'react'
import { resolveCRS } from '../engine/crs'
import type { GeoBounds } from '../engine/crs'
import { buildTerrainMesh } from '../engine/terrainMesh'
import type { TerrainData } from '../engine/terrainMesh'
import { computeTileGrid, tileKey } from '../engine/tileGrid'
import type { TileGrid } from '../engine/tileGrid'
import { useStore } from '../state/store'
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

export default function TerrainPage({ onBack }: { onBack: () => void }) {
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
        setTerrain({
          elevations: data.elevations,
          width: data.width,
          height: data.height,
          bounds: data.bounds,
          minElevation: data.minElevation,
          maxElevation: data.maxElevation,
        })
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
      } else {
        setProgress(`Export complete → ${result.files?.heightmap || ''} ${result.files?.albedo || ''}`.trim())
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
    <div className="editor-page">
      <header className="editor-topbar">
        <button type="button" className="btn-ghost" onClick={onBack}>← Projects</button>
        <div className="editor-title">
          <strong>{project.name}</strong>
          <span>Terrain Workspace</span>
        </div>
        <div className="mode-toggle">
          <button type="button" className={view === 'map' ? 'active' : ''} onClick={() => setView('map')}>Map</button>
          <button type="button" className={view === '3d' ? 'active' : ''} onClick={() => setView('3d')} disabled={!terrain}>3D</button>
        </div>
      </header>

      <div className="editor-body">
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

        <aside className="editor-panel terrain-sidebar">
          {/* ─── Tile Grid ──────────────────────────────────── */}
          <h3>Tile Grid</h3>
          <div className="tile-size-buttons">
            {TILE_SIZES.map((size) => (
              <button
                key={size}
                type="button"
                className={tileSizeKm === size ? 'active' : ''}
                onClick={() => setTileSizeKm(size)}
                title={`${size}km tiles`}
              >
                {size}km
              </button>
            ))}
            <button
              type="button"
              className={gridVisible ? 'active' : ''}
              onClick={() => setGridVisible((v) => !v)}
              title="Toggle grid visibility"
            >
              Grid
            </button>
          </div>

          {tileGrid && (
            <div className="tile-grid-actions">
              <button type="button" onClick={selectAllTiles}>Select All</button>
              <button type="button" onClick={deselectAllTiles}>Clear</button>
              <span className="tile-count">{selectedCount}/{totalTiles} tiles</span>
            </div>
          )}

          {/* ─── DEM Source ─────────────────────────────────── */}
          <h3 style={{ marginTop: '18px' }}>DEM Source (Heightmap)</h3>
          <label className="field">
            <span>Provider</span>
            <select value={demProvider} onChange={(e) => { setDemProvider(e.target.value as DEMProvider); setDemApiKey('') }}>
              {DEM_PROVIDERS.map((p) => (
                <option key={p.value} value={p.value}>{p.label}</option>
              ))}
            </select>
          </label>
          {demInfo.needsKey && (
            <label className="field">
              <span>{demInfo.keyLabel}</span>
              <input type="password" value={demApiKey} onChange={(e) => setDemApiKey(e.target.value)} placeholder={demInfo.keyHint || 'Enter API key'} />
            </label>
          )}

          {/* ─── Imagery Source ─────────────────────────────── */}
          <h3 style={{ marginTop: '18px' }}>Imagery Source (Albedo)</h3>
          <label className="field">
            <span>Provider</span>
            <select value={imagerySource} onChange={(e) => { setImagerySource(e.target.value as ImagerySource); setImageryApiKey('') }}>
              {IMAGERY_SOURCES.map((p) => (
                <option key={p.value} value={p.value}>{p.label}</option>
              ))}
            </select>
          </label>
          {imageryInfo.needsKey && (
            <label className="field">
              <span>{imageryInfo.keyLabel}</span>
              <input type="password" value={imageryApiKey} onChange={(e) => setImageryApiKey(e.target.value)} placeholder={imageryInfo.keyHint || 'Enter API key'} />
            </label>
          )}

          {selectedBounds && (
            <div className="terrain-info">
              <div className="stat-row"><span>West</span><b>{selectedBounds.west.toFixed(4)}</b></div>
              <div className="stat-row"><span>East</span><b>{selectedBounds.east.toFixed(4)}</b></div>
              <div className="stat-row"><span>South</span><b>{selectedBounds.south.toFixed(4)}</b></div>
              <div className="stat-row"><span>North</span><b>{selectedBounds.north.toFixed(4)}</b></div>
            </div>
          )}

          <div className="panel-actions">
            <button type="button" onClick={handleDownload} disabled={loading || !selectedBounds}>
              {loading ? 'Downloading...' : 'Preview DEM (3D)'}
            </button>
          </div>

          {progress && <p className="empty-note">{progress}</p>}
          {error && <p className="empty-note" style={{ color: '#f0883e' }}>{error}</p>}

          {terrain && (
            <>
              <h3 style={{ marginTop: '18px' }}>3D Preview</h3>
              <label className="field">
                <span>Height scale</span>
                <input type="number" min={0.1} max={10} step={0.1} value={heightScale} onChange={(e) => setHeightScale(parseFloat(e.target.value) || 1)} />
              </label>

              <div className="terrain-info">
                <div className="stat-row"><span>Size</span><b>{terrain.width} × {terrain.height}</b></div>
                <div className="stat-row"><span>Elevation</span><b>{terrain.minElevation.toFixed(1)} – {terrain.maxElevation.toFixed(1)} m</b></div>
              </div>

              <div className="panel-actions">
                <button type="button" onClick={handleSaveGeoTIFF} disabled={loading}>Save DEM as GeoTIFF</button>
              </div>
            </>
          )}

          {/* ─── Export ─────────────────────────────────────── */}
          <h3 style={{ marginTop: '18px' }}>Export</h3>
          <label className="field">
            <span>Heightmap format</span>
            <select value={heightmapFormat} onChange={(e) => setHeightmapFormat(e.target.value as HeightmapFormat)}>
              {HEIGHTMAP_FORMATS.map((f) => (
                <option key={f.value} value={f.value}>{f.label}</option>
              ))}
            </select>
          </label>
          <label className="field">
            <span>Albedo format</span>
            <select value={albedoFormat} onChange={(e) => setAlbedoFormat(e.target.value as AlbedoFormat)}>
              {ALBEDO_FORMATS.map((f) => (
                <option key={f.value} value={f.value}>{f.label}</option>
              ))}
            </select>
          </label>
          <label className="field">
            <span>Heightmap size (px)</span>
            <input type="number" min={128} max={4096} step={128} value={heightmapSize} onChange={(e) => setHeightmapSize(parseInt(e.target.value, 10) || 1024)} />
          </label>
          <label className="field">
            <span>Albedo size (px)</span>
            <input type="number" min={128} max={4096} step={128} value={albedoSize} onChange={(e) => setAlbedoSize(parseInt(e.target.value, 10) || 1024)} />
          </label>
          <label className="field">
            <span>CRS</span>
            <select value={crsSource} onChange={(e) => setCrsSource(e.target.value)}>
              <option value="auto">Auto (UTM from centroid)</option>
              <option value="EPSG:4326">EPSG:4326 (WGS84)</option>
              <option value="EPSG:3857">EPSG:3857 (Web Mercator)</option>
            </select>
          </label>
          <label className="field">
            <span>Compression</span>
            <select value={compression} onChange={(e) => setCompression(e.target.value as 'none' | 'deflate')}>
              <option value="deflate">Deflate (zlib)</option>
              <option value="none">None</option>
            </select>
          </label>

          <div className="panel-actions">
            <button type="button" onClick={handleExport} disabled={exporting || !selectedBounds || selectedCount === 0}>
              {exporting ? 'Exporting...' : `Export ${selectedCount} tile${selectedCount !== 1 ? 's' : ''}`}
            </button>
          </div>
        </aside>
      </div>
    </div>
  )
}
