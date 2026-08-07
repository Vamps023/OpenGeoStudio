/**
 * Road Studio Store — Zustand state for road editing.
 *
 * Manages roads, control points, tool selection, undo/redo,
 * and the reference origin for geo-to-local conversion.
 */

import { create } from 'zustand';
import type { Road, ControlPoint, Tool, Selection, HistorySnapshot, Vec2, RoadProfile, Intersection, GeneratedIntersection } from '../../shared/types';
import { generateId, ROAD_PROFILES, detectIntersections, distanceMeters, sampleRoad, localToGeo, generateIntersection, geoToLocal } from '../../shared/types';

interface RoadStudioState {
  /** All roads in the project */
  roads: Road[];
  /** Active editing tool */
  tool: Tool;
  /** Current selection */
  selection: Selection;
  /** Multi-selected road IDs (for intersection creation, etc.) */
  selectedRoadIds: string[];
  /** Reference origin (lat/lon) for local coordinate conversion */
  refLat: number;
  refLon: number;
  /** Default road width (meters) */
  defaultWidth: number;
  /** Default lane count */
  defaultLaneCount: number;
  /** Grid size for snapping (meters, 0 = off) */
  gridSize: number;
  /** Snap to existing endpoints */
  snapEnabled: boolean;
  /** Undo stack */
  undoStack: HistorySnapshot[];
  /** Redo stack */
  redoStack: HistorySnapshot[];
  /** Currently drawing road ID (for line/pen tool) */
  drawingRoadId: string | null;
  /** Whether the pen tool is dragging (creating bezier handle) */
  penDragging: boolean;
  /** View mode: 'top' = 2D top-down, 'perspective' = 3D angled */
  viewMode: 'top' | 'perspective';
  /** Whether the satellite map overlay is shown in top view */
  showMapOverlay: boolean;
  /** Detected intersections (auto-computed from road endpoints) */
  intersections: Intersection[];
  /** Generated intersections (from Detect Intersection tool) */
  generatedIntersections: GeneratedIntersection[];

  // ─── Actions ────────────────────────────────────

  setTool: (tool: Tool) => void;
  setViewMode: (mode: 'top' | 'perspective') => void;
  setShowMapOverlay: (show: boolean) => void;
  setSelection: (sel: Selection) => void;
  setRefOrigin: (lat: number, lon: number) => void;
  setGridSize: (size: number) => void;
  setSnapEnabled: (enabled: boolean) => void;

  /** Create a new empty road and start drawing */
  startNewRoad: (startLat: number, startLon: number) => string;

  /** Add a control point to the currently drawing road */
  addControlPoint: (roadId: string, lat: number, lon: number, z?: number) => void;

  /** Update a control point position */
  updateControlPoint: (roadId: string, pointIndex: number, lat: number, lon: number, z?: number) => void;

  /** Set bezier handle on a control point */
  setHandle: (roadId: string, pointIndex: number, handle: 'in' | 'out', offset: Vec2 | null) => void;

  /** Set control point type (smooth/corner) */
  setPointType: (roadId: string, pointIndex: number, type: 'smooth' | 'corner') => void;

  /** Set road profile (SCANeR-style road type) */
  setRoadProfile: (roadId: string, profile: RoadProfile) => void;

  /** Delete a control point */
  deleteControlPoint: (roadId: string, pointIndex: number) => void;

  /** Insert a control point at a specific position in the road */
  insertControlPoint: (roadId: string, afterIndex: number, lat: number, lon: number, z?: number) => void;

  /** Split a road at a control point into two roads */
  splitRoad: (roadId: string, pointIndex: number) => void;

  /** Merge two roads at a shared endpoint */
  mergeRoads: (roadId1: string, roadId2: string) => void;

  /** Delete a road */
  deleteRoad: (roadId: string) => void;

  /** Finish drawing the current road */
  finishDrawing: () => void;

  /** Set pen dragging state */
  setPenDragging: (dragging: boolean) => void;

  /** Undo */
  undo: () => void;
  /** Redo */
  redo: () => void;
  /** Push current state to undo stack (call before mutations) */
  pushHistory: (description: string) => void;

