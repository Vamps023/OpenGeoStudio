/**
 * Multi-provider DEM fetcher.
 *
 * Providers:
 *   - aws-terrarium    — free, no API key (Terrarium PNG tiles)
 *   - mapbox-terrain   — requires Mapbox token (Terrain RGB tiles)
 *   - opentopo-*       — requires OpenTopography API key (GeoTIFF)
 *   - nasa-earthdata   — free (Copernicus DEM GeoTIFF tiles)
 *   - gpxz             — requires GPXZ API key (GeoTIFF)
 */

const https = require('node:https')
const { fromArrayBuffer } = require('geotiff')
const sharp = require('sharp')
const {
  TILE_SIZE,
  getTileRange,
  chooseZoom,
  lngToPixelX,
  latToPixelY,
  tileXToLng,
  tileYToLat,
} = require('./tileMath.cjs')
const { downloadBuffer, downloadWithRetry } = require('./downloader.cjs')

// ─── Helpers ──────────────────────────────────────────────────

function mergeDEMTiles(tiles, range) {
  const tilesX = range.maxX - range.minX + 1
  const tilesY = range.maxY - range.minY + 1
  const tileSize = 256
  const w = tilesX * tileSize
  const h = tilesY * tileSize
  const elevations = new Float32Array(w * h)
  for (const tile of tiles) {
    const offsetX = (tile.x - range.minX) * tileSize
    const offsetY = (tile.y - range.minY) * tileSize
    const tw = tile.width
    const th = tile.height
    for (let y = 0; y < th; y++) {
      const srcStart = y * tw
      const dstStart = (offsetY + y) * w + offsetX
      elevations.set(tile.elevations.subarray(srcStart, srcStart + tw), dstStart)
    }
  }
  return { elevations, width: w, height: h }
}

function cropDEM(elevations, fullW, fullH, bounds, range, zoom) {
  const pxWest = lngToPixelX(bounds.west, zoom)
  const pxEast = lngToPixelX(bounds.east, zoom)
  const pxNorth = latToPixelY(bounds.north, zoom)
  const pxSouth = latToPixelY(bounds.south, zoom)
  const fullPxWest = lngToPixelX(tileXToLng(range.minX, zoom), zoom)
  const fullPxNorth = latToPixelY(tileYToLat(range.minY, zoom), zoom)
  const left = Math.round(pxWest - fullPxWest)
  const top = Math.round(pxNorth - fullPxNorth)
  const width = Math.max(1, Math.round(Math.abs(pxEast - pxWest)) + 1)
  const height = Math.max(1, Math.round(Math.abs(pxSouth - pxNorth)) + 1)
  const cropped = new Float32Array(width * height)
  for (let y = 0; y < height; y++) {
    const srcY = top + y
    for (let x = 0; x < width; x++) {
      const srcX = left + x
      if (srcX >= 0 && srcX < fullW && srcY >= 0 && srcY < fullH) {
        cropped[y * width + x] = elevations[srcY * fullW + srcX]
      }
    }
  }
  return { elevations: cropped, width, height }
}

function resizeDEM(elevations, srcW, srcH, dstW, dstH) {
  const result = new Float32Array(dstW * dstH)
  const sxScale = (srcW - 1) / (dstW - 1 || 1)
  const syScale = (srcH - 1) / (dstH - 1 || 1)
  for (let y = 0; y < dstH; y++) {
    const sy = y * syScale
    const sy0 = Math.floor(sy)
    const sy1 = Math.min(sy0 + 1, srcH - 1)
    const fy = sy - sy0
    for (let x = 0; x < dstW; x++) {
      const sx = x * sxScale
      const sx0 = Math.floor(sx)
      const sx1 = Math.min(sx0 + 1, srcW - 1)
      const fx = sx - sx0
      const v00 = elevations[sy0 * srcW + sx0]
      const v01 = elevations[sy0 * srcW + sx1]
      const v10 = elevations[sy1 * srcW + sx0]
      const v11 = elevations[sy1 * srcW + sx1]
      result[y * dstW + x] = (v00 * (1 - fx) + v01 * fx) * (1 - fy) + (v10 * (1 - fx) + v11 * fx) * fy
    }
  }
  return result
}

function computeStats(elevations) {
  let min = Infinity
  let max = -Infinity
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i]
    if (v < min) min = v
    if (v > max) max = v
  }
  if (!Number.isFinite(min)) min = 0
  if (!Number.isFinite(max)) max = 0
  return { minElevation: min, maxElevation: max }
}

