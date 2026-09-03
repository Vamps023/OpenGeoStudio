/**
 * Imagery (albedo) downloader — satellite tile fetch + merge + crop + resize.
 *
 * Sources:
 *   - arcgis   — Esri World Imagery (free)
 *   - google   — Google Satellite (free)
 *   - mapbox   — Mapbox Satellite (requires token)
 *   - maptiler — MapTiler Satellite (requires API key)
 */

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
const { downloadWithRetry } = require('./downloader.cjs')

const IMAGERY_SOURCES = {
  arcgis: {
    label: 'Esri World Imagery (Free)',
    needsKey: false,
    getUrl: (x, y, z) => `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${z}/${y}/${x}`,
  },
  google: {
    label: 'Google Satellite (Free)',
    needsKey: false,
    getUrl: (x, y, z) => `https://mt1.google.com/vt/lyrs=s&x=${x}&y=${y}&z=${z}`,
  },
  mapbox: {
    label: 'Mapbox Satellite',
    needsKey: true,
    keyLabel: 'Mapbox Access Token',
    getUrl: (x, y, z, token) => `https://api.mapbox.com/v4/mapbox.satellite/${z}/${x}/${y}@2x.png?access_token=${token}`,
  },
  maptiler: {
    label: 'MapTiler Satellite',
    needsKey: true,
    keyLabel: 'MapTiler API Key',
    getUrl: (x, y, z, key) => `https://api.maptiler.com/tiles/satellite/${z}/${x}/${y}.jpg?key=${key}`,
  },
}

function getImageryUrl(source, x, y, z, apiKey) {
  const cfg = IMAGERY_SOURCES[source]
  if (!cfg) throw new Error(`Unknown imagery source: ${source}`)
  if (cfg.needsKey && !apiKey) {
    throw new Error(`${cfg.keyLabel} required for ${cfg.label}`)
  }
  return cfg.getUrl(x, y, z, apiKey)
}

async function mergeImageryTiles(tiles, range, onProgress) {
  const tilesX = range.maxX - range.minX + 1
  const tilesY = range.maxY - range.minY + 1
  const canvasW = tilesX * TILE_SIZE
  const canvasH = tilesY * TILE_SIZE
  const requiredBytes = canvasW * canvasH * 4
  const MAX_MERGE_BYTES = 4096 * 1024 * 1024
  if (requiredBytes > MAX_MERGE_BYTES) {
    throw new Error(`Imagery merge too large (${canvasW}x${canvasH}). Reduce area or zoom.`)
  }

  const canvas = Buffer.alloc(requiredBytes)
  let processed = 0
  for (const tile of tiles) {
    const { data, info } = await sharp(tile.buffer).raw().ensureAlpha().toBuffer({ resolveWithObject: true })
    const tileW = info.width
    const tileH = info.height
    const offsetX = (tile.x - range.minX) * TILE_SIZE
    const offsetY = (tile.y - range.minY) * TILE_SIZE
    for (let ty = 0; ty < tileH; ty++) {
      const srcRowStart = ty * tileW * 4
      const dstRowStart = ((offsetY + ty) * canvasW + offsetX) * 4
      for (let tx = 0; tx < tileW; tx++) {
        const srcIdx = srcRowStart + tx * 4
        const dstIdx = dstRowStart + tx * 4
        canvas[dstIdx] = data[srcIdx]
        canvas[dstIdx + 1] = data[srcIdx + 1]
        canvas[dstIdx + 2] = data[srcIdx + 2]
        canvas[dstIdx + 3] = data[srcIdx + 3]
      }
    }
    processed++
    if (onProgress) onProgress(processed, tiles.length)
  }
  return { buffer: canvas, width: canvasW, height: canvasH }
}

