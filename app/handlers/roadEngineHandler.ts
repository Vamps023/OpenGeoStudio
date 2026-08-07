/**
 * Road Engine IPC Handler
 *
 * Bridges the renderer process to the C++ road geometry engine native addon.
 * All road geometry computation happens in C++ for performance and precision.
 *
 * Architecture:
 *   Renderer → preload (window.roadEngine.*) → IPC → this handler → C++ addon
 */

import { ipcMain } from 'electron';
import * as path from 'path';
import * as fs from 'fs/promises';
import {
  ROAD_GET_VERSION,
  ROAD_GENERATE_INTERSECTION,
  ROAD_COMPUTE_CIRCLE_ARC,
  ROAD_COMPUTE_CLOTHOID,
  ROAD_GENERATE_ROAD_MESH,
  ROAD_GENERATE_INTERSECTION_MESH,
  ROAD_EXPORT_OPENDRIVE,
  ROAD_SAMPLE_CENTERLINE,
  ROAD_GEO_TO_LOCAL,
  ROAD_LOCAL_TO_GEO,
} from '../../shared/ipcChannels-electron';

// ─── Native Addon Interface ────────────────────────────────
export interface RoadEngineAddon {
  getVersion(): string;
  roadGetVersion(): string;
  roadGenerateIntersection(
    road1: unknown,
    road2: unknown,
    refLat: number,
    refLon: number
  ): unknown;
  roadComputeCircleArc(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    segments?: number
  ): unknown;
  roadComputeClothoid(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    endDirection: { x: number; y: number },
    initialA?: number,
    segments?: number
  ): unknown;
  roadGenerateRoadMesh(road: unknown, numSamples?: number): unknown;
  roadGenerateIntersectionMesh(intersection: unknown, z?: number): unknown;
  roadExportOpenDrive(roads: unknown[], refLat: number, refLon: number): string;
  roadSampleCenterline(road: unknown, numSamples?: number): unknown;
  roadGeoToLocal(lat: number, lon: number, refLat: number, refLon: number): { x: number; y: number };
  roadLocalToGeo(x: number, y: number, refLat: number, refLon: number): { lat: number; lon: number };
}

// ─── Lazy-loaded addon singleton ───────────────────────────
let roadEngineAddon: RoadEngineAddon | null = null;
let roadEngineLoadAttempted = false;

async function getRoadEngine(): Promise<RoadEngineAddon | null> {
  if (roadEngineLoadAttempted) return roadEngineAddon;
  roadEngineLoadAttempted = true;

  // Try multiple possible locations for the native addon
  const candidates = [
    // When running from dist-electron/app/ (compiled)
    path.join(__dirname, '..', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
    // When running from dist-electron/app/ (flat copy)
    path.join(__dirname, 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
    // Development: from app/handlers/ to app/native/road_engine/
    path.join(__dirname, '..', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
    // Packaged app resources
    path.join(process.resourcesPath || __dirname, 'native', 'road_engine_native.node'),
    // Fallback: direct from source
    path.join(__dirname, '..', '..', 'app', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
  ];

  for (const candidate of candidates) {
    try {
      await fs.access(candidate);
      roadEngineAddon = require(candidate) as RoadEngineAddon;
      console.log('[RoadEngine] Loaded native addon from:', candidate);
      return roadEngineAddon;
    } catch {
      // try next
    }
  }

  console.warn('[RoadEngine] Native addon not found — road geometry will use TypeScript fallback');
  return null;
}

// ─── Register IPC Handlers ─────────────────────────────────
export async function registerRoadEngineHandlers(): Promise<void> {
  const addon = await getRoadEngine();

  ipcMain.handle(ROAD_GET_VERSION, () => {
    return addon?.roadGetVersion?.() ?? '0.0.0-ts-fallback';
  });

  ipcMain.handle(ROAD_GENERATE_INTERSECTION, async (_event, road1, road2, refLat, refLon) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadGenerateIntersection(road1, road2, refLat, refLon);
  });

  ipcMain.handle(ROAD_COMPUTE_CIRCLE_ARC, async (_event, startPoint, startDirection, endPoint, segments) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadComputeCircleArc(startPoint, startDirection, endPoint, segments);
  });

  ipcMain.handle(ROAD_COMPUTE_CLOTHOID, async (_event, startPoint, startDirection, endPoint, endDirection, initialA, segments) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadComputeClothoid(startPoint, startDirection, endPoint, endDirection, initialA, segments);
  });

  ipcMain.handle(ROAD_GENERATE_ROAD_MESH, async (_event, road, numSamples) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadGenerateRoadMesh(road, numSamples);
  });

  ipcMain.handle(ROAD_GENERATE_INTERSECTION_MESH, async (_event, intersection, z) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadGenerateIntersectionMesh(intersection, z);
  });

  ipcMain.handle(ROAD_EXPORT_OPENDRIVE, async (_event, roads, refLat, refLon) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadExportOpenDrive(roads, refLat, refLon);
  });

  ipcMain.handle(ROAD_SAMPLE_CENTERLINE, async (_event, road, numSamples) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadSampleCenterline(road, numSamples);
  });

  ipcMain.handle(ROAD_GEO_TO_LOCAL, async (_event, lat, lon, refLat, refLon) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadGeoToLocal(lat, lon, refLat, refLon);
  });

  ipcMain.handle(ROAD_LOCAL_TO_GEO, async (_event, x, y, refLat, refLon) => {
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadLocalToGeo(x, y, refLat, refLon);
  });
}
