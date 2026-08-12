# OpenGeoStudio — Phase 1 Migration Audit

> Target: Migrate from Electron/React/Node/TypeScript to a native C++/Qt 6 desktop application.
> Status: **Audit only — no code changes made.** This document is the Phase 1 deliverable.
> Date: 2026-08-12

This audit was produced by inspecting the actual source tree. Several items in
`AGENTS.md` are **out of date** and are corrected here (flagged with ⚠️).

---

## 1. Executive Summary

The repository is a mature Electron + React + TypeScript application with a
substantial **C++ road geometry engine** that is the most valuable asset to
preserve. The migration is feasible but non-trivial because:

1. The "C++ road engine" is actually **three distinct C++ codebases** glued
   together by a 110 KB N-API bridge (`road_bridge.cpp`).
2. The **terrain/export** native addon (`geoterrain_native`) is **mostly stubs**
   and references a separate C++ terrain project (`rts_*` libs, GDAL) that is
   **not present in this repo**. Real terrain/export work is done in
   **TypeScript** with `sharp`, `geotiff`, and raw HTTPS.
3. There are **4 workspaces**, not 3 — `train-studio` exists and is missing from
   `AGENTS.md`.
4. The 3D renderer is **Three.js**, not Babylon.js (Babylon was removed).
5. An in-progress **"road engine separation"** effort (`.kiro/specs/`) has
   already produced a clean `public/` + `internal/` API split with a working
   CMake build — this is the foundation Phase 2 should build on.
6. There is **no existing Qt, CMake app skeleton, or vcpkg manifest**. The
   migration starts the Qt application from scratch.

**Recommended path:** Preserve the road engine C++ as a CMake library, rewrite
the N-API bridge as a direct C++ service layer, rebuild the UI in Qt Widgets
(not QML), and rewrite the terrain/export pipeline in C++ (it is currently
TypeScript, so this is a real port, not a preservation task).

---

## 2. Current Architecture (As-Built)

```
React 19 + TypeScript 5.9 (renderer/)
        │  Vite 8 build → dist/
        ▼
Electron 42 Renderer (BrowserWindow)
        │  contextBridge (preload.ts) → window.electronAPI.*
        │  ipcRenderer.invoke(channel, ...args)
        ▼
Electron Main Process (app/main.ts → dist-electron/app/main.js)
        │  AppBootstrap (core/module/bootstrap.ts)
        │  registerBuiltinModules() → module-registry
        │  ipcMain.handle(channel, fn) per domain
        ▼
┌───────────────┬─────────────────┬──────────────────┐
│ Domain        │ Handler         │ Backend          │
├───────────────┼─────────────────┼──────────────────┤
│ Road engine   │ roadEngineHandler│ C++ N-API addon │
│ Terrain/Export│ exportHandler   │ TypeScript (Node)│
│ Native (stub) │ nativeHandler   │ C++ stub addon   │
│ FS/Dialog/OSM │ fs/dialog/osm   │ Node fs/https    │
│ Core services │ coreIpcHandler  │ core/* (TS)      │
└───────────────┴─────────────────┴──────────────────┘
        │
        ▼
Native Addons (.node files)
  • road_engine_native.node  → C++ road engine + LaneMaker (REAL)
  • geoterrain_native.node   → C++ terrain core (STUBS, not buildable here)
```

### 2.0 Codebase Size (LOC / File Counts)

Line counts were collected by scanning the actual source tree (excluding
`build/`, `node_modules/`, `dist/`, `.git/`). Blank lines are reported
separately; "Code" = total minus blank. These numbers ground the Effort
tags (S/M/L/XL) in the migration matrix (§7).

#### C++ code (preserve + rewrite bridge)

| Component | Files | Code LOC | Disposition |
|---|---|---|---|
| Codebase A — original road engine (`app/native/src/road/` excl. `road_engine/`) | 23 | 15,174 | **Preserve** (extra headers: intersection, lane_*, road_mesh_generator, etc.) |
| Codebase B — refactored public/internal split (`road_engine/`) | 19 | 7,780 | **Preserve** (clean CMake lib foundation; public: 12 files / 4,389 LOC, internal: 6 files / 3,370 LOC) |
| Codebase C — LaneMaker + libOpenDRIVE (`app/native/src/lanemaker/`) | 80 | 23,749 | **Preserve** (Apache-2.0, see §8.1) |
| N-API bridge files (`app/native/src/*.cpp` root) | 7 | 3,013 | **Rewrite** as `RoadEngineService` (direct C++) |
| **C++ total** | **129** | **49,716** | ~46.7 K preserve, ~3.0 K rewrite |

> Note: Codebase A and B overlap — B's `public/` headers are copies of A's
> headers. The counts above exclude `road_engine/` from A to avoid
> double-counting. In Phase 2, A's non-duplicated headers (intersection,
> lane_engine, lane_network, road_mesh_generator, road_mark_generator, etc.)
> must be merged into the B library structure.

#### TypeScript code (rewrite in C++/Qt)

| Component | Files | Code LOC | Disposition |
|---|---|---|---|
| Core framework (`core/`) | 28 | 4,284 | Port to C++ (DI, events, commands, jobs, project, workspace, etc.) |
| Renderer/UI (`renderer/`) | 41 | 6,830 | Rewrite as Qt Widgets (shell, panels, components, registry) |
| Road Studio module (`modules/road-studio/`) | 16 | 4,367 | Rewrite as Qt Road Studio widgets + C++ controller |
| Train Studio module (`modules/train-studio/`) | 9 | 3,207 | Rewrite as Qt Train Studio widgets + C++ |
| Terrain module (`modules/terrain/`) | 14 | 2,507 | Rewrite in C++ (map viewport + elevation clients) |
| Export module (`modules/export/`) | 14 | 3,096 | Rewrite in C++ (DEM fetch, image proc, format writers) |
| Shared module (`modules/shared/`) | 1 | 56 | Port geoUtils to C++ |
| App main/preload/handlers (`app/` excl. `native/`) | 12 | 1,802 | Eliminate (IPC → direct C++ calls) |
| Shared IPC channels + types (`shared/`) | 2 | 654 | Eliminate (channel → method mapping) |
| Tests (`tests/`) | 6 | 3,073 | Mirror to Qt Test (keep vitest as oracle until Phase 8) |
| **TS total (excl. tests)** | **137** | **26,803** | Full rewrite |
| **TS total (incl. tests)** | **143** | **29,876** | |

#### Summary

| Metric | C++ (preserve) | C++ (rewrite) | TS (rewrite) | Total |
|---|---|---|---|---|
| Code LOC | ~46,700 | ~3,000 | ~26,800 | ~76,500 |
| Files | ~122 | 7 | ~137 | ~266 |

The C++ engine (46.7 K LOC) is the largest single asset and is mostly
preserved. The TS rewrite (~26.8 K LOC) is the primary new work, with the
UI (6.8 K) + Road Studio (4.4 K) + Terrain/Export (5.6 K) + Core framework
(4.3 K) being the largest workstreams — consistent with the XL effort tags
on those rows in §7.

### 2.1 Process / Lifecycle

| Concern | Implementation | File |
|---|---|---|
| Electron entry | `app.whenReady()` → bootstrap → window → IPC | `app/main.ts` |
| Window | `BrowserWindow`, dark theme, CSP headers, nav hardening | `app/windows/mainWindow.ts` |
| Core bootstrap | `AppBootstrap` builds `AppContext` (DI root) | `core/module/bootstrap.ts` |
| Module registry | `registerBuiltinModules` (terrain, export, road-studio, train-studio, shared) | `core/module/builtin-modules.ts` |
| Plugin loader | Loads `plugins/` dir (currently empty/optional) | `core/module/plugin-loader.ts` |
| Global error guard | EPIPE/ERR_STREAM_DESTROYED swallowed | `app/main.ts` |
| Preload | `contextBridge.exposeInMainWorld('electronAPI', api)` | `app/preload.ts` |

