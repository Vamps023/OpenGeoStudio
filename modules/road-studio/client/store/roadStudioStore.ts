/**
 * Road Studio Store — Zustand state for road editing.
 *
 * Manages roads, control points, tool selection, undo/redo,
 * and the reference origin for geo-to-local conversion.
 */

import { create } from 'zustand';
import type { Road, ControlPoint, Tool, Selection, HistorySnapshot, Vec2, RoadProfile, Intersection } from '../../shared/types';
import { generateId, ROAD_PROFILES, detectIntersections } from '../../shared/types';

interface RoadStudioState {
  /** All roads in the project */
  roads: Road[];
  /** Active editing tool */
  tool: Tool;
  /** Current selection */
  selection: Selection;
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

  getDrawingRoad: () => {
    const state = get();
    if (!state.drawingRoadId) return null;
    return state.roads.find((r) => r.id === state.drawingRoadId) ?? null;
  },
}));
