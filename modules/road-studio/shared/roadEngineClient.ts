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

// ─── Phase 2.8 — RoadBuildResult types ─────────────────────

/** A mesh section with typed arrays, ready for Babylon.js */
export interface MeshSectionData {
  material: string;
  materialType: number;
  positions: Float32Array;   // x, y, z interleaved
  normals: Float32Array;     // nx, ny, nz
  uvs: Float32Array;         // u, v
  indices: Uint32Array;
  vertexCount: number;
  indexCount: number;
  triangleCount: number;
}

/** A sampled point on a lane or boundary */
export interface SamplePointData {
  position: { x: number; y: number };
  s: number;
  heading: number;
  laneOffset: number;
}

/** A lane centerline in the lane network */
export interface LaneCenterlineData {
  laneId: number;
  type: number;
  startS: number;
  endS: number;
  length: number;
  numSamples: number;
  samples: SamplePointData[];
}

/** A lane boundary in the lane network */
export interface LaneBoundaryData {
  innerLaneId: number;
  outerLaneId: number;
  isRoadEdge: boolean;
  startS: number;
  endS: number;
  length: number;
  markType: number;
  markColor: string;
  markWidth: number;
  numSamples: number;
  samples: SamplePointData[];
}

/** The lane network returned by buildRoad() */
export interface LaneNetworkData {
  roadId: string;
  totalLength: number;
  numLaneSections: number;
  numCenterlines: number;
  numBoundaries: number;
  centerlines: LaneCenterlineData[];
  boundaries: LaneBoundaryData[];
}

/** A road marking polyline */
export interface RoadMarkPolylineData {
  innerLaneId: number;
  outerLaneId: number;
  isRoadEdge: boolean;
  isCenterLine: boolean;
  startS: number;
  endS: number;
  length: number;
  color: string;
  width: number;
  markType: number;
  numSamples: number;
  samples: SamplePointData[];
}

/** The road mark network returned by buildRoad() */
export interface RoadMarkNetworkData {
  roadId: string;
  numMarkings: number;
  markings: RoadMarkPolylineData[];
}

/** Adapter report from the road→RoadV2 conversion */
export interface BuildAdapterReport {
  exact: boolean;
  exactSegments: number;
  legacySegments: number;
  unsupportedSegments: number;
  numSegments: number;
  totalLength: number;
  warnings: string[];
}

/** Result of buildRoad() — the single API for the full lane engine pipeline */
export interface RoadBuildResult {
  meshSections: MeshSectionData[];
  totalVertices: number;
  totalTriangles: number;
  lanes: LaneNetworkData;
  markings: RoadMarkNetworkData;
  adapter: BuildAdapterReport;
}

// ─── Phase 3: Road Graph + Junction Builder types ────────

export interface RoadNodeData {
  id: string;
  position: { x: number; y: number };
  z: number;
  type: number; // 0=Junction, 1=EndPoint, 2=Merge, 3=Split
  typeName: string;
  connectedRoadIds: string[];
}

export interface RoadEdgeData {
  id: string;
  fromNodeId: string;
  toNodeId: string;
  roadId: string;
  length: number;
  laneCount: number;
  width: number;
  isOneWay: boolean;
}

export interface RoadGraphData {
  numNodes: number;
  numEdges: number;
  nodes: RoadNodeData[];
  edges: RoadEdgeData[];
}

export interface JunctionStripeData {
  type: number; // 0=Solid, 1=Dashed, 2=None
  color: string;
  samples: Array<{ x: number; y: number }>;
}

export interface JunctionMeshData {
  positions: Float32Array;
  normals: Float32Array;
  uvs: Float32Array;
  indices: Uint32Array;
  vertexCount: number;
  triangleCount: number;
}

export interface JunctionResultData {
  junctionId: string;
  center: { x: number; y: number };
  polygon: Array<{ x: number; y: number }>;
  laneStripes: JunctionStripeData[];
  asphaltMesh: JunctionMeshData;
  markingMesh: JunctionMeshData;
  numLaneConnections: number;
  numApproaches: number;
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
        // Road creation tools
        createSegment(sx: number, sy: number, ex: number, ey: number, params?: unknown): Promise<unknown>;
        createCircleArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, numCPs?: number, params?: unknown): Promise<unknown>;
        createClothoidArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, edx: number, edy: number, numCPs?: number, params?: unknown): Promise<unknown>;
        createPolyline(points: unknown[], filletR?: number, filletSegs?: number, params?: unknown): Promise<unknown>;
        createBezier(sx: number, sy: number, hox: number, hoy: number, ex: number, ey: number, hix: number, hiy: number, params?: unknown): Promise<unknown>;
        createClothoidSpline(points: unknown[], stx: number, sty: number, etx: number, ety: number, segsPerSpan?: number, params?: unknown): Promise<unknown>;
        // Phase 1.9 — RoadV2 bridge integration
        sampleCenterlineV2(road: unknown, numSamples?: number): Promise<unknown>;
        getAdapterReport(road: unknown): Promise<unknown>;
        convertFromV2(road: unknown): Promise<unknown>;
        // Phase 2.8 — Full lane engine pipeline
        buildRoad(road: unknown): Promise<unknown>;
        // Phase 3 — Road Graph + Junction Builder
        buildRoadGraph(roads: unknown[], intersections: unknown[]): Promise<unknown>;
        buildJunction(junctionId: string, roadGraph: unknown, laneNetworks: unknown): Promise<unknown>;
      };
    };
  }
}