### 2.2 Core Framework (`core/`)

A TypeScript DI/event/command framework. **All of this must be re-implemented in C++.**

| Subsystem | TS File | Notes |
|---|---|---|
| Service registry (DI) | `core/di/service-registry.ts` | Token-based DI |
| Event bus | `core/events/event-bus.ts` | Pub/sub |
| Command system | `core/commands/command-system.ts` | Menu/keyboard actions |
| Core commands | `core/commands/core-commands.ts` | Built-in commands |
| Undo/redo | `core/commands/undo-redo.ts` | Stack (renderer-side bridge exists) |
| Config | `core/config/config.ts`, `preferences.ts` | Settings store |
| Filesystem | `core/filesystem/file-system.ts` | fs abstraction |
| Job system | `core/jobs/job-system.ts` | Background tasks |
| Layer system | `core/layers/layer-system.ts` | GIS map layers |
| Logger | `core/logger/logger.ts` | Scoped logger |
| Module system | `core/module/*.ts` | Bootstrap, registry, contributions, plugin SDK |
| Notifications | `core/notifications/notification-system.ts` | Toasts |
| Project manager | `core/project/project-manager.ts` | `.ogproj` JSON, recent list, autosave, 15 subfolders |
| Project context | `core/project/projectContext.ts` | Central terrain/GIS/scene state |
| Scene graph | `core/scene/scene-graph.ts` | 3D hierarchy |
| Selection manager | `core/selection/selection-manager.ts` | Selection state |
| Workspace manager | `core/workspace/workspace-manager.ts` | 4 workspaces (see §3) |
| Cache layer | `core/cache/cache-layer.ts` | TTL cache |

`AppContext` (`core/interfaces.ts`) is the DI root exposing 13 services. The
Qt `ApplicationContext` must mirror this surface.

### 2.3 IPC Surface (`shared/ipcChannels-electron.ts`)

~70 typed channels across 11 groups: Native/Export, OpenDRIVE, Road engine,
LaneMaker (core + full), Dialog, OSM, Settings, FS, Job, Notification,
Command, Selection, Workspace, Project. The full list is the contract that the
Qt signal/slot + direct-call layer must replace.

---

## 3. Workspaces (Feature Inventory)

`core/workspace/workspace-manager.ts` defines **4** default workspaces (⚠️
`AGENTS.md` lists only 3 — `train-studio` is missing).

| ID | Name | Modules | Center | Right dock |
|---|---|---|---|---|
| `home` | Home | — | table | — |
| `terrain` | Terrain | terrain, export | map | export-panel |
| `road-studio` | Road Studio | road-studio | road-studio | road-inspector |
| `train-studio` | Train Studio | train-studio | train-studio | — |

### 3.1 Home

- Recent projects list, quick actions, create project.
- `renderer/panels/RecentProjects/RecentProjects.tsx`, `ProjectExplorer.tsx`.
- Project templates / workflow wizard (`WorkflowWizard.tsx`).

### 3.2 Terrain

- Map area selection (shift+drag), TIFF/PNG download via Export panel.
- `modules/terrain/client/MapViewport/MapViewport.tsx` (MapLibre).
- Hooks: `useMapInstance`, `useSelectionOverlay`, `useTileGridOverlay`, `useLabelsVisibility`.
- `LayerStack.tsx`, `SearchBar.tsx`.
- **Server (Node):** `here-elevation-client.ts`, `stadia-elevation-client.ts`.
- **Shared:** `geotiff-writer.ts`, `terrain.ts`.
- ⚠️ The C++ `geoterrain_native` addon intended for this workspace is **stubs
  only** (see §5). Real terrain work is TypeScript.

### 3.3 Road Studio (most important — migrate first)

| File | Role | Size |
|---|---|---|
| `RoadStudioWorkspace.tsx` | Workspace shell | — |
| `RoadViewport.tsx` | 2D (Skia/MapLibre) + 3D (Three.js) viewport | 10.6 KB |
| `SkiaViewport.tsx` | MapLibre + Canvas 2D editor | 40 KB |
| `RoadToolbar.tsx` | Tools, debug toggles, elevation editor | — |
| `RoadInspector.tsx` | Property panel | — |
| `RoadElevationEditor.tsx` | Elevation editing | — |
| `ArcSegment.tsx`, `ConstraintOverlay.tsx` | Overlays | — |
| `snapping.ts` | Snap-to-endpoint | — |
| `skiaArcRenderer.ts` | Arc rendering | — |
| `store/roadStudioStore.ts` | Zustand state (roads, tools, undo/redo, debug, LM bridge) | 28 KB+ |
| `shared/roadEngineClient.ts` | IPC client (the only geometry backend) | 11.4 KB |
| `shared/types.ts` | Road, ControlPoint, Tool, Selection, RoadProfile, geo conv | — |

⚠️ `AGENTS.md` says "MapLibre 2D + Babylon.js 3D". **Babylon.js was removed;
3D is now Three.js** (`three` + `OrbitControls`). 2D is MapLibre + Canvas 2D
(no actual Skia — `canvaskit-wasm` is a dependency but the 2D path uses the
HTML Canvas 2D API inside `SkiaViewport`).

### 3.4 Train Studio (⚠️ not in AGENTS.md)

- 2D track editing (line + arc tools), MapLibre + Canvas 2D.
- `TrainViewport.tsx` (28 KB), `TrainToolbar.tsx`, `TrainStudioWorkspace.tsx`.
- `store/trainStudioStore.ts` (Zustand).
- Shared: `osmRailwayImporter.ts`, `osmCurveSmoother.ts`,
  `networkDefinitionExporter.ts`, `networkDefinitionValidator.ts`,
  `types.ts`.
- Uses `roadEngine.computeCircleArc` (C++) for arc tool.
- Exports a network definition XML (`train-studio-xml.test.ts`).

---

## 4. C++ Road Engine — Detailed (the asset to preserve)

The "road engine" is **three C++ codebases** combined in one N-API bridge.

### 4.1 Codebase A — Original header-only engine (`app/native/src/road/`)

Header-only C++20. Files (sizes in bytes):

| File | Purpose |
|---|---|
| `geometry.hpp` (21 KB) | Point2D, Vec2, intersections, offsets, bezier |
| `geometry_segment.hpp` (26 KB) | GeometrySegment hierarchy (Line/Arc/Spiral/Bezier), SegmentSequence |
| `road.hpp` (16 KB) | Road, ControlPoint, SegmentMetadata, SegmentKind |
| `road_v2.hpp` (9 KB) | RoadV2 segment-based model |
| `road_adapter.hpp` (26 KB) | roadToV2 / roadFromV2 (frozen) |
| `arc.hpp`, `clothoid.hpp` | Arc + Euler spiral |
| `intersection.hpp` (23 KB) | Edge-based junction polygon + fillets |
| `mesh.hpp` (12 KB) | Triangle strip + ear-clip triangulation |
| `opendrive.hpp` (14 KB) | OpenDRIVE XML **export** (no import) |
| `road_tools.hpp` (22 KB) | SCANeR-style creation tools (6 tools) |
| `lane_engine.hpp` (25 KB) | Per-lane geometry (Phase 2.8) |
| `lane_network.hpp` (22 KB) | Per-road lane representation |
| `lane_geometry.hpp`, `lane_sampling.hpp` | Lane sampling |
| `road_mesh_generator.hpp` (41 KB) | Lane-level mesh generation |
| `road_mark_generator.hpp` (15 KB) | Lane markings |
| `lanemaker_curve.hpp`, `skia_arc.hpp`, `st_coords.hpp` | Helpers / s-t coords |
| `geometry_segment_tests.cpp` | doctest: 261 tests, 2210 assertions |
| `constrained_triangulation_tests.cpp` | Constrained triangulation tests |