  /** Clear all roads */
  clearAll: () => void;

  /** Recompute intersections from current roads */
  recomputeIntersections: () => void;

  /** Toggle a road in multi-selection (shift-click) */
  toggleRoadSelection: (roadId: string) => void;

  /** Clear multi-selection */
  clearRoadSelection: () => void;

  /** Manually create an intersection between 2 roads at their closest point */
  createIntersectionAtClosestPoint: (roadId1: string, roadId2: string) => void;

  /** Detect intersection between 2 roads (full algorithm: split, trim, generate) */
  detectIntersection: (roadId1: string, roadId2: string) => boolean;

  /** Get the currently drawing road */
  getDrawingRoad: () => Road | null;
}

function snapshot(roads: Road[], description: string): HistorySnapshot {
  return {
    roads: JSON.parse(JSON.stringify(roads)),
    description,
    timestamp: Date.now(),
  };
}

export const useRoadStudioStore = create<RoadStudioState>((set, get) => ({
  roads: [],
  tool: 'select',
  selection: { roadId: null, pointIndices: [], handle: null },
  selectedRoadIds: [],
  refLat: 18.52,
  refLon: 73.85,
  defaultWidth: 8,
  defaultLaneCount: 2,
  gridSize: 10,
  snapEnabled: true,
  undoStack: [],
  redoStack: [],
  drawingRoadId: null,
  penDragging: false,
  viewMode: 'top',
  showMapOverlay: false,
  intersections: [],
  generatedIntersections: [],

  setTool: (tool) => {
    const state = get();
    if (state.drawingRoadId) {
      get().finishDrawing();
    }
    set({ tool, selection: { roadId: null, pointIndices: [], handle: null } });
  },

  setViewMode: (mode) => set({ viewMode: mode }),

  setShowMapOverlay: (show) => set({ showMapOverlay: show }),

  setSelection: (sel) => set({ selection: sel }),

  setRefOrigin: (lat, lon) => set({ refLat: lat, refLon: lon }),

  setGridSize: (size) => set({ gridSize: size }),

  setSnapEnabled: (enabled) => set({ snapEnabled: enabled }),

  startNewRoad: (startLat, startLon) => {
    const state = get();
    const roadId = `road_${generateId()}`;
    const firstPoint: ControlPoint = {
      id: generateId(),
      lat: startLat,
      lon: startLon,
      z: 0,
      handleIn: null,
      handleOut: null,
      type: 'corner',
    };
    const road: Road = {
      id: roadId,
      name: `Road ${state.roads.length + 1}`,
      points: [firstPoint],
      width: state.defaultWidth,
      laneCount: state.defaultLaneCount,
      color: '#4ecca3',
      profile: { ...ROAD_PROFILES.city_2x1 },
    };
    set({
      roads: [...state.roads, road],
      drawingRoadId: roadId,
      selection: { roadId, pointIndices: [0], handle: null },
      undoStack: [...state.undoStack, snapshot(state.roads, 'Start new road')],
      redoStack: [],
    });
    return roadId;
  },

  addControlPoint: (roadId, lat, lon, z = 0) => {
    const state = get();
    const point: ControlPoint = {
      id: generateId(),
      lat,
      lon,
      z,
      handleIn: null,
      handleOut: null,
      type: 'corner',
    };
    set({
      roads: state.roads.map((r) =>
        r.id === roadId
          ? { ...r, points: [...r.points, point] }
          : r
      ),
      selection: { roadId, pointIndices: [0], handle: null },
    });
  },

  updateControlPoint: (roadId, pointIndex, lat, lon, z) => {
    const state = get();
    set({
      roads: state.roads.map((r) =>
        r.id === roadId
          ? {
              ...r,
              points: r.points.map((p, i) =>
                i === pointIndex
                  ? { ...p, lat, lon, z: z ?? p.z }
                  : p
              ),
            }
          : r
      ),
    });
  },

  setHandle: (roadId, pointIndex, handle, offset) => {
    const state = get();
    set({
      roads: state.roads.map((r) =>
        r.id === roadId
          ? {
              ...r,
              points: r.points.map((p, i) => {
                if (i !== pointIndex) return p;
                if (handle === 'in') {
                  return { ...p, handleIn: offset, type: 'smooth' as const };
                } else {
                  return { ...p, handleOut: offset, type: 'smooth' as const };
                }
              }),
            }
          : r
      ),
    });
  },

  setPointType: (roadId, pointIndex, type) => {
    const state = get();
    set({
      roads: state.roads.map((r) =>
        r.id === roadId
          ? {
              ...r,
              points: r.points.map((p, i) =>
                i === pointIndex
                  ? {
                      ...p,
                      type,
                      handleIn: type === 'corner' ? null : p.handleIn,
                      handleOut: type === 'corner' ? null : p.handleOut,
                    }
                  : p
              ),
            }
          : r
      ),
    });
  },

  setRoadProfile: (roadId, profile) => {
    const state = get();
    get().pushHistory('Set road profile');
    // Update width based on profile
    const width = profile.laneWidth * (profile.type.includes('2x2') ? 4 : profile.type.includes('2x3') ? 6 : 2);
    set({
      roads: state.roads.map((r) =>
        r.id === roadId
          ? { ...r, profile: { ...profile }, width, laneCount: width / profile.laneWidth }
          : r
      ),
    });
  },

  deleteControlPoint: (roadId, pointIndex) => {
    const state = get();
    get().pushHistory('Delete control point');
    set({
      roads: state.roads
        .map((r) =>
          r.id === roadId
            ? { ...r, points: r.points.filter((_, i) => i !== pointIndex) }
            : r
        )
        .filter((r) => r.points.length >= 1),
      selection: { roadId: null, pointIndices: [], handle: null },
    });
  },

  insertControlPoint: (roadId, afterIndex, lat, lon, z = 0) => {
    const state = get();
    const point: ControlPoint = {
      id: generateId(),
      lat,
      lon,
      z,
      handleIn: null,
      handleOut: null,
      type: 'corner',
    };
    get().pushHistory('Insert control point');
    set({
      roads: state.roads.map((r) =>
        r.id === roadId
          ? {
              ...r,
              points: [
                ...r.points.slice(0, afterIndex + 1),
                point,
                ...r.points.slice(afterIndex + 1),
              ],
            }
          : r
      ),
      selection: { roadId, pointIndices: [afterIndex + 1], handle: null },
    });
  },

  splitRoad: (roadId, pointIndex) => {
    const state = get();
    const road = state.roads.find((r) => r.id === roadId);
    if (!road || pointIndex <= 0 || pointIndex >= road.points.length - 1) return;

    get().pushHistory('Split road');
    const road1: Road = {
      ...road,
      id: `road_${generateId()}`,
      name: `${road.name} A`,
      points: road.points.slice(0, pointIndex + 1),
    };
    const road2: Road = {
      ...road,
      id: `road_${generateId()}`,
      name: `${road.name} B`,
      points: road.points.slice(pointIndex),
    };
    set({
      roads: [...state.roads.filter((r) => r.id !== roadId), road1, road2],
      selection: { roadId: road2.id, pointIndices: [0], handle: null },
    });
  },

  mergeRoads: (roadId1, roadId2) => {
    const state = get();
    const r1 = state.roads.find((r) => r.id === roadId1);
    const r2 = state.roads.find((r) => r.id === roadId2);
    if (!r1 || !r2) return;

    get().pushHistory('Merge roads');
    const merged: Road = {
      ...r1,
      points: [...r1.points, ...r2.points.slice(1)],
    };
    set({
      roads: state.roads.filter((r) => r.id !== roadId1 && r.id !== roadId2).concat(merged),
      selection: { roadId: merged.id, pointIndices: [], handle: null },
    });
  },

  deleteRoad: (roadId) => {
    const state = get();
    get().pushHistory('Delete road');
    set({
      roads: state.roads.filter((r) => r.id !== roadId),
      selection: { roadId: null, pointIndices: [], handle: null },
    });
  },

  finishDrawing: () => {
    const state = get();
    // Remove roads with only 1 point (not a valid road)
    const roads = state.roads.filter((r) =>
      r.id === state.drawingRoadId ? r.points.length >= 2 : true
    );
    set({
      roads,
      drawingRoadId: null,
      selection: { roadId: null, pointIndices: [], handle: null },
    });
  },

  setPenDragging: (dragging) => set({ penDragging: dragging }),

  pushHistory: (description) => {
    const state = get();
    set({
      undoStack: [...state.undoStack, snapshot(state.roads, description)].slice(-50),
      redoStack: [],
    });
  },

  undo: () => {
    const state = get();
    if (state.undoStack.length === 0) return;
    const prev = state.undoStack[state.undoStack.length - 1];
    set({
      roads: prev.roads,
      undoStack: state.undoStack.slice(0, -1),
      redoStack: [...state.redoStack, snapshot(state.roads, 'redo')],
      selection: { roadId: null, pointIndices: [], handle: null },
      drawingRoadId: null,
    });
  },

  redo: () => {
    const state = get();
    if (state.redoStack.length === 0) return;
    const next = state.redoStack[state.redoStack.length - 1];
    set({
      roads: next.roads,
      redoStack: state.redoStack.slice(0, -1),
      undoStack: [...state.undoStack, snapshot(state.roads, 'undo')],
      selection: { roadId: null, pointIndices: [], handle: null },
      drawingRoadId: null,
    });
  },

  clearAll: () => {
    const state = get();
    get().pushHistory('Clear all');
    set({
      roads: [],
      selection: { roadId: null, pointIndices: [], handle: null },
      drawingRoadId: null,
      intersections: [],
    });
  },

  recomputeIntersections: () => {
    const state = get();
    const intersections = detectIntersections(state.roads);
    set({ intersections });
  },

  toggleRoadSelection: (roadId) => {
    const state = get();
    if (state.selectedRoadIds.includes(roadId)) {
      // Remove if already selected
      set({ selectedRoadIds: state.selectedRoadIds.filter((id) => id !== roadId) });
    } else {
      // Add (limit to 2 for intersection creation)
      set({ selectedRoadIds: [...state.selectedRoadIds, roadId].slice(-2) });
    }
  },

  clearRoadSelection: () => set({ selectedRoadIds: [] }),

  createIntersectionAtClosestPoint: (roadId1, roadId2) => {
    const state = get();
    const road1 = state.roads.find((r) => r.id === roadId1);
    const road2 = state.roads.find((r) => r.id === roadId2);
    if (!road1 || !road2 || road1.points.length < 2 || road2.points.length < 2) return;

    // Sample both roads
    const samples1 = sampleRoad(road1, state.refLat, state.refLon, 16);
    const samples2 = sampleRoad(road2, state.refLat, state.refLon, 16);

    // Find the closest pair of points between the two roads
    let minDist = Infinity;
    let bestS1 = samples1[0];
    let bestS2 = samples2[0];
    let bestIdx1 = 0;
    let bestIdx2 = 0;

    for (let i = 0; i < samples1.length; i++) {
      for (let j = 0; j < samples2.length; j++) {
        const dx = samples1[i].x - samples2[j].x;
        const dy = samples1[i].y - samples2[j].y;
        const d = Math.sqrt(dx * dx + dy * dy);
        if (d < minDist) {
          minDist = d;
          bestS1 = samples1[i];
          bestS2 = samples2[j];
          bestIdx1 = i;
          bestIdx2 = j;
        }
      }
    }

    // Intersection center = midpoint of closest points
    const centerLat = (bestS1.x + bestS2.x) / 2;
    const centerLon = (bestS1.y + bestS2.y) / 2;
    const centerZ = (bestS1.z + bestS2.z) / 2;

    // Convert local meters back to lat/lon
    const geo = localToGeo(centerLat, centerLon, state.refLat, state.refLon);

    // Determine which end of each road is closest to the intersection
    const dist1Start = Math.sqrt(
      (samples1[0].x - centerLat) ** 2 + (samples1[0].y - centerLon) ** 2
    );
    const dist1End = Math.sqrt(
      (samples1[samples1.length - 1].x - centerLat) ** 2 +
      (samples1[samples1.length - 1].y - centerLon) ** 2
    );
    const dist2Start = Math.sqrt(
      (samples2[0].x - centerLat) ** 2 + (samples2[0].y - centerLon) ** 2
    );
    const dist2End = Math.sqrt(
      (samples2[samples2.length - 1].x - centerLat) ** 2 +
      (samples2[samples2.length - 1].y - centerLon) ** 2
    );

    const end1 = dist1Start < dist1End ? 'start' : 'end';
    const end2 = dist2Start < dist2End ? 'start' : 'end';

    // Create the intersection
    const newIntersection: Intersection = {
      id: `ix_${generateId()}`,
      name: `Intersection ${state.intersections.length + 1}`,
      lat: geo.lat,
      lon: geo.lon,
      z: centerZ,
      connections: [
        { roadId: roadId1, end: end1 },
        { roadId: roadId2, end: end2 },
      ],
      bannedLinks: [],
      autoDetected: false,
    };

    // Also update the road endpoints to actually meet at the intersection
    // (snap the closest endpoint to the intersection center)
    const snapEndpoint = (road: Road, end: 'start' | 'end', lat: number, lon: number, z: number): Road => {
      const points = [...road.points];
      if (end === 'start') {
        points[0] = { ...points[0], lat, lon, z };
      } else {
        points[points.length - 1] = { ...points[points.length - 1], lat, lon, z };
      }
      return { ...road, points };
    };

    const updatedRoads = state.roads.map((r) => {
      if (r.id === roadId1) return snapEndpoint(r, end1, geo.lat, geo.lon, centerZ);
      if (r.id === roadId2) return snapEndpoint(r, end2, geo.lat, geo.lon, centerZ);
      return r;
    });

    get().pushHistory('Create intersection');
    set({
      roads: updatedRoads,
      intersections: [...state.intersections, newIntersection],
      selectedRoadIds: [],
      selection: { roadId: null, pointIndices: [], handle: null },
    });
  },

  detectIntersection: (roadId1, roadId2) => {
    const state = get();
    const road1 = state.roads.find((r) => r.id === roadId1);
    const road2 = state.roads.find((r) => r.id === roadId2);
    if (!road1 || !road2 || road1.points.length < 2 || road2.points.length < 2) return false;

    // Run the full intersection generation algorithm
    const generated = generateIntersection(road1, road2, state.refLat, state.refLon);
    if (!generated) return false;

    // Step 3 (split): Trim the original roads at the intersection boundary
    // For each road, find the closest sample to the intersection center,
    // then trim the road to end at the trim distance from center.
    const trimRoad = (road: Road): Road => {
      const samples = sampleRoad(road, state.refLat, state.refLon, 24);
      if (samples.length < 2) return road;

      // Find closest sample to intersection center
      let closestIdx = 0;
      let closestDist = Infinity;
      for (let i = 0; i < samples.length; i++) {
        const d = Math.sqrt((samples[i].x - generated.center.x) ** 2 + (samples[i].y - generated.center.y) ** 2);
        if (d < closestDist) { closestDist = d; closestIdx = i; }
      }

      // Find which approaches belong to this road
      const roadApproaches = generated.approaches.filter((a) => a.roadId === road.id);
      if (roadApproaches.length === 0) return road;

      // Determine trim indices: the road should be cut at the intersection
      // Keep the part before the first approach's start, and the part after the last approach's end
      const maxHalfWidth = Math.max(road1.width, road2.width) / 2;
      const trimDist = maxHalfWidth + 2;

      // Find the sample indices at trim distance from center (before and after closest)
      let trimBefore = -1;
      let trimAfter = -1;
      for (let i = 0; i < samples.length; i++) {
        const d = Math.sqrt((samples[i].x - generated.center.x) ** 2 + (samples[i].y - generated.center.y) ** 2);
        if (i <= closestIdx && d >= trimDist) trimBefore = i;
        if (i >= closestIdx && d >= trimDist && trimAfter === -1) trimAfter = i;
      }

      // Build new control points: keep points before trimBefore and after trimAfter
      // Convert sample points back to lat/lon
      const newPoints: ControlPoint[] = [];

      // Keep original control points that are before the trim region
      // Simple approach: find which original control points are outside the trim zone
      for (const cp of road.points) {
        const local = geoToLocal(cp.lat, cp.lon, state.refLat, state.refLon);
        const d = Math.sqrt((local.x - generated.center.x) ** 2 + (local.y - generated.center.y) ** 2);
        if (d >= trimDist) {
          newPoints.push(cp);
        }
      }

      // If we removed all points, keep the road as-is (shouldn't happen)
      if (newPoints.length < 2) return road;

      // Add trimmed endpoints at the trim boundary
      // Find the direction from center toward the first remaining point
      if (newPoints.length > 0) {
        const firstLocal = geoToLocal(newPoints[0].lat, newPoints[0].lon, state.refLat, state.refLon);
        const dx1 = firstLocal.x - generated.center.x;
        const dy1 = firstLocal.y - generated.center.y;
        const d1 = Math.sqrt(dx1 * dx1 + dy1 * dy1);
        if (d1 > trimDist) {
          // Add a trimmed point at trimDist from center (toward first point)
          const trimPt = localToGeo(
            generated.center.x + (dx1 / d1) * trimDist,
            generated.center.y + (dy1 / d1) * trimDist,
            state.refLat, state.refLon
          );
          newPoints.unshift({
            id: generateId(),
            lat: trimPt.lat, lon: trimPt.lon, z: generated.approaches[0]?.z ?? 0,
            handleIn: null, handleOut: null, type: 'corner',
          });
        }

        const lastLocal = geoToLocal(newPoints[newPoints.length - 1].lat, newPoints[newPoints.length - 1].lon, state.refLat, state.refLon);
        const dx2 = lastLocal.x - generated.center.x;
        const dy2 = lastLocal.y - generated.center.y;
        const d2 = Math.sqrt(dx2 * dx2 + dy2 * dy2);
        if (d2 > trimDist) {
          const trimPt = localToGeo(
            generated.center.x + (dx2 / d2) * trimDist,
            generated.center.y + (dy2 / d2) * trimDist,
            state.refLat, state.refLon
          );
          newPoints.push({
            id: generateId(),
            lat: trimPt.lat, lon: trimPt.lon, z: generated.approaches[0]?.z ?? 0,
            handleIn: null, handleOut: null, type: 'corner',
          });
        }
      }

      return { ...road, points: newPoints };
    };

    const updatedRoads = state.roads.map((r) =>
      r.id === roadId1 || r.id === roadId2 ? trimRoad(r) : r
    );

    // Also create an Intersection object for compatibility with existing rendering
    const ixLocal = generated.center;
    const ixGeo = localToGeo(ixLocal.x, ixLocal.y, state.refLat, state.refLon);
    const newIntersection: Intersection = {
      id: `ix_${generateId()}`,
      name: `Intersection ${state.intersections.length + state.generatedIntersections.length + 1}`,
      lat: ixGeo.lat,
      lon: ixGeo.lon,
      z: generated.approaches[0]?.z ?? 0,
      connections: generated.approaches.map((a) => ({
        roadId: a.roadId,
        end: 'end' as const, // approaches are always from the end
      })),
      bannedLinks: [],
      autoDetected: false,
    };

    get().pushHistory('Detect intersection');
    set({
      roads: updatedRoads,
      intersections: [...state.intersections, newIntersection],
      generatedIntersections: [...state.generatedIntersections, generated],
      selectedRoadIds: [],
      selection: { roadId: null, pointIndices: [], handle: null },
    });

    return true;
  },

  getDrawingRoad: () => {
    const state = get();
    if (!state.drawingRoadId) return null;
    return state.roads.find((r) => r.id === state.drawingRoadId) ?? null;
  },
}));
