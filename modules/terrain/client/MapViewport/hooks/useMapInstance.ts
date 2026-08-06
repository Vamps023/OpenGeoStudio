import { useEffect, useRef, useState, useCallback, type RefObject } from 'react';
import maplibregl from 'maplibre-gl';
import type { GeoBounds } from '@types/terrain';

const PLACE_LABEL_LAYER_IDS = [
  'place-labels-country',
  'place-labels-city',
  'place-labels-town',
  'place-labels-village',
  'place-labels-poi',
] as const;

function addPlaceLabelLayers(map: maplibregl.Map): void {
  try {
    map.addSource('place-labels-source', {
      type: 'vector',
      tiles: ['https://tiles.openfreemap.org/planet/{z}/{x}/{y}.pbf'],
      maxzoom: 14,
      attribution: '┬⌐ OpenMapTiles ┬⌐ OpenStreetMap contributors',
    });

    map.addLayer({
      id: 'place-labels-country',
      type: 'symbol',
      source: 'place-labels-source',
      'source-layer': 'place',
      filter: ['==', ['get', 'class'], 'country'],
      minzoom: 2,
      maxzoom: 5,
      layout: {
        'text-field': ['get', 'name'],
        'text-size': ['interpolate', ['linear'], ['zoom'], 2, 12, 5, 16],
        'text-font': ['Noto Sans Regular'],
        'text-transform': 'uppercase',
        'text-letter-spacing': 0.1,
        'text-max-width': 8,
        'text-allow-overlap': false,
        'icon-allow-overlap': false,
      },
      paint: {
        'text-color': '#ffffff',
        'text-halo-color': 'rgba(0, 0, 0, 0.8)',
        'text-halo-width': 1.5,
      },
    });

    map.addLayer({
      id: 'place-labels-city',
      type: 'symbol',
      source: 'place-labels-source',
      'source-layer': 'place',
      filter: ['==', ['get', 'class'], 'city'],
      minzoom: 4,
      maxzoom: 10,
      layout: {
        'text-field': ['get', 'name'],
        'text-size': ['interpolate', ['linear'], ['zoom'], 4, 11, 10, 16],
        'text-font': ['Noto Sans Regular'],
        'text-max-width': 8,
        'text-allow-overlap': false,
        'icon-allow-overlap': false,
      },
      paint: {
        'text-color': '#ffffff',
        'text-halo-color': 'rgba(0, 0, 0, 0.8)',
        'text-halo-width': 1.5,
      },
    });

    map.addLayer({
      id: 'place-labels-town',
      type: 'symbol',
      source: 'place-labels-source',
      'source-layer': 'place',
      filter: ['==', ['get', 'class'], 'town'],
      minzoom: 8,
      maxzoom: 13,
      layout: {
        'text-field': ['get', 'name'],
        'text-size': ['interpolate', ['linear'], ['zoom'], 8, 10, 13, 14],
        'text-font': ['Noto Sans Regular'],
        'text-max-width': 7,
        'text-allow-overlap': false,
        'icon-allow-overlap': false,
      },
      paint: {
        'text-color': '#f0f0f0',
        'text-halo-color': 'rgba(0, 0, 0, 0.75)',
        'text-halo-width': 1.2,
      },
    });

    map.addLayer({
      id: 'place-labels-village',
      type: 'symbol',
      source: 'place-labels-source',
      'source-layer': 'place',
      filter: ['any', ['==', ['get', 'class'], 'village'], ['==', ['get', 'class'], 'suburb']],
      minzoom: 11,
      maxzoom: 15,
      layout: {
        'text-field': ['get', 'name'],
        'text-size': ['interpolate', ['linear'], ['zoom'], 11, 10, 15, 13],
        'text-font': ['Noto Sans Regular'],
        'text-max-width': 7,
        'text-allow-overlap': false,
        'icon-allow-overlap': false,
      },
      paint: {
        'text-color': '#e8e8e8',
        'text-halo-color': 'rgba(0, 0, 0, 0.7)',
        'text-halo-width': 1,
      },
    });

    map.addLayer({
      id: 'place-labels-poi',
      type: 'symbol',
      source: 'place-labels-source',
      'source-layer': 'poi',
      minzoom: 13,
      maxzoom: 17,
      layout: {
        'text-field': ['get', 'name'],
        'text-size': ['interpolate', ['linear'], ['zoom'], 13, 9, 17, 12],
        'text-font': ['Noto Sans Regular'],
        'text-max-width': 6,
        'text-allow-overlap': false,
        'icon-allow-overlap': false,
        'text-offset': [0, 0.5],
      },
      paint: {
        'text-color': '#e0e0e0',
        'text-halo-color': 'rgba(0, 0, 0, 0.7)',
        'text-halo-width': 1,
      },
    });
  } catch {
    // Graceful degradation: map continues without labels
  }
}

