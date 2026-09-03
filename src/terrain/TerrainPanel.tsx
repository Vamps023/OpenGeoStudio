import { useState } from 'react'
import { resolveCRS } from '../engine/crs'
import type { GeoBounds } from '../engine/crs'
import { useStore } from '../state/store'

export default function TerrainPanel() {
  const setTerrain = useStore((s) => s.setTerrain)
  const setTerrainLoading = useStore((s) => s.setTerrainLoading)
  const setTerrainError = useStore((s) => s.setTerrainError)
  const terrainLoading = useStore((s) => s.terrainLoading)
  const terrainError = useStore((s) => s.terrainError)
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const project = projects.find((p) => p.id === activeProjectId)

  const [bounds, setBounds] = useState<GeoBounds>({
    west: -122.52,
    south: 37.7,
    east: -122.45,
    north: 37.75,
  })
  const [targetSize, setTargetSize] = useState(512)
  const [maxZoom, setMaxZoom] = useState(12)
  const [heightScale, setHeightScale] = useState(1)
  const [crsSource, setCrsSource] = useState('auto')
  const [progress, setProgress] = useState<string | null>(null)

  const terrain = project?.terrain

  async function handleDownload() {
    if (!window.ogs) {
      setTerrainError('Electron bridge not available. Run via npm start.')
      return
    }
    setTerrainLoading(true)
    setTerrainError(null)
    setProgress('Starting download...')
    try {
      const result = await window.ogs.downloadTerrain(bounds, {
        targetSize,
        maxZoom,
        onProgress: (p: { stage: string; current: number; total: number; message: string }) => {
          setProgress(p.message)
        },
      })
      if (!result.success) {
        setTerrainError(result.error || 'Download failed')
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
      }
    } catch (err) {
      setTerrainError((err as Error).message)
    } finally {
      setTerrainLoading(false)
    }
  }

  async function handleSaveGeoTIFF() {
    if (!window.ogs || !terrain) return
    setTerrainError(null)
    const crs = resolveCRS(crsSource, bounds)
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
      setTerrainError(result.error)
    }
  }

  function updateBound(key: keyof GeoBounds, value: string) {
    const parsed = parseFloat(value)
    if (Number.isNaN(parsed)) return
    setBounds((prev) => ({ ...prev, [key]: parsed }))
  }

  return (
    <div className="terrain-panel">
      <h3>Terrain</h3>

      <label className="field">
        <span>West (longitude)</span>
        <input type="number" step={0.01} value={bounds.west} onChange={(e) => updateBound('west', e.target.value)} />
      </label>
      <label className="field">
        <span>South (latitude)</span>
        <input type="number" step={0.01} value={bounds.south} onChange={(e) => updateBound('south', e.target.value)} />
      </label>
      <label className="field">
        <span>East (longitude)</span>
        <input type="number" step={0.01} value={bounds.east} onChange={(e) => updateBound('east', e.target.value)} />
      </label>
      <label className="field">
        <span>North (latitude)</span>
        <input type="number" step={0.01} value={bounds.north} onChange={(e) => updateBound('north', e.target.value)} />
      </label>

      <label className="field">
        <span>Resolution (pixels)</span>
        <input type="number" min={128} max={2048} step={128} value={targetSize} onChange={(e) => setTargetSize(parseInt(e.target.value, 10) || 512)} />
      </label>
      <label className="field">
        <span>Max zoom level</span>
        <input type="number" min={1} max={15} value={maxZoom} onChange={(e) => setMaxZoom(parseInt(e.target.value, 10) || 12)} />
      </label>
      <label className="field">
        <span>Height scale (3D exaggeration)</span>
        <input type="number" min={0.1} max={10} step={0.1} value={heightScale} onChange={(e) => setHeightScale(parseFloat(e.target.value) || 1)} />
      </label>
      <label className="field">
        <span>CRS (GeoTIFF export)</span>
        <select value={crsSource} onChange={(e) => setCrsSource(e.target.value)}>
          <option value="auto">Auto (UTM from centroid)</option>
          <option value="EPSG:4326">EPSG:4326 (WGS84)</option>
          <option value="EPSG:3857">EPSG:3857 (Web Mercator)</option>
        </select>
      </label>

      <div className="panel-actions">
        <button type="button" onClick={handleDownload} disabled={terrainLoading}>
          {terrainLoading ? 'Downloading...' : 'Download Terrain'}
        </button>
        <button type="button" onClick={handleSaveGeoTIFF} disabled={!terrain || terrainLoading}>
          Save as GeoTIFF
        </button>
      </div>

      {progress && <p className="empty-note">{progress}</p>}
      {terrainError && <p className="empty-note" style={{ color: '#f0883e' }}>{terrainError}</p>}

      {terrain && (
        <div className="terrain-info">
          <div className="stat-row"><span>Size</span><b>{terrain.width} × {terrain.height}</b></div>
          <div className="stat-row"><span>Elevation range</span><b>{terrain.minElevation.toFixed(1)} – {terrain.maxElevation.toFixed(1)} m</b></div>
          <div className="stat-row"><span>CRS</span><b>{resolveCRS(crsSource, bounds)}</b></div>
        </div>
      )}
    </div>
  )
}
