/**
 * Tile coordinate math and export validation
 */

import {
  TILE_SIZE,
  MAX_TILES_PER_EXPORT,
  MAX_MEMORY_MB,
  isOpenTopoSource,
  type GeoBounds,
  type TileRange,
  type ExportOptions,
  type ExportValidation,
} from './types';

// ─── Tile Math ────────────────────────────────────────────────

export function lngToPixelX(lng: number, zoom: number): number {
  return ((lng + 180) / 360) * Math.pow(2, zoom) * TILE_SIZE;
}

export function latToPixelY(lat: number, zoom: number): number {
  const latRad = (lat * Math.PI) / 180;
  return (
    (1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) /
    2 * Math.pow(2, zoom) * TILE_SIZE
  );
}

export function lngToTileX(lng: number, zoom: number): number {
  return Math.floor(((lng + 180) / 360) * Math.pow(2, zoom));
}

export function latToTileY(lat: number, zoom: number): number {
  const latRad = (lat * Math.PI) / 180;
  return Math.floor(
    (1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2 *
      Math.pow(2, zoom)
  );
}

export function tileXToLng(x: number, zoom: number): number {
  return (x / Math.pow(2, zoom)) * 360 - 180;
}

export function tileYToLat(y: number, zoom: number): number {
  const n = Math.PI - (2 * Math.PI * y) / Math.pow(2, zoom);
  return (180 / Math.PI) * Math.atan(0.5 * (Math.exp(n) - Math.exp(-n)));
}

export function chooseZoom(bounds: GeoBounds, targetSize: number, maxZoom = 19): number {
  const widthDeg = bounds.east - bounds.west;
  const heightDeg = bounds.north - bounds.south;
  const minDimDeg = Math.min(widthDeg, heightDeg);
  const z = Math.log2((targetSize * 360) / (minDimDeg * TILE_SIZE));
  return Math.max(1, Math.min(maxZoom, Math.ceil(z)));
}

export function getTileRange(bounds: GeoBounds, zoom: number): TileRange {
  return {
    minX: lngToTileX(bounds.west, zoom),
    maxX: lngToTileX(bounds.east, zoom),
    minY: latToTileY(bounds.north, zoom),
    maxY: latToTileY(bounds.south, zoom),
    zoom,
  };
}

// ─── Validation ───────────────────────────────────────────────

export function validateExport(options: ExportOptions): ExportValidation {
  const errors: string[] = [];
  const warnings: string[] = [];

  const {
    bounds,
    heightmapSize = 1024,
    albedoSize = 1024,
    imageryZoom = 0,
    demSource = 'aws-terrarium',
  } = options;

  const demIsOpenTopo = isOpenTopoSource(demSource);

  if (bounds.west >= bounds.east) {
    errors.push('Invalid bounds: west must be less than east');
  }
  if (bounds.south >= bounds.north) {
    errors.push('Invalid bounds: south must be less than north');
  }
  if (bounds.west < -180 || bounds.east > 180) {
    errors.push('Invalid bounds: longitude out of range (-180 to 180)');
  }
  if (bounds.south < -90 || bounds.north > 90) {
    errors.push('Invalid bounds: latitude out of range (-90 to 90)');
  }

  const demZoom = chooseZoom(bounds, heightmapSize, 20);
  const imgZoom = imageryZoom > 0 ? imageryZoom : chooseZoom(bounds, albedoSize, 22);

  const demRange = getTileRange(bounds, demZoom);
  const imgRange = getTileRange(bounds, imgZoom);

  const demTilesX = demRange.maxX - demRange.minX + 1;
  const demTilesY = demRange.maxY - demRange.minY + 1;
  const imgTilesX = imgRange.maxX - imgRange.minX + 1;
  const imgTilesY = imgRange.maxY - imgRange.minY + 1;

  const demTileCount = demIsOpenTopo ? 1 : demTilesX * demTilesY;
  const imgTileCount = imgTilesX * imgTilesY;
  const totalTiles = demTileCount + imgTileCount;

  const demPixels = demIsOpenTopo
    ? heightmapSize * heightmapSize
    : demTilesX * TILE_SIZE * demTilesY * TILE_SIZE;
  const imgPixels = imgTilesX * TILE_SIZE * imgTilesY * TILE_SIZE;
  const estimatedMemoryBytes = (demPixels * 4) + (imgPixels * 4);

  if (totalTiles > MAX_TILES_PER_EXPORT) {
    errors.push(
      `Export too large: ${totalTiles} tiles exceeds maximum of ${MAX_TILES_PER_EXPORT}. ` +
      `Reduce bounding box area or use lower zoom levels.`
    );
  }

  if (estimatedMemoryBytes > MAX_MEMORY_MB * 1024 * 1024) {
    errors.push(
      `Export too large: estimated ${Math.round(estimatedMemoryBytes / (1024 * 1024))}MB ` +
      `exceeds memory limit of ${MAX_MEMORY_MB}MB. Reduce bounding box or resolution.`
    );
  }

  if (heightmapSize > 8192 || albedoSize > 8192) {
    warnings.push('Large resolution may cause high memory usage');
  }

  return {
    valid: errors.length === 0,
    errors,
    warnings,
    estimatedTiles: totalTiles,
    estimatedMemoryBytes,
  };
}
