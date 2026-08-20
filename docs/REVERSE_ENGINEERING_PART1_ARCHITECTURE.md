# OpenGeoStudio-Qt — Reverse-Engineering & Developer Guide
## Part 1 of 3: Architecture & Structure (Sections 1–12)

> **Repository:** `D:\git\OpenGeoStudio-Qt`
> **Analysis date:** 2026-08-19
> **Method:** Static source inspection only — no builds, no test runs, no code modifications.
> **Status:** **Reverse-engineering baseline — not yet runtime-validated.** All findings are from source reading. Test counts, runtime behavior, and `[UNKNOWN]` items require runtime verification before being treated as authoritative.
> **Certainty labels:**
> - `[CONFIRMED]` = verified from source code
> - `[INFERENCE]` = strong inference from evidence but not directly verified
> - `[UNKNOWN]` = not determinable from source
> - `[RISK]` = identified architectural or operational risk
> - `[RECOMMENDATION]` = suggested improvement (not current behavior)

This is Part 1 of a 3-part reverse-engineering document.
- **Part 1** (this file): Sections 0–12 — Start Here, Executive Summary, Repository Structure, Technology Stack, Entry Points, Architecture, Module Map, Dependency Graph, Core Logic, Execution Flows, Data Flow, Important Classes, Important Functions.
- **Part 2** (`docs/REVERSE_ENGINEERING_PART2_SYSTEMS.md`): Sections 13–25 — Algorithms, Plugin System, API/External Integrations, Database, File/Asset Pipelines, Configuration, Error Handling, Logging, Concurrency, Performance, Testing, Build System, Deployment.
- **Part 3** (`docs/REVERSE_ENGINEERING_PART3_GUIDE.md`): Sections 26–39 — Duplicate/Dead Code, Security, Technical Debt, Change Impact Map, Safe vs High-Risk Areas, New Developer Guide, AI Repository Context, Master System Map, Improvement Roadmap, Verification Backlog, Change/Test Workflows, Do Not Touch Rules, Developer Recipes, Persistence & Versioning Strategy.

---

## 0. Start Here — 30-Second Mental Model

> Read this first. It gives you the entire system shape before you dive into details.

```
Application
  main.cpp (src/app/main.cpp)
    ↓
ApplicationContext (src/core/ApplicationContext.hpp)
    ├── EventBus          — pub/sub decoupling
    ├── ProjectManager    — .ogproj CRUD, autosave, recents
    ├── WorkspaceManager  — 5 workspaces, activation
    └── TerrainStore      — terrain selection, export settings

Workspaces (QStackedWidget, index 0-4)
    ├── 0  Home           — templates, recent projects
    ├── 1  Terrain Studio — MapLibre map + DEM/imagery export
    ├── 2  Road Studio    — embedded LaneMaker (OpenGL, road mode)
    ├── 3  Train Studio   — embedded LaneMaker (OpenGL, rail mode)
    └── 4  3D Studio      — OGRE-Next 4.x (D3D11)

Road / Track
    Road Studio / Train Studio
      ↓ embeds
    LaneMaker::MainWindow
      ↓ uses
    RoadV2 / OpenDRIVE (.xodr)

Terrain
    Terrain Studio
      ↓
    Terrain pipeline (download → decode → resample → GeoTIFF)

3D
    3D Studio
      ↓
    World model (actors, splines, PCG)
      ↓
    OGRE-Next rendering

OSM Import (cross-cutting)
    .osm file → OsmImportPipeline → RoadV2[] → .xodr → LaneMaker

Persistence (multi-file, non-transactional)
    .ogproj (project metadata + terrain state)
    .xodr   (road network)
    world.json (3D scene)
```

### Critical Architecture Constraint — Read Before Touching LaneMaker

> **`[RISK]` #1 — LaneMaker is a singleton-oriented engine.**
>
> LaneMaker uses **process-wide global pointers** that are rebound on `showEvent()`:
>
> ```cpp
> extern MainWindow*      g_mainWindow;      // main_window.h:125
> extern LaneConfigWidget* g_laneConfig;      // LaneConfigWidget.h:177
> extern MapViewGL*        g_mapViewGL;       // map_view_gl.h:230
> extern double            g_PointerRoadS;    // map_view_gl.h:232
> extern int               g_PointerLane;     // map_view_gl.h:233
> extern int               g_createRoadElevationOption; // map_view_gl.h:236
> extern UserPreference    g_preference;      // preference.h:38
> ```
>
> Additionally, these are **process-wide singletons**:
> - `MainWidget::instance` (main_widget.h:141)
> - `World::Instance()` (world.h:10) — LaneMaker's road container
> - `ActionManager::Instance()` (action_manager.h:22)
> - `SignRegistry::Instance()`, `MarkingRegistry::Instance()`, `FurnitureRegistry::Instance()`
>
> **Do NOT assume Road Studio and Train Studio are independent instances.**
>
> Both workspaces construct a `LaneMaker::MainWindow` at startup. The globals
> point to whichever workspace was last shown. `World::Instance()` is shared —
> both workspaces' roads are in the same `std::set`.
>
> `[RECOMMENDATION]` Before changing any of these classes, verify global
> ownership and workspace rebinding:
> - `MainWidget`, `MainWindow`, `MapViewGL`, `LaneConfigWidget`
> - `ActionManager`, `World` (LaneMaker), `SignRegistry`, `MarkingRegistry`
>
> See Part 3, Section 28 (Technical Debt) and Section 30.4 (Do Not Change
> Without Architectural Review) for full details.

### Road Model Relationship — Which Model Is Authoritative?

> The repository has **three road representations** plus a dual-layer RoadV2.
> This is the first question every developer asks.

```
                   ┌──────────────────────────┐
                   │   Public RoadV2 API      │
                   │   (road_engine/public/)  │
                   │   Placeholder LaneSection│
                   └────────────┬─────────────┘
                                │
                       facade / must stay in sync
                                │
                   ┌────────────▼─────────────┐
                   │   Internal RoadV2        │
                   │   (src/engine/road/)     │
                   │   Full LaneSection from  │
                   │   lane_engine.hpp        │
                   └────────────┬─────────────┘
                                │
                    ┌───────────┴───────────┐
                    │                       │
              OSM Pipeline              Road Engine
              (core/osm/)            (lane_*.hpp)
                    │                       │
                    │                  road_adapter.hpp
                    │                  (legacy ↔ RoadV2)
                    │                       │
                    ▼                       ▼
              .xodr export             LaneMaker
              (OsmExporter)              │
                                         ▼
                                    LM::Road
                                    (xodr/road.h)
                                    wraps odr::Road

              roads::Road (RoadTypes.hpp)
              Legacy: ControlPoint[], used by UI types
              and RoadProfileCatalog/RailProfileCatalog
```

**Authority:**
- **For geometry/lane algorithms:** `road_v2::RoadV2` (internal) is authoritative.
- **For interactive editing and OpenDRIVE I/O:** `LM::Road` is authoritative (wraps `odr::Road`).
- **For profile presets and UI metadata:** `roads::RoadProfile` / `roads::RailProfile` (RoadTypes.hpp) are authoritative.
- **For OSM import/export:** `road_v2::RoadV2` → `OsmExporter` → `.xodr` (LaneMaker loads the `.xodr`).

`[RISK]` The dual `road_v2.hpp` files cause a header conflict if the wrong one is included. AGENTS.md rule: "OSM headers that use RoadV2 must only be included from `.cpp` files in the main app (not from headers)."

`[RECOMMENDATION]` Before modifying any road model, read `docs/ROAD_V2_DUAL_LAYERS.md` and trace which representation your consumers use.

---

## 1. Executive Summary

OpenGeoStudio-Qt is a **native C++20 / Qt 6 desktop application** for authoring geo-referenced 3D worlds: terrain, road networks, railways, and procedural scenes. It is the native successor to an earlier TypeScript/Electron stack (now fully removed) and is intended to be a professional, production-ready GIS-and-road authoring tool.

The application is organized as a **single-window, multi-workspace** Qt application with five workspaces:

| Index | Workspace | Engine |
|-------|-----------|--------|
| 0 | Home | Qt Widgets |
| 1 | Terrain Studio | MapLibre Native Qt + libtiff |
| 2 | Road Studio | Embedded LaneMaker (OpenGL) |
| 3 | Train Studio | Embedded LaneMaker (OpenGL, rail mode) |
| 4 | 3D Studio | OGRE-Next 4.x |

Key architectural characteristics `[CONFIRMED]`:

- **C++20, Qt 6.8, MSVC, CMake + Ninja, vcpkg manifest mode.**
- **Three internal libraries:** `road_engine` (header-only), `lanemaker` (static), `plugin_api` (interface).
- **Dual RoadV2 model** — an internal full model and a public API facade, intentionally kept in sync.
- **Embedded LaneMaker engine** for road/rail editing, shared between Road Studio and Train Studio via global pointers rebound on `showEvent()`.
- **OSM import pipeline** (header-only) that converts OSM XML → RoadV2 roads with topology, junctions, lanes, markings, and signs, then exports to OpenDRIVE/GeoJSON.
- **Terrain pipeline** that downloads DEM and imagery from multiple providers, processes them, and writes georeferenced GeoTIFF/PNG outputs.
- **World authoring model** for 3D Studio with actors, layers, splines, PCG graphs, water, lighting, and undo/redo.
- **Plugin system** defined as a header-only ABI (`Q_DECLARE_INTERFACE`), with `PluginManager` and `PluginLoader` provided, but no plugins shipped and no runtime discovery wired into `ApplicationContext` yet `[CONFIRMED]`.

