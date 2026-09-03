/**
 * Tile coordinate math for Web Mercator tile pyramids.
 * CommonJS version for Electron main process.
 */

const TILE_SIZE = 256

function lngToTileX(lng, zoom) {
  return Math.floor(((lng + 180) / 360) * Math.pow(2, zoom))
}

function latToTileY(lat, zoom) {
  const latRad = (lat * Math.PI) / 180
  return Math.floor(
    ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * Math.pow(2, zoom),
  )
}

function tileXToLng(x, zoom) {
  return (x / Math.pow(2, zoom)) * 360 - 180
}

function tileYToLat(y, zoom) {
  const n = Math.PI - (2 * Math.PI * y) / Math.pow(2, zoom)
  return (180 / Math.PI) * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)))
}

function lngToPixelX(lng, zoom) {
  return ((lng + 180) / 360) * Math.pow(2, zoom) * TILE_SIZE
}

function latToPixelY(lat, zoom) {
  const latRad = (lat * Math.PI) / 180
  return ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * Math.pow(2, zoom) * TILE_SIZE
}

function chooseZoom(bounds, targetSize, maxZoom) {
  const widthDeg = bounds.east - bounds.west
  const heightDeg = bounds.north - bounds.south
  const minDimDeg = Math.min(widthDeg, heightDeg)
  const z = Math.log2((targetSize * 360) / (minDimDeg * TILE_SIZE))
  return Math.max(1, Math.min(maxZoom, Math.ceil(z)))
}

function getTileRange(bounds, zoom) {
  return {
    minX: lngToTileX(bounds.west, zoom),
    maxX: lngToTileX(bounds.east, zoom),
    minY: latToTileY(bounds.north, zoom),
    maxY: latToTileY(bounds.south, zoom),
    zoom,
  }
}

module.exports = {
  TILE_SIZE,
  lngToTileX,
  latToTileY,
  tileXToLng,
  tileYToLat,
  lngToPixelX,
  latToPixelY,
  chooseZoom,
  getTileRange,
}
