/**
 * Road Engine Client — Renderer-side wrapper for the C++ road geometry engine.
 *
 * This module provides a unified API for road geometry operations.
 * It delegates to the C++ native addon via IPC when available,
 * and falls back to the TypeScript implementations in types.ts when not.
 *
 * Usage:
 *   import { roadEngine } from '../shared/roadEngineClient';
 *   const intersection = await roadEngine.generateIntersection(road1, road2, refLat, refLon);
 */

import type { Road, GeneratedIntersection, CircleArc, ControlPoint, Point2D } from './types';
import { geoToLocal as tsGeoToLocal, localToGeo as tsLocalToGeo } from './types';

// ─── Clothoid result type ──────────────────────────────────
export interface ClothoidResult {
  points: Point2D[];
  tangentIn: Point2D;
  tangentOut: Point2D;
  totalAngle: number;  // radians
  A: number;           // clothoid parameter
  L: number;           // total length
  isLeftTurn: boolean;
}

// ─── Mesh data type ────────────────────────────────────────
export interface MeshData {
  vertices: Float32Array;   // x, y, z interleaved
  normals: Float32Array;    // nx, ny, nz
  uvs: Float32Array;        // u, v
  indices: Uint32Array;     // triangle indices
  vertexCount: number;
  indexCount: number;
  triangleCount: number;
}

// ─── Global window type augmentation ───────────────────────
declare global {
  interface Window {
    electronAPI?: {
      roadEngine?: {
        getVersion(): Promise<string>;
        generateIntersection(road1: unknown, road2: unknown, refLat: number, refLon: number): Promise<unknown>;
        computeCircleArc(sp: { x: number; y: number }, sd: { x: number; y: number }, ep: { x: number; y: number }, segments?: number): Promise<unknown>;
        computeClothoid(sp: { x: number; y: number }, sd: { x: number; y: number }, ep: { x: number; y: number }, ed: { x: number; y: number }, initialA?: number, segments?: number): Promise<unknown>;
        generateRoadMesh(road: unknown, numSamples?: number): Promise<unknown>;
        generateIntersectionMesh(intersection: unknown, z?: number): Promise<unknown>;
        exportOpenDrive(roads: unknown[], refLat: number, refLon: number): Promise<string>;
        sampleCenterline(road: unknown, numSamples?: number): Promise<unknown>;
        geoToLocal(lat: number, lon: number, refLat: number, refLon: number): Promise<{ x: number; y: number }>;
        localToGeo(x: number, y: number, refLat: number, refLon: number): Promise<{ lat: number; lon: number }>;
      };
    };
  }
}

// ─── Road format conversion (TS lat/lon → C++ local x/y) ──
interface CppControlPoint {
  x: number;
  y: number;
  z: number;
  type: string;
  handleIn: { x: number; y: number } | null;
  handleOut: { x: number; y: number } | null;
}

interface CppRoad {
  id: string;
  name: string;
  width: number;
  laneCount: number;
  points: CppControlPoint[];
}

/** Convert a TS Road (lat/lon) to C++ Road format (local x/y meters) */
function toCppRoad(road: Road, refLat: number, refLon: number): CppRoad {
  return {
    id: road.id,
    name: road.name,
    width: road.width,
    laneCount: road.laneCount,
    points: road.points.map((cp) => {
      const local = tsGeoToLocal(cp.lat, cp.lon, refLat, refLon);
      const cppCp: CppControlPoint = {
        x: local.x,
        y: local.y,
        z: cp.z,
        type: cp.type,
        handleIn: null,
        handleOut: null,
      };
      if (cp.handleIn) {
        const hLocal = tsGeoToLocal(cp.lat + cp.handleIn.lat, cp.lon + cp.handleIn.lon, refLat, refLon);
        cppCp.handleIn = { x: hLocal.x - local.x, y: hLocal.y - local.y };
      }
      if (cp.handleOut) {
        const hLocal = tsGeoToLocal(cp.lat + cp.handleOut.lat, cp.lon + cp.handleOut.lon, refLat, refLon);
        cppCp.handleOut = { x: hLocal.x - local.x, y: hLocal.y - local.y };
      }
      return cppCp;
    }),
  };
}

// ─── Type definitions for the C++ addon return types ───────
// The C++ addon returns plain objects with the same shape as our TS types.