The application is at version **0.1.0** and is pre-1.0. Recent work focused on Road Studio simplification (removing all tools except Draw New Road and View), a Cross-Section Studio unification, and bug hardening.

---

## 2. Repository Structure

### 2.1 Conceptual Inventory

```
REPOSITORY (D:\git\OpenGeoStudio-Qt)
│
├── Applications          → src/app/ (main.cpp, MapViewportWidget)
├── Core                 → src/core/ (context, project, workspace, events,
│                                     logger, osm/, world/, terrain/, map/)
├── Modules              → (no separate modules dir; subsystems live in core/ and ui/)
├── Plugins              → src/plugin/ (PluginApi.hpp header-only ABI)
├── Libraries            → src/engine/road/ (road_engine), src/engine/lanemaker/
│                         (embedded LaneMaker), third-party via vcpkg
├── APIs                 → No REST/GraphQL API. External integrations are HTTP
│                         tile/DEM providers and file-format I/O.
├── Services             → ApplicationContext-owned: EventBus, ProjectManager,
│                         WorkspaceManager, TerrainStore
├── Utilities            → src/core/PathHelper.hpp, GeoConvert.hpp, scripts/
├── Tests                → src/core/osm/test_*, src/core/world/test_*,
│                         src/core/terrain/test_*, src/engine/road/*_tests,
│                         src/engine/lanemaker/test/*, src/ui/terrain/test_*
├── Scripts              → scripts/ (packaging/build tooling)
├── Configuration        → CMakeLists.txt, vcpkg.json, .github/workflows/ci.yml,
│                         .gitignore, AGENTS.md
├── Build                → build/ (generated, deploy/ subfolder)
├── Deployment           → build/deploy/ (windeployqt output, OGRE DLLs, media)
└── Documentation        → README.md, AGENTS.md, PORTABLE_README.txt, docs/
```

### 2.2 Major Directory Reference

| Directory | Purpose | Important Files | Used By | Depends On | Role |
|-----------|---------|-----------------|---------|------------|------|
| `src/app/` | Application shell & entry point | `main.cpp`, `MapViewportWidget.cpp/.hpp` | User (launches app) | Qt6, core, all UI workspaces | Runtime |
| `src/core/` | Domain core: context, project, workspace, events, logger | `ApplicationContext.cpp/.hpp`, `ProjectManager.cpp/.hpp`, `WorkspaceManager.cpp/.hpp`, `EventBus.hpp`, `Logger.hpp`, `PathHelper.hpp` | app, ui, tests | Qt6 | Runtime |
| `src/core/osm/` | OSM import pipeline (header-only) | `OsmTypes.hpp`, `OsmXmlParser.hpp`, `CoordinateConverter.hpp`, `RoadClassifier.hpp`, `RoadNetworkBuilder.hpp`, `JunctionDetector.hpp`, `RoadValidator.hpp`, `OsmImportPipeline.hpp`, `LaneGenerator.hpp`, `RoundaboutGenerator.hpp`, `RoadMarkingGenerator.hpp`, `TrafficSignGenerator.hpp`, `OsmProjectSerializer.hpp`, `OsmExporter.hpp`, `RailImportPipeline.hpp`, `DemElevationSampler.hpp` | ui/roadstudio, ui/trainstudio, tests | road_engine (internal road_v2), Qt6 | Runtime |
| `src/core/world/` | World authoring model | `WorldTypes.hpp`, `World.hpp`, `WorldBuilder.hpp`, `Spline.hpp`, `SplineEditor.hpp`, `UndoRedo.hpp`, `TerrainWorldBridge.hpp`, `PCGEngine.hpp` | ui/studio3d, tests | Qt6 | Runtime |
| `src/core/terrain/` | Terrain pipeline (header-only) | `TerrainManager.hpp`, `TerrainPipelineTypes.hpp`, `CacheManager.hpp`, `DownloadManager.hpp`, `GISProcessor.hpp`, `TerrainAnalyzer.hpp`, `TileManager.hpp`, `ValidationManager.hpp`, `MaskManager.hpp` | ui/terrain, tests | Qt6, TIFF, PNG | Runtime |
| `src/core/map/` | Map/coordinate subsystem | `map_tests.cpp` (+ headers referenced by tests) | tests, lanemaker map subsystem | Qt6 | Runtime/Tooling |
| `src/engine/road/` | Road engine (header-only) | `geometry.hpp`, `geometry_segment.hpp`, `road.hpp`, `road_v2.hpp`, `road_adapter.hpp`, `arc.hpp`, `clothoid.hpp`, `intersection.hpp`, `mesh.hpp`, `opendrive.hpp`, `road_tools.hpp`, `lane_engine.hpp`, `lane_geometry.hpp`, `lane_sampling.hpp`, `lane_network.hpp`, `road_mark_generator.hpp`, `road_mesh_generator.hpp`, `geometry_segment_tests.cpp` | core/osm, lanemaker, tests | nlohmann_json | Runtime/Build |
| `src/engine/road/road_engine/public/` | Public API facade | `road_v2.hpp` (placeholder LaneSection), `road_engine.hpp` | external consumers, tests | — | Runtime |
| `src/engine/lanemaker/` | Embedded LaneMaker engine & UI | `ui/main_widget.cpp/.h`, `ui/main_window.cpp/.h`, `ui/action_manager.cpp/.h`, `ui/action_defs.h`, `ui/road_drawing.h`, `engine/map_view_gl.cpp/.h`, `widgets/LaneConfigWidget.cpp/.h`, `widgets/DrawOptionDialog.cpp/.h`, `widgets/AnimatedPopupDialog.cpp/.h`, `xodr/road.cpp/.h`, `xodr/junction.cpp/.h`, `xodr/world.cpp/.h`, `libOpenDRIVE/...`, `util/...`, `traffic/...`, `test/...` | ui/roadstudio, ui/trainstudio, tests | Qt6, CGAL, Boost, spdlog, cereal, road_engine, optional QMapLibre | Runtime |
| `src/ui/home/` | Home workspace | `HomeWidget.cpp/.hpp` | app | core | Runtime |
| `src/ui/roadstudio/` | Road Studio workspace & OSM import UI | `RoadStudioWidget.cpp/.hpp`, `RoadTypes.hpp`, `OsmImportDialog.cpp/.hpp`, `GeoConvert.hpp` | app | core, lanemaker, core/osm | Runtime |
| `src/ui/trainstudio/` | Train Studio workspace & rail OSM import UI | `TrainStudioWidget.cpp/.hpp`, `RailOsmImportDialog.hpp` | app | core, lanemaker, core/osm | Runtime |
| `src/ui/terrain/` | Terrain Studio workspace & export engine | `TerrainStudioWidget.cpp/.hpp`, `TerrainViewport.cpp/.hpp`, `TerrainStore.cpp/.hpp`, `ExportPanel.cpp/.hpp`, `ExportEngine.cpp/.hpp`, `LayerStack.hpp`, `SearchBar.hpp`, `RasterWriter.cpp/.hpp`, `TerrainTypes.hpp`, `TerrainPipelinePanel.hpp`, `DemDecoder.*`, `test_geotiff_writer.cpp`, `test_gpxz_download.cpp` | app | core, MapLibre, TIFF, PNG | Runtime |
| `src/ui/studio3d/` | 3D Studio workspace (OGRE-Next) | `Studio3DWidget.cpp/.hpp`, `OgreWidget.cpp/.hpp`, `NPanel.hpp`, `PropertiesEditor.hpp`, `EditorPanels.hpp` | app | core/world, OGRE-Next, libOpenDRIVE | Runtime |
| `src/plugin/` | Plugin ABI (header-only) | `PluginApi.hpp` | (future plugins) | Qt6 | Runtime |
| `resources/` | Qt resources, icons, shaders, compositor | `app.rc`, `app_icons.qrc`, `road_studio/road_studio.qrc`, `road_studio/svg/*.svg`, `compositor/OpenGeoStudio.compositor` | app (compiled into executable) | — | Build |
| `scripts/` | Packaging/build tooling | (packaging scripts) | build/deploy | — | Tooling |
| `.github/workflows/` | CI | `ci.yml` | CI | — | CI |
| `docs/` | Documentation | `CROSS_SECTION_STUDIO.md`, `ROAD_V2_DUAL_LAYERS.md`, `ogproj-schema.json`, this 3-part RE doc | developers | — | Documentation |
| `build/` | Generated build output | `deploy/` subfolder | — | — | Build |

---

## 3. Technology Stack

### 3.1 Technology Table

