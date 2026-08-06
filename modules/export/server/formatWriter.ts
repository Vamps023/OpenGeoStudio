/**
 * Heightmap and albedo format writers + tile file naming
 */

import * as fs from 'fs';
import sharp from 'sharp';
import { writeGeoTIFF, type GeoTIFFCompression } from '../../terrain/shared/geotiff-writer';
import {
  type GeoBounds,
  type HeightmapFormat,
  type AlbedoFormat,
  type ElevationMetadata,
  assertNever,
} from './types';

// ─── Tile Naming ──────────────────────────────────────────────

export function generateTileFileNames(
  tileRow: number,
  tileCol: number,
  heightmapFormat: HeightmapFormat,
  albedoFormat: AlbedoFormat
): { heightmap?: string; albedo: string } {
  const heightmapExt = getHeightmapExtension(heightmapFormat);
  const albedoExt = getAlbedoExtension(albedoFormat);

  return {
    heightmap: heightmapFormat === 'none' ? undefined : `tile_${tileRow}_${tileCol}_heightmap.${heightmapExt}`,
    albedo: `tile_${tileRow}_${tileCol}_albedo.${albedoExt}`,
  };
}

export function getHeightmapExtension(format: HeightmapFormat): string {
  switch (format) {
    case 'png': return 'png';
    case 'r16': return 'r16';
    case 'float32': return 'tif';
    case 'geotiff': return 'tif';
    case 'dem': return 'tif';
    case 'none': return '';
    default: return assertNever(format);
  }
}

export function getAlbedoExtension(format: AlbedoFormat): string {
  switch (format) {
    case 'geotiff': return 'tif';
    case 'png': return 'png';
    default: return assertNever(format);
  }
}

export function getHeightmapFormatLabel(format: HeightmapFormat): string {
  switch (format) {
    case 'float32': return 'Float32 GeoTIFF';
    case 'geotiff': return 'UInt16 GeoTIFF (normalized)';
    case 'dem': return 'DEM (Int16 GeoTIFF)';
    case 'r16': return 'R16 (Raw 16-bit)';
    case 'png': return 'PNG (16-bit grayscale)';
    case 'none': return 'None';
    default: return assertNever(format);
  }
}

// ─── Heightmap Writers ────────────────────────────────────────

export async function writeHeightmapPNG(
  elevations: Float32Array,
  width: number,
  height: number,
  outputPath: string,
  metadata: ElevationMetadata
): Promise<void> {
  const { min, range } = metadata;

  const uint16 = new Uint16Array(width * height);
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i];
    if (isNaN(v) || v === -Infinity) v = min;
    const norm = Math.round(((v - min) / (range || 1)) * 65535);
    uint16[i] = Math.max(0, Math.min(65535, norm));
  }

  await sharp(uint16, { raw: { width, height, channels: 1 } })
    .png({ compressionLevel: 9 })
    .toFile(outputPath);
}

export async function writeHeightmapR16(
  elevations: Float32Array,
  width: number,
  height: number,
  outputPath: string,
  metadata: ElevationMetadata
): Promise<void> {
  const { min, range } = metadata;

  const buf = Buffer.allocUnsafe(width * height * 2);
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i];
    if (isNaN(v) || v === -Infinity) v = min;
    const norm = Math.round(((v - min) / (range || 1)) * 65535);
    buf.writeUInt16LE(Math.max(0, Math.min(65535, norm)), i * 2);
  }

  await fs.promises.writeFile(outputPath, buf);
}

