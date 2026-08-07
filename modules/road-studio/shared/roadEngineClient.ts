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

import type { Road, GeneratedIntersection, CircleArc } from './types';

// ─── Global window type augmentation ───────────────────────
declare global {
  interface Window {
    electronAPI?: {
      roadEngine?: {
        getVersion(): Promise<string>;
        generateIntersection(road1: unknown, road2: unknown, refLat: number, refLon: number): Promise<unknown>;
        computeCircleArc(sp: { x: number; y: number }, sd: { x: number; y: number }, ep: { x: number; y: number }, segments?: number): Promise<unknown>;
        sampleCenterline(road: unknown, numSamples?: number): Promise<unknown>;
        geoToLocal(lat: number, lon: number, refLat: number, refLon: number): Promise<{ x: number; y: number }>;
        localToGeo(x: number, y: number, refLat: number, refLon: number): Promise<{ lat: number; lon: number }>;
      };
    };
  }
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
    return this.api.generateIntersection(road1, road2, refLat, refLon) as Promise<GeneratedIntersection>;
  }

  async computeCircleArc(
    startPoint: { x: number; y: number },
    startDirection: { x: number; y: number },
    endPoint: { x: number; y: number },
    segments?: number
  ): Promise<CircleArc> {
    return this.api.computeCircleArc(startPoint, startDirection, endPoint, segments) as Promise<CircleArc>;
  }

  async sampleCenterline(road: Road, numSamples?: number): Promise<Array<{ x: number; y: number }>> {
    return this.api.sampleCenterline(road, numSamples) as Promise<Array<{ x: number; y: number }>>;
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
    _engine = new NativeRoadEngine();
  } else {
    // Fallback: use TypeScript implementations
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
  geoToLocal as tsGeoToLocal,
  localToGeo as tsLocalToGeo,
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
  sampleCenterline: (road: Road, numSamples?: number) =>
    getRoadEngine().sampleCenterline(road, numSamples),
  geoToLocal: (lat: number, lon: number, refLat: number, refLon: number) =>
    getRoadEngine().geoToLocal(lat, lon, refLat, refLon),
  localToGeo: (x: number, y: number, refLat: number, refLon: number) =>
    getRoadEngine().localToGeo(x, y, refLat, refLon),
  isNative: () => getRoadEngine().isNative(),
};
