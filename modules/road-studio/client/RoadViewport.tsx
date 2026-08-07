/**
 * RoadViewport — Unified 2D/3D road editing viewport.
 *
 * Top mode (default):
 *   - Full world map (MapLibre satellite imagery)
 *   - Roads rendered as styled MapLibre layers:
 *     - Road surface (asphalt color, proper width)
 *     - Sidewalks (lighter, wider outline)
 *     - Curbs (dark edge)
 *     - Lane markings (center line + lane dividers)
 *   - Control points as interactive circles
 *   - Draw roads by clicking on the map
 *
 * 3D mode:
 *   - Babylon.js 3D perspective viewport
 *   - Roads as 3D ribbons with SCANeR textures
 *   - Sidewalks, curbs, lane markings in 3D
 *   - Middle-mouse rotate, right-drag pan, scroll zoom
 */

import React, { useEffect, useRef } from 'react';
import 'maplibre-gl/dist/maplibre-gl.css';
import maplibregl from 'maplibre-gl';
import { useRoadStudioStore } from './store/roadStudioStore';
import {
  type Road,
  type ControlPoint,
  type Intersection,
  geoToLocal,
  localToGeo,
  sampleRoad,
  detectIntersections,
  computeIntersectionPolygon,
  convexHull,
  distanceMeters,
} from '../shared/types';
import {
  Engine, Scene, ArcRotateCamera, Vector3, HemisphericLight, DirectionalLight,
  MeshBuilder, StandardMaterial, Color3, Color4, Mesh, LinesMesh, Texture,
  VertexData, VertexBuffer,
  PointerEventTypes, PointerInfo,
} from '@babylonjs/core';

interface RoadViewportProps {
  className?: string;
}

