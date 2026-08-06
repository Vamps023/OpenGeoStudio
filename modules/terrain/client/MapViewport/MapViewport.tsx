/**
 * MapViewport — Terrain area selection map.
 *
 * This is a focused component for the Terrain workspace:
 *   - Shift+drag to select an area (bounding box)
 *   - View tile grid overlay
 *   - Plan generation from selection
 *   - Toggle satellite/labels
 *   - Search location
 *
 * Downloads (TIFF, PNG) are handled by the Export panel based on the selected area.
 * No GIS editing, no OSM overlay, no shapefile — those have been removed.
 */

import React, { useEffect, useRef, useCallback, useState } from 'react';
import 'maplibre-gl/dist/maplibre-gl.css';
import { useTerrainStore } from '@renderer/core/store';
import { useCoreStore } from '@renderer/core/coreStore';
import { getIpcBridge } from '@renderer/core/ipc';
import {
  setBoundsWithUndo, setTileSizeWithUndo,
  selectAllTilesWithUndo, deselectAllTilesWithUndo,
} from '@renderer/core/undoRedoBridge';
import {
  Grid3x3, CheckSquare, Square, Eye, EyeOff, Lock, Type, Download, X,
} from 'lucide-react';
import { SearchBar } from '../SearchBar/SearchBar';
import { WorkflowWizard } from '@renderer/components/WorkflowWizard/WorkflowWizard';
import { useMapInstance } from './hooks/useMapInstance';
import { useSelectionOverlay } from './hooks/useSelectionOverlay';
import { useTileGridOverlay } from './hooks/useTileGridOverlay';
import { useLabelsVisibility } from './hooks/useLabelsVisibility';

interface MapViewportProps {
  className?: string;
}

