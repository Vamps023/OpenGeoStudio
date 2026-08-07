/**
 * Road Studio Store — Zustand state for road editing.
 *
 * Manages roads, control points, tool selection, undo/redo,
 * and the reference origin for geo-to-local conversion.
 */

import { create } from 'zustand';
import type { Road, ControlPoint, Tool, Selection, HistorySnapshot, Vec2, RoadProfile, Intersection, GeneratedIntersection, Point2D, CircleArc } from '../../shared/types';
import { generateId, ROAD_PROFILES, detectIntersections, distanceMeters, sampleRoad, localToGeo, generateIntersection, geoToLocal, computeCircleArc } from '../../shared/types';

/** Compute tangent direction at a sample index */
function computeTangentAtSamples(samples: Array<{ x: number; y: number; z: number }>, idx: number): Point2D {
  let dx: number, dy: number;
  if (idx === 0) {
    dx = samples[1].x - samples[0].x;
    dy = samples[1].y - samples[0].y;
  } else if (idx === samples.length - 1) {
    dx = samples[idx].x - samples[idx - 1].x;
    dy = samples[idx].y - samples[idx - 1].y;
  } else {
    dx = samples[idx + 1].x - samples[idx - 1].x;
    dy = samples[idx + 1].y - samples[idx - 1].y;
  }
  const len = Math.sqrt(dx * dx + dy * dy) || 1;
  return { x: dx / len, y: dy / len };
}

interface RoadStudioState {
  /** All roads in the project */
  roads: Road[];
  /** Active editing tool */
  tool: Tool;
  /** Current selection */
  selection: Selection;
  /** Multi-selected road IDs (for intersection creation, etc.) */
  selectedRoadIds: string[];
  /** When true, next click on a road selects it for intersection (no shift needed) */
  roadPickMode: boolean;
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
  /** Arc tool: the start point of the arc (in local meters) — null if not started */
  arcStartPoint: Point2D | null;
  /** Arc tool: the start direction (normalized) — from the last road segment */
  arcStartDirection: Point2D | null;
  /** Arc tool: current preview arc (updated on mouse move) */
  arcPreview: CircleArc | null;
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

  /** Arc tool: start an arc from a point with a given direction */
  startArc: (startPoint: Point2D, startDirection: Point2D) => void;
  /** Arc tool: update the arc preview based on mouse position */
  updateArcPreview: (endPoint: Point2D) => void;
  /** Arc tool: finish the arc — creates a new road from the arc */
  finishArc: () => void;
  /** Arc tool: cancel the current arc */
  cancelArc: () => void;

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

  /** Enable/disable road pick mode (click to select road without shift) */
  setRoadPickMode: (enabled: boolean) => void;

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
  roadPickMode: false,
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
  arcStartPoint: null,
  arcStartDirection: null,
  arcPreview: null,
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

  startArc: (startPoint, startDirection) => {
    set({ arcStartPoint: startPoint, arcStartDirection: startDirection, arcPreview: null });
  },

  updateArcPreview: (endPoint) => {
    const state = get();
    if (!state.arcStartPoint || !state.arcStartDirection) return;
    const arc = computeCircleArc(state.arcStartPoint, state.arcStartDirection, endPoint, 32);
    set({ arcPreview: arc });
  },

  finishArc: () => {
    const state = get();
    if (!state.arcPreview || !state.arcPreview.points || state.arcPreview.points.length < 2) {
      set({ arcStartPoint: null, arcStartDirection: null, arcPreview: null });
      return;
    }

    // Convert arc points to control points
    const controlPoints: ControlPoint[] = state.arcPreview.points.map((p) => {
      const geo = localToGeo(p.x, p.y, state.refLat, state.refLon);
      return {
        id: generateId(),
        lat: geo.lat,
        lon: geo.lon,
        z: 0,
        handleIn: null,
        handleOut: null,
        type: 'smooth' as const,
      };
    });

    // Create a new road from the arc
    const roadId = `road_${generateId()}`;
    const road: Road = {
      id: roadId,
      name: `Arc Road ${state.roads.length + 1}`,
      points: controlPoints,
      width: state.defaultWidth,
      laneCount: state.defaultLaneCount,
      color: '#4ecca3',
      profile: { ...ROAD_PROFILES.city_2x1 },
      startIntersectionId: null,
      endIntersectionId: null,
    };

    get().pushHistory('Finish arc road');
    set({
      roads: [...state.roads, road],
      arcStartPoint: null,
      arcStartDirection: null,
      arcPreview: null,
      selection: { roadId, pointIndices: [], handle: null },
    });
  },

