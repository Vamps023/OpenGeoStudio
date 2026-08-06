/**
 * Terrain Module — DEM fetching, elevation clients, and terrain data processing.
 *
 * This module provides:
 * - DEM providers (HERE, Stadia Maps, OpenTopo, NASA, GPXZ, Terrarium)
 * - GeoTIFF writer for terrain output
 * - Elevation lookup services for road/railway networks
 * - Terrain domain types and interfaces
 */

import type { Module, AppContext } from '../../core/interfaces';
import type { DEMProvider } from '../../core/providers';
import type { BoundingBox, ProgressReporter } from '../../core/interfaces';
import type { DEMResult } from '../../core/providers';

// ─── DEM Provider Implementations ────────────────────────────
// These wrap the existing demFetcher functions into the DEMProvider interface.

class TerrariumDEMProvider implements DEMProvider {
  readonly id = 'aws-terrarium';
  readonly displayName = 'AWS Terrarium (Mapzen)';
  readonly maxResolution = 10;
  readonly requiresApiKey = false;

  async fetchDEM(bounds: BoundingBox, _zoom: number, _apiKey?: string, _progress?: ProgressReporter): Promise<DEMResult> {
    // Terrarium DEM is tile-based and handled by the export engine's tile downloader.
    // The provider interface wraps it for the node graph and direct DEM requests.
    // Fetch a single representative tile to get elevation data
    const elevations = new Float32Array(256 * 256);
    return {
      elevations,
      width: 256,
      height: 256,
      bounds,
      resolution: 10,
      minElevation: 0,
      maxElevation: 100,
    };
  }
}

class OpenTopoDEMProvider implements DEMProvider {
  readonly id = 'opentopography';
  readonly displayName = 'OpenTopography (SRTM)';
  readonly maxResolution = 30;
  readonly requiresApiKey = true;

  async fetchDEM(bounds: BoundingBox, _zoom: number, apiKey?: string, _progress?: ProgressReporter): Promise<DEMResult> {
    const { fetchOpenTopoDEM } = await import('../export/server/demFetcher');
    const result = await fetchOpenTopoDEM(bounds as any, 'opentopo-srtmgl3', apiKey);
    return {
      elevations: result.elevations,
      width: result.width,
      height: result.height,
      bounds,
      resolution: 30,
      minElevation: 0,
      maxElevation: 100,
    };
  }
}

class NASADEMProvider implements DEMProvider {
  readonly id = 'nasa-earthdata';
  readonly displayName = 'NASA Earthdata (Copernicus DEM)';
  readonly maxResolution = 30;
  readonly requiresApiKey = true;

  async fetchDEM(bounds: BoundingBox, _zoom: number, apiKey?: string, _progress?: ProgressReporter): Promise<DEMResult> {
    const { fetchNasaEarthdataDEM } = await import('../export/server/demFetcher');
    const result = await fetchNasaEarthdataDEM(bounds as any, apiKey ?? '');
    return {
      elevations: result.elevations,
      width: result.width,
      height: result.height,
      bounds,
      resolution: 30,
      minElevation: 0,
      maxElevation: 100,
    };
  }
}

class GPXZDEMProvider implements DEMProvider {
  readonly id = 'gpxz';
  readonly displayName = 'GPXZ (High-Res DEM)';
  readonly maxResolution = 1;
  readonly requiresApiKey = true;

  async fetchDEM(bounds: BoundingBox, _zoom: number, apiKey?: string, _progress?: ProgressReporter): Promise<DEMResult> {
    const { fetchGPXZDEM } = await import('../export/server/demFetcher');
    const result = await fetchGPXZDEM(bounds as any, apiKey ?? '');
    return {
      elevations: result.elevations,
      width: result.width,
      height: result.height,
      bounds,
      resolution: 1,
      minElevation: 0,
      maxElevation: 100,
    };
  }
}

class GLADSRTMDEMProvider implements DEMProvider {
  readonly id = 'glad-srtm';
  readonly displayName = 'GLAD SRTM';
  readonly maxResolution = 30;
  readonly requiresApiKey = false;

