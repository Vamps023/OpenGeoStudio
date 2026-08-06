/**
 * Terrain module interfaces.
 *
 * Handles DEM processing, LiDAR import, terrain reconstruction,
 * elevation correction, and multi-resolution terrain generation.
 */

import type { BoundingBox, ProgressReporter, Point3D } from '../../../core/interfaces';
import type { DEMResult, LiDARResult } from '../../../core/providers';

// ─── Terrain Model ─────────────────────────────────────────────

export interface TerrainModel {
  /** Elevation grid (row-major, north to south) */
  elevations: Float32Array;
  width: number;
  height: number;
  /** Geographic bounds */
  bounds: BoundingBox;
  /** CRS */
  crs: string;
  /** Resolution in meters per pixel */
  resolution: number;
  /** Elevation statistics */
  stats: TerrainStats;
  /** Source data provenance */
  source: TerrainSource;
}

export interface TerrainStats {
  min: number;
  max: number;
  mean: number;
  stdDev: number;
  /** Number of NoData pixels */
  noDataCount: number;
  /** Total pixel count */
  totalPixels: number;
}

export type TerrainSource =
  | { type: 'dem'; provider: string; zoom: number }
  | { type: 'lidar'; provider: string; pointCount: number }
  | { type: 'merged'; sources: TerrainSource[] }
  | { type: 'imported'; fileName: string; format: string };

// ─── Terrain Processor ─────────────────────────────────────────

export interface TerrainProcessor {
  readonly id: string;
  readonly name: string;

  /** Process terrain data (smooth, correct, etc.) */
  process(
    terrain: TerrainModel,
    options: TerrainProcessOptions,
    progress?: ProgressReporter,
  ): Promise<TerrainModel>;
}

export interface TerrainProcessOptions {
  /** Smooth terrain with Gaussian filter */
  smoothIterations?: number;
  /** Remove elevation spikes above this threshold (meters) */
  spikeThreshold?: number;
  /** Fill NoData values with interpolated values */
  fillNoData?: boolean;
  /** Apply height correction offset (meters) */
  heightOffset?: number;
  /** Clip to bounds */
  clipBounds?: BoundingBox;
  /** Resize to target resolution */
  targetResolution?: number;
}

// ─── Terrain Reconstruction ────────────────────────────────────

export interface TerrainReconstructor {
  /** Reconstruct terrain from LiDAR point cloud */
  fromLiDAR(
    lidar: LiDARResult,
    bounds: BoundingBox,
    resolution: number,
    progress?: ProgressReporter,
  ): Promise<TerrainModel>;

  /** Merge DEM with LiDAR for higher resolution */
  mergeDEMANDLiDAR(
    dem: DEMResult,
    lidar: LiDARResult,
    progress?: ProgressReporter,
  ): Promise<TerrainModel>;

  /** Generate DTM (Digital Terrain Model) from DSM */
  dtmFromDSM(
    dsm: TerrainModel,
    options: DTMOptions,
    progress?: ProgressReporter,
  ): Promise<TerrainModel>;
}

export interface DTMOptions {
  /** Classification to treat as ground */
  groundClassifications?: number[];
  /** Maximum slope for ground detection (degrees) */
  maxSlope?: number;
  /** Window size for progressive morphological filter */
  windowSize?: number;
}

// ─── Elevation Sampling ────────────────────────────────────────

export interface ElevationSampler {
  /** Sample elevation at a single point */
  sample(lat: number, lon: number): number;
  /** Sample elevation at multiple points */
  sampleBatch(points: Array<{ lat: number; lon: number }>): number[];
  /** Sample elevation along a line (for road profiles) */
  sampleLine(start: { lat: number; lon: number }, end: { lat: number; lon: number }, samples: number): number[];
  /** Get elevation profile with bridge/tunnel awareness */
  sampleForRoad(
    points: Array<{ lat: number; lon: number }>,
    roadInfo: { bridge?: string; tunnel?: string; layer: number; tags: Record<string, string> },
  ): Point3D[];
}

// ─── Multi-Resolution Terrain ──────────────────────────────────

export interface MultiResolutionTerrain {
  /** LOD levels (0 = highest resolution) */
  levels: TerrainLOD[];
  /** Get the best LOD for a given screen size */
  getLOD(screenSize: number, bounds: BoundingBox): TerrainLOD;
}

export interface TerrainLOD {
  level: number;
  terrain: TerrainModel;
  /** Error metric (geometric error in meters) */
  geometricError: number;
}