| Technology | Version | Purpose | Evidence/Location |
|------------|---------|---------|-------------------|
| C++ | C++20 | Application language | `CMakeLists.txt` line 32 (`CMAKE_CXX_STANDARD 20`) |
| Qt | 6.8.0 | GUI framework (Widgets, OpenGL, Network) | `vcpkg.json`, `ci.yml` line 17, `CMakeLists.txt` `find_package(Qt6 ...)`, AGENTS.md |
| CMake | ≥3.16 | Build system | `CMakeLists.txt` line 1, `ci.yml` line 33 |
| Ninja | — | Build generator | `ci.yml` line 36, AGENTS.md |
| MSVC | VS 2022 BuildTools | Compiler | AGENTS.md (vcvars64.bat path) |
| vcpkg | manifest mode | Package manager | `vcpkg.json`, AGENTS.md toolchain reference |
| nlohmann_json | (vcpkg) | JSON serialization | `vcpkg.json`, `CMakeLists.txt` `find_package(nlohmann_json)` |
| libtiff | (vcpkg) | GeoTIFF read/write | `vcpkg.json` ("tiff"), `RasterWriter.cpp`, `CMakeLists.txt` `TIFF::TIFF` |
| libpng | (vcpkg) | PNG read/write | `vcpkg.json` ("libpng"), `CMakeLists.txt` `PNG::PNG` |
| spdlog | (vcpkg) | LaneMaker internal logging | `vcpkg.json`, `CMakeLists.txt` `spdlog::spdlog`, `SPDLOG_ACTIVE_LEVEL` define |
| cereal | (vcpkg) | Binary serialization (ActionManager replay) | `vcpkg.json`, `CMakeLists.txt` `cereal::cereal`, `action_manager.cpp` |
| CGAL | (vcpkg) | Computational geometry (LaneMaker) | `vcpkg.json` ("cgal"), `CMakeLists.txt` `CGAL::CGAL` |
| Boost | (vcpkg) | Support libraries (LaneMaker) | `vcpkg.json` ("boost-optional"), `CMakeLists.txt` `Boost::boost` |
| curl | (vcpkg) | HTTP (vcpkg transitive) | `vcpkg.json` ("curl") |
| gtest | (vcpkg) | Listed in manifest but not used by tests `[UNKNOWN]` | `vcpkg.json` ("gtest") — tests use doctest/custom runners |
| MapLibre Native Qt | (optional, local install) | Map viewport for Terrain Studio | `CMakeLists.txt` `find_package(QMapLibre)`, `MapViewportWidget.cpp`, `HAVE_MAPLIBRE` define |
| OGRE-Next | 4.x (optional, `D:/git/ogre-next`) | 3D rendering for 3D Studio | `CMakeLists.txt` lines 386-459, `OgreWidget.cpp`, `HAVE_OGRE` define |
| libOpenDRIVE | (embedded in LaneMaker) | OpenDRIVE (.xodr) parsing & road geometry | `src/engine/lanemaker/libOpenDRIVE/`, `OgreWidget.cpp` includes |
| doctest | (header-only, vendored or system) | Test framework for geometry tests | `geometry_segment_tests.cpp`, `map_tests.cpp` |
| Qt Resource System | — | Compiled-in resources (icons, shaders, QML) | `app_icons.qrc`, `road_studio.qrc`, `images.qrc`, `shaders.qrc` |
| windeployqt | (Qt tool) | Windows DLL deployment | `CMakeLists.txt` lines 466-513 |
| GitHub Actions | — | CI | `.github/workflows/ci.yml` |

### 3.2 Dependency Classification

**Direct dependencies (required):** Qt6 (Core/Gui/Widgets/OpenGL/OpenGLWidgets/Network), nlohmann_json, libtiff, libpng, CGAL, Boost, spdlog, cereal.

**Optional dependencies:** MapLibre Native Qt (Terrain Studio map), OGRE-Next (3D Studio).

**Development dependencies:** CMake, Ninja, MSVC BuildTools, vcpkg, windeployqt.

**Transitive dependencies (via vcpkg):** curl (likely transitive for network ops), zlib (via libtiff/libpng), and others resolved by vcpkg.

**Suspicious/unnecessary dependencies `[INFERENCE]`:**
- `gtest` is listed in `vcpkg.json` but no test uses Google Test — all tests use doctest or custom runners. This may be a leftover from the TypeScript-era migration plan.
- `boost-optional` is listed but the codebase uses `std::optional` in modern C++20 code; Boost may only be needed by LaneMaker legacy code `[UNKNOWN — needs Boost usage trace]`.

**Deprecated dependencies:** None identified as deprecated, but the LaneMaker spdlog usage is intentionally "leave untouched" per AGENTS.md, suggesting it is legacy-locked.

---

## 4. Entry Points

### 4.1 Application Entry Point

**File:** `src/app/main.cpp`
**Function:** `main(int argc, char** argv)` (line 858)
**Purpose:** Application bootstrap.

**Startup flow `[CONFIRMED]`:**

```
main()
  ↓
QApplication created (line 859)
  ↓
Set app icon from resources (line 862)
  ↓
Initialize LaneMaker Qt resources — Q_INIT_RESOURCE (lines 867-868)
  ↓
Set app metadata: name "OpenGeoStudio", version "0.1.0", org "OpenGeoStudio" (lines 869-871)
  ↓
Enable log file transport to log.txt next to executable (lines 874-878)
  ↓
Set dark theme palette (GitHub-inspired) (lines 884-904)
  ↓
Set global stylesheet (scrollbars, toolbars, menus, docks, inputs) (lines 907-984)
  ↓
Create ApplicationContext (instantiates EventBus, ProjectManager, WorkspaceManager, TerrainStore) (line 987)
  ↓
Create AppMainWindow with ApplicationContext (line 989)
  ↓
Show main window (line 990)
  ↓
Auto-open project from command line if .ogproj argument provided (lines 993-1001)
  ↓
Log startup with road engine version (lines 1003-1004)
  ↓
Enter Qt event loop (line 1006)
```

**AppMainWindow construction (line 224-852):**
- `setupMenuBar()` — File, View, Help menus
- `setupToolBar()` — workspace tabs and global actions
- `setupStatusBar()` — status label
- `setupCenterWidget()` — `QStackedWidget` with 5 pages (Home, Terrain, Road, Train, 3D)
- `setupDockWidgets()` — left (Project) and right (Inspector) docks
- Connects `WorkspaceManager::workspaceActivated` → `onWorkspaceActivated`
- Connects `ProjectManager::projectChanged` → `onProjectChanged`
- Connects `ProjectManager::projectOpened` → `onProjectOpened`

### 4.2 Other Entry Points

| Entry Point | File | Purpose |
|-------------|------|---------|
| Test runner (doctest) | `geometry_segment_tests.cpp` | `main()` with doctest framework; 504 test cases |
| Test runner (custom) | `test_osm_pipeline.cpp` | `main()` with static-registration `TEST()` macro; 35 functions |
| Test runner (custom) | `test_world_model.cpp` | `main()` with `VERIFY` macro; 20 functions |
| Test runner (custom) | `test_world_workflow.cpp` | `main()` with `VERIFY` macro; 19 functions |
| Test runner (custom) | `test_terrain_pipeline.cpp` | `main()` with `VERIFY` macro; 26 functions |
| Test runner (custom) | `test_road_studio.cpp` | `main()` with `CHECK` macro; 39 functions, 26 invoked |
| Test runner (custom) | `test_road_studio_ui.cpp` | `main()` with `CHECK` macro; offscreen UI smoke test |
| Test runner (custom) | `test_houston_roundtrip.cpp` | `main()` with `TEST()` macro; 1 data-dependent test |
| Standalone test | `test_geotiff_writer.cpp` | `main()` with no formal framework |
| Standalone test | `test_gpxz_download.cpp` | `main()` with 7 network test groups |
| Plugin init | `IPlugin::init(ApplicationContext*)` | Called on app startup (not yet wired) `[CONFIRMED not wired]` |
| CI build | `.github/workflows/ci.yml` | `cmake -B build -G Ninja` + `cmake --build build` + `ctest` |

---

## 5. Architecture

### 5.1 Actual Architecture

The application implements a **layered, event-driven, single-process desktop architecture** — not MVC, MVVM, ECS, or microservices. It is best described as:

```
┌─────────────────────────────────────────────────────────┐
│                    Application Shell                     │
│              (AppMainWindow, main.cpp)                   │
│  QStackedWidget: Home | Terrain | Road | Train | 3D      │
└────────────┬────────────────────────────┬───────────────┘
             │                            │
     ┌───────▼────────┐          ┌───────▼────────┐
     │  Core Services │          │   UI Workspaces │
     │  (ApplicationContext)│    │   (Widgets)     │
     │  EventBus       │          │  HomeWidget     │
     │  ProjectManager │          │  TerrainStudio  │
     │  WorkspaceMgr   │          │  RoadStudio     │
     │  TerrainStore   │          │  TrainStudio    │
     └───────┬────────┘          │  Studio3D       │
             │                    └───┬────────┬────┘
             │                        │        │
     ┌───────▼────────┐     ┌─────────▼──┐  ┌──▼──────────┐
     │  Domain Models │     │  Engines    │  │  Pipelines   │
     │  Project       │     │  LaneMaker  │  │  OSM Import  │
     │  Workspace     │     │  (OpenGL)   │  │  Terrain     │
     │  World         │     │  OGRE-Next  │  │  Export      │
     │  Terrain       │     │  (D3D11)    │  │  GeoTIFF     │
     └────────────────┘     └─────────────┘  └──────────────┘
             │                        │            │
     ┌───────▼────────────────────────▼────────────▼──────┐
     │              Road Engine (header-only)              │
     │  RoadV2, LaneSection, Geometry Segments, Mesh       │
     └─────────────────────────────────────────────────────┘
             │
     ┌───────▼────────────────────────────────────────────┐
     │          External Libraries (vcpkg / local)         │
     │  Qt6, CGAL, Boost, spdlog, cereal, libtiff, libpng  │
     │  MapLibre (optional), OGRE-Next (optional)          │
     └─────────────────────────────────────────────────────┘
```