// ─── Terrarium (AWS) — free, no API key ───────────────────────

function decodeTerrariumElevation(r, g, b) {
  return r * 256 + g + b / 256 - 32768
}

async function decodeTerrariumTile(buffer) {
  const { data, info } = await sharp(buffer).raw().ensureAlpha().toBuffer({ resolveWithObject: true })
  const w = info.width
  const h = info.height
  const elevations = new Float32Array(w * h)
  for (let i = 0; i < w * h; i++) {
    elevations[i] = decodeTerrariumElevation(data[i * 4], data[i * 4 + 1], data[i * 4 + 2])
  }
  return { elevations, width: w, height: h }
}

async function fetchTerrariumDEM(bounds, options) {
  const targetSize = options.targetSize || 512
  const maxZoom = options.maxZoom || 12
  const onProgress = options.onProgress || (() => {})

  const zoom = Math.min(maxZoom, chooseZoom(bounds, targetSize, maxZoom))
  const range = getTileRange(bounds, zoom)
  const tilesX = range.maxX - range.minX + 1
  const tilesY = range.maxY - range.minY + 1
  const totalTiles = tilesX * tilesY
  if (totalTiles > 1024) throw new Error(`Too many tiles (${totalTiles}). Reduce the area or zoom level.`)

  onProgress({ stage: 'download', current: 0, total: totalTiles, message: `Downloading ${totalTiles} Terrarium tiles at zoom ${zoom}...` })

  const tileResults = []
  let completed = 0
  let index = 0
  async function worker() {
    while (index < totalTiles) {
      const taskIndex = index++
      const x = range.minX + (taskIndex % tilesX)
      const y = range.minY + Math.floor(taskIndex / tilesX)
      const url = `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${zoom}/${x}/${y}.png`
      try {
        const buffer = await downloadWithRetry(url, 3, 30000)
        const decoded = await decodeTerrariumTile(buffer)
        tileResults.push({ x, y, ...decoded })
      } catch {
        tileResults.push({ x, y, elevations: new Float32Array(TILE_SIZE * TILE_SIZE), width: TILE_SIZE, height: TILE_SIZE })
      }
      completed++
      onProgress({ stage: 'download', current: completed, total: totalTiles, message: `Downloaded ${completed}/${totalTiles} tiles` })
    }
  }
  await Promise.all(Array.from({ length: Math.min(6, totalTiles) }, () => worker()))
  if (tileResults.length === 0) throw new Error('All DEM tiles failed to download.')

  const merged = mergeDEMTiles(tileResults, range)
  const cropped = cropDEM(merged.elevations, merged.width, merged.height, bounds, range, zoom)
  const resized = resizeDEM(cropped.elevations, cropped.width, cropped.height, targetSize, targetSize)
  const stats = computeStats(resized)
  return { elevations: resized, width: targetSize, height: targetSize, bounds, zoom, ...stats }
}

// ─── Mapbox Terrain RGB — requires token ──────────────────────

function decodeMapboxTerrainRGB(r, g, b) {
  return -10000 + ((r * 256 * 256 + g * 256 + b) * 0.1)
}

async function decodeMapboxTile(buffer) {
  const { data, info } = await sharp(buffer).raw().ensureAlpha().toBuffer({ resolveWithObject: true })
  const w = info.width
  const h = info.height
  const elevations = new Float32Array(w * h)
  for (let i = 0; i < w * h; i++) {
    elevations[i] = decodeMapboxTerrainRGB(data[i * 4], data[i * 4 + 1], data[i * 4 + 2])
  }
  return { elevations, width: w, height: h }
}