export function MapViewport({ className }: MapViewportProps): React.JSX.Element {
  const mapContainerRef = useRef<HTMLDivElement>(null);

  const setSelectedBounds = setBoundsWithUndo;
  const selectedBounds = useTerrainStore((s) => s.selectedBounds);
  const tileSizeKm = useTerrainStore((s) => s.tileSizeKm);
  const setTileSizeKm = setTileSizeWithUndo;
  const tileGrid = useTerrainStore((s) => s.tileGrid);
  const selectedTiles = useTerrainStore((s) => s.selectedTiles);
  const selectAllTiles = selectAllTilesWithUndo;
  const deselectAllTiles = deselectAllTilesWithUndo;
  const satelliteVisible = useTerrainStore((s) => s.satelliteVisible);

  const activeWorkspace = useCoreStore((s) => s.activeWorkspace);
  const executeCommand = useCoreStore((s) => s.executeCommand);
  const exportProgress = useTerrainStore((s) => s.exportProgress);
  const [wizardDismissed, setWizardDismissed] = useState(() => {
    return localStorage.getItem('ogstudio:wizard-dismissed') === 'true';
  });
  const showWizard = activeWorkspace?.id === 'terrain' && !wizardDismissed;
  const dismissWizard = useCallback(() => {
    setWizardDismissed(true);
    localStorage.setItem('ogstudio:wizard-dismissed', 'true');
  }, []);

  // Map lifecycle, bbox drag-select, zoom-lock
  const { mapRef, liveBounds, zoomLocked, lockedZoom, toggleZoomLock, placeLabelLayerIds } =
    useMapInstance(mapContainerRef, setSelectedBounds);

  // Overlays (selection rectangle, tile grid, place labels)
  const { selectionVisible, toggleSelectionVisible } = useSelectionOverlay(mapRef, selectedBounds, liveBounds);
  const { gridVisible, toggleGridVisible } = useTileGridOverlay(mapRef, selectedBounds, tileSizeKm);
  const { labelsVisible, toggleLabelsVisible } = useLabelsVisibility(mapRef, placeLabelLayerIds);

  // Toggle satellite imagery layer visibility
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    try {
      map.setLayoutProperty('esri-imagery', 'visibility', satelliteVisible ? 'visible' : 'none');
    } catch {
      // Layer might not exist yet
    }
  }, [mapRef, satelliteVisible]);

  // Listen for map commands from main process (zoom, fit-selection)
  useEffect(() => {
    const ipc = getIpcBridge();
    if (!ipc) return;
    let unsubs: (() => void)[] = [];
    const onZoom = (_e: unknown, data: { direction: string }) => {
      const map = mapRef.current;
      if (!map) return;
      const z = map.getZoom();
      map.easeTo({ zoom: data.direction === 'in' ? z + 1 : z - 1 });
    };
    const onFit = () => {
      const map = mapRef.current;
      if (!map || !selectedBounds) return;
      map.fitBounds([[selectedBounds.west, selectedBounds.south], [selectedBounds.east, selectedBounds.north]], { padding: 40 });
    };
    unsubs.push(ipc.on('map:zoom', onZoom));
    unsubs.push(ipc.on('map:fit-selection', onFit));
    return () => { unsubs.forEach(u => u()); };
  }, [mapRef, selectedBounds]);

  // Auto-fit to terrain bounds when workspace activates
  useEffect(() => {
    if (activeWorkspace?.id === 'terrain' && selectedBounds && mapRef.current) {
      const map = mapRef.current;
      const timer = setTimeout(() => {
        map.fitBounds(
          [[selectedBounds.west, selectedBounds.south], [selectedBounds.east, selectedBounds.north]],
          { padding: 40, duration: 500 }
        );
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [activeWorkspace?.id, selectedBounds, mapRef]);

  // Listen for map-restore event (from project context restore)
  useEffect(() => {
    const onMapRestore = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const map = mapRef.current;
      if (map && detail?.lat != null && detail?.lon != null) {
        map.easeTo({
          center: [detail.lon, detail.lat],
          zoom: detail.zoom ?? 13,
          duration: 500,
        });
      }
    };
    window.addEventListener('ogstudio:map-restore', onMapRestore);
    return () => window.removeEventListener('ogstudio:map-restore', onMapRestore);
  }, [mapRef]);

  const selectedCount = selectedTiles.size;
  const totalTiles = tileGrid?.tiles.length ?? 0;

  return (
    <div className={`relative w-full h-full ${className ?? ''}`}>
      <div
        ref={mapContainerRef}
        className="absolute inset-0"
        style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}
      />

      {/* ─── Map Toolbar (top-left) ─── */}
      <div className="absolute top-2 left-2 z-20 flex items-center gap-1 bg-surface-panel/95 backdrop-blur-sm rounded-lg border border-edge px-2 py-1 shadow-lg pointer-events-auto">
        {/* Selection visibility */}
        <button
          onClick={toggleSelectionVisible}
          className={`p-1.5 rounded transition-all ${selectionVisible ? 'bg-blue-500/20 text-blue-400 ring-1 ring-blue-500/40' : 'text-fg-secondary hover:bg-white/8 hover:text-fg-primary'}`}
          title={selectionVisible ? 'Hide selection overlay' : 'Show selection overlay'}
        >
          {selectionVisible ? <Eye size={14} /> : <EyeOff size={14} />}
        </button>

        {/* Grid visibility */}
        <button
          onClick={toggleGridVisible}
          className={`p-1.5 rounded transition-all ${gridVisible ? 'bg-blue-500/20 text-blue-400 ring-1 ring-blue-500/40' : 'text-fg-secondary hover:bg-white/8 hover:text-fg-primary'}`}
          title={gridVisible ? 'Hide tile grid' : 'Show tile grid'}
        >
          <Grid3x3 size={14} />
        </button>

        {/* Labels visibility */}
        <button
          onClick={toggleLabelsVisible}
          className={`p-1.5 rounded transition-all ${labelsVisible ? 'bg-blue-500/20 text-blue-400 ring-1 ring-blue-500/40' : 'text-fg-secondary hover:bg-white/8 hover:text-fg-primary'}`}
          title={labelsVisible ? 'Hide labels' : 'Show labels'}
        >
          <Type size={14} />
        </button>

        {/* Zoom lock */}
        <button
          onClick={toggleZoomLock}
          className={`p-1.5 rounded transition-all ${zoomLocked ? 'bg-blue-500/20 text-blue-400 ring-1 ring-blue-500/40' : 'text-fg-secondary hover:bg-white/8 hover:text-fg-primary'}`}
          title={zoomLocked ? `Zoom locked at ${lockedZoom?.toFixed(1)}` : 'Lock zoom level'}
        >
          <Lock size={14} />
        </button>

        <div className="w-px h-5 bg-edge mx-1" />

        {/* Tile size selector (only when bounds selected) */}
        {selectedBounds && tileGrid && (
          <>
            {[1, 2, 4, 8, 16].map((size) => (
              <button
                key={size}
                onClick={() => setTileSizeKm(size)}
                className={`px-1.5 py-1 rounded text-[10px] font-medium transition-colors ${
                  tileSizeKm === size
                    ? 'bg-blue-500/20 text-blue-400 ring-1 ring-blue-500/40'
                    : 'text-fg-secondary hover:bg-white/8 hover:text-fg-primary'
                }`}
                title={`${size}km tiles`}
              >
                {size}km
              </button>
            ))}

            <div className="w-px h-5 bg-edge mx-1" />

            {/* Select all / none */}
            <button
              onClick={selectAllTiles}
              className="p-1.5 rounded text-fg-secondary hover:bg-white/8 hover:text-fg-primary transition-all"
              title="Select all tiles"
            >
              <CheckSquare size={14} />
            </button>
            <button
              onClick={deselectAllTiles}
              className="p-1.5 rounded text-fg-secondary hover:bg-white/8 hover:text-fg-primary transition-all"
              title="Deselect all tiles"
            >
              <Square size={14} />
            </button>

            {/* Tile count */}
            <span className="text-[10px] text-fg-muted ml-1">
              {selectedCount}/{totalTiles}
            </span>
          </>
        )}

        <div className="w-px h-5 bg-edge mx-1" />

        {/* Export / Cancel */}
        {exportProgress ? (
          <button
            onClick={() => executeCommand('export.cancel')}
            className="flex items-center gap-1 px-2 py-1 rounded bg-red-600/20 text-red-400 hover:bg-red-600/30 text-[11px] font-medium transition-colors"
            title="Cancel Export"
          >
            <X size={12} />
            Cancel
          </button>
        ) : (
          <button
            onClick={() => executeCommand('export.run')}
            className="flex items-center gap-1 px-2 py-1 rounded bg-blue-600/20 text-blue-400 hover:bg-blue-600/30 text-[11px] font-medium transition-colors"
            title="Export Terrain (Ctrl+E)"
          >
            <Download size={12} />
            Export
          </button>
        )}
      </div>



      {/* Top Center: Search Bar */}
      <div className="absolute top-2 left-1/2 -translate-x-1/2 z-20 w-72 pointer-events-auto">
        <SearchBar mapRef={mapRef} />
      </div>

      {/* Top Right: Workflow Wizard (terrain workspace, shown once) */}
      {showWizard && <WorkflowWizard onDismiss={dismissWizard} />}
    </div>
  );
}
