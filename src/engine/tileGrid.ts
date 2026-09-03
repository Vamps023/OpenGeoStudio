import type { GeoBounds } from './crs'

export interface TileDefinition {
  row: number
  col: number
  bounds: GeoBounds
  center: { lng: number; lat: number }
  selected: boolean
}

export interface TileGrid {
  rows: number
  cols: number
  tileSizeKm: number
  tiles: TileDefinition[]
}

/**
 * Compute a square tile grid over the given bounds.
 * The selection is divided into a square grid where each tile is
 * approximately tileSizeKm × tileSizeKm.
 */
export function computeTileGrid(bounds: GeoBounds, tileSizeKm: number): TileGrid {
  const centerLat = (bounds.north + bounds.south) / 2
  const kmPerDegLat = 111.32
  const kmPerDegLng = 111.32 * Math.cos((centerLat * Math.PI) / 180)

  const tileSizeDegLat = tileSizeKm / kmPerDegLat
  const tileSizeDegLng = tileSizeKm / kmPerDegLng

  const widthDeg = bounds.east - bounds.west
  const heightDeg = bounds.north - bounds.south

  const requiredCols = Math.max(1, Math.ceil(widthDeg / tileSizeDegLng))
  const requiredRows = Math.max(1, Math.ceil(heightDeg / tileSizeDegLat))

  // Square grid — use the larger dimension
  const tilesNeeded = Math.max(requiredCols, requiredRows)
  const cols = tilesNeeded
  const rows = tilesNeeded

  const actualTileWidth = widthDeg / cols
  const actualTileHeight = heightDeg / rows

  const tiles: TileDefinition[] = []
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const west = bounds.west + c * actualTileWidth
      const east = c === cols - 1 ? bounds.east : bounds.west + (c + 1) * actualTileWidth
      const south = bounds.south + r * actualTileHeight
      const north = r === rows - 1 ? bounds.north : bounds.south + (r + 1) * actualTileHeight
      tiles.push({
        row: r,
        col: c,
        bounds: { west, south, east, north },
        center: { lng: (west + east) / 2, lat: (south + north) / 2 },
        selected: true,
      })
    }
  }

  return { rows, cols, tileSizeKm, tiles }
}

export function tileKey(row: number, col: number): string {
  return `${row},${col}`
}