export async function writeHeightmapGeoTIFFInt16(
  elevations: Float32Array,
  width: number,
  height: number,
  bounds: GeoBounds,
  outputPath: string,
  compression: GeoTIFFCompression = 'none',
  crs: string = 'EPSG:4326'
): Promise<void> {
  let minElev = Infinity;
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i];
    if (isFinite(v) && v < minElev) minElev = v;
  }
  if (minElev === Infinity) minElev = 0;

  const int16 = new Int16Array(width * height);
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i];
    if (isNaN(v) || v === -Infinity) v = minElev;
    int16[i] = Math.round(Math.max(-32768, Math.min(32767, v)));
  }

  const buf = writeGeoTIFF(int16, {
    width,
    height,
    bitsPerSample: 16,
    sampleFormat: 2,
    samplesPerPixel: 1,
    photometricInterpretation: 1,
    bounds,
    compression,
    rasterType: 'point',
    crs,
  });

  await fs.promises.writeFile(outputPath, buf);
}

export async function writeHeightmapGeoTIFFUint16(
  elevations: Float32Array,
  width: number,
  height: number,
  bounds: GeoBounds,
  outputPath: string,
  compression: GeoTIFFCompression = 'none',
  crs: string = 'EPSG:4326'
): Promise<void> {
  let min = Infinity;
  let max = -Infinity;
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i];
    if (isNaN(v) || v === -Infinity) continue;
    if (v < min) min = v;
    if (v > max) max = v;
  }
  if (min === Infinity) { min = 0; max = 0; }
  const range = max - min;

  const uint16 = new Uint16Array(width * height);
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i];
    if (isNaN(v) || v === -Infinity) v = min;
    const norm = Math.round(((v - min) / (range || 1)) * 65535);
    uint16[i] = Math.max(0, Math.min(65535, norm));
  }

  const buf = writeGeoTIFF(uint16, {
    width,
    height,
    bitsPerSample: 16,
    sampleFormat: 1,
    samplesPerPixel: 1,
    photometricInterpretation: 1,
    bounds,
    compression,
    rasterType: 'point',
    crs,
  });

  await fs.promises.writeFile(outputPath, buf);
}

export async function writeHeightmapGeoTIFFFloat32(
  elevations: Float32Array,
  width: number,
  height: number,
  bounds: GeoBounds,
  outputPath: string,
  compression: GeoTIFFCompression = 'none',
  crs: string = 'EPSG:4326'
): Promise<void> {
  let minElev = Infinity;
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i];
    if (isFinite(v) && v < minElev) minElev = v;
  }
  if (minElev === Infinity) minElev = 0;

  const float32 = new Float32Array(width * height);
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i];
    if (isNaN(v) || v === -Infinity) v = minElev;
    float32[i] = v;
  }

  const buf = writeGeoTIFF(float32, {
    width,
    height,
    bitsPerSample: 32,
    sampleFormat: 3,
    samplesPerPixel: 1,
    photometricInterpretation: 1,
    bounds,
    compression,
    rasterType: 'point',
    crs,
  });

  await fs.promises.writeFile(outputPath, buf);
}

// ─── Albedo Writers ───────────────────────────────────────────

export async function writeAlbedoPNG(
  rgba: Buffer,
  width: number,
  height: number,
  outputPath: string
): Promise<void> {
  await sharp(rgba, { raw: { width, height, channels: 4 } })
    .removeAlpha()
    .png({ compressionLevel: 9 })
    .toFile(outputPath);
}

export async function writeAlbedoGeoTIFF(
  rgba: Buffer,
  width: number,
  height: number,
  bounds: GeoBounds,
  outputPath: string,
  compression: GeoTIFFCompression = 'none',
  crs: string = 'EPSG:4326'
): Promise<void> {
  const rgb = Buffer.allocUnsafe(width * height * 3);
  for (let i = 0; i < width * height; i++) {
    rgb[i * 3] = rgba[i * 4];
    rgb[i * 3 + 1] = rgba[i * 4 + 1];
    rgb[i * 3 + 2] = rgba[i * 4 + 2];
  }

  const buf = writeGeoTIFF(rgb, {
    width,
    height,
    bitsPerSample: 8,
    sampleFormat: 1,
    samplesPerPixel: 3,
    photometricInterpretation: 2,
    bounds,
    compression,
    crs,
  });

  await fs.promises.writeFile(outputPath, buf);
}
