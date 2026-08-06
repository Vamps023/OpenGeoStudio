import { useEffect, useRef, useState, type RefObject } from 'react';
import type maplibregl from 'maplibre-gl';
import type { GeoBounds } from '@types/terrain';
import { useTerrainStore } from '@renderer/core/store';
import { toggleTileWithUndo } from '@renderer/core/undoRedoBridge';
import { computeTileGrid } from '../tileGrid';

const SOURCE_ID = 'tilegrid-source';
const FILL_LAYER_ID = 'tilegrid-fill';
const OUTLINE_LAYER_ID = 'tilegrid-outline';
const LABEL_LAYER_ID = 'tilegrid-label';

export interface UseTileGridOverlayResult {
  gridVisible: boolean;
  toggleGridVisible: () => void;
}

/**
 * Computes and renders the tile grid overlay for the current selection,
 * wires up click-to-toggle-tile interaction, and manages grid visibility.
 * Reads/writes tile grid + selection state directly from the terrain store.
 */
export function useTileGridOverlay(
  mapRef: RefObject<maplibregl.Map | null>,
  selectedBounds: GeoBounds | null,
  tileSizeKm: number,
): UseTileGridOverlayResult {
  const tileGrid = useTerrainStore((s) => s.tileGrid);
  const setTileGrid = useTerrainStore((s) => s.setTileGrid);
  const selectedTiles = useTerrainStore((s) => s.selectedTiles);
  const toggleTileSelection = useTerrainStore((s) => s.toggleTileSelection);

  const selectedTilesRef = useRef(selectedTiles);
  useEffect(() => {
    selectedTilesRef.current = selectedTiles;
  }, [selectedTiles]);

  const [gridVisible, setGridVisible] = useState(true);

  // Compute tile grid when bounds or tile size changes
  useEffect(() => {
    if (!selectedBounds) {
      setTileGrid(null);
      // Clear selected tiles when bounds are cleared
      useTerrainStore.getState().deselectAllTiles();
      return;
    }
    const grid = computeTileGrid(selectedBounds, tileSizeKm);
    setTileGrid(grid);
    // Always auto-select all tiles when the grid is recomputed (bounds or tile size changed).
    // Old tile selections are invalid for the new grid.
    useTerrainStore.getState().selectAllTiles();
  }, [selectedBounds, tileSizeKm, setTileGrid]);

  // Update GeoJSON data whenever tileGrid or selectedTiles changes
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !tileGrid) return;

    const features = tileGrid.tiles.map((tile) => {
      const isSelected = selectedTilesRef.current.has(`${tile.row},${tile.col}`);
      return {
        type: 'Feature' as const,
        geometry: {
          type: 'Polygon' as const,
          coordinates: [
            [
              [tile.bounds.west, tile.bounds.south],
              [tile.bounds.east, tile.bounds.south],
              [tile.bounds.east, tile.bounds.north],
              [tile.bounds.west, tile.bounds.north],
              [tile.bounds.west, tile.bounds.south],
            ],
          ],
        },
        properties: {
          row: tile.row,
          col: tile.col,
          selected: isSelected ? 1 : 0,
          label: `${tile.row},${tile.col}`,
        },
      };
    });

    const geojson = {
      type: 'FeatureCollection' as const,
      features,
    };

    const applyTileGrid = () => {
      if (map.getSource(SOURCE_ID)) {
        (map.getSource(SOURCE_ID) as maplibregl.GeoJSONSource).setData(geojson);
      } else {
        map.addSource(SOURCE_ID, {
          type: 'geojson',
          data: geojson,
        });

        // Fill layer - selected tiles are cyan, unselected are transparent
        map.addLayer({
          id: FILL_LAYER_ID,
          type: 'fill',
          source: SOURCE_ID,
          paint: {
            'fill-color': '#06b6d4',
            'fill-opacity': ['case', ['==', ['get', 'selected'], 1], 0.25, 0.05],
          },
        });

        // Outline layer
        map.addLayer({
          id: OUTLINE_LAYER_ID,
          type: 'line',
          source: SOURCE_ID,
          paint: {
            'line-color': ['case', ['==', ['get', 'selected'], 1], '#06b6d4', '#666666'],
            'line-width': ['case', ['==', ['get', 'selected'], 1], 2, 1],
            'line-dasharray': [4, 2],
          },
        });

        // Label layer
        map.addLayer({
          id: LABEL_LAYER_ID,
          type: 'symbol',
          source: SOURCE_ID,
          layout: {
            'text-field': ['get', 'label'],
            'text-size': 12,
            'text-anchor': 'center',
          },
          paint: {
            'text-color': ['case', ['==', ['get', 'selected'], 1], '#06b6d4', '#888888'],
            'text-halo-color': '#000000',
            'text-halo-width': 1,
          },
        });
      }
    };

    // MapLibre requires the style to be fully loaded before adding sources/layers.
    if (map.isStyleLoaded()) {
      applyTileGrid();
    } else {
      map.once('style.load', applyTileGrid);
    }
  }, [mapRef, tileGrid, selectedTiles]);

  // One-time handler setup for tile grid interaction ΓÇö only depends on tileGrid
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !tileGrid) return;

    const handleClick = (e: maplibregl.MapLayerMouseEvent) => {
      if (!e.features || e.features.length === 0) return;
      const feature = e.features[0];
      const row = feature.properties?.row as number;
      const col = feature.properties?.col as number;
      toggleTileWithUndo(row, col);
    };

    const handleMouseEnter = () => {
      map.getCanvas().style.cursor = 'pointer';
    };
    const handleMouseLeave = () => {
      map.getCanvas().style.cursor = '';
    };

    map.on('click', FILL_LAYER_ID, handleClick);
    map.on('mouseenter', FILL_LAYER_ID, handleMouseEnter);
    map.on('mouseleave', FILL_LAYER_ID, handleMouseLeave);

    return () => {
      map.off('click', FILL_LAYER_ID, handleClick);
      map.off('mouseenter', FILL_LAYER_ID, handleMouseEnter);
      map.off('mouseleave', FILL_LAYER_ID, handleMouseLeave);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps -- toggleTileWithUndo is a stable store action
  }, [mapRef, tileGrid, toggleTileSelection, toggleTileWithUndo]);

  // Cleanup tile grid when bounds cleared
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    if (!tileGrid && !selectedBounds) {
      if (map.getLayer(FILL_LAYER_ID)) map.removeLayer(FILL_LAYER_ID);
      if (map.getLayer(OUTLINE_LAYER_ID)) map.removeLayer(OUTLINE_LAYER_ID);
      if (map.getLayer(LABEL_LAYER_ID)) map.removeLayer(LABEL_LAYER_ID);
      if (map.getSource(SOURCE_ID)) map.removeSource(SOURCE_ID);
    }
  }, [mapRef, tileGrid, selectedBounds]);

  // Toggle grid visibility
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    const visibility = gridVisible ? 'visible' : 'none';
    const applyVisibility = () => {
      if (map.getLayer(FILL_LAYER_ID)) map.setLayoutProperty(FILL_LAYER_ID, 'visibility', visibility);
      if (map.getLayer(OUTLINE_LAYER_ID)) map.setLayoutProperty(OUTLINE_LAYER_ID, 'visibility', visibility);
      if (map.getLayer(LABEL_LAYER_ID)) map.setLayoutProperty(LABEL_LAYER_ID, 'visibility', visibility);
    };
    if (map.isStyleLoaded()) {
      applyVisibility();
    } else {
      map.once('style.load', applyVisibility);
    }
  }, [mapRef, gridVisible]);

  return { gridVisible, toggleGridVisible: () => setGridVisible((v) => !v) };
}