  cancelArc: () => set({ arcStartPoint: null, arcStartDirection: null, arcPreview: null }),

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

  clearRoadSelection: () => set({ selectedRoadIds: [], roadPickMode: false }),

  setRoadPickMode: (enabled) => set({ roadPickMode: enabled }),

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

    // Step 3 (split): Split each road into two halves at the intersection,
    // trimming each end so roads stop well before the junction.
    // Trim distance is angle-dependent: trimDist = halfWidth_other + R × tan(θ/2)
    const halfWidth1 = road1.width / 2;
    const halfWidth2 = road2.width / 2;
    const maxHalfWidth = Math.max(halfWidth1, halfWidth2);
    const cornerRadius = Math.min(maxHalfWidth, 5);

    // Compute angle between the two roads at the intersection
    const s1Angle = sampleRoad(road1, state.refLat, state.refLon, 32);
    const s2Angle = sampleRoad(road2, state.refLat, state.refLon, 32);
    let idx1Angle = 0, idx2Angle = 0;
    let minD1 = Infinity, minD2 = Infinity;
    for (let i = 0; i < s1Angle.length; i++) {
      const d = Math.sqrt((s1Angle[i].x - generated.center.x) ** 2 + (s1Angle[i].y - generated.center.y) ** 2);
      if (d < minD1) { minD1 = d; idx1Angle = i; }
    }
    for (let i = 0; i < s2Angle.length; i++) {
      const d = Math.sqrt((s2Angle[i].x - generated.center.x) ** 2 + (s2Angle[i].y - generated.center.y) ** 2);
      if (d < minD2) { minD2 = d; idx2Angle = i; }
    }
    // Tangents at intersection
    const t1 = computeTangentAtSamples(s1Angle, idx1Angle);
    const t2 = computeTangentAtSamples(s2Angle, idx2Angle);
    const dot = t1.x * t2.x + t1.y * t2.y;
    const angleBetween = Math.acos(Math.min(1, Math.max(-1, Math.abs(dot))));
    const halfAngle = angleBetween / 2;

    // Trim distance for each road (along its centerline)
    // Road 1 stops where road 2's edge + corner arc begins
    const trimDist1 = halfWidth2 + cornerRadius * Math.tan(halfAngle);
    const trimDist2 = halfWidth1 + cornerRadius * Math.tan(halfAngle);