### 5.2 Key Architectural Decisions

1. **ApplicationContext as service container** `[CONFIRMED]` — replaces the TypeScript `AppContext` + `ServiceRegistry`. Owns `EventBus`, `ProjectManager`, `WorkspaceManager`, `TerrainStore`. No global singleton; instance passed around.

2. **EventBus for decoupled communication** `[CONFIRMED]` — lightweight pub/sub using `std::function` handlers, not Qt signals/slots. Used by `ProjectManager` (publishes `project:created`, `project:opened`, etc.) and `WorkspaceManager` (publishes `workspace:activated`).

3. **Workspace switching via QStackedWidget** `[CONFIRMED]` — index 0-4 maps to Home/Terrain/Road/Train/3D. `WorkspaceManager::activate(id)` emits signal, `AppMainWindow::onWorkspaceActivated` switches the stack index, shows/hides docks, and installs Road Studio menus.

4. **Embedded LaneMaker with global pointer rebinding** `[CONFIRMED]` `[RISK]` — both Road Studio and Train Studio construct a LaneMaker `MainWindow` at startup. Global pointers (`g_mainWindow`, `g_laneConfig`, `g_mapViewGL`) are rebound in `MainWindow::showEvent()` to the currently-visible workspace's instance. **This is the #1 architectural risk.** See "Critical Architecture Constraint" in Section 0 above and Part 3, Section 28. `[RECOMMENDATION]` Refactor to per-instance context or ensure single-instance lifecycle.

5. **Dual RoadV2 model** `[CONFIRMED]` — internal full model at `src/engine/road/road_v2.hpp` (uses `lane_engine.hpp` `LaneSection`) and public API facade at `src/engine/road/road_engine/public/road_v2.hpp` (self-contained placeholder). Both must stay in sync. See `docs/ROAD_V2_DUAL_LAYERS.md`.

6. **Header-only core pipelines** `[CONFIRMED]` — OSM pipeline (`src/core/osm/`) and terrain pipeline (`src/core/terrain/`) are entirely header-only, included from `.cpp` files in the main app and tests.

7. **Plugin system as ABI contract only** `[CONFIRMED]` — `PluginApi.hpp` defines `IPlugin`, `PluginManager`, `PluginLoader` with `Q_DECLARE_INTERFACE(IPlugin, "opengeostudio.plugin/1.0")`. However, `PluginManager` is NOT instantiated by `ApplicationContext` and no plugin discovery/load is wired into startup. This is a Phase 3 placeholder.

8. **OpenGL context per workspace** `[CONFIRMED]` — LaneMaker's `MapViewGL` is a `QOpenGLWidget` with Core Profile 3.3. Road file loading is deferred until Road Studio is visible because the OpenGL context isn't ready until `showEvent()`.

9. **World model as pure data** `[CONFIRMED]` — `World` class in `src/core/world/` is a pure data model with no rendering. `OgreWidget` syncs the World model to OGRE-Next rendering. `QUndoStack` provides undo/redo.

10. **Project state split across files (non-transactional)** `[CONFIRMED]` `[RISK]` — `.ogproj` file stores terrain state in `moduleState`; road network is saved separately as `{project}/Roads/road.xodr` via LaneMaker; world scene in `world.json`. **Saving is multi-file and non-transactional** — if one file write fails, the project is left in an inconsistent state. `[RECOMMENDATION]` Before 1.0, implement transactional save (temp files → validate → atomic rename) and add a `schemaVersion` field to `.ogproj` with migration functions (see Part 3, Section 34).

---

## 6. Module Map

### 6.1 Application Shell

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Main window, menu bar, toolbar, dock widgets, workspace switching, project state save/load |
| **Inputs** | User actions (menu, toolbar, keyboard shortcuts), command-line arguments |
| **Outputs** | Workspace activation, project CRUD, status bar updates |
| **State** | `ApplicationContext* m_ctx`, `QStackedWidget* m_centerStack`, workspace widgets, dock widgets |
| **Callers** | `main()` |
| **Callees** | `ApplicationContext`, `WorkspaceManager`, `ProjectManager`, all workspace widgets |
| **Persistence** | Saves terrain state to `.ogproj` `moduleState`, road `.xodr` to `{project}/Roads/` |
| **Validation** | None at shell level (delegated to workspaces) |
| **Failure modes** | Project open/save failures show `QMessageBox`; workspace switch failures are silent |
| **Role** | Runtime |

### 6.2 Core Services (ApplicationContext)

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Own and provide access to EventBus, ProjectManager, WorkspaceManager, TerrainStore |
| **Inputs** | None (constructor only) |
| **Outputs** | Service references via `events()`, `projects()`, `workspaces()`, `terrain()` |
| **State** | `unique_ptr` to each service |
| **Callers** | `main()`, `AppMainWindow`, workspace widgets |
| **Callees** | EventBus, ProjectManager, WorkspaceManager, TerrainStore constructors |
| **Persistence** | None directly |
| **Validation** | None |
| **Failure modes** | Service construction failure would crash (no error handling) |
| **Role** | Runtime |

### 6.3 OSM Import Pipeline

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Parse OSM XML → project coordinates → build RoadV2 roads → detect junctions → validate → generate lanes/markings/signs → export OpenDRIVE/GeoJSON |
| **Inputs** | OSM file path, import settings (simplification, validation, auto-repair) |
| **Outputs** | `OsmImportPipeline::Result` (roads, junctions, issues, stats), `.xodr`, `.geojson`, `.ogosm` |
| **State** | Stateless (static methods) |
| **Callers** | `OsmImportDialog`, `RailOsmImportDialog`, tests |
| **Callees** | `OsmXmlParser`, `CoordinateConverter`, `RoadNetworkBuilder`, `JunctionDetector`, `RoadValidator`, `LaneGenerator`, `RoundaboutGenerator`, `RoadMarkingGenerator`, `TrafficSignGenerator`, `OsmExporter`, `OsmProjectSerializer`, `DemElevationSampler` |
| **Persistence** | `.ogosm` via `OsmProjectSerializer`, `.xodr` via `OsmExporter` |
| **Validation** | `RoadValidator` checks geometry, topology, lanes, junctions; auto-repair available |
| **Failure modes** | Returns `Result.success=false` + `errorMessage`; XML parse errors with line/column |
| **Role** | Runtime |

### 6.4 LaneMaker Engine

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Interactive road/rail editing with OpenGL rendering, OpenDRIVE I/O, undo/redo, action replay |
| **Inputs** | Mouse/keyboard events, profile selections, .xodr files |
| **Outputs** | OpenGL rendering, .xodr files, JSON sidecars (signs/markings/furniture) |
| **State** | `World` singleton (roads), `ActionManager` singleton (history), global pointers, `LaneConfigWidget`, `MapViewGL` |
| **Callers** | `RoadStudioWidget`, `TrainStudioWidget` |
| **Callees** | `MapViewGL`, `Road`, `Junction`, `LaneProfile`, `LaneConfigWidget`, `ActionManager`, `ChangeTracker`, `SignRegistry`, `MarkingRegistry`, `FurnitureRegistry` |
| **Persistence** | `.xodr` via `MainWindow::saveToPath()`, JSON sidecar for custom graphics |
| **Validation** | `MainWindow::verifyMap()` runs validation |
| **Failure modes** | OpenGL context issues (deferred loading), Unicode path issues (temp file workaround), SEH exceptions in road destruction (test mode) |
| **Role** | Runtime |

### 6.5 Terrain Pipeline

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Download DEM + imagery from multiple providers, process (decode, resample, clip), write georeferenced GeoTIFF/PNG |
| **Inputs** | `ExportSettings` (DEM source, imagery source, API keys, resolutions, CRS), tile selection |
| **Outputs** | GeoTIFF heightmaps, GeoTIFF/PNG imagery, manifest.json |
| **State** | `TerrainStore` (bounds, tile grid, selection, export settings, mask settings) |
| **Callers** | `ExportPanel`, `TerrainPipelinePanel` |
| **Callees** | `ExportEngine`, `DemDecoder`, `RasterWriter`, `DownloadManager`, `CacheManager`, `GISProcessor`, `TerrainAnalyzer`, `TileManager`, `MaskManager`, `ValidationManager` |
| **Persistence** | GeoTIFF/PNG files, manifest.json, terrain state in `.ogproj` |
| **Validation** | `ValidationManager` with 26 test functions |
| **Failure modes** | Network download failures (retry with backoff), DEM decode failures, cache miss |
| **Role** | Runtime |

