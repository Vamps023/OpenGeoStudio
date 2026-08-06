/**
 * Export types, constants, and shared helpers
 *
 * Domain types (GeoBounds, HeightmapFormat, DEMSource, etc.) are imported from
 * the canonical shared types to avoid divergence.
 */

import type { GeoTIFFCompression } from '../../terrain/shared/geotiff-writer';
import type {
  GeoBounds,
  HeightmapFormat,
  AlbedoFormat,
  DEMSource,
  ImagerySource,
  MaskSettings,
} from '../../../shared/types/terrain';

// Re-export shared types for convenience within the export module
export type { GeoBounds, HeightmapFormat, AlbedoFormat, DEMSource, ImagerySource, MaskSettings };

// ─── Constants ─────────────────────────────────────────────────

export const TILE_SIZE = 256;
export const MAX_CONCURRENT_DOWNLOADS = 6;
export const MAX_TILES_PER_EXPORT = 16384;
export const MAX_MEMORY_MB = 8192;

export interface TileRange {
  minX: number;
  maxX: number;
  minY: number;
  maxY: number;
  zoom: number;
}

export interface ExportValidation {
  valid: boolean;
  errors: string[];
  warnings: string[];
  estimatedTiles: number;
  estimatedMemoryBytes: number;
}

export interface ElevationMetadata {
  min: number;
  max: number;
  range: number;
  hasNoData: boolean;
}

export interface ExportOptions {
  sessionId: string;
  outputPath: string;
  preset: string;
  bounds: GeoBounds;
  heightmapFormat: HeightmapFormat;
  albedoFormat: AlbedoFormat;
  heightmapSize?: number;
  albedoSize?: number;
  demSource?: DEMSource;
  imagerySource?: ImagerySource;
  imageryZoom?: number;
  tileRow?: number;
  tileCol?: number;
  compression?: GeoTIFFCompression;
  opentopographyApiKey?: string;
  mapboxAccessToken?: string;
  maptilerApiKey?: string;
  gpxzApiKey?: string;
  stadiaApiKey?: string;
  maskSettings?: MaskSettings;
  downloadDem?: boolean;
  crsSource?: string;
  gladArdInterval?: number;
  cancellationToken?: CancellationToken;
  /** Generate 3D geometry files (buildings, roads, signs) from OSM. Default: true */
  enable3D?: boolean;
}

// ─── Cancellation Support ─────────────────────────────────────

export interface CancellationToken {
  cancelled: boolean;
}

export function createCancellationToken(): { token: CancellationToken; cancel: () => void } {
  const token: CancellationToken = { cancelled: false };
  return {
    token,
    cancel: () => { token.cancelled = true; },
  };
}

export function checkCancellation(token?: CancellationToken, stage?: string): void {
  if (token?.cancelled) {
    throw new Error(`Export cancelled${stage ? ` during ${stage}` : ''}`);
  }
}

// ─── Helpers ──────────────────────────────────────────────────

export function assertNever(x: never): never {
  throw new Error(`Unexpected value: ${x}`);
}

export function redactUrl(url: string): string {
  return url
    .replace(/API_Key=[^&]+/gi, 'API_Key=****')
    .replace(/access_token=[^&]+/gi, 'access_token=****')
    .replace(/api_key=[^&]+/gi, 'api_key=****')
    .replace(/apikey=[^&]+/gi, 'apikey=****')
    .replace(/\btoken=[^&]+/gi, 'token=****')
    .replace(/\bkey=[^&]+/gi, 'key=****');
}

// ─── DEM Source Helpers ───────────────────────────────────────

export const OPENTOPO_SOURCES: DEMSource[] = [
  'opentopo-srtmgl1',
  'opentopo-srtmgl3',
  'opentopo-aw3d30',
  'opentopo-cop30',
  'opentopo-nasadem',
  'opentopo-usgs10m',
];

export function isOpenTopoSource(source: DEMSource): boolean {
  return OPENTOPO_SOURCES.includes(source);
}

export function getOpenTopoDemType(source: DEMSource): string {
  switch (source) {
    case 'opentopo-srtmgl1': return 'SRTMGL1';
    case 'opentopo-srtmgl3': return 'SRTMGL3';
    case 'opentopo-aw3d30': return 'AW3D30';
    case 'opentopo-cop30': return 'COP30';
    case 'opentopo-nasadem': return 'NASADEM';
    case 'opentopo-usgs10m': return 'USGS10m';
    case 'aws-terrarium':
    case 'mapzen':
    case 'mapbox-terrain-rgb':
    case 'nasa-earthdata':
    case 'gpxz':
    case 'glad-srtm':
    case 'local-file':
      throw new Error(`getOpenTopoDemType called with non-OpenTopo source: ${source}`);
    default: return assertNever(source);
  }
}

export function getDEMSourceInfo(source: DEMSource): { name: string; attribution: string } {
  switch (source) {
    case 'aws-terrarium':
      return { name: 'AWS Terrain Tiles (Terrarium)', attribution: 'Mapzen / AWS' };
    case 'mapzen':
      return { name: 'Mapzen Terrain Tiles', attribution: 'Mapzen' };
    case 'mapbox-terrain-rgb':
      return { name: 'Mapbox Terrain RGB', attribution: 'Mapbox' };
    case 'opentopo-srtmgl1':
      return { name: 'SRTM GL1 (30m)', attribution: 'NASA / OpenTopography' };
    case 'opentopo-srtmgl3':
      return { name: 'SRTM GL3 (90m)', attribution: 'NASA / OpenTopography' };
    case 'opentopo-aw3d30':
      return { name: 'ALOS World 3D (30m)', attribution: 'JAXA / OpenTopography' };
    case 'opentopo-cop30':
      return { name: 'Copernicus GLO-30', attribution: 'ESA / OpenTopography' };
    case 'opentopo-nasadem':
      return { name: 'NASADEM (30m)', attribution: 'NASA / OpenTopography' };
    case 'opentopo-usgs10m':
      return { name: 'USGS 3DEP (10m)', attribution: 'USGS / OpenTopography' };
    case 'nasa-earthdata':
      return { name: 'Copernicus GLO-30 (30m) via AWS', attribution: 'ESA / Copernicus' };
    case 'gpxz':
      return { name: 'GPXZ (High-res)', attribution: 'GPXZ - LiDAR & Copernicus' };
    case 'glad-srtm':
      return { name: 'GLAD SRTM (30m)', attribution: 'UMD GLAD / NASA SRTMGL1' };
    case 'local-file':
      return { name: 'Local DEM File', attribution: 'User-provided GeoTIFF' };
    default:
      return assertNever(source);
  }
}

export function estimateTileSizeMeters(bounds: GeoBounds): { widthM: number; heightM: number; chunkSizeM: number } {
  const midLatRad = ((bounds.north + bounds.south) * 0.5 * Math.PI) / 180.0;
  const metersPerDegLat = 111320.0;
  const metersPerDegLon = metersPerDegLat * Math.cos(midLatRad);
  const widthM = Math.max(1.0, (bounds.east - bounds.west) * metersPerDegLon);
  const heightM = Math.max(1.0, (bounds.north - bounds.south) * metersPerDegLat);
  return { widthM, heightM, chunkSizeM: Math.max(widthM, heightM) };
}