export interface UseMapInstanceResult {
  mapRef: RefObject<maplibregl.Map | null>;
  /** Bounding box currently being dragged (shift+drag), null when not dragging. */
  liveBounds: GeoBounds | null;
  zoomLocked: boolean;
  /** The zoom level locked at, when `zoomLocked` is true. */
  lockedZoom: number | null;
  toggleZoomLock: () => void;
  /** Layer ids for the place-labels source, exposed for visibility toggling. */
  placeLabelLayerIds: readonly string[];
}

/**
 * Owns the MapLibre map lifecycle: creation, base style, place-label layers,
 * shift-drag bounding-box selection, zoom-lock, and resize handling.
 */
export function useMapInstance(
  containerRef: RefObject<HTMLDivElement | null>,
  onBoundsSelected: (bounds: GeoBounds) => void,
): UseMapInstanceResult {
  const mapRef = useRef<maplibregl.Map | null>(null);
  const isDraggingRef = useRef(false);
  const dragStartRef = useRef<{ x: number; y: number } | null>(null);
  const liveBoundsRef = useRef<GeoBounds | null>(null);
  const [liveBounds, setLiveBounds] = useState<GeoBounds | null>(null);

  const [zoomLocked, setZoomLocked] = useState(false);
  const [lockedZoom, setLockedZoom] = useState<number | null>(null);
  const zoomLockedRef = useRef(false);
  const lockedZoomRef = useRef<number | null>(null);

  const onBoundsSelectedRef = useRef(onBoundsSelected);
  onBoundsSelectedRef.current = onBoundsSelected;

  const toggleZoomLock = useCallback(() => {
    const map = mapRef.current;
    if (!map) return;
    if (!zoomLockedRef.current) {
      const zoom = map.getZoom();
      lockedZoomRef.current = zoom;
      zoomLockedRef.current = true;
      setZoomLocked(true);
      setLockedZoom(zoom);
    } else {
      lockedZoomRef.current = null;
      zoomLockedRef.current = false;
      setZoomLocked(false);
      setLockedZoom(null);
    }
  }, []);

  useEffect(() => {
    if (!containerRef.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: containerRef.current,
      style: {
        version: 8,
        sources: {
          'esri-imagery': {
            type: 'raster',
            tiles: [
              'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
            ],
            tileSize: 256,
            maxzoom: 19,
            attribution: 'Esri',
          },
        },
        layers: [
          {
            id: 'esri-imagery',
            type: 'raster',
            source: 'esri-imagery',
            minzoom: 0,
            maxzoom: 22,
          },
        ],
      },
      center: [0, 20],
      zoom: 2,
      maxZoom: 22,
      attributionControl: false,
    });

    map.boxZoom.disable();
    map.addControl(new maplibregl.NavigationControl(), 'top-right');
    map.addControl(new maplibregl.ScaleControl(), 'bottom-left');

    map.on('load', () => {
      addPlaceLabelLayers(map);
    });
    map.on('style.load', () => {
      // Style loaded — no action needed
    });

    // Handle vector tile source errors gracefully
    map.on('error', (e) => {
      // Suppress errors from the place-labels source to avoid noisy console
      const errMsg = e.error?.message || '';
      if (errMsg.includes('place-labels') || errMsg.includes('openfreemap')) {
        return;
      }
    });

    // Zoom lock handler ΓÇö uses ref to always get current state
    const onZoom = () => {
      if (zoomLockedRef.current && lockedZoomRef.current !== null) {
        const currentZoom = map.getZoom();
        if (Math.abs(currentZoom - lockedZoomRef.current) > 0.01) {
          map.setZoom(lockedZoomRef.current);
        }
      }
    };
    map.on('zoom', onZoom);

    const canvas = map.getCanvas();

    const onMouseDown = (e: MouseEvent) => {
      if (!e.shiftKey) return;
      e.preventDefault();
      e.stopPropagation();
      map.dragPan.disable();
      isDraggingRef.current = true;
      dragStartRef.current = { x: e.offsetX, y: e.offsetY };
      canvas.style.cursor = 'crosshair';
    };

    const onMouseMove = (e: MouseEvent) => {
      if (!isDraggingRef.current || !dragStartRef.current) return;
      e.preventDefault();
      const start = dragStartRef.current;

      // Constrain to 1:1 square ratio using pixel coordinates for true screen square
      const dX = e.offsetX - start.x;
      const dY = e.offsetY - start.y;
      const size = Math.max(Math.abs(dX), Math.abs(dY));

      // Determine direction of drag
      const signX = dX >= 0 ? 1 : -1;
      const signY = dY >= 0 ? 1 : -1;

      const endX = start.x + signX * size;
      const endY = start.y + signY * size;

      // Convert pixel corners to lat/lng bounds
      const startPoint = new maplibregl.Point(start.x, start.y);
      const endPoint = new maplibregl.Point(endX, endY);
      const startLngLat = map.unproject(startPoint);
      const endLngLat = map.unproject(endPoint);

      const bounds: GeoBounds = {
        west: Math.min(startLngLat.lng, endLngLat.lng),
        south: Math.min(startLngLat.lat, endLngLat.lat),
        east: Math.max(startLngLat.lng, endLngLat.lng),
        north: Math.max(startLngLat.lat, endLngLat.lat),
      };
      liveBoundsRef.current = bounds;
      setLiveBounds(bounds);
    };

    const onMouseUp = (e: MouseEvent) => {
      if (!isDraggingRef.current) return;
      e.preventDefault();
      isDraggingRef.current = false;
      canvas.style.cursor = '';
      map.dragPan.enable();
      if (liveBoundsRef.current) {
        onBoundsSelectedRef.current(liveBoundsRef.current);
      }
      dragStartRef.current = null;
      liveBoundsRef.current = null;
      setLiveBounds(null);
    };

    canvas.addEventListener('mousedown', onMouseDown);
    canvas.addEventListener('mousemove', onMouseMove);
    canvas.addEventListener('mouseup', onMouseUp);
    window.addEventListener('mouseup', onMouseUp);

    mapRef.current = map;

    // Emit cursor coordinates + zoom for the status bar + viewport sync
    const onMapMove = () => {
      const center = map.getCenter();
      const zoom = map.getZoom();
      window.dispatchEvent(new CustomEvent('ogstudio:map-cursor', {
        detail: { lat: center.lat, lon: center.lng, zoom },
      }));
      // Sync viewport to ProjectContext (debounced via event)
      window.dispatchEvent(new CustomEvent('ogstudio:viewport-changed', {
        detail: { mapLat: center.lat, mapLon: center.lng, mapZoom: zoom },
      }));
    };
    const onMapZoom = () => {
      const center = map.getCenter();
      const zoom = map.getZoom();
      window.dispatchEvent(new CustomEvent('ogstudio:map-cursor', {
        detail: { lat: center.lat, lon: center.lng, zoom },
      }));
      window.dispatchEvent(new CustomEvent('ogstudio:viewport-changed', {
        detail: { mapLat: center.lat, mapLon: center.lng, mapZoom: zoom },
      }));
    };
    map.on('move', onMapMove);
    map.on('zoom', onMapZoom);

    // Guard against the container's final layout size settling after the
    // map canvas was created (e.g. flex layout still resolving on mount).
    // MapLibre's own resize tracking can miss this, leaving the canvas
    // stuck at its initial (too-small) size.
    const resizeObserver = new ResizeObserver(() => {
      map.resize();
    });
    resizeObserver.observe(containerRef.current);

    return () => {
      resizeObserver.disconnect();
      canvas.removeEventListener('mousedown', onMouseDown);
      canvas.removeEventListener('mousemove', onMouseMove);
      canvas.removeEventListener('mouseup', onMouseUp);
      window.removeEventListener('mouseup', onMouseUp);
      map.off('move', onMapMove);
      map.off('zoom', onMapZoom);
      map.remove();
      mapRef.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  return { mapRef, liveBounds, zoomLocked, lockedZoom, toggleZoomLock, placeLabelLayerIds: PLACE_LABEL_LAYER_IDS };
}