### 6.6 World Authoring (3D Studio)

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Pure data model for 3D world: actors, layers, splines, PCG graphs, water, lighting |
| **Inputs** | User actions via `OgreWidget`, procedural generation requests |
| **Outputs** | `World` JSON serialization, OGRE-Next rendering (via `OgreWidget`) |
| **State** | `World` instance (actors, layers, splines, PCG graphs, water, settings) |
| **Callers** | `Studio3DWidget`, `OgreWidget`, tests |
| **Callees** | `WorldBuilder`, `SplineEvaluator`, `SplineEditor`, `PCGEngine`, `TerrainWorldBridge`, `UndoRedo` commands |
| **Persistence** | `World::saveToFile()` / `loadFromFile()` (JSON) |
| **Validation** | `World::validate()` returns `ValidationError` list |
| **Failure modes** | Cycle detection in parent-child, missing assets, PCG graph cycles |
| **Role** | Runtime |

### 6.7 Road Engine (header-only)

| Attribute | Value |
|-----------|-------|
| **Responsibility** | RoadV2 data model, geometry segments (Line/Arc/Spiral/Bezier), lane engine, mesh generation, road marks |
| **Inputs** | Geometry parameters, lane profiles |
| **Outputs** | RoadV2 objects, sampled polylines, meshes, road marks |
| **State** | Stateless (pure data structures and algorithms) |
| **Callers** | OSM pipeline, LaneMaker, tests |
| **Callees** | nlohmann_json (for serialization) |
| **Persistence** | Via OSM exporter (OpenDRIVE) and LaneMaker (.xodr) |
| **Validation** | Geometry tolerance constants, continuity diagnostics |
| **Failure modes** | Floating-point accumulation over long roads, degenerate geometry |
| **Role** | Runtime/Build |

### 6.8 Plugin System

| Attribute | Value |
|-----------|-------|
| **Responsibility** | Define plugin ABI, register/query plugins by capability |
| **Inputs** | Plugin shared libraries (via `QPluginLoader`) |
| **Outputs** | Registered `IPlugin*` instances |
| **State** | `PluginManager::m_plugins` list |
| **Callers** | None currently (not wired into `ApplicationContext`) |
| **Callees** | `QPluginLoader`, `IPlugin::init()` |
| **Persistence** | None |
| **Validation** | Metadata validation in `PluginLoader::discoverPlugins()` |
| **Failure modes** | Version mismatch, missing metadata, load failure |
| **Role** | Runtime (defined but not active) |

---

## 7. Dependency Graph

### 7.1 Inter-module Dependencies

```
main.cpp
  ├── ApplicationContext
  │     ├── EventBus
  │     ├── ProjectManager ── EventBus, Logger, Project
  │     ├── WorkspaceManager ── EventBus, Logger, Workspace
  │     └── TerrainStore ── EventBus
  ├── AppMainWindow
  │     ├── HomeWidget ── ApplicationContext, ProjectManager
  │     ├── TerrainStudioWidget ── ApplicationContext, TerrainStore,
  │     │     MapViewportWidget (optional MapLibre),
  │     │     ExportEngine, RasterWriter, DemDecoder
  │     ├── RoadStudioWidget ── ApplicationContext,
  │     │     LaneMaker::MainWindow,
  │     │     OsmImportDialog ── OsmImportPipeline, OsmExporter,
  │     │                       OsmProjectSerializer, DemElevationSampler
  │     ├── TrainStudioWidget ── LaneMaker::MainWindow,
  │     │     RailOsmImportDialog ── RailImportPipeline,
  │     │                           RailNetworkDefinitionExporter
  │     └── Studio3DWidget ── ApplicationContext,
  │           OgreWidget ── OGRE-Next, libOpenDRIVE, World, WorldBuilder,
  │                         SplineEvaluator, PCGEngine, DemDecoder
  └── (Logger via appLog())

LaneMaker::MainWindow
  ├── MainWidget ── MapViewGL, LaneConfigWidget, DrawOptionDialog,
  │                  ActionManager, RoadDrawingSession, Road, Junction,
  │                  SignRegistry, MarkingRegistry, FurnitureRegistry
  ├── MapViewGL ── OpenGL, TileMatrixSet (map subsystem)
  ├── LaneConfigWidget ── CrossSectionVisual, RoadProfileCatalog,
  │                        RailProfileCatalog
  └── Road ── LaneProfile, odr::RefLine, odr::RoadGeometry,
              SectionGraphics, AbstractJunction

OSM Pipeline (header-only)
  ├── OsmXmlParser ── QXmlStreamReader
  ├── CoordinateConverter ── (math only)
  ├── RoadNetworkBuilder ── road_v2 (internal), RoadClassifier
  ├── JunctionDetector ── road_v2, RoadNetworkBuilder
  ├── RoadValidator ── road_v2, JunctionDetector
  ├── LaneGenerator ── road_v2, RoadClassifier
  ├── RoundaboutGenerator ── road_v2, CoordinateConverter
  ├── RoadMarkingGenerator ── road_v2, JunctionDetector
  ├── TrafficSignGenerator ── road_v2, JunctionDetector
  ├── OsmExporter ── road_v2, CoordinateConverter, DemElevationSampler
  └── OsmProjectSerializer ── road_v2, CoordinateConverter

Road Engine (header-only)
  ├── road_v2.hpp (internal) ── lane_engine.hpp
  ├── road_v2.hpp (public) ── (self-contained)
  ├── road_adapter.hpp ── road_v2 (internal), legacy Road
  ├── geometry_segment.hpp ── (math only)
  └── lane_*.hpp ── road_v2, geometry_segment
```

### 7.2 Cycles and Risky Boundaries

1. **Global-state coupling `[CONFIRMED]`:** `g_mainWindow`, `g_laneConfig`, `g_mapViewGL` are rebound on `showEvent()`. Road Studio and Train Studio share these globals. If both workspaces interact with LaneMaker state simultaneously, behavior is undefined.

2. **Header conflict `[CONFIRMED]`:** Two `road_v2.hpp` files exist. Including the wrong one causes `LaneSection` type mismatch. AGENTS.md warns: "OSM headers that use RoadV2 must only be included from `.cpp` files in the main app (not from headers) to avoid conflict with the public placeholder."

3. **No circular module dependencies detected** `[CONFIRMED]` — the dependency graph is a DAG. Core does not depend on UI; UI depends on core. Road engine depends on nothing external except nlohmann_json.

4. **LaneMaker ↔ Road Engine coupling `[CONFIRMED]`:** LaneMaker links `road_engine::core` and uses internal `road_v2.hpp`. Changes to the road engine affect LaneMaker.

5. **World model ↔ OGRE-Next coupling `[CONFIRMED]`:** `OgreWidget` directly owns a `World` instance and syncs it to OGRE rendering. No abstraction layer between world model and renderer.

---

## 8. Core Logic

### 8.1 Application Startup

**Why:** Bootstrap the application, initialize services, show the main window.
**What triggers it:** User launches `OpenGeoStudio.exe`.
**Who calls it:** OS / user.
**Data movement:** Command-line arguments → project auto-open.
**State changes:** `ApplicationContext` created, `AppMainWindow` shown, default workspace = Home.
**Output:** Visible application window.
**What can break:** Qt initialization, resource loading, MSVC runtime missing.
**Confirmed behavior:** See Section 4.1 for the full startup flow.

### 8.2 Workspace Switching

**Why:** Allow user to move between Home, Terrain, Road, Train, 3D workspaces.
**What triggers it:** Toolbar tab click, `Alt+1`–`Alt+4` shortcuts, command palette, `WorkspaceManager::activate(id)`.
**Who calls it:** `AppMainWindow` toolbar actions, `HomeWidget` template selection.
**Data movement:** Workspace ID → `QStackedWidget` index.
**State changes:** `WorkspaceManager` active workspace, dock visibility, menu bar (Road Studio menus installed/removed).
**Output:** Visible workspace widget.
**Confirmed behavior:** `onWorkspaceActivated` (main.cpp line 595) switches stack index, shows/hides docks, installs Road Studio menus for road-studio workspace, loads pending road file if queued via 2-second `QTimer`.

### 8.3 Road Drawing (LaneMaker)

**Why:** Create new road geometry interactively.
**What triggers it:** User selects Road tool (R), clicks points in viewport, presses Space to confirm.
**Who calls it:** `MainWidget::OnMouseAction()` → `RoadCreationSession::Update()`.
**Data movement:** Mouse screen coords → world coords → staged geometry → `odr::RefLine` → `Road` object.
**State changes:** `editMode` = `Mode_Create`, `drawingSession` active, `World::allRoads` gains new road.
**Output:** New `Road` in `World`, OpenGL geometry uploaded via `MapViewGL::AddQuads()`.
**What can break:** OpenGL context not initialized, snapping to wrong road, geometry degeneracy.
**Confirmed behavior:** `gotoCreateRoadMode()` (main_widget.cpp:859) → `SetEditMode(LM::Mode_Create)` creates `RoadCreationSession` → `confirmEdit()` (main_widget.cpp:2558) → `RoadCreationSession::Complete()` → `Road::Generate()` → `refreshAllCustomGraphics()`.

