import { useEffect, useState, type RefObject } from 'react';
import type maplibregl from 'maplibre-gl';

export interface UseLabelsVisibilityResult {
  labelsVisible: boolean;
  toggleLabelsVisible: () => void;
}

/**
 * Manages visibility of the place-label symbol layers added by
 * `useMapInstance`. Layer ids are passed in rather than hardcoded to keep
 * this hook decoupled from how the labels were created.
 */
export function useLabelsVisibility(
  mapRef: RefObject<maplibregl.Map | null>,
  labelLayerIds: readonly string[],
): UseLabelsVisibilityResult {
  const [labelsVisible, setLabelsVisible] = useState(true);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    const visibility = labelsVisible ? 'visible' : 'none';
    for (const layerId of labelLayerIds) {
      try {
        if (map.getLayer(layerId)) {
          map.setLayoutProperty(layerId, 'visibility', visibility);
        }
      } catch {
        // Graceful degradation: if vector tile source is unavailable, map continues without labels
      }
    }
  }, [mapRef, labelLayerIds, labelsVisible]);

  return { labelsVisible, toggleLabelsVisible: () => setLabelsVisible((v) => !v) };
}
