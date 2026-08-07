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
  ROAD_CREATE_SEGMENT,
  ROAD_CREATE_CIRCLE_ARC,
  ROAD_CREATE_CLOTHOID_ARC,
  ROAD_CREATE_POLYLINE,
  ROAD_CREATE_BEZIER,
  ROAD_CREATE_CLOTHOID_SPLINE,
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
  // Road creation tools (SCANeR-style)
  roadCreateSegment(sx: number, sy: number, ex: number, ey: number, params?: unknown): unknown;
  roadCreateCircleArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, numCPs?: number, params?: unknown): unknown;
  roadCreateClothoidArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, edx: number, edy: number, numCPs?: number, params?: unknown): unknown;
  roadCreatePolyline(points: unknown[], filletR?: number, filletSegs?: number, params?: unknown): unknown;
  roadCreateBezier(sx: number, sy: number, hox: number, hoy: number, ex: number, ey: number, hix: number, hiy: number, params?: unknown): unknown;
  roadCreateClothoidSpline(points: unknown[], stx: number, sty: number, etx: number, ety: number, segsPerSpan?: number, params?: unknown): unknown;
}

// ─── Lazy-loaded addon singleton ───────────────────────────
let roadEngineAddon: RoadEngineAddon | null = null;
let roadEngineLoadAttempted = false;

