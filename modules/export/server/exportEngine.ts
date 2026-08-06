/**
 * Export orchestrator — slim entry point that coordinates the export pipeline.
 *
 * Heavy logic is split into focused modules:
 * - types.ts         — shared types, constants, helpers
 * - tileMath.ts      — tile coordinate math + validation
 * - downloader.ts    — HTTP download utilities
 * - demFetcher.ts    — DEM fetching (OpenTopo, NASA, GPXZ, Terrarium/Mapbox)
 * - imageProcessor.ts — merge, crop, resize, elevation metadata
 * - formatWriter.ts  — heightmap/albedo format writers + tile naming
 */

import * as fsPromises from 'fs/promises';
import * as path from 'path';
import sharp from 'sharp';

import {
  type ExportOptions,
  type ElevationMetadata,
  type CancellationToken,
  TILE_SIZE,
  MAX_CONCURRENT_DOWNLOADS,
  assertNever,
  isOpenTopoSource,
  getOpenTopoDemType,
  getDEMSourceInfo,
  estimateTileSizeMeters,
  createCancellationToken,
  checkCancellation,
} from './types';

import { validateExport, chooseZoom, getTileRange } from './tileMath';
import { parallelDownload, type DownloadTask } from './downloader';
import { fetchOpenTopoDEM, fetchNasaEarthdataDEM, fetchGPXZDEM, fetchGladSRTMDEM, decodeDEMTile } from './demFetcher';
import { fetchGladARDImagery, gladBandsToRGB } from './gladClient';
import { mergeDEMTiles, cropDEM, mergeImageryTilesChunked, cropImagery, resizeDEM, resizeImagery, computeElevationMetadata } from './imageProcessor';
import {
  generateTileFileNames,
  getHeightmapFormatLabel,
  writeHeightmapPNG,
  writeHeightmapR16,
  writeHeightmapGeoTIFFInt16,
  writeHeightmapGeoTIFFUint16,
  writeHeightmapGeoTIFFFloat32,
  writeAlbedoPNG,
  writeAlbedoGeoTIFF,
} from './formatWriter';

// Re-export for backwards compatibility
export { validateExport, createCancellationToken };
export type { CancellationToken, ExportOptions };

// ─── Main Export ──────────────────────────────────────────────

