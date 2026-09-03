import type { TerrainData } from './engine/terrainMesh'
import type { GeoBounds } from './engine/crs'

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

interface OgsTerrainDownloadOptions {
  provider?: DEMProvider
  apiKey?: string
  targetSize?: number
  maxZoom?: number
  onProgress?: (p: { stage: string; current: number; total: number; message: string }) => void
}

interface OgsSaveGeoTIFFData {
  elevations: Float32Array
}

interface OgsSaveGeoTIFFOptions {
  width: number
  height: number
  bitsPerSample: 8 | 16 | 32
  sampleFormat: 1 | 2 | 3
  samplesPerPixel: 1 | 3
  photometricInterpretation: 1 | 2
  bounds: GeoBounds
  compression?: 'none' | 'deflate'
  rasterType?: 'area' | 'point'
  crs?: string
  filename?: string
}

interface OgsExportTile {
  row: number
  col: number
  bounds: GeoBounds
}

interface OgsExportOptions {
  bounds: GeoBounds
  outputPath?: string
  demSource?: DEMProvider
  imagerySource?: ImagerySource
  heightmapFormat?: HeightmapFormat
  albedoFormat?: AlbedoFormat
  heightmapSize?: number
  albedoSize?: number
  demApiKey?: string
  imageryApiKey?: string
  crsSource?: string
  compression?: 'none' | 'deflate'
  downloadDem?: boolean
  downloadImagery?: boolean
  tiles?: OgsExportTile[]
}

interface OgsExportResult {
  success: boolean
  error?: string
  manifestPath?: string
  elevationRange?: { min: number; max: number }
  files?: Record<string, string>
}

interface OgsBridge {
  downloadTerrain: (
    bounds: GeoBounds,
    options?: OgsTerrainDownloadOptions,
  ) => Promise<{
    success: boolean
    error?: string
    data?: TerrainData & { zoom: number }
  }>
  saveGeoTIFF: (
    data: OgsSaveGeoTIFFData,
    options: OgsSaveGeoTIFFOptions,
  ) => Promise<{ success: boolean; error?: string; path?: string }>
  exportTerrain: (options: OgsExportOptions) => Promise<OgsExportResult>
  onExportProgress: (callback: (p: { stage: string; current: number; total: number; message: string }) => void) => () => void
}

interface Window {
  ogs?: OgsBridge
}

export type { DEMProvider, ImagerySource, HeightmapFormat, AlbedoFormat }
