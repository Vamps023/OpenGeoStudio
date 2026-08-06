/**
 * Provider interfaces for all data sources.
 * Every provider (DEM, imagery, OSM, LiDAR, etc.) implements
 * these common interfaces. No provider-specific logic leaks
 * into business code.
 */

import type { BoundingBox, LatLon, ProgressReporter } from './interfaces';

// ─── DEM Provider ──────────────────────────────────────────────

export interface DEMProvider {
  readonly id: string;
  readonly displayName: string;
  /** Max resolution in meters per pixel */
  readonly maxResolution: number;
  /** Whether this provider requires an API key */
  readonly requiresApiKey: boolean;

  /** Fetch DEM tiles for a bounding box at given zoom level */
  fetchDEM(
    bounds: BoundingBox,
    zoom: number,
    apiKey: string | undefined,
    progress?: ProgressReporter,
  ): Promise<DEMResult>;
}

export interface DEMResult {
  /** Elevation grid, row-major (north to south) */
  elevations: Float32Array;
  width: number;
  height: number;
  /** Geographic bounds the grid covers */
  bounds: BoundingBox;
  /** Source resolution in meters per pixel */
  resolution: number;
  /** Min/max elevation in meters */
  minElevation: number;
  maxElevation: number;
  /** NoData value (if any) */
  noDataValue?: number;
}

// ─── Imagery Provider ──────────────────────────────────────────

export interface ImageryProvider {
  readonly id: string;
  readonly displayName: string;
  readonly maxZoom: number;
  readonly requiresApiKey: boolean;

  fetchImagery(
    bounds: BoundingBox,
    zoom: number,
    apiKey: string | undefined,
    progress?: ProgressReporter,
  ): Promise<ImageryResult>;
}

export interface ImageryResult {
  /** PNG/GeoTIFF buffer */
  buffer: Buffer;
  width: number;
  height: number;
  bounds: BoundingBox;
  format: 'png' | 'geotiff' | 'jpeg';
}

// ─── OSM/Vector Data Provider ──────────────────────────────────

export interface VectorDataProvider {
  readonly id: string;
  readonly displayName: string;

  /** Fetch vector features (roads, buildings, etc.) within bounds */
  fetchFeatures(
    bounds: BoundingBox,
    options: VectorFetchOptions,
    progress?: ProgressReporter,
  ): Promise<VectorDataResult>;
}

export interface VectorFetchOptions {
  /** Feature types to extract */
  features: VectorFeatureType[];
  /** Timeout in milliseconds */
  timeoutMs?: number;
  /** Max retries */
  maxRetries?: number;
}

export type VectorFeatureType =
  | 'roads'
  | 'railways'
  | 'buildings'
  | 'traffic-signs'
  | 'traffic-signals'
  | 'water'
  | 'vegetation'
  | 'administrative'
  | 'all';

export interface VectorDataResult {
  roads: OSMRoad[];
  railways: OSMRailway[];
  buildings: OSMBuilding[];
  trafficSigns: OSMTrafficSign[];
  trafficSignals: OSMTrafficSignal[];
  /** Raw OSM tags for any other features */
  rawFeatures?: OSMTaggedFeature[];
}

export interface OSMRoad {
  id: string;
  centerline: LatLon[];
  width: number;
  surface: string;
  highway: string;
  bridge?: string;
  tunnel?: string;
  layer: number;
  tags: Record<string, string>;
}

export interface OSMRailway {
  id: string;
  centerline: LatLon[];
  width: number;
  railway: string;
  gauge?: string;
  bridge?: string;
  tunnel?: string;
  layer: number;
  tags: Record<string, string>;
}

export interface OSMBuilding {
  id: string;
  footprint: LatLon[];
  height: number;
  levels: number;
  name?: string;
  tags: Record<string, string>;
}

export interface OSMTrafficSign {
  id: string;
  position: LatLon;
  type: string;
  value?: string;
  tags: Record<string, string>;
}

export interface OSMTrafficSignal {
  id: string;
  position: LatLon;
  tags: Record<string, string>;
}

export interface OSMTaggedFeature {
  id: string;
  geometry: LatLon[] | LatLon; // point or polygon
  tags: Record<string, string>;
}

// ─── LiDAR Provider ────────────────────────────────────────────

export interface LiDARProvider {
  readonly id: string;
  readonly displayName: string;

  /** Import a LAS/LAZ file */
  importLAS(filePath: string, progress?: ProgressReporter): Promise<LiDARResult>;

  /** Fetch LiDAR data for a region (if online provider) */
  fetchLiDAR(
    bounds: BoundingBox,
    progress?: ProgressReporter,
  ): Promise<LiDARResult>;
}

export interface LiDARResult {
  points: LiDARPoint[];
  /** Point count */
  count: number;
  /** Bounds of the point cloud */
  bounds: BoundingBox;
  /** Classification available */
  hasClassification: boolean;
  /** CRS of the point cloud */
  crs: string;
}

export interface LiDARPoint {
  x: number;
  y: number;
  z: number;
  classification?: LiDARClassification;
  intensity?: number;
  returnNumber?: number;
  numberOfReturns?: number;
}

export enum LiDARClassification {
  Created = 0,
  Unclassified = 1,
  Ground = 2,
  LowVegetation = 3,
  MediumVegetation = 4,
  HighVegetation = 5,
  Building = 6,
  LowPoint = 7,
  ModelKeyPoint = 8,
  Water = 9,
  Bridge = 11,
  Road = 12,
}

// ─── File Import Provider ──────────────────────────────────────

export interface FileImportProvider {
  readonly id: string;
  readonly displayName: string;
  /** Supported file extensions */
  readonly extensions: string[];

  import(filePath: string, progress?: ProgressReporter): Promise<ImportedData>;
}

export interface ImportedData {
  type: 'vector' | 'raster' | 'pointcloud' | 'unknown';
  /** Vector data (if applicable) */
  vector?: VectorDataResult;
  /** Raster/DEM data (if applicable) */
  raster?: DEMResult;
  /** Point cloud (if applicable) */
  pointcloud?: LiDARResult;
  /** CRS of imported data */
  crs: string;
  /** Bounds of imported data */
  bounds: BoundingBox;
}