### 8.4 Lane/Profile Selection (Cross-Section Studio)

**Why:** Let user pick a road profile (e.g., `city_2x1`) and edit lane configuration.
**What triggers it:** Profile combo selection in `LaneConfigWidget`.
**Who calls it:** `LaneConfigWidget::OnProfileComboChanged()`.
**Data movement:** Profile key → `RoadProfileCatalog::all()` → `RoadProfile` → `LanePlan` (left/right) → `CrossSectionVisual` → `MapViewGL` rendering.
**State changes:** `currentProfileKey`, `loadedProfile`, `modifiedFromProfile = false`, `applyingProfile = true` during load.
**Output:** `ProfileChanged` signal, updated cross-section visualization.
**Confirmed behavior:** `LoadProfile()` (LaneConfigWidget.cpp:849) loads from `RoadProfileCatalog`, sets lane plans, updates metadata controls, clears `applyingProfile`, emits `ProfileChanged`.

### 8.5 OSM Import

**Why:** Import real-world road networks from OpenStreetMap.
**What triggers it:** User clicks OSM import button in Road Studio, selects `.osm` file.
**Who calls it:** `RoadStudioWidget` → `OsmImportDialog::exec()`.
**Data movement:** `.osm` file → `OsmXmlParser` → `OsmData` → `CoordinateConverter` → `RoadNetworkBuilder` → `RoadV2[]` → `JunctionDetector` → `RoadValidator` → `OsmExporter` → `.xodr`.
**State changes:** Import result stored, roads/junctions/issues counted, optional `.ogosm` saved.
**Output:** `.xodr` file loaded into LaneMaker, `.geojson` export, validation report.
**What can break:** Malformed XML, missing nodes, projection errors, DEM sampling failures.
**Confirmed behavior:** `OsmImportPipeline::importFromFile()` orchestrates the full pipeline. `OsmImportDialog` displays results in tabs (Summary, Roads, Junctions, Validation).

### 8.6 Terrain Export

**Why:** Download and process DEM + satellite imagery for a selected area.
**What triggers it:** User selects area on map (Shift+drag), configures export settings, clicks Export.
**Who calls it:** `ExportPanel` → `ExportEngine::exportToDirectory()`.
**Data movement:** Tile selection → HTTP downloads (DEM + imagery) → `DemDecoder` → resample → `RasterWriter` → GeoTIFF/PNG files.
**State changes:** `TerrainStore` export settings, progress bar, manifest.json.
**Output:** GeoTIFF heightmaps, GeoTIFF/PNG imagery, manifest.json in project's Terrain folder.
**What can break:** Network failures (retry with backoff), DEM decode failures, Unicode paths (PathHelper workaround), coarse source resolution warnings.
**Confirmed behavior:** `ExportEngine` supports tiled sources (AWS Terrarium, Mapbox), Copernicus GLO-30 (multi-cell), area providers (OpenTopo, GPXZ, GLAD), and local files.

### 8.7 Persistence and Reload

**Why:** Save and restore project state across application sessions.
**What triggers it:** File → Save, File → Open, autosave timer (60s).
**Who calls it:** `AppMainWindow::saveProjectState()`, `ProjectManager::save()`/`open()`.
**Data movement:** `Project` → `.ogproj` JSON; road network → `.xodr` + JSON sidecar; terrain state → `.ogproj` `moduleState`.
**State changes:** `Project` dirty flag cleared, recent projects updated.
**Output:** `.ogproj` file, `{project}/Roads/road.xodr`, JSON sidecar.
**What can break:** Unicode paths (LaneMaker uses temp file workaround), GL not initialized (deferred load), missing project folders.
**Confirmed behavior:** `ProjectManager::save()` writes `.ogproj`; `MainWindow::saveToPath()` saves `.xodr` to temp file then copies; `MainWindow::loadFromPath()` defers if GL not ready.

### 8.8 Plugin Loading

**Why:** Allow third-party extensions (importers, exporters, road generators, etc.).
**What triggers it:** (Not currently triggered — plugin system is defined but not wired.)
**Who calls it:** Nobody currently `[CONFIRMED]`.
**Data movement:** (Designed: plugin directory → `QPluginLoader` → `IPlugin*` → `PluginManager::registerPlugin()`.)
**State changes:** (Designed: `PluginManager::m_plugins` list.)
**Output:** (Designed: registered plugins queryable by capability.)
**Confirmed behavior:** `PluginApi.hpp` defines the full ABI and `PluginLoader::discoverPlugins()` implementation exists, but `ApplicationContext` does not create a `PluginManager` and no plugins are shipped.

---

## 9. Execution Flows

### 9.1 Application Startup Flow

```
User launches OpenGeoStudio.exe
  ↓
main() [main.cpp:858]
  ↓
QApplication created
  ↓
Q_INIT_RESOURCE (LaneMaker shaders/images)
  ↓
App metadata set (name, version, org)
  ↓
Logger::addFileTransport("log.txt")
  ↓
Dark theme palette + stylesheet applied
  ↓
ApplicationContext created
  ├── EventBus
  ├── ProjectManager (with EventBus + autosave timer 60s)
  ├── WorkspaceManager (registers 5 workspaces)
  └── TerrainStore (with EventBus)
  ↓
AppMainWindow created
  ├── setupMenuBar() — File, View, Help
  ├── setupToolBar() — workspace tabs
  ├── setupStatusBar()
  ├── setupCenterWidget()
  │     ├── QStackedWidget
  │     ├── page 0: HomeWidget(ctx)
  │     ├── page 1: TerrainStudioWidget(ctx) [with MapViewportWidget if HAVE_MAPLIBRE]
  │     ├── page 2: RoadStudioWidget(ctx) [embeds LaneMaker::MainWindow, road mode]
  │     ├── page 3: TrainStudioWidget() [embeds LaneMaker::MainWindow, rail mode]
  │     └── page 4: Studio3DWidget(ctx) [OGRE-Next]
  ├── setupDockWidgets() — Project (left), Inspector (right)
  └── Connect signals: workspaceActivated, projectChanged, projectOpened
  ↓
mainWindow.show()
  ↓
If command-line .ogproj argument → openProjectPath() → deferred 3D Studio activation
  ↓
appLog().info("Started OpenGeoStudio with road engine version ...")
  ↓
app.exec() — Qt event loop
```

### 9.2 Workspace Switching Flow

```
User clicks workspace tab / presses Alt+1..4 / command palette
  ↓
WorkspaceManager::activate(id)
  ↓
Emits workspaceActivated(Workspace)
  ↓
AppMainWindow::onWorkspaceActivated(workspace)
  ├── m_centerStack->setCurrentIndex(index)
  ├── Show/hide docks based on workspace.panels
  ├── If "road-studio":
  │     ├── setupRoadStudioMenus()
  │     ├── showRoadStudioMenus(true)
  │     └── If pending road file → QTimer 2s → loadFromPath()
  ├── If not "road-studio":
  │     └── showRoadStudioMenus(false)
  ├── Update window title
  └── Update status bar
  ↓
Visible workspace widget shown
  ↓
If LaneMaker workspace: showEvent() rebinds g_mainWindow, g_mapViewGL
```

### 9.3 Road Drawing Flow

```
User presses R (Road tool)
  ↓
MainWidget::gotoCreateRoadMode() [main_widget.cpp:859]
  ↓
SetEditMode(LM::Mode_Create)
  ↓
Creates RoadCreationSession
  ↓
User clicks in viewport
  ↓
MapViewGL emits MousePerformedAction(MouseAction)
  ↓
MainWidget::OnMouseAction() [main_widget.cpp:2244]
  ↓
drawingSession->Update(MouseAction)
  ↓
RoadCreationSession::Update()
  ├── Snap to existing roads
  ├── Create staged geometry
  └── Update preview (temporary buffer)
  ↓
User presses Space or clicks confirm
  ↓
MainWidget::OnKeyPress() → confirmEdit() [main_widget.cpp:2558]
  ↓
RoadCreationSession::Complete()
  ├── Assemble RefLine from staged segments
  ├── Create LaneProfile from current LaneConfigWidget settings
  ├── Create new Road(profile, refLine)
  └── Road::Generate() → lane sections + graphics
  ↓
ChangeTracker::FinishRecordEdit()
  ↓
ActionManager::Record()
  ↓
refreshAllCustomGraphics() — markings, signs, furniture
  ↓
refreshObjectTree()
  ↓
MapViewGL::paintGL() — renders new road
```

### 9.4 OSM Import Flow

