/**
 * Export module interfaces.
 *
 * All export formats (OpenDRIVE, GeoJSON, Shapefile, OBJ, etc.)
 * implement these interfaces, enabling pluggable exporters.
 */

import type { DEMResult, ImageryResult } from '../../../core/providers';
import type { ProgressReporter } from '../../../core/interfaces';

// GIS/roads modules removed; keep the property available but loosely typed.
type RoadNetwork = any;

// ─── Export Provider ───────────────────────────────────────────

export interface ExportProvider {
  readonly id: string;
  readonly displayName: string;
  /** File extension (without dot) */
  readonly extension: string;
  /** MIME type (if applicable) */
  readonly mimeType?: string;
  /** Whether this exporter supports road networks */
  readonly supportsRoadNetwork: boolean;
  /** Whether this exporter supports terrain */
  readonly supportsTerrain: boolean;

  /** Export data to a file */
  export(
    outputPath: string,
    data: ExportData,
    options: ExportOptions,
    progress?: ProgressReporter,
  ): Promise<ExportResult>;
}

// ─── Export Data ───────────────────────────────────────────────

export interface ExportData {
  /** Road network (if exporting roads) */
  roadNetwork?: RoadNetwork;
  /** DEM terrain (if exporting terrain) */
  terrain?: DEMResult;
  /** Satellite imagery (if exporting imagery) */
  imagery?: ImageryResult;
  /** 3D buildings (if available) */
  buildings?: ExportBuilding[];
  /** Vegetation mask (if available) */
  vegetationMask?: Uint8Array;
  /** Water mask (if available) */
  waterMask?: Uint8Array;
  /** Bounds of the export area */
  bounds: { north: number; south: number; east: number; west: number };
  /** CRS */
  crs: string;
}

export interface ExportBuilding {
  footprint: Array<{ x: number; y: number }>;
  height: number;
  levels: number;
  name?: string;
}

// ─── Export Options ────────────────────────────────────────────

export interface ExportOptions {
  /** Output format-specific settings */
  format?: Record<string, any>;
  /** Whether to include signals/objects */
  includeObjects?: boolean;
  /** Whether to include junctions */
  includeJunctions?: boolean;
  /** Whether to generate connecting roads for junctions */
  generateJunctionConnections?: boolean;
  /** OpenDRIVE version */
  openDriveVersion?: '1.4' | '1.5' | '1.6' | '1.7';
  /** Pretty-print XML */
  prettyPrint?: boolean;
  /** Validate output after generation */
  validate?: boolean;
  /** Tile prefix for multi-tile exports */
  tilePrefix?: string;
  /** Tile coordinates */
  tileRow?: number;
  tileCol?: number;
}

// ─── Export Result ─────────────────────────────────────────────

export interface ExportResult {
  /** Output file name (relative to outputPath) */
  fileName: string;
  /** Number of roads exported */
  roadCount: number;
  /** Number of signals/objects exported */
  signalCount: number;
  /** Number of junctions exported */
  junctionCount: number;
  /** Export time in milliseconds */
  exportTimeMs: number;
  /** Validation result (if validation was run) */
  validation?: { valid: boolean; errorCount: number; warningCount: number };
  /** Additional files generated (e.g. manifest.json) */
  additionalFiles?: string[];
}
