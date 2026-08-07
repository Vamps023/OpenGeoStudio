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
  type GeneratedIntersection,
  type Point2D,
  geoToLocal,
  localToGeo,
  detectIntersections,
  distanceMeters,
} from '../shared/types';
import { roadEngine, type RoadBuildResult, type MeshSectionData, type LaneCenterlineData, type LaneBoundaryData } from '../shared/roadEngineClient';
import {
  Engine, Scene, ArcRotateCamera, Vector3, HemisphericLight, DirectionalLight,
  MeshBuilder, StandardMaterial, Color3, Color4, Mesh, LinesMesh, Texture,
  VertexData, VertexBuffer,
  PointerEventTypes, PointerInfo,
} from '@babylonjs/core';

interface RoadViewportProps {
  className?: string;
}

// ─── Legacy TS dashed marking builder (fallback when buildRoad() unavailable) ──
function buildDashedMarkingTS(
  name: string,
  samples: Array<{ x: number; y: number; z: number }>,
  offsetMeters: number,
  normalAt: (i: number) => { nx: number; ny: number },
  dashLength: number,
  gapLength: number,
  markingWidth: number,
  yOffset: number,
  color: Color3,
  scene: Scene,
  buildStripMesh: (name: string, leftPath: Vector3[], rightPath: Vector3[], uTileSize: number, vRepeat: number, scene: Scene) => Mesh,
  roadMeshesRef: React.MutableRefObject<Map<string, Mesh>>,
) {
  const cumDist: number[] = [0];
  for (let i = 1; i < samples.length; i++) {
    const d = Math.sqrt((samples[i].x - samples[i-1].x) ** 2 + (samples[i].y - samples[i-1].y) ** 2);
    cumDist[i] = cumDist[i - 1] + d;
  }
  const totalLength = cumDist[cumDist.length - 1];
  let pos = 0;
  let dashIdx = 0;
  while (pos < totalLength) {
    const dashEnd = Math.min(pos + dashLength, totalLength);
    const startIdx = cumDist.findIndex((d) => d >= pos);
    const endIdx = cumDist.findIndex((d) => d >= dashEnd);
    if (startIdx < 0 || endIdx < 0) break;
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
  const generatedIntersectionsRef = useRef<GeneratedIntersection[]>([]);
  const selectedRoadIdsRef = useRef<string[]>([]);
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

  // ─── Road sample cache (populated async by C++ engine, used sync for rendering) ───
  const sampleCacheRef = useRef<Map<string, Array<{ x: number; y: number; z: number }>>>(new Map());
  // ─── Road build result cache (Phase 2.8 — full lane engine pipeline) ───
  const buildCacheRef = useRef<Map<string, RoadBuildResult>>(new Map());

  // Track shift key state globally (more reliable than e.originalEvent.shiftKey in Electron)
  const shiftDownRef = useRef(false);

  // Debug layer refs (sync access from update3DMeshes)
  const debugModeRef = useRef(false);
  const debugLayersRef = useRef<{
    laneCenters: boolean; laneBoundaryLines: boolean; laneIds: boolean; meshWireframe: boolean;
  }>({ laneCenters: false, laneBoundaryLines: false, laneIds: false, meshWireframe: false });

  const store = useRoadStudioStore();
  const viewMode = store.viewMode;

  useEffect(() => { toolRef.current = store.tool; }, [store.tool]);
  useEffect(() => { selectionRef.current = store.selection; }, [store.selection]);
  useEffect(() => { selectedRoadIdsRef.current = store.selectedRoadIds; updateAllViews(); }, [store.selectedRoadIds]);
  useEffect(() => { generatedIntersectionsRef.current = store.generatedIntersections; updateAllViews(); }, [store.generatedIntersections]);
  useEffect(() => { updateAllViews(); }, [store.arcPreview]);
  useEffect(() => { updateAllViews(); }, [store.clothoidPreview]);
  useEffect(() => { updateAllViews(); }, [store.polylinePoints]);
  useEffect(() => { updateAllViews(); }, [store.splinePoints]);
  useEffect(() => { updateAllViews(); }, [store.previewPoint]);
  useEffect(() => {
    roadsRef.current = store.roads;
    // Recompute intersections whenever roads change
    intersectionsRef.current = detectIntersections(store.roads);
    if (store.recomputeIntersections) store.recomputeIntersections();
    // Refresh sample cache from C++ engine
    refreshSampleCache();
    updateAllViews();
  }, [store.roads]);
  useEffect(() => { refLatRef.current = store.refLat; refLonRef.current = store.refLon; updateAllViews(); }, [store.refLat, store.refLon]);
  useEffect(() => { gridSizeRef.current = store.gridSize; }, [store.gridSize]);
  useEffect(() => { snapEnabledRef.current = store.snapEnabled; }, [store.snapEnabled]);
  useEffect(() => { drawingRoadIdRef.current = store.drawingRoadId; }, [store.drawingRoadId]);
  const debugMode = store.debugMode;
  const debugLayers = store.debugLayers;
  useEffect(() => {
    debugModeRef.current = debugMode;
    debugLayersRef.current = {
      laneCenters: debugLayers.laneCenters,
      laneBoundaryLines: debugLayers.laneBoundaryLines,
      laneIds: debugLayers.laneIds,
      meshWireframe: debugLayers.meshWireframe,
    };
    updateAllViews();
  }, [debugMode, debugLayers]);

  // Track shift key globally — more reliable than e.originalEvent.shiftKey in Electron
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Shift') shiftDownRef.current = true;
      // Escape: cancel any in-progress drawing
      if (e.key === 'Escape') {
        const st = useRoadStudioStore.getState();
        st.cancelDrawing();
        st.finishDrawing();
      }
      // Enter: finish current drawing operation
      if (e.key === 'Enter') {
        const st = useRoadStudioStore.getState();
        if (st.arcStartPoint && (st.tool === 'arc' || st.tool === 'clothoid')) {
          if (st.tool === 'arc') st.finishArc();
          else st.finishClothoid();
        } else if (st.polylinePoints) {
          st.finishPolyline();
        } else if (st.splinePoints) {
          st.finishSpline();
        } else {
          st.finishDrawing();
        }
      }
      // Ctrl+Shift+G: Toggle geometry debug mode
      if (e.key === 'g' && e.ctrlKey && e.shiftKey) {
        e.preventDefault();
        useRoadStudioStore.getState().toggleDebugMode();
      }
    };
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.key === 'Shift') shiftDownRef.current = false;
    };
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
    };
  }, []);

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
      // Use latest store state (avoid stale closure)
      const st = useRoadStudioStore.getState();

      // Shift+click OR road pick mode = road selection for intersection creation
      const isShift = shiftDownRef.current || e.originalEvent.shiftKey || st.roadPickMode;
      if (isShift) {
        console.log('[RoadSelect] Detected! Roads:', roadsRef.current.length, 'pickMode:', st.roadPickMode, 'shiftDown:', shiftDownRef.current);
        const clickLat = e.lngLat.lat;
        const clickLon = e.lngLat.lng;
        const refLat = refLatRef.current;
        const refLon = refLonRef.current;

        // Method 1: Try queryRenderedFeatures on road surface layers
        const roadLayers = roadsRef.current
          .map((r) => `rd-surface-${r.id}`)
          .filter((id) => map.getLayer(id));
        console.log('[Shift+Click] Road layers found:', roadLayers.length, roadLayers);
        let selectedRoadId: string | null = null;

        if (roadLayers.length > 0) {
          const features = map.queryRenderedFeatures(e.point, { layers: roadLayers });
          console.log('[Shift+Click] Features hit:', features.length);
          if (features.length > 0) {
            selectedRoadId = features[0].properties.roadId;
            console.log('[Shift+Click] Selected via features:', selectedRoadId);
          }
        }

        // Method 2: Fallback — find closest road by distance to click point
        if (!selectedRoadId) {
          const clickLocal = geoToLocal(clickLat, clickLon, refLat, refLon);
          console.log('[Shift+Click] Click local:', clickLocal.x.toFixed(1), clickLocal.y.toFixed(1));
          let closestRoadId: string | null = null;
          let closestDist = Infinity;
          for (const road of roadsRef.current) {
            if (road.points.length < 2) continue;
            const samples = getCachedSamples(road.id);
            for (const s of samples) {
              const dx = s.x - clickLocal.x;
              const dy = s.y - clickLocal.y;
              const d = Math.sqrt(dx * dx + dy * dy);
              if (d < closestDist) {
                closestDist = d;
                closestRoadId = road.id;
              }
            }
          }
          console.log('[Shift+Click] Closest road:', closestRoadId, 'dist:', closestDist.toFixed(1) + 'm');
          // Only select if within 50m
          if (closestRoadId && closestDist < 50) {
            selectedRoadId = closestRoadId;
            console.log('[Shift+Click] Selected via closest:', selectedRoadId);
          }
        }

        if (selectedRoadId) {
          console.log('[Shift+Click] Toggling selection for:', selectedRoadId);
          st.toggleRoadSelection(selectedRoadId);
          console.log('[Shift+Click] After toggle, selectedRoadIds:', useRoadStudioStore.getState().selectedRoadIds);
        } else {
          console.log('[Shift+Click] No road found near click point');
        }
        return;
      }

      // Check if clicking on a control point (cp-dot layer)
      if (map.getLayer('cp-dot')) {
        const cpFeatures = map.queryRenderedFeatures(e.point, { layers: ['cp-dot'] });
        if (cpFeatures.length > 0) {
          const { roadId, pointIndex } = cpFeatures[0].properties;
          st.setSelection({ roadId, pointIndices: [pointIndex], handle: null });
          return;
        }
      }

      const tool = toolRef.current;
      const refLat = refLatRef.current;
      const refLon = refLonRef.current;
      const clickLocal = geoToLocal(e.lngLat.lat, e.lngLat.lng, refLat, refLon);

      // ─── Helper: snap to existing endpoints ────────────────
      const snapPoint = (lat: number, lon: number): { lat: number; lon: number } => {
        if (!snapEnabledRef.current) return { lat, lon };
        const snapDist = 0.00005;
        for (const road of roadsRef.current) {
          for (const p of road.points) {
            if (Math.abs(p.lat - lat) < snapDist && Math.abs(p.lon - lon) < snapDist) {
              return { lat: p.lat, lon: p.lon };
            }
          }
        }
        return { lat, lon };
      };

      // ─── Helper: get tangent from last road segment ─────────
      const getLastTangent = (): { point: Point2D; dir: Point2D } | null => {
        const drawingId = drawingRoadIdRef.current;
        if (!drawingId) return null;
        const road = roadsRef.current.find((r) => r.id === drawingId);
        if (!road || road.points.length < 2) return null;
        const p0 = road.points[road.points.length - 2];
        const p1 = road.points[road.points.length - 1];
        const l0 = geoToLocal(p0.lat, p0.lon, refLat, refLon);
        const l1 = geoToLocal(p1.lat, p1.lon, refLat, refLon);
        const dx = l1.x - l0.x;
        const dy = l1.y - l0.y;
        const len = Math.sqrt(dx * dx + dy * dy) || 1;
        return { point: l1, dir: { x: dx / len, y: dy / len } };
      };

      // ─── Segment tool (straight line) ──────────────────────
      if (tool === 'line') {
        const snapped = snapPoint(e.lngLat.lat, e.lngLat.lng);
        const drawingId = drawingRoadIdRef.current;
        if (!drawingId) {
          st.startNewRoad(snapped.lat, snapped.lon);
        } else {
          st.pushHistory('Add segment point');
          st.addControlPoint(drawingId, snapped.lat, snapped.lon);
        }
        return;
      }

      // ─── Pen (Bézier) tool ──────────────────────────────────
      if (tool === 'pen') {
        const snapped = snapPoint(e.lngLat.lat, e.lngLat.lng);
        const drawingId = drawingRoadIdRef.current;
        if (!drawingId) {
          st.startNewRoad(snapped.lat, snapped.lon);
        } else {
          st.pushHistory('Add bézier point');
          st.addControlPoint(drawingId, snapped.lat, snapped.lon);
        }
        return;
      }

      // ─── Circle Arc tool ────────────────────────────────────
      if (tool === 'arc') {
        if (!st.arcStartPoint) {
          // Start: use last road segment direction if available
          const lastTan = getLastTangent();
          if (lastTan) {
            st.startArc(lastTan.point, lastTan.dir);
          } else {
            // No existing road — start from click with default direction
            st.startNewRoad(e.lngLat.lat, e.lngLat.lng);
            return;
          }
        } else {
          st.finishArc();
        }
        return;
      }

      // ─── Clothoid Arc tool ──────────────────────────────────
      if (tool === 'clothoid') {
        if (!st.arcStartPoint) {
          const lastTan = getLastTangent();
          if (lastTan) {
            st.startClothoid(lastTan.point, lastTan.dir);
          } else {
            st.startNewRoad(e.lngLat.lat, e.lngLat.lng);
            return;
          }
        } else {
          st.finishClothoid();
        }
        return;
      }

      // ─── Polyline tool ──────────────────────────────────────
      if (tool === 'polyline') {
        if (!st.polylinePoints) {
          st.startPolyline(clickLocal);
        } else {
          st.addPolylinePoint(clickLocal);
        }
        return;
      }

      // ─── Clothoid Spline tool ───────────────────────────────
      if (tool === 'spline') {
        if (!st.splinePoints) {
          st.startSpline(clickLocal);
        } else {
          st.addSplinePoint(clickLocal);
        }
        return;
      }
    });

    // Right-click: finish current drawing operation
    map.on('contextmenu', (e) => {
      e.preventDefault();
      const st = useRoadStudioStore.getState();
      if (st.arcStartPoint && st.tool === 'arc') {
        st.finishArc();
      } else if (st.arcStartPoint && st.tool === 'clothoid') {
        st.finishClothoid();
      } else if (st.polylinePoints) {
        st.finishPolyline();
      } else if (st.splinePoints) {
        st.finishSpline();
      } else if (st.drawingRoadId) {
        st.finishDrawing();
      }
    });

    // Cursor: pointer when hovering over a road surface or control point
    map.on('mousemove', (e) => {
      const roadLayers = roadsRef.current.map((r) => `rd-surface-${r.id}`).filter((id) => map.getLayer(id));
      const cpLayers = map.getLayer('cp-dot') ? ['cp-dot'] : [];
      const features = map.queryRenderedFeatures(e.point, { layers: [...roadLayers, ...cpLayers] });
      const st = useRoadStudioStore.getState();
      const tool = toolRef.current;
      const mouseLocal = geoToLocal(e.lngLat.lat, e.lngLat.lon, refLatRef.current, refLonRef.current);

      // Update preview point for all tools
      st.setPreviewPoint(mouseLocal);

      // Tool-specific preview updates
      if (st.arcStartPoint && tool === 'arc') {
        st.updateArcPreview(mouseLocal);
        map.getCanvas().style.cursor = 'crosshair';
      } else if (st.arcStartPoint && tool === 'clothoid') {
        st.updateClothoidPreview(mouseLocal);
        map.getCanvas().style.cursor = 'crosshair';
      } else if (st.polylinePoints && tool === 'polyline') {
        map.getCanvas().style.cursor = 'crosshair';
      } else if (st.splinePoints && tool === 'spline') {
        map.getCanvas().style.cursor = 'crosshair';
      } else if (st.drawingRoadId && (tool === 'line' || tool === 'pen')) {
        map.getCanvas().style.cursor = 'crosshair';
      } else {
        map.getCanvas().style.cursor = features.length > 0 ? 'pointer' : '';
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

    // Remove old layers/sources (including all preview layers)
    const style = map.getStyle();
    const previewPrefixes = ['rd-', 'cp-', 'ix-', 'cw-', 'gi-', 'arc-', 'line-', 'clothoid-', 'polyline-', 'spline-', 'dbg-'];
    if (style?.layers) {
      for (const layer of style.layers) {
        if (previewPrefixes.some((p) => layer.id.startsWith(p))) {
          map.removeLayer(layer.id);
        }
      }
    }
    if (style?.sources) {
      for (const src of Object.keys(style.sources)) {
        if (previewPrefixes.some((p) => src.startsWith(p))) {
          map.removeSource(src);
        }
      }
    }

    const roads = roadsRef.current;
    const selection = selectionRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;
    const genIntersections = generatedIntersectionsRef.current;

    // Helper: check if a local point is inside any generated intersection zone
    function isInsideIntersection(x: number, y: number): boolean {
      for (const gen of genIntersections) {
        const d = Math.sqrt((x - gen.center.x) ** 2 + (y - gen.center.y) ** 2);
        const maxHalfW = Math.max(...gen.approaches.map((a) => a.width / 2));
        if (d < maxHalfW + 8) return true; // generous zone for markings removal
      }
      return false;
    }

    // Helper: filter samples to remove those inside intersection zones
    function filterSamplesOutsideIntersections(
      samples: Array<{ x: number; y: number; z: number }>
    ): Array<{ x: number; y: number; z: number }> {
      if (genIntersections.length === 0) return samples;
      return samples.filter((s) => !isInsideIntersection(s.x, s.y));
    }

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

      // Sample road path (from C++ engine cache)
      const samples = getCachedSamples(road.id);
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
        data: { type: 'Feature', properties: { roadId: road.id, name: road.name }, geometry: { type: 'Polygon', coordinates: roadPolygon } },
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

      // Shift+click on road surface = toggle road selection (for intersection)
      // NOTE: Handler is registered globally once (in map init), not per-road.
      // We use queryRenderedFeatures to find which road was clicked.
      // Cursor hover handled globally too.

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
      // Filter out samples inside intersection zones
      const filteredSamples = filterSamplesOutsideIntersections(samples);
      if (road.laneCount > 1 && filteredSamples.length >= 2) {
        const laneSpacing = road.width / road.laneCount;
        for (let lane = 1; lane < road.laneCount; lane++) {
          const offset = -halfWidth + laneSpacing * lane;
          const dividerCoords = buildOffsetLine(filteredSamples, offset);
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
      // Use filtered samples (no markings inside intersection)
      const centerCoords = filteredSamples.map((s) => {
        const geo = localToGeo(s.x, s.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });
      if (centerCoords.length < 2) continue; // skip if all samples were inside intersection
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

      // ─── Multi-selection highlight (blue, for intersection creation) ──
      if (selectedRoadIdsRef.current.includes(road.id)) {
        const msHalf = halfWidth + 2;
        const msPolygon = buildRoadPolygon(samples, msHalf);
        const msSrcId = `rd-msel-src-${road.id}`;
        map.addSource(msSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: msPolygon } },
        });
        map.addLayer({
          id: `rd-msel-${road.id}`,
          type: 'line',
          source: msSrcId,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: {
            'line-color': '#3b82f6',
            'line-width': 3,
            'line-opacity': 0.9,
          },
        });
      }
    }

    // ─── Intersection polygons (asphalt fill + crosswalk markings) ───
    const intersections = intersectionsRef.current;
    for (const ix of intersections) {
      if (ix.connections.length < 2) continue;

      // Compute intersection polygon from cached road samples
      const polyLocal = computeIntersectionPolygonFromCache(ix, roads);
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

        const samples = getCachedSamples(road.id);
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

    // ─── Generated intersections (full algorithm: polygon + stop lines + crosswalks + lane paths) ───
    for (let gi = 0; gi < genIntersections.length; gi++) {
      const gen = genIntersections[gi];

      // 1. Intersection surface polygon
      if (gen.polygon.length >= 3) {
        const polyCoords: [number, number][] = gen.polygon.map((p) => {
          const geo = localToGeo(p.x, p.y, refLat, refLon);
          return [geo.lon, geo.lat];
        });
        polyCoords.push(polyCoords[0]); // close ring

        const giSrcId = `gi-src-${gi}`;
        map.addSource(giSrcId, {
          type: 'geojson',
          data: {
            type: 'Feature',
            properties: { genIdx: gi },
            geometry: { type: 'Polygon', coordinates: [polyCoords] },
          },
        });

        // Intersection surface (asphalt)
        map.addLayer({
          id: `gi-surface-${gi}`,
          type: 'fill',
          source: giSrcId,
          paint: { 'fill-color': '#383838', 'fill-opacity': 0.95 },
        });

        // Intersection outline (white)
        map.addLayer({
          id: `gi-outline-${gi}`,
          type: 'line',
          source: giSrcId,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': '#ffffff', 'line-width': 1.5, 'line-opacity': 0.5 },
        });
      }

      // 2. Stop lines (white, thick, perpendicular to each approach)
      for (const sl of gen.stopLines) {
        const p1Geo = localToGeo(sl.p1.x, sl.p1.y, refLat, refLon);
        const p2Geo = localToGeo(sl.p2.x, sl.p2.y, refLat, refLon);
        const slSrcId = `gi-sl-src-${gi}-${sl.approach}`;
        map.addSource(slSrcId, {
          type: 'geojson',
          data: {
            type: 'Feature',
            properties: {},
            geometry: { type: 'LineString', coordinates: [[p1Geo.lon, p1Geo.lat], [p2Geo.lon, p2Geo.lat]] },
          },
        });
        map.addLayer({
          id: `gi-sl-${gi}-${sl.approach}`,
          type: 'line',
          source: slSrcId,
          layout: { 'line-cap': 'round' },
          paint: { 'line-color': '#ffffff', 'line-width': 3, 'line-opacity': 0.9 },
        });
      }

      // 3. Crosswalks (white striped rectangles)
      for (const cw of gen.crosswalks) {
        const cwCoords: [number, number][] = cw.corners.map((c) => {
          const geo = localToGeo(c.x, c.y, refLat, refLon);
          return [geo.lon, geo.lat];
        });
        cwCoords.push(cwCoords[0]); // close ring

        const cwSrcId = `gi-cw-src-${gi}-${cw.approach}`;
        map.addSource(cwSrcId, {
          type: 'geojson',
          data: {
            type: 'Feature',
            properties: {},
            geometry: { type: 'Polygon', coordinates: [cwCoords] },
          },
        });
        // Crosswalk outline
        map.addLayer({
          id: `gi-cw-${gi}-${cw.approach}`,
          type: 'line',
          source: cwSrcId,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': '#ffffff', 'line-width': 2, 'line-opacity': 0.7 },
        });
        // Crosswalk fill (semi-transparent white)
        map.addLayer({
          id: `gi-cw-fill-${gi}-${cw.approach}`,
          type: 'fill',
          source: cwSrcId,
          paint: { 'fill-color': '#ffffff', 'fill-opacity': 0.15 },
        });
      }

      // 4. Lane connection paths (colored by turn type)
      const turnColors: Record<string, string> = {
        straight: '#4ecca3', // green
        left: '#ffaa00',     // orange
        right: '#ff6b6b',    // red
      };
      for (let li = 0; li < gen.laneConnections.length; li++) {
        const lc = gen.laneConnections[li];
        if (lc.path.length < 2) continue;
        const pathCoords: [number, number][] = lc.path.map((p) => {
          const geo = localToGeo(p.x, p.y, refLat, refLon);
          return [geo.lon, geo.lat];
        });
        const lcSrcId = `gi-lc-src-${gi}-${li}`;
        map.addSource(lcSrcId, {
          type: 'geojson',
          data: {
            type: 'Feature',
            properties: { type: lc.type },
            geometry: { type: 'LineString', coordinates: pathCoords },
          },
        });
        map.addLayer({
          id: `gi-lc-${gi}-${li}`,
          type: 'line',
          source: lcSrcId,
          layout: { 'line-cap': 'round' },
          paint: {
            'line-color': turnColors[lc.type] || '#ffffff',
            'line-width': 1.5,
            'line-opacity': 0.4,
            'line-dasharray': [2, 1],
          },
        });
      }

      // 5. Intersection center marker
      const centerGeo = localToGeo(gen.center.x, gen.center.y, refLat, refLon);
      const giCenterSrc = `gi-center-src-${gi}`;
      map.addSource(giCenterSrc, {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: {},
          geometry: { type: 'Point', coordinates: [centerGeo.lon, centerGeo.lat] },
        },
      });
      map.addLayer({
        id: `gi-center-${gi}`,
        type: 'circle',
        source: giCenterSrc,
        paint: {
          'circle-radius': 5,
          'circle-color': '#ff6b6b',
          'circle-stroke-color': '#ffffff',
          'circle-stroke-width': 2,
        },
      });
    }

    // ─── Arc preview (while drawing with arc tool) ─────────────
    const arcPreview = useRoadStudioStore.getState().arcPreview;
    if (arcPreview && arcPreview.points && arcPreview.points.length >= 2) {
      const arcCoords: [number, number][] = arcPreview.points.map((p) => {
        const geo = localToGeo(p.x, p.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });

      // Arc centerline preview (dashed green)
      const arcSrcId = 'arc-preview-src';
      map.addSource(arcSrcId, {
        type: 'geojson',
        data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: arcCoords } },
      });
      map.addLayer({
        id: 'arc-preview-line',
        type: 'line',
        source: arcSrcId,
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#4ecca3',
          'line-width': 4,
          'line-opacity': 0.6,
          'line-dasharray': [3, 2],
        },
      });

      // Arc road surface preview (semi-transparent fill)
      const halfW = useRoadStudioStore.getState().defaultWidth / 2;
      const leftEdge: [number, number][] = [];
      const rightEdge: [number, number][] = [];
      for (let i = 0; i < arcPreview.points.length; i++) {
        const p = arcPreview.points[i];
        let tx: number, ty: number;
        if (i === 0) { tx = arcPreview.points[1].x - p.x; ty = arcPreview.points[1].y - p.y; }
        else if (i === arcPreview.points.length - 1) { tx = p.x - arcPreview.points[i - 1].x; ty = p.y - arcPreview.points[i - 1].y; }
        else { tx = arcPreview.points[i + 1].x - arcPreview.points[i - 1].x; ty = arcPreview.points[i + 1].y - arcPreview.points[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;
        const lGeo = localToGeo(p.x + nx * halfW, p.y + ny * halfW, refLat, refLon);
        const rGeo = localToGeo(p.x - nx * halfW, p.y - ny * halfW, refLat, refLon);
        leftEdge.push([lGeo.lon, lGeo.lat]);
        rightEdge.push([rGeo.lon, rGeo.lat]);
      }
      const arcPolygonCoords = [...leftEdge, ...rightEdge.reverse()];
      arcPolygonCoords.push(arcPolygonCoords[0]); // close ring

      const arcFillSrcId = 'arc-preview-fill-src';
      map.addSource(arcFillSrcId, {
        type: 'geojson',
        data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: [arcPolygonCoords] } },
      });
      map.addLayer({
        id: 'arc-preview-fill',
        type: 'fill',
        source: arcFillSrcId,
        paint: { 'fill-color': '#3a3a3a', 'fill-opacity': 0.5 },
      });

      // Show radius info at center
      if (isFinite(arcPreview.radius)) {
        const centerGeo = localToGeo(arcPreview.center.x, arcPreview.center.y, refLat, refLon);
        const rSrcId = 'arc-radius-src';
        map.addSource(rSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: { radius: Math.round(arcPreview.radius) }, geometry: { type: 'Point', coordinates: [centerGeo.lon, centerGeo.lat] } },
        });
        map.addLayer({
          id: 'arc-radius-marker',
          type: 'circle',
          source: rSrcId,
          paint: {
            'circle-radius': 4,
            'circle-color': '#ffaa00',
            'circle-stroke-color': '#ffffff',
            'circle-stroke-width': 1,
          },
        });
      }
    }

    // ─── Clothoid preview ─────────────────────────────────────
    const clothoidPreview = useRoadStudioStore.getState().clothoidPreview;
    if (clothoidPreview && clothoidPreview.length >= 2) {
      const clCoords: [number, number][] = clothoidPreview.map((p) => {
        const geo = localToGeo(p.x, p.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });
      map.addSource('clothoid-preview-src', {
        type: 'geojson',
        data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: clCoords } },
      });
      map.addLayer({
        id: 'clothoid-preview-line',
        type: 'line',
        source: 'clothoid-preview-src',
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: { 'line-color': '#ff9800', 'line-width': 4, 'line-opacity': 0.6, 'line-dasharray': [3, 2] },
      });
    }

    // ─── Polyline preview ─────────────────────────────────────
    const polylinePoints = useRoadStudioStore.getState().polylinePoints;
    const previewPoint = useRoadStudioStore.getState().previewPoint;
    if (polylinePoints && polylinePoints.length >= 1) {
      // Show collected points + line to cursor
      const pts = [...polylinePoints];
      if (previewPoint) pts.push(previewPoint);
      const plCoords: [number, number][] = pts.map((p) => {
        const geo = localToGeo(p.x, p.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });

      if (plCoords.length >= 2) {
        map.addSource('polyline-preview-src', {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: plCoords } },
        });
        map.addLayer({
          id: 'polyline-preview-line',
          type: 'line',
          source: 'polyline-preview-src',
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': '#2196f3', 'line-width': 3, 'line-opacity': 0.6, 'line-dasharray': [3, 2] },
        });
      }

      // Show collected points as markers
      for (let i = 0; i < polylinePoints.length; i++) {
        const geo = localToGeo(polylinePoints[i].x, polylinePoints[i].y, refLat, refLon);
        const ptSrcId = `polyline-pt-${i}-src`;
        map.addSource(ptSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Point', coordinates: [geo.lon, geo.lat] } },
        });
        map.addLayer({
          id: `polyline-pt-${i}`,
          type: 'circle',
          source: ptSrcId,
          paint: {
            'circle-radius': 5,
            'circle-color': '#2196f3',
            'circle-stroke-color': '#ffffff',
            'circle-stroke-width': 2,
          },
        });
      }
    }

    // ─── Spline preview ───────────────────────────────────────
    const splinePoints = useRoadStudioStore.getState().splinePoints;
    if (splinePoints && splinePoints.length >= 1) {
      const pts = [...splinePoints];
      if (previewPoint) pts.push(previewPoint);
      const spCoords: [number, number][] = pts.map((p) => {
        const geo = localToGeo(p.x, p.y, refLat, refLon);
        return [geo.lon, geo.lat];
      });

      if (spCoords.length >= 2) {
        map.addSource('spline-preview-src', {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: spCoords } },
        });
        map.addLayer({
          id: 'spline-preview-line',
          type: 'line',
          source: 'spline-preview-src',
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': '#9c27b0', 'line-width': 3, 'line-opacity': 0.6, 'line-dasharray': [3, 2] },
        });
      }

      for (let i = 0; i < splinePoints.length; i++) {
        const geo = localToGeo(splinePoints[i].x, splinePoints[i].y, refLat, refLon);
        const ptSrcId = `spline-pt-${i}-src`;
        map.addSource(ptSrcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Point', coordinates: [geo.lon, geo.lat] } },
        });
        map.addLayer({
          id: `spline-pt-${i}`,
          type: 'circle',
          source: ptSrcId,
          paint: {
            'circle-radius': 5,
            'circle-color': '#9c27b0',
            'circle-stroke-color': '#ffffff',
            'circle-stroke-width': 2,
          },
        });
      }
    }

    // ─── Line/Pen preview (line from last point to cursor) ───
    const drawingRoadId = useRoadStudioStore.getState().drawingRoadId;
    const currentTool = toolRef.current;
    if (drawingRoadId && previewPoint && (currentTool === 'line' || currentTool === 'pen')) {
      const road = roads.find((r) => r.id === drawingRoadId);
      if (road && road.points.length >= 1) {
        const lastPt = road.points[road.points.length - 1];
        const lastLocal = geoToLocal(lastPt.lat, lastPt.lon, refLat, refLon);
        const startGeo = localToGeo(lastLocal.x, lastLocal.y, refLat, refLon);
        const endGeo = localToGeo(previewPoint.x, previewPoint.y, refLat, refLon);
        map.addSource('line-preview-src', {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: [[startGeo.lon, startGeo.lat], [endGeo.lon, endGeo.lat]] } },
        });
        map.addLayer({
          id: 'line-preview',
          type: 'line',
          source: 'line-preview-src',
          layout: { 'line-cap': 'round' },
          paint: { 'line-color': '#4ecca3', 'line-width': 3, 'line-opacity': 0.5, 'line-dasharray': [3, 2] },
        });
      }
    }

    // ─── Geometry Debug Mode overlays ───────────────────────────
    if (debugMode) {
      const dbg = debugLayers;
      const dbgPrefix = 'dbg-';

      // Helper to add a debug line layer
      const addDbgLine = (id: string, coords: [number, number][], color: string, width = 2, opacity = 0.8) => {
        const srcId = `${dbgPrefix}${id}-src`;
        map.addSource(srcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: coords } },
        });
        map.addLayer({
          id: `${dbgPrefix}${id}`,
          type: 'line',
          source: srcId,
          layout: { 'line-cap': 'round', 'line-join': 'round' },
          paint: { 'line-color': color, 'line-width': width, 'line-opacity': opacity },
        });
      };

      // Helper to add debug circle markers
      const addDbgCircles = (id: string, points: Array<{ x: number; y: number }>, color: string, radius = 4) => {
        const features = points.map((p) => {
          const geo = localToGeo(p.x, p.y, refLat, refLon);
          return { type: 'Feature', properties: {}, geometry: { type: 'Point', coordinates: [geo.lon, geo.lat] } };
        });
        const srcId = `${dbgPrefix}${id}-src`;
        map.addSource(srcId, { type: 'geojson', data: { type: 'FeatureCollection', features } });
        map.addLayer({
          id: `${dbgPrefix}${id}`,
          type: 'circle',
          source: srcId,
          paint: { 'circle-radius': radius, 'circle-color': color, 'circle-stroke-color': '#ffffff', 'circle-stroke-width': 1 },
        });
      };

      // Helper to add debug polygon fill
      const addDbgPolygon = (id: string, polygon: Array<{ x: number; y: number }>, color: string, opacity = 0.3) => {
        if (polygon.length < 3) return;
        const coords: [number, number][] = polygon.map((p) => {
          const geo = localToGeo(p.x, p.y, refLat, refLon);
          return [geo.lon, geo.lat];
        });
        coords.push(coords[0]); // close ring
        const srcId = `${dbgPrefix}${id}-src`;
        map.addSource(srcId, {
          type: 'geojson',
          data: { type: 'Feature', properties: {}, geometry: { type: 'Polygon', coordinates: [coords] } },
        });
        map.addLayer({
          id: `${dbgPrefix}${id}`,
          type: 'fill',
          source: srcId,
          paint: { 'fill-color': color, 'fill-opacity': opacity },
        });
        // Also add outline
        map.addLayer({
          id: `${dbgPrefix}${id}-outline`,
          type: 'line',
          source: srcId,
          paint: { 'line-color': color, 'line-width': 2, 'line-opacity': 0.8 },
        });
      };

      // Render debug overlays for each road
      for (const road of roads) {
        const samples = getCachedSamples(road.id);
        if (samples.length < 2) continue;

        const halfW = road.width / 2;

        // Centerline (cyan)
        if (dbg.centerline) {
          const coords = samples.map((s) => {
            const geo = localToGeo(s.x, s.y, refLat, refLon);
            return [geo.lon, geo.lat] as [number, number];
          });
          addDbgLine(`cl-${road.id}`, coords, '#00ffff', 2, 0.9);
        }

        // Sample points (yellow dots)
        if (dbg.samplePoints) {
          addDbgCircles(`samples-${road.id}`, samples, '#ffff00', 3);
        }

        // Left edge (green)
        if (dbg.leftEdge) {
          const leftEdge: [number, number][] = [];
          for (let i = 0; i < samples.length; i++) {
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - samples[0].x; ty = samples[1].y - samples[0].y; }
            else if (i === samples.length - 1) { tx = samples[i].x - samples[i - 1].x; ty = samples[i].y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len, ny = tx / len;
            const geo = localToGeo(samples[i].x + nx * halfW, samples[i].y + ny * halfW, refLat, refLon);
            leftEdge.push([geo.lon, geo.lat]);
          }
          addDbgLine(`le-${road.id}`, leftEdge, '#00ff00', 2, 0.8);
        }

        // Right edge (red)
        if (dbg.rightEdge) {
          const rightEdge: [number, number][] = [];
          for (let i = 0; i < samples.length; i++) {
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - samples[0].x; ty = samples[1].y - samples[0].y; }
            else if (i === samples.length - 1) { tx = samples[i].x - samples[i - 1].x; ty = samples[i].y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len, ny = tx / len;
            const geo = localToGeo(samples[i].x - nx * halfW, samples[i].y - ny * halfW, refLat, refLon);
            rightEdge.push([geo.lon, geo.lat]);
          }
          addDbgLine(`re-${road.id}`, rightEdge, '#ff0000', 2, 0.8);
        }

        // Lane boundaries (orange)
        if (dbg.laneBoundaries && road.laneCount > 1) {
          const laneWidth = road.width / road.laneCount;
          const numBoundaries = road.laneCount - 1;
          for (let b = 0; b < numBoundaries; b++) {
            const offset = (b - (numBoundaries - 1) / 2.0) * laneWidth;
            const boundaryCoords: [number, number][] = [];
            for (let i = 0; i < samples.length; i++) {
              let tx: number, ty: number;
              if (i === 0) { tx = samples[1].x - samples[0].x; ty = samples[1].y - samples[0].y; }
              else if (i === samples.length - 1) { tx = samples[i].x - samples[i - 1].x; ty = samples[i].y - samples[i - 1].y; }
              else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
              const len = Math.sqrt(tx * tx + ty * ty) || 1;
              const nx = -ty / len, ny = tx / len;
              const geo = localToGeo(samples[i].x + nx * offset, samples[i].y + ny * offset, refLat, refLon);
              boundaryCoords.push([geo.lon, geo.lat]);
            }
            addDbgLine(`lb-${road.id}-${b}`, boundaryCoords, '#ff8800', 1, 0.6);
          }
        }

        // Road polygon (semi-transparent blue fill)
        if (dbg.roadPolygon) {
          const leftEdge: Array<{ x: number; y: number }> = [];
          const rightEdge: Array<{ x: number; y: number }> = [];
          for (let i = 0; i < samples.length; i++) {
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - samples[0].x; ty = samples[1].y - samples[0].y; }
            else if (i === samples.length - 1) { tx = samples[i].x - samples[i - 1].x; ty = samples[i].y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len, ny = tx / len;
            leftEdge.push({ x: samples[i].x + nx * halfW, y: samples[i].y + ny * halfW });
            rightEdge.push({ x: samples[i].x - nx * halfW, y: samples[i].y - ny * halfW });
          }
          const polygon = [...leftEdge, ...rightEdge.reverse()];
          addDbgPolygon(`rp-${road.id}`, polygon, '#0088ff', 0.2);
        }
      }

      // Intersection debug overlays
      for (const gen of genIntersections) {
        const ixId = `${gen.center.x.toFixed(0)}-${gen.center.y.toFixed(0)}`;

        // Intersection polygon (magenta fill + outline)
        if (dbg.intersectionPolygon && gen.polygon.length >= 3) {
          addDbgPolygon(`ix-poly-${ixId}`, gen.polygon, '#ff00ff', 0.2);
          // Polygon vertices (small white dots with numbering)
          addDbgCircles(`ix-verts-${ixId}`, gen.polygon, '#ffffff', 3);
        }

        // Trim lines (red lines across each road at trim distance)
        if (dbg.trimPoints && gen.trimLines) {
          for (const tl of gen.trimLines) {
            const leftGeo = localToGeo(tl.leftEnd.x, tl.leftEnd.y, refLat, refLon);
            const rightGeo = localToGeo(tl.rightEnd.x, tl.rightEnd.y, refLat, refLon);
            addDbgLine(`ix-trimline-${ixId}-${tl.approachIdx}`,
              [[leftGeo.lon, leftGeo.lat], [rightGeo.lon, rightGeo.lat]],
              '#ff0000', 2, 0.6);
          }
          // Trim center points (large red circles)
          const trimCenters = gen.trimLines.map((tl) => tl.centerPt);
          addDbgCircles(`ix-trim-${ixId}`, trimCenters, '#ff0000', 6);
        }

        // Road edges through intersection (green = left, red = right)
        if (dbg.leftEdge || dbg.rightEdge) {
          for (const approach of gen.approaches) {
            if (approach.centerline.length >= 2) {
              const p0 = approach.centerline[0];
              const p1 = approach.centerline[approach.centerline.length - 1];
              const tx = p1.x - p0.x, ty = p1.y - p0.y;
              const len = Math.sqrt(tx * tx + ty * ty) || 1;
              const nx = -ty / len, ny = tx / len;
              const halfW = approach.width / 2;

              if (dbg.leftEdge) {
                const startGeo = localToGeo(p0.x + nx * halfW, p0.y + ny * halfW, refLat, refLon);
                const endGeo = localToGeo(p1.x + nx * halfW, p1.y + ny * halfW, refLat, refLon);
                addDbgLine(`ix-le-${ixId}-${approach.direction}`,
                  [[startGeo.lon, startGeo.lat], [endGeo.lon, endGeo.lat]],
                  '#00ff00', 2, 0.7);
              }
              if (dbg.rightEdge) {
                const startGeo = localToGeo(p0.x - nx * halfW, p0.y - ny * halfW, refLat, refLon);
                const endGeo = localToGeo(p1.x - nx * halfW, p1.y - ny * halfW, refLat, refLon);
                addDbgLine(`ix-re-${ixId}-${approach.direction}`,
                  [[startGeo.lon, startGeo.lat], [endGeo.lon, endGeo.lat]],
                  '#ff0000', 2, 0.7);
              }
            }
          }
        }

        // Boundary intersections (orange X markers)
        if (dbg.tangentPoints && gen.boundaryIntersections) {
          addDbgCircles(`ix-bint-${ixId}`, gen.boundaryIntersections, '#ff8800', 6);
        }

        // Fillet corners: tangent points, arc centers, arcs, radius lines
        if (dbg.filletArcs && gen.corners) {
          for (let ci = 0; ci < gen.corners.length; ci++) {
            const corner = gen.corners[ci];
            if (!isFinite(corner.boundaryIntersection.x)) continue;

            // Fillet arc (magenta curve)
            if (corner.arcPoints && corner.arcPoints.length >= 2) {
              const arcCoords = corner.arcPoints.map((p) => {
                const geo = localToGeo(p.x, p.y, refLat, refLon);
                return [geo.lon, geo.lat] as [number, number];
              });
              addDbgLine(`ix-farc-${ixId}-${ci}`, arcCoords, '#ff00ff', 3, 0.8);
            }

            // Tangent points (cyan diamonds)
            const tanPts = [corner.tangentIn, corner.tangentOut].filter(p => isFinite(p.x));
            if (tanPts.length > 0) {
              addDbgCircles(`ix-tp-${ixId}-${ci}`, tanPts, '#00ffff', 5);
            }

            // Arc center (orange circle)
            if (isFinite(corner.arcCenter.x)) {
              addDbgCircles(`ix-ac-${ixId}-${ci}`, [corner.arcCenter], '#ffaa00', 4);
            }

            // Radius line (from arc center to tangentIn)
            if (isFinite(corner.arcCenter.x) && isFinite(corner.tangentIn.x)) {
              const cGeo = localToGeo(corner.arcCenter.x, corner.arcCenter.y, refLat, refLon);
              const tGeo = localToGeo(corner.tangentIn.x, corner.tangentIn.y, refLat, refLon);
              addDbgLine(`ix-rad-${ixId}-${ci}`,
                [[cGeo.lon, cGeo.lat], [tGeo.lon, tGeo.lat]],
                '#ffaa00', 1, 0.5);
            }
          }
        }

        // Approach centerlines (orange dashed)
        if (dbg.filletArcs) {
          for (const approach of gen.approaches) {
            if (approach.centerline.length >= 2) {
              const coords = approach.centerline.map((p) => {
                const geo = localToGeo(p.x, p.y, refLat, refLon);
                return [geo.lon, geo.lat] as [number, number];
              });
              addDbgLine(`ix-cl-${ixId}-${approach.direction}`, coords, '#ff8800', 1, 0.5);
            }
          }
        }

        // Triangulation (white wireframe)
        if (dbg.triangulation && gen.polygon.length >= 3) {
          const coords: [number, number][] = [];
          for (let i = 1; i < gen.polygon.length - 1; i++) {
            const g0 = localToGeo(gen.polygon[0].x, gen.polygon[0].y, refLat, refLon);
            const g1 = localToGeo(gen.polygon[i].x, gen.polygon[i].y, refLat, refLon);
            const g2 = localToGeo(gen.polygon[i + 1].x, gen.polygon[i + 1].y, refLat, refLon);
            coords.push([g0.lon, g0.lat], [g1.lon, g1.lat], [g2.lon, g2.lat], [g0.lon, g0.lat]);
          }
          if (coords.length >= 2) {
            addDbgLine(`ix-tri-${ixId}`, coords, '#ffffff', 1, 0.4);
          }
        }
      }
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

      // NOTE: cp-dot click handler is registered globally in map init.
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
        // Also try picking road meshes (for shift+click intersection selection)
        const roadPick = scene.pick(scene.pointerX, scene.pointerY, (m) => m.name?.startsWith('road_'));
        lastX = evt.clientX; lastY = evt.clientY;

        if (button === 1) { isMiddleDown = true; dragStateRef.current.mode = 'rotate'; }
        else if (button === 2) { dragStateRef.current.mode = 'pan'; }
        else if (button === 0) {
          const st3d = useRoadStudioStore.getState();
          const isShift3d = shiftDownRef.current || (evt as any).shiftKey || st3d.roadPickMode;
          // Shift+click or pick mode on road mesh = toggle road selection (for intersection)
          if (isShift3d && roadPick?.hit && roadPick.pickedMesh) {
            const roadName = roadPick.pickedMesh.name; // "road_<id>"
            const roadId = roadName.replace('road_', '');
            st3d.toggleRoadSelection(roadId);
          } else if ((evt as any).shiftKey) {
            // Shift+click but missed road mesh — try closest road by distance
          } else if (isShift3d) {
            const pickInfo = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);
            if (pickInfo?.hit && pickInfo.pickedPoint) {
              const px = pickInfo.pickedPoint.x;
              const py = pickInfo.pickedPoint.z;
              let closestId: string | null = null;
              let closestDist = Infinity;
              for (const road of roadsRef.current) {
                if (road.points.length < 2) continue;
                const samples = getCachedSamples(road.id);
                for (const s of samples) {
                  const d = Math.sqrt((s.x - px) ** 2 + (s.y - py) ** 2);
                  if (d < closestDist) { closestDist = d; closestId = road.id; }
                }
              }
              if (closestId && closestDist < 50) {
                st3d.toggleRoadSelection(closestId);
              }
            }
          } else if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'control-point') {
            const meta = pickResult.pickedMesh.metadata;
            dragStateRef.current = { mode: 'move-point', roadId: meta.roadId, pointIndex: meta.pointIndex, handle: null };
            st3d.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: null });
          } else if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'handle') {
            const meta = pickResult.pickedMesh.metadata;
            dragStateRef.current = { mode: 'move-handle', roadId: meta.roadId, pointIndex: meta.pointIndex, handle: meta.handle };
            st3d.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: meta.handle });
          } else {
            st3d.setSelection({ roadId: null, pointIndices: [], handle: null });
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
          useRoadStudioStore.getState().updateControlPoint(ds.roadId!, ds.pointIndex, geo.lat, geo.lon);
        } else if (ds.mode === 'move-handle' && gp?.hit) {
          const w = gp.pickedPoint!;
          const road = roadsRef.current.find((r) => r.id === ds.roadId);
          if (road) {
            const pt = road.points[ds.pointIndex];
            if (pt) {
              const pl = geoToLocal(pt.lat, pt.lon, refLatRef.current, refLonRef.current);
              const off = localToGeo(w.x - pl.x, w.z - pl.y, 0, 0);
              useRoadStudioStore.getState().setHandle(ds.roadId!, ds.pointIndex, ds.handle!, { lat: off.lat, lon: off.lon });
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

  // ─── Refresh sample cache from C++ engine ───────────────
  // Also refreshes the build cache (Phase 2.8 — full lane engine pipeline)
  async function refreshSampleCache() {
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;
    const newCache = new Map<string, Array<{ x: number; y: number; z: number }>>();
    const newBuildCache = new Map<string, RoadBuildResult>();
    for (const road of roadsRef.current) {
      if (road.points.length < 2) continue;
      try {
        // Phase 1: centerline samples (for 2D map view)
        const samples = await roadEngine.sampleCenterline(road, refLat, refLon, 24);
        newCache.set(road.id, samples);
      } catch (err) {
        console.error('[RoadViewport] Failed to sample road', road.id, err);
      }
      try {
        // Phase 2.8: full build (for 3D mesh view)
        const buildResult = await roadEngine.buildRoad(road, refLat, refLon);
        newBuildCache.set(road.id, buildResult);
      } catch (err) {
        console.error('[RoadViewport] Failed to build road', road.id, err);
      }
    }
    sampleCacheRef.current = newCache;
    buildCacheRef.current = newBuildCache;
    updateAllViews();
  }

  /** Get cached road samples (sync) or empty array */
  function getCachedSamples(roadId: string): Array<{ x: number; y: number; z: number }> {
    return sampleCacheRef.current.get(roadId) ?? [];
  }

  /** Get cached build result (sync) or null */
  function getCachedBuild(roadId: string): RoadBuildResult | null {
    return buildCacheRef.current.get(roadId) ?? null;
  }

  /** Compute intersection polygon from cached road samples (replaces TS computeIntersectionPolygon) */
  function computeIntersectionPolygonFromCache(
    ix: Intersection,
    roads: Road[]
  ): Array<{ x: number; y: number }> {
    const ixLocal = geoToLocal(ix.lat, ix.lon, refLatRef.current, refLonRef.current);
    const points: Array<{ x: number; y: number }> = [];

    for (const conn of ix.connections) {
      const road = roads.find((r) => r.id === conn.roadId);
      if (!road || road.points.length < 2) continue;

      const samples = getCachedSamples(road.id);
      if (samples.length < 2) continue;
      const halfW = road.width / 2;

      // Find closest sample to intersection
      let closestIdx = 0;
      let closestDist = Infinity;
      for (let i = 0; i < samples.length; i++) {
        const d = Math.sqrt((samples[i].x - ixLocal.x) ** 2 + (samples[i].y - ixLocal.y) ** 2);
        if (d < closestDist) { closestDist = d; closestIdx = i; }
      }

      // Take samples around closest point and compute edge points
      const range = 3;
      for (let i = Math.max(0, closestIdx - range); i <= Math.min(samples.length - 1, closestIdx + range); i++) {
        const s = samples[i];
        let tx: number, ty: number;
        if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
        else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;
        points.push({ x: s.x + nx * halfW, y: s.y + ny * halfW });
        points.push({ x: s.x - nx * halfW, y: s.y - ny * halfW });
      }
    }

    // Simple convex hull (Andrew's monotone chain)
    if (points.length < 3) return points;
    const sorted = [...points].sort((a, b) => a.x - b.x || a.y - b.y);
    const cross = (o: typeof sorted[0], a: typeof sorted[0], b: typeof sorted[0]) =>
      (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    const lower: typeof sorted = [];
    for (const p of sorted) {
      while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop();
      lower.push(p);
    }
    const upper: typeof sorted = [];
    for (let i = sorted.length - 1; i >= 0; i--) {
      const p = sorted[i];
      while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop();
      upper.push(p);
    }
    return lower.slice(0, -1).concat(upper.slice(0, -1));
  }

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

      const samples = getCachedSamples(road.id);
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

      // ─── Phase 2.8: Upload C++ buildRoad() mesh sections to Babylon ──
      // The C++ engine generates typed arrays (Float32Array, Uint32Array)
      // that can be uploaded directly to GPU without per-vertex TS processing.
      const buildResult = getCachedBuild(road.id);
      if (buildResult && buildResult.meshSections.length > 0) {
        for (const sec of buildResult.meshSections) {
          // Swap Y/Z for Babylon (C++ uses Z-up, Babylon uses Y-up)
          // C++ position (x, y, z) → Babylon (x, z, y)
          const babylonPositions = new Float32Array(sec.positions.length);
          for (let i = 0; i < sec.vertexCount; i++) {
            babylonPositions[i * 3]     = sec.positions[i * 3];       // x
            babylonPositions[i * 3 + 1] = sec.positions[i * 3 + 2];   // z → Babylon Y
            babylonPositions[i * 3 + 2] = sec.positions[i * 3 + 1];   // y → Babylon Z
          }
          const babylonNormals = new Float32Array(sec.normals.length);
          for (let i = 0; i < sec.vertexCount; i++) {
            babylonNormals[i * 3]     = sec.normals[i * 3];       // nx
            babylonNormals[i * 3 + 1] = sec.normals[i * 3 + 2];   // nz → Babylon Y
            babylonNormals[i * 3 + 2] = sec.normals[i * 3 + 1];   // ny → Babylon Z
          }

          const mesh = new Mesh(`${sec.material}_${road.id}`, scene);
          const vertexData = new VertexData();
          vertexData.positions = babylonPositions;
          vertexData.indices = sec.indices;
          vertexData.normals = babylonNormals;
          vertexData.uvs = sec.uvs;
          vertexData.applyToMesh(mesh, true);

          // Apply material based on section type
          const mat = new StandardMaterial(`${sec.material}_mat_${road.id}`, scene);
          mat.backFaceCulling = false;

          if (sec.material === 'asphalt') {
            const tex = getTexture(road.profile?.surfaceTexture || 'asphalt', scene);
            if (tex) {
              mat.diffuseTexture = tex;
              mat.diffuseColor = new Color3(1, 1, 1);
              tex.wrapU = Texture.WRAP_ADDRESSMODE;
              tex.wrapV = Texture.WRAP_ADDRESSMODE;
            } else {
              mat.diffuseColor = new Color3(0.23, 0.23, 0.23);
            }
            mat.emissiveColor = new Color3(0.15, 0.15, 0.15);
            mat.specularColor = new Color3(0.05, 0.05, 0.05);
            mesh.isPickable = true; // Enable picking for shift+click road selection
            roadMeshesRef.current.set(road.id, mesh);
          } else if (sec.material === 'yellow_marking') {
            mat.diffuseColor = new Color3(1, 0.85, 0.2);
            mat.emissiveColor = new Color3(0.15, 0.13, 0.03);
            mat.specularColor = new Color3(0, 0, 0);
            mesh.isPickable = false;
            roadMeshesRef.current.set(`yellow_${road.id}`, mesh);
          } else if (sec.material === 'white_marking') {
            mat.diffuseColor = new Color3(1, 1, 1);
            mat.emissiveColor = new Color3(0.12, 0.12, 0.12);
            mat.specularColor = new Color3(0, 0, 0);
            mesh.isPickable = false;
            roadMeshesRef.current.set(`white_${road.id}`, mesh);
          } else {
            mat.diffuseColor = new Color3(0.5, 0.5, 0.5);
            mesh.isPickable = false;
            roadMeshesRef.current.set(`${sec.material}_${road.id}`, mesh);
          }

          mesh.material = mat;
        }
      } else {
        // ─── Fallback: TS-generated road surface + markings ──
        // Used when buildRoad() is not available (e.g. addon not loaded)

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
        roadMesh.isPickable = true;
        roadMeshesRef.current.set(road.id, roadMesh);

        // ─── Center line marking (dashed yellow) ──────────────────
        buildDashedMarkingTS(
          `center_${road.id}`, samples, 0, normalAt,
          3, 3, 0.15, 0.05,
          new Color3(1, 0.85, 0.2), scene, buildStripMesh, roadMeshesRef
        );

        // ─── Lane divider markings (dashed white) ─────────────────
        if (road.laneCount > 1) {
          const laneSpacing = road.width / road.laneCount;
          for (let lane = 1; lane < road.laneCount; lane++) {
            const offset = -halfW + laneSpacing * lane;
            buildDashedMarkingTS(
              `div_${road.id}_${lane}`, samples, offset, normalAt,
              3, 3, 0.12, 0.04,
              new Color3(1, 1, 1), scene, buildStripMesh, roadMeshesRef
            );
          }
        }

        // ─── Edge lines (solid white at road edges) ───────────────
        for (const side of [-1, 1]) {
          const edgeOffset = halfW * side - (0.1 * side);
          buildDashedMarkingTS(
            `edge_${road.id}_${side}`, samples, edgeOffset, normalAt,
            9999, 0, 0.1, 0.04,
            new Color3(1, 1, 1), scene, buildStripMesh, roadMeshesRef
          );
        }
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

    // ─── Phase 2.8: Lane engine debug overlays (3D) ──────────────
    const dbg3d = debugLayersRef.current;
    if (dbg3d.laneCenters || dbg3d.laneBoundaryLines || dbg3d.laneIds || dbg3d.meshWireframe) {
      for (const road of roads) {
        const build = getCachedBuild(road.id);
        if (!build) continue;

        // Lane centerlines (cyan lines)
        if (dbg3d.laneCenters) {
          for (const cl of build.lanes.centerlines) {
            if (cl.samples.length < 2) continue;
            const pts: Vector3[] = cl.samples.map((s) =>
              new Vector3(s.position.x, 0.15, s.position.y));
            const line = MeshBuilder.CreateLines(`dbg_lc_${road.id}_${cl.laneId}`, { points: pts }, scene);
            line.color = cl.laneId === 0 ? Color3.Yellow() : (cl.laneId > 0 ? Color3.Green() : Color3.Red());
            line.isPickable = false;
            roadMeshesRef.current.set(`dbg_lc_${road.id}_${cl.laneId}`, line);
          }
        }

        // Lane boundaries (orange lines, road edges in white)
        if (dbg3d.laneBoundaryLines) {
          for (const b of build.lanes.boundaries) {
            if (b.samples.length < 2) continue;
            const pts: Vector3[] = b.samples.map((s) =>
              new Vector3(s.position.x, 0.12, s.position.y));
            const line = MeshBuilder.CreateLines(`dbg_lb_${road.id}_${b.innerLaneId}_${b.outerLaneId}`, { points: pts }, scene);
            line.color = b.isRoadEdge ? Color3.White() : Color3.Teal();
            line.isPickable = false;
            roadMeshesRef.current.set(`dbg_lb_${road.id}_${b.innerLaneId}_${b.outerLaneId}`, line);
          }
        }

        // Lane IDs (colored spheres at centerline midpoint)
        if (dbg3d.laneIds) {
          for (const cl of build.lanes.centerlines) {
            if (cl.samples.length < 2) continue;
            const mid = cl.samples[Math.floor(cl.samples.length / 2)];
            const sphere = MeshBuilder.CreateSphere(`dbg_lid_${road.id}_${cl.laneId}`, { diameter: 1.5, segments: 8 }, scene);
            sphere.position = new Vector3(mid.position.x, 0.3, mid.position.y);
            const mat = new StandardMaterial(`dbg_lid_mat_${road.id}_${cl.laneId}`, scene);
            mat.diffuseColor = cl.laneId === 0 ? Color3.Yellow() : (cl.laneId > 0 ? Color3.Green() : Color3.Red());
            mat.emissiveColor = mat.diffuseColor.scale(0.5);
            sphere.material = mat;
            sphere.isPickable = false;
            roadMeshesRef.current.set(`dbg_lid_${road.id}_${cl.laneId}`, sphere);
          }
        }

        // Mesh wireframe (overlay on the asphalt mesh)
        if (dbg3d.meshWireframe) {
          for (const sec of build.meshSections) {
            if (sec.material !== 'asphalt') continue;
            // Build wireframe from indices
            const wirePts: Vector3[] = [];
            for (let i = 0; i < sec.indexCount; i += 3) {
              const i0 = sec.indices[i], i1 = sec.indices[i + 1], i2 = sec.indices[i + 2];
              const p0 = new Vector3(sec.positions[i0 * 3], sec.positions[i0 * 3 + 2] + 0.05, sec.positions[i0 * 3 + 1]);
              const p1 = new Vector3(sec.positions[i1 * 3], sec.positions[i1 * 3 + 2] + 0.05, sec.positions[i1 * 3 + 1]);
              const p2 = new Vector3(sec.positions[i2 * 3], sec.positions[i2 * 3 + 2] + 0.05, sec.positions[i2 * 3 + 1]);
              wirePts.push(p0, p1, p1, p2, p2, p0);
            }
            if (wirePts.length >= 2) {
              const wf = MeshBuilder.CreateLines(`dbg_wf_${road.id}`, { points: wirePts }, scene);
              wf.color = Color3.Magenta();
              wf.isPickable = false;
              roadMeshesRef.current.set(`dbg_wf_${road.id}`, wf);
            }
          }
        }
      }
    }

    // ─── Intersection surfaces in 3D ───────────────────────────
    const intersections = intersectionsRef.current;
    for (const ix of intersections) {
      if (ix.connections.length < 2) continue;

      const polyLocal = computeIntersectionPolygonFromCache(ix, roads);
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

        const samples = getCachedSamples(road.id);
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

    // ─── Generated intersections in 3D (full algorithm) ────────
    const genIntersections = generatedIntersectionsRef.current;
    for (let gi = 0; gi < genIntersections.length; gi++) {
      const gen = genIntersections[gi];
      const zHeight = (gen.approaches[0]?.z ?? 0) + 0.03;

      // 1. Intersection surface mesh (filled polygon)
      if (gen.polygon.length >= 3) {
        const positions: number[] = [];
        const indices: number[] = [];
        const uvs: number[] = [];

        // Center vertex
        positions.push(gen.center.x, zHeight, gen.center.y);
        uvs.push(0.5, 0.5);

        // Boundary vertices
        for (let i = 0; i < gen.polygon.length; i++) {
          positions.push(gen.polygon[i].x, zHeight, gen.polygon[i].y);
          const dx = gen.polygon[i].x - gen.center.x;
          const dy = gen.polygon[i].y - gen.center.y;
          uvs.push(0.5 + dx / 20, 0.5 + dy / 20);
        }

        // Fan triangulation
        for (let i = 0; i < gen.polygon.length; i++) {
          const next = (i + 1) % gen.polygon.length;
          indices.push(0, i + 1, next + 1);
        }

        const giMesh = new Mesh(`gi_surface_${gi}`, scene);
        const giVd = new VertexData();
        giVd.positions = positions;
        giVd.indices = indices;
        giVd.uvs = uvs;
        const giNormals: number[] = [];
        VertexData.ComputeNormals(positions, indices, giNormals);
        giVd.normals = giNormals;
        giVd.applyToMesh(giMesh, true);

        const giMat = new StandardMaterial(`gi_mat_${gi}`, scene);
        giMat.backFaceCulling = false;
        const giTex = getTexture('asphalt', scene);
        if (giTex) {
          giMat.diffuseTexture = giTex;
          giMat.diffuseColor = new Color3(0.9, 0.9, 0.9);
        } else {
          giMat.diffuseColor = new Color3(0.22, 0.22, 0.22);
        }
        giMat.emissiveColor = new Color3(0.12, 0.12, 0.12);
        giMat.specularColor = new Color3(0.05, 0.05, 0.05);
        giMesh.material = giMat;
        giMesh.isPickable = false;
        roadMeshesRef.current.set(`gi_surface_${gi}`, giMesh);
      }

      // 2. Stop lines (thin white strips)
      for (const sl of gen.stopLines) {
        const slMesh = MeshBuilder.CreateLines(
          `gi_sl_${gi}_${sl.approach}`,
          {
            points: [
              new Vector3(sl.p1.x, zHeight + 0.02, sl.p1.y),
              new Vector3(sl.p2.x, zHeight + 0.02, sl.p2.y),
            ],
          },
          scene
        );
        const slMat = new StandardMaterial(`gi_sl_mat_${gi}_${sl.approach}`, scene);
        slMat.emissiveColor = new Color3(1, 1, 1);
        slMat.disableLighting = true;
        slMat.lineWidth = 4;
        slMesh.material = slMat;
        slMesh.isPickable = false;
        roadMeshesRef.current.set(`gi_sl_${gi}_${sl.approach}`, slMesh);
      }

      // 3. Crosswalks (white striped rectangles)
      for (const cw of gen.crosswalks) {
        if (cw.corners.length < 4) continue;
        const cwPositions: number[] = [];
        const cwIndices: number[] = [];
        for (const c of cw.corners) {
          cwPositions.push(c.x, zHeight + 0.04, c.y);
        }
        // Two triangles for the rectangle
        cwIndices.push(0, 1, 2, 0, 2, 3);

        const cwMesh = new Mesh(`gi_cw_${gi}_${cw.approach}`, scene);
        const cwVd = new VertexData();
        cwVd.positions = cwPositions;
        cwVd.indices = cwIndices;
        const cwNormals: number[] = [];
        VertexData.ComputeNormals(cwPositions, cwIndices, cwNormals);
        cwVd.normals = cwNormals;
        cwVd.applyToMesh(cwMesh, true);

        const cwMat = new StandardMaterial(`gi_cw_mat_${gi}_${cw.approach}`, scene);
        cwMat.backFaceCulling = false;
        cwMat.diffuseColor = new Color3(0.95, 0.95, 0.95);
        cwMat.emissiveColor = new Color3(0.25, 0.25, 0.25);
        cwMat.specularColor = new Color3(0, 0, 0);
        cwMesh.material = cwMat;
        cwMesh.isPickable = false;
        roadMeshesRef.current.set(`gi_cw_${gi}_${cw.approach}`, cwMesh);
      }

      // 4. Lane connection paths (colored lines)
      const turnColors3D: Record<string, Color3> = {
        straight: new Color3(0.3, 0.8, 0.64),
        left: new Color3(1, 0.67, 0),
        right: new Color3(1, 0.42, 0.42),
      };
      for (let li = 0; li < gen.laneConnections.length; li++) {
        const lc = gen.laneConnections[li];
        if (lc.path.length < 2) continue;
        const lcPoints = lc.path.map((p) => new Vector3(p.x, zHeight + 0.06, p.y));
        const lcMesh = MeshBuilder.CreateLines(`gi_lc_${gi}_${li}`, { points: lcPoints }, scene);
        const lcMat = new StandardMaterial(`gi_lc_mat_${gi}_${li}`, scene);
        lcMat.emissiveColor = turnColors3D[lc.type] || new Color3(1, 1, 1);
        lcMat.disableLighting = true;
        lcMesh.material = lcMat;
        lcMesh.isPickable = false;
        roadMeshesRef.current.set(`gi_lc_${gi}_${li}`, lcMesh);
      }

      // 5. Intersection center marker (red sphere)
      const giMarker = MeshBuilder.CreateSphere(`gi_marker_${gi}`, { diameter: 1.5, segments: 8 }, scene);
      giMarker.position = new Vector3(gen.center.x, zHeight + 1, gen.center.y);
      const giMarkerMat = new StandardMaterial(`gi_marker_mat_${gi}`, scene);
      giMarkerMat.diffuseColor = new Color3(1, 0.3, 0.3);
      giMarkerMat.emissiveColor = new Color3(0.4, 0.1, 0.1);
      giMarkerMat.specularColor = new Color3(0, 0, 0);
      giMarker.material = giMarkerMat;
      giMarker.isPickable = false;
      roadMeshesRef.current.set(`gi_marker_${gi}`, giMarker);
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
