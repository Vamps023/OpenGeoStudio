/**
 * RoadToolbar — Minimal editing toolbar (Figma-style)
 *
 * Tools: Select, Line, Pen
 * Actions: Undo, Redo, Delete, Finish
 * Elevation: Slider for selected control point Z value
 */

import React from 'react';
import {
  MousePointer2, Spline, PenTool, Undo2, Redo2, Trash2,
  Check, Mountain, Grid3x3, Magnet, Square, Box, GitMerge, X, Circle,
} from 'lucide-react';
import { useRoadStudioStore } from './store/roadStudioStore';
import { ROAD_PROFILES } from '../shared/types';

const ToolButton: React.FC<{
  active: boolean;
  onClick: () => void;
  icon: React.ReactNode;
  label: string;
  shortcut?: string;
}> = ({ active, onClick, icon, label, shortcut }) => (
  <button
    onClick={onClick}
    className={`flex items-center gap-2 px-3 h-8 rounded-md text-xs font-medium transition-colors ${
      active
        ? 'bg-accent/20 text-accent border border-accent/40'
        : 'text-fg-secondary hover:text-fg-primary hover:bg-surface-elevated border border-transparent'
    }`}
    title={`${label}${shortcut ? ` (${shortcut})` : ''}`}
  >
    {icon}
    <span className="hidden sm:inline">{label}</span>
  </button>
);

const IconButton: React.FC<{
  onClick: () => void;
  icon: React.ReactNode;
  label: string;
  disabled?: boolean;
}> = ({ onClick, icon, label, disabled }) => (
  <button
    onClick={onClick}
    disabled={disabled}
    className="flex items-center justify-center w-8 h-8 rounded-md text-fg-secondary hover:text-fg-primary hover:bg-surface-elevated disabled:opacity-30 disabled:cursor-not-allowed transition-colors"
    title={label}
  >
    {icon}
  </button>
);