### 4.2 Codebase B — Refactored public/internal split (`app/native/src/road/road_engine/`)

An in-progress **"road engine separation"** (see `.kiro/specs/road-engine-separation/`)
has produced a clean library layout with a **working CMake build**
(`road_engine/CMakeLists.txt`, target `road_engine::core`):

```
road_engine/
├── public/   ← stable API (road_engine.hpp umbrella + 11 headers)
│   ├── road_engine.hpp      (21 KB umbrella, version 1.0.0)
│   ├── road_error.hpp, geometry.hpp, geometry_segment.hpp
│   ├── road.hpp, road_v2.hpp, road_adapter.hpp
│   ├── arc.hpp, clothoid.hpp, mesh.hpp, opendrive.hpp, road_tools.hpp
└── internal/ ← implementation details
    ├── road_graph.hpp        (road network topology)
    ├── lane_graph.hpp        (lane-to-lane connectivity at junctions, Phase 3.2)
    ├── junction_builder.hpp  (lane-based junction geometry, Phase 3.3)
    ├── constrained_triangulation.hpp
    ├── phase3_tests.cpp, benchmark.cpp
```

Public API surface (from `road_engine.hpp`): `createCircleArc`,
`createClothoidArc`, `createBezierArc`, `sampleCenterline`,
`sampleCenterline3D`, `sampleAtLength`, `sampleLeftEdge`, `sampleRightEdge`,
`generateRoadMesh`, `generateIntersectionMesh`, `triangulatePolygon`,
`generateLaneBoundaries`, `parseRoad`, `serializeRoad`, `exportOpenDrive`,
`roadToV2` (exact + deprecated legacy + auto), `roadFromV2`. Uses
`nlohmann::json` for serialization.

**This is the clean foundation Phase 2 should adopt.** The internal headers
(road_graph, lane_graph, junction_builder) are **not yet frozen** (Phase 3
in progress).

### 4.3 Codebase C — LaneMaker / libOpenDRIVE (`app/native/src/lanemaker/`)

A **separate** C++ library providing the `LM::` and `odr::` namespaces used by
the `lm*` IPC methods. Composed of:

- `lanemaker/libOpenDRIVE/` — a vendored OpenDRIVE library (odr::):
  `OpenDriveMap`, `Road`, `Lane`, `LaneSection`, `RefLine`, `RoadMark`,
  `Junction`, `Mesh`, `RoadNetworkMesh`, `RoutingGraph`, geometries
  (Line/Arc/Spiral/ParamPoly3/CubicSpline/CubicBezier), `odrSpiral` (Fresnel),
  third-party `earcut` + `pugixml`.
- `lanemaker/xodr/` — LaneMaker (LM::): `world`, `road`, `junction`,
  `curve_fitting` (ConnectRays/FitSpiral/FitArcOrLine), `id_generator`,
  `polyline`, `change_tracker`, `road_operation`, `junction_generation`.
- `lanemaker/stubs/` — stubs (map_view_gl, spatial_indexer, validation, etc.)

The `lm*` bridge methods (`lmConnectRays`, `lmCreateRoad`, `lmGetAllMeshes`,
`lmDetectJunctions`, `lmExportOpenDrive`, …) drive this library. The renderer
store (`roadStudioStore.ts`) uses **both** the original engine
(`sampleCenterline`, `computeCircleArc`, geo conv) **and** LaneMaker
(`lmConnectRays` for the road tool, `lmCreateRoad`/meshes/junctions).

### 4.4 N-API Bridge (`app/native/src/road_bridge.cpp`, 110 KB)

Includes headers from **all three** codebases. Implements every `ROAD_*` and
`LM_*` channel. Notable: uses `_set_se_translator` (requires `/EHa`) to guard
`LM::ConnectRays` against SEH segfaults on certain angle combinations — a
sign that LaneMaker has known numerical fragility that must be preserved or
hardened. Built via `road_binding.gyp` / `binding.gyp` target
`road_engine_native` with `ROAD_ENGINE_STANDALONE` define.

### 4.5 Tests

| File | Size | Scope |
|---|---|---|
| `tests/road-engine.test.ts` | 63 KB | 130 TS tests (per AGENTS.md) |
| `tests/road-engine-property.test.ts` | 19.6 KB | fast-check property tests |
| `tests/road-engine-perf.test.ts` | 6.7 KB | performance benchmarks |
| `tests/golden-fixture.test.ts` + `golden_fixtures.ts` | 8 + 11.8 KB | golden output regression |
| `tests/train-studio-xml.test.ts` | 17.5 KB | train network XML export |
| C++ `geometry_segment_tests.cpp` | — | 261 doctest tests / 2210 assertions |

These are **critical regression tests**. The TS tests exercise the engine
through the N-API bridge; in Qt they must be re-pointed at the direct C++ API
(or kept as a separate Node test harness during transition).

---

## 5. Terrain / Export — Critical Finding

⚠️ **The C++ terrain native addon is not real.**

`app/native/src/{addon,session_bridge,datasource_bridge,pipeline_bridge}.cpp`
form `geoterrain_native.node`. `binding.gyp` links
`../../../build/bin/rts_{core,datasources,pipeline,cache,session}.lib` and
`../../../third_party/gdal/lib/gdal_i.lib`. **None of these paths exist in this
repo** (`third_party/` is empty; there is no `build/bin/`). The
`datasource_bridge` and `pipeline_bridge` are explicitly TODO stubs. `main.ts`
loads this addon in a try/catch and **silently ignores failure**.

**Real terrain/export is TypeScript** in `modules/export/server/`:

| File | Role |
|---|---|
| `exportEngine.ts` | Orchestrator |
| `tileMath.ts` | Tile coords + validation |
| `downloader.ts` | HTTPS download w/ retry + redirect + bounded parallelism |
| `demFetcher.ts` | OpenTopo, NASA Earthdata (Copernicus), GPXZ, Terrarium/Mapbox DEM |
| `gladClient.ts` | GLAD ARD imagery + SRTM |
| `imageProcessor.ts` | Merge/crop/resize, elevation metadata |
| `formatWriter.ts` | Heightmap PNG/R16/GeoTIFF(Int16/Uint16/Float32), Albedo PNG/GeoTIFF |
| `types.ts` | Export types, constants, cancellation |

Dependencies: `sharp` (native image), `geotiff` (GeoTIFF read), `@turf/turf`
(geo), raw `https`. DEM sources: OpenTopography (API key), NASA Earthdata
(Copernicus GLO-30), GPXZ, GLAD SRTM, Terrarium/Mapbox tiles. Imagery: Esri
World Imagery (MapLibre), GLAD ARD.

**Implication:** Terrain/export migration is a **full C++ rewrite**, not a
preservation task. Required C++ libs: a TIFF/GeoTIFF library (libtiff + GeoTIFF
tags, or GDAL), libpng (or stb_image_write), libcurl (HTTP), nlohmann::json.
This is a substantial workstream comparable in size to the road engine port.

---

## 6. Renderer / UI Inventory

### 6.1 Shell (`renderer/shell/`)

`DockShell`, `IconRail`, `ResizableDock`, `SplitView`, `StatusBar`, `Toolbar`,
`WorkspaceTabs`, `CommandPalette`, `WorkflowBanner`. Panel system:
`registry/panelRegistry.ts` + `panelLayoutStore.ts` (persisted layout) +
`registerPanels.ts`. Common components: `ContextMenu`, `EmptyState`,
`FormFields`, `PanelError`, `PanelHeader`, `Spinner`, `ErrorBoundary`,
`SettingsDialog`, `Toast`, `WorkflowWizard`.