export const RoadViewport: React.FC<RoadViewportProps> = ({ className }) => {
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const engineRef = useRef<Engine | null>(null);
  const sceneRef = useRef<Scene | null>(null);
  const cameraRef = useRef<ArcRotateCamera | null>(null);
  const groundRef = useRef<Mesh | null>(null);
  const roadMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const pointMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const handleLinesRef = useRef<Map<string, LinesMesh>>(new Map());
  const textureCacheRef = useRef<Map<string, Texture>>(new Map());

  // Store refs
  const toolRef = useRef<string>('select');
  const selectionRef = useRef<{ roadId: string | null; pointIndices: number[]; handle: 'in' | 'out' | null }>({
    roadId: null, pointIndices: [], handle: null,
  });
  const roadsRef = useRef<Road[]>([]);
  const intersectionsRef = useRef<Intersection[]>([]);
  const refLatRef = useRef(18.52);
  const refLonRef = useRef(73.85);
  const gridSizeRef = useRef(10);
  const snapEnabledRef = useRef(true);
  const drawingRoadIdRef = useRef<string | null>(null);

  const dragStateRef = useRef<{
    mode: 'none' | 'pan' | 'rotate' | 'move-point' | 'move-handle';
    roadId: string | null;
    pointIndex: number;
    handle: 'in' | 'out' | null;
  }>({ mode: 'none', roadId: null, pointIndex: -1, handle: null });

  const store = useRoadStudioStore();
  const viewMode = store.viewMode;

  useEffect(() => { toolRef.current = store.tool; }, [store.tool]);
  useEffect(() => { selectionRef.current = store.selection; }, [store.selection]);
  useEffect(() => {
    roadsRef.current = store.roads;
    // Recompute intersections whenever roads change
    intersectionsRef.current = detectIntersections(store.roads);
    if (store.recomputeIntersections) store.recomputeIntersections();
    updateAllViews();
  }, [store.roads]);
  useEffect(() => { refLatRef.current = store.refLat; refLonRef.current = store.refLon; updateAllViews(); }, [store.refLat, store.refLon]);
  useEffect(() => { gridSizeRef.current = store.gridSize; }, [store.gridSize]);
  useEffect(() => { snapEnabledRef.current = store.snapEnabled; }, [store.snapEnabled]);
  useEffect(() => { drawingRoadIdRef.current = store.drawingRoadId; }, [store.drawingRoadId]);

  // ═══════════════════════════════════════════════════════════
  // TOP VIEW — MapLibre 2D world map with styled roads
  // ═══════════════════════════════════════════════════════════

  useEffect(() => {
    if (viewMode !== 'top') return;
    if (!mapContainerRef.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: mapContainerRef.current,
      style: {
        version: 8,
        sources: {
          'esri-imagery': {
            type: 'raster',
            tiles: ['https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}'],
            tileSize: 256,
            maxzoom: 19,
            attribution: 'Esri',
          },
        },
        layers: [{
          id: 'esri-imagery',
          type: 'raster',
          source: 'esri-imagery',
          minzoom: 0,
          maxzoom: 22,
        }],
      },
      center: [refLonRef.current, refLatRef.current],
      zoom: 14,
      maxZoom: 22,
      attributionControl: false,
    });

    map.addControl(new maplibregl.NavigationControl(), 'top-right');
    map.addControl(new maplibregl.ScaleControl(), 'bottom-left');

    map.on('load', () => {
      updateMapRoads(map);
    });

    map.on('click', (e) => {
      const tool = toolRef.current;
      if (tool !== 'line' && tool !== 'pen') return;

      let finalLat = e.lngLat.lat;
      let finalLon = e.lngLat.lng;

      // Snap to existing endpoints
      if (snapEnabledRef.current) {
        const snapDist = 0.00005;
        for (const road of roadsRef.current) {
          for (const p of road.points) {
            if (Math.abs(p.lat - finalLat) < snapDist && Math.abs(p.lon - finalLon) < snapDist) {
              finalLat = p.lat;
              finalLon = p.lon;
            }
          }
        }
      }

      const drawingId = drawingRoadIdRef.current;
      if (!drawingId) {
        store.startNewRoad(finalLat, finalLon);
      } else {
        store.pushHistory('Add control point');
        store.addControlPoint(drawingId, finalLat, finalLon);
      }
    });

    mapRef.current = map;

    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, [viewMode]);

  useEffect(() => {
    if (viewMode === 'top' && mapRef.current) {
      updateMapRoads(mapRef.current);
    }
  }, [store.roads, store.selection, viewMode]);

  // ─── Render roads as styled MapLibre layers ────────────────
  function updateMapRoads(map: maplibregl.Map) {
    if (!map.loaded()) return;

    // Remove old layers/sources
    const style = map.getStyle();
    if (style?.layers) {
      for (const layer of style.layers) {
        if (layer.id.startsWith('rd-') || layer.id.startsWith('cp-') || layer.id.startsWith('ix-') || layer.id.startsWith('cw-')) {
          map.removeLayer(layer.id);
        }
      }
    }
    if (style?.sources) {
      for (const src of Object.keys(style.sources)) {
        if (src.startsWith('rd-') || src.startsWith('cp-') || src.startsWith('ix-') || src.startsWith('cw-')) {
          map.removeSource(src);
        }
      }
    }

    const roads = roadsRef.current;
    const selection = selectionRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;

    // ─── Helper: build a road polygon (left edge + reversed right edge) ───
    // Offsets the centerline by ±halfWidth in meters, converts to lat/lon
    function buildRoadPolygon(
      samples: Array<{ x: number; y: number; z: number }>,
      halfWidthMeters: number,
    ): [number, number][][] {
      if (samples.length < 2) return [];
      const leftEdge: [number, number][] = [];
      const rightEdge: [number, number][] = [];

      for (let i = 0; i < samples.length; i++) {
        const s = samples[i];
        // Compute tangent direction
        let tx: number, ty: number;
        if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
        else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        // Normal = perpendicular to tangent (rotate 90°)
        const nx = -ty / len;
        const ny = tx / len;

        // Offset centerline point by ±halfWidth in local meters
        const lGeo = localToGeo(s.x + nx * halfWidthMeters, s.y + ny * halfWidthMeters, refLat, refLon);
        const rGeo = localToGeo(s.x - nx * halfWidthMeters, s.y - ny * halfWidthMeters, refLat, refLon);
        leftEdge.push([lGeo.lon, lGeo.lat]);
        rightEdge.push([rGeo.lon, rGeo.lat]);
      }

      // Polygon = left edge forward + right edge backward
      const ring: [number, number][] = [...leftEdge, ...rightEdge.reverse()];
      // Close the ring
      if (ring.length > 0) ring.push(ring[0]);
      return [ring];
    }

    // ─── Helper: build offset line for lane markings ──────────────
    function buildOffsetLine(
      samples: Array<{ x: number; y: number; z: number }>,
      offsetMeters: number,
    ): [number, number][] {
      const result: [number, number][] = [];
      for (let i = 0; i < samples.length; i++) {
        const s = samples[i];
        let tx: number, ty: number;
        if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
        else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;
        const geo = localToGeo(s.x + nx * offsetMeters, s.y + ny * offsetMeters, refLat, refLon);
        result.push([geo.lon, geo.lat]);
      }
      return result;
    }

    for (const road of roads) {
      if (road.points.length < 2) continue;

      // Sample road path with bezier curves
      const samples = sampleRoad(road, refLat, refLon, 24);
      if (samples.length < 2) continue;

      const profile = road.profile;
      const hasSidewalk = profile?.hasSidewalk ?? false;
      const hasCurb = profile?.hasCurb ?? false;
      const sidewalkWidth = 2.0; // meters per side
      const curbWidth = 0.3; // meters per side
      const halfWidth = road.width / 2;

      // ─── Layer 1: Sidewalk polygon (widest) ────────────────────
      if (hasSidewalk) {
        const swHalf = halfWidth + curbWidth + sidewalkWidth;
        const swPolygon = buildRoadPolygon(samples, swHalf);
        const swSrcId = `rd-sw-src-${road.id}`;
        map.addSource(swSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: swPolygon } },
        });
        map.addLayer({
          id: `rd-sw-${road.id}`,
          type: 'fill',
          source: swSrcId,
          paint: {
            'fill-color': '#9e9e9e',
            'fill-opacity': 0.5,
          },
        });
      }

      // ─── Layer 2: Curb polygon (road + curb width) ─────────────
      if (hasCurb) {
        const curbHalf = halfWidth + curbWidth;
        const curbPolygon = buildRoadPolygon(samples, curbHalf);
        const curbSrcId = `rd-curb-src-${road.id}`;
        map.addSource(curbSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: curbPolygon } },
        });
        map.addLayer({
          id: `rd-curb-${road.id}`,
          type: 'fill',
          source: curbSrcId,
          paint: {
            'fill-color': '#616161',
            'fill-opacity': 0.7,
          },
        });
      }

      // ─── Layer 3: Road surface polygon (asphalt) ───────────────
      const roadPolygon = buildRoadPolygon(samples, halfWidth);
      const surfaceSrcId = `rd-surface-src-${road.id}`;
      map.addSource(surfaceSrcId, {
        type: 'geojson',
        data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: roadPolygon } },
      });
      map.addLayer({
        id: `rd-surface-${road.id}`,
        type: 'fill',
        source: surfaceSrcId,
        paint: {
          'fill-color': '#3a3a3a',
          'fill-opacity': 0.9,
        },
      });

      // ─── Layer 4: Road outline (thin line on edges) ────────────
      const outlineSrcId = `rd-outline-src-${road.id}`;
      const outlineCoords = buildRoadPolygon(samples, halfWidth)[0];
      map.addSource(outlineSrcId, {
        type: 'geojson',
        data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: outlineCoords } },
      });
      map.addLayer({
        id: `rd-outline-${road.id}`,
        type: 'line',
        source: outlineSrcId,
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#2a2a2a',
          'line-width': 1,
          'line-opacity': 0.8,
        },
      });

      // ─── Layer 5: Lane divider lines (dashed white) ────────────
      if (road.laneCount > 1) {
        const laneSpacing = road.width / road.laneCount;
        for (let lane = 1; lane < road.laneCount; lane++) {
          const offset = -halfWidth + laneSpacing * lane;
          const dividerCoords = buildOffsetLine(samples, offset);
          const divSrcId = `rd-div-src-${road.id}-${lane}`;
          map.addSource(divSrcId, {
            type: 'geojson',
            data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: dividerCoords } },
          });
          map.addLayer({
            id: `rd-div-${road.id}-${lane}`,
            type: 'line',
            source: divSrcId,
            layout: { 'line-cap': 'round', 'line-join': 'round' },
            paint: {
              'line-color': '#ffffff',
              'line-width': 1.5,
              'line-dasharray': [3, 3],
              'line-opacity': 0.7,
            },
          });
        }
      }

      // ─── Layer 6: Center line (dashed yellow) ──────────────────
      const centerCoords = samples.map((s) => {
        const geo = localToGeo(s.x, s.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });
      const centerSrcId = `rd-center-src-${road.id}`;
      map.addSource(centerSrcId, {
        type: 'geojson',
        data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: centerCoords } },
      });
      map.addLayer({
        id: `rd-center-${road.id}`,
        type: 'line',
        source: centerSrcId,
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#ffeb3b',
          'line-width': 1.5,
          'line-dasharray': [2, 2],
          'line-opacity': 0.9,
        },
      });

      // ─── Layer 7: Selected road highlight ──────────────────────
      if (selection.roadId === road.id) {
        const selHalf = halfWidth + 1.5;
        const selPolygon = buildRoadPolygon(samples, selHalf);
        const selSrcId = `rd-sel-src-${road.id}`;
        map.addSource(selSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: selPolygon } },
        });
        map.addLayer({
          id: `rd-sel-${road.id}`,
          type: 'fill',
          source: selSrcId,
          paint: {
            'fill-color': '#4ecca3',
            'fill-opacity': 0.15,
          },
        });
        // Green outline
        map.addLayer({
          id: `rd-sel-outline-${road.id}`,
          type: 'line',
          source: selSrcId,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: {
            'line-color': '#4ecca3',
            'line-width': 2,
            'line-opacity': 0.8,
          },
        });
      }
    }

    // ─── Intersection polygons (asphalt fill + crosswalk markings) ───
    const intersections = intersectionsRef.current;
    for (const ix of intersections) {
      if (ix.connections.length < 2) continue;

      // Compute intersection polygon in local meters
      const polyLocal = computeIntersectionPolygon(ix, roads, refLat, refLon);
      if (polyLocal.length < 3) continue;

      // Convert to lat/lon for GeoJSON
      const polyCoords: [number, number][] = polyLocal.map((p) => {
        const geo = localToGeo(p.x, p.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });
      // Close the ring
      polyCoords.push(polyCoords[0]);

      const ixSrcId = `ix-src-${ix.id}`;
      map.addSource(ixSrcId, {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: { ixId: ix.id, name: ix.name },
          geometry: { type: 'Polygon', coordinates: [polyCoords] },
        },
      });

      // Intersection surface (asphalt, slightly lighter than road)
      map.addLayer({
        id: `ix-surface-${ix.id}`,
        type: 'fill',
        source: ixSrcId,
        paint: {
          'fill-color': '#404040',
          'fill-opacity': 0.95,
        },
      });

      // Intersection outline (thin white dashed)
      map.addLayer({
        id: `ix-outline-${ix.id}`,
        type: 'line',
        source: ixSrcId,
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#ffffff',
          'line-width': 1,
          'line-dasharray': [1, 1],
          'line-opacity': 0.4,
        },
      });

      // Crosswalk markings at each connected road entry
      for (const conn of ix.connections) {
        const road = roads.find((r) => r.id === conn.roadId);
        if (!road || road.points.length < 2) continue;

        const samples = sampleRoad(road, refLat, refLon, 16);
        const ixLocal = geoToLocal(ix.lat, ix.lon, refLat, refLon);
        const halfW = road.width / 2;

        // Find closest sample to intersection
        let closestIdx = 0;
        let closestDist = Infinity;
        for (let i = 0; i < samples.length; i++) {
          const d = Math.sqrt((samples[i].x - ixLocal.x) ** 2 + (samples[i].y - ixLocal.y) ** 2);
          if (d < closestDist) { closestDist = d; closestIdx = i; }
        }

        // Place crosswalk ~3m before the intersection on this road
        const dir = conn.end === 'start' ? 1 : -1; // toward intersection
        const cwIdx = closestIdx + dir * 2; // a few samples back from intersection
        if (cwIdx < 0 || cwIdx >= samples.length) continue;

        const s = samples[cwIdx];
        let tx: number, ty: number;
        if (cwIdx === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (cwIdx === samples.length - 1) { tx = s.x - samples[cwIdx - 1].x; ty = s.y - samples[cwIdx - 1].y; }
        else { tx = samples[cwIdx + 1].x - samples[cwIdx - 1].x; ty = samples[cwIdx + 1].y - samples[cwIdx - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;

        // Crosswalk = series of stripes perpendicular to road
        const stripeCount = 5;
        const stripeWidth = 0.4; // meters
        const stripeGap = 0.6; // meters
        const crosswalkLength = stripeCount * (stripeWidth + stripeGap);
        const cwFeatures: any[] = [];

        for (let st = 0; st < stripeCount; st++) {
          const offsetAlong = -crosswalkLength / 2 + st * (stripeWidth + stripeGap);
          // Stripe is a thin rectangle perpendicular to road direction
          const cx = s.x + (tx / len) * offsetAlong;
          const cy = s.y + (ty / len) * offsetAlong;
          // Four corners of the stripe
          const p1 = localToGeo(cx + nx * halfW, cy + ny * halfW, refLat, refLon);
          const p2 = localToGeo(cx - nx * halfW, cy - ny * halfW, refLat, refLon);
          const p3 = localToGeo(
            cx + (tx / len) * stripeWidth + nx * halfW,
            cy + (ty / len) * stripeWidth + ny * halfW,
            refLat, refLon
          );
          const p4 = localToGeo(
            cx + (tx / len) * stripeWidth - nx * halfW,
            cy + (ty / len) * stripeWidth - ny * halfW,
            refLat, refLon
          );
          cwFeatures.push({
            type: 'Feature',
            properties: { ixId: ix.id, conn: conn.roadId },
            geometry: {
              type: 'Polygon',
              coordinates: [[[p1.lon, p1.lat], [p2.lon, p2.lat], [p4.lon, p4.lat], [p3.lon, p3.lat], [p1.lon, p1.lat]]],
            },
          });
        }

        if (cwFeatures.length > 0) {
          const cwSrcId = `cw-src-${ix.id}-${conn.roadId}`;
          map.addSource(cwSrcId, {
            type: 'geojson',
            data: { type: 'FeatureCollection', features: cwFeatures },
          });
          map.addLayer({
            id: `cw-${ix.id}-${conn.roadId}`,
            type: 'fill',
            source: cwSrcId,
            paint: {
              'fill-color': '#ffffff',
              'fill-opacity': 0.8,
            },
          });
        }
      }

      // Intersection center marker (small dot)
      const ixCenterSrc = `ix-center-src-${ix.id}`;
      map.addSource(ixCenterSrc, {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: { ixId: ix.id },
          geometry: { type: 'Point', coordinates: [ix.lon, ix.lat] },
        },
      });
      map.addLayer({
        id: `ix-center-${ix.id}`,
        type: 'circle',
        source: ixCenterSrc,
        paint: {
          'circle-radius': 4,
          'circle-color': '#ff6b6b',
          'circle-stroke-color': '#ffffff',
          'circle-stroke-width': 1.5,
        },
      });
    }

    // ─── Control points as circles ──────────────────────────────
    const cpFeatures: any[] = [];
    for (const road of roads) {
      for (let i = 0; i < road.points.length; i++) {
        const p = road.points[i];
        const isSelected = selection.roadId === road.id && selection.pointIndices.includes(i);
        cpFeatures.push({
          type: 'Feature',
          properties: { roadId: road.id, pointIndex: i, selected: isSelected, z: p.z },
          geometry: { type: 'Point', coordinates: [p.lon, p.lat] },
        });
      }
    }

    if (cpFeatures.length > 0) {
      map.addSource('cp-src', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: cpFeatures },
      });

      map.addLayer({
        id: 'cp-ring',
        type: 'circle',
        source: 'cp-src',
        paint: {
          'circle-radius': ['case', ['get', 'selected'], 10, 7],
          'circle-color': '#000000',
          'circle-opacity': 0.5,
        },
      });

      map.addLayer({
        id: 'cp-dot',
        type: 'circle',
        source: 'cp-src',
        paint: {
          'circle-radius': ['case', ['get', 'selected'], 6, 4],
          'circle-color': ['case', ['get', 'selected'], '#ffaa00', '#4ecca3'],
          'circle-stroke-color': '#ffffff',
          'circle-stroke-width': 2,
        },
      });

      map.on('click', 'cp-dot', (e: any) => {
        e.originalEvent.stopPropagation();
        const feature = e.features?.[0];
        if (feature) {
          const { roadId, pointIndex } = feature.properties;
          store.setSelection({ roadId, pointIndices: [pointIndex], handle: null });
        }
      });

      map.on('mouseenter', 'cp-dot', () => { map.getCanvas().style.cursor = 'pointer'; });
      map.on('mouseleave', 'cp-dot', () => { map.getCanvas().style.cursor = ''; });
    }
  }

  // ═══════════════════════════════════════════════════════════
  // 3D VIEW — Babylon.js perspective with full 3D roads
  // ═══════════════════════════════════════════════════════════

  useEffect(() => {
    if (viewMode !== 'perspective') return;
    if (!canvasRef.current || engineRef.current) return;

    const canvas = canvasRef.current;
    const engine = new Engine(canvas, true, { preserveDrawingBuffer: true, stencil: true });
    const scene = new Scene(engine);
    engineRef.current = engine;
    sceneRef.current = scene;

    // ─── Camera — auto-focus on roads if any exist ─────────────
    const roads = roadsRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;

    let centerX = 0, centerZ = 0, count = 0;
    for (const road of roads) {
      for (const p of road.points) {
        const local = geoToLocal(p.lat, p.lon, refLat, refLon);
        centerX += local.x;
        centerZ += local.y;
        count++;
      }
    }
    if (count > 0) { centerX /= count; centerZ /= count; }

    // Compute bounding box for camera distance
    let maxDist = 100;
    if (count > 0) {
      for (const road of roads) {
        for (const p of road.points) {
          const local = geoToLocal(p.lat, p.lon, refLat, refLon);
          const d = Math.sqrt((local.x - centerX) ** 2 + (local.y - centerZ) ** 2);
          maxDist = Math.max(maxDist, d);
        }
      }
    }

    const camera = new ArcRotateCamera(
      'camera', -Math.PI / 2, Math.PI / 3.5,
      Math.max(200, maxDist * 2.5),
      new Vector3(centerX, 0, centerZ), scene
    );
    camera.attachControl(canvas, true);
    cameraRef.current = camera;
    camera.lowerRadiusLimit = 5;
    camera.upperRadiusLimit = 50000;
    camera.lowerBetaLimit = 0;
    camera.upperBetaLimit = Math.PI / 2.1;
    camera.wheelDeltaPercentage = 0.01;
    camera.minZ = 0.1;

    const hemi = new HemisphericLight('hemi', new Vector3(0, 1, 0), scene);
    hemi.intensity = 0.8;
    const dir = new DirectionalLight('dir', new Vector3(-1, -2, -1), scene);
    dir.intensity = 0.4;

    // ─── Ground plane with satellite imagery ───────────────────
    const groundSize = Math.max(4000, maxDist * 6);
    const ground = MeshBuilder.CreateGround('ground', {
      width: groundSize, height: groundSize, subdivisions: 2,
    }, scene);
    const groundMat = new StandardMaterial('groundMat', scene);

    // Try to load satellite tile as ground texture
    // Use Esri tile at current zoom level centered on road area
    const groundLat = refLat;
    const groundLon = refLon;
    const groundTileUrl = `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/14/${Math.floor((1 - Math.sin(groundLat * Math.PI / 180)) / 2 * Math.pow(2, 14))}/${Math.floor((groundLon + 180) / 360 * Math.pow(2, 14))}`;
    try {
      const groundTex = new Texture(groundTileUrl, scene, false, true);
      groundMat.diffuseTexture = groundTex;
      groundMat.diffuseColor = new Color3(0.6, 0.6, 0.6);
    } catch {
      groundMat.diffuseColor = new Color3(0.15, 0.17, 0.19);
    }
    groundMat.backFaceCulling = false;
    groundMat.specularColor = new Color3(0, 0, 0);
    ground.material = groundMat;
    ground.position = new Vector3(centerX, 0, centerZ);
    groundRef.current = ground;

    // Grid — centered on road area
    const gridExtent = Math.min(2000, maxDist * 2);
    const gridStep = gridExtent > 500 ? 50 : 10;
    const gridPoints: Vector3[] = [];
    for (let i = -gridExtent; i <= gridExtent; i += gridStep) {
      gridPoints.push(new Vector3(centerX + i, 0.01, centerZ - gridExtent), new Vector3(centerX + i, 0.01, centerZ + gridExtent));
      gridPoints.push(new Vector3(centerX - gridExtent, 0.01, centerZ + i), new Vector3(centerX + gridExtent, 0.01, centerZ + i));
    }
    const grid = MeshBuilder.CreateLines('grid', { points: gridPoints }, scene);
    grid.color = new Color3(0.2, 0.25, 0.28);
    grid.isPickable = false;

    // Pointer handling
    let isMiddleDown = false;
    let lastX = 0, lastY = 0;

    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      camera.radius = Math.max(camera.lowerRadiusLimit!, Math.min(camera.upperRadiusLimit!, camera.radius + e.deltaY * 0.01));
    }, { passive: false });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());

    scene.onPointerObservable.add((pointerInfo: PointerInfo) => {
      const pt = pointerInfo.type;
      const evt = pointerInfo.event as PointerEvent;

      if (pt === PointerEventTypes.POINTERDOWN) {
        const button = (evt as any).button;
        const pickResult = scene.pick(scene.pointerX, scene.pointerY, (m) => m.metadata?.type === 'control-point' || m.metadata?.type === 'handle');
        const groundPick = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);
        lastX = evt.clientX; lastY = evt.clientY;

        if (button === 1) { isMiddleDown = true; dragStateRef.current.mode = 'rotate'; }
        else if (button === 2) { dragStateRef.current.mode = 'pan'; }
        else if (button === 0) {
          if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'control-point') {
            const meta = pickResult.pickedMesh.metadata;
            dragStateRef.current = { mode: 'move-point', roadId: meta.roadId, pointIndex: meta.pointIndex, handle: null };
            store.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: null });
          } else if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'handle') {
            const meta = pickResult.pickedMesh.metadata;
            dragStateRef.current = { mode: 'move-handle', roadId: meta.roadId, pointIndex: meta.pointIndex, handle: meta.handle };
            store.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: meta.handle });
          } else {
            store.setSelection({ roadId: null, pointIndices: [], handle: null });
          }
        }
      }

      if (pt === PointerEventTypes.POINTERMOVE) {
        const ds = dragStateRef.current;
        const dx = evt.clientX - lastX, dy = evt.clientY - lastY;
        lastX = evt.clientX; lastY = evt.clientY;

        if (ds.mode === 'rotate' && isMiddleDown) {
          camera.alpha -= dx * 0.005;
          camera.beta = Math.max(0, Math.min(Math.PI / 2.1, camera.beta - dy * 0.005));
        } else if (ds.mode === 'pan') {
          const ps = camera.radius * 0.001;
          camera.target.x += dx * ps * Math.cos(camera.alpha);
          camera.target.z -= dx * ps * Math.sin(camera.alpha);
          camera.target.y += dy * ps;
        }

        const gp = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);
        if (ds.mode === 'move-point' && gp?.hit) {
          const w = gp.pickedPoint!;
          const geo = localToGeo(w.x, w.z, refLatRef.current, refLonRef.current);
          store.updateControlPoint(ds.roadId!, ds.pointIndex, geo.lat, geo.lon);
        } else if (ds.mode === 'move-handle' && gp?.hit) {
          const w = gp.pickedPoint!;
          const road = roadsRef.current.find((r) => r.id === ds.roadId);
          if (road) {
            const pt = road.points[ds.pointIndex];
            if (pt) {
              const pl = geoToLocal(pt.lat, pt.lon, refLatRef.current, refLonRef.current);
              const off = localToGeo(w.x - pl.x, w.z - pl.y, 0, 0);
              store.setHandle(ds.roadId!, ds.pointIndex, ds.handle!, { lat: off.lat, lon: off.lon });
            }
          }
        }
      }

      if (pt === PointerEventTypes.POINTERUP) {
        if ((evt as any).button === 1) isMiddleDown = false;
        dragStateRef.current.mode = 'none';
      }
    });

    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') store.finishDrawing();
      else if (e.key === 'Delete' || e.key === 'Backspace') {
        const sel = selectionRef.current;
        if (sel.roadId && sel.pointIndices.length > 0) store.deleteControlPoint(sel.roadId, sel.pointIndices[0]);
      } else if ((e.ctrlKey || e.metaKey) && e.key === 'z') { e.preventDefault(); store.undo(); }
      else if ((e.ctrlKey || e.metaKey) && e.key === 'y') { e.preventDefault(); store.redo(); }
    };
    canvas.addEventListener('keydown', onKeyDown);

    engine.runRenderLoop(() => scene.render());
    window.addEventListener('resize', () => engine.resize());

    update3DMeshes();

    return () => {
      canvas.removeEventListener('keydown', onKeyDown);
      engine.dispose();
      engineRef.current = null;
      sceneRef.current = null;
    };
  }, [viewMode]);

  useEffect(() => {
    if (viewMode === 'perspective') update3DMeshes();
  }, [store.roads, store.selection, viewMode]);

  function updateAllViews() {
    if (viewMode === 'top' && mapRef.current) updateMapRoads(mapRef.current);
    else if (viewMode === 'perspective') update3DMeshes();
  }

  function getTexture(name: string, scene: Scene): Texture | null {
    if (textureCacheRef.current.has(name)) return textureCacheRef.current.get(name)!;
    const map: Record<string, string> = {
      asphalt: '/assets/scaner-roads/pbr/asphalt_diff.png',
      marking: '/assets/scaner-roads/pbr/marking_diff.png',
      macadam: '/assets/scaner-roads/textures/macadam.png',
      bitume: '/assets/scaner-roads/textures/131_bitume.png',
      cobblestone: '/assets/scaner-roads/textures/urban_cobblestone.png',
      pavement: '/assets/scaner-roads/textures/pavement.png',
      sidewalk: '/assets/scaner-roads/textures/sidewalk.png',
    };
    const url = map[name];
    if (!url) return null;
    const tex = new Texture(url, scene, false, true);
    textureCacheRef.current.set(name, tex);
    return tex;
  }

  // ─── 3D mesh generation (Babylon) ──────────────────────────
  function update3DMeshes() {
    const scene = sceneRef.current;
    if (!scene) return;

    roadMeshesRef.current.forEach((m) => m.dispose());
    roadMeshesRef.current.clear();
    pointMeshesRef.current.forEach((m) => m.dispose());
    pointMeshesRef.current.clear();
    handleLinesRef.current.forEach((m) => m.dispose());
    handleLinesRef.current.clear();

    const roads = roadsRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;
    const selection = selectionRef.current;

    for (const road of roads) {
      if (road.points.length < 2) { createCPMeshes(road, refLat, refLon, scene, selection); continue; }

      const samples = sampleRoad(road, refLat, refLon, 16);
      if (samples.length < 2) continue;

      const halfW = road.width / 2;

      // Helper: compute normal at sample i
      const normalAt = (i: number) => {
        let tx: number, ty: number;
        if (i === 0) { tx = samples[1].x - samples[0].x; ty = samples[1].y - samples[0].y; }
        else if (i === samples.length - 1) { tx = samples[i].x - samples[i - 1].x; ty = samples[i].y - samples[i - 1].y; }
        else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        return { nx: -ty / len, ny: tx / len };
      };

      // ─── Helper: build a custom road strip mesh with proper UVs ──
      // Creates a strip from two edge paths with UVs that tile correctly:
      // U goes along the road length (0 to roadLength/tileSize)
      // V goes across the road width (0 to 1)
      function buildStripMesh(
        name: string,
        leftPath: Vector3[],
        rightPath: Vector3[],
        uTileSize: number,  // meters per U tile
        vRepeat: number,    // V repeats across width
        scene: Scene,
      ): Mesh {
        const n = leftPath.length;
        const positions: number[] = [];
        const uvs: number[] = [];
        const indices: number[] = [];

        // Compute cumulative distance along path for U coordinate
        const cumDist: number[] = [0];
        for (let i = 1; i < n; i++) {
          const d = Vector3.Distance(leftPath[i], leftPath[i - 1]);
          cumDist[i] = cumDist[i - 1] + d;
        }

        for (let i = 0; i < n; i++) {
          // Left vertex
          positions.push(leftPath[i].x, leftPath[i].y, leftPath[i].z);
          uvs.push(cumDist[i] / uTileSize, 0);
          // Right vertex
          positions.push(rightPath[i].x, rightPath[i].y, rightPath[i].z);
          uvs.push(cumDist[i] / uTileSize, vRepeat);
        }

        // Build triangles between consecutive segments
        for (let i = 0; i < n - 1; i++) {
          const li = i * 2;       // left current
          const ri = i * 2 + 1;   // right current
          const li2 = (i + 1) * 2;     // left next
          const ri2 = (i + 1) * 2 + 1; // right next
          // Two triangles per quad
          indices.push(li, ri, li2);
          indices.push(ri, ri2, li2);
        }

        const mesh = new Mesh(name, scene);
        const vertexData = new VertexData();
        vertexData.positions = positions;
        vertexData.indices = indices;
        vertexData.uvs = uvs;
        // Compute normals
        const normals: number[] = [];
        VertexData.ComputeNormals(positions, indices, normals);
        vertexData.normals = normals;
        vertexData.applyToMesh(mesh, true);
        return mesh;
      }

      // ─── Helper: build dashed marking stripes ─────────────────
      // Creates a series of thin rectangular meshes along an offset path
      function buildDashedMarking(
        name: string,
        samples: Array<{ x: number; y: number; z: number }>,
        offsetMeters: number,
        normalAt: (i: number) => { nx: number; ny: number },
        dashLength: number,   // meters
        gapLength: number,    // meters
        markingWidth: number, // meters
        yOffset: number,      // height above road
        color: Color3,
        scene: Scene,
      ) {
        // Compute cumulative distance
        const cumDist: number[] = [0];
        for (let i = 1; i < samples.length; i++) {
          const d = Math.sqrt((samples[i].x - samples[i-1].x) ** 2 + (samples[i].y - samples[i-1].y) ** 2);
          cumDist[i] = cumDist[i - 1] + d;
        }
        const totalLength = cumDist[cumDist.length - 1];

        // Generate dashes at regular intervals
        let pos = 0;
        let dashIdx = 0;
        while (pos < totalLength) {
          const dashEnd = Math.min(pos + dashLength, totalLength);
          // Find sample indices for this dash segment
          const startIdx = cumDist.findIndex((d) => d >= pos);
          const endIdx = cumDist.findIndex((d) => d >= dashEnd);
          if (startIdx < 0 || endIdx < 0) break;

          // Build dash as a thin strip
          const dashLeft: Vector3[] = [];
          const dashRight: Vector3[] = [];
          for (let i = startIdx; i <= endIdx; i++) {
            const s = samples[i];
            const { nx, ny } = normalAt(i);
            const halfMw = markingWidth / 2;
            dashLeft.push(new Vector3(s.x + nx * (offsetMeters + halfMw), s.z + yOffset, s.y + ny * (offsetMeters + halfMw)));
            dashRight.push(new Vector3(s.x + nx * (offsetMeters - halfMw), s.z + yOffset, s.y + ny * (offsetMeters - halfMw)));
          }
          const dashMesh = buildStripMesh(`${name}_${dashIdx}`, dashLeft, dashRight, 1, 1, scene);
          const dashMat = new StandardMaterial(`${name}_mat_${dashIdx}`, scene);
          dashMat.backFaceCulling = false;
          dashMat.diffuseColor = color;
          dashMat.emissiveColor = color.scale(0.15);
          dashMat.specularColor = new Color3(0, 0, 0);
          dashMesh.material = dashMat;
          dashMesh.isPickable = false;
          roadMeshesRef.current.set(`${name}_${dashIdx}`, dashMesh);

          pos = dashEnd + gapLength;
          dashIdx++;
        }
      }

      // ─── Road surface with proper UV mapping ──────────────────
      const pL: Vector3[] = [], pR: Vector3[] = [];
      for (let i = 0; i < samples.length; i++) {
        const s = samples[i]; const { nx, ny } = normalAt(i);
        pL.push(new Vector3(s.x + nx * halfW, s.z + 0.02, s.y + ny * halfW));
        pR.push(new Vector3(s.x - nx * halfW, s.z + 0.02, s.y - ny * halfW));
      }
      const roadMesh = buildStripMesh(`road_${road.id}`, pL, pR, 5, 1, scene);
      const roadMat = new StandardMaterial(`rmat_${road.id}`, scene);
      roadMat.backFaceCulling = false;
      const tex = getTexture(road.profile?.surfaceTexture || 'asphalt', scene);
      if (tex) {
        roadMat.diffuseTexture = tex;
        roadMat.diffuseColor = new Color3(1, 1, 1);
        tex.wrapU = Texture.WRAP_ADDRESSMODE;
        tex.wrapV = Texture.WRAP_ADDRESSMODE;
      } else {
        roadMat.diffuseColor = new Color3(0.23, 0.23, 0.23);
      }
      roadMat.emissiveColor = new Color3(0.15, 0.15, 0.15);
      roadMat.specularColor = new Color3(0.05, 0.05, 0.05);
      roadMesh.material = roadMat;
      roadMesh.isPickable = false;
      roadMeshesRef.current.set(road.id, roadMesh);

      // ─── Center line marking (dashed yellow) ──────────────────
      buildDashedMarking(
        `center_${road.id}`, samples, 0, normalAt,
        3, 3, 0.15, 0.05,  // 3m dash, 3m gap, 15cm wide, 5cm above road
        new Color3(1, 0.85, 0.2), scene
      );

      // ─── Lane divider markings (dashed white) ─────────────────
      if (road.laneCount > 1) {
        const laneSpacing = road.width / road.laneCount;
        for (let lane = 1; lane < road.laneCount; lane++) {
          const offset = -halfW + laneSpacing * lane;
          buildDashedMarking(
            `div_${road.id}_${lane}`, samples, offset, normalAt,
            3, 3, 0.12, 0.04,  // 3m dash, 3m gap, 12cm wide
            new Color3(1, 1, 1), scene
          );
        }
      }

      // ─── Edge lines (solid white at road edges) ───────────────
      for (const side of [-1, 1]) {
        const edgeOffset = halfW * side - (0.1 * side); // 10cm inside edge
        buildDashedMarking(
          `edge_${road.id}_${side}`, samples, edgeOffset, normalAt,
          9999, 0, 0.1, 0.04,  // continuous (long dash, no gap)
          new Color3(1, 1, 1), scene
        );
      }

      // ─── Curbs — raised edges on both sides ───────────────────
      if (road.profile?.hasCurb) {
        const cw = 0.3, ch = 0.15;
        for (const side of [-1, 1]) {
          const cL: Vector3[] = [], cR: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i]; const { nx, ny } = normalAt(i);
            cL.push(new Vector3(s.x + nx * halfW * side, s.z + ch, s.y + ny * halfW * side));
            cR.push(new Vector3(s.x + nx * (halfW + cw) * side, s.z + ch, s.y + ny * (halfW + cw) * side));
          }
          const cm = buildStripMesh(`curb_${road.id}_${side}`, cL, cR, 1, 1, scene);
          const cmMat = new StandardMaterial(`curbM_${road.id}_${side}`, scene);
          cmMat.backFaceCulling = false;
          const curbTex = getTexture('pavement', scene);
          if (curbTex) {
            cmMat.diffuseTexture = curbTex;
            cmMat.diffuseColor = new Color3(0.7, 0.7, 0.7);
            curbTex.wrapU = Texture.WRAP_ADDRESSMODE;
            curbTex.wrapV = Texture.WRAP_ADDRESSMODE;
          } else {
            cmMat.diffuseColor = new Color3(0.5, 0.5, 0.5);
          }
          cmMat.emissiveColor = new Color3(0.08, 0.08, 0.08);
          cmMat.specularColor = new Color3(0, 0, 0);
          cm.material = cmMat; cm.isPickable = false;
          roadMeshesRef.current.set(`curb_${road.id}_${side}`, cm);
        }
      }

      // ─── Sidewalks with proper UV tiling ──────────────────────
      if (road.profile?.hasSidewalk) {
        const sw = 2.0, sh = 0.15, cw = road.profile?.hasCurb ? 0.3 : 0;
        for (const side of [-1, 1]) {
          const sL: Vector3[] = [], sR: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i]; const { nx, ny } = normalAt(i);
            sL.push(new Vector3(s.x + nx * (halfW + cw) * side, s.z + sh, s.y + ny * (halfW + cw) * side));
            sR.push(new Vector3(s.x + nx * (halfW + cw + sw) * side, s.z + sh, s.y + ny * (halfW + cw + sw) * side));
          }
          const sm = buildStripMesh(`sw_${road.id}_${side}`, sL, sR, 2, 1, scene);
          const smMat = new StandardMaterial(`swM_${road.id}_${side}`, scene);
          smMat.backFaceCulling = false;
          const stex = getTexture('sidewalk', scene);
          if (stex) {
            smMat.diffuseTexture = stex;
            smMat.diffuseColor = new Color3(0.85, 0.85, 0.85);
            stex.wrapU = Texture.WRAP_ADDRESSMODE;
            stex.wrapV = Texture.WRAP_ADDRESSMODE;
          } else {
            smMat.diffuseColor = new Color3(0.65, 0.65, 0.65);
          }
          smMat.emissiveColor = new Color3(0.1, 0.1, 0.1);
          smMat.specularColor = new Color3(0, 0, 0);
          sm.material = smMat; sm.isPickable = false;
          roadMeshesRef.current.set(`sw_${road.id}_${side}`, sm);
        }
      }

      createCPMeshes(road, refLat, refLon, scene, selection);
    }

    // ─── Intersection surfaces in 3D ───────────────────────────
    const intersections = intersectionsRef.current;
    for (const ix of intersections) {
      if (ix.connections.length < 2) continue;

      const polyLocal = computeIntersectionPolygon(ix, roads, refLat, refLon);
      if (polyLocal.length < 3) continue;

      // Build intersection surface as a custom mesh (filled polygon at road height)
      const positions: number[] = [];
      const indices: number[] = [];
      const uvs: number[] = [];

      // Use the intersection center as the first vertex (fan triangulation)
      const ixLocal = geoToLocal(ix.lat, ix.lon, refLat, refLon);
      const zHeight = ix.z + 0.03; // slightly above road surface

      // Center vertex
      positions.push(ixLocal.x, zHeight, ixLocal.y);
      uvs.push(0.5, 0.5);

      // Boundary vertices
      for (let i = 0; i < polyLocal.length; i++) {
        positions.push(polyLocal[i].x, zHeight, polyLocal[i].y);
        // UV based on distance from center
        const dx = polyLocal[i].x - ixLocal.x;
        const dy = polyLocal[i].y - ixLocal.y;
        uvs.push(0.5 + dx / 20, 0.5 + dy / 20);
      }

      // Fan triangulation from center
      for (let i = 0; i < polyLocal.length; i++) {
        const next = (i + 1) % polyLocal.length;
        indices.push(0, i + 1, next + 1);
      }

      const ixMesh = new Mesh(`ix_surface_${ix.id}`, scene);
      const ixVertexData = new VertexData();
      ixVertexData.positions = positions;
      ixVertexData.indices = indices;
      ixVertexData.uvs = uvs;
      const ixNormals: number[] = [];
      VertexData.ComputeNormals(positions, indices, ixNormals);
      ixVertexData.normals = ixNormals;
      ixVertexData.applyToMesh(ixMesh, true);

      const ixMat = new StandardMaterial(`ix_mat_${ix.id}`, scene);
      ixMat.backFaceCulling = false;
      const ixTex = getTexture('asphalt', scene);
      if (ixTex) {
        ixMat.diffuseTexture = ixTex;
        ixMat.diffuseColor = new Color3(0.9, 0.9, 0.9);
        ixTex.wrapU = Texture.WRAP_ADDRESSMODE;
        ixTex.wrapV = Texture.WRAP_ADDRESSMODE;
      } else {
        ixMat.diffuseColor = new Color3(0.25, 0.25, 0.25);
      }
      ixMat.emissiveColor = new Color3(0.12, 0.12, 0.12);
      ixMat.specularColor = new Color3(0.05, 0.05, 0.05);
      ixMesh.material = ixMat;
      ixMesh.isPickable = false;
      roadMeshesRef.current.set(`ix_surface_${ix.id}`, ixMesh);

      // ─── Crosswalk stripes at each connected road entry ────────
      for (const conn of ix.connections) {
        const road = roads.find((r) => r.id === conn.roadId);
        if (!road || road.points.length < 2) continue;

        const samples = sampleRoad(road, refLat, refLon, 16);
        const halfW = road.width / 2;

        // Find closest sample to intersection
        let closestIdx = 0;
        let closestDist = Infinity;
        for (let i = 0; i < samples.length; i++) {
          const d = Math.sqrt((samples[i].x - ixLocal.x) ** 2 + (samples[i].y - ixLocal.y) ** 2);
          if (d < closestDist) { closestDist = d; closestIdx = i; }
        }

        const dir = conn.end === 'start' ? 1 : -1;
        const cwIdx = closestIdx + dir * 2;
        if (cwIdx < 0 || cwIdx >= samples.length) continue;

        const s = samples[cwIdx];
        let tx: number, ty: number;
        if (cwIdx === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (cwIdx === samples.length - 1) { tx = s.x - samples[cwIdx - 1].x; ty = s.y - samples[cwIdx - 1].y; }
        else { tx = samples[cwIdx + 1].x - samples[cwIdx - 1].x; ty = samples[cwIdx + 1].y - samples[cwIdx - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;

        // Create crosswalk stripes as thin meshes
        const stripeCount = 5;
        const stripeWidth = 0.4;
        const stripeGap = 0.6;
        const crosswalkLength = stripeCount * (stripeWidth + stripeGap);

        for (let st = 0; st < stripeCount; st++) {
          const offsetAlong = -crosswalkLength / 2 + st * (stripeWidth + stripeGap);
          const cx = s.x + (tx / len) * offsetAlong;
          const cy = s.y + (ty / len) * offsetAlong;

          // Stripe corners (perpendicular to road)
          const sL: Vector3[] = [new Vector3(cx + nx * halfW, s.z + 0.05, cy + ny * halfW)];
          const sR: Vector3[] = [new Vector3(cx - nx * halfW, s.z + 0.05, cy - ny * halfW)];
          const sL2: Vector3[] = [new Vector3(
            cx + (tx / len) * stripeWidth + nx * halfW,
            s.z + 0.05,
            cy + (ty / len) * stripeWidth + ny * halfW
          )];
          const sR2: Vector3[] = [new Vector3(
            cx + (tx / len) * stripeWidth - nx * halfW,
            s.z + 0.05,
            cy + (ty / len) * stripeWidth - ny * halfW
          )];

          // Build stripe as a small strip mesh
          const stripeMesh = buildStripMesh(
            `cw_${ix.id}_${conn.roadId}_${st}`,
            [sL[0], sL2[0]], [sR[0], sR2[0]], 1, 1, scene
          );
          const stripeMat = new StandardMaterial(`cw_mat_${ix.id}_${st}`, scene);
          stripeMat.backFaceCulling = false;
          stripeMat.diffuseColor = new Color3(0.95, 0.95, 0.95);
          stripeMat.emissiveColor = new Color3(0.2, 0.2, 0.2);
          stripeMat.specularColor = new Color3(0, 0, 0);
          stripeMesh.material = stripeMat;
          stripeMesh.isPickable = false;
          roadMeshesRef.current.set(`cw_${ix.id}_${conn.roadId}_${st}`, stripeMesh);
        }
      }

      // Intersection center marker (small red sphere)
      const ixMarker = MeshBuilder.CreateSphere(`ix_marker_${ix.id}`, { diameter: 1.5, segments: 8 }, scene);
      ixMarker.position = new Vector3(ixLocal.x, zHeight + 1, ixLocal.y);
      const ixMarkerMat = new StandardMaterial(`ix_marker_mat_${ix.id}`, scene);
      ixMarkerMat.diffuseColor = new Color3(1, 0.3, 0.3);
      ixMarkerMat.emissiveColor = new Color3(0.4, 0.1, 0.1);
      ixMarkerMat.specularColor = new Color3(0, 0, 0);
      ixMarker.material = ixMarkerMat;
      ixMarker.isPickable = false;
      roadMeshesRef.current.set(`ix_marker_${ix.id}`, ixMarker);
    }
  }

  function createCPMeshes(road: Road, refLat: number, refLon: number, scene: Scene, selection: any) {
    for (let i = 0; i < road.points.length; i++) {
      const p = road.points[i];
      const local = geoToLocal(p.lat, p.lon, refLat, refLon);
      const isSel = selection.roadId === road.id && selection.pointIndices.includes(i);

      const cpMesh = MeshBuilder.CreateSphere(`cp_${road.id}_${i}`, { diameter: isSel ? 4 : 3, segments: 12 }, scene);
      cpMesh.position = new Vector3(local.x, p.z, local.y);
      const cpMat = new StandardMaterial(`cpm_${road.id}_${i}`, scene);
      cpMat.diffuseColor = isSel ? new Color3(1, 0.8, 0.2) : new Color3(0.3, 0.8, 0.65);
      cpMat.emissiveColor = isSel ? new Color3(0.4, 0.3, 0) : new Color3(0, 0, 0);
      cpMat.specularColor = new Color3(0, 0, 0);
      cpMesh.material = cpMat;
      cpMesh.metadata = { type: 'control-point', roadId: road.id, pointIndex: i };
      pointMeshesRef.current.set(`cp_${road.id}_${i}`, cpMesh);

      if ((p.type === 'smooth' && (p.handleIn || p.handleOut)) || isSel) {
        for (const [handle, color] of [['in', new Color3(0.4, 0.6, 1)], ['out', new Color3(1, 0.5, 0.3)]] as const) {
          const h = handle === 'in' ? p.handleIn : p.handleOut;
          if (!h) continue;
          const hl = geoToLocal(p.lat + h.lat, p.lon + h.lon, refLat, refLon);
          const hLine = MeshBuilder.CreateLines(`h${handle}_${road.id}_${i}`, {
            points: [new Vector3(local.x, p.z, local.y), new Vector3(hl.x, p.z, hl.y)],
          }, scene);
          hLine.color = color; hLine.isPickable = false;
          const hSphere = MeshBuilder.CreateSphere(`h${handle}S_${road.id}_${i}`, { diameter: 2, segments: 8 }, scene);
          hSphere.position = new Vector3(hl.x, p.z, hl.y);
          const hMat = new StandardMaterial(`h${handle}M_${road.id}_${i}`, scene);
          hMat.diffuseColor = color; hMat.emissiveColor = color.scale(0.2); hMat.specularColor = new Color3(0, 0, 0);
          hSphere.material = hMat;
          hSphere.metadata = { type: 'handle', roadId: road.id, pointIndex: i, handle };
        }
      }
    }
  }

  // ─── Render ────────────────────────────────────────────────
  return (
    <div className={className} style={{ position: 'relative', width: '100%', height: '100%' }}>
      {viewMode === 'top' && (
        <div ref={mapContainerRef} style={{ width: '100%', height: '100%' }} />
      )}
      {viewMode === 'perspective' && (
        <canvas
          ref={canvasRef}
          style={{ width: '100%', height: '100%', outline: 'none', touchAction: 'none' }}
          tabIndex={0}
        />
      )}
    </div>
  );
};
