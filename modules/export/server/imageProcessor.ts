/**
 * Image processing: merge tiles, crop, resize, elevation metadata
 */

import sharp from 'sharp';
import {
  TILE_SIZE,
  type GeoBounds,
  type TileRange,
  type ElevationMetadata,
} from './types';
import { lngToPixelX, latToPixelY, tileXToLng, tileYToLat } from './tileMath';

// ─── DEM Merge & Crop ─────────────────────────────────────────

export function mergeDEMTiles(
  tiles: Array<{ x: number; y: number; elevations: Float32Array }>,
  range: TileRange
): { elevations: Float32Array; width: number; height: number } {
  const tilesX = range.maxX - range.minX + 1;
  const tilesY = range.maxY - range.minY + 1;

  const firstTile = tiles[0];
  if (!firstTile) {
    return { elevations: new Float32Array(0), width: 0, height: 0 };
  }
  const tilePixelSize = Math.round(Math.sqrt(firstTile.elevations.length));
  if (tilePixelSize * tilePixelSize !== firstTile.elevations.length) {
    // no-op
  }

  const effectiveTileSize = (tilePixelSize * tilePixelSize === firstTile.elevations.length) ? tilePixelSize : TILE_SIZE;
  const w = tilesX * effectiveTileSize;
  const h = tilesY * effectiveTileSize;
  const elevations = new Float32Array(w * h);

  for (const tile of tiles) {
    const offsetX = (tile.x - range.minX) * effectiveTileSize;
    const offsetY = (tile.y - range.minY) * effectiveTileSize;

    const tileW = Math.round(Math.sqrt(tile.elevations.length));
    const tileH = (tileW * tileW === tile.elevations.length) ? tileW : effectiveTileSize;

    for (let ty = 0; ty < tileH; ty++) {
      const srcStart = ty * tileW;
      const dstStart = (offsetY + ty) * w + offsetX;
      elevations.set(tile.elevations.subarray(srcStart, srcStart + tileW), dstStart);
    }
  }

  return { elevations, width: w, height: h };
}

export function cropDEM(
  elevations: Float32Array,
  fullW: number,
  fullH: number,
  bounds: GeoBounds,
  range: TileRange,
  zoom: number
): { elevations: Float32Array; width: number; height: number } {
  const pxWest = lngToPixelX(bounds.west, zoom);
  const pxEast = lngToPixelX(bounds.east, zoom);
  const pxNorth = latToPixelY(bounds.north, zoom);
  const pxSouth = latToPixelY(bounds.south, zoom);

  const fullPxWest = lngToPixelX(tileXToLng(range.minX, zoom), zoom);
  const fullPxNorth = latToPixelY(tileYToLat(range.minY, zoom), zoom);

  const left = Math.round(pxWest - fullPxWest);
  const top = Math.round(pxNorth - fullPxNorth);
  const width = Math.max(1, Math.round(Math.abs(pxEast - pxWest)) + 1);
  const height = Math.max(1, Math.round(Math.abs(pxSouth - pxNorth)) + 1);

  if (width <= 0 || height <= 0) {
    throw new Error(`[cropDEM] Invalid crop dimensions: width=${width}, height=${height}. Check bounds coordinates.`);
  }

  const cropped = new Float32Array(width * height);

  for (let y = 0; y < height; y++) {
    const srcY = top + y;
    const dstRowStart = y * width;
    const srcRowStart = srcY * fullW;

    for (let x = 0; x < width; x++) {
      const srcX = left + x;
      if (srcX >= 0 && srcX < fullW && srcY >= 0 && srcY < fullH) {
        cropped[dstRowStart + x] = elevations[srcRowStart + srcX];
      } else {
        cropped[dstRowStart + x] = 0;
      }
    }
  }

  return { elevations: cropped, width, height };
}

// ─── Imagery Merge & Crop ─────────────────────────────────────

export async function mergeImageryTilesChunked(
  tiles: Array<{ x: number; y: number; buffer: Buffer }>,
  range: TileRange,
  onProgress?: (completed: number, total: number) => void
): Promise<Buffer> {
  const tilesX = range.maxX - range.minX + 1;
  const tilesY = range.maxY - range.minY + 1;
  const canvasW = tilesX * TILE_SIZE;
  const canvasH = tilesY * TILE_SIZE;

  const requiredBytes = canvasW * canvasH * 4;
  const MAX_MERGE_BYTES = 4096 * 1024 * 1024;
  if (requiredBytes > MAX_MERGE_BYTES) {
    throw new Error(
      `[Export] Imagery merge would require ${Math.round(requiredBytes / (1024 * 1024))}MB ` +
      `(${canvasW}x${canvasH} pixels), exceeding the ${Math.round(MAX_MERGE_BYTES / (1024 * 1024))}MB safety limit. ` +
      `Reduce the export area, lower the imagery zoom level, or use fewer tiles.`
    );
  }

  const canvas = Buffer.alloc(requiredBytes);
  canvas.fill(0);

  let processed = 0;

  for (const tile of tiles) {
    const { data, info } = await sharp(tile.buffer)
      .raw()
      .ensureAlpha()
      .toBuffer({ resolveWithObject: true });

    const tileW = info.width;
    const tileH = info.height;
    const offsetX = (tile.x - range.minX) * TILE_SIZE;
    const offsetY = (tile.y - range.minY) * TILE_SIZE;

    for (let ty = 0; ty < tileH; ty++) {
      const srcRowStart = ty * tileW * 4;
      const dstRowStart = ((offsetY + ty) * canvasW + offsetX) * 4;

      for (let tx = 0; tx < tileW; tx++) {
        const srcIdx = srcRowStart + tx * 4;
        const dstIdx = dstRowStart + tx * 4;
        canvas[dstIdx] = data[srcIdx];
        canvas[dstIdx + 1] = data[srcIdx + 1];
        canvas[dstIdx + 2] = data[srcIdx + 2];
        canvas[dstIdx + 3] = data[srcIdx + 3];
      }
    }

    processed++;
    if (onProgress) {
      onProgress(processed, tiles.length);
    }
  }

  return canvas;
}

