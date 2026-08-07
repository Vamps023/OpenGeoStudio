/**
 * Road Engine Client — Renderer-side wrapper for the C++ road geometry engine.
 *
 * This is the ONLY geometry backend. All road geometry computation is done
 * in the C++ native addon via IPC. There is no TypeScript fallback.
 *
 * Usage:
 *   import { roadEngine } from '../shared/roadEngineClient';
 *   const intersection = await roadEngine.generateIntersection(road1, road2, refLat, refLon);
 */

import type { Road, GeneratedIntersection, CircleArc, Point2D } from './types';
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
export function toCppRoad(road: Road, refLat: number, refLon: number): CppRoad {
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
  generateRoadMesh(road: Road, refLat: number, refLon: number, numSamples?: number): Promise<MeshData>;
  generateIntersectionMesh(intersection: GeneratedIntersection, z?: number): Promise<MeshData>;
  exportOpenDrive(roads: Road[], refLat: number, refLon: number): Promise<string>;
  sampleCenterline(road: Road, refLat: number, refLon: number, numSamples?: number): Promise<Array<{ x: number; y: number; z: number }>>;
  geoToLocal(lat: number, lon: number, refLat: number, refLon: number): Promise<{ x: number; y: number }>;
  localToGeo(x: number, y: number, refLat: number, refLon: number): Promise<{ lat: number; lon: number }>;
  /** Whether the C++ native engine is available */
  isNative(): boolean;
}

// ─── C++ Native Engine (the only implementation) ───────────
class NativeRoadEngine implements RoadEngineAPI {
  private get api() {
    const ea = window.electronAPI;
    if (!ea?.roadEngine) {
      throw new Error('Road engine IPC bridge not available (electronAPI.roadEngine missing). The C++ native addon is not loaded.');
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

  async generateRoadMesh(road: Road, refLat: number, refLon: number, numSamples?: number): Promise<MeshData> {
    const cppRoad = toCppRoad(road, refLat, refLon);
    return this.api.generateRoadMesh(cppRoad, numSamples) as Promise<MeshData>;
  }

  async generateIntersectionMesh(intersection: GeneratedIntersection, z?: number): Promise<MeshData> {
    return this.api.generateIntersectionMesh(intersection, z) as Promise<MeshData>;
  }

  async exportOpenDrive(roads: Road[], refLat: number, refLon: number): Promise<string> {
    const cppRoads = roads.map(r => toCppRoad(r, refLat, refLon));
    return this.api.exportOpenDrive(cppRoads, refLat, refLon);
  }

  async sampleCenterline(road: Road, refLat: number, refLon: number, numSamples?: number): Promise<Array<{ x: number; y: number; z: number }>> {
    const cppRoad = toCppRoad(road, refLat, refLon);
    const result = await this.api.sampleCenterline(cppRoad, numSamples);
    // C++ returns { x, y } — we need to add z from the road's elevation
    const samples = result as Array<{ x: number; y: number }>;
    // Interpolate z along the centerline
    return samples.map((s) => {
      // Find closest control point for z interpolation
      let z = 0;
      if (road.points.length > 0) {
        // Simple: interpolate z based on position along road
        // For now, use linear interpolation between control points
        z = road.points[0].z;
        if (road.points.length > 1) {
          // Find which segment this sample falls on
          let bestDist = Infinity;
          let bestZ = road.points[0].z;
          for (const cp of road.points) {
            const cpLocal = tsGeoToLocal(cp.lat, cp.lon, refLat, refLon);
            const d = Math.sqrt((s.x - cpLocal.x) ** 2 + (s.y - cpLocal.y) ** 2);
            if (d < bestDist) { bestDist = d; bestZ = cp.z; }
          }
          z = bestZ;
        }
      }
      return { x: s.x, y: s.y, z };
    });
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
 * Always uses the C++ native addon. Throws if not available.
 */
export function getRoadEngine(): RoadEngineAPI {
  if (_engine) return _engine;

  if (window.electronAPI?.roadEngine) {
    console.log('[RoadEngine] Using C++ native engine (window.electronAPI.roadEngine available)');
    _engine = new NativeRoadEngine();
  } else {
    console.error('[RoadEngine] ❌ C++ native engine NOT available! window.electronAPI.roadEngine is missing.');
    console.error('[RoadEngine] The C++ addon is not loaded. Road geometry will not work.');
    console.error('[RoadEngine] Run: npm run rebuild:road-engine');
    // Return a stub that throws on every call
    _engine = new NativeRoadEngine();
  }

  return _engine;
}

// ─── Convenience exports ───────────────────────────────────
export const roadEngine = {
  isNative: () => getRoadEngine().isNative(),
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
  generateRoadMesh: (road: Road, refLat: number, refLon: number, numSamples?: number) =>
    getRoadEngine().generateRoadMesh(road, refLat, refLon, numSamples),
  generateIntersectionMesh: (intersection: GeneratedIntersection, z?: number) =>
    getRoadEngine().generateIntersectionMesh(intersection, z),
  exportOpenDrive: (roads: Road[], refLat: number, refLon: number) =>
    getRoadEngine().exportOpenDrive(roads, refLat, refLon),
  sampleCenterline: (road: Road, refLat: number, refLon: number, numSamples?: number) =>
    getRoadEngine().sampleCenterline(road, refLat, refLon, numSamples),
  geoToLocal: (lat: number, lon: number, refLat: number, refLon: number) =>
    getRoadEngine().geoToLocal(lat, lon, refLat, refLon),
  localToGeo: (x: number, y: number, refLat: number, refLon: number) =>
    getRoadEngine().localToGeo(x, y, refLat, refLon),
};
