/**
 * RoadViewport — Unified 2D/3D road editing viewport (Babylon.js)
 *
 * - Camera starts top-down (orthographic-like, beta=0) = "2D mode"
 * - Hold middle mouse + drag = rotate into 3D perspective
 * - Scroll = zoom, right-drag = pan
 * - Drawing tools work in top-down view via raycasting to ground plane
 * - Roads rendered as ribbons with elevation
 * - Control points as spheres, bezier handles as lines + small spheres
 * - Everything synchronized with the Zustand store in real-time
 */

import React, { useEffect, useRef, useCallback } from 'react';
import { useRoadStudioStore } from './store/roadStudioStore';
import {
  type Road,
  type ControlPoint,
  geoToLocal,
  localToGeo,
  sampleRoad,
  generateId,
} from '../shared/types';
import {
  Engine, Scene, ArcRotateCamera, Vector3, HemisphericLight, DirectionalLight,
  MeshBuilder, StandardMaterial, Color3, Mesh, LinesMesh, Observable,
  PointerEventTypes, PointerInfo, Ray, Plane, Quaternion,
  Texture, DynamicTexture,
} from '@babylonjs/core';

interface RoadViewportProps {
  className?: string;
}

export const RoadViewport: React.FC<RoadViewportProps> = ({ className }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const engineRef = useRef<Engine | null>(null);
  const sceneRef = useRef<Scene | null>(null);
  const cameraRef = useRef<ArcRotateCamera | null>(null);
  const groundRef = useRef<Mesh | null>(null);
  const roadMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const pointMeshesRef = useRef<Map<string, Mesh>>(new Map());
  const handleLinesRef = useRef<Map<string, LinesMesh>>(new Map());

  // Store state refs (for use inside Babylon event handlers without re-creating them)
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
  const viewModeRef = useRef<'top' | 'perspective'>('top');
  const showMapOverlayRef = useRef(false);
  const groundMatRef = useRef<StandardMaterial | null>(null);

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

  useEffect(() => {
    toolRef.current = store.tool;
  }, [store.tool]);

  useEffect(() => {
    selectionRef.current = store.selection;
  }, [store.selection]);

  useEffect(() => {
    roadsRef.current = store.roads;
    updateRoadMeshes();
  }, [store.roads]);

  useEffect(() => {
    refLatRef.current = store.refLat;
    refLonRef.current = store.refLon;
    updateRoadMeshes();
  }, [store.refLat, store.refLon]);

  useEffect(() => {
    gridSizeRef.current = store.gridSize;
  }, [store.gridSize]);

  useEffect(() => {
    snapEnabledRef.current = store.snapEnabled;
  }, [store.snapEnabled]);

  useEffect(() => {
    drawingRoadIdRef.current = store.drawingRoadId;
  }, [store.drawingRoadId]);

  useEffect(() => {
    penDraggingRef.current = store.penDragging;
  }, [store.penDragging]);

  // ─── View mode + map overlay ──────────────────────────────
  useEffect(() => {
    viewModeRef.current = store.viewMode;
    const camera = cameraRef.current;
    if (!camera) return;
    if (store.viewMode === 'top') {
      // Smoothly animate to top-down
      camera.beta = 0.01;
    } else {
      // Perspective: angled view
      camera.beta = Math.PI / 3.5; // ~50 degrees
    }
  }, [store.viewMode]);

  useEffect(() => {
    showMapOverlayRef.current = store.showMapOverlay;
    const groundMat = groundMatRef.current;
    if (!groundMat) return;
    if (store.showMapOverlay) {
      // Create a dynamic texture with a map tile centered on ref origin
      const scene = sceneRef.current;
      if (!scene) return;
      const refLat = refLatRef.current;
      const refLon = refLonRef.current;
      // Use ArcGIS satellite tile at zoom 12 centered on ref origin
      const latTile = Math.floor((1 - Math.log(Math.tan((refLat * Math.PI) / 180) + 1 / Math.cos((refLat * Math.PI) / 180)) / Math.PI) / 2 * 4096);
      const lonTile = Math.floor(((refLon + 180) / 360) * 4096);
      const url = `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/12/${latTile}/${lonTile}`;
      const tex = new Texture(url, scene, false, true);
      groundMat.diffuseTexture = tex;
      groundMat.diffuseColor = new Color3(1, 1, 1);
    } else {
      groundMat.diffuseTexture = null;
      groundMat.diffuseColor = new Color3(0.12, 0.14, 0.16);
    }
  }, [store.showMapOverlay, store.refLat, store.refLon]);

  // ─── Babylon Scene Setup ───────────────────────────────────
  useEffect(() => {
    if (!canvasRef.current) return;

    const canvas = canvasRef.current;
    const engine = new Engine(canvas, true, { preserveDrawingBuffer: true, stencil: true });
    const scene = new Scene(engine);
    engineRef.current = engine;
    sceneRef.current = scene;

    // Camera — ArcRotate starts top-down (beta = 0.01 ≈ looking straight down)
    const camera = new ArcRotateCamera(
      'camera',
      -Math.PI / 2,  // alpha (rotation around Y)
      0.01,           // beta (0 = top-down, PI/2 = horizon)
      200,            // radius (zoom)
      new Vector3(0, 0, 0),
      scene
    );
    camera.attachControl(canvas, true);
    cameraRef.current = camera;

    // Camera limits
    camera.lowerRadiusLimit = 10;
    camera.upperRadiusLimit = 2000;
    camera.lowerBetaLimit = 0;
    camera.upperBetaLimit = Math.PI / 2.1; // Can't go below horizon
    camera.wheelDeltaPercentage = 0.01;
    camera.pinchDeltaPercentage = 0.01;

    // Don't detach — just prevent default left-button camera actions
    // by handling pointer events ourselves (middle=rotate, right=pan, left=draw)

    // Lights
    const hemi = new HemisphericLight('hemi', new Vector3(0, 1, 0), scene);
    hemi.intensity = 0.7;
    const dir = new DirectionalLight('dir', new Vector3(-1, -2, -1), scene);
    dir.intensity = 0.5;

    // Ground plane (for raycasting and visual grid)
    const ground = MeshBuilder.CreateGround('ground', { width: 4000, height: 4000, subdivisions: 2 }, scene);
    const groundMat = new StandardMaterial('groundMat', scene);
    groundMat.diffuseColor = new Color3(0.12, 0.14, 0.16);
    groundMat.specularColor = new Color3(0, 0, 0);
    ground.material = groundMat;
    groundRef.current = ground;
    groundMatRef.current = groundMat;

    // Grid lines (visual reference for 2D mode)
    const gridSize = 50;
    const gridExtent = 2000;
    const gridPoints: Vector3[] = [];
    for (let i = -gridExtent; i <= gridExtent; i += gridSize) {
      gridPoints.push(new Vector3(i, 0.01, -gridExtent));
      gridPoints.push(new Vector3(i, 0.01, gridExtent));
      gridPoints.push(new Vector3(-gridExtent, 0.01, i));
      gridPoints.push(new Vector3(gridExtent, 0.01, i));
    }
    const grid = MeshBuilder.CreateLines('grid', { points: gridPoints }, scene);
    grid.color = new Color3(0.2, 0.25, 0.28);
    grid.isPickable = false;

    // Major grid lines (every 200m)
    const majorGridPoints: Vector3[] = [];
    for (let i = -gridExtent; i <= gridExtent; i += 200) {
      majorGridPoints.push(new Vector3(i, 0.02, -gridExtent));
      majorGridPoints.push(new Vector3(i, 0.02, gridExtent));
      majorGridPoints.push(new Vector3(-gridExtent, 0.02, i));
      majorGridPoints.push(new Vector3(gridExtent, 0.02, i));
    }
    const majorGrid = MeshBuilder.CreateLines('majorGrid', { points: majorGridPoints }, scene);
    majorGrid.color = new Color3(0.3, 0.38, 0.35);
    majorGrid.isPickable = false;

    // ─── Pointer handling ──────────────────────────────────
    let isMiddleDown = false;
    let isLeftDown = false;
    let isRightDown = false;

    // ─── Manual camera controls ─────────────────────────────
    // Middle mouse = rotate, Right mouse = pan, Scroll = zoom
    // Left mouse = drawing/editing (handled in pointer observable below)
    let lastPointerX = 0;
    let lastPointerY = 0;

    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const delta = e.deltaY * 0.01;
      camera.radius = Math.max(camera.lowerRadiusLimit!, Math.min(camera.upperRadiusLimit!, camera.radius + delta));
    }, { passive: false });

    // ─── Pointer handling for drawing + camera ──────────────
    scene.onPointerObservable.add((pointerInfo: PointerInfo) => {
      const pt = pointerInfo.type;
      const evt = pointerInfo.event as PointerEvent;

      // ─── Pointer Down ────────────────────────────────────
      if (pt === PointerEventTypes.POINTERDOWN) {
        const button = (evt as any).button;
        const pickResult = scene.pick(scene.pointerX, scene.pointerY, (m) => m.metadata?.type === 'control-point' || m.metadata?.type === 'handle');
        const groundPick = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);

        lastPointerX = evt.clientX;
        lastPointerY = evt.clientY;

        if (button === 1) {
          // Middle mouse — manual rotate
          isMiddleDown = true;
          dragStateRef.current.mode = 'rotate';
        } else if (button === 2) {
          // Right mouse — manual pan
          isRightDown = true;
          dragStateRef.current.mode = 'pan';
        } else if (button === 0) {
          // Left mouse — drawing / editing
          isLeftDown = true;

          const tool = toolRef.current;

          if (tool === 'select') {
            // Check if we clicked a control point
            if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'control-point') {
              const meta = pickResult.pickedMesh.metadata;
              dragStateRef.current = {
                mode: 'move-point',
                roadId: meta.roadId,
                pointIndex: meta.pointIndex,
                handle: null,
                startX: scene.pointerX,
                startY: scene.pointerY,
              };
              store.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: null });
            } else if (pickResult?.hit && pickResult.pickedMesh?.metadata?.type === 'handle') {
              const meta = pickResult.pickedMesh.metadata;
              dragStateRef.current = {
                mode: 'move-handle',
                roadId: meta.roadId,
                pointIndex: meta.pointIndex,
                handle: meta.handle,
                startX: scene.pointerX,
                startY: scene.pointerY,
              };
              store.setSelection({ roadId: meta.roadId, pointIndices: [meta.pointIndex], handle: meta.handle });
            } else {
              // Click on empty space — deselect
              store.setSelection({ roadId: null, pointIndices: [], handle: null });
            }
          } else if (tool === 'line' || tool === 'pen') {
            // Drawing tools — click on ground to add points
            if (groundPick?.hit) {
              const worldPos = groundPick.pickedPoint!;
              const geo = localToGeo(worldPos.x, worldPos.z, refLatRef.current, refLonRef.current);

              // Snap to grid if enabled
              let finalLat = geo.lat;
              let finalLon = geo.lon;
              if (snapEnabledRef.current && gridSizeRef.current > 0) {
                const local = geoToLocal(geo.lat, geo.lon, refLatRef.current, refLonRef.current);
                const snappedX = Math.round(local.x / gridSizeRef.current) * gridSizeRef.current;
                const snappedY = Math.round(local.y / gridSizeRef.current) * gridSizeRef.current;
                const snapped = localToGeo(snappedX, snappedY, refLatRef.current, refLonRef.current);
                finalLat = snapped.lat;
                finalLon = snapped.lon;
              }

              // Snap to existing endpoints
              if (snapEnabledRef.current) {
                const snapDist = 5; // 5 meters snap radius
                for (const road of roadsRef.current) {
                  for (let i = 0; i < road.points.length; i++) {
                    const p = road.points[i];
                    const pLocal = geoToLocal(p.lat, p.lon, refLatRef.current, refLonRef.current);
                    const dist = Math.sqrt(
                      Math.pow(pLocal.x - worldPos.x, 2) + Math.pow(pLocal.y - worldPos.z, 2)
                    );
                    if (dist < snapDist) {
                      finalLat = p.lat;
                      finalLon = p.lon;
                      break;
                    }
                  }
                }
              }

              const drawingId = drawingRoadIdRef.current;
              if (!drawingId) {
                // Start a new road
                store.startNewRoad(finalLat, finalLon);
              } else {
                // Add point to existing road
                store.pushHistory('Add control point');
                store.addControlPoint(drawingId, finalLat, finalLon);

                if (tool === 'pen') {
                  // Pen tool: start dragging to create bezier handle
                  dragStateRef.current = {
                    mode: 'draw',
                    roadId: drawingId,
                    pointIndex: -1, // Will be set when we know the index
                    handle: null,
                    startX: scene.pointerX,
                    startY: scene.pointerY,
                  };
                  store.setPenDragging(true);
                }
              }
            }
          }
        }
      }

      // ─── Pointer Move ────────────────────────────────────
      if (pt === PointerEventTypes.POINTERMOVE) {
        const ds = dragStateRef.current;
        const dx = evt.clientX - lastPointerX;
        const dy = evt.clientY - lastPointerY;
        lastPointerX = evt.clientX;
        lastPointerY = evt.clientY;

        // Manual camera controls
        if (ds.mode === 'rotate' && isMiddleDown) {
          // Middle-drag = rotate camera
          camera.alpha -= dx * 0.005;
          camera.beta = Math.max(0, Math.min(Math.PI / 2.1, camera.beta - dy * 0.005));
        } else if (ds.mode === 'pan' && isRightDown) {
          // Right-drag = pan camera target
          const panSpeed = camera.radius * 0.001;
          camera.target.x += dx * panSpeed * Math.cos(camera.alpha);
          camera.target.z -= dx * panSpeed * Math.sin(camera.alpha);
          camera.target.y += dy * panSpeed;
        }

        if (ds.mode === 'move-point' && groundPick?.hit) {
          // Move control point
          const worldPos = groundPick.pickedPoint!;
          const geo = localToGeo(worldPos.x, worldPos.z, refLatRef.current, refLonRef.current);
          store.updateControlPoint(ds.roadId!, ds.pointIndex, geo.lat, geo.lon);
        } else if (ds.mode === 'move-handle' && groundPick?.hit) {
          // Move bezier handle
          const worldPos = groundPick.pickedPoint!;
          const road = roadsRef.current.find((r) => r.id === ds.roadId);
          if (road) {
            const point = road.points[ds.pointIndex];
            if (point) {
              const pointLocal = geoToLocal(point.lat, point.lon, refLatRef.current, refLonRef.current);
              const dx = worldPos.x - pointLocal.x;
              const dy = worldPos.z - pointLocal.y;
              const geoOffset = localToGeo(dx, dy, 0, 0);
              const offset = { lat: geoOffset.lat, lon: geoOffset.lon };
              store.setHandle(ds.roadId!, ds.pointIndex, ds.handle!, offset);
            }
          }
        } else if (ds.mode === 'draw' && toolRef.current === 'pen') {
          // Pen tool dragging — create bezier handle
          const gp = scene.pick(scene.pointerX, scene.pointerY, (m) => m === ground);
          if (gp?.hit) {
            const worldPos = gp.pickedPoint!;
            const road = roadsRef.current.find((r) => r.id === ds.roadId);
            if (road && road.points.length > 0) {
              const lastIdx = road.points.length - 1;
              const point = road.points[lastIdx];
              const pointLocal = geoToLocal(point.lat, point.lon, refLatRef.current, refLonRef.current);
              const dx = worldPos.x - pointLocal.x;
              const dy = worldPos.z - pointLocal.y;
              const geoOffset = localToGeo(dx, dy, 0, 0);
              const offset = { lat: geoOffset.lat, lon: geoOffset.lon };
              // Set handleOut on the new point
              store.setHandle(ds.roadId!, lastIdx, 'out', offset);
              // Mirror handleIn for smooth point
              store.setHandle(ds.roadId!, lastIdx, 'in', { lat: -offset.lat, lon: -offset.lon });
            }
          }
        }
      }

      // ─── Pointer Up ──────────────────────────────────────
      if (pt === PointerEventTypes.POINTERUP) {
        const button = (evt as any).button;
        if (button === 1) isMiddleDown = false;
        if (button === 2) isRightDown = false;
        if (button === 0) isLeftDown = false;

        if (dragStateRef.current.mode === 'draw') {
          store.setPenDragging(false);
        }
        if (dragStateRef.current.mode === 'move-point' || dragStateRef.current.mode === 'move-handle') {
          // Push history on drag end
          // (Already updated in real-time, history pushed before drag)
        }
        dragStateRef.current.mode = 'none';
      }
    });

    // ─── Keyboard: Escape to finish drawing, Delete to remove ───
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        store.finishDrawing();
      } else if (e.key === 'Delete' || e.key === 'Backspace') {
        const sel = selectionRef.current;
        if (sel.roadId && sel.pointIndices.length > 0) {
          store.deleteControlPoint(sel.roadId, sel.pointIndices[0]);
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === 'z') {
        e.preventDefault();
        store.undo();
      } else if ((e.ctrlKey || e.metaKey) && e.key === 'y') {
        e.preventDefault();
        store.redo();
      }
    };
    canvas.addEventListener('keydown', onKeyDown);

    // Render loop
    engine.runRenderLoop(() => {
      scene.render();
    });

    // Resize handler
    const onResize = () => engine.resize();
    window.addEventListener('resize', onResize);

    // Prevent context menu on right-click (we use right mouse for panning)
    const onContextMenu = (e: Event) => e.preventDefault();
    canvas.addEventListener('contextmenu', onContextMenu);

    // Initial road mesh update
    updateRoadMeshes();

    return () => {
      window.removeEventListener('resize', onResize);
      canvas.removeEventListener('keydown', onKeyDown);
      canvas.removeEventListener('contextmenu', onContextMenu);
      engine.dispose();
      engineRef.current = null;
      sceneRef.current = null;
    };
  }, []); // Run once

  // ─── Update road meshes from store ────────────────────────────
  const updateRoadMeshes = useCallback(() => {
    const scene = sceneRef.current;
    if (!scene) return;

    const roads = roadsRef.current;
    const refLat = refLatRef.current;
    const refLon = refLonRef.current;
    const selection = selectionRef.current;

    // ─── Remove old meshes ─────────────────────────────────
    roadMeshesRef.current.forEach((mesh) => mesh.dispose());
    roadMeshesRef.current.clear();
    pointMeshesRef.current.forEach((mesh) => mesh.dispose());
    pointMeshesRef.current.clear();
    handleLinesRef.current.forEach((mesh) => mesh.dispose());
    handleLinesRef.current.clear();

    // ─── Create road meshes ────────────────────────────────
    for (const road of roads) {
      if (road.points.length < 2) {
        // Single point — just show the control point
        createControlPointMeshes(road, refLat, refLon, scene, selection);
        continue;
      }

      // Sample the road path
      const samples = sampleRoad(road, refLat, refLon, 16);
      if (samples.length < 2) continue;

      // Create road ribbon
      const halfWidth = road.width / 2;
      const pathLeft: Vector3[] = [];
      const pathRight: Vector3[] = [];

      for (let i = 0; i < samples.length; i++) {
        const s = samples[i];
        // Calculate tangent direction
        let tx: number, ty: number;
        if (i === 0) {
          tx = samples[1].x - s.x;
          ty = samples[1].y - s.y;
        } else if (i === samples.length - 1) {
          tx = s.x - samples[i - 1].x;
          ty = s.y - samples[i - 1].y;
        } else {
          tx = samples[i + 1].x - samples[i - 1].x;
          ty = samples[i + 1].y - samples[i - 1].y;
        }
        const len = Math.sqrt(tx * tx + ty * ty) || 1;
        const nx = -ty / len;
        const ny = tx / len;

        pathLeft.push(new Vector3(s.x + nx * halfWidth, s.z, s.y + ny * halfWidth));
        pathRight.push(new Vector3(s.x - nx * halfWidth, s.z, s.y - ny * halfWidth));
      }

      const roadMesh = MeshBuilder.CreateRibbon(
        `road_${road.id}`,
        { pathArray: [pathLeft, pathRight], closeArray: false, closePath: false },
        scene
      );
      const roadMat = new StandardMaterial(`roadMat_${road.id}`, scene);
      const color = Color3.FromHexString(road.color);
      roadMat.diffuseColor = color;
      roadMat.specularColor = new Color3(0.1, 0.1, 0.1);
      roadMesh.material = roadMat;
      roadMesh.isPickable = false;
      roadMeshesRef.current.set(road.id, roadMesh);

      // Center line (dashed yellow)
      const centerPoints: Vector3[] = samples.map((s) => new Vector3(s.x, s.z + 0.05, s.y));
      if (centerPoints.length >= 2) {
        const centerLine = MeshBuilder.CreateLines(`center_${road.id}`, { points: centerPoints }, scene);
        centerLine.color = new Color3(1, 0.9, 0.3);
        centerLine.isPickable = false;
        roadMeshesRef.current.set(`center_${road.id}`, centerLine);
      }

      // Control points
      createControlPointMeshes(road, refLat, refLon, scene, selection);
    }
  }, []);

  // ─── Create control point + handle meshes ───────────────────
  function createControlPointMeshes(
    road: Road,
    refLat: number,
    refLon: number,
    scene: Scene,
    selection: { roadId: string | null; pointIndices: number[]; handle: 'in' | 'out' | null }
  ) {
    for (let i = 0; i < road.points.length; i++) {
      const p = road.points[i];
      const local = geoToLocal(p.lat, p.lon, refLat, refLon);
      const isSelected = selection.roadId === road.id && selection.pointIndices.includes(i);

      // Control point sphere
      const cpMesh = MeshBuilder.CreateSphere(
        `cp_${road.id}_${i}`,
        { diameter: isSelected ? 4 : 3, segments: 12 },
        scene
      );
      cpMesh.position = new Vector3(local.x, p.z, local.y);
      const cpMat = new StandardMaterial(`cpMat_${road.id}_${i}`, scene);
      cpMat.diffuseColor = isSelected ? new Color3(1, 0.8, 0.2) : new Color3(0.3, 0.8, 0.65);
      cpMat.emissiveColor = isSelected ? new Color3(0.4, 0.3, 0) : new Color3(0, 0, 0);
      cpMat.specularColor = new Color3(0, 0, 0);
      cpMesh.material = cpMat;
      cpMesh.metadata = { type: 'control-point', roadId: road.id, pointIndex: i };
      pointMeshesRef.current.set(`cp_${road.id}_${i}`, cpMesh);

      // Bezier handles (only for smooth points with handles, or selected point)
      if ((p.type === 'smooth' && (p.handleIn || p.handleOut)) || isSelected) {
        // Handle In
        if (p.handleIn) {
          const hLocal = geoToLocal(p.lat + p.handleIn.lat, p.lon + p.handleIn.lon, refLat, refLon);
          const handleLinePts = [
            new Vector3(local.x, p.z, local.y),
            new Vector3(hLocal.x, p.z, hLocal.y),
          ];
          const hLine = MeshBuilder.CreateLines(`hin_${road.id}_${i}`, { points: handleLinePts }, scene);
          hLine.color = new Color3(0.5, 0.7, 1);
          hLine.isPickable = false;
          handleLinesRef.current.set(`hin_${road.id}_${i}`, hLine);

          // Handle sphere
          const hSphere = MeshBuilder.CreateSphere(`hinS_${road.id}_${i}`, { diameter: 2, segments: 8 }, scene);
          hSphere.position = new Vector3(hLocal.x, p.z, hLocal.y);
          const hMat = new StandardMaterial(`hinM_${road.id}_${i}`, scene);
          hMat.diffuseColor = new Color3(0.4, 0.6, 1);
          hMat.emissiveColor = new Color3(0.1, 0.2, 0.3);
          hMat.specularColor = new Color3(0, 0, 0);
          hSphere.material = hMat;
          hSphere.metadata = { type: 'handle', roadId: road.id, pointIndex: i, handle: 'in' };
          handleLinesRef.current.set(`hinS_${road.id}_${i}`, hSphere);
        }

        // Handle Out
        if (p.handleOut) {
          const hLocal = geoToLocal(p.lat + p.handleOut.lat, p.lon + p.handleOut.lon, refLat, refLon);
          const handleLinePts = [
            new Vector3(local.x, p.z, local.y),
            new Vector3(hLocal.x, p.z, hLocal.y),
          ];
          const hLine = MeshBuilder.CreateLines(`hout_${road.id}_${i}`, { points: handleLinePts }, scene);
          hLine.color = new Color3(1, 0.6, 0.4);
          hLine.isPickable = false;
          handleLinesRef.current.set(`hout_${road.id}_${i}`, hLine);

          const hSphere = MeshBuilder.CreateSphere(`houtS_${road.id}_${i}`, { diameter: 2, segments: 8 }, scene);
          hSphere.position = new Vector3(hLocal.x, p.z, hLocal.y);
          const hMat = new StandardMaterial(`houtM_${road.id}_${i}`, scene);
          hMat.diffuseColor = new Color3(1, 0.5, 0.3);
          hMat.emissiveColor = new Color3(0.3, 0.1, 0);
          hMat.specularColor = new Color3(0, 0, 0);
          hSphere.material = hMat;
          hSphere.metadata = { type: 'handle', roadId: road.id, pointIndex: i, handle: 'out' };
          handleLinesRef.current.set(`houtS_${road.id}_${i}`, hSphere);
        }
      }
    }
  }

  return (
    <canvas
      ref={canvasRef}
      className={className}
      tabIndex={0}
      style={{ outline: 'none', touchAction: 'none' }}
    />
  );
};