export async function cropImagery(
  merged: Buffer,
  fullW: number,
  fullH: number,
  bounds: GeoBounds,
  range: TileRange,
  zoom: number
): Promise<{ buffer: Buffer; width: number; height: number }> {
  const pxWest = lngToPixelX(bounds.west, zoom);
  const pxEast = lngToPixelX(bounds.east, zoom);
  const pxNorth = latToPixelY(bounds.north, zoom);
  const pxSouth = latToPixelY(bounds.south, zoom);

  const fullPxWest = lngToPixelX(tileXToLng(range.minX, zoom), zoom);
  const fullPxNorth = latToPixelY(tileYToLat(range.minY, zoom), zoom);

  const left = Math.round(pxWest - fullPxWest);
  const top = Math.round(pxNorth - fullPxNorth);
  const width = Math.max(1, Math.round(Math.abs(pxEast - pxWest)) + 1);
  const height = Math.max(1, Math.round(Math.abs(pxSouth - pxNorth)) + 1);

  const cropped = await sharp(merged, { raw: { width: fullW, height: fullH, channels: 4 } })
    .extract({ left, top, width, height })
    .raw()
    .toBuffer();

  return { buffer: cropped, width, height };
}

// ─── Resize ───────────────────────────────────────────────────

export function resizeDEM(
  elevations: Float32Array,
  srcW: number,
  srcH: number,
  dstW: number,
  dstH: number
): Float32Array {
  if (dstW < 2 || dstH < 2) {
    throw new Error(`[resizeDEM] Target dimensions must be at least 2x2, got ${dstW}x${dstH}`);
  }
  const result = new Float32Array(dstW * dstH);

  const sxScale = (srcW - 1) / (dstW - 1 || 1);
  const syScale = (srcH - 1) / (dstH - 1 || 1);

  for (let y = 0; y < dstH; y++) {
    const sy = y * syScale;
    const sy0 = Math.floor(sy);
    const sy1 = Math.min(sy0 + 1, srcH - 1);
    const fy = sy - sy0;

    for (let x = 0; x < dstW; x++) {
      const sx = x * sxScale;
      const sx0 = Math.floor(sx);
      const sx1 = Math.min(sx0 + 1, srcW - 1);
      const fx = sx - sx0;

      const v00 = elevations[sy0 * srcW + sx0];
      const v01 = elevations[sy0 * srcW + sx1];
      const v10 = elevations[sy1 * srcW + sx0];
      const v11 = elevations[sy1 * srcW + sx1];

      const top = v00 * (1 - fx) + v01 * fx;
      const bot = v10 * (1 - fx) + v11 * fx;
      result[y * dstW + x] = top * (1 - fy) + bot * fy;
    }
  }

  return result;
}

export async function resizeImagery(
  buffer: Buffer,
  srcW: number,
  srcH: number,
  dstW: number,
  dstH: number
): Promise<Buffer> {
  const isDownsampling = srcW > dstW || srcH > dstH;
  const kernel = isDownsampling ? sharp.kernel.lanczos3 : sharp.kernel.linear;

  return sharp(buffer, { raw: { width: srcW, height: srcH, channels: 4 } })
    .resize(dstW, dstH, { kernel, fit: 'fill' })
    .raw()
    .toBuffer();
}

// ─── Elevation Metadata ───────────────────────────────────────

export function computeElevationMetadata(elevations: Float32Array): ElevationMetadata {
  let min = Infinity;
  let max = -Infinity;
  let hasNoData = false;

  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i];

    if (!isFinite(v) || isNaN(v) || v === -Infinity || v === Infinity) {
      hasNoData = true;
      continue;
    }

    if (v < min) min = v;
    if (v > max) max = v;
  }

  if (min === Infinity) {
    return {
      min: 0,
      max: 0,
      range: 0,
      hasNoData: true,
    };
  }

  return {
    min,
    max,
    range: max - min,
    hasNoData,
  };
}
