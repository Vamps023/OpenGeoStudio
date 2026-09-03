/**
 * Export engine — orchestrates DEM download, imagery download,
 * resize, and format writing for heightmap + albedo.
 */

const fs = require('node:fs/promises')
const path = require('node:path')

function resolveCRS(crsSource, bounds) {
  const crs = crsSource || 'EPSG:4326'
  if (crs !== 'auto') return crs
  const centroidLon = (bounds.west + bounds.east) / 2
  const centroidLat = (bounds.north + bounds.south) / 2
  const zone = Math.min(60, Math.max(1, Math.floor((centroidLon + 180) / 6) + 1))
  return centroidLat >= 0 ? `EPSG:${32600 + zone}` : `EPSG:${32700 + zone}`
}

function getHeightmapExtension(format) {
  switch (format) {
    case 'png': return 'png'
    case 'r16': return 'r16'
    case 'float32': return 'tif'
    case 'geotiff': return 'tif'
    case 'dem': return 'tif'
    case 'none': return ''
    default: throw new Error(`Unknown heightmap format: ${format}`)
  }
}

function getAlbedoExtension(format) {
  switch (format) {
    case 'geotiff': return 'tif'
    case 'png': return 'png'
    case 'none': return ''
    default: throw new Error(`Unknown albedo format: ${format}`)
  }
}

/**
 * Execute a full terrain export.
 *
 * options:
 *   bounds           — GeoBounds { west, south, east, north }
 *   outputPath       — directory to write files into
 *   demSource        — DEM provider id (see demDownloader.cjs)
 *   imagerySource    — imagery provider id (see imageryDownloader.cjs)
 *   heightmapFormat  — 'png' | 'r16' | 'geotiff' | 'dem' | 'float32' | 'none'
 *   albedoFormat     — 'png' | 'geotiff' | 'none'
 *   heightmapSize    — pixel resolution for heightmap (default 1024)
 *   albedoSize       — pixel resolution for albedo (default 1024)
 *   demApiKey        — API key/token for DEM provider (if needed)
 *   imageryApiKey    — API key/token for imagery provider (if needed)
 *   crsSource        — 'auto' | 'EPSG:4326' | 'EPSG:3857' | 'EPSG:326xx'
 *   compression      — 'none' | 'deflate'
 *   downloadDem      — boolean (default true)
 *   downloadImagery  — boolean (default true)
 *   tiles            — array of { row, col, bounds } for per-tile export
 *   onProgress       — callback({ stage, current, total, message })
 */