### 6.2 Renderer core bridges (`renderer/core/`)

`coreService.ts`, `coreStore.ts` (Zustand), `geocoding-service.ts`, `ipc.ts`
(generic IPC bridge), `sceneService.ts`, `store.ts` (terrain store),
`undoRedoBridge.ts`. Hooks: `useFocusTrap`, `useKeyboardShortcuts`.

### 6.3 Styling

Tailwind 4 + Emotion/MUI 7 + Lucide icons. Dark theme (`theme/darkTheme.ts`).

### 6.4 Rendering tech actually in use

| Layer | Technology |
|---|---|
| 2D map | MapLibre GL 5.24 (Esri World Imagery) |
| 2D road/track overlay | HTML Canvas 2D API (inside SkiaViewport/TrainViewport) |
| 3D road | Three.js 0.160 + OrbitControls |
| Image processing | sharp (libvips), geotiff |
| Canvas kit | canvaskit-wasm (dependency; verify actual usage) |

---

## 7. Migration Matrix

| # | Existing Component | Current Tech | Qt/C++ Replacement | Reuse? | Effort | Risk |
|---|---|---|---|---|---|---|
| 1 | Electron main | Electron | `QApplication` + `QMainWindow` | No | S | Low |
| 2 | React UI | React 19 + MUI/Tailwind | **Qt Widgets** (recommended, §10) | No | XL | Med |
| 3 | Zustand stores | Zustand | C++ app state + Qt signals | No | M | Low |
| 4 | Electron IPC (~70 channels) | ipcMain/ipcRenderer | Direct C++ calls + Qt signals/slots | No | M | Low |
| 5 | Preload bridge | contextBridge | Eliminated (direct) | No | S | Low |
| 6 | Road engine (Codebase A+B) | C++ header-only | **Preserve** as CMake lib | **Yes** | M | Med |
| 7 | LaneMaker/libOpenDRIVE (Codebase C) | C++ | **Preserve** as CMake lib | **Yes** | M | **High** (SEH fragility) |
| 8 | N-API road bridge | N-API (110 KB) | C++ `RoadEngineService` (direct) | Rewrite | M | Med |
| 9 | Terrain native addon | C++ stubs | **Not preserved** — rewrite pipeline in C++ | No | XL | **High** |
| 10 | Terrain/export pipeline | TypeScript + sharp/geotiff | C++ + libtiff/libpng/libcurl | No | XL | **High** |
| 11 | MapLibre 2D | MapLibre GL JS | Qt-native map widget (§11) | No | L | **High** |
| 12 | Three.js 3D | Three.js | Qt OpenGL/QRhi (§11) | No | L | Med |
| 13 | Project manager | TypeScript | C++ `ProjectManager` (keep `.ogproj` format) | Port | M | Low |
| 14 | Workspace manager | TypeScript | C++ `WorkspaceManager` (4 workspaces) | Port | S | Low |
| 15 | Core DI/events/commands/jobs/cache | TypeScript | C++ equivalents | Port | L | Low |
| 16 | Undo/redo | Zustand snapshots | Command pattern (`ICommand`) | Redesign | M | Low |
| 17 | Train Studio | React + TS | Qt Widgets + C++ (uses road arc engine) | Port | L | Med |
| 18 | Vite build | Vite 8 | CMake | No | S | Low |
| 19 | node-gyp / N-API | node-gyp | Eliminated | No | S | Low |
| 20 | Tests (TS + doctest) | vitest + doctest | Qt Test + doctest (keep doctest) | Partial | M | Low |
| 21 | Packaging | electron-builder | Qt Installer Framework / CPack | No | M | Low |

Effort: S=Small, M=Medium, L=Large, XL=Extra-large.

---

## 8. What Can Be Reused Directly

1. **Codebase A + B (road engine)** — header-only C++20, already has a CMake
   target (`road_engine::core`). Strip N-API includes; link directly from Qt.
   The public API (`road_engine.hpp`) is the integration surface.
2. **Codebase C (LaneMaker/libOpenDRIVE)** — vendored C++ library. Reusable
   but needs its own CMake target and the SEH guard logic preserved/hardened.
3. **C++ doctest tests** (`geometry_segment_tests.cpp`) — run unchanged.
4. **Project file format** (`.ogproj` JSON + 15 subfolders) — keep verbatim
   for backward compatibility; C++ reads/writes the same JSON. (See §8.2
   for schema status.)
5. **OpenDRIVE export** (`opendrive.hpp`) — unchanged.
6. **Road data model & geo conversion** — the `Road`/`ControlPoint`/
   `SegmentMetadata` schema and lat/lon↔local-meters conversion are the wire
   format; preserve to keep project files and engine behavior identical.

### 8.1 License Audit — LaneMaker / libOpenDRIVE (BLOCKER)

Codebase C is vendored from two upstream open-source projects. Their
licenses were verified by inspecting the source and confirming against the
upstream repositories:

| Component | Upstream | License | Verified |
|---|---|---|---|
| libOpenDRIVE (`lanemaker/libOpenDRIVE/`) | `pageldev/libOpenDRIVE` | **Apache-2.0** | GitHub repo + FreeBSD port (`LICENSE=APACHE20`) |
| LaneMaker xodr (`lanemaker/xodr/`) | `guotata1996/lanemaker` | **Apache-2.0** | GitHub repo LICENSE file |
| pugixml (`lanemaker/libOpenDRIVE/thirdparty/pugixml/`) | `pugixml` 1.10 | **MIT** | `readme.txt` in vendored copy |
| earcut (`lanemaker/libOpenDRIVE/thirdparty/earcut/`) | `mapbox/earcut` | **ISC** (header) | — |

OpenGeoStudio's `package.json` declares `"license": "MIT"`.

**Compatibility assessment:**

- **Apache-2.0 is compatible with MIT distribution.** OpenGeoStudio may
  ship Apache-2.0 code in its binary and source distribution. However,
  Apache-2.0 imposes obligations that MIT does not:
  1. **Preserve the LICENSE file** for the Apache-2.0 portions.
  2. **Preserve any NOTICE file** (if the upstream projects ship one).
  3. **State any modifications** to Apache-2.0 files (a prominent notice in
     modified files is required).
  4. **Patent grant** — Apache-2.0 includes an explicit patent retaliation
     clause that MIT lacks. This is generally beneficial (more protection)
     but means the Qt app's overall license is effectively MIT + Apache-2.0
     for the LaneMaker/libOpenDRIVE portions, not pure MIT.

- **Current compliance gap (exists today, must be fixed before shipping):**
  The vendored copies in this repo contain **no LICENSE file, no NOTICE
  file, and no per-file license headers**. The git commit that introduced
  them (`4f4fe5c`) copied source only, omitting license artifacts. This is
  an existing compliance violation that must be remediated regardless of
  the migration.

**NOTICE file status — CONFIRMED (2026-08-12):**

Both upstream repositories were inspected directly on GitHub:

| Repository | Has `LICENSE`? | Has `NOTICE`? | Root files |
|---|---|---|---|
| `pageldev/libOpenDRIVE` | ✅ Apache-2.0 | ❌ **No NOTICE file** | `.devcontainer`, `.github/workflows`, `include`, `src`, `tests`, `.clang-format`, `.gitignore`, `CMakeLists.txt`, `LICENSE`, `OpenDriveConfig.cmake.in`, `README.md`, `format-files.sh` |
| `guotata1996/lanemaker` | ✅ Apache-2.0 | ❌ **No NOTICE file** | `.github/workflows`, `cereal`, `engine`, `libOpenDRIVE-master`, `test`, `traffic`, `ui`, `util`, `widgets`, `xodr`, `.gitignore`, `.gitmodules`, `CMakeLists.txt`, `LICENSE` |