export async function executeExport(
  options: ExportOptions,
  onProgress?: (progress: { stage: string; current: number; total: number; message: string }) => void
): Promise<{
  manifestPath: string;
  elevationRange: { min: number; max: number };
  files: { heightmap?: string; albedo: string };
}> {
  const {
    sessionId,
    outputPath,
    preset,
    bounds,
    heightmapFormat,
    albedoFormat,
    heightmapSize = 1024,
    albedoSize = 1024,
    demSource = 'aws-terrarium',
    imagerySource = 'arcgis',
    imageryZoom = 0,
    tileRow = 0,
    tileCol = 0,
    compression = 'none',
    downloadDem = true,
  } = options;

  const cancellationToken = options.cancellationToken;
  const crs = options.crsSource || 'EPSG:4326';

  const validation = validateExport(options);
  if (!validation.valid) {
    throw new Error(`Export validation failed: ${validation.errors.join(', ')}`);
  }

  if (validation.warnings.length > 0) {
    // no-op
  }

  checkCancellation(cancellationToken, 'setup');
  await fsPromises.mkdir(outputPath, { recursive: true });

  const fileNames = generateTileFileNames(tileRow, tileCol, heightmapFormat, albedoFormat);

  const demZoom = chooseZoom(bounds, heightmapSize, 20);
  const imgZoom = imageryZoom > 0 ? imageryZoom : chooseZoom(bounds, albedoSize, 22);

  if (onProgress) {
    onProgress({ stage: 'setup', current: 0, total: 100, message: 'Starting export...' });
  }

  // ── Build URLs ───────────────────────────────────────────────
  function getDEMUrl(x: number, y: number, z: number): string {
    switch (demSource) {
      case 'mapbox-terrain-rgb': {
        const token = options.mapboxAccessToken || process.env.MAPBOX_ACCESS_TOKEN;
        if (!token) {
          throw new Error('Mapbox access token required for Mapbox Terrain-RGB. Add it in the Export panel → API Keys section, or get a free token at https://account.mapbox.com/');
        }
        return `https://api.mapbox.com/v4/mapbox.terrain-rgb/${z}/${x}/${y}@2x.pngraw?access_token=${token}`;
      }
      case 'mapzen':
        return `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${z}/${x}/${y}.png`;
      case 'aws-terrarium':
        return `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/${z}/${x}/${y}.png`;
      case 'opentopo-srtmgl1':
      case 'opentopo-srtmgl3':
      case 'opentopo-aw3d30':
      case 'opentopo-cop30':
      case 'opentopo-nasadem':
      case 'opentopo-usgs10m':
        throw new Error(`getDEMUrl called with OpenTopo source: ${demSource}. Use fetchOpenTopoDEM instead.`);
      case 'nasa-earthdata':
        throw new Error(`getDEMUrl called with NASA Earthdata source. Use fetchNasaEarthdataDEM instead.`);
      case 'gpxz':
        throw new Error(`getDEMUrl called with GPXZ source. Use fetchGPXZDEM instead.`);
      case 'glad-srtm':
        throw new Error(`getDEMUrl called with GLAD SRTM source. Use fetchGladSRTMDEM instead.`);
      case 'local-file':
        throw new Error(`getDEMUrl called with local-file source. Use local DEM file instead.`);
      default: return assertNever(demSource);
    }
  }

  function getImageryUrl(x: number, y: number, z: number): string {
    switch (imagerySource) {
      case 'mapbox': {
        const token = options.mapboxAccessToken || process.env.MAPBOX_ACCESS_TOKEN;
        if (!token) {
          throw new Error('Mapbox access token required. Add it in the Export panel → API Keys section.');
        }
        return `https://api.mapbox.com/v4/mapbox.satellite/${z}/${x}/${y}@2x.png?access_token=${token}`;
      }
      case 'maptiler': {
        const key = options.maptilerApiKey || process.env.MAPTILER_API_KEY;
        if (!key) {
          throw new Error('MapTiler API key required. Add it in the Export panel → API Keys section.');
        }
        return `https://api.maptiler.com/tiles/satellite/${z}/${x}/${y}.jpg?key=${key}`;
      }
      case 'google':
        return `https://mt1.google.com/vt/lyrs=s&x=${x}&y=${y}&z=${z}`;
      case 'arcgis':
        return `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${z}/${y}/${x}`;
      case 'glad-ard':
        throw new Error(`getImageryUrl called with GLAD ARD source. Use fetchGladARDImagery instead.`);
      case 'local-file':
        throw new Error(`getImageryUrl called with local-file source. Use local imagery file instead.`);
      default: return assertNever(imagerySource);
    }
  }

  // ── Download DEM ──────────────────────────────────────────────
  checkCancellation(cancellationToken, 'DEM download');
  let demSourceData: { elevations: Float32Array; width: number; height: number } = { elevations: new Float32Array(0), width: 0, height: 0 };
  const demRange = getTileRange(bounds, demZoom);

  if (downloadDem && heightmapFormat !== 'none') {
    if (isOpenTopoSource(demSource)) {
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 0, total: 1, message: `Requesting ${getOpenTopoDemType(demSource)} from OpenTopography...` });
      }
      demSourceData = await fetchOpenTopoDEM(bounds, demSource, options.opentopographyApiKey);
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 1, total: 1, message: `Downloaded ${getOpenTopoDemType(demSource)} (${demSourceData.width}x${demSourceData.height})` });
      }
    } else if (demSource === 'gpxz') {
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 0, total: 1, message: 'Downloading DEM from GPXZ...' });
      }
      demSourceData = await fetchGPXZDEM(bounds, options.gpxzApiKey);
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 1, total: 1, message: `Downloaded GPXZ DEM (${demSourceData.width}x${demSourceData.height})` });
      }
    } else if (demSource === 'nasa-earthdata') {
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 0, total: 1, message: 'Downloading NASADEM from NASA Earthdata...' });
      }
      demSourceData = await fetchNasaEarthdataDEM(bounds, options.opentopographyApiKey);
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 1, total: 1, message: `Downloaded NASADEM (${demSourceData.width}x${demSourceData.height})` });
      }
    } else if (demSource === 'glad-srtm') {
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 0, total: 1, message: 'Downloading SRTM from GLAD (UMD)...' });
      }
      demSourceData = await fetchGladSRTMDEM(bounds);
      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 1, total: 1, message: `Downloaded GLAD SRTM (${demSourceData.width}x${demSourceData.height})` });
      }
    } else if (demSource === 'local-file') {
      throw new Error('Local DEM file export is not yet supported in the export pipeline. Please select a remote DEM source for export.');
    } else {
      const demTasks: DownloadTask<Float32Array>[] = [];

      for (let y = demRange.minY; y <= demRange.maxY; y++) {
        for (let x = demRange.minX; x <= demRange.maxX; x++) {
          demTasks.push({
            url: getDEMUrl(x, y, demZoom),
            x,
            y,
            processor: async (buffer) => {
              try {
                return await decodeDEMTile(buffer, demSource);
              } catch {
                return new Float32Array(TILE_SIZE * TILE_SIZE);
              }
            },
          });
        }
      }

      if (onProgress) {
        onProgress({ stage: 'download_dem', current: 0, total: demTasks.length, message: `Downloading ${demTasks.length} DEM tiles...` });
      }

      const demResults = await parallelDownload(
        demTasks,
        MAX_CONCURRENT_DOWNLOADS,
        (completed, total) => {
          if (onProgress) {
            onProgress({ stage: 'download_dem', current: completed, total, message: `Downloaded ${completed}/${total} DEM tiles` });
          }
        }
      );

      const demTiles = demResults
        .filter((r): r is { success: true; data: Float32Array; x: number; y: number } => r.success && r.data !== undefined)
        .map((r) => ({ x: r.x, y: r.y, elevations: r.data }));

      const merged = mergeDEMTiles(demTiles, demRange);
      const cropped = cropDEM(merged.elevations, merged.width, merged.height, bounds, demRange, demZoom);
      demSourceData = cropped;
    }
  }

  // ── Download imagery tiles (parallel) ────────────────────────
  checkCancellation(cancellationToken, 'imagery download');

  let gladArdBuffer: Buffer | null = null;
  let gladArdWidth = 0;
  let gladArdHeight = 0;

  if (imagerySource === 'glad-ard') {
    // GLAD ARD: fetch Landsat composite directly, bypass tile-based download
    const gladInterval = options.gladArdInterval || 920;
    if (onProgress) {
      onProgress({ stage: 'download_imagery', current: 0, total: 1, message: `Downloading GLAD ARD interval ${gladInterval}...` });
    }
    const ardResult = await fetchGladARDImagery(bounds, gladInterval);
    gladArdWidth = ardResult.width;
    gladArdHeight = ardResult.height;
    gladArdBuffer = gladBandsToRGB(ardResult.bands, ardResult.width, ardResult.height);
    if (onProgress) {
      onProgress({ stage: 'download_imagery', current: 1, total: 1, message: `Downloaded GLAD ARD (${gladArdWidth}x${gladArdHeight})` });
    }
  }

  const imgRange = getTileRange(bounds, imgZoom);
  const imgTasks: DownloadTask<Buffer>[] = [];

  if (imagerySource === 'local-file') {
    throw new Error('Local imagery file export is not yet supported in the export pipeline. Please select a remote imagery source for export.');
  }

  if (imagerySource !== 'glad-ard') {
    for (let y = imgRange.minY; y <= imgRange.maxY; y++) {
      for (let x = imgRange.minX; x <= imgRange.maxX; x++) {
        imgTasks.push({
          url: getImageryUrl(x, y, imgZoom),
          x,
          y,
          processor: (buffer) => buffer,
        });
      }
    }
  }

  if (onProgress) {
    onProgress({ stage: 'download_imagery', current: 0, total: imgTasks.length, message: `Downloading ${imgTasks.length} imagery tiles...` });
  }

  const imgResults = await parallelDownload(
    imgTasks,
    MAX_CONCURRENT_DOWNLOADS,
    (completed, total) => {
      if (onProgress) {
        onProgress({ stage: 'download_imagery', current: completed, total, message: `Downloaded ${completed}/${total} imagery tiles` });
      }
    }
  );

  let fallbackBlackTile: Buffer | null = null;
  const imgTiles = await Promise.all(
    imgResults.map(async (r) => {
      if (r.success && r.data) {
        return { x: r.x, y: r.y, buffer: r.data };
      } else {
        if (!fallbackBlackTile) {
          fallbackBlackTile = await sharp({
            create: { width: TILE_SIZE, height: TILE_SIZE, channels: 4, background: { r: 0, g: 0, b: 0, alpha: 255 } },
          }).png().toBuffer();
        }
        return { x: r.x, y: r.y, buffer: fallbackBlackTile };
      }
    })
  );

  // ── Process DEM ──────────────────────────────────────────────
  checkCancellation(cancellationToken, 'DEM processing');
  let elevationMeta: ElevationMetadata = { min: 0, max: 0, range: 0, hasNoData: false };
  let resizedDEM = new Float32Array(0) as Float32Array;

  if (downloadDem && heightmapFormat !== 'none') {
    if (onProgress) {
      onProgress({ stage: 'process_dem', current: 0, total: 100, message: 'Processing DEM...' });
    }

    resizedDEM = resizeDEM(
      demSourceData.elevations,
      demSourceData.width,
      demSourceData.height,
      heightmapSize,
      heightmapSize
    );

    elevationMeta = computeElevationMetadata(resizedDEM);
  }

  if (onProgress) {
    onProgress({ stage: 'process_imagery', current: 0, total: 100, message: 'Processing imagery...' });
  }

  // ── Process imagery ──────────────────────────────────────────
  let resizedImg: Buffer;

  if (gladArdBuffer && gladArdWidth > 0) {
    // GLAD ARD: resize the RGB composite directly to albedo size
    resizedImg = await sharp(gladArdBuffer, {
      raw: { width: gladArdWidth, height: gladArdHeight, channels: 3 },
    })
      .resize(albedoSize, albedoSize, { kernel: 'lanczos2' })
      .ensureAlpha()
      .raw()
      .toBuffer();
  } else {
    const mergedImg = await mergeImageryTilesChunked(imgTiles, imgRange, (completed, total) => {
      if (onProgress) {
        onProgress({ stage: 'process_imagery', current: completed, total, message: `Merged ${completed}/${total} imagery tiles` });
      }
    });

    const imgFullW = (imgRange.maxX - imgRange.minX + 1) * TILE_SIZE;
    const imgFullH = (imgRange.maxY - imgRange.minY + 1) * TILE_SIZE;
    const { buffer: croppedImg, width: cropW, height: cropH } = await cropImagery(
      mergedImg,
      imgFullW,
      imgFullH,
      bounds,
      imgRange,
      imgZoom
    );
    resizedImg = await resizeImagery(croppedImg, cropW, cropH, albedoSize, albedoSize);
  }

  // ── Write heightmap ──────────────────────────────────────────
  checkCancellation(cancellationToken, 'heightmap writing');
  if (!downloadDem && heightmapFormat !== 'none' && fileNames.heightmap) {
    if (onProgress) {
      onProgress({ stage: 'write_heightmap', current: 0, total: 100, message: 'Skipping heightmap (DEM download disabled)' });
    }
  } else if (downloadDem && heightmapFormat !== 'none' && fileNames.heightmap) {
    if (onProgress) {
      onProgress({ stage: 'write_heightmap', current: 0, total: 100, message: `Writing ${getHeightmapFormatLabel(heightmapFormat)}...` });
    }

    const heightmapPath = path.join(outputPath, fileNames.heightmap);

    switch (heightmapFormat) {
      case 'png':
        await writeHeightmapPNG(resizedDEM, heightmapSize, heightmapSize, heightmapPath, elevationMeta);
        break;
      case 'r16':
        await writeHeightmapR16(resizedDEM, heightmapSize, heightmapSize, heightmapPath, elevationMeta);
        break;
      case 'float32':
        await writeHeightmapGeoTIFFFloat32(resizedDEM, heightmapSize, heightmapSize, bounds, heightmapPath, compression, crs);
        break;
      case 'geotiff':
        await writeHeightmapGeoTIFFUint16(resizedDEM, heightmapSize, heightmapSize, bounds, heightmapPath, compression, crs);
        break;
      case 'dem':
        await writeHeightmapGeoTIFFInt16(resizedDEM, heightmapSize, heightmapSize, bounds, heightmapPath, compression, crs);
        break;
      default:
        assertNever(heightmapFormat);
    }
  }

  // ── Write albedo ─────────────────────────────────────────────
  checkCancellation(cancellationToken, 'albedo writing');
  if (onProgress) {
    onProgress({ stage: 'write_albedo', current: 0, total: 100, message: 'Writing albedo...' });
  }

  const albedoPath = path.join(outputPath, fileNames.albedo);

  switch (albedoFormat) {
    case 'geotiff':
      await writeAlbedoGeoTIFF(resizedImg, albedoSize, albedoSize, bounds, albedoPath, compression, crs);
      break;
    case 'png':
      await writeAlbedoPNG(resizedImg, albedoSize, albedoSize, albedoPath);
      break;
    default:
      assertNever(albedoFormat);
  }

  // ── Generate terrain masks (skipped; GIS mask-generator removed) ──────────
  const { maskSettings } = options;
  const maskFiles: Record<string, string> = {};

  // ── Generate 3D geometry files (skipped; GIS OSM extractor removed) ─────────
  const files3D: { buildings3D?: string; roads3D?: string; railways3D?: string; trafficSigns3D?: string } = {};

  const { widthM: tileWidthM, heightM: tileHeightM, chunkSizeM } = estimateTileSizeMeters(bounds);
  const worldOffsetX = tileCol * tileWidthM;
  const worldOffsetZ = tileRow * tileHeightM;

  const demInfo = getDEMSourceInfo(demSource);

  const manifest = {
    version: '1.0.0',
    createdBy: 'GeoTerrain Studio',
    createdAt: new Date().toISOString(),
    terrainName: `Terrain_${sessionId}`,
    bounds,
    crs,
    tileGrid: {
      rows: 1,
      cols: 1,
      chunkSizeM,
      tileWidthM,
      tileHeightM,
      heightmapResolution: heightmapSize,
      albedoResolution: albedoSize,
    },
    tiles: [
      {
        row: tileRow,
        col: tileCol,
        bounds,
        worldOffset: { x: worldOffsetX, y: 0, z: worldOffsetZ },
        files: {
          heightmap: (downloadDem && heightmapFormat !== 'none') ? fileNames.heightmap : undefined,
          albedo: fileNames.albedo,
          ...(maskFiles.roadMask ? { roadMask: maskFiles.roadMask } : {}),
          ...(maskFiles.waterMask ? { waterMask: maskFiles.waterMask } : {}),
          ...(maskFiles.vegetationMask ? { vegetationMask: maskFiles.vegetationMask } : {}),
          ...(maskFiles.buildingMask ? { buildingMask: maskFiles.buildingMask } : {}),
          ...(maskFiles.cliffMask ? { cliffMask: maskFiles.cliffMask } : {}),
          ...(files3D.buildings3D ? { buildings3D: files3D.buildings3D } : {}),
          ...(files3D.roads3D ? { roads3D: files3D.roads3D } : {}),
          ...(files3D.railways3D ? { railways3D: files3D.railways3D } : {}),
          ...(files3D.trafficSigns3D ? { trafficSigns3D: files3D.trafficSigns3D } : {}),
        },
        elevation: {
          min: Math.round(elevationMeta.min * 100) / 100,
          max: Math.round(elevationMeta.max * 100) / 100,
          units: 'meters' as const,
          actualMin: elevationMeta.min,
          actualMax: elevationMeta.max,
          hasNoData: elevationMeta.hasNoData,
        },
      },
    ],
    sources: {
      dem: { id: demSource, name: demInfo.name, attribution: demInfo.attribution },
      imagery: { id: imagerySource, name: imagerySource === 'arcgis' ? 'ArcGIS World Imagery' : imagerySource === 'mapbox' ? 'Mapbox Satellite' : imagerySource === 'maptiler' ? 'MapTiler Satellite' : imagerySource === 'google' ? 'Google Satellite' : imagerySource === 'glad-ard' ? 'GLAD ARD Landsat' : 'Local File', attribution: imagerySource === 'arcgis' ? 'Esri' : imagerySource === 'mapbox' ? 'Mapbox' : imagerySource === 'maptiler' ? 'MapTiler' : imagerySource === 'google' ? 'Google' : imagerySource === 'glad-ard' ? 'UMD GLAD' : 'User-provided' },
    },
    exportPreset: preset,
    processing: {
      normalizeHeights: true,
      heightScale: 1.0,
      seamStitching: true,
      fillNodata: true,
      generateRoadMasks: maskSettings?.generateRoadMask ?? false,
      generateWaterMasks: maskSettings?.generateWaterMask ?? false,
      generateVegetationMasks: maskSettings?.generateVegetationMask ?? false,
      generateBuildingMasks: maskSettings?.generateBuildingMask ?? false,
      generateCliffMasks: maskSettings?.generateCliffMask ?? false,
      cliffThresholdDegrees: maskSettings?.cliffThresholdDegrees ?? 45.0,
    },
  };

  const manifestPath = path.join(outputPath, 'manifest.json');
  await fsPromises.writeFile(manifestPath, JSON.stringify(manifest, null, 2));

  if (onProgress) {
    onProgress({ stage: 'complete', current: 100, total: 100, message: 'Export complete!' });
  }

  return {
    manifestPath: outputPath,
    elevationRange: { min: elevationMeta.min, max: elevationMeta.max },
    files: fileNames,
  };
}