async function cropImagery(merged, fullW, fullH, bounds, range, zoom) {
  const pxWest = lngToPixelX(bounds.west, zoom)
  const pxEast = lngToPixelX(bounds.east, zoom)
  const pxNorth = latToPixelY(bounds.north, zoom)
  const pxSouth = latToPixelY(bounds.south, zoom)
  const fullPxWest = lngToPixelX(tileXToLng(range.minX, zoom), zoom)
  const fullPxNorth = latToPixelY(tileYToLat(range.minY, zoom), zoom)
  const left = Math.max(0, Math.round(pxWest - fullPxWest))
  const top = Math.max(0, Math.round(pxNorth - fullPxNorth))
  const width = Math.max(1, Math.round(Math.abs(pxEast - pxWest)) + 1)
  const height = Math.max(1, Math.round(Math.abs(pxSouth - pxNorth)) + 1)
  const cropped = await sharp(merged, { raw: { width: fullW, height: fullH, channels: 4 } })
    .extract({ left, top, width, height })
    .raw()
    .toBuffer()
  return { buffer: cropped, width, height }
}

async function resizeImagery(buffer, srcW, srcH, dstW, dstH) {
  const isDownsampling = srcW > dstW || srcH > dstH
  const kernel = isDownsampling ? sharp.kernel.lanczos3 : sharp.kernel.linear
  return sharp(buffer, { raw: { width: srcW, height: srcH, channels: 4 } })
    .resize(dstW, dstH, { kernel, fit: 'fill' })
    .raw()
    .toBuffer()
}

async function downloadImagery(bounds, options) {
  const source = options.source || 'arcgis'
  const apiKey = options.apiKey
  const targetSize = options.targetSize || 1024
  const maxZoom = options.maxZoom || 18
  const onProgress = options.onProgress || (() => {})

  const zoom = options.zoom > 0 ? options.zoom : chooseZoom(bounds, targetSize, maxZoom)
  const range = getTileRange(bounds, zoom)
  const tilesX = range.maxX - range.minX + 1
  const tilesY = range.maxY - range.minY + 1
  const totalTiles = tilesX * tilesY
  if (totalTiles > 1024) throw new Error(`Too many imagery tiles (${totalTiles}). Reduce area or zoom.`)

  onProgress({ stage: 'download_imagery', current: 0, total: totalTiles, message: `Downloading ${totalTiles} imagery tiles at zoom ${zoom}...` })

  const tileResults = []
  let completed = 0
  let index = 0
  async function worker() {
    while (index < totalTiles) {
      const taskIndex = index++
      const x = range.minX + (taskIndex % tilesX)
      const y = range.minY + Math.floor(taskIndex / tilesX)
      const url = getImageryUrl(source, x, y, zoom, apiKey)
      try {
        const buffer = await downloadWithRetry(url, 3, 30000)
        tileResults.push({ x, y, buffer })
      } catch {
        // Use black fallback
        const black = await sharp({
          create: { width: TILE_SIZE, height: TILE_SIZE, channels: 4, background: { r: 0, g: 0, b: 0, alpha: 255 } },
        }).png().toBuffer()
        tileResults.push({ x, y, buffer: black })
      }
      completed++
      onProgress({ stage: 'download_imagery', current: completed, total: totalTiles, message: `Downloaded ${completed}/${totalTiles} imagery tiles` })
    }
  }
  await Promise.all(Array.from({ length: Math.min(6, totalTiles) }, () => worker()))
  if (tileResults.length === 0) throw new Error('All imagery tiles failed to download.')

  onProgress({ stage: 'process_imagery', current: 0, total: tileResults.length, message: 'Merging imagery tiles...' })
  const merged = await mergeImageryTiles(tileResults, range, (c, t) => {
    onProgress({ stage: 'process_imagery', current: c, total: t, message: `Merged ${c}/${t} imagery tiles` })
  })
  const cropped = await cropImagery(merged.buffer, merged.width, merged.height, bounds, range, zoom)
  const resized = await resizeImagery(cropped.buffer, cropped.width, cropped.height, targetSize, targetSize)
  onProgress({ stage: 'done', current: 1, total: 1, message: `Imagery ready (${targetSize}x${targetSize})` })
  return { buffer: resized, width: targetSize, height: targetSize, bounds, zoom }
}

module.exports = { downloadImagery, IMAGERY_SOURCES }
