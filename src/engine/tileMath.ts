/**
 * Tile coordinate math for Web Mercator (EPSG:3857) tile pyramids.
 * Used for DEM tile download (Terrarium, Mapbox Terrain-RGB).
 */

export const TILE_SIZE = 256

export interface GeoBounds {
  west: number
  south: number
  east: number
  north: number
}

export interface TileRange {
  minX: number
  maxX: number
  minY: number
  maxY: number
  zoom: number
}

export function lngToTileX(lng: number, zoom: number): number {
  return Math.floor(((lng + 180) / 360) * Math.pow(2, zoom))
}

export function latToTileY(lat: number, zoom: number): number {
  const latRad = (lat * Math.PI) / 180
  return Math.floor(
    ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * Math.pow(2, zoom),
  )
}

export function tileXToLng(x: number, zoom: number): number {
  return (x / Math.pow(2, zoom)) * 360 - 180
}

export function tileYToLat(y: number, zoom: number): number {
  const n = Math.PI - (2 * Math.PI * y) / Math.pow(2, zoom)
  return (180 / Math.PI) * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)))
}

export function lngToPixelX(lng: number, zoom: number): number {
  return ((lng + 180) / 360) * Math.pow(2, zoom) * TILE_SIZE
}

export function latToPixelY(lat: number, zoom: number): number {
  const latRad = (lat * Math.PI) / 180
  return ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * Math.pow(2, zoom) * TILE_SIZE
}

/**
 * Choose an appropriate zoom level for a target pixel size.
 */
export function chooseZoom(bounds: GeoBounds, targetSize: number, maxZoom = 19): number {
  const widthDeg = bounds.east - bounds.west
  const heightDeg = bounds.north - bounds.south
  const minDimDeg = Math.min(widthDeg, heightDeg)
  const z = Math.log2((targetSize * 360) / (minDimDeg * TILE_SIZE))
  return Math.max(1, Math.min(maxZoom, Math.ceil(z)))
}

/**
 * Get the tile range covering a bounding box at a given zoom.
 */
export function getTileRange(bounds: GeoBounds, zoom: number): TileRange {
  return {
    minX: lngToTileX(bounds.west, zoom),
    maxX: lngToTileX(bounds.east, zoom),
    minY: latToTileY(bounds.north, zoom),
    maxY: latToTileY(bounds.south, zoom),
    zoom,
  }
}

/**
 * Count tiles in a range.
 */
export function tileCount(range: TileRange): number {
  const x = range.maxX - range.minX + 1
  const y = range.maxY - range.minY + 1
  return x * y
}