// ─── Road format conversion (TS lat/lon → C++ local x/y) ──

/** Segment metadata for exact geometry reconstruction (Phase 1.8.3d) */
export interface CppSegmentMetadata {
  kind: 'line' | 'bezier' | 'arc' | 'spiral';
  version: number;
  startHeading: number;
  /** Arc parameters */
  curvature: number;
  arcLength: number;
  /** Spiral parameters */
  curvatureStart: number;
  curvatureEnd: number;
  segmentLength: number;
}

interface CppControlPoint {
  x: number;
  y: number;
  z: number;
  type: string;
  id?: string;
  handleIn: { x: number; y: number } | null;
  handleOut: { x: number; y: number } | null;
  /** Optional segment metadata for exact reconstruction */
  segmentMeta: CppSegmentMetadata | null;
}

interface CppRoad {
  id: string;
  name: string;
  width: number;
  laneCount: number;
  /** Schema version (1=legacy, 2=with segmentMeta) */
  formatVersion?: number;
  points: CppControlPoint[];
}

/** Convert a TS Road (lat/lon) to C++ Road format (local x/y meters) */
export function toCppRoad(road: Road, refLat: number, refLon: number): CppRoad {
  return {
    id: road.id,
    name: road.name,
    width: road.width,
    laneCount: road.laneCount,
    formatVersion: road.formatVersion ?? 1,
    points: road.points.map((cp) => {
      const local = tsGeoToLocal(cp.lat, cp.lon, refLat, refLon);
      const cppCp: CppControlPoint = {
        x: local.x,
        y: local.y,
        z: cp.z,
        type: cp.type,
        id: cp.id,
        handleIn: null,
        handleOut: null,
        segmentMeta: cp.segmentMeta ?? null,
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
  // Road creation tools (SCANeR-style)
  createSegment(sx: number, sy: number, ex: number, ey: number, params?: unknown): Promise<unknown>;
  createCircleArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, numCPs?: number, params?: unknown): Promise<unknown>;
  createClothoidArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, edx: number, edy: number, numCPs?: number, params?: unknown): Promise<unknown>;
  createPolyline(points: Array<{ x: number; y: number }>, filletR?: number, filletSegs?: number, params?: unknown): Promise<unknown>;
  createBezier(sx: number, sy: number, hox: number, hoy: number, ex: number, ey: number, hix: number, hiy: number, params?: unknown): Promise<unknown>;
  createClothoidSpline(points: Array<{ x: number; y: number }>, stx: number, sty: number, etx: number, ety: number, segsPerSpan?: number, params?: unknown): Promise<unknown>;
  /** Phase 1.9 — RoadV2 bridge integration */
  sampleCenterlineV2(road: Road, refLat: number, refLon: number, numSamples?: number): Promise<Array<{ x: number; y: number; z: number }>>;
  getAdapterReport(road: Road, refLat: number, refLon: number): Promise<AdapterReportResult>;
  convertFromV2(road: Road, refLat: number, refLon: number): Promise<unknown>;
  /** Phase 2.8 — Full lane engine pipeline */
  buildRoad(road: Road, refLat: number, refLon: number): Promise<RoadBuildResult>;
  /** Whether the C++ native engine is available */
  isNative(): boolean;
}

/** Adapter report from the C++ bridge */
export interface AdapterReportResult {
  exact: boolean;
  exactSegments: number;
  legacySegments: number;
  unsupportedSegments: number;
  lineSegments: number;
  bezierSegments: number;
  arcSegments: number;
  spiralSegments: number;
  numSegments: number;
  totalLength: number;
  warnings: string[];
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

  // ─── Road creation tools (SCANeR-style) ───────────────────
  async createSegment(sx: number, sy: number, ex: number, ey: number, params?: unknown): Promise<unknown> {
    return this.api.createSegment(sx, sy, ex, ey, params);
  }
  async createCircleArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, numCPs?: number, params?: unknown): Promise<unknown> {
    return this.api.createCircleArc(sx, sy, dx, dy, ex, ey, numCPs, params);
  }
  async createClothoidArc(sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, edx: number, edy: number, numCPs?: number, params?: unknown): Promise<unknown> {
    return this.api.createClothoidArc(sx, sy, dx, dy, ex, ey, edx, edy, numCPs, params);
  }
  async createPolyline(points: Array<{ x: number; y: number }>, filletR?: number, filletSegs?: number, params?: unknown): Promise<unknown> {
    return this.api.createPolyline(points, filletR, filletSegs, params);
  }
  async createBezier(sx: number, sy: number, hox: number, hoy: number, ex: number, ey: number, hix: number, hiy: number, params?: unknown): Promise<unknown> {
    return this.api.createBezier(sx, sy, hox, hoy, ex, ey, hix, hiy, params);
  }
  async createClothoidSpline(points: Array<{ x: number; y: number }>, stx: number, sty: number, etx: number, ety: number, segsPerSpan?: number, params?: unknown): Promise<unknown> {
    return this.api.createClothoidSpline(points, stx, sty, etx, ety, segsPerSpan, params);
  }

  // ─── Phase 1.9 — RoadV2 bridge integration ───────────────
  async sampleCenterlineV2(road: Road, refLat: number, refLon: number, numSamples?: number): Promise<Array<{ x: number; y: number; z: number }>> {
    const cppRoad = toCppRoad(road, refLat, refLon);
    const result = await this.api.sampleCenterlineV2(cppRoad, numSamples);
    const samples = result as Array<{ x: number; y: number }>;
    // Interpolate z from road's control points (same as sampleCenterline)
    return samples.map((s) => {
      let z = 0;
      if (road.points.length > 0) {
        let bestDist = Infinity;
        let bestZ = road.points[0].z;
        for (const cp of road.points) {
          const cpLocal = tsGeoToLocal(cp.lat, cp.lon, refLat, refLon);
          const d = Math.sqrt((s.x - cpLocal.x) ** 2 + (s.y - cpLocal.y) ** 2);
          if (d < bestDist) { bestDist = d; bestZ = cp.z; }
        }
        z = bestZ;
      }
      return { x: s.x, y: s.y, z };
    });
  }

  async getAdapterReport(road: Road, refLat: number, refLon: number): Promise<AdapterReportResult> {
    const cppRoad = toCppRoad(road, refLat, refLon);
    return this.api.getAdapterReport(cppRoad) as Promise<AdapterReportResult>;
  }

  async convertFromV2(road: Road, refLat: number, refLon: number): Promise<unknown> {
    const cppRoad = toCppRoad(road, refLat, refLon);
    return this.api.convertFromV2(cppRoad);
  }

  // ─── Phase 2.8 — Full lane engine pipeline ───────────────
  async buildRoad(road: Road, refLat: number, refLon: number): Promise<RoadBuildResult> {
    const cppRoad = toCppRoad(road, refLat, refLon);
    return this.api.buildRoad(cppRoad) as Promise<RoadBuildResult>;
  }

  // Phase 3 — Road Graph + Junction Builder
  async buildRoadGraph(
    roads: Road[],
    intersections: unknown[],
    refLat: number,
    refLon: number,
  ): Promise<RoadGraphData> {
    const cppRoads = roads.map((r) => toCppRoad(r, refLat, refLon));
    return this.api.buildRoadGraph(cppRoads, intersections) as Promise<RoadGraphData>;
  }

  async buildJunction(
    junctionId: string,
    roadGraph: RoadGraphData,
    laneNetworks: Record<string, LaneNetworkData>,
  ): Promise<JunctionResultData> {
    return this.api.buildJunction(junctionId, roadGraph, laneNetworks) as Promise<JunctionResultData>;
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
  // Road creation tools (SCANeR-style)
  createSegment: (sx: number, sy: number, ex: number, ey: number, params?: unknown) =>
    getRoadEngine().createSegment(sx, sy, ex, ey, params),
  createCircleArc: (sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, numCPs?: number, params?: unknown) =>
    getRoadEngine().createCircleArc(sx, sy, dx, dy, ex, ey, numCPs, params),
  createClothoidArc: (sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, edx: number, edy: number, numCPs?: number, params?: unknown) =>
    getRoadEngine().createClothoidArc(sx, sy, dx, dy, ex, ey, edx, edy, numCPs, params),
  createPolyline: (points: Array<{ x: number; y: number }>, filletR?: number, filletSegs?: number, params?: unknown) =>
    getRoadEngine().createPolyline(points, filletR, filletSegs, params),
  createBezier: (sx: number, sy: number, hox: number, hoy: number, ex: number, ey: number, hix: number, hiy: number, params?: unknown) =>
    getRoadEngine().createBezier(sx, sy, hox, hoy, ex, ey, hix, hiy, params),
  createClothoidSpline: (points: Array<{ x: number; y: number }>, stx: number, sty: number, etx: number, ety: number, segsPerSpan?: number, params?: unknown) =>
    getRoadEngine().createClothoidSpline(points, stx, sty, etx, ety, segsPerSpan, params),
  // Phase 2.8 — Full lane engine pipeline
  buildRoad: (road: Road, refLat: number, refLon: number) =>
    getRoadEngine().buildRoad(road, refLat, refLon),
  // Phase 3 — Road Graph + Junction Builder
  buildRoadGraph: (roads: Road[], intersections: unknown[], refLat: number, refLon: number) =>
    getRoadEngine().buildRoadGraph(roads, intersections, refLat, refLon),
  buildJunction: (junctionId: string, roadGraph: RoadGraphData, laneNetworks: Record<string, LaneNetworkData>) =>
    getRoadEngine().buildJunction(junctionId, roadGraph, laneNetworks),
};