// ─── Road Engine API ───────────────────────────────────────
export interface RoadEngineAPI {
  getVersion(): Promise<string>;
  generateIntersection(road1: Road, road2: Road, refLat: number, refLon: number): Promise<GeneratedIntersection>;
  computeCircleArc(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    segments?: number
  ): Promise<CircleArc>;
  computeClothoid(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    endDirection: { x: number; y: number },
    initialA?: number,
    segments?: number
  ): Promise<ClothoidResult>;
  generateRoadMesh(road: Road, numSamples?: number): Promise<MeshData>;
  generateIntersectionMesh(intersection: GeneratedIntersection, z?: number): Promise<MeshData>;
  exportOpenDrive(roads: Road[], refLat: number, refLon: number): Promise<string>;
  sampleCenterline(road: Road, numSamples?: number): Promise<Array<{ x: number; y: number }>>;
  geoToLocal(lat: number, lon: number, refLat: number, refLon: number): Promise<{ x: number; y: number }>;
  localToGeo(x: number, y: number, refLat: number, refLon: number): Promise<{ lat: number; lon: number }>;
  /** Whether the C++ native engine is available */
  isNative(): boolean;
}

// ─── Native (C++) implementation ───────────────────────────
class NativeRoadEngine implements RoadEngineAPI {
  private get api() {
    const ea = window.electronAPI;
    if (!ea?.roadEngine) {
      throw new Error('Road engine IPC bridge not available (electronAPI.roadEngine missing)');
    }
    return ea.roadEngine;
  }

  isNative(): boolean {
    return !!window.electronAPI?.roadEngine;
  }

  async getVersion(): Promise<string> {
    return this.api.getVersion();
  }

  async generateIntersection(road1: Road, road2: Road, refLat: number, refLon: number): Promise<GeneratedIntersection> {
    const cppRoad1 = toCppRoad(road1, refLat, refLon);
    const cppRoad2 = toCppRoad(road2, refLat, refLon);
    return this.api.generateIntersection(cppRoad1, cppRoad2, refLat, refLon) as Promise<GeneratedIntersection>;
  }

  async computeCircleArc(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    segments?: number
  ): Promise<CircleArc> {
    return this.api.computeCircleArc(startPoint, startDirection, endPoint, segments) as Promise<CircleArc>;
  }

  async computeClothoid(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    endDirection: { x: number; y: number },
    initialA?: number,
    segments?: number
  ): Promise<ClothoidResult> {
    return this.api.computeClothoid(startPoint, startDirection, endPoint, endDirection, initialA, segments) as Promise<ClothoidResult>;
  }

  async generateRoadMesh(road: Road, numSamples?: number): Promise<MeshData> {
    const cppRoad = toCppRoad(road, 0, 0);  // mesh uses local coords
    return this.api.generateRoadMesh(cppRoad, numSamples) as Promise<MeshData>;
  }

  async generateIntersectionMesh(intersection: GeneratedIntersection, z?: number): Promise<MeshData> {
    return this.api.generateIntersectionMesh(intersection, z) as Promise<MeshData>;
  }

  async exportOpenDrive(roads: Road[], refLat: number, refLon: number): Promise<string> {
    const cppRoads = roads.map(r => toCppRoad(r, refLat, refLon));
    return this.api.exportOpenDrive(cppRoads, refLat, refLon);
  }

  async sampleCenterline(road: Road, numSamples?: number): Promise<Array<{ x: number; y: number }>> {
    // Convert road to C++ format using 0,0 as reference (points are already in local if pre-converted)
    // For the general case, we need refLat/refLon — but sampleCenterline is mainly used internally
    // with already-converted roads. If the road has lat/lon points, convert them.
    const refLat = 0;
    const refLon = 0;
    const cppRoad = toCppRoad(road, refLat, refLon);
    return this.api.sampleCenterline(cppRoad, numSamples) as Promise<Array<{ x: number; y: number }>>;
  }

  async geoToLocal(lat: number, lon: number, refLat: number, refLon: number): Promise<{ x: number; y: number }> {
    return this.api.geoToLocal(lat, lon, refLat, refLon);
  }

  async localToGeo(x: number, y: number, refLat: number, refLon: number): Promise<{ lat: number; lon: number }> {
    return this.api.localToGeo(x, y, refLat, refLon);
  }
}

// ─── Lazy singleton ────────────────────────────────────────
let _engine: RoadEngineAPI | null = null;

/**
 * Get the road engine instance.
 * Uses C++ native addon when available, falls back to TypeScript otherwise.
 */
