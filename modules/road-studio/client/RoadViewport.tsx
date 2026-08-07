/**
 * RoadViewport — Unified 2D/3D road editing viewport.
 *
 * Top mode (default):
 *   - Full world map (MapLibre satellite imagery)
 *   - Camera locked — no rotation, pure 2D top-down
 *   - Draw roads on the map with Line/Pen tools
 *
 * 3D mode:
 *   - Babylon.js 3D perspective viewport
 *   - Roads rendered as 3D ribbons with elevation
 *   - Middle-mouse rotate, right-drag pan, scroll zoom
 *   - Click "Top" button to return to 2D map
 *
 * Both views are synchronized via the Zustand store.
 */

import React, { useEffect, useRef, useCallback } from 'react';
import 'maplibre-gl/dist/maplibre-gl.css';
import maplibregl from 'maplibre-gl';
import { useRoadStudioStore } from './store/roadStudioStore';
import {
  type Road,
  type ControlPoint,
  type RoadProfile,
  geoToLocal,
  localToGeo,
  sampleRoad,
  generateId,
  ROAD_PROFILES,
} from '../shared/types';
import {
  Engine, Scene, ArcRotateCamera, Vector3, HemisphericLight, DirectionalLight,
  MeshBuilder, StandardMaterial, Color3, Color4, Mesh, LinesMesh, Texture,
  PointerEventTypes, PointerInfo,
} from '@babylonjs/core';

interface RoadViewportProps {
  className?: string;
}