export const RoadToolbar: React.FC = () => {
  const tool = useRoadStudioStore((s) => s.tool);
  const setTool = useRoadStudioStore((s) => s.setTool);
  const undo = useRoadStudioStore((s) => s.undo);
  const redo = useRoadStudioStore((s) => s.redo);
  const undoStack = useRoadStudioStore((s) => s.undoStack);
  const redoStack = useRoadStudioStore((s) => s.redoStack);
  const selection = useRoadStudioStore((s) => s.selection);
  const roads = useRoadStudioStore((s) => s.roads);
  const updateControlPoint = useRoadStudioStore((s) => s.updateControlPoint);
  const deleteControlPoint = useRoadStudioStore((s) => s.deleteControlPoint);
  const deleteRoad = useRoadStudioStore((s) => s.deleteRoad);
  const finishDrawing = useRoadStudioStore((s) => s.finishDrawing);
  const drawingRoadId = useRoadStudioStore((s) => s.drawingRoadId);
  const gridSize = useRoadStudioStore((s) => s.gridSize);
  const setGridSize = useRoadStudioStore((s) => s.setGridSize);
  const snapEnabled = useRoadStudioStore((s) => s.snapEnabled);
  const setSnapEnabled = useRoadStudioStore((s) => s.setSnapEnabled);
  const setPointType = useRoadStudioStore((s) => s.setPointType);
  const viewMode = useRoadStudioStore((s) => s.viewMode);
  const setViewMode = useRoadStudioStore((s) => s.setViewMode);
  const setRoadProfile = useRoadStudioStore((s) => s.setRoadProfile);
  const selectedRoadIds = useRoadStudioStore((s) => s.selectedRoadIds);
  const toggleRoadSelection = useRoadStudioStore((s) => s.toggleRoadSelection);
  const clearRoadSelection = useRoadStudioStore((s) => s.clearRoadSelection);
  const detectIntersection = useRoadStudioStore((s) => s.detectIntersection);
  const roadPickMode = useRoadStudioStore((s) => s.roadPickMode);
  const setRoadPickMode = useRoadStudioStore((s) => s.setRoadPickMode);

  // Selected control point
  const selectedRoad = roads.find((r) => r.id === selection.roadId);
  const selectedPoint =
    selectedRoad && selection.pointIndices.length > 0
      ? selectedRoad.points[selection.pointIndices[0]]
      : null;

  return (
    <div className="flex items-center gap-1 px-3 h-12 bg-surface-base border-b border-edge shrink-0">
      {/* ─── Tools ─────────────────────────────── */}
      <div className="flex items-center gap-1">
        <ToolButton active={tool === 'select'} onClick={() => setTool('select')} icon={<MousePointer2 size={14} />} label="Select" shortcut="V" />
        <ToolButton active={tool === 'line'} onClick={() => setTool('line')} icon={<Spline size={14} />} label="Line" shortcut="L" />
        <ToolButton active={tool === 'pen'} onClick={() => setTool('pen')} icon={<PenTool size={14} />} label="Pen" shortcut="P" />
        <ToolButton active={tool === 'arc'} onClick={() => setTool('arc')} icon={<Circle size={14} />} label="Arc" shortcut="A" />
      </div>

      <div className="w-px h-6 bg-edge mx-1" />

      {/* ─── View Mode ────────────────────────── */}
      <ToolButton
        active={viewMode === 'top'}
        onClick={() => setViewMode('top')}
        icon={<Square size={14} />}
        label="Top"
        shortcut="2"
      />
      <ToolButton
        active={viewMode === 'perspective'}
        onClick={() => setViewMode('perspective')}
        icon={<Box size={14} />}
        label="3D"
        shortcut="3"
      />

      <div className="w-px h-6 bg-edge mx-1" />

      {/* ─── Undo/Redo ────────────────────────── */}
      <IconButton onClick={undo} icon={<Undo2 size={16} />} label="Undo (Ctrl+Z)" disabled={undoStack.length === 0} />
      <IconButton onClick={redo} icon={<Redo2 size={16} />} label="Redo (Ctrl+Y)" disabled={redoStack.length === 0} />

      <div className="w-px h-6 bg-edge mx-1" />

      {/* ─── Snapping / Grid ──────────────────── */}
      <IconButton
        onClick={() => setSnapEnabled(!snapEnabled)}
        icon={<Magnet size={16} />}
        label={`Snapping ${snapEnabled ? 'ON' : 'OFF'}`}
      />
      <div className={`flex items-center gap-1 px-2 h-8 rounded-md ${gridSize > 0 ? 'text-fg-primary' : 'text-fg-muted'}`}>
        <Grid3x3 size={14} />
        <input
          type="number"
          value={gridSize}
          onChange={(e) => setGridSize(Math.max(0, parseInt(e.target.value) || 0))}
          className="w-12 bg-transparent text-xs outline-none"
          min={0}
          step={5}
          title="Grid size (meters, 0 = off)"
        />
      </div>

      <div className="w-px h-6 bg-edge mx-1" />

      {/* ─── Drawing actions ──────────────────── */}
      {drawingRoadId && (
        <button
          onClick={finishDrawing}
          className="flex items-center gap-1.5 px-3 h-8 rounded-md bg-ok/20 text-ok border border-ok/40 text-xs font-medium hover:bg-ok/30 transition-colors"
        >
          <Check size={14} />
          Finish Road
        </button>
      )}

      {/* ─── Elevation editor ─────────────────── */}
      {selectedPoint && (
        <>
          <div className="w-px h-6 bg-edge mx-1" />
          <div className="flex items-center gap-2 px-2 h-8 rounded-md bg-surface-elevated">
            <Mountain size={14} className="text-accent" />
            <span className="text-2xs text-fg-muted">Elevation</span>
            <input
              type="range"
              min={-100}
              max={500}
              step={1}
              value={selectedPoint.z}
              onChange={(e) => {
                const z = parseFloat(e.target.value);
                if (selection.roadId && selection.pointIndices.length > 0) {
                  updateControlPoint(selection.roadId, selection.pointIndices[0], selectedPoint.lat, selectedPoint.lon, z);
                }
              }}
              className="w-24 accent-accent"
            />
            <input
              type="number"
              value={Math.round(selectedPoint.z)}
              onChange={(e) => {
                const z = parseFloat(e.target.value) || 0;
                if (selection.roadId && selection.pointIndices.length > 0) {
                  updateControlPoint(selection.roadId, selection.pointIndices[0], selectedPoint.lat, selectedPoint.lon, z);
                }
              }}
              className="w-16 bg-transparent text-xs text-fg-primary outline-none tabular-nums"
              step={1}
            />
            <span className="text-2xs text-fg-muted">m</span>
          </div>

          {/* Point type toggle */}
          <button
            onClick={() => {
              if (selection.roadId && selection.pointIndices.length > 0) {
                const newType = selectedPoint.type === 'smooth' ? 'corner' : 'smooth';
                setPointType(selection.roadId, selection.pointIndices[0], newType);
              }
            }}
            className={`flex items-center px-2 h-8 rounded-md text-xs font-medium transition-colors ${
              selectedPoint.type === 'smooth'
                ? 'bg-accent/20 text-accent border border-accent/40'
                : 'text-fg-secondary hover:text-fg-primary border border-transparent'
            }`}
            title="Toggle smooth/corner"
          >
            {selectedPoint.type === 'smooth' ? 'Smooth' : 'Corner'}
          </button>
        </>
      )}

      {/* ─── Road Profile (SCANeR-style) ──────── */}
      {selectedRoad && (
        <>
          <div className="w-px h-6 bg-edge mx-1" />
          <div className="flex items-center gap-1.5 px-2 h-8 rounded-md bg-surface-elevated">
            <span className="text-2xs text-fg-muted">Profile</span>
            <select
              value={selectedRoad.profile?.type || 'city_2x1'}
              onChange={(e) => {
                const profileType = e.target.value as any;
                const profile = { ...ROAD_PROFILES[profileType] };
                if (profile) setRoadProfile(selectedRoad.id, profile);
              }}
              className="bg-transparent text-xs text-fg-primary outline-none cursor-pointer"
            >
              <option value="city_2x1" className="bg-surface-base">City 2×1</option>
              <option value="city_2x2" className="bg-surface-base">City 2×2</option>
              <option value="country_2x1" className="bg-surface-base">Country 2×1</option>
              <option value="highway_2x3" className="bg-surface-base">Highway 2×3</option>
              <option value="custom" className="bg-surface-base">Custom</option>
            </select>
          </div>
        </>
      )}

      {/* ─── Intersection creation ─────────────── */}
      <div className="ml-auto flex items-center gap-1">
        {/* Select Road button — activates pick mode (no shift needed) */}
        <button
          onClick={() => setRoadPickMode(!roadPickMode)}
          className={`flex items-center gap-1.5 px-3 h-8 rounded-md text-xs font-medium transition-colors border ${
            roadPickMode
              ? 'bg-blue-500/20 text-blue-400 border-blue-500/40'
              : 'text-fg-secondary hover:text-fg-primary hover:bg-surface-elevated border-transparent'
          }`}
          title="Click this, then click on roads to select them for intersection"
        >
          <GitMerge size={14} />
          {roadPickMode ? 'Pick Mode ON — click roads' : 'Select Roads'}
        </button>

        {selectedRoadIds.length === 1 && (
          <div className="flex items-center gap-1.5 px-2 h-8 rounded-md bg-accent/10 text-accent border border-accent/30 text-xs">
            <span>1 road selected — pick 2nd</span>
            <button
              onClick={clearRoadSelection}
              className="ml-1 hover:bg-accent/20 rounded p-0.5"
              title="Cancel"
            >
              <X size={12} />
            </button>
          </div>
        )}
        {selectedRoadIds.length === 2 && (
          <button
            onClick={async () => {
              const success = await detectIntersection(selectedRoadIds[0], selectedRoadIds[1]);
              if (!success) {
                alert('No intersection found between the selected roads. Make sure their centerlines cross or come close together.');
              }
            }}
            className="flex items-center gap-1.5 px-3 h-8 rounded-md bg-accent/20 text-accent border border-accent/40 text-xs font-medium hover:bg-accent/30 transition-colors"
            title="Detect intersection: splits roads, trims ends, generates junction with lane connectivity"
          >
            <GitMerge size={14} />
            Detect Intersection
          </button>
        )}
        {selectedRoadIds.length > 0 && (
          <IconButton
            onClick={clearRoadSelection}
            icon={<X size={16} />}
            label="Clear road selection"
          />
        )}
        {selection.roadId && (
          <IconButton
            onClick={() => {
              if (selection.pointIndices.length > 0) {
                deleteControlPoint(selection.roadId!, selection.pointIndices[0]);
              } else {
                deleteRoad(selection.roadId!);
              }
            }}
            icon={<Trash2 size={16} />}
            label="Delete selected"
          />
        )}
      </div>
    </div>
  );
};