```
User clicks OSM import button in Road Studio
  ↓
OsmImportDialog::exec()
  ↓
User selects .osm file, configures settings
  ↓
OsmImportPipeline::importFromFile(path, settings)
  ├── OsmXmlParser::parseFile() → OsmData
  ├── CoordinateConverter::setReferenceOrigin() (from bounds)
  ├── RoadNetworkBuilder::build()
  │     ├── Filter highway ways
  │     ├── Project coordinates (equirectangular or UTM)
  │     ├── Douglas-Peucker simplification
  │     ├── Create LineSegments
  │     ├── Build RoadV2 objects
  │     ├── Detect shared nodes
  │     └── Build topology
  ├── JunctionDetector::detect()
  ├── RoadValidator::validateAndRepair()
  ├── RoundaboutGenerator::generate()
  ├── RoadMarkingGenerator::generateForNetwork()
  ├── TrafficSignGenerator::generateForNetwork()
  └── Return Result
  ↓
Dialog displays results (Summary, Roads, Junctions, Validation tabs)
  ↓
User clicks Export → OsmExporter::exportToOpenDrive() → .xodr
  ↓
User clicks "Open in Editor"
  ↓
RoadStudioWidget loads .xodr into LaneMaker MainWindow
  ↓
MainWindow::loadFromPath() → defers if GL not ready
  ↓
refreshAllCustomGraphics() + refreshObjectTree()
```

### 9.5 Terrain Export Flow

```
User Shift+drags on map to select area
  ↓
TerrainOverlayWidget converts screen rect to geo bounds
  ↓
TerrainStore::setBounds() → computeTileGrid()
  ↓
User configures export settings in ExportPanel
  ├── DEM source (AWS Terrarium, OpenTopo, GPXZ, Copernicus, local)
  ├── Imagery source (Google, ArcGIS, Mapbox, local)
  ├── Resolutions, CRS, API keys
  └── Mask settings
  ↓
User clicks Export
  ↓
ExportEngine::exportToDirectory(dir)
  ├── For each selected tile:
  │     ├── Download DEM (tiled/Copernicus/area/local)
  │     ├── Download imagery
  │     ├── DemDecoder::decodeAuto() — auto-detect format
  │     ├── Resample to target resolution (bilinear)
  │     ├── RasterWriter::writeFloat32GeoTiff() — heightmap
  │     └── RasterWriter::writeRgbGeoTiff() or writePngWithWorldFile() — imagery
  ├── Write merged outputs
  └── Write manifest.json
  ↓
Progress signals → ExportPanel progress bar
  ↓
finished(success, message) signal
```

---

## 10. Data Flow

### 10.1 Road Network Data Flow

```
OSM XML file
  ↓ OsmXmlParser
OsmData (nodes, ways, relations)
  ↓ CoordinateConverter
Projected nodes (local meters)
  ↓ RoadNetworkBuilder
RoadV2[] (geometry segments + lane sections)
  ↓ JunctionDetector
Junction[] (typed: T, X, Y, roundabout, overpass)
  ↓ RoadValidator
Issue[] (severity, category, message, fix)
  ↓ OsmExporter
OpenDRIVE .xodr  ←→  GeoJSON .geojson
  ↓ MainWindow::loadFromPath()
LaneMaker World (Road objects)
  ↓ Road::Generate()
Lane sections + SectionGraphics
  ↓ MapViewGL::AddQuads()
OpenGL vertex buffers
  ↓ paintGL()
Screen rendering
```

### 10.2 Terrain Data Flow

```
User area selection (geo bounds)
  ↓ TerrainStore::computeTileGrid()
TileGrid (tiles with bounds)
  ↓ ExportEngine
HTTP downloads (DEM tiles, imagery tiles)
  ↓ DemDecoder
RasterGrid (float elevations)
  ↓ GISProcessor::resample()
Resampled RasterGrid (target resolution)
  ↓ RasterWriter
GeoTIFF Float32 (heightmap) + GeoTIFF RGB / PNG (imagery)
  ↓ Filesystem
{project}/Terrain/*.tif + manifest.json
  ↓ OgreWidget::loadTerrain()
OGRE ManualObject terrain mesh
  ↓ OGRE render
Screen rendering
```

### 10.3 World Authoring Data Flow

```
User actions (place actor, edit spline, generate PCG)
  ↓ OgreWidget
World model (actors, layers, splines, PCG graphs)
  ↓ QUndoStack
Undo/redo commands
  ↓ World::saveToFile()
World JSON file
  ↓ World::loadFromFile()
Restored World
  ↓ OgreWidget::rebuildActor()
OGRE SceneNode + Item
  ↓ OGRE render
Screen rendering
```

### 10.4 Project Persistence Data Flow

```
Project state
  ├── Terrain state → Project.moduleState["terrain"] → .ogproj JSON
  ├── Road network → LaneMaker::MainWindow::saveToPath() → {project}/Roads/road.xodr
  ├── Road annotations → JSON sidecar → {project}/Roads/road.xodr.json
  ├── OSM project → OsmProjectSerializer → {project}/GIS/import.ogosm
  └── World scene → World::saveToFile() → {project}/Scene/world.json
  ↓
.ogproj file (JSON, schema: docs/ogproj-schema.json)
  ↓ ProjectManager::open()
Project restored + terrain state loaded + road file deferred-load
```

---

## 11. Important Classes

### AppMainWindow

| Attribute | Value |
|-----------|-------|
| **File** | `src/app/main.cpp` |
| **Lines** | 224-852 |
| **Purpose** | Main application window: menu bar, toolbar, dock widgets, workspace switching, project state |
| **Key methods** | `AppMainWindow(ApplicationContext*)`, `openProjectPath()`, `activate3DStudio()`, `onWorkspaceActivated()`, `onProjectChanged()`, `saveProjectState()` |
| **Called by** | `main()` |
| **Calls** | `ApplicationContext`, `WorkspaceManager`, `ProjectManager`, all workspace widgets |

### ApplicationContext

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/ApplicationContext.hpp` |
| **Lines** | 22-44 |
| **Purpose** | Central service container; owns EventBus, ProjectManager, WorkspaceManager, TerrainStore |
| **Key methods** | `events()`, `projects()`, `workspaces()`, `terrain()` |
| **Called by** | `main()`, `AppMainWindow`, workspace widgets |
| **Calls** | Service constructors |

### ProjectManager

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/project/ProjectManager.cpp/.hpp` |
| **Purpose** | Project CRUD, folder creation, recent projects, autosave |
| **Key methods** | `createWithFolder()`, `open()`, `save()`, `saveAs()`, `close()`, `markDirty()`, `addRecent()`, `deleteProject()`, `setAutosaveInterval()` |
| **Constants** | `kSubfolders` (15 subfolders: Terrain, GIS, Roads, Railway, Scene, Simulation, Infrastructure, Assets, Environment, Validation, Exports, Cache, Temp, Logs, Config), autosave interval 60s |
| **Emits** | `projectCreated`, `projectOpened`, `projectSaved`, `projectClosed`, `projectChanged`, `recentChanged` |

### WorkspaceManager

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/workspace/WorkspaceManager.cpp/.hpp` |
| **Purpose** | Manages 5 workspaces and activation |
| **Key methods** | `activate(id)`, `active()`, `activeId()`, `getById()` |
| **Workspaces** | home, terrain, road-studio, train-studio, 3d-studio |
| **Emits** | `workspaceActivated(Workspace)` |

### LaneMaker::MainWidget

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/ui/main_widget.cpp/.h` |
| **Lines** | .cpp 1-2736, .h 67-281 |
| **Purpose** | Main LaneMaker UI widget: OpenGL viewport, tool palette, property panels |
| **Key methods** | `Instance()` (singleton), `SetRailMode()`, `UseSharedSatelliteView()`, `OnMouseAction()`, `OnKeyPress()`, `gotoCreateRoadMode()`, `gotoDragMode()`, `confirmEdit()`, `SetEditMode()`, `refreshObjectTree()`, `refreshAllCustomGraphics()`, `onSplitRoad()`, `onMergeRoads()`, `onReverseRoad()` |
| **Singleton** | `static MainWidget* instance` |
| **Globals set** | (via `MainWindow`) `g_mainWindow`, `g_mapViewGL`, `g_laneConfig` |

### LaneMaker::MapViewGL

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/engine/map_view_gl.cpp/.h` |
| **Purpose** | QOpenGLWidget rendering: road geometry, map background, vehicles |
| **Key methods** | `ResetCamera()`, `SetViewMode()`, `SetMapCenter()`, `UpdateMapTiles()`, `AddQuads()`, `AddLine()`, `AddInstance()`, `isGLInitialized()` |
| **Buffers** | `permanentBuffer` (roads), `temporaryBuffer` (previews), `backgroundBuffer`, `vehicleBuffer[]` (instanced) |
| **Globals** | `g_mapViewGL`, `g_PointerRoadID`, `g_PointerRoadS`, `g_PointerLane`, `g_PointerOnGround`, `g_CameraPosition`, `g_createRoadElevationOption`, `g_PointerVehicle`, `touchScreen` |

### LaneConfigWidget

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/widgets/LaneConfigWidget.cpp/.h` |
| **Purpose** | Cross-Section Studio: profile selector, lane editor, metadata controls |
| **Key methods** | `LaneConfigWidget(vertical, showProfileSelector)`, `Reset()`, `SetOption()`, `GotoRoadMode()`, `GotoRailMode()`, `SetRoadModeOnly()`, `SetRailModeOnly()`, `LoadProfile()`, `PopulateRoadProfiles()`, `PopulateRailProfiles()`, `CurrentProfileKey()`, `IsModifiedFromProfile()` |
| **Signals** | `ProfileChanged(key)`, `RoadMetadataChanged(speedLimit, hasSidewalk, hasCurb)` |
| **State** | `currentProfileKey`, `modifiedFromProfile`, `applyingProfile`, `hasProfileSelector`, `loadedProfile` |
| **Global** | `g_laneConfig` (set in constructor) |