export const RoadViewport: React.FC<RoadViewportProps> = ({ className }) => {
  // ─── Refs for both views ────────────────────────────────────
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const overlayCanvasRef = useRef<HTMLCanvasElement>(null); // 3D overlay on top of map
  const mapRef = useRef<maplibregl.Map | null>(null);
  const engineRef = useRef<Engine | null>(null);
  const sceneRef = useRef<Scene | null>(null);
  const cameraRef = useRef<ArcRotateCamera | null>(null);
  const groundRef = useRef<Mesh | null>(null);
  const roadMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const pointMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const handleLinesRef = useRef<Map<string, LinesMesh>>(new Map());

  // Overlay engine/scene (for 3D roads on top of MapLibre)
  const overlayEngineRef = useRef<Engine | null>(null);
  const overlaySceneRef = useRef<Scene | null>(null);
  const overlayRoadMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const textureCacheRef = useRef<Map<string, Texture>>(new Map());

  // Store refs (for use inside event handlers)
  const toolRef = useRef<string>('select');
  const selectionRef = useRef<{ roadId: string | null; pointIndices: number[]; handle: 'in' | 'out' | null }>({
    roadId: null, pointIndices: [], handle: null,
  });
  const roadsRef = useRef<Road[]>([]);
  const refLatRef = useRef(18.52);
  const refLonRef = useRef(73.85);
  const gridSizeRef = useRef(10);
  const snapEnabledRef = useRef(true);
  const drawingRoadIdRef = useRef<string | null>(null);
  const penDraggingRef = useRef(false);

  // Drag state
  const dragStateRef = useRef<{
    mode: 'none' | 'pan' | 'rotate' | 'move-point' | 'move-handle' | 'draw';
    roadId: string | null;
    pointIndex: number;
    handle: 'in' | 'out' | null;
    startX: number;
    startY: number;
  }>({ mode: 'none', roadId: null, pointIndex: -1, handle: null, startX: 0, startY: 0 });

  // ─── Sync refs from store ──────────────────────────────────
  const store = useRoadStudioStore();
  const viewMode = store.viewMode;

  useEffect(() => { toolRef.current = store.tool; }, [store.tool]);
  useEffect(() => { selectionRef.current = store.selection; }, [store.selection]);
  useEffect(() => { roadsRef.current = store.roads; updateAllViews(); }, [store.roads]);
  useEffect(() => { refLatRef.current = store.refLat; refLonRef.current = store.refLon; updateAllViews(); }, [store.refLat, store.refLon]);
  useEffect(() => { gridSizeRef.current = store.gridSize; }, [store.gridSize]);
  useEffect(() => { snapEnabledRef.current = store.snapEnabled; }, [store.snapEnabled]);
  useEffect(() => { drawingRoadIdRef.current = store.drawingRoadId; }, [store.drawingRoadId]);
  useEffect(() => { penDraggingRef.current = store.penDragging; }, [store.penDragging]);

  // ═══════════════════════════════════════════════════════════
  // TOP VIEW — MapLibre 2D world map
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

    // Click handler for drawing on the map
    map.on('click', (e) => {
      const tool = toolRef.current;
      if (tool !== 'line' && tool !== 'pen') return;

      const lat = e.lngLat.lat;
      const lon = e.lngLat.lng;

      // Snap to existing endpoints
      let finalLat = lat;
      let finalLon = lon;
      if (snapEnabledRef.current) {
        const snapDist = 0.00005; // ~5m in degrees
        for (const road of roadsRef.current) {
          for (const p of road.points) {
            if (Math.abs(p.lat - lat) < snapDist && Math.abs(p.lon - lon) < snapDist) {
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

    // Update overlay when map moves
    map.on('move', () => {
      // Camera sync happens in render loop automatically
    });
    map.on('load', () => {
      updateOverlayRoads();
    });

    mapRef.current = map;

    // ─── 3D Road Overlay (Babylon.js on top of MapLibre) ─────
    // Transparent canvas that renders 3D roads with elevation
    // synced to the MapLibre camera position
    if (overlayCanvasRef.current && !overlayEngineRef.current) {
      const overlayCanvas = overlayCanvasRef.current;
      const overlayEngine = new Engine(overlayCanvas, true, {
        preserveDrawingBuffer: true,
        stencil: true,
        alpha: true, // transparent background
      });
      const overlayScene = new Scene(overlayEngine);
      // Use Color4 with alpha=0 for fully transparent background
      overlayScene.clearColor = new Color4(0, 0, 0, 0);

      overlayEngineRef.current = overlayEngine;
      overlaySceneRef.current = overlayScene;

      // Camera — will be synced with MapLibre
      const overlayCam = new ArcRotateCamera(
        'overlayCam', -Math.PI / 2, 0.01, 500, new Vector3(0, 0, 0), overlayScene
      );
      overlayCam.lowerRadiusLimit = 1;
      overlayCam.upperRadiusLimit = 500000;
      // Use the same FOV as default (0.8 radians = ~45.8 degrees vertical)
      overlayCam.fov = 0.8;

      // Light
      const overlayHemi = new HemisphericLight('overlayHemi', new Vector3(0, 1, 0), overlayScene);
      overlayHemi.intensity = 0.8;

      // Render loop — sync camera with MapLibre every frame
      overlayEngine.runRenderLoop(() => {
        if (mapRef.current && mapRef.current.loaded()) {
          const center = mapRef.current.getCenter();
          const zoom = mapRef.current.getZoom();
          const bearing = mapRef.current.getBearing();
          const pitch = mapRef.current.getPitch();

          // Convert map center to local meters (our coordinate system: X=east, Z=north)
          const local = geoToLocal(center.lat, center.lng, refLatRef.current, refLonRef.current);

          // Meters per pixel at this zoom and latitude
          const metersPerPixel = 156543.03392 * Math.cos((center.lat * Math.PI) / 180) / Math.pow(2, zoom);

          // Canvas dimensions in CSS pixels (not device pixels)
          const cssWidth = overlayCanvas.clientWidth;
          const cssHeight = overlayCanvas.clientHeight;

          // Visible viewport size in meters
          const viewportHeightMeters = cssHeight * metersPerPixel;
          const viewportWidthMeters = cssWidth * metersPerPixel;

          // For ArcRotateCamera: visible height = 2 * radius * tan(fov/2)
          // So radius = viewportHeightMeters / (2 * tan(fov/2))
          const fov = overlayCam.fov;
          const radius = viewportHeightMeters / (2 * Math.tan(fov / 2));

          // Position camera
          overlayCam.target = new Vector3(local.x, 0, local.y);
          overlayCam.radius = Math.max(1, radius);

          // Bearing: MapLibre bearing is clockwise, Babylon alpha is counterclockwise
          // At bearing=0 (north up), alpha = -PI/2 (camera south, looking north)
          // At bearing=90 (east up), alpha = -PI (camera west, looking east)
          overlayCam.alpha = -Math.PI / 2 - (bearing * Math.PI) / 180;

          // Pitch: MapLibre pitch is 0-60 degrees, Babylon beta is 0 (top) to PI/2 (horizon)
          // At pitch=0 (top down), beta = 0.001
          // At pitch=60, beta = 60 * PI/180
          overlayCam.beta = Math.max(0.001, (pitch * Math.PI) / 180);
        }
        overlayScene.render();
      });

      // Resize handler — also observe the container element
      const onOverlayResize = () => overlayEngine.resize();
      window.addEventListener('resize', onOverlayResize);

      // ResizeObserver for the map container (handles layout changes)
      const resizeObserver = new ResizeObserver(() => {
        overlayEngine.resize();
      });
      if (mapContainerRef.current) {
        resizeObserver.observe(mapContainerRef.current);
      }
    }

    // Update overlay roads after a short delay (let canvas initialize)
    setTimeout(() => updateOverlayRoads(), 100);

    return () => {
      map.remove();
      mapRef.current = null;
      // Dispose overlay
      if (overlayEngineRef.current) {
        overlayEngineRef.current.dispose();
        overlayEngineRef.current = null;
        overlaySceneRef.current = null;
        overlayRoadMeshesRef.current.clear();
        textureCacheRef.current.clear();
      }
    };
  }, [viewMode]);

  // Update map roads + overlay when store changes (in top mode)
  useEffect(() => {
    if (viewMode === 'top' && mapRef.current) {
      updateMapRoads(mapRef.current);
      updateOverlayRoads();
    }
  }, [store.roads, store.selection, viewMode]);

  function updateMapRoads(map: maplibregl.Map) {
    if (!map.loaded()) return;

    // Remove old road layers/sources
    const style = map.getStyle();
    if (style?.layers) {
      for (const layer of style.layers) {
        if (layer.id.startsWith('road-') || layer.id.startsWith('cp-')) {
          map.removeLayer(layer.id);
        }
      }
    }
    if (style?.sources) {
      for (const src of Object.keys(style.sources)) {
        if (src.startsWith('road-') || src.startsWith('cp-')) {
          map.removeSource(src);
        }
      }
    }

    const roads = roadsRef.current;
    const selection = selectionRef.current;

    for (const road of roads) {
      if (road.points.length < 2) continue;

      // Build GeoJSON LineString with bezier sampling
      const coordinates: [number, number][] = [];
      const samples = sampleRoad(road, refLatRef.current, refLonRef.current, 16);
      for (const s of samples) {
        const geo = localToGeo(s.x, s.y, refLatRef.current, refLonRef.current);
        coordinates.push([geo.lon, geo.lat]);
      }

      const sourceId = `road-${road.id}`;
      map.addSource(sourceId, {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: { width: road.width, color: road.color },
          geometry: { type: 'LineString', coordinates },
        },
      });

      map.addLayer({
        id: `road-line-${road.id}`,
        type: 'line',
        source: sourceId,
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': road.color,
          'line-width': 2,
          'line-opacity': 0.4,
        },
      });

      // Center line (dashed yellow) — thin guide only
      map.addLayer({
        id: `road-center-${road.id}`,
        type: 'line',
        source: sourceId,
        layout: { 'line-cap': 'round', 'line-join': 'round' },
        paint: {
          'line-color': '#ffeb3b',
          'line-width': 1,
          'line-dasharray': [2, 2],
          'line-opacity': 0.5,
        },
      });
    }

    // Add control points as circles
    const cpFeatures: any[] = [];
    for (const road of roads) {
      for (let i = 0; i < road.points.length; i++) {
        const p = road.points[i];
        const isSelected = selection.roadId === road.id && selection.pointIndices.includes(i);
        cpFeatures.push({
          type: 'Feature',
          properties: {
            roadId: road.id,
            pointIndex: i,
            selected: isSelected,
            z: p.z,
          },
          geometry: { type: 'Point', coordinates: [p.lon, p.lat] },
        });
      }
    }

    if (cpFeatures.length > 0) {
      map.addSource('cp-source', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: cpFeatures },
      });

      map.addLayer({
        id: 'cp-circles',
        type: 'circle',
        source: 'cp-source',
        paint: {
          'circle-radius': ['case', ['get', 'selected'], 8, 5],
          'circle-color': ['case', ['get', 'selected'], '#ffaa00', '#4ecca3'],
          'circle-stroke-color': '#ffffff',
          'circle-stroke-width': 2,
        },
      });

      // Click on control point to select
      map.on('click', 'cp-circles', (e: any) => {
        e.originalEvent.stopPropagation();
        const feature = e.features?.[0];
        if (feature) {
          const { roadId, pointIndex } = feature.properties;
          store.setSelection({ roadId, pointIndices: [pointIndex], handle: null });
        }
      });

      // Cursor pointer on hover
      map.on('mouseenter', 'cp-circles', () => {
        map.getCanvas().style.cursor = 'pointer';
      });
      map.on('mouseleave', 'cp-circles', () => {
        map.getCanvas().style.cursor = '';
      });
    }
  }

  // ═══════════════════════════════════════════════════════════
  // 3D VIEW — Babylon.js perspective viewport
  // ═══════════════════════════════════════════════════════════

  useEffect(() => {
    if (viewMode !== 'perspective') return;
    if (!canvasRef.current || engineRef.current) return;

    const canvas = canvasRef.current;
    const engine = new Engine(canvas, true, { preserveDrawingBuffer: true, stencil: true });
    const scene = new Scene(engine);
    engineRef.current = engine;
    sceneRef.current = scene;

    // Camera — perspective angled view
    const camera = new ArcRotateCamera(
      'camera',
      -Math.PI / 2,
      Math.PI / 3.5, // ~50 degrees = perspective
      200,
      new Vector3(0, 0, 0),
      scene
    );
    camera.attachControl(canvas, true);
    cameraRef.current = camera;

    camera.lowerRadiusLimit = 10;
    camera.upperRadiusLimit = 2000;
    camera.lowerBetaLimit = 0;
    camera.upperBetaLimit = Math.PI / 2.1;
    camera.wheelDeltaPercentage = 0.01;

    // Lights
    const hemi = new HemisphericLight('hemi', new Vector3(0, 1, 0), scene);
    hemi.intensity = 0.7;
    const dir = new DirectionalLight('dir', new Vector3(-1, -2, -1), scene);
    dir.intensity = 0.5;

    // Ground
    const ground = MeshBuilder.CreateGround('ground', { width: 4000, height: 4000, subdivisions: 2 }, scene);
    const groundMat = new StandardMaterial('groundMat', scene);
    groundMat.diffuseColor = new Color3(0.12, 0.14, 0.16);
    groundMat.specularColor = new Color3(0, 0, 0);
    ground.material = groundMat;
    groundRef.current = ground;

    // Grid
    const gridPoints: Vector3[] = [];
    for (let i = -2000; i <= 2000; i += 50) {
      gridPoints.push(new Vector3(i, 0.01, -2000));
      gridPoints.push(new Vector3(i, 0.01, 2000));
      gridPoints.push(new Vector3(-2000, 0.01, i));
      gridPoints.push(new Vector3(2000, 0.01, i));
    }
    const grid = MeshBuilder.CreateLines('grid', { points: gridPoints }, scene);
    grid.color = new Color3(0.2, 0.25, 0.28);
    grid.isPickable = false;

    // Pointer handling — middle=rotate, right=pan, left=select/move
    let isMiddleDown = false;
    let lastPointerX = 0;
    let lastPointerY = 0;

    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const delta = e.deltaY * 0.01;
      camera.radius = Math.max(camera.lowerRadiusLimit!, Math.min(camera.upperRadiusLimit!, camera.radius + delta));
    }, { passive: false });

    canvas.addEventListener('contextmenu', (e) => e.preventDefault());

    scene.onPointerObservable.add((pointerInfo: PointerInfo) => {
      const pt = pointerInfo.type;
      const evt = pointerInfo.event as PointerEvent;

      if (pt === PointerEventTypes.POINTERDOWN) {
        const button = (evt as any).button;
        const pickResult = scene.pick(scene.pointerX, scene.pointerY, (m) => m.metadata?.type === 'control-point' || m.metadata?.type === 'handle');
        const groundPick = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);
        lastPointerX = evt.clientX;
        lastPointerY = evt.clientY;

        if (button === 1) {
          isMiddleDown = true;
          dragStateRef.current.mode = 'rotate';
        } else if (button === 2) {
          dragStateRef.current.mode = 'pan';
        } else if (button === 0) {
          if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'control-point') {
            const meta = pickResult.pickedMesh.metadata;
            dragStateRef.current = {
              mode: 'move-point', roadId: meta.roadId, pointIndex: meta.pointIndex,
              handle: null, startX: scene.pointerX, startY: scene.pointerY,
            };
            store.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: null });
          } else if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'handle') {
            const meta = pickResult.pickedMesh.metadata;
            dragStateRef.current = {
              mode: 'move-handle', roadId: meta.roadId, pointIndex: meta.pointIndex,
              handle: meta.handle, startX: scene.pointerX, startY: scene.pointerY,
            };
            store.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: meta.handle });
          } else {
            store.setSelection({ roadId: null, pointIndices: [], handle: null });
          }
        }
      }

      if (pt === PointerEventTypes.POINTERMOVE) {
        const ds = dragStateRef.current;
        const dx = evt.clientX - lastPointerX;
        const dy = evt.clientY - lastPointerY;
        lastPointerX = evt.clientX;
        lastPointerY = evt.clientY;

        if (ds.mode === 'rotate' && isMiddleDown) {
          camera.alpha -= dx * 0.005;
          camera.beta = Math.max(0, Math.min(Math.PI / 2.1, camera.beta - dy * 0.005));
        } else if (ds.mode === 'pan') {
          const panSpeed = camera.radius * 0.001;
          camera.target.x += dx * panSpeed * Math.cos(camera.alpha);
          camera.target.z -= dx * panSpeed * Math.sin(camera.alpha);
          camera.target.y += dy * panSpeed;
        }

        const groundPick = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);
        if (ds.mode === 'move-point' && groundPick?.hit) {
          const worldPos = groundPick.pickedPoint!;
          const geo = localToGeo(worldPos.x, worldPos.z, refLatRef.current, refLonRef.current);
          store.updateControlPoint(ds.roadId!, ds.pointIndex, geo.lat, geo.lon);
        } else if (ds.mode === 'move-handle' && groundPick?.hit) {
          const worldPos = groundPick.pickedPoint!;
          const road = roadsRef.current.find((r) => r.id === ds.roadId);
          if (road) {
            const point = road.points[ds.pointIndex];
            if (point) {
              const pointLocal = geoToLocal(point.lat, point.lon, refLatRef.current, refLonRef.current);
              const dx2 = worldPos.x - pointLocal.x;
              const dy2 = worldPos.z - pointLocal.y;
              const geoOffset = localToGeo(dx2, dy2, 0, 0);
              store.setHandle(ds.roadId!, ds.pointIndex, ds.handle!, { lat: geoOffset.lat, lon: geoOffset.lon });
            }
          }
        }
      }

      if (pt === PointerEventTypes.POINTERUP) {
        const button = (evt as any).button;
        if (button === 1) isMiddleDown = false;
        dragStateRef.current.mode = 'none';
      }
    });

    // Keyboard
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
    const onResize = () => engine.resize();
    window.addEventListener('resize', onResize);

    update3DMeshes();

    return () => {
      window.removeEventListener('resize', onResize);
      canvas.removeEventListener('keydown', onKeyDown);
      canvas.removeEventListener('contextmenu', (e) => e.preventDefault());
      engine.dispose();
      engineRef.current = null;
      sceneRef.current = null;
    };
  }, [viewMode]);

  // Update 3D meshes when store changes (in 3D mode)
  useEffect(() => {
    if (viewMode === 'perspective') {
      update3DMeshes();
    }
  }, [store.roads, store.selection, viewMode]);

  function updateAllViews() {
    if (viewMode === 'top' && mapRef.current) {
      updateMapRoads(mapRef.current);
      updateOverlayRoads();
    } else if (viewMode === 'perspective') {
      update3DMeshes();
    }
  }

  // ─── Get or create a cached texture ────────────────────────
  function getTexture(name: string, scene: Scene): Texture | null {
    if (textureCacheRef.current.has(name)) {
      return textureCacheRef.current.get(name)!;
    }
    const textureMap: Record<string, string> = {
      asphalt: '/assets/scaner-roads/pbr/asphalt_diff.png',
      marking: '/assets/scaner-roads/pbr/marking_diff.png',
      macadam: '/assets/scaner-roads/textures/macadam.png',
      bitume: '/assets/scaner-roads/textures/131_bitume.png',
      road_2lane: '/assets/scaner-roads/textures/ROAD_1_NoMark_NEW_2voies.png',
      road_1lane: '/assets/scaner-roads/textures/ROAD_1_NoMark_NEW_1voie.png',
      cobblestone: '/assets/scaner-roads/textures/urban_cobblestone.png',
      pavement: '/assets/scaner-roads/textures/pavement.png',
      sidewalk: '/assets/scaner-roads/textures/sidewalk.png',
    };
    const url = textureMap[name];
    if (!url) return null;
    const tex = new Texture(url, scene, false, true);
    textureCacheRef.current.set(name, tex);
    return tex;
  }

  // ─── Update 3D road overlay (on top of MapLibre) ───────────
  function updateOverlayRoads() {
    const scene = overlaySceneRef.current;
    if (!scene) return;

    // Clear old meshes
    overlayRoadMeshesRef.current.forEach((m) => m.dispose());
    overlayRoadMeshesRef.current.clear();

    const roads = roadsRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;

    for (const road of roads) {
      if (road.points.length < 2) continue;

      const samples = sampleRoad(road, refLat, refLon, 16);
      if (samples.length < 2) continue;

      const halfWidth = road.width / 2;
      const pathLeft: Vector3[] = [];
      const pathRight: Vector3[] = [];

      for (let i = 0; i < samples.length; i++) {
        const s = samples[i];
        let tx: number, ty: number;
        if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
        else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;
        pathLeft.push(new Vector3(s.x + nx * halfWidth, s.z, s.y + ny * halfWidth));
        pathRight.push(new Vector3(s.x - nx * halfWidth, s.z, s.y - ny * halfWidth));
      }

      // Road surface mesh
      const roadMesh = MeshBuilder.CreateRibbon(`overlay_road_${road.id}`, {
        pathArray: [pathLeft, pathRight],
        closeArray: false,
        closePath: false,
        sideOrientation: Mesh.DOUBLESIDE,
      }, scene);

      const roadMat = new StandardMaterial(`overlay_roadMat_${road.id}`, scene);

      // Apply SCANeR texture based on road profile
      const surfaceName = road.profile?.surfaceTexture || 'asphalt';
      const tex = getTexture(surfaceName, scene);
      if (tex) {
        roadMat.diffuseTexture = tex;
        roadMat.diffuseColor = new Color3(1, 1, 1);
        // Repeat texture along the road
        tex.uScale = road.points.length * 2;
        tex.vScale = 1;
      } else {
        roadMat.diffuseColor = Color3.FromHexString(road.color);
      }
      roadMat.specularColor = new Color3(0.05, 0.05, 0.05);
      roadMat.alpha = 0.95;
      roadMesh.material = roadMat;
      roadMesh.isPickable = false;
      overlayRoadMeshesRef.current.set(road.id, roadMesh);

      // Center line (lane marking)
      const centerPoints: Vector3[] = samples.map((s) => new Vector3(s.x, s.z + 0.1, s.y));
      if (centerPoints.length >= 2) {
        const centerLine = MeshBuilder.CreateLines(`overlay_center_${road.id}`, {
          points: centerPoints,
        }, scene);
        centerLine.color = new Color3(1, 0.9, 0.3);
        centerLine.isPickable = false;
        overlayRoadMeshesRef.current.set(`overlay_center_${road.id}`, centerLine);
      }

      // Lane divider lines (if multi-lane)
      if (road.laneCount > 1) {
        const laneOffset = halfWidth - (halfWidth * 2 / road.laneCount);
        for (let lane = 1; lane < road.laneCount; lane++) {
          const offset = -halfWidth + (halfWidth * 2 / road.laneCount) * lane;
          const dividerPoints: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i];
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
            else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len;
            const ny = tx / len;
            dividerPoints.push(new Vector3(s.x + nx * offset, s.z + 0.08, s.y + ny * offset));
          }
          const dividerLine = MeshBuilder.CreateLines(`overlay_divider_${road.id}_${lane}`, {
            points: dividerPoints,
          }, scene);
          dividerLine.color = new Color3(1, 1, 1);
          dividerLine.isPickable = false;
          overlayRoadMeshesRef.current.set(`overlay_divider_${road.id}_${lane}`, dividerLine);
        }
      }

      // ─── Curbs (raised edges on both sides) ─────────────────
      if (road.profile?.hasCurb) {
        const curbWidth = 0.3; // 30cm curb
        const curbHeight = 0.15; // 15cm raised
        for (const side of [-1, 1]) {
          const curbLeft: Vector3[] = [];
          const curbRight: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i];
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
            else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len;
            const ny = tx / len;
            const innerOffset = halfWidth * side;
            const outerOffset = (halfWidth + curbWidth) * side;
            curbLeft.push(new Vector3(s.x + nx * innerOffset, s.z + curbHeight, s.y + ny * innerOffset));
            curbRight.push(new Vector3(s.x + nx * outerOffset, s.z + curbHeight, s.y + ny * outerOffset));
          }
          const curbMesh = MeshBuilder.CreateRibbon(`overlay_curb_${road.id}_${side}`, {
            pathArray: [curbLeft, curbRight],
            closeArray: false,
            closePath: false,
          }, scene);
          const curbMat = new StandardMaterial(`overlay_curbMat_${road.id}_${side}`, scene);
          const curbTex = getTexture('pavement', scene);
          if (curbTex) {
            curbMat.diffuseTexture = curbTex;
            curbMat.diffuseColor = new Color3(0.8, 0.8, 0.8);
          } else {
            curbMat.diffuseColor = new Color3(0.5, 0.5, 0.5);
          }
          curbMat.specularColor = new Color3(0, 0, 0);
          curbMesh.material = curbMat;
          curbMesh.isPickable = false;
          overlayRoadMeshesRef.current.set(`overlay_curb_${road.id}_${side}`, curbMesh);
        }
      }

      // ─── Sidewalks (wider walking surface outside curbs) ────
      if (road.profile?.hasSidewalk) {
        const sidewalkWidth = 2.0; // 2m wide sidewalk
        const sidewalkHeight = 0.15; // same level as curb
        const curbWidth = road.profile?.hasCurb ? 0.3 : 0;
        for (const side of [-1, 1]) {
          const swLeft: Vector3[] = [];
          const swRight: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i];
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
            else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len;
            const ny = tx / len;
            const innerOffset = (halfWidth + curbWidth) * side;
            const outerOffset = (halfWidth + curbWidth + sidewalkWidth) * side;
            swLeft.push(new Vector3(s.x + nx * innerOffset, s.z + sidewalkHeight, s.y + ny * innerOffset));
            swRight.push(new Vector3(s.x + nx * outerOffset, s.z + sidewalkHeight, s.y + ny * outerOffset));
          }
          const swMesh = MeshBuilder.CreateRibbon(`overlay_sw_${road.id}_${side}`, {
            pathArray: [swLeft, swRight],
            closeArray: false,
            closePath: false,
          }, scene);
          const swMat = new StandardMaterial(`overlay_swMat_${road.id}_${side}`, scene);
          const swTex = getTexture('cobblestone', scene);
          if (swTex) {
            swMat.diffuseTexture = swTex;
            swMat.diffuseColor = new Color3(0.9, 0.9, 0.9);
            swTex.uScale = road.points.length * 2;
            swTex.vScale = 2;
          } else {
            swMat.diffuseColor = new Color3(0.7, 0.7, 0.7);
          }
          swMat.specularColor = new Color3(0, 0, 0);
          swMesh.material = swMat;
          swMesh.isPickable = false;
          overlayRoadMeshesRef.current.set(`overlay_sw_${road.id}_${side}`, swMesh);
        }
      }

      // Control point markers (small spheres)
      for (let i = 0; i < road.points.length; i++) {
        const p = road.points[i];
        const local = geoToLocal(p.lat, p.lon, refLat, refLon);
        const cpMesh = MeshBuilder.CreateSphere(`overlay_cp_${road.id}_${i}`, {
          diameter: Math.max(2, road.width * 0.3),
          segments: 8,
        }, scene);
        cpMesh.position = new Vector3(local.x, p.z + 0.5, local.y);
        const cpMat = new StandardMaterial(`overlay_cpMat_${road.id}_${i}`, scene);
        cpMat.diffuseColor = new Color3(1, 0.7, 0.1);
        cpMat.emissiveColor = new Color3(0.3, 0.2, 0);
        cpMat.specularColor = new Color3(0, 0, 0);
        cpMesh.material = cpMat;
        cpMesh.isPickable = false;
        overlayRoadMeshesRef.current.set(`overlay_cp_${road.id}_${i}`, cpMesh);
      }
    }
  }

  function update3DMeshes() {
    const scene = sceneRef.current;
    if (!scene) return;

    const roads = roadsRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;
    const selection = selectionRef.current;

    roadMeshesRef.current.forEach((m) => m.dispose());
    roadMeshesRef.current.clear();
    pointMeshesRef.current.forEach((m) => m.dispose());
    pointMeshesRef.current.clear();
    handleLinesRef.current.forEach((m) => m.dispose());
    handleLinesRef.current.clear();

    for (const road of roads) {
      if (road.points.length < 2) {
        createControlPointMeshes(road, refLat, refLon, scene, selection);
        continue;
      }

      const samples = sampleRoad(road, refLat, refLon, 16);
      if (samples.length < 2) continue;

      const halfWidth = road.width / 2;
      const pathLeft: Vector3[] = [];
      const pathRight: Vector3[] = [];

      for (let i = 0; i < samples.length; i++) {
        const s = samples[i];
        let tx: number, ty: number;
        if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
        else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
        else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;
        pathLeft.push(new Vector3(s.x + nx * halfWidth, s.z, s.y + ny * halfWidth));
        pathRight.push(new Vector3(s.x - nx * halfWidth, s.z, s.y - ny * halfWidth));
      }

      const roadMesh = MeshBuilder.CreateRibbon(`road_${road.id}`, { pathArray: [pathLeft, pathRight] }, scene);
      const roadMat = new StandardMaterial(`roadMat_${road.id}`, scene);
      // Apply SCANeR texture based on road profile
      const surfaceName = road.profile?.surfaceTexture || 'asphalt';
      const tex = getTexture(surfaceName, scene);
      if (tex) {
        roadMat.diffuseTexture = tex;
        roadMat.diffuseColor = new Color3(1, 1, 1);
        tex.uScale = road.points.length * 2;
        tex.vScale = 1;
      } else {
        roadMat.diffuseColor = Color3.FromHexString(road.color);
      }
      roadMat.specularColor = new Color3(0.1, 0.1, 0.1);
      roadMesh.material = roadMat;
      roadMesh.isPickable = false;
      roadMeshesRef.current.set(road.id, roadMesh);

      const centerPoints: Vector3[] = samples.map((s) => new Vector3(s.x, s.z + 0.05, s.y));
      if (centerPoints.length >= 2) {
        const centerLine = MeshBuilder.CreateLines(`center_${road.id}`, { points: centerPoints }, scene);
        centerLine.color = new Color3(1, 0.9, 0.3);
        centerLine.isPickable = false;
      }

      // ─── Curbs in 3D view ──────────────────────────────────
      if (road.profile?.hasCurb) {
        const curbWidth = 0.3;
        const curbHeight = 0.15;
        for (const side of [-1, 1]) {
          const cL: Vector3[] = [];
          const cR: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i];
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
            else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len;
            const ny = tx / len;
            cL.push(new Vector3(s.x + nx * halfWidth * side, s.z + curbHeight, s.y + ny * halfWidth * side));
            cR.push(new Vector3(s.x + nx * (halfWidth + curbWidth) * side, s.z + curbHeight, s.y + ny * (halfWidth + curbWidth) * side));
          }
          const curbMesh = MeshBuilder.CreateRibbon(`curb_${road.id}_${side}`, { pathArray: [cL, cR] }, scene);
          const curbMat = new StandardMaterial(`curbMat_${road.id}_${side}`, scene);
          curbMat.diffuseColor = new Color3(0.5, 0.5, 0.5);
          curbMat.specularColor = new Color3(0, 0, 0);
          curbMesh.material = curbMat;
          curbMesh.isPickable = false;
          roadMeshesRef.current.set(`curb_${road.id}_${side}`, curbMesh);
        }
      }

      // ─── Sidewalks in 3D view ──────────────────────────────
      if (road.profile?.hasSidewalk) {
        const swWidth = 2.0;
        const swHeight = 0.15;
        const curbW = road.profile?.hasCurb ? 0.3 : 0;
        for (const side of [-1, 1]) {
          const sL: Vector3[] = [];
          const sR: Vector3[] = [];
          for (let i = 0; i < samples.length; i++) {
            const s = samples[i];
            let tx: number, ty: number;
            if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
            else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
            else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
            const len = Math.sqrt(tx * tx + ty * ty) || 1;
            const nx = -ty / len;
            const ny = tx / len;
            sL.push(new Vector3(s.x + nx * (halfWidth + curbW) * side, s.z + swHeight, s.y + ny * (halfWidth + curbW) * side));
            sR.push(new Vector3(s.x + nx * (halfWidth + curbW + swWidth) * side, s.z + swHeight, s.y + ny * (halfWidth + curbW + swWidth) * side));
          }
          const swMesh = MeshBuilder.CreateRibbon(`sw_${road.id}_${side}`, { pathArray: [sL, sR] }, scene);
          const swMat = new StandardMaterial(`swMat_${road.id}_${side}`, scene);
          const swTex = getTexture('cobblestone', scene);
          if (swTex) {
            swMat.diffuseTexture = swTex;
            swMat.diffuseColor = new Color3(0.9, 0.9, 0.9);
          } else {
            swMat.diffuseColor = new Color3(0.7, 0.7, 0.7);
          }
          swMat.specularColor = new Color3(0, 0, 0);
          swMesh.material = swMat;
          swMesh.isPickable = false;
          roadMeshesRef.current.set(`sw_${road.id}_${side}`, swMesh);
        }
      }

      createControlPointMeshes(road, refLat, refLon, scene, selection);
    }
  }

  function createControlPointMeshes(
    road: Road, refLat: number, refLon: number, scene: Scene,
    selection: { roadId: string | null; pointIndices: number[]; handle: 'in' | 'out' | null }
  ) {
    for (let i = 0; i < road.points.length; i++) {
      const p = road.points[i];
      const local = geoToLocal(p.lat, p.lon, refLat, refLon);
      const isSelected = selection.roadId === road.id && selection.pointIndices.includes(i);

      const cpMesh = MeshBuilder.CreateSphere(`cp_${road.id}_${i}`, { diameter: isSelected ? 4 : 3, segments: 12 }, scene);
      cpMesh.position = new Vector3(local.x, p.z, local.y);
      const cpMat = new StandardMaterial(`cpMat_${road.id}_${i}`, scene);
      cpMat.diffuseColor = isSelected ? new Color3(1, 0.8, 0.2) : new Color3(0.3, 0.8, 0.65);
      cpMat.emissiveColor = isSelected ? new Color3(0.4, 0.3, 0) : new Color3(0, 0, 0);
      cpMat.specularColor = new Color3(0, 0, 0);
      cpMesh.material = cpMat;
      cpMesh.metadata = { type: 'control-point', roadId: road.id, pointIndex: i };
      pointMeshesRef.current.set(`cp_${road.id}_${i}`, cpMesh);

      if ((p.type === 'smooth' && (p.handleIn || p.handleOut)) || isSelected) {
        if (p.handleIn) {
          const hLocal = geoToLocal(p.lat + p.handleIn.lat, p.lon + p.handleIn.lon, refLat, refLon);
          const hLine = MeshBuilder.CreateLines(`hin_${road.id}_${i}`, {
            points: [new Vector3(local.x, p.z, local.y), new Vector3(hLocal.x, p.z, hLocal.y)],
          }, scene);
          hLine.color = new Color3(0.5, 0.7, 1);
          hLine.isPickable = false;

          const hSphere = MeshBuilder.CreateSphere(`hinS_${road.id}_${i}`, { diameter: 2, segments: 8 }, scene);
          hSphere.position = new Vector3(hLocal.x, p.z, hLocal.y);
          const hMat = new StandardMaterial(`hinM_${road.id}_${i}`, scene);
          hMat.diffuseColor = new Color3(0.4, 0.6, 1);
          hMat.emissiveColor = new Color3(0.1, 0.2, 0.3);
          hMat.specularColor = new Color3(0, 0, 0);
          hSphere.material = hMat;
          hSphere.metadata = { type: 'handle', roadId: road.id, pointIndex: i, handle: 'in' };
        }
        if (p.handleOut) {
          const hLocal = geoToLocal(p.lat + p.handleOut.lat, p.lon + p.handleOut.lon, refLat, refLon);
          const hLine = MeshBuilder.CreateLines(`hout_${road.id}_${i}`, {
            points: [new Vector3(local.x, p.z, local.y), new Vector3(hLocal.x, p.z, hLocal.y)],
          }, scene);
          hLine.color = new Color3(1, 0.6, 0.4);
          hLine.isPickable = false;

          const hSphere = MeshBuilder.CreateSphere(`houtS_${road.id}_${i}`, { diameter: 2, segments: 8 }, scene);
          hSphere.position = new Vector3(hLocal.x, p.z, hLocal.y);
          const hMat = new StandardMaterial(`houtM_${road.id}_${i}`, scene);
          hMat.diffuseColor = new Color3(1, 0.5, 0.3);
          hMat.emissiveColor = new Color3(0.3, 0.1, 0);
          hMat.specularColor = new Color3(0, 0, 0);
          hSphere.material = hMat;
          hSphere.metadata = { type: 'handle', roadId: road.id, pointIndex: i, handle: 'out' };
        }
      }
    }
  }

  // ─── Render ────────────────────────────────────────────────
  return (
    <div className={className} style={{ position: 'relative', width: '100%', height: '100%' }}>
      {/* Top view — MapLibre base map */}
      {viewMode === 'top' && (
        <>
          <div ref={mapContainerRef} style={{ width: '100%', height: '100%' }} />
          {/* 3D road overlay — transparent Babylon canvas on top of map */}
          <canvas
            ref={overlayCanvasRef}
            style={{
              position: 'absolute',
              top: 0,
              left: 0,
              width: '100%',
              height: '100%',
              pointerEvents: 'none', // let clicks pass through to map
              outline: 'none',
            }}
          />
        </>
      )}

      {/* 3D view — Babylon.js perspective */}
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