async function fetchMapboxDEM(bounds, options) {
  const token = options.apiKey || process.env.MAPBOX_ACCESS_TOKEN
  if (!token) throw new Error('Mapbox access token required. Add it in the API Keys section.')
  const targetSize = options.targetSize || 512
  const maxZoom = options.maxZoom || 14
  const onProgress = options.onProgress || (() => {})

  const zoom = Math.min(maxZoom, chooseZoom(bounds, targetSize, maxZoom))
  const range = getTileRange(bounds, zoom)
  const tilesX = range.maxX - range.minX + 1
  const tilesY = range.maxY - range.minY + 1
  const totalTiles = tilesX * tilesY
  if (totalTiles > 1024) throw new Error(`Too many tiles (${totalTiles}). Reduce the area or zoom level.`)

  onProgress({ stage: 'download', current: 0, total: totalTiles, message: `Downloading ${totalTiles} Mapbox Terrain RGB tiles...` })

  const tileResults = []
  let completed = 0
  let index = 0
  async function worker() {
    while (index < totalTiles) {
      const taskIndex = index++
      const x = range.minX + (taskIndex % tilesX)
      const y = range.minY + Math.floor(taskIndex / tilesX)
      const url = `https://api.mapbox.com/v4/mapbox.terrain-rgb/${zoom}/${x}/${y}@2x.pngraw?access_token=${token}`
      try {
        const buffer = await downloadWithRetry(url, 3, 30000)
        const decoded = await decodeMapboxTile(buffer)
        tileResults.push({ x, y, ...decoded })
      } catch {
        tileResults.push({ x, y, elevations: new Float32Array(512 * 512), width: 512, height: 512 })
      }
      completed++
      onProgress({ stage: 'download', current: completed, total: totalTiles, message: `Downloaded ${completed}/${totalTiles} tiles` })
    }
  }
  await Promise.all(Array.from({ length: Math.min(6, totalTiles) }, () => worker()))
  if (tileResults.length === 0) throw new Error('All Mapbox tiles failed to download.')

  const merged = mergeDEMTiles(tileResults, range)
  const cropped = cropDEM(merged.elevations, merged.width, merged.height, bounds, range, zoom)
  const resized = resizeDEM(cropped.elevations, cropped.width, cropped.height, targetSize, targetSize)
  const stats = computeStats(resized)
  return { elevations: resized, width: targetSize, height: targetSize, bounds, zoom, ...stats }
}

// ─── OpenTopography — requires API key ────────────────────────

const OPENTOPO_DEM_TYPES = {
  'opentopo-srtmgl1': 'SRTMGL1',
  'opentopo-srtmgl3': 'SRTMGL3',
  'opentopo-aw3d30': 'AW3D30',
  'opentopo-cop30': 'COP30',
  'opentopo-nasadem': 'NASADEM',
  'opentopo-usgs10m': 'USGS10m',
}

async function fetchOpenTopoDEM(bounds, options) {
  const apiKey = (options.apiKey || process.env.OPENTOPOGRAPHY_API_KEY || '').trim()
  if (!apiKey) throw new Error('OpenTopography API key required. Get a free key at https://portal.opentopography.org/myopentopo')
  const demType = OPENTOPO_DEM_TYPES[options.provider] || 'SRTMGL1'
  const onProgress = options.onProgress || (() => {})
  const isUsgs3dep = options.provider === 'opentopo-usgs10m'

  const url =
    `https://portal.opentopography.org/API/${isUsgs3dep ? 'usgsdem' : 'globaldem'}?` +
    (isUsgs3dep ? `datasetName=${demType}` : `demtype=${demType}`) +
    `&south=${bounds.south}&north=${bounds.north}&west=${bounds.west}&east=${bounds.east}` +
    `&outputFormat=GTiff&API_Key=${encodeURIComponent(apiKey)}`

  onProgress({ stage: 'download', current: 0, total: 1, message: `Requesting ${demType} from OpenTopography...` })

  const buffer = await downloadWithRetry(url, 3, 60000)
  if (buffer.length < 100) throw new Error(`OpenTopography returned small response: ${buffer.toString('utf-8').substring(0, 200)}`)

  const isTiff = (buffer[0] === 0x49 && buffer[1] === 0x49 && buffer[2] === 0x2a && buffer[3] === 0x00) ||
    (buffer[0] === 0x4d && buffer[1] === 0x4d && buffer[2] === 0x00 && buffer[3] === 0x2a)
  if (!isTiff) throw new Error(`OpenTopography did not return a TIFF. Response: ${buffer.toString('utf-8').substring(0, 500)}`)

  const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength)
  const tiff = await fromArrayBuffer(arrayBuffer)
  const image = await tiff.getImage()
  const width = image.getWidth()
  const height = image.getHeight()
  const rasters = await image.readRasters()
  const raw = rasters[0]
  const elevations = new Float32Array(width * height)
  const noDataValue = image.getGDALNoData()

  for (let i = 0; i < raw.length; i++) {
    elevations[i] = noDataValue !== null && noDataValue !== undefined && raw[i] === noDataValue ? 0 : raw[i]
  }

  onProgress({ stage: 'done', current: 1, total: 1, message: `Downloaded ${demType} (${width}x${height})` })
  const stats = computeStats(elevations)
  return { elevations, width, height, bounds, zoom: 0, ...stats }
}