async function executeExport(options) {
  // Lazy-load modules inside the function so a load failure doesn't
  // crash the entire Electron main process at startup.
  const { downloadTerrainDEM } = require('./demDownloader.cjs')
  const { downloadImagery: fetchImagery } = require('./imageryDownloader.cjs')
  const { writeHeightmap, writeAlbedo, computeElevationMetadata } = require('./formatWriter.cjs')

  const {
    bounds,
    outputPath,
    demSource = 'aws-terrarium',
    imagerySource = 'arcgis',
    heightmapFormat = 'geotiff',
    albedoFormat = 'png',
    heightmapSize = 1024,
    albedoSize = 1024,
    demApiKey,
    imageryApiKey,
    compression = 'none',
    downloadDem = true,
    downloadImagery = true,
    tiles,
  } = options

  const onProgress = options.onProgress || (() => {})
  const crs = resolveCRS(options.crsSource, bounds)

  if (!outputPath) throw new Error('outputPath is required')
  await fs.mkdir(outputPath, { recursive: true })
  console.log('[exportEngine] Output directory:', outputPath)
  console.log('[exportEngine] CRS:', crs, '| DEM:', demSource, '| Imagery:', imagerySource)
  console.log('[exportEngine] Heightmap:', heightmapFormat, heightmapSize, '| Albedo:', albedoFormat, albedoSize)
  console.log('[exportEngine] Tiles to export:', tiles ? tiles.length : 1)

  const heightmapExt = getHeightmapExtension(heightmapFormat)
  const albedoExt = getAlbedoExtension(albedoFormat)

  // Determine export units: per-tile or single bounds
  const exportTiles = tiles && tiles.length > 0
    ? tiles
    : [{ row: 0, col: 0, bounds }]

  const allFiles = {}
  let elevationMeta = { min: 0, max: 0, range: 0, hasNoData: false }

  for (let i = 0; i < exportTiles.length; i++) {
    const tile = exportTiles[i]
    const tileLabel = exportTiles.length > 1 ? `tile_${tile.row}_${tile.col}` : ''
    const prefix = tileLabel ? `${tileLabel}_` : ''
    const heightmapFile = heightmapFormat !== 'none' ? `${prefix}heightmap.${heightmapExt}` : null
    const albedoFile = albedoFormat !== 'none' ? `${prefix}albedo.${albedoExt}` : null

    onProgress({ stage: 'tile', current: i + 1, total: exportTiles.length, message: `Processing tile ${i + 1}/${exportTiles.length} (row ${tile.row}, col ${tile.col})` })

    // ── Download + write heightmap ───────────────────────────
    if (downloadDem && heightmapFormat !== 'none') {
      console.log('[exportEngine] Downloading DEM for tile', tile.row, tile.col, 'with', demSource)
      onProgress({ stage: 'download_dem', current: 0, total: 1, message: `Downloading DEM for tile ${tile.row},${tile.col}...` })
      const dem = await downloadTerrainDEM(tile.bounds, {
        provider: demSource,
        apiKey: demApiKey,
        targetSize: heightmapSize,
        onProgress: (p) => onProgress({ stage: 'download_dem', ...p }),
      })
      console.log('[exportEngine] DEM downloaded:', dem.width, 'x', dem.height, 'min:', dem.minElevation, 'max:', dem.maxElevation)
      const meta = computeElevationMetadata(dem.elevations)
      if (meta.min < elevationMeta.min || elevationMeta.min === 0) elevationMeta.min = meta.min
      if (meta.max > elevationMeta.max) elevationMeta.max = meta.max

      onProgress({ stage: 'write_heightmap', current: 0, total: 1, message: `Writing ${heightmapFormat} heightmap...` })
      const heightmapPath = path.join(outputPath, heightmapFile)
      await writeHeightmap(dem.elevations, dem.width, dem.height, tile.bounds, heightmapFormat, heightmapPath, compression, crs)
      console.log('[exportEngine] Heightmap written:', heightmapPath)
      allFiles[`tile_${tile.row}_${tile.col}_heightmap`] = heightmapPath
      onProgress({ stage: 'write_heightmap', current: 1, total: 1, message: 'Heightmap written.' })
    }

    // ── Download + write albedo ──────────────────────────────
    if (downloadImagery && albedoFormat !== 'none') {
      console.log('[exportEngine] Downloading imagery for tile', tile.row, tile.col, 'with', imagerySource)
      onProgress({ stage: 'download_imagery', current: 0, total: 1, message: `Downloading imagery for tile ${tile.row},${tile.col}...` })
      const img = await fetchImagery(tile.bounds, {
        source: imagerySource,
        apiKey: imageryApiKey,
        targetSize: albedoSize,
        onProgress: (p) => onProgress({ stage: 'download_imagery', ...p }),
      })
      console.log('[exportEngine] Imagery downloaded:', img.width, 'x', img.height)

      onProgress({ stage: 'write_albedo', current: 0, total: 1, message: `Writing ${albedoFormat} albedo...` })
      const albedoPath = path.join(outputPath, albedoFile)
      await writeAlbedo(img.buffer, img.width, img.height, tile.bounds, albedoFormat, albedoPath, compression, crs)
      allFiles[`tile_${tile.row}_${tile.col}_albedo`] = albedoPath
      onProgress({ stage: 'write_albedo', current: 1, total: 1, message: 'Albedo written.' })
    }
  }

  // ── Write manifest ───────────────────────────────────────────
  const manifest = {
    version: '1.0.0',
    createdBy: 'OpenGeoStudio',
    createdAt: new Date().toISOString(),
    bounds,
    crs,
    tileSizeKm: tiles && tiles.length > 0 ? null : undefined,
    tiles: exportTiles.map((t) => ({
      row: t.row,
      col: t.col,
      bounds: t.bounds,
      heightmap: heightmapFormat !== 'none' ? (exportTiles.length > 1 ? `tile_${t.row}_${t.col}_heightmap.${heightmapExt}` : `heightmap.${heightmapExt}`) : null,
      albedo: albedoFormat !== 'none' ? (exportTiles.length > 1 ? `tile_${t.row}_${t.col}_albedo.${albedoExt}` : `albedo.${albedoExt}`) : null,
    })),
    heightmap: { format: heightmapFormat, size: heightmapSize },
    albedo: { format: albedoFormat, size: albedoSize },
    elevation: { min: elevationMeta.min, max: elevationMeta.max, range: elevationMeta.max - elevationMeta.min },
    demSource,
    imagerySource,
  }
  const manifestPath = path.join(outputPath, 'manifest.json')
  await fs.writeFile(manifestPath, JSON.stringify(manifest, null, 2))
  allFiles.manifest = manifestPath

  onProgress({ stage: 'done', current: 1, total: 1, message: `Export complete — ${exportTiles.length} tile(s).` })

  return {
    manifestPath,
    elevationRange: { min: elevationMeta.min, max: elevationMeta.max },
    files: allFiles,
  }
}

module.exports = { executeExport, resolveCRS }
