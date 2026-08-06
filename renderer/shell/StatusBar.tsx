/**
 * StatusBar — bottom status bar (24px) showing live application state.
 *
 * Left:   project name, cursor coordinates (lat/lon), zoom level, CRS, tile count
 * Center: selection info, export progress
 * Right:  active jobs, memory, workspace name, version
 *
 * Clicking coordinates copies to clipboard.
 */
import React, { useCallback, useState, useEffect } from 'react';
import {
  Loader2, MapPin, Crosshair, Layers as LayersIcon,
  Download, Database, Activity, Box, Cpu,
} from 'lucide-react';
import { useCoreStore } from '../core/coreStore';
import { useTerrainStore } from '../core/store';
import { getIpcBridge } from '../core/ipc';
import { NATIVE_GET_MEMORY_USAGE } from '../../shared/ipcChannels-electron';

interface StatusBarProps {
  /** Live cursor coordinates from the map */
  coords?: { lat: number; lon: number } | null;
  zoom?: number;
  fps?: number;
}

const Divider: React.FC = () => <span className="w-px h-3.5 bg-edge mx-2.5 shrink-0" />;

export const StatusBar: React.FC<StatusBarProps> = ({ coords, zoom, fps: fpsProp }) => {
  const fps = fpsProp;
  const activeProject = useCoreStore((s) => s.activeProject);
  const activeWorkspace = useCoreStore((s) => s.activeWorkspace);
  const jobs = useCoreStore((s) => s.jobs);
  const selection = useCoreStore((s) => s.selection);
  const crsSource = useTerrainStore((s) => s.crsSource);
  const selectedTiles = useTerrainStore((s) => s.selectedTiles);
  const exportProgress = useTerrainStore((s) => s.exportProgress);
  const selectedBounds = useTerrainStore((s) => s.selectedBounds);

  const activeJobs = jobs.filter((j) => j.status === 'running' || j.status === 'pending');
  const selectionCount = selection.length;
  const tileCount = selectedTiles.size;

  // Memory usage (polled every 15s from main process)
  const [memoryMb, setMemoryMb] = useState<number | null>(null);
  useEffect(() => {
    const fetchMemory = () => {
      const ipc = getIpcBridge();
      if (!ipc) return;
      ipc.invoke(NATIVE_GET_MEMORY_USAGE).then((mb: number) => {
        if (typeof mb === 'number') setMemoryMb(mb);
      }).catch(() => {});
    };
    fetchMemory();
    const interval = setInterval(fetchMemory, 15000);
    return () => clearInterval(interval);
  }, []);

  const copyCoords = useCallback(() => {
    if (!coords) return;
    navigator.clipboard?.writeText(`${coords.lat.toFixed(6)}, ${coords.lon.toFixed(6)}`);
  }, [coords]);

  // Compute area of selected bounds (in km²)
  const areaKm2 = selectedBounds
    ? ((selectedBounds.north - selectedBounds.south) * 111) *
      ((selectedBounds.east - selectedBounds.west) * 111 * Math.cos((selectedBounds.north + selectedBounds.south) / 2 * Math.PI / 180))
    : null;

  return (
    <footer
      className="flex items-center h-6 px-3 text-3xs text-fg-secondary bg-surface-elevated border-t border-edge shrink-0 select-none"
      role="status"
      aria-live="polite"
      aria-label="Application status"
    >
      {/* Project name */}
      {activeProject && (
        <>
          <span className="text-fg-primary font-medium truncate max-w-[180px]">
            {activeProject.name}
          </span>
          <Divider />
        </>
      )}

      {/* Coordinates */}
      <button
        onClick={copyCoords}
        className="flex items-center gap-1 hover:text-fg-primary transition-colors tabular-nums"
        title="Click to copy coordinates"
        disabled={!coords}
      >
        <MapPin size={11} className={coords ? 'text-accent' : 'text-fg-muted'} />
        {coords
          ? `${coords.lat.toFixed(4)}°, ${coords.lon.toFixed(4)}°`
          : '—'}
      </button>

      <Divider />

      {/* Zoom */}
      <span className="flex items-center gap-1 tabular-nums">
        <Crosshair size={11} className="text-fg-muted" />
        Zoom: {zoom ?? '—'}
      </span>

      <Divider />

      {/* CRS */}
      <span className="flex items-center gap-1">
        <span className="text-fg-muted">CRS:</span>
        <span className="text-fg-primary">{crsSource}</span>
      </span>

      {/* Tile count (only if tiles selected) */}
      {tileCount > 0 && (
        <>
          <Divider />
          <span className="flex items-center gap-1 tabular-nums">
            <Box size={11} className="text-fg-muted" />
            {tileCount} tile{tileCount > 1 ? 's' : ''}
          </span>
        </>
      )}

      {/* Area (only if bounds selected) */}
      {areaKm2 !== null && areaKm2 > 0 && (
        <>
          <Divider />
          <span className="flex items-center gap-1 tabular-nums text-fg-muted">
            {areaKm2 < 1 ? `${(areaKm2 * 1000000).toFixed(0)} m²` : `${areaKm2.toFixed(2)} km²`}
          </span>
        </>
      )}

      {/* Selection (only if something selected) */}
      {selectionCount > 0 && (
        <>
          <Divider />
          <span className="flex items-center gap-1">
            <LayersIcon size={11} className="text-accent" />
            {selectionCount} selected
          </span>
        </>
      )}

      {/* Export progress (center, only when exporting) */}
      {exportProgress && (
        <>
          <Divider />
          <span className="flex items-center gap-1 text-info tabular-nums">
            <Download size={11} className="animate-pulse" />
            {exportProgress.stage}: {exportProgress.percent ?? 0}%
          </span>
        </>
      )}

      {/* Right side: jobs + memory + FPS + workspace */}
      <div className="ml-auto flex items-center">
        {activeJobs.length > 0 && (
          <>
            <span className="flex items-center gap-1 text-accent tabular-nums">
              <Loader2 size={11} className="animate-spin" />
              {activeJobs.length} job{activeJobs.length > 1 ? 's' : ''}
            </span>
            <Divider />
          </>
        )}

        {/* Memory usage */}
        {memoryMb !== null && (
          <>
            <span className="flex items-center gap-1 tabular-nums text-fg-muted" title="Process memory usage">
              <Database size={11} />
              {memoryMb < 1024 ? `${memoryMb.toFixed(0)} MB` : `${(memoryMb / 1024).toFixed(1)} GB`}
            </span>
            <Divider />
          </>
        )}

        {fps !== undefined && (
          <>
            <span className="flex items-center gap-1 tabular-nums text-fg-muted">
              <Cpu size={11} />
              {fps} FPS
            </span>
            <Divider />
          </>
        )}

        {/* Active workspace — always shown so the user knows where they are */}
        {activeWorkspace && (
          <>
            <span className="flex items-center gap-1 text-fg-secondary">
              <Activity size={11} className="text-fg-muted" />
              {activeWorkspace.name}
            </span>
            <Divider />
          </>
        )}

        <span className="text-fg-muted">OpenGeoStudio</span>
      </div>
    </footer>
  );
};