// ─── NASA Earthdata / Copernicus DEM — free ───────────────────

async function fetchCopernicusDEMTile(lat, lon) {
  const latDir = lat >= 0 ? 'N' : 'S'
  const lonDir = lon >= 0 ? 'E' : 'W'
  const latStr = String(Math.abs(lat)).padStart(2, '0')
  const lonStr = String(Math.abs(lon)).padStart(3, '0')
  const tileName = `Copernicus_DSM_COG_10_${latDir}${latStr}_00_${lonDir}${lonStr}_00_DEM`
  const url = `https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/${tileName}/${tileName}.tif`

  const buffer = await downloadWithRetry(url, 3, 120000)
  if (buffer.length < 100) throw new Error(`Copernicus DEM tile ${latDir}${latStr}${lonDir}${lonStr} returned empty response`)

  const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength)
  const tiff = await fromArrayBuffer(arrayBuffer)
  const image = await tiff.getImage()
  const width = image.getWidth()
  const height = image.getHeight()
  const origin = image.getOrigin()
  const resolution = image.getResolution()
  const originLon = origin[0]
  const originLat = origin[1]
  const pixelWidth = Math.abs(resolution[0])
  const pixelHeight = Math.abs(resolution[1])
  const rasters = await image.readRasters()
  const raw = rasters[0]
  const elevations = new Float32Array(width * height)
  const noDataValue = image.getGDALNoData()

  for (let i = 0; i < raw.length; i++) {
    elevations[i] = noDataValue !== null && noDataValue !== undefined && raw[i] === noDataValue ? 0 : raw[i]
  }
  return { elevations, width, height, originLat, originLon, pixelWidth, pixelHeight }
}

function cropUsingGeoref(elevations, srcWidth, srcHeight, originLat, originLon, pixelWidth, pixelHeight, bounds) {
  const startCol = Math.max(0, Math.floor((bounds.west - originLon) / pixelWidth))
  const endCol = Math.min(srcWidth - 1, Math.ceil((bounds.east - originLon) / pixelWidth) - 1)
  const startRow = Math.max(0, Math.floor((originLat - bounds.north) / pixelHeight))
  const endRow = Math.min(srcHeight - 1, Math.ceil((originLat - bounds.south) / pixelHeight) - 1)
  const cropWidth = Math.max(1, endCol - startCol + 1)
  const cropHeight = Math.max(1, endRow - startRow + 1)
  const cropped = new Float32Array(cropWidth * cropHeight)
  for (let y = 0; y < cropHeight; y++) {
    for (let x = 0; x < cropWidth; x++) {
      cropped[y * cropWidth + x] = elevations[(startRow + y) * srcWidth + (startCol + x)]
    }
  }
  return { elevations: cropped, width: cropWidth, height: cropHeight }
}

async function fetchNasaEarthdataDEM(bounds, options) {
  const onProgress = options.onProgress || (() => {})
  const minLat = Math.floor(bounds.south)
  const maxLat = Math.floor(bounds.north)
  const minLon = Math.floor(bounds.west)
  const maxLon = Math.floor(bounds.east)
  const totalTiles = (maxLat - minLat + 1) * (maxLon - minLon + 1)

  onProgress({ stage: 'download', current: 0, total: totalTiles, message: `Downloading ${totalTiles} Copernicus DEM tiles...` })

  const tileData = []
  let completed = 0
  for (let lat = minLat; lat <= maxLat; lat++) {
    for (let lon = minLon; lon <= maxLon; lon++) {
      try {
        const tile = await fetchCopernicusDEMTile(lat, lon)
        tileData.push(tile)
      } catch (err) {
        // skip failed tile
      }
      completed++
      onProgress({ stage: 'download', current: completed, total: totalTiles, message: `Downloaded ${completed}/${totalTiles} Copernicus tiles` })
    }
  }

  if (tileData.length === 0) throw new Error('No Copernicus DEM tiles found for the selected area')

  if (tileData.length === 1) {
    const t = tileData[0]
    const result = cropUsingGeoref(t.elevations, t.width, t.height, t.originLat, t.originLon, t.pixelWidth, t.pixelHeight, bounds)
    const stats = computeStats(result.elevations)
    return { ...result, bounds, zoom: 0, ...stats }
  }

  const numTilesLat = maxLat - minLat + 1
  const numTilesLon = maxLon - minLon + 1
  const tileW = tileData[0].width
  const tileH = tileData[0].height
  const mergedWidth = numTilesLon * tileW
  const mergedHeight = numTilesLat * tileH
  const merged = new Float32Array(mergedWidth * mergedHeight)

  for (const t of tileData) {
    const tileCol = Math.round((t.originLon - minLon) / 1)
    const tileRow = Math.round((maxLat + 1 - t.originLat) / 1)
    const offsetX = tileCol * tileW
    const offsetY = tileRow * tileH
    for (let y = 0; y < t.height; y++) {
      for (let x = 0; x < t.width; x++) {
        const dx = offsetX + x
        const dy = offsetY + y
        if (dx < mergedWidth && dy < mergedHeight) {
          merged[dy * mergedWidth + dx] = t.elevations[y * t.width + x]
        }
      }
    }
  }

  const result = cropUsingGeoref(merged, mergedWidth, mergedHeight, maxLat + 1, minLon, tileData[0].pixelWidth, tileData[0].pixelHeight, bounds)
  const stats = computeStats(result.elevations)
  return { ...result, bounds, zoom: 0, ...stats }
}