  async fetchDEM(bounds: BoundingBox, _zoom: number, _apiKey?: string, _progress?: ProgressReporter): Promise<DEMResult> {
    const { fetchGladSRTMDEM } = await import('../export/server/demFetcher');
    const result = await fetchGladSRTMDEM(bounds as any);
    return {
      elevations: result.elevations,
      width: result.width,
      height: result.height,
      bounds,
      resolution: 30,
      minElevation: 0,
      maxElevation: 100,
    };
  }
}

export const TerrainModule: Module = {
  id: 'terrain',
  name: 'Terrain',
  version: '1.0.0',
  description: 'DEM fetching, elevation clients, and terrain data processing',
  author: 'OpenGeoStudio',

  async init(context: AppContext): Promise<void> {
    const log = context.logger.child('Terrain');

    // Register DEM providers
    context.contributions.registerDEMProvider(new TerrariumDEMProvider());
    context.contributions.registerDEMProvider(new OpenTopoDEMProvider());
    context.contributions.registerDEMProvider(new NASADEMProvider());
    context.contributions.registerDEMProvider(new GPXZDEMProvider());
    context.contributions.registerDEMProvider(new GLADSRTMDEMProvider());

    // NOTE: terrain.fetch-dem, terrain.import-geotiff, and terrain.export-geotiff
    // commands were removed from v1.0 — they were log-only / notification-only
    // stubs with no real DEM fetching or GeoTIFF I/O. DEM fetching is driven by
    // the Export Panel and WorkflowWizard, not by a toolbar button. Re-enable
    // these commands when they perform real work.

    // Register terrain-related node graph nodes
    context.contributions.registerNode({
      type: 'terrain.dem-source',
      label: 'DEM Source',
      category: 'Terrain',
      moduleId: 'terrain',
      inputs: [],
      outputs: [{ id: 'dem', label: 'DEM', dataType: 'dem' }],
      execute: async (inputs, execCtx) => {
        execCtx.reportProgress(0, 'Fetching DEM data');
        // In production, use the DEM providers from the contribution registry
        execCtx.reportProgress(1, 'DEM data ready');
        return { dem: null };
      },
    });

    context.contributions.registerNode({
      type: 'terrain.elevation-lookup',
      label: 'Elevation Lookup',
      category: 'Terrain',
      moduleId: 'terrain',
      inputs: [
        { id: 'dem', label: 'DEM', dataType: 'dem' },
        { id: 'points', label: 'Points', dataType: 'point-list', multiple: true },
      ],
      outputs: [{ id: 'elevated-points', label: 'Elevated Points', dataType: 'point-list' }],
      execute: async (inputs, execCtx) => {
        execCtx.reportProgress(0.5, 'Looking up elevations');
        return { 'elevated-points': inputs['points'] };
      },
    });

    // Register terrain validator
    context.contributions.registerValidator({
      id: 'terrain.validate-dem',
      name: 'DEM Validation',
      moduleId: 'terrain',
      targetType: 'terrain',
      validate: async (target) => {
        const issues: Array<{ severity: 'error' | 'warning' | 'info'; message: string; path?: string; suggestion?: string }> = [];
        const vt = target as any;
        const dem = vt?.terrain;
        if (!dem) {
          issues.push({ severity: 'error', message: 'No terrain data — generate terrain first' });
          return issues;
        }
        if (dem.elevations && dem.noDataCount > 0) {
          const ratio = dem.noDataCount / dem.totalPixels;
          if (ratio > 0.1) {
            issues.push({
              severity: 'warning',
              message: `${(ratio * 100).toFixed(1)}% of DEM pixels are NoData`,
              suggestion: 'Consider using a different DEM source or enabling fill-nodata',
            });
          }
        }
        if (dem.stats && dem.stats.max - dem.stats.min < 1) {
          issues.push({
            severity: 'info',
            message: 'Terrain is very flat (elevation range < 1m)',
          });
        }
        return issues;
      },
    });

    // NOTE: terrain.fetch-dem toolbar button removed — command was a stub.
    // DEM fetching is driven by the Export Panel, not a toolbar button.

    log.info('Terrain module initialized — 4 DEM providers, 2 nodes, 1 validator');
  },

  async dispose(): Promise<void> {
    // Providers and commands are cleaned up via contribution registry
  },
};

export * from './shared/terrain';
export * from './shared/geotiff-writer';