export function getRoadEngine(): RoadEngineAPI {
  if (_engine) return _engine;

  // Check if native C++ engine is available via Electron preload
  if (window.electronAPI?.roadEngine) {
    console.log('[RoadEngine] Using C++ native engine (window.electronAPI.roadEngine available)');
    _engine = new NativeRoadEngine();
  } else {
    console.warn('[RoadEngine] ⚠️ Using TypeScript fallback engine (window.electronAPI.roadEngine NOT available)');
    console.warn('[RoadEngine] This means the C++ native addon is not loaded. Road geometry will be slower and may produce different results.');
    _engine = new TypeScriptRoadEngineFallback();
  }

  return _engine;
}

// ─── TypeScript fallback ───────────────────────────────────
// Used when the C++ native addon is not available (e.g. in dev mode
// before the addon is built, or when running outside Electron).
import {
  generateIntersection as tsGenerateIntersection,
  computeCircleArc as tsComputeCircleArc,
  sampleRoad as tsSampleRoad,
} from './types';

class TypeScriptRoadEngineFallback implements RoadEngineAPI {
  isNative(): boolean {
    return false;
  }

  async getVersion(): Promise<string> {
    return '0.0.0-ts-fallback';
  }

  async generateIntersection(road1: Road, road2: Road, refLat: number, refLon: number): Promise<GeneratedIntersection> {
    const result = tsGenerateIntersection(road1, road2, refLat, refLon);
    if (!result) throw new Error('Failed to generate intersection');
    return result;
  }

  async computeCircleArc(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    segments?: number
  ): Promise<CircleArc> {
    const result = tsComputeCircleArc(startPoint as any, startDirection as any, endPoint as any, segments ?? 32);
    if (!result) throw new Error('Failed to compute circle arc');
    return result;
  }

  async sampleCenterline(road: Road, numSamples?: number): Promise<Array<{ x: number; y: number }>> {
    // TS sampleRoad needs refLat/refLon from the road — use 0,0 as fallback
    // (the C++ version uses local coordinates directly)
    const refLat = (road as any)._refLat ?? 0;
    const refLon = (road as any)._refLon ?? 0;
    return tsSampleRoad(road, refLat, refLon, numSamples ?? 24);
  }

  async geoToLocal(lat: number, lon: number, refLat: number, refLon: number): Promise<{ x: number; y: number }> {
    return tsGeoToLocal(lat, lon, refLat, refLon);
  }

  async localToGeo(x: number, y: number, refLat: number, refLon: number): Promise<{ lat: number; lon: number }> {
    const result = tsLocalToGeo(x, y, refLat, refLon);
    return { lat: result.lat, lon: result.lon };
  }
}

// ─── Convenience exports ───────────────────────────────────
export const roadEngine = {
  getVersion: () => getRoadEngine().getVersion(),
  generateIntersection: (r1: Road, r2: Road, refLat: number, refLon: number) =>
    getRoadEngine().generateIntersection(r1, r2, refLat, refLon),
  computeCircleArc: (
    sp: { x: number; y: number },
    sd: { x: number; y: number },
    ep: { x: number; y: number },
    segments?: number
  ) => getRoadEngine().computeCircleArc(sp, sd, ep, segments),
  computeClothoid: (
    sp: { x: number; y: number },
    sd: { x: number; y: number },
    ep: { x: number; y: number },
    ed: { x: number; y: number },
    initialA?: number,
    segments?: number
  ) => getRoadEngine().computeClothoid(sp, sd, ep, ed, initialA, segments),
  generateRoadMesh: (road: Road, numSamples?: number) =>
    getRoadEngine().generateRoadMesh(road, numSamples),
  generateIntersectionMesh: (intersection: GeneratedIntersection, z?: number) =>
    getRoadEngine().generateIntersectionMesh(intersection, z),
  exportOpenDrive: (roads: Road[], refLat: number, refLon: number) =>
    getRoadEngine().exportOpenDrive(roads, refLat, refLon),
  sampleCenterline: (road: Road, numSamples?: number) =>
    getRoadEngine().sampleCenterline(road, numSamples),
  geoToLocal: (lat: number, lon: number, refLat: number, refLon: number) =>
    getRoadEngine().geoToLocal(lat, lon, refLat, refLon),
  localToGeo: (x: number, y: number, refLat: number, refLon: number) =>
    getRoadEngine().localToGeo(x, y, refLat, refLon),
  isNative: () => getRoadEngine().isNative(),
};