### LaneMaker::Road

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/xodr/road.cpp/.h` |
| **Purpose** | Single road with geometry, lanes, junctions, graphics |
| **Key methods** | `Road(profile, geometry)`, `Generate()`, `ReverseRefLine()`, `Length()`, `ID()`, `SplitRoad()`, `JoinRoads()`, `ModifyProfile()`, `GenerateAllSectionGraphics()`, `FirstOverlap()`, `AllOverlaps()` |
| **Members** | `odr::Road generated`, `successorJunction`, `predecessorJunction`, `s_to_section_graphics` |

### LaneMaker::AbstractJunction / Junction / DirectJunction

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/xodr/junction.cpp/.h` |
| **Purpose** | Junction management: connecting roads, turning semantics, degeneration |
| **Key methods** | `CreateFrom()`, `Attach()`, `NotifyPotentialChange()`, `DetachNoRegenerate()`, `Degenerate()`, `GenerateGraphics()`, `GetTurningSemanticsForIncoming()` |

### road_v2::RoadV2

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/road/road_v2.hpp` (internal) and `src/engine/road/road_engine/public/road_v2.hpp` (facade) |
| **Purpose** | Road data model with geometry segments and lane sections |
| **Note** | Dual-layer: internal uses `lane_engine.hpp` `LaneSection`; public facade has placeholder `LaneSection` |

### World

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/world/World.hpp` |
| **Purpose** | Pure data model for 3D world: actors, layers, splines, PCG, water, terrain tiles, masks |
| **Key methods** | `addActor()`, `removeActor()`, `findActor()`, `select()`, `addLayer()`, `addSpline()`, `addPCGGraph()`, `addTerrainTile()`, `addMask()`, `addWater()`, `validate()`, `toJson()`, `fromJson()`, `saveToFile()`, `loadFromFile()` |

### OgreWidget

| Attribute | Value |
|-----------|-------|
| **File** | `src/ui/studio3d/OgreWidget.cpp/.hpp` |
| **Purpose** | QWindow embedding OGRE-Next viewport |
| **Key methods** | `loadTerrain()`, `loadRoads()`, `addActor()`, `removeActor()`, `updateActorTransform()`, `generateBuildings()`, `generateVegetation()`, `setSunDirection()`, `screenToWorld()`, `pickActor()` |
| **Signals** | `actorSelected`, `actorTransformed`, `actorAdded`, `actorRemoved`, `sceneChanged`, `worldChanged` |

### ExportEngine

| Attribute | Value |
|-----------|-------|
| **File** | `src/ui/terrain/ExportEngine.cpp/.hpp` |
| **Purpose** | DEM and imagery download + processing + file writing |
| **Key methods** | `exportToDirectory(dir)` |
| **Signals** | `progress(percent, stage)`, `finished(success, message)` |

### OsmImportPipeline

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/osm/OsmImportPipeline.hpp` |
| **Purpose** | Complete OSM import orchestration |
| **Key methods** | `importFromFile()`, `importFromString()`, `parseAndSetup()`, `buildRoads()`, `detectJunctions()`, `validateAndRepair()` |

---

## 12. Important Functions

### main()

| Attribute | Value |
|-----------|-------|
| **File** | `src/app/main.cpp` |
| **Line** | 858 |
| **Purpose** | Application entry point |
| **Calls** | QApplication, ApplicationContext, AppMainWindow |

### AppMainWindow::onWorkspaceActivated()

| Attribute | Value |
|-----------|-------|
| **File** | `src/app/main.cpp` |
| **Line** | 595 |
| **Purpose** | Switch workspace: stack index, docks, menus, deferred road load |
| **Calls** | `m_centerStack->setCurrentIndex()`, `setupRoadStudioMenus()`, `showRoadStudioMenus()` |

### ProjectManager::createWithFolder()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/project/ProjectManager.cpp` |
| **Line** | 53 |
| **Purpose** | Create new project with folder structure (15 subfolders) |
| **Calls** | `QDir::mkpath()`, `Project::toJson()`, `QFile::write()`, `EventBus::publish("project:created")` |

### ProjectManager::open()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/project/ProjectManager.cpp` |
| **Line** | 83 |
| **Purpose** | Open existing .ogproj file |
| **Calls** | `QFile::readAll()`, `Project::fromJson()`, `addRecent()`, `EventBus::publish("project:opened")` |

### MainWidget::confirmEdit()

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/ui/main_widget.cpp` |
| **Line** | 2558 |
| **Purpose** | Complete drawing session, create Road, record in ChangeTracker |
| **Calls** | `drawingSession->Complete()`, `ChangeTracker::FinishRecordEdit()`, `refreshAllCustomGraphics()`, `refreshObjectTree()` |

### MainWidget::SetEditMode()

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/ui/main_widget.cpp` |
| **Line** | 2649 |
| **Purpose** | Switch edit mode and create appropriate drawing session |
| **Creates** | `RoadCreationSession`, `LanesCreationSession`, `RoadDestroySession`, `RoadModificationSession`, `LaneFlipSession` |

### Road::Generate()

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/xodr/road.cpp` |
| **Line** | 74 |
| **Purpose** | Generate lane sections, markings, lane borders, graphics |
| **Calls** | `LaneProfile::Apply()`, `GenerateAllSectionGraphics()`, junction notification |

### LaneProfile::Apply()

| Attribute | Value |
|-----------|-------|
| **File** | `src/engine/lanemaker/libOpenDRIVE/include/road_profile.h` |
| **Line** | 79 |
| **Purpose** | Convert LaneProfile to OpenDRIVE lane sections |
| **Calls** | `_MakeTransition()`, `_MakeStraight()`, `_ComputeMedian()`, `_MergeSides()` |

### RoadNetworkBuilder::build()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/osm/RoadNetworkBuilder.hpp` |
| **Line** | 87 |
| **Purpose** | Convert OSM ways to RoadV2 roads with topology |
| **Algorithm** | Filter highways → project coords → Douglas-Peucker → LineSegments → RoadV2 → shared nodes → topology |

### JunctionDetector::detect()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/osm/JunctionDetector.hpp` |
| **Line** | 76 |
| **Purpose** | Detect and classify junctions from topology nodes |
| **Algorithm** | Iterate nodes with degree ≥ 2 → classify by road count and angles → detect roundabouts |

### OsmExporter::exportToOpenDrive()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/osm/OsmExporter.hpp` |
| **Line** | 34 |
| **Purpose** | Export RoadV2 roads to OpenDRIVE .xodr XML |
| **Calls** | `DemElevationSampler::sampleLonLat()` for elevation profiles |

### ExportEngine::exportToDirectory()

| Attribute | Value |
|-----------|-------|
| **File** | `src/ui/terrain/ExportEngine.cpp` |
| **Purpose** | Full terrain export pipeline: download → decode → resample → write |
| **Calls** | `downloadDemForTile()`, `downloadImageryForTile()`, `DemDecoder::decodeAuto()`, `RasterWriter::writeFloat32GeoTiff()` |

### RasterWriter::writeFloat32GeoTiff()

| Attribute | Value |
|-----------|-------|
| **File** | `src/ui/terrain/RasterWriter.cpp` |
| **Purpose** | Write Float32 GeoTIFF with proper geo tags |
| **Calls** | libtiff `TIFFOpen` (via custom Unicode-safe I/O), `TIFFMergeFieldInfo`, GeoKey directory setup |

### OgreWidget::loadTerrain()

| Attribute | Value |
|-----------|-------|
| **File** | `src/ui/studio3d/OgreWidget.cpp` |
| **Line** | 411 |
| **Purpose** | Load terrain from GeoTIFF heightmap + albedo into OGRE |
| **Calls** | `DemDecoder::decode()`, ManualObject creation, PBS datablock, albedo texture load |

### World::validate()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/world/World.hpp` |
| **Line** | 356 |
| **Purpose** | Validate world: duplicate IDs, broken references, missing assets |
| **Returns** | `QList<ValidationError>` |

### PCGEngine::evaluate()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/world/PCGEngine.hpp` |
| **Line** | 47 |
| **Purpose** | Evaluate PCG graph to generate points |
| **Algorithm** | Kahn topological sort → evaluate nodes in order → apply filters (slope, mask, random) → apply transforms |

### CoordinateConverter::project()

| Attribute | Value |
|-----------|-------|
| **File** | `src/core/osm/CoordinateConverter.hpp` |
| **Line** | 71 |
| **Purpose** | Project WGS84 lat/lon to local meters |
| **Algorithm** | Equirectangular approximation (small areas) or full UTM (WGS84 ellipsoid) |

---

*End of Part 1. Continue to `docs/REVERSE_ENGINEERING_PART2_SYSTEMS.md` for Sections 13–25.*