    const splitAndTrimRoad = (road: Road, trimDist: number): Road[] => {
      const samples = sampleRoad(road, state.refLat, state.refLon, 32);
      if (samples.length < 2) return [road];

      // Find closest sample to intersection center
      let closestIdx = 0;
      let closestDist = Infinity;
      for (let i = 0; i < samples.length; i++) {
        const d = Math.sqrt((samples[i].x - generated.center.x) ** 2 + (samples[i].y - generated.center.y) ** 2);
        if (d < closestDist) { closestDist = d; closestIdx = i; }
      }

      // Find trim indices
      let trimBeforeIdx = -1;
      let trimAfterIdx = -1;
      for (let i = 0; i < samples.length; i++) {
        const d = Math.sqrt((samples[i].x - generated.center.x) ** 2 + (samples[i].y - generated.center.y) ** 2);
        if (i < closestIdx && d >= trimDist) trimBeforeIdx = i;
        if (i > closestIdx && d >= trimDist && trimAfterIdx === -1) trimAfterIdx = i;
      }

      const toCP = (x: number, y: number, z: number): ControlPoint => {
        const geo = localToGeo(x, y, state.refLat, state.refLon);
        return {
          id: generateId(),
          lat: geo.lat, lon: geo.lon, z,
          handleIn: null, handleOut: null, type: 'corner',
        };
      };

      const newRoads: Road[] = [];

      // Road A: from start to trim boundary (before intersection)
      if (trimBeforeIdx >= 0) {
        const ptsA: ControlPoint[] = [];
        for (const cp of road.points) {
          const local = geoToLocal(cp.lat, cp.lon, state.refLat, state.refLon);
          const d = Math.sqrt((local.x - generated.center.x) ** 2 + (local.y - generated.center.y) ** 2);
          if (d >= trimDist) {
            // Check if on the "before" side
            let cpClosestIdx = 0;
            let cpClosestDist = Infinity;
            for (let i = 0; i <= closestIdx; i++) {
              const sd = Math.sqrt((samples[i].x - local.x) ** 2 + (samples[i].y - local.y) ** 2);
              if (sd < cpClosestDist) { cpClosestDist = sd; cpClosestIdx = i; }
            }
            if (cpClosestIdx <= closestIdx) ptsA.push(cp);
          }
        }
        // Add trimmed endpoint at exact trimDist
        const trimSample = samples[trimBeforeIdx];
        const dTrim = Math.sqrt((trimSample.x - generated.center.x) ** 2 + (trimSample.y - generated.center.y) ** 2);
        if (dTrim > 0) {
          const dx = trimSample.x - generated.center.x;
          const dy = trimSample.y - generated.center.y;
          const ratio = trimDist / dTrim;
          ptsA.push(toCP(
            generated.center.x + dx * ratio,
            generated.center.y + dy * ratio,
            trimSample.z
          ));
        }
        if (ptsA.length >= 2) {
          newRoads.push({ ...road, id: `road_${generateId()}`, name: `${road.name} A`, points: ptsA });
        }
      }

      // Road B: from trim boundary (after intersection) to end
      if (trimAfterIdx >= 0 && trimAfterIdx < samples.length) {
        const ptsB: ControlPoint[] = [];
        // Add trimmed endpoint at exact trimDist
        const trimSample = samples[trimAfterIdx];
        const dTrim = Math.sqrt((trimSample.x - generated.center.x) ** 2 + (trimSample.y - generated.center.y) ** 2);
        if (dTrim > 0) {
          const dx = trimSample.x - generated.center.x;
          const dy = trimSample.y - generated.center.y;
          const ratio = trimDist / dTrim;
          ptsB.push(toCP(
            generated.center.x + dx * ratio,
            generated.center.y + dy * ratio,
            trimSample.z
          ));
        }
        for (const cp of road.points) {
          const local = geoToLocal(cp.lat, cp.lon, state.refLat, state.refLon);
          const d = Math.sqrt((local.x - generated.center.x) ** 2 + (local.y - generated.center.y) ** 2);
          if (d >= trimDist) {
            let cpClosestIdx = 0;
            let cpClosestDist = Infinity;
            for (let i = closestIdx; i < samples.length; i++) {
              const sd = Math.sqrt((samples[i].x - local.x) ** 2 + (samples[i].y - local.y) ** 2);
              if (sd < cpClosestDist) { cpClosestDist = sd; cpClosestIdx = i; }
            }
            if (cpClosestIdx >= closestIdx) ptsB.push(cp);
          }
        }
        if (ptsB.length >= 2) {
          newRoads.push({ ...road, id: `road_${generateId()}`, name: `${road.name} B`, points: ptsB });
        }
      }

      if (newRoads.length === 0) return [road];
      return newRoads;
    };

    const roads1 = splitAndTrimRoad(road1, trimDist1);
    const roads2 = splitAndTrimRoad(road2, trimDist2);
    const updatedRoads = state.roads
      .filter((r) => r.id !== roadId1 && r.id !== roadId2)
      .concat(roads1, roads2);

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