**Apache-2.0 §4(d) implication:** Section 4(d) requires preserving NOTICE
file contents *only if the original Work includes a NOTICE text file as
part of its original distribution*. Since **neither upstream project ships
a NOTICE file**, §4(d) obligations are **not triggered**. The remediation
only requires:

**Required actions (blockers before any public Qt release):**

1. Add `LICENSE` file from `pageldev/libOpenDRIVE` to
   `app/native/src/lanemaker/libOpenDRIVE/`.
2. Add `LICENSE` file from `guotata1996/lanemaker` to
   `app/native/src/lanemaker/xodr/`.
3. Add MIT license text for pugixml (already partially present in
   `readme.txt`; add a formal `LICENSE` file in
   `app/native/src/lanemaker/libOpenDRIVE/thirdparty/pugixml/`).
4. Add ISC license text for earcut in
   `app/native/src/lanemaker/libOpenDRIVE/thirdparty/earcut/`.
5. Document the combined license in the Qt app's top-level license file:
   "MIT (application) + Apache-2.0 (libOpenDRIVE, LaneMaker) + MIT
   (pugixml) + ISC (earcut)".
6. If any Apache-2.0 files are modified during the migration, add a
   prominent modification notice per Apache-2.0 §4(b).
7. ~~Preserve NOTICE files~~ — **not required** (none exist upstream).

> **This is a blocker, not a Phase 6 item.** The license remediation is
> assigned to Phase 2a (§14) so the Qt app is distributable from the first
> build onward.

### 8.2 Project File Schema (`.ogproj`)

**There is no formal JSON schema for `.ogproj` files.** No `.schema.json`,
no JSON Schema document, and no example `.ogproj` file exists in the repo.
The "schema" is implicitly defined by two TypeScript interfaces:

1. **`Project` interface** — `core/project/project-manager.ts`:
   `id`, `name`, `filePath?`, `basePath?`, `createdAt`, `modifiedAt`,
   `bounds?`, `workspaceId`, `moduleState` (Record<string, unknown>),
   `dirty`. Plus `PROJECT_SUBFOLDERS` (15 subfolders) and
   `PROJECT_FILE_EXT = '.ogproj'`.

2. **Road data inside `moduleState['road-studio']`** —
   `modules/road-studio/shared/types.ts`: `Road[]` where each `Road` has
   `id`, `name`, `points: ControlPoint[]`, `width`, `laneCount`, `color`,
   `profile: RoadProfile`, `startIntersectionId`, `endIntersectionId`,
   `formatVersion?` (1=legacy, 2=with segmentMeta). `ControlPoint` has
   `id`, `lat`, `lon`, `z`, `handleIn/handleOut: Vec2|null`, `type`,
   `segmentMeta?: SegmentMetadata|null`.

**This is the interop contract between the old and new app during
transition.** Before Phase 2 begins:

1. **Extract a formal JSON Schema** (Draft 2020-12) from these TS
   interfaces. This becomes the validation contract for the C++
   `ProjectManager` and ensures old `.ogproj` files load in the Qt app
   and vice versa.
2. **Pin `formatVersion`** — the Road schema has v1 (legacy) and v2
   (with `segmentMeta`). The C++ `parseRoad`/`serializeRoad` already
   handles both; the Qt `ProjectManager` must do the same.
3. **Document `moduleState` keys** — each workspace stores its state
   under a key in `moduleState`. The known keys are: `road-studio`
   (roads array), `train-studio` (tracks array), `terrain` (bounds,
   export settings). Unknown keys must be preserved as opaque JSON
   (forward compatibility).

> **Phase 2 deliverable:** a `docs/ogproj-schema.json` file extracted
> from the TS interfaces, validated against the existing `parseRoad`/
> `serializeRoad` C++ round-trip tests.

## 9. What Must Be Rewritten

1. **Entire UI** (React → Qt Widgets).
2. **All IPC** (Electron channels → direct C++ service calls + Qt signals).
3. **Terrain + export pipeline** (TypeScript → C++ with libtiff/libpng/libcurl).
4. **Core framework** (DI, events, commands, jobs, cache, selection,
   notifications, scene graph, layer system) — port TS designs to C++.
5. **Undo/redo** — redesign as command pattern (see §9.1 for rationale).
6. **2D map + 3D viewport** — replace MapLibre/Three.js with Qt-native
   rendering (§11).
7. **Build system** — Vite/node-gyp → CMake + vcpkg.
8. **TS tests** — re-point at C++ API (or keep Node harness temporarily).

### 9.1 Undo/Redo Redesign Rationale

The current Zustand store uses **full-state snapshots**: every mutation
calls `pushHistory()` which deep-clones the entire `roads[]` array via
`JSON.parse(JSON.stringify(roads))` and pushes it onto `undoStack`. This
works for the TS/Electron app but is the wrong design for the C++/Qt port
for three reasons:

1. **Memory cost for large road networks.** A road network with 100 roads
   × 50 control points × 8 bytes/field × ~10 fields per point ≈ 400 KB per
   snapshot. With a 50-step undo stack that's ~20 MB of snapshots held in
   memory — all deep-cloned on the JS heap. In C++, the same data is
   compact, but the principle holds: storing N full copies of the document
   is O(N × document_size) when it could be O(N × delta_size). The command
   pattern stores only the delta (e.g. "move control point 3 of road 7
   from (x1,y1) to (x2,y2)") — O(1) per command regardless of network size.

2. **Architectural mismatch with C++ ownership.** Zustand snapshots work
   because JS has GC and `JSON.parse(JSON.stringify())` is a one-liner. In
   C++, deep-copying a road network with `GeometrySegment` polymorphism,
   `shared_ptr` lane graphs, and `LaneMaker::World` state is non-trivial —
   there is no universal `clone()` and getting it wrong leads to
   use-after-free or dangling pointers. The command pattern avoids this
   entirely: each command holds only the minimal state needed to
   apply/revert its specific mutation.

3. **Coarse granularity.** Snapshot-based undo reverts the entire document
   to a previous state, which means a user who moved one point and then
   changed a road profile gets both reverted together. The command pattern
   enables fine-grained undo (revert just the profile change) and command
   composition (group multiple micro-operations into one undoable unit).

The command pattern (`ICommand` with `execute()`/`undo()`) is the standard
approach in C++/Qt desktop applications (Qt itself provides
`QUndoCommand`/`QUndoStack` which can be leveraged directly). This is a
deliberate architectural improvement, not just a port.

### 9.2 CI / Build Baseline (Current State)

**There is no automated CI pipeline.** The repository has:

- ❌ No `.github/workflows/` directory (no GitHub Actions)
- ❌ No `.gitlab-ci.yml`, `azure-pipelines.yml`, `.circleci/`, `appveyor.yml`,
  or any other CI config file
- ❌ No git hooks (`.git/hooks/` contains only `.sample` files)
- ❌ No `husky`, `lint-staged`, or pre-commit configuration in `package.json`
- ✅ `npm test` script (runs `vitest --run`) — **manual only**
- ✅ `npm run lint` script (runs `eslint`) — **manual only**
- ✅ `npx node-gyp build` for the C++ addon — **manual only**
- ✅ C++ doctest tests compiled into `geometry_segment_tests.cpp` — run via
  CMake build or node-gyp, **manual only**

**What "green" looks like today (manual baseline):**

| Check | Command | Expected result |
|---|---|---|
| TS road-engine tests | `npm test` (vitest) | 130 tests pass (per AGENTS.md; commit `4f4fe5c` says "176 tests") |
| C++ doctest tests | Build `geometry_segment_tests.cpp` | 261 tests / 2210 assertions pass |
| Lint | `npm run lint` | No errors |
| Native addon build | `npm run build:road-engine` | `road_engine_native.node` produced |
| Full Electron build | `npm run build` | `dist/` + `dist-electron/` produced |

### 9.3 Test Baseline — RESOLVED (2026-08-12)

The test-count discrepancy is now resolved. `npm test` was run and the full
output saved to `docs/baseline-test-results.txt`. The actual count is
**986 tests, all passing** — neither 130 (AGENTS.md) nor 176 (commit
`4f4fe5c`) was correct.

**Per-file breakdown (the Phase 7 regression oracle):**

| Test file | Tests | Duration |
|---|---|---|
| `tests/golden-fixture.test.ts` | 72 | 44 ms |
| `tests/road-engine-perf.test.ts` | 6 | 194 ms |
| `tests/road-engine-property.test.ts` | 787 | 180 ms |
| `tests/road-engine.test.ts` | 95 (+1 perf) | 404 ms |
| `tests/train-studio-xml.test.ts` | 26 | 32 ms |
| **Total** | **986** | ~9.9 s |

**This is the canonical baseline.** The Qt app's Qt Test mirror suite must
produce 986 equivalent test results before Phase 8 (Remove Web Stack) can
begin. The `AGENTS.md` claim of "130 tests" should be corrected to 986.

> The earlier discrepancy likely arose because `AGENTS.md` was written
> before the property tests (787) and golden fixtures (72) were added, and
> commit `4f4fe5c`'s "176" was a snapshot before the property test suite
> was expanded.

---

## 10. Qt Technology Choice: Qt Widgets (Recommended)

Per Phase 5 requirement to evaluate Qt Widgets vs QML:

| Criterion | Qt Widgets | Qt Quick/QML |
|---|---|---|
| Complex desktop tools / property panels / docking | **Excellent** (QDockWidget, QTreeView, property browsers) | Adequate but more work |
| Engineering/technical workflow fit | **Excellent** | Good |
| Keyboard/mouse interaction, large datasets | **Excellent** (model/view, QAbstractItemModel) | Good |
| Performance for dense road networks | **Excellent** (native C++ widgets) | Good (scene graph) |
| Maintainability for C++-first codebase | **Excellent** (no JS layer) | Adds QML/JS layer |
| 3D integration | QOpenGLWidget / QRhi window | Qt Quick 3D (good but separate paradigm) |
| Long-term fit for a C++-only app | **Excellent** | Introduces a second language |

**Decision: Qt Widgets.** Rationale: the migration's explicit goal is a
C++-only application with no JS runtime; QML reintroduces a JS/declarative
layer and a second language to maintain. OpenGeoStudio is a dense engineering
tool (dockable panels, property inspectors, debug layer toggles, large road
networks) — the classic Qt Widgets sweet spot. 3D will be handled by a
QOpenGLWidget/QRhi widget embedded in the dock layout, not Qt Quick 3D.

> This decision should be confirmed by the user before Phase 5 implementation.

---

## 11. Rendering Strategy (Open Question — needs decision)

The 2D editor currently relies on **MapLibre GL JS** (WebGL) for satellite
imagery + pan/zoom, with a Canvas 2D overlay for roads. 3D uses Three.js.
None of these are Qt-native. Options:

| Option | 2D map | 3D | Pros | Cons |
|---|---|---|---|---|
| A | **Tangram-es / QMapLibreCore** (MapLibre Native) | QOpenGLWidget + custom GL | Native MapLibre, keeps imagery UX | MapLibre Native C++ bindings are less mature; extra dep |
| B | QGraphicsView + custom tile fetcher (Esri tiles) | QOpenGLWidget + custom GL | Pure Qt, no maplibre dep | Must reimplement tile math/zoom/pan (already have `tileMath.ts`) |
| C | Embed Marble / QGIS qgsquick | Qt Quick 3D | Heavy geo libs | Overkill, different paradigms |
| D | Qt WebEngine + MapLibre JS | Qt WebEngine + Three.js | Fastest port | **Violates Rule 2/3** (reintroduces web runtime) — rejected |

**Recommendation:** Option A (MapLibre Native via QMapLibreCore or
tangram-es) for 2D to preserve the satellite-imagery editing UX, with a
QOpenGLWidget/QRhi-based 3D viewport consuming C++ engine mesh output
directly. If MapLibre Native proves too immature, fall back to Option B
(QGraphicsView + the existing tile math ported to C++ + libcurl tile fetch).

### 11.1 Rendering Spike — Exit Criteria & Fallback Trigger (Phase 2c)

The rendering spike (Phase 2c) must produce a **go/no-go decision** on the
2D map approach before Phase 4 (Road Studio) begins. The spike has a
**hard timebox of 2 weeks** from the start of Phase 2c. If the criteria
below are not met by the deadline, the fallback (Option B) is automatically
adopted — no extension.

**Spike deliverable:** A minimal Qt widget that:
1. Displays Esri World Imagery satellite tiles for a user-visible area.
2. Supports pan (drag) and zoom (scroll wheel) with smooth interaction.
3. Overlays a Canvas/QPainter layer drawing a simple polyline in local
   meter coordinates (the road rendering primitive).
4. Converts mouse clicks to local meter coordinates (the editing primitive).

**Exit criteria for Option A (MapLibre Native) — ALL must be met:**

| # | Criterion | Measurement |
|---|---|---|
| A1 | QMapLibreCore (or equivalent) builds against Qt 6 with CMake on Windows | Successful `cmake --build` with no patches to maplibre source |
| A2 | Satellite tile rendering works with Esri World Imagery URL template | Tiles visible in the widget |
| A3 | Pan/zoom interaction is smooth (≥ 30 FPS) at zoom levels 8–18 | Visual inspection; no stutter |
| A4 | Custom overlay layer draws on top of the map without flicker | QPainter or custom draw callback works |
| A5 | Mouse-to-local-meter coordinate conversion is accurate (< 1 m error) | Compare against known lat/lon reference point |
| A6 | No blocking calls on the UI thread during tile load | Tiles load asynchronously; UI stays responsive |

**Fallback trigger (automatic):** If **any** of A1–A6 is not met by the
2-week deadline, Option A is abandoned and Option B (QGraphicsView +
libcurl tile fetcher + ported `tileMath.ts`) is adopted. The fallback is
**not** a failure — it is the pre-agreed contingency. Option B has no
external map library dependency and is lower-risk; it just requires
porting the existing `tileMath.ts` (sliding-map tile math) to C++.

**Exit criteria for Option B (QGraphicsView) — if fallback is triggered:**

| # | Criterion | Measurement |
|---|---|---|
| B1 | `QGraphicsView` + `QGraphicsScene` renders tiled background | Esri tiles painted as pixmap items |
| B2 | Pan/zoom with scroll wheel and drag works | Standard QGraphicsView navigation |
| B3 | Tile fetch via libcurl is async (QNetworkAccessManager or thread pool) | No UI freezing |
| B4 | `tileMath.ts` ported to C++ produces correct tile X/Y for lat/lon/zoom | Unit test matches TS output |

> **Rationale for the hard deadline:** Road Studio (Phase 4) cannot begin
> until the 2D rendering approach is decided. An open-ended spike would
> block the entire migration. The 2-week timebox forces a decision and
> guarantees Phase 4 can start on schedule.

---

## 12. Risks & Unknowns

| # | Risk / Unknown | Severity | Mitigation |
|---|---|---|---|
| R1 | LaneMaker `ConnectRays` SEH fragility (needs `/EHa` + `_set_se_translator`) | High | Preserve guard in C++ service; add fuzz tests; consider hardening algorithm |
| R2 | MapLibre Native C++ maturity for 2D editor | High | Phase 2c spike (2-week timebox, §11.1); fallback to QGraphicsView |
| R3 | Terrain pipeline full C++ rewrite (no existing C++ impl) | High | Phase 6; use libtiff+libpng+libcurl; port `tileMath`/`downloader`/`formatWriter` logic directly |
| R4 | Three C++ codebases with overlapping types (Road vs RoadV2 vs odr::Road vs LM::Road) | Med | Keep boundaries explicit; C++ service layer translates at edges |
| R5 | Internal road_engine headers (road_graph, lane_graph, junction_builder) not frozen | Med | Treat as unstable until Phase 3 of engine separation completes; service layer wraps them |
| R6 | ~70 IPC channels to map to direct calls/signals | Med | Mechanical; produce a channel→method mapping table in Phase 3 |
| R7 | TS tests (130) currently go through N-API; can't run against Qt directly | Med | Keep Node+vitest harness alive during transition for engine regression; add Qt Test equivalents |
| R8 | `canvaskit-wasm` listed as dep but 2D uses Canvas 2D API — verify Skia need | Low | Confirm in Phase 2; if no real Skia use, drop |
| R9 | Project autosave + recent-projects persistence paths tied to Electron `userData` | Low | Use `QStandardPaths::AppDataLocation` in Qt |
| R10 | Plugin loader (`plugins/` dir) — no plugins exist yet | Low | Design C++ plugin ABI in Phase 2/3 per user decision (§15); use QPluginLoader |
| R11 | Geo coordinate conversion (lat/lon↔local meters) duplicated in TS and C++ | Low | Consolidate into C++ `geoToLocal`/`localToGeo`; remove TS copies |

---

## 13. Proposed Qt Architecture

```
OpenGeoStudio (C++20 / Qt 6 / CMake / vcpkg)
│
├── app/                       ← Qt application
│   ├── main.cpp
│   ├── Application            ← QApplication bootstrap
│   ├── MainWindow             ← QMainWindow + QDockWidget layout
│   ├── ApplicationContext     ← DI root (mirrors core/interfaces.ts)
│   └── SettingsDialog
│
├── core/                      ← framework (port of core/*)
│   ├── Project, ProjectManager        ← .ogproj JSON (format-compatible)
│   ├── WorkspaceManager               ← 4 workspaces
│   ├── CommandManager, ICommand       ← command pattern + undo/redo
│   ├── EventBus, Logger, JobManager
│   ├── SelectionManager, CacheLayer
│   └── GeoConversion                  ← lat/lon ↔ local meters (single source)
│
├── road/                      ← Road Studio
│   ├── engine/                ← Codebase A+B (preserved, CMake lib)
│   ├── lanemaker/             ← Codebase C (preserved, CMake lib)
│   ├── RoadEngineService      ← replaces N-API bridge (direct C++ calls)
│   ├── RoadStudioWidget, RoadViewport2D, RoadViewport3D
│   ├── RoadToolbar, RoadInspector, RoadElevationEditor
│   ├── RoadToolController, RoadSelection, RoadDebugRenderer
│   └── commands/              ← RoadCommand, RoadEditCommand, RoadSplitCommand,
│                                IntersectionCommand, ElevationCommand, ...
│
├── train/                     ← Train Studio (port)
│   ├── TrainStudioWidget, TrainViewport, TrainToolbar
│   ├── TrainStudioState, OsmRailwayImporter, NetworkDefinitionExporter
│
├── terrain/                   ← Terrain (rewrite in C++)
│   ├── TerrainViewport        ← map widget (MapLibre Native or QGraphicsView)
│   ├── DemFetcher             ← libcurl: OpenTopo/NASA/GPXZ/Terrarium
│   ├── ImageryFetcher         ← Esri tiles / GLAD ARD
│   ├── GeoTiffWriter, PngWriter, HeightmapWriter
│   └── ExportEngine           ← orchestrator (port of exportEngine.ts)
│
├── export/                    ← Export panel + format writers
│   └── ExportPanel, FormatWriter (R16/PNG/GeoTIFF Int16/Uint16/Float32)
│
├── ui/                        ← shared Qt Widgets (Home, common widgets)
│   ├── home/ (RecentProjects, ProjectExplorer)
│   └── common/ (PanelHeader, Spinner, ErrorBoundary, Toast, ContextMenu)
│
└── tests/                     ← Qt Test + doctest
    ├── road_engine_tests (doctest, preserved)
    └── *_tests (Qt Test, new)
```

### 13.1 RoadEngineService (replaces N-API bridge + IPC handler + client)

```cpp
class RoadEngineService : public QObject {
    Q_OBJECT
public:
    explicit RoadEngineService(QObject* parent = nullptr);
    // Direct C++ calls — no IPC. Mirrors roadEngineClient.ts API.
    QString getVersion() const;
    CircleArc computeCircleArc(Point2D start, Vec2 startDir, Point2D end, int segments = 8);
    std::vector<Point3D> sampleCenterline(const Road& road, int n = 24);
    // LM:: bridge
    LmRoadResult lmCreateRoad(...);
    LmMeshResult lmGetAllMeshes();
    std::vector<LmJunctionResult> lmDetectJunctions();
    QString lmExportOpenDrive();
signals:
    void roadChanged(const QString& roadId);
    void operationStarted(const QString& op);
    void operationFinished(const QString& op);
    void errorOccurred(const QString& message);
};
```

### 13.2 Command-based undo/redo

```cpp
class ICommand { public: virtual ~ICommand()=default;
    virtual void execute()=0; virtual void undo()=0;
    virtual QString description() const = 0; };
class RoadEditCommand : public ICommand { /* mutate control point */ };
class RoadSplitCommand : public ICommand { ... };
class IntersectionCommand : public ICommand { ... };
class ElevationCommand : public ICommand { ... };
class UndoStack { void push(std::unique_ptr<ICommand>); void undo(); void redo(); };
```

---

## 14. Phased Implementation Plan (with complexity/risk)

| Phase | Scope | Complexity | Risk | Depends on | Deliverable |
|---|---|---|---|---|---|
| **1 — Audit** | This document | S | Low | — | Migration matrix + plan (✅ this) |
| **2a — Compliance & Baseline** | License remediation (§8.1 — add LICENSE files for libOpenDRIVE, LaneMaker, pugixml, earcut; document combined licensing); `.ogproj` JSON Schema extraction (§8.2); **test baseline capture** (§9.3 — ✅ done, 986 tests); C++ plugin ABI design (§15 #6, using QPluginLoader). | M | Low | Phase 1 ✅ | LICENSE files + `docs/ogproj-schema.json` + `docs/baseline-test-results.txt` (✅) + plugin ABI spec |
| **2b — Engine Extraction** | Promote road_engine + lanemaker to standalone CMake libs (vcpkg); strip N-API includes; add `RoadEngineService` skeleton; port `GeoConversion`, `ProjectManager`, `WorkspaceManager`, core DI/events/commands to C++; CMake app skeleton builds an empty Qt window. | L | Med | Phase 1 ✅ | CMake libs + empty Qt app linking the engine |
| **2c — Rendering spike** | Validate MapLibre Native (or QGraphicsView fallback) for 2D + QOpenGLWidget for 3D in Qt. **2-week hard timebox; automatic fallback to Option B if exit criteria (§11.1) not met.** | M | **High** | Phase 2b (CMake + Qt window builds) | Go/no-go on 2D map approach |
| **3 — Qt App Skeleton** | `QApplication`, `MainWindow`, docking, `ApplicationContext`, logging, settings, command palette, workspace tabs. | L | Low | Phase 2b | Navigable empty shell with 4 workspaces |
| **4 — Road Studio** | Viewport (2D+3D), toolbar, tools, engine integration via `RoadEngineService`, selection, editing, intersections, debug layers (Ctrl+Shift+G), command-based undo/redo, OpenDRIVE export. | **XL** | Med | Phase 2b (`RoadEngineService` stable), Phase 2c (rendering decision), Phase 3 (shell + docking) | Feature-parity Road Studio |
| **5 — Train Studio** | Port track editor + OSM importer + XML export. | L | Med | **Phase 4** (`RoadEngineService.computeCircleArc` must be stable — Train Studio calls it for the arc tool) | Feature-parity Train Studio |
| **6 — Terrain + Export** | C++ DEM/imagery fetch (libcurl), GeoTIFF/PNG/R16 writers, export panel. | **XL** | **High** | Phase 2c (rendering decision — terrain viewport needs the same 2D map widget), Phase 3 (shell + export panel docking) | Feature-parity Terrain + Export |
| **7 — Validation** | Side-by-side old vs new; geometry/topology/mesh/terrain/export/undo/shortcut parity; perf benchmarks. Compare against baseline (§9.3). | L | Low | Phases 4, 5, 6 | Validation report |
| **8 — Remove Web Stack** | Delete Electron/React/Node/Vite/N-API/node-gyp. | M | Low | Phase 7 (validation passed) | Pure C++/Qt repo |
| **9 — Packaging** | Qt IFW/CPack installers for Windows-first (Linux/macOS later per §15 #7). | M | Low | Phase 8 | Windows installer |

### 14.1 Phase Dependency Graph

```
Phase 1 (Audit) ✅
    │
    ├──► Phase 2a (Compliance & Baseline) ──────────────────────┐
    │       • License remediation (§8.1)                        │
    │       • .ogproj JSON Schema (§8.2)                        │
    │       • Test baseline ✅ (§9.3 — 986 tests)               │
    │       • Plugin ABI design (§15 #6)                        │
    │       [does NOT gate 2b — runs in parallel]               │
    │                                                           │
    └──► Phase 2b (Engine Extraction)                           │
            │  • CMake libs (road_engine + lanemaker)           │
            │  • RoadEngineService skeleton                     │
            │  • Empty Qt window                                │
            │                                                   │
            ├──► Phase 2c (Rendering spike, 2-week timebox)     │
            │       │                                           │
            │       └──► [fallback to Option B if §11.1         │
            │             exit criteria not met]                │
            │                                                   │
            ▼                                                   │
    Phase 3 (Qt App Skeleton) ◄── rendering decision (P2c)      │
            │                                                   │
            ▼                                                   │
    Phase 4 (Road Studio) ◄── requires: RoadEngineService (P2b) │
    │       │               + rendering (P2c) + shell (P3)      │
    │       │                                                   │
    │       ├──► Phase 5 (Train Studio) ◄── HARD DEP on P4:     │
    │       │       computeCircleArc                            │
    │       │                                                   │
    │       └──► Phase 6 (Terrain + Export) ◄── requires:       │
    │               rendering (P2c) + shell (P3)                │
    │                       │                                   │
    │                       ▼                                   │
    │                  Phase 7 (Validation) ◄── P4 + P5 + P6    │
    │                       │                                   │
    │                       ▼                                   │
    │                  Phase 8 (Remove Web Stack) ◄── P7 passed │
    │                       │                                   │
    │                       ▼                                   │
    │                  Phase 9 (Packaging) ◄── P8               │
    │                                                           │
    └───────────────────────────────────────────────────────────┘
```

**Key dependency notes:**

- **Phase 2a and 2b are parallel — neither gates the other.** 2a is
  compliance/schema/baseline housekeeping; 2b is engine/build extraction.
  Both start from Phase 1 and proceed independently. 2a's deliverables
  (LICENSE files, schema, plugin ABI) are needed before Phase 9
  (Packaging) but do not block 2b, 2c, 3, 4, 5, or 6.
- **Phase 2c → Phase 2b (hard):** The rendering spike needs a Qt window
  to test rendering in, which requires Phase 2b's CMake + Qt skeleton.
  Once the empty Qt window builds, 2c can start — it does not need 2b
  to be fully complete.
- **Phase 5 → Phase 4 (hard):** Train Studio's arc tool calls
  `roadEngine.computeCircleArc()` (C++ engine via IPC today, via
  `RoadEngineService` in Qt). The `RoadEngineService` API must be frozen
  and tested in Phase 4 before Train Studio can integrate. If
  `RoadEngineService` changes during Phase 5, Train Studio breaks.
- **Phase 6 → Phase 2c (hard):** The Terrain workspace needs the same 2D
  map widget as Road Studio. The rendering decision (Phase 2c) must be
  made before Terrain's viewport can be built.
- **Phase 6 → Phase 3 (soft):** The Export panel is a dockable widget;
  it needs the docking shell from Phase 3 but can be developed in
  parallel with Phase 4/5 if the shell is ready.
- **Phase 7 → Phases 4, 5, 6 (hard):** Validation requires all three
  feature workspaces to be feature-complete.
- **Phase 8 → Phase 7 (hard):** The web stack cannot be removed until
  validation confirms the Qt app is at feature parity. Removing it early
  destroys the reference implementation.

**Suggested first implementation step (after approval):** Kick off Phase 2a
and Phase 2b in parallel. 2a starts with license remediation (adding
LICENSE files — mechanical, low-risk) and `.ogproj` schema extraction.
2b starts with the CMake application skeleton that links the existing
`road_engine::core` target and the LaneMaker sources, with a
`RoadEngineService` that calls the engine directly (no N-API), and a
single Qt window that prints the engine version. Once 2b's empty Qt
window builds, 2c (rendering spike) starts on its 2-week timebox.

---

## 15. Open Decisions — Resolved

| # | Decision | Resolution | Notes |
|---|---|---|---|
| 1 | Qt Widgets vs QML | **Deferred** — decide after Phase 2 rendering spike | Audit recommends Qt Widgets (§10) |
| 2 | 2D map approach | **Deferred** — decide after Phase 2c spike (MapLibre Native vs QGraphicsView) | Biggest technical risk (R2) |
| 3 | Terrain dependency set | **Deferred** — decide at Phase 6 planning | libtiff+libpng+libcurl recommended vs GDAL |
| 4 | Test strategy | **Mirror both** — keep vitest as regression oracle AND build Qt Test mirrors per phase; remove vitest at Phase 8 | doctest C++ tests (261) run unchanged throughout |
| 5 | LaneMaker ConnectRays SEH | **Preserve guard now, harden later** — keep `_set_se_translator` + `/EHa` in the C++ service for behavior parity; add a tracked follow-up task to harden `ConnectRays` (fuzz + input validation) | Mitigates R1 without blocking the port |
| 6 | Plugin system | **Design C++ plugin ABI now** — include a C++ plugin interface as part of the core framework port (Phase 2/3), even though `plugins/` is empty today | The TS `Plugin`/`PluginCapability` design (`core/interfaces.ts`) is the spec basis |
| 7 | Platforms | **Windows-first** — Windows target first; add Linux and macOS later | Matches current primary electron-builder target (`build:win`) |

### Follow-up tasks created by these decisions

- **[HARDEN-CONNECTRAYS]** (post-Phase-4): Fuzz `LM::ConnectRays` across
  degenerate angle combinations; add input validation; aim to remove the SEH
  guard. Track in `docs/` once created.
- **[PLUGIN-ABI]** (Phase 2/3): Define a C++ plugin interface mirroring
  `PluginCapability` types (importer, exporter, road-generator,
  terrain-processor, validator, visualization-layer, tool). Specify the
  dynamic-load mechanism (QtPlugin / QPluginLoader).
- **[RENDER-SPIKE]** (Phase 2c): Build minimal Qt prototypes for both
  MapLibre Native and QGraphicsView+libcurl-tiles to decide #2.
- **[TEST-MIRROR]** (per phase): For each migrated subsystem, add a Qt Test
  mirror of the corresponding vitest suite; keep vitest green until Phase 8.

---

*End of Phase 1 Audit. No source code was modified. All open decisions
resolved (3 deferred to later phases, 4 decided). Ready to begin Phase 2 on
approval.*
