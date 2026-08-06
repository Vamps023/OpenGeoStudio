import { useEffect, useState, type RefObject } from 'react';
import type maplibregl from 'maplibre-gl';
import type { GeoBounds } from '@types/terrain';

const SOURCE_ID = 'selection-source';
const FILL_LAYER_ID = 'selection-fill';
const OUTLINE_LAYER_ID = 'selection-outline';

interface BoundsPolygonFeature {
  type: 'Feature';
  geometry: { type: 'Polygon'; coordinates: number[][][] };
  properties: Record<string, never>;
}

export interface UseSelectionOverlayResult {
  selectionVisible: boolean;
  toggleSelectionVisible: () => void;
}

/**
 * Renders the confirmed selection rectangle (and the live rectangle while
 * shift-dragging) as a MapLibre layer, and manages its visibility toggle.
 */
export function useSelectionOverlay(
  mapRef: RefObject<maplibregl.Map | null>,
  selectedBounds: GeoBounds | null,
  liveBounds: GeoBounds | null,
): UseSelectionOverlayResult {
  const [selectionVisible, setSelectionVisible] = useState(true);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    const bounds = selectedBounds || liveBounds;

    const applyOverlay = () => {
      if (bounds) {
        const geojson: BoundsPolygonFeature = {
          type: 'Feature',
          geometry: {
            type: 'Polygon',
            coordinates: [
              [
                [bounds.west, bounds.south],
                [bounds.east, bounds.south],
                [bounds.east, bounds.north],
                [bounds.west, bounds.north],
                [bounds.west, bounds.south],
              ],
            ],
          },
          properties: {},
        };

        if (map.getSource(SOURCE_ID)) {
          (map.getSource(SOURCE_ID) as maplibregl.GeoJSONSource).setData(geojson);
        } else {
          map.addSource(SOURCE_ID, {
            type: 'geojson',
            data: geojson,
          });
          map.addLayer({
            id: FILL_LAYER_ID,
            type: 'fill',
            source: SOURCE_ID,
            paint: {
              'fill-color': '#06b6d4',
              'fill-opacity': liveBounds ? 0.15 : 0.3,
            },
          });
          map.addLayer({
            id: OUTLINE_LAYER_ID,
            type: 'line',
            source: SOURCE_ID,
            paint: {
              'line-color': '#06b6d4',
              'line-width': liveBounds ? 2 : 3,
            },
          });
        }
      } else {
        if (map.getLayer(FILL_LAYER_ID)) map.removeLayer(FILL_LAYER_ID);
        if (map.getLayer(OUTLINE_LAYER_ID)) map.removeLayer(OUTLINE_LAYER_ID);
        if (map.getSource(SOURCE_ID)) map.removeSource(SOURCE_ID);
      }
    };

    // MapLibre requires the style to be fully loaded before adding sources/layers.
    // If the style isn't ready yet, wait for the 'style.load' event.
    if (map.isStyleLoaded()) {
      applyOverlay();
    } else {
      map.once('style.load', applyOverlay);
    }
  }, [mapRef, selectedBounds, liveBounds]);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    const applyVisibility = () => {
      if (map.getLayer(FILL_LAYER_ID)) {
        map.setLayoutProperty(FILL_LAYER_ID, 'visibility', selectionVisible ? 'visible' : 'none');
      }
      if (map.getLayer(OUTLINE_LAYER_ID)) {
        map.setLayoutProperty(OUTLINE_LAYER_ID, 'visibility', selectionVisible ? 'visible' : 'none');
      }
    };

    if (map.isStyleLoaded()) {
      applyVisibility();
    } else {
      map.once('style.load', applyVisibility);
    }
  }, [mapRef, selectionVisible]);

  return {
    selectionVisible,
    toggleSelectionVisible: () => setSelectionVisible((v) => !v),
  };
}