// ─── GPXZ — requires API key ──────────────────────────────────

async function fetchGPXZDEM(bounds, options) {
  const apiKey = (options.apiKey || process.env.GPXZ_API_KEY || '').trim()
  if (!apiKey) throw new Error('GPXZ API key required. Sign up at https://www.gpxz.io/')
  const onProgress = options.onProgress || (() => {})

  const url = new URL('https://api.gpxz.io/v1/elevation/raster')
  url.searchParams.set('bbox_left', bounds.west.toString())
  url.searchParams.set('bbox_right', bounds.east.toString())
  url.searchParams.set('bbox_bottom', bounds.south.toString())
  url.searchParams.set('bbox_top', bounds.north.toString())
  url.searchParams.set('resolution_m', '5')
  url.searchParams.set('bathymetry', 'false')
  url.searchParams.set('api_key', apiKey)

  onProgress({ stage: 'download', current: 0, total: 1, message: 'Requesting DEM from GPXZ...' })

  const response = await fetch(url.toString(), {
    headers: { 'User-Agent': 'OpenGeoStudio/1.0', Accept: 'image/tiff' },
    signal: AbortSignal.timeout(30000),
  })

  if (!response.ok) {
    if (response.status === 401) throw new Error('GPXZ API key is invalid or expired.')
    if (response.status === 429) throw new Error('GPXZ rate limit exceeded. Free tier has 100 requests/day.')
    if (response.status === 402) throw new Error('GPXZ quota exceeded.')
    const errorText = await response.text().catch(() => 'Unknown error')
    throw new Error(`GPXZ API error (${response.status}): ${errorText}`)
  }

  const buffer = Buffer.from(await response.arrayBuffer())
  const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength)
  const tiff = await fromArrayBuffer(arrayBuffer)
  const image = await tiff.getImage()
  const rasters = await image.readRasters()
  const elevations = new Float32Array(rasters[0])

  onProgress({ stage: 'done', current: 1, total: 1, message: `Downloaded GPXZ DEM (${image.getWidth()}x${image.getHeight()})` })
  const stats = computeStats(elevations)
  return { elevations, width: image.getWidth(), height: image.getHeight(), bounds, zoom: 0, ...stats }
}

// ─── Main dispatcher ──────────────────────────────────────────

async function downloadTerrainDEM(bounds, options = {}) {
  const provider = options.provider || 'aws-terrarium'
  switch (provider) {
    case 'aws-terrarium':
    case 'mapzen':
      return fetchTerrariumDEM(bounds, options)
    case 'mapbox-terrain-rgb':
      return fetchMapboxDEM(bounds, options)
    case 'opentopo-srtmgl1':
    case 'opentopo-srtmgl3':
    case 'opentopo-aw3d30':
    case 'opentopo-cop30':
    case 'opentopo-nasadem':
    case 'opentopo-usgs10m':
      return fetchOpenTopoDEM(bounds, options)
    case 'nasa-earthdata':
      return fetchNasaEarthdataDEM(bounds, options)
    case 'gpxz':
      return fetchGPXZDEM(bounds, options)
    default:
      throw new Error(`Unknown DEM provider: ${provider}`)
  }
}

module.exports = { downloadTerrainDEM }