async function getRoadEngine(): Promise<RoadEngineAddon | null> {
  if (roadEngineLoadAttempted) return roadEngineAddon;
  roadEngineLoadAttempted = true;

  // Try multiple possible locations for the native addon
  // __dirname = dist-electron/app/handlers/ (when compiled)
  const candidates = [
    // dist-electron/native/road_engine/build/Release/ (copied alongside dist-electron/app/)
    path.join(__dirname, '..', '..', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
    // dist-electron/app/native/... (flat copy inside app/)
    path.join(__dirname, '..', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
    // dist-electron/app/handlers/native/... (same directory as handler)
    path.join(__dirname, 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
    // Packaged app resources
    path.join(process.resourcesPath || __dirname, 'native', 'road_engine_native.node'),
    // Fallback: direct from source (development) — app/native/road_engine/build/Release/
    path.join(__dirname, '..', '..', '..', 'app', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'),
  ];

  console.log('[RoadEngine] Searching for native addon...');
  console.log('[RoadEngine] __dirname =', __dirname);

  for (const candidate of candidates) {
    try {
      await fs.access(candidate);
      roadEngineAddon = require(candidate) as RoadEngineAddon;
      console.log('[RoadEngine] ✅ Loaded native addon from:', candidate);
      console.log('[RoadEngine] Addon version:', roadEngineAddon?.roadGetVersion?.() ?? 'unknown');
      console.log('[RoadEngine] Available functions:', Object.keys(roadEngineAddon ?? {}));
      return roadEngineAddon;
    } catch (err) {
      console.log('[RoadEngine] Not found at:', candidate, '-', (err as Error).message);
    }
  }

  console.error('[RoadEngine] ❌ Native addon NOT FOUND! Road geometry will use TypeScript fallback.');
  console.error('[RoadEngine] Checked paths:', candidates);
  return null;
}

// ─── Register IPC Handlers ─────────────────────────────────
export async function registerRoadEngineHandlers(): Promise<void> {
  const addon = await getRoadEngine();

  const tag = addon ? '[RoadEngine:C++]' : '[RoadEngine:TS-Fallback]';

  ipcMain.handle(ROAD_GET_VERSION, () => {
    console.log(tag, 'getVersion()');
    return addon?.roadGetVersion?.() ?? '0.0.0-ts-fallback';
  });

  ipcMain.handle(ROAD_GENERATE_INTERSECTION, async (_event, road1, road2, refLat, refLon) => {
    console.log(tag, 'generateIntersection() — roads:', road1?.id, road2?.id);
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    const result = addon.roadGenerateIntersection(road1, road2, refLat, refLon);
    console.log(tag, 'generateIntersection() → polygon pts:', (result as any)?.polygon?.length, 'approaches:', (result as any)?.approaches?.length);
    return result;
  });

  ipcMain.handle(ROAD_COMPUTE_CIRCLE_ARC, async (_event, startPoint, startDirection, endPoint, segments) => {
    console.log(tag, 'computeCircleArc()');
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadComputeCircleArc(startPoint, startDirection, endPoint, segments);
  });

  ipcMain.handle(ROAD_COMPUTE_CLOTHOID, async (_event, startPoint, startDirection, endPoint, endDirection, initialA, segments) => {
    console.log(tag, 'computeClothoid()');
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadComputeClothoid(startPoint, startDirection, endPoint, endDirection, initialA, segments);
  });

  ipcMain.handle(ROAD_GENERATE_ROAD_MESH, async (_event, road, numSamples) => {
    console.log(tag, 'generateRoadMesh()');
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadGenerateRoadMesh(road, numSamples);
  });

  ipcMain.handle(ROAD_GENERATE_INTERSECTION_MESH, async (_event, intersection, z) => {
    console.log(tag, 'generateIntersectionMesh()');
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadGenerateIntersectionMesh(intersection, z);
  });

  ipcMain.handle(ROAD_EXPORT_OPENDRIVE, async (_event, roads, refLat, refLon) => {
    console.log(tag, 'exportOpenDrive() — roads:', roads?.length);
    if (!addon) {
      throw new Error('Road engine native addon not available');
    }
    return addon.roadExportOpenDrive(roads, refLat, refLon);
  });

  ipcMain.handle(ROAD_SAMPLE_CENTERLINE, async (_event, road, numSamples) => {
    console.log(tag, 'sampleCenterline()');
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

  // ─── Road Creation Tools (SCANeR-style) ───────────────────
  ipcMain.handle(ROAD_CREATE_SEGMENT, async (_event, sx, sy, ex, ey, params) => {
    console.log(tag, 'createSegment()');
    if (!addon) throw new Error('Road engine native addon not available');
    return addon.roadCreateSegment(sx, sy, ex, ey, params);
  });

  ipcMain.handle(ROAD_CREATE_CIRCLE_ARC, async (_event, sx, sy, dx, dy, ex, ey, numCPs, params) => {
    console.log(tag, 'createCircleArc()');
    if (!addon) throw new Error('Road engine native addon not available');
    return addon.roadCreateCircleArc(sx, sy, dx, dy, ex, ey, numCPs, params);
  });

  ipcMain.handle(ROAD_CREATE_CLOTHOID_ARC, async (_event, sx, sy, dx, dy, ex, ey, edx, edy, numCPs, params) => {
    console.log(tag, 'createClothoidArc()');
    if (!addon) throw new Error('Road engine native addon not available');
    return addon.roadCreateClothoidArc(sx, sy, dx, dy, ex, ey, edx, edy, numCPs, params);
  });

  ipcMain.handle(ROAD_CREATE_POLYLINE, async (_event, points, filletR, filletSegs, params) => {
    console.log(tag, 'createPolyline() — points:', points?.length);
    if (!addon) throw new Error('Road engine native addon not available');
    return addon.roadCreatePolyline(points, filletR, filletSegs, params);
  });

  ipcMain.handle(ROAD_CREATE_BEZIER, async (_event, sx, sy, hox, hoy, ex, ey, hix, hiy, params) => {
    console.log(tag, 'createBezier()');
    if (!addon) throw new Error('Road engine native addon not available');
    return addon.roadCreateBezier(sx, sy, hox, hoy, ex, ey, hix, hiy, params);
  });

  ipcMain.handle(ROAD_CREATE_CLOTHOID_SPLINE, async (_event, points, stx, sty, etx, ety, segsPerSpan, params) => {
    console.log(tag, 'createClothoidSpline() — points:', points?.length);
    if (!addon) throw new Error('Road engine native addon not available');
    return addon.roadCreateClothoidSpline(points, stx, sty, etx, ety, segsPerSpan, params);
  });
}
