# OpenGeoStudio-Qt — Reverse-Engineering & Developer Guide
## Part 3 of 3: Quality, Risk & Developer Guide (Sections 26–34)

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

This is Part 3 of a 3-part reverse-engineering document.
- **Part 1** (`docs/REVERSE_ENGINEERING_PART1_ARCHITECTURE.md`): Sections 0–12 — Start Here, Executive Summary, Repository Structure, Technology Stack, Entry Points, Architecture, Module Map, Dependency Graph, Core Logic, Execution Flows, Data Flow, Important Classes, Important Functions.
- **Part 2** (`docs/REVERSE_ENGINEERING_PART2_SYSTEMS.md`): Sections 13–25.
- **Part 3** (this file): Sections 26–39 — Duplicate/Dead Code, Security, Technical Debt, Change Impact Map, Safe vs High-Risk Areas, New Developer Guide, AI Repository Context, Master System Map, Improvement Roadmap (P0/P1/P2), Verification Backlog, Change/Test Workflows, Do Not Touch Rules, Developer Recipes, Persistence & Versioning Strategy.

---

## 26. Duplicate/Dead Code

### 26.1 Dual RoadV2 Layers (Intentional)

**Files:**
- `src/engine/road/road_v2.hpp` (internal full model)
- `src/engine/road/road_engine/public/road_v2.hpp` (public API facade)

**Status:** Intentional `[CONFIRMED]` — documented in `docs/ROAD_V2_DUAL_LAYERS.md`. The internal model uses `lane_engine.hpp`'s full `LaneSection`; the public facade has a self-contained placeholder `LaneSection`. Both must stay in sync for the RoadV2 class surface.

**Risk:** If the two diverge, OSM pipeline (uses internal) and external consumers (use public) will break. AGENTS.md warns: "OSM headers that use RoadV2 must only be included from `.cpp` files in the main app (not from headers) to avoid conflict."

### 26.2 Legacy Web-Stack Remnants

**Status:** Removed `[CONFIRMED]` — the TypeScript/Electron stack was fully removed. Commit history includes `PHASE_8_WEB_STACK_REMOVAL`. No TypeScript, JavaScript, Node, Electron, React, or Vite files remain in the repository.

### 26.3 Hidden but Compiled Road Studio Modes

**Status:** `[CONFIRMED]` — `main_widget.cpp` still contains implementation code for inspector, road operations (split, merge, reverse), markings, signs, furniture, roundabouts, measurement, and other systems. The visible tool palette was simplified to only Road (Draw New Road) and View, but the underlying mode implementation (`gotoPlaceSignMode`, `gotoPlaceMarkingMode`, `gotoCreateRoundaboutMode`, `gotoMeasureMode`, etc.) still exists and is compiled.

**Risk:** Dead UI code increases binary size and maintenance burden. However, removing it would require careful untangling of signal/slot connections and object tree population code.

### 26.4 Duplicate UI Widgets

**LaneConfigWidget instances:** `[CONFIRMED]` Two instances can exist:
1. Main sidebar instance in `MainWidget` (with profile selector)
2. Popup instance in `DrawOptionDialog` (without profile selector, `verticalLayout=true, showProfileSelector=false`)

This is intentional after the preset-crash fix. The popup instance is a simple lane editor; the sidebar instance has full profile integration.

### 26.5 Duplicate Serializers

**Road network serialization:**
- `OsmExporter::exportToOpenDrive()` — OSM pipeline → .xodr
- `MainWindow::saveToPath()` — LaneMaker → .xodr (via libOpenDRIVE)

These are separate code paths that produce the same format `[CONFIRMED]`. Not strictly duplicate (different producers), but both write OpenDRIVE.

**World serialization:**
- `World::saveToFile()` / `loadFromFile()` — JSON
- `World::toJson()` / `fromJson()` — JSON (used by saveToFile)

These are layered, not duplicate `[CONFIRMED]`.

### 26.6 Unused Dependencies

**gtest** `[CONFIRMED]`: Listed in `vcpkg.json` but no test uses Google Test. All tests use doctest or custom runners. Likely a leftover from migration planning.

### 26.7 Old Docs/Artifacts

**Status:** Cleaned `[CONFIRMED]` — old planning/audit docs under `docs/` were deleted in commit `3aad9ec`. Retained docs: `README.md`, `AGENTS.md`, `PORTABLE_README.txt`, `docs/CROSS_SECTION_STUDIO.md`, `docs/ROAD_V2_DUAL_LAYERS.md`, `docs/ogproj-schema.json`.

### 26.8 Stale Tests

**`test_road_studio_ui`** `[CONFIRMED]`: Created under `if(BUILD_TESTING)` (line 719) while all other tests are under `if(BUILD_TESTS)` (line 515). If `BUILD_TESTING` is not set (it's set by `enable_testing()` which is called conditionally), this target may not build. Potential CMake condition mismatch.

### 26.9 Global State Duplication

**LaneMaker globals** `[CONFIRMED]`: `g_mainWindow`, `g_laneConfig`, `g_mapViewGL` are single pointers that get rebound. There is only one set of globals, but they serve two workspaces (Road Studio + Train Studio), creating effective state duplication when switching.

### 26.10 Generated Files Checked Into Source

**None found** `[CONFIRMED]` — no generated MOC files, no generated Qt resource C++ files, no build artifacts in source tree.

---

## 27. Security

### 27.1 Filesystem Access

**Project files:** Read/written under `C:/OpenGeoStudio/Projects/` (default) or user-selected directory.
**Path validation:** `[UNKNOWN]` — no explicit path traversal validation found. Project open uses user-selected file dialog, which limits risk.
**Temp files:** LaneMaker uses temp files for Unicode path workaround (`MainWindow::saveToPath`/`loadFromPath`).

### 27.2 Archive/Package Handling

**No archive handling** `[CONFIRMED]` — no zip/tar/rar extraction code found.

### 27.3 XML/JSON Parsing

**OSM XML:** `QXmlStreamReader` — Qt's XML parser is generally safe against XML bombs, but `[UNKNOWN]` whether `QXmlStreamReader` has entity expansion limits configured.
**OpenDRIVE XML:** libOpenDRIVE parser `[UNKNOWN]` — security of embedded parser not verified.
**JSON:** `QJsonDocument` — Qt's JSON parser, generally safe.

### 27.4 Network Requests

**HTTPS:** `[UNKNOWN]` — `QNetworkAccessManager` supports HTTPS but it's unclear if all providers use HTTPS. Tile downloads from Google/Esri likely use HTTPS.
**Authentication:** API keys for OpenTopo, Mapbox, MapTiler, GPXZ, Stadia. Stored in `ExportSettings` and `SettingsDialog`.
**Rate limiting:** Nominatim geocoding has 300ms debounce + 1000ms rate limiting `[CONFIRMED]`. Other providers `[UNKNOWN]`.

### 27.5 External URLs

**Hard-coded URLs:**
- Esri World Imagery style (inline JSON in `MapViewportWidget.cpp`)
- Google Maps satellite tiles (`MapViewGL::requestTile`)
- Nominatim geocoding API (`SearchBar.hpp`)
- GPXZ API (`test_gpxz_download.cpp`)
- Various DEM provider URLs in `ExportEngine`

### 27.6 Plugin Loading

**Risk:** `QPluginLoader` loads shared libraries — a malicious plugin could execute arbitrary code.
**Mitigation:** Plugin system is not wired `[CONFIRMED]`, so no runtime risk currently.
**Future risk:** When wired, plugin directory should be trusted/admin-owned only.

### 27.7 Dynamic Libraries

**OGRE-Next:** Loads plugins via `plugins.cfg` (`OgreWidget` init). Plugin DLLs are in deploy directory.
**Risk:** DLL search path — if deploy directory is writable, DLL hijacking possible `[INFERENCE]`.

### 27.8 Command/Script Execution

**None found** `[CONFIRMED]` — no `system()`, `popen()`, `QProcess::execute()` calls found in application code.

### 27.9 Environment Variable Use

**`QT_QPA_PLATFORM=offscreen`** — set by `test_road_studio_ui.cpp` for headless testing.
**MSVC environment** — required for builds (vcvars64.bat).
**No other environment variable use found** `[CONFIRMED]`.

### 27.10 Untrusted OSM/GeoTIFF/OpenDRIVE Inputs

**OSM:** User-selected files; parsed by `QXmlStreamReader`. Risk: malformed XML, very large files blocking UI.
**GeoTIFF:** User-selected or downloaded; parsed by libtiff. Risk: malformed TIFF `[UNKNOWN]` — libtiff security depends on version.
**OpenDRIVE:** User-selected; parsed by libOpenDRIVE. Risk: malformed XML `[UNKNOWN]`.

### 27.11 Secrets/Authentication

**Hard-coded API key** `[CONFIRMED]` `[RISK]`: `test_gpxz_download.cpp` contains `ak_NgEXLGho_z5TBKb44GCFKIirC` (line ~100). This is a real GPXZ API key committed to the repository — **security risk**.

> **P0 ACTION ITEM** — See Section 34.1, item #1. Rotate this key immediately at GPXZ, replace with env var `GPXZ_API_KEY` or CI secret, and scrub from git history if possible.

**User-entered API keys:** Stored in `ExportSettings` and `SettingsDialog`; persisted in `.ogproj` `moduleState` `[CONFIRMED]` `[RISK]` — API keys are written to project files in **plaintext**.

> **P0 ACTION ITEM** — See Section 34.1, item #2. Move API keys to Windows Credential Manager / OS secure storage; reference by provider name in `.ogproj`.

`[RECOMMENDATION]` Do not copy the API key value into any new documentation, code, or commits. Reference it by filename only.

### 27.12 Unsafe Deserialization

**cereal binary:** `ActionManager` uses cereal for binary serialization of action history. Deserializing untrusted `.dat` files could be risky `[INFERENCE]`.
**World JSON:** `World::fromJson()` — JSON deserialization, generally safe.
**Project JSON:** `Project::fromJson()` — JSON deserialization, generally safe.

### 27.13 Temporary Files

**LaneMaker temp files:** Created for Unicode path workaround `[CONFIRMED]`. Cleanup behavior `[UNKNOWN]` — may leave temp files on crash.

### 27.14 DLL Search Paths

**Deploy directory:** OGRE-Next DLLs copied to deploy directory. Windows DLL search order includes the application directory, so this is standard `[CONFIRMED]`.

### 27.15 Unverified Security Behavior

- XML entity expansion limits `[UNKNOWN]`
- libtiff version and known vulnerabilities `[UNKNOWN]`
- libOpenDRIVE parser security `[UNKNOWN]`
- HTTPS enforcement for all providers `[UNKNOWN]`
- API key storage encryption `[CONFIRMED — none, plaintext]`

---

## 28. Technical Debt

### 28.1 High-Risk Ownership/Global-State Areas

**LaneMaker global pointers** `[CONFIRMED]` `[RISK]`: `g_mainWindow`, `g_laneConfig`, `g_mapViewGL`, `g_PointerRoadID`, `g_PointerRoadS`, `g_PointerLane`, `g_PointerOnGround`, `g_CameraPosition`, `g_createRoadElevationOption`, `g_PointerVehicle`, `touchScreen`, `g_preference`.

These are process-wide globals rebound on `showEvent()`. Road Studio and Train Studio share them. **This is the #1 architectural risk** (see Part 1, Section 0 "Critical Architecture Constraint"):
- If both workspaces are ever visible simultaneously, globals point to the last-shown instance.
- `World::Instance()` singleton is shared — both workspaces' roads are in the same `World` set.
- `ActionManager::Instance()` singleton is shared — action history is mixed.
- `MainWidget::instance` singleton — last-constructed MainWidget wins.

`[RECOMMENDATION]` Refactor LaneMaker to use instance-based state instead of globals, or ensure only one LaneMaker instance is alive at a time. See Section 37.2 (Require Architectural Review).

### 28.2 Header Conflicts

**Dual `road_v2.hpp`** `[CONFIRMED]` `[RISK]`: Including the wrong one causes `LaneSection` type mismatch. Mitigated by AGENTS.md convention but not enforced by build system. `[RECOMMENDATION]` Consider namespacing or renaming one of the headers to make the conflict impossible.

### 28.3 Duplicated Data Models

**Road representations** `[CONFIRMED]` `[RISK]`:
- `road_v2::RoadV2` (road engine)
- `LM::Road` (LaneMaker, wraps `odr::Road`)
- `roads::Road` (Road Studio UI, legacy with `ControlPoint[]`)

Three different road representations with conversion adapters (`road_adapter.hpp`). This is intentional but creates maintenance overhead. See Part 1, Section 0 "Road Model Relationship" for the authority diagram.

### 28.4 Fragile UI State

**LaneConfigWidget signal flow** `[CONFIRMED]` `[RISK]`: `SetOption()` → `OnOptionChange()` → `CheckModified()` → may record actions. During profile loading (`applyingProfile=true`), signal blocking must be correct or unintended actions are recorded. `[RECOMMENDATION]` See Section 37.1 (Do Not Touch Rules) before modifying.

**Mode switching with `show()`** `[CONFIRMED]`: `GotoRoadMode()`/`GotoRailMode()` call `show()`, which was undesirable for embedded initialization. Fixed by adding `SetRoadModeOnly()`/`SetRailModeOnly()`, but the original methods still exist and could be misused.

### 28.5 Missing Validation

**`TerrainWorldBridge::sampleHeight()`** `[CONFIRMED]` `[RISK]`: Returns 0 (stub). World-side height sampling is not functional. This means PCG surface alignment and terrain-projected splines don't work from the world model side. `[RECOMMENDATION]` Wire to actual DEM sampling (Roadmap item #10).

**Plugin system** `[CONFIRMED]`: Defined but not wired. No validation of plugin metadata at runtime. `[RECOMMENDATION]` Wire into `ApplicationContext` (Roadmap item #5).

### 28.6 Incomplete Persistence

**Project state split** `[CONFIRMED]` `[RISK]`: Terrain state in `.ogproj`, road network in `.xodr`, world scene in `world.json`. **No transactional save** — if one file fails, state is inconsistent. No `schemaVersion` field — no migration path. `[RECOMMENDATION]` See Section 39 (Persistence & Versioning Strategy) for the full recommended approach.

**Action replay autosave** `[CONFIRMED]`: `ActionManager::AutosavePath()` creates `action_rec__{timestamp}.dat` files. Cleanup on success, but `[UNKNOWN]` if cleanup happens on crash.

### 28.7 Legacy Code

**LaneMaker** `[CONFIRMED]`: Embedded engine with its own spdlog, cereal, CGAL dependencies. AGENTS.md says "leave untouched." This creates a frozen legacy island.

**Hidden Road Studio modes** `[CONFIRMED]`: Mode implementation for signs, markings, furniture, roundabouts, measurement still compiled but not accessible from simplified UI. `[RECOMMENDATION]` Either remove dead code or re-enable as optional features (Roadmap item #18).

### 28.8 Build Reproducibility Issues

**Hardcoded paths** `[CONFIRMED]` `[RISK]`:
- OGRE-Next: `D:/git/ogre-next` (CMakeLists.txt line 388)
- MapLibre: `D:/git/maplibre-native-qt/install` (CMakeLists.txt hint)
- vcpkg: `C:/dev/vcpkg` (AGENTS.md)
- Qt: `C:/Qt/6.8.0/msvc2022_64` (AGENTS.md)

These paths are machine-specific and not portable. `[RECOMMENDATION]` Use CMake cache variables with `find_package` (Roadmap item #9, #28).

**CI missing vcpkg** `[CONFIRMED]` `[RISK]`: `ci.yml` does not pass `CMAKE_TOOLCHAIN_FILE` for vcpkg. CI may fail to find vcpkg dependencies. `[RECOMMENDATION]` Pass toolchain file or install vcpkg on runners (Roadmap item #6).

### 28.9 Undocumented Contracts

- LaneMaker global rebinding behavior (only documented in AGENTS.md and this RE doc)
- `applyingProfile` flag semantics in `LaneConfigWidget`
- `G_TEST` define behavior in LaneMaker test mode
- `GraphicsDivision = 10` constant rationale
- `MaxRoadVertices` / `MaxTemporaryVertices` limits

### 28.10 Incomplete Tests

- Plugin system: no tests
- MapLibre: no tests
- OGRE-Next: no tests
- UI: only offscreen smoke test
- Concurrency: no tests
- Performance: no benchmarks
- CTest registration: only `geometry_segment_tests` registered

---

## 29. Change Impact Map

### 29.1 RoadV2 (internal)

**Files affected:** `src/engine/road/road_v2.hpp`, `lane_engine.hpp`, `lane_geometry.hpp`, `lane_sampling.hpp`, `lane_network.hpp`, `road_mark_generator.hpp`, `road_mesh_generator.hpp`, `road_adapter.hpp`
**Consumers affected:** OSM pipeline (`RoadNetworkBuilder`, `JunctionDetector`, `RoadValidator`, `LaneGenerator`, `RoundaboutGenerator`, `RoadMarkingGenerator`, `TrafficSignGenerator`, `OsmExporter`, `OsmProjectSerializer`), LaneMaker (via `road_adapter.hpp`), tests (`geometry_segment_tests`, `test_osm_pipeline`, `test_houston_roundtrip`)
**Risk:** HIGH — changes ripple through entire OSM pipeline and LaneMaker.

### 29.2 RoadV2 (public facade)

**Files affected:** `src/engine/road/road_engine/public/road_v2.hpp`
**Consumers affected:** External consumers via `road_engine.hpp`, tests
**Risk:** MEDIUM — must stay in sync with internal model.

### 29.3 LaneMaker Engine

**Files affected:** `src/engine/lanemaker/` (all subdirectories)
**Consumers affected:** `RoadStudioWidget`, `TrainStudioWidget`, `test_road_studio`, `test_road_studio_ui`, `test_houston_roundtrip`
**Risk:** HIGH — embedded engine with global state; changes affect both Road and Train Studio.

### 29.4 LaneConfigWidget

**Files affected:** `src/engine/lanemaker/widgets/LaneConfigWidget.cpp/.h`
**Consumers affected:** `MainWidget` (sidebar), `DrawOptionDialog` (popup), `ActionManager` (replay), `test_road_studio`
**Risk:** HIGH — signal flow is delicate; `applyingProfile` flag must be correct.

### 29.5 MapViewGL

**Files affected:** `src/engine/lanemaker/engine/map_view_gl.cpp/.h`
**Consumers affected:** `MainWidget`, `MainWindow` (showEvent rebinds `g_mapViewGL`), `test_road_studio_ui`
**Risk:** HIGH — OpenGL context, global pointer, rendering pipeline.

### 29.6 Workspace Switching

**Files affected:** `src/app/main.cpp` (`AppMainWindow::onWorkspaceActivated`), `src/core/workspace/WorkspaceManager.cpp`
**Consumers affected:** All workspace widgets, dock widgets, menu bar
**Risk:** MEDIUM — central control flow; bugs affect all workspaces.

### 29.7 OSM Schema

**Files affected:** `src/core/osm/OsmTypes.hpp`, `OsmXmlParser.hpp`, `OsmProjectSerializer.hpp`
**Consumers affected:** Entire OSM pipeline, `OsmImportDialog`, `RailOsmImportDialog`, tests
**Risk:** MEDIUM — header-only, changes propagate widely.

### 29.8 Project Persistence

**Files affected:** `src/core/project/Project.hpp`, `ProjectManager.cpp`, `docs/ogproj-schema.json`
**Consumers affected:** `AppMainWindow`, `HomeWidget`, all workspaces that read `moduleState`
**Risk:** MEDIUM — backward compatibility with existing `.ogproj` files.

### 29.9 Resources

**Files affected:** `resources/app_icons.qrc`, `resources/road_studio/road_studio.qrc`, LaneMaker `images.qrc`/`shaders.qrc`
**Consumers affected:** Application UI, LaneMaker rendering
**Risk:** LOW — adding resources is safe; removing may break UI.

### 29.10 CMake/vcpkg

**Files affected:** `CMakeLists.txt`, `vcpkg.json`
**Consumers affected:** All targets, CI, deployment
**Risk:** MEDIUM — build system changes affect all development.

### 29.11 Plugin ABI

**Files affected:** `src/plugin/PluginApi.hpp`
**Consumers affected:** Future plugins (none currently)
**Risk:** LOW — not wired, no current consumers.

---

## 30. Safe vs High-Risk Areas

### 30.1 Safe to Modify

| Area | Rationale | Affected Consumers |
|------|-----------|-------------------|
| `resources/` (adding icons) | Additive, no breaking changes | UI |
| `docs/` | Documentation only | None |
| `scripts/` | Build tooling, not runtime | Build/deploy |
| `HomeWidget` | Self-contained, no globals | Home workspace |
| `SearchBar` | Self-contained, no globals | Terrain Studio |
| `LayerStack` | Self-contained, no globals | Terrain Studio |
| `ExportPanel` UI layout | Widget layout, no logic changes | Terrain Studio |
| `NPanel` | Simple overlay panel | 3D Studio |
| `EditorPanels` (WorldOutliner, LayerPanel, ContentBrowser) | UI panels, delegate to OgreWidget | 3D Studio |
| `TerrainTypes.hpp` (adding enum values) | Additive | Terrain pipeline |
| `RoadClassifier` defaults | Configuration values | OSM pipeline |
| `RoadProfileCatalog` / `RailProfileCatalog` (adding profiles) | Additive | LaneConfigWidget |

### 30.2 Modify with Tests

| Area | Rationale | Affected Consumers | Tests to Run |
|------|-----------|-------------------|--------------|
| `geometry_segment.hpp` | Core geometry, well-tested | Road engine, OSM pipeline, LaneMaker | `geometry_segment_tests` |
| `lane_engine.hpp` / `lane_geometry.hpp` | Lane model, well-tested | Road engine, OSM pipeline | `geometry_segment_tests` |
| `World` model | Pure data, well-tested | 3D Studio, tests | `test_world_model`, `test_world_workflow` |
| `WorldBuilder` / `SplineEditor` | World construction | 3D Studio, tests | `test_world_workflow`, `test_houston_roundtrip` |
| `PCGEngine` | PCG evaluation | 3D Studio, tests | `test_world_model` |
| `OsmXmlParser` | XML parsing | OSM pipeline | `test_osm_pipeline` |
| `CoordinateConverter` | Coordinate math | OSM pipeline | `test_osm_pipeline` |
| `RoadNetworkBuilder` | Network construction | OSM pipeline | `test_osm_pipeline` |
| `JunctionDetector` | Junction detection | OSM pipeline | `test_osm_pipeline` |
| `LaneGenerator` | Lane generation | OSM pipeline | `test_osm_pipeline` |
| `RoundaboutGenerator` | Roundabout generation | OSM pipeline | `test_osm_pipeline` |
| `RoadMarkingGenerator` | Marking generation | OSM pipeline | `test_osm_pipeline` |
| `TrafficSignGenerator` | Sign generation | OSM pipeline | `test_osm_pipeline` |
| `OsmExporter` | Export | OSM pipeline | `test_osm_pipeline`, `test_houston_roundtrip` |
| `OsmProjectSerializer` | Persistence | OSM pipeline | `test_osm_pipeline` |
| `RasterWriter` | GeoTIFF writing | Terrain pipeline | `test_geotiff_writer`, `test_terrain_pipeline` |
| `DemDecoder` | DEM decoding | Terrain pipeline, 3D Studio | `test_terrain_pipeline`, `test_gpxz_download` |
| `GISProcessor` | Geospatial processing | Terrain pipeline | `test_terrain_pipeline` |
| `TerrainAnalyzer` | Terrain analysis | Terrain pipeline | `test_terrain_pipeline` |
| `MaskManager` | Mask generation | Terrain pipeline | `test_terrain_pipeline` |
| `TileManager` | Tile management | Terrain pipeline | `test_terrain_pipeline` |
| `ValidationManager` | Terrain validation | Terrain pipeline | `test_terrain_pipeline` |
| `ProjectManager` | Project CRUD | App, all workspaces | `test_world_workflow` |
| `WorkspaceManager` | Workspace switching | App, all workspaces | (no direct tests — run app) |
| `ExportEngine` | Terrain export | Terrain Studio | `test_terrain_pipeline` |

### 30.3 High Risk

| Area | Rationale | Affected Consumers |
|------|-----------|-------------------|
| `LaneConfigWidget` signal flow | Delicate signal/slot chains, `applyingProfile` flag | MainWidget, DrawOptionDialog, ActionManager |
| `MapViewGL` OpenGL rendering | GL context, global pointer, buffer management | MainWidget, MainWindow, tests |
| `MainWindow::showEvent()` | Global rebinding | Road Studio, Train Studio |
| `MainWindow::saveToPath()`/`loadFromPath()` | Unicode temp file workaround, deferred load | Road Studio, Train Studio |
| `Road::Generate()` | Lane sections, graphics, junction notification | LaneMaker, all road operations |
| `AbstractJunction` / `Junction` / `DirectJunction` | Junction regeneration, degeneration | LaneMaker, road topology |
| `ActionManager` | Replay determinism, buffering | LaneMaker, testing |
| `road_adapter.hpp` | Legacy ↔ RoadV2 conversion | LaneMaker, OSM pipeline |
| `OgreWidget` init | OGRE-Next setup, D3D11, HLMS, compositor | 3D Studio |
| `OgreWidget::loadTerrain()` | DEM decode + mesh generation | 3D Studio |
| `ExportEngine` download logic | Multiple providers, mosaic, Copernicus multi-cell | Terrain Studio |
| `PathHelper` | Windows 8.3 short path conversion | All libtiff users |

### 30.4 Do Not Change Without Architectural Review

| Area | Rationale |
|------|-----------|
| LaneMaker global pointer system (`g_mainWindow`, `g_laneConfig`, `g_mapViewGL`) | Process-wide singleton rebinding; affects both Road and Train Studio |
| `World::Instance()` singleton in LaneMaker | Shared between Road and Train Studio |
| Dual `road_v2.hpp` layer separation | Header conflict risk; must stay in sync |
| LaneMaker spdlog usage | "Leave untouched" per AGENTS.md |
| `ApplicationContext` service ownership | Central service container; changes affect all services |
| `EventBus` pub/sub pattern | Decoupled communication; changes affect all services |
| CMake AUTOMOC + .cpp inclusion pattern | Q_OBJECT headers from .cpp files need CMake entries |

---

## 31. New Developer Guide

### 31.1 Where to Start Reading

1. **`src/app/main.cpp`** — Application entry point, startup flow, workspace setup.
2. **`src/core/ApplicationContext.hpp`** — Service container.
3. **`src/core/workspace/WorkspaceManager.cpp`** — Workspace definitions and switching.
4. **`src/core/project/ProjectManager.cpp`** — Project CRUD and folder structure.
5. **`AGENTS.md`** — Build commands, conventions, architecture overview.
6. **`docs/ROAD_V2_DUAL_LAYERS.md`** — Dual RoadV2 model explanation.
7. **`docs/CROSS_SECTION_STUDIO.md`** — LaneConfigWidget and profile system.

### 31.2 How to Configure/Build

```cmd
:: 1. Configure (once or after CMakeLists.txt changes)
cmake -B D:\git\OpenGeoStudio-Qt\build -S D:\git\OpenGeoStudio-Qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6

:: 2. Build
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target OpenGeoStudio 2>&1"
```

**Critical:** Use VS 2022 **BuildTools** vcvars64.bat, NOT Community (causes `STL1001`).

### 31.3 How to Run Tests

```cmd
:: Build and run a test
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target test_osm_pipeline 2>&1 && cd /d D:\git\OpenGeoStudio-Qt\build && test_osm_pipeline.exe 2>&1"
```

**Test targets:** `geometry_segment_tests`, `test_osm_pipeline`, `test_road_studio`, `test_road_studio_ui`, `test_world_model`, `test_world_workflow`, `test_terrain_pipeline`, `test_houston_roundtrip`, `test_geotiff_writer`, `test_gpxz_download`.

### 31.4 How to Launch the Application

```cmd
D:\git\OpenGeoStudio-Qt\build\deploy\OpenGeoStudio.exe
```

Or with a project file:
```cmd
OpenGeoStudio.exe C:\OpenGeoStudio\Projects\MyProject\MyProject.ogproj
```

### 31.5 How to Add a Workspace

1. Add workspace definition in `WorkspaceManager::registerDefaults()` (`src/core/workspace/WorkspaceManager.cpp`).
2. Create workspace widget in `src/ui/<name>/`.
3. Add widget to `QStackedWidget` in `AppMainWindow::setupCenterWidget()` (`src/app/main.cpp`).
4. Add toolbar tab and `Alt+N` shortcut in `AppMainWindow`.
5. Connect workspace-specific signals.

### 31.6 How to Add a Road Studio Capability

1. Add edit mode to `EditMode` enum (`src/engine/lanemaker/ui/action_defs.h`).
2. Add tool button to `MainWidget` tool palette (`main_widget.cpp` constructor).
3. Implement `goto<Mode>Mode()` method in `MainWidget`.
4. Create drawing session class inheriting `RoadDrawingSession` (`road_drawing.h`).
5. Add mode handling in `SetEditMode()`.
6. Add mouse/keyboard handling in `OnMouseAction()` / `OnKeyPress()`.
7. Add action recording in `ActionManager`.
8. Add object tree entry in `refreshObjectTree()`.
9. Add inspector fields in `onSelectionChanged()`.

**Note:** Current simplified UI only shows Road and View tools. Other modes exist but are hidden.

### 31.7 How to Add an OSM Pipeline Stage

1. Create header in `src/core/osm/` (header-only).
2. Add stage to `OsmImportPipeline::importFromFile()` or `importFromString()`.
3. Add `Result` fields if the stage produces output.
4. Add `Stats` fields if the stage produces statistics.
5. Add tests in `test_osm_pipeline.cpp`.
6. Update `OsmImportDialog` to display stage results.

### 31.8 How to Add a Plugin

1. Create a new CMake project linking `plugin_api` interface library.
2. Inherit from `IPlugin`:
   ```cpp
   class MyPlugin : public QObject, public IPlugin {
       Q_OBJECT
       Q_INTERFACES(IPlugin)
       Q_PLUGIN_METADATA(IID "opengeostudio.plugin/1.0" FILE "myplugin.json")
   public:
       QString id() const override { return "my-plugin"; }
       QString name() const override { return "My Plugin"; }
       QString version() const override { return "1.0.0"; }
       QList<PluginCapability> capabilities() const override { /* ... */ }
   };
   ```
3. Build as a shared library.
4. Place in plugins directory.
5. **Note:** Plugin loading is not wired into `ApplicationContext` yet — you'll need to add `PluginManager` creation and `PluginLoader::discoverPlugins()` call in `ApplicationContext` or `main.cpp`.

### 31.9 How to Add a Resource

1. Add file to appropriate `.qrc` resource file (e.g., `resources/app_icons.qrc`).
2. If new `.qrc` file, add to CMakeLists.txt target sources.
3. Use `:/path/to/resource` prefix in code (e.g., `setIcon(QIcon(":/icons/road.svg"))`).

### 31.10 How to Modify Persistence

1. **Project (.ogproj):** Modify `Project` struct (`src/core/project/Project.hpp`), update `toJson()`/`fromJson()`, update `docs/ogproj-schema.json`.
2. **World (world.json):** Modify `World` struct (`src/core/world/World.hpp`), update `toJson()`/`fromJson()`.
3. **OSM (.ogosm):** Modify `OsmProjectSerializer` (`src/core/osm/OsmProjectSerializer.hpp`).
4. **Road (.xodr):** Modify LaneMaker's libOpenDRIVE or `OsmExporter`.
5. **Backward compatibility:** Ensure `fromJson()` handles missing fields gracefully.

### 31.11 How to Debug OpenGL/Context Issues

1. **LaneMaker OpenGL:** `MapViewGL` is a `QOpenGLWidget` with Core Profile 3.3. Context is created on first show. Check `isGLInitialized()` before GL calls.
2. **Deferred load:** Road files are loaded 2s after workspace switch (main.cpp QTimer). If loading fails, check if GL was initialized.
3. **OGRE-Next:** `OgreWidget` is a `QWindow` with D3D11. Check `plugins.cfg` path and OGRE media paths.
4. **MapLibre:** `StyleHttpServer` serves style JSON on localhost. Check if port is available.
5. **Debug output:** Enable `appLog().debug(...)` or check `log.txt` in deploy directory.

### 31.12 How to Avoid Global-State/Header Conflicts

1. **Dual `road_v2.hpp`:** Only include internal `road_v2.hpp` from `.cpp` files in the main app. Never include it from headers that might also include `road_engine.hpp`.
2. **LaneMaker globals:** Don't assume `g_mainWindow`/`g_laneConfig`/`g_mapViewGL` point to a specific instance — they're rebound on `showEvent()`.
3. **Q_OBJECT from .cpp:** If you include a Q_OBJECT header from a .cpp file, add the .cpp to CMakeLists.txt for AUTOMOC.
4. **Windows min/max:** `NOMINMAX` is defined globally; use `std::min`/`std::max`, not `min`/`max`.

---

## 32. AI Repository Context

### 32.1 Project Purpose

OpenGeoStudio-Qt is a native C++20/Qt 6 desktop application for authoring geo-referenced 3D worlds: terrain, road networks, railways, and procedural scenes. It is the native successor to a removed TypeScript/Electron stack.

### 32.2 Actual Architecture

Single-window, multi-workspace Qt application with 5 workspaces (Home, Terrain, Road, Train, 3D). Core services in `ApplicationContext`. Embedded LaneMaker engine for road/rail editing. OGRE-Next for 3D rendering. Header-only OSM and terrain pipelines.

### 32.3 Canonical Models

- **Road (road engine):** `road_v2::RoadV2` with `LaneSection` from `lane_engine.hpp`
- **Road (LaneMaker):** `LM::Road` wrapping `odr::Road` with `LaneProfile`
- **Road (UI):** `roads::Road` with `ControlPoint[]` (legacy)
- **World:** `world::World` with actors, layers, splines, PCG graphs
- **Project:** `Project` with `moduleState` map
- **Terrain:** `TerrainStore` with bounds, tile grid, export settings

### 32.4 Entry Points

- `main()` in `src/app/main.cpp` (line 858)
- Test `main()` functions in respective test files

### 32.5 Module Boundaries

- `src/app/` — Application shell
- `src/core/` — Domain core (services, OSM, world, terrain, map)
- `src/engine/road/` — Road engine (header-only)
- `src/engine/lanemaker/` — Embedded LaneMaker
- `src/ui/` — UI workspaces
- `src/plugin/` — Plugin ABI (not wired)

### 32.6 Important Symbols

- `ApplicationContext`, `EventBus`, `ProjectManager`, `WorkspaceManager`, `TerrainStore`
- `AppMainWindow`, `MapViewportWidget`
- `LaneMaker::MainWidget`, `MainWindow`, `MapViewGL`, `LaneConfigWidget`
- `LaneMaker::Road`, `AbstractJunction`, `Junction`, `DirectJunction`, `World` (LaneMaker)
- `LaneProfile`, `LanePlan`, `RoadProfile`, `RailProfile`
- `road_v2::RoadV2`, `LaneSection`, `Polynomial3`
- `world::World`, `WorldBuilder`, `SplineEvaluator`, `PCGEngine`
- `OsmImportPipeline`, `RoadNetworkBuilder`, `JunctionDetector`, `OsmExporter`
- `ExportEngine`, `RasterWriter`, `DemDecoder`, `GISProcessor`, `TerrainAnalyzer`
- `OgreWidget`, `Studio3DWidget`
- `IPlugin`, `PluginManager`, `PluginLoader`
- `appLog()`, `Logger`

### 32.7 Data Formats

- `.ogproj` — Project JSON (schema: `docs/ogproj-schema.json`)
- `.xodr` — OpenDRIVE XML (road network)
- `.xodr.json` — Road annotations sidecar JSON
- `.ogosm` — OSM project JSON
- `world.json` — World scene JSON
- `.tif` — GeoTIFF (heightmap, imagery)
- `.png` — PNG (imagery, masks)
- `manifest.json` — Terrain export manifest
- `recent-projects.json` — Recent projects list
- `.dat` — Action replay binary (cereal)

### 32.8 Build Commands

```cmd
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build build --target OpenGeoStudio 2>&1"
```

### 32.9 Test Commands

```cmd
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build build --target <test_target> 2>&1 && cd /d build && <test_target>.exe 2>&1"
```

### 32.10 Known Hazards

1. **LaneMaker global pointers** — rebound on `showEvent()`, shared between Road and Train Studio
2. **Dual `road_v2.hpp`** — header conflict if included incorrectly
3. **`TerrainWorldBridge::sampleHeight()`** — stub returning 0
4. **Hard-coded API key** in `test_gpxz_download.cpp`
5. **Hardcoded OGRE-Next/MapLibre paths** in CMake
6. **CI missing vcpkg toolchain** — may fail to find dependencies
7. **`gtest` in vcpkg.json but unused**
8. **`test_road_studio_ui` CMake condition mismatch** (`BUILD_TESTING` vs `BUILD_TESTS`)

### 32.11 Files to Inspect Before Modifying Major Subsystems

| Subsystem | Files to Read First |
|-----------|-------------------|
| Road engine | `docs/ROAD_V2_DUAL_LAYERS.md`, `src/engine/road/road_v2.hpp`, `geometry_segment.hpp`, `lane_engine.hpp` |
| LaneMaker | `src/engine/lanemaker/ui/main_widget.h`, `main_window.h`, `map_view_gl.h`, `LaneConfigWidget.h`, `road.h`, `junction.h`, `road_profile.h` |
| OSM pipeline | `src/core/osm/OsmImportPipeline.hpp`, `OsmTypes.hpp`, `RoadNetworkBuilder.hpp` |
| Terrain | `src/ui/terrain/ExportEngine.hpp`, `TerrainTypes.hpp`, `RasterWriter.hpp` |
| 3D Studio | `src/ui/studio3d/OgreWidget.hpp`, `src/core/world/World.hpp` |
| Project | `src/core/project/Project.hpp`, `ProjectManager.hpp`, `docs/ogproj-schema.json` |
| Build | `CMakeLists.txt`, `vcpkg.json`, `AGENTS.md` |
| Plugin | `src/plugin/PluginApi.hpp` |

### 32.12 Safe and Unsafe Assumptions

**Safe assumptions:**
- Core pipelines return `Result` with `success` + `errorMessage`
- `appLog()` is available globally for logging
- `ApplicationContext` owns all core services
- Qt 6.8 API conventions apply
- Header-only OSM/terrain pipelines are stateless

**Unsafe assumptions:**
- `g_mainWindow`/`g_laneConfig`/`g_mapViewGL` point to a specific instance (they're rebound)
- `World::Instance()` (LaneMaker) is empty (it's shared between workspaces)
- Plugin system is active (it's not wired)
- `TerrainWorldBridge::sampleHeight()` returns real elevation (it returns 0)
- All test targets are registered with CTest (only `geometry_segment_tests` is)
- CI will find vcpkg dependencies (it doesn't pass the toolchain file)

### 32.13 Explicit Unknowns

- `.osm.pbf` binary format support
- Mapzen DEM availability
- libtiff version and security
- libOpenDRIVE parser security
- HTTPS enforcement for all providers
- Action replay autosave cleanup on crash
- LaneMaker temp file cleanup on crash
- OpenGL context loss handling
- Exact retry policies for all DEM/imagery providers

---

## 33. Master System Map

### 33.1 Road Drawing Master Map

```
User action: Click Road tool (R)
  ↓ Qt signal: toolButton->clicked()
  ↓ MainWidget::gotoCreateRoadMode()
  ↓ Domain: SetEditMode(Mode_Create) → RoadCreationSession
  ↓ Geometry: User clicks points → staged geometry (Line/Arc/Spiral/Bezier)
  ↓ User presses Space
  ↓ confirmEdit() → RoadCreationSession::Complete()
  ↓ Domain: Road(LaneProfile, RefLine) → World::allRoads
  ↓ Geometry: Road::Generate() → LaneProfile::Apply() → lane sections
  ↓ Mesh: SectionGraphics → MapViewGL::AddQuads() → OpenGL vertex buffers
  ↓ Rendering: MapViewGL::paintGL() → screen
  ↓ Persistence: (on save) MainWindow::saveToPath() → .xodr
  ↓ Validation: (on verify) MainWindow::verifyMap()
  ↓ Logging: appLog() / spdlog
```

### 33.2 OSM Import Master Map

```
User action: Click OSM Import button
  ↓ Qt signal: importButton->clicked()
  ↓ OsmImportDialog::exec()
  ↓ Domain: User selects .osm file + settings
  ↓ Command: OsmImportPipeline::importFromFile(path, settings)
  ↓ Parse: OsmXmlParser::parseFile() → OsmData
  ↓ Geometry: CoordinateConverter::project() → local meters
  ↓ Topology: RoadNetworkBuilder::build() → RoadV2[] + NetworkNode[]
  ↓ Junction: JunctionDetector::detect() → Junction[]
  ↓ Validation: RoadValidator::validateAndRepair() → Issue[]
  ↓ Lanes: LaneGenerator::generate() → LaneSection[]
  ↓ Roundabouts: RoundaboutGenerator::generate() → RoadV2[]
  ↓ Markings: RoadMarkingGenerator::generateForNetwork() → Marking[]
  ↓ Signs: TrafficSignGenerator::generateForNetwork() → Sign[]
  ↓ Export: OsmExporter::exportToOpenDrive() → .xodr
  ↓ Persistence: OsmProjectSerializer::saveToFile() → .ogosm
  ↓ Rendering: (loaded into LaneMaker) MapViewGL::paintGL()
  ↓ Validation: RoadValidator issues displayed in dialog
  ↓ Logging: appLog()
```

### 33.3 Terrain Export Master Map

```
User action: Shift+drag on map to select area
  ↓ Qt signal: TerrainOverlayWidget mouse events
  ↓ Domain: TerrainStore::setBounds() → computeTileGrid()
  ↓ User configures export settings in ExportPanel
  ↓ User clicks Export
  ↓ Command: ExportEngine::exportToDirectory(dir)
  ↓ Download: QNetworkAccessManager → DEM tiles + imagery tiles
  ↓ Process: DemDecoder::decodeAuto() → RasterGrid
  ↓ Geometry: GISProcessor::resample() → target resolution
  ↓ Export: RasterWriter::writeFloat32GeoTiff() → .tif
  ↓ Export: RasterWriter::writeRgbGeoTiff() / writePngWithWorldFile() → .tif/.png
  ↓ Persistence: manifest.json
  ↓ Rendering: (loaded in 3D Studio) OgreWidget::loadTerrain()
  ↓ Validation: ValidationManager (optional)
  ↓ Logging: appLog() + progress signals
```

### 33.4 World Authoring Master Map

```
User action: Place actor / edit spline / generate PCG
  ↓ Qt signal: OgreWidget mouse/keyboard events
  ↓ Command: QUndoStack commands (AddActorCommand, etc.)
  ↓ Domain: World::addActor() / addSpline() / addPCGGraph()
  ↓ Geometry: SplineEvaluator::sample() / generateRoadMesh()
  ↓ PCG: PCGEngine::evaluate() → points
  ↓ Mesh: OgreWidget::rebuildActor() → OGRE SceneNode + Item
  ↓ Rendering: OGRE render → screen
  ↓ Persistence: World::saveToFile() → world.json
  ↓ Validation: World::validate() → ValidationError[]
  ↓ Logging: appLog()
```

### 33.5 Project Persistence Master Map

```
User action: File → Save
  ↓ Qt signal: saveAction->triggered()
  ↓ AppMainWindow::saveProjectState()
  ↓ Domain: ProjectManager::save()
  ↓ Persistence: .ogproj JSON (terrain state in moduleState)
  ↓ Command: MainWindow::saveToPath() → temp file → .xodr
  ↓ Persistence: .xodr + .xodr.json sidecar
  ↓ Logging: appLog()
  ↓
User action: File → Open
  ↓ ProjectManager::open() → Project::fromJson()
  ↓ AppMainWindow::loadProjectState() → terrain state from moduleState
  ↓ Workspace: activate("terrain")
  ↓ (If road file) Deferred: QTimer 2s → MainWindow::loadFromPath()
  ↓ Rendering: refreshAllCustomGraphics() + refreshObjectTree()
```

### 33.6 Workspace Switching Master Map

```
User action: Click workspace tab / Alt+1..4
  ↓ Qt signal: tabButton->clicked() / QShortcut::activated
  ↓ WorkspaceManager::activate(id)
  ↓ Signal: workspaceActivated(Workspace)
  ↓ AppMainWindow::onWorkspaceActivated()
  ↓ Domain: m_centerStack->setCurrentIndex(index)
  ↓ UI: Show/hide docks
  ↓ UI: (Road Studio) setupRoadStudioMenus() + showRoadStudioMenus(true)
  ↓ (LaneMaker workspace) showEvent() → rebind g_mainWindow, g_mapViewGL
  ↓ (If pending road file) QTimer 2s → loadFromPath()
  ↓ Rendering: workspace widget shown
  ↓ Logging: appLog()
```

---

## 34. Improvement Roadmap

### 34.1 P0 — Immediate Action Required

| # | Item | Rationale | Action |
|---|------|-----------|--------|
| 1 | **Rotate/remove committed GPXZ API key** | `test_gpxz_download.cpp` contains `ak_NgEXLGho_z5TBKb44GCFKIirC` in git history | Rotate the key at GPXZ, replace with env var `GPXZ_API_KEY` or CI secret, scrub from git history if possible |
| 2 | **Stop persisting provider API keys in plaintext `.ogproj`** | `[CONFIRMED]` API keys (OpenTopo, Mapbox, MapTiler, GPXZ, Stadia) are written to `moduleState` in project JSON | Move to Windows Credential Manager / OS secure storage; reference by provider name in `.ogproj` |
| 3 | **Promote LaneMaker global state to documented critical constraint** | `[RISK]` #1 — globals rebound on `showEvent()`, shared between Road and Train Studio | Done in this revision (Part 1, Section 0). Next: refactor to per-instance context or enforce single-instance lifecycle |
| 4 | **Add Road model relationship diagram** | Three road representations + dual RoadV2 cause confusion | Done in this revision (Part 1, Section 0) |

### 34.2 P1 — High Priority

| # | Item | Rationale |
|---|------|-----------|
| 5 | **Wire plugin system** into `ApplicationContext` | Defined but not active; create `PluginManager`, call `PluginLoader::discoverPlugins()` at startup |
| 6 | **Fix CI vcpkg integration** | `ci.yml` does not pass `CMAKE_TOOLCHAIN_FILE`; CI may fail to find vcpkg dependencies |
| 7 | **Fix `test_road_studio_ui` CMake condition** | Uses `BUILD_TESTING` while other tests use `BUILD_TESTS` — potential mismatch |
| 8 | **Remove `gtest` from `vcpkg.json`** | Listed but unused; all tests use doctest or custom runners |
| 9 | **Make OGRE-Next/MapLibre paths configurable** | Hardcoded to `D:/git/ogre-next` and `D:/git/maplibre-native-qt/install` |
| 10 | **Implement `TerrainWorldBridge::sampleHeight()`** | Currently returns 0 (stub); PCG surface alignment doesn't work from world model |
| 11 | **Register all test targets with CTest** | Only `geometry_segment_tests` is registered |
| 12 | **Add XML entity expansion limits** to `OsmXmlParser` | Security — prevent XML bomb attacks on untrusted OSM input |
| 13 | **Add log rotation** to `Logger` file transport | `log.txt` grows unbounded |
| 14 | **Add `schemaVersion` to `.ogproj`** | No versioning/migration strategy; schema changes will break existing projects |
| 15 | **Add transactional project save** | Multi-file non-transactional save can leave inconsistent state (temp → validate → atomic rename) |

### 34.3 P2 — Medium Priority

| # | Item | Rationale |
|---|------|-----------|
| 16 | **Refactor LaneMaker global state** | Replace global pointers with instance-based state |
| 17 | **Unify LaneMaker spdlog and application Logger** | Two separate log streams; route to same file at minimum |
| 18 | **Clean up hidden Road Studio modes** | Mode implementation compiled but not accessible from simplified UI |
| 19 | **Add backward-compatibility tests** for `.ogproj` schema | No migration tests |
| 20 | **Add formal benchmarks** | No performance regression tracking |
| 21 | **Add concurrency** for OSM parsing and terrain processing | Blocks UI thread on large files |
| 22 | **Add frustum culling** to `MapViewGL` | Large road networks render all geometry every frame |
| 23 | **Cache `RoadProfileCatalog::all()` / `RailProfileCatalog::all()`** | Repeated calls may reconstruct catalogs |
| 24 | **Add `.osm.pbf` support** | If needed for large imports |
| 25 | **Add OpenGL context loss handling** | `[UNKNOWN]` — no handling found |
| 26 | **Document LaneMaker constants** | `GraphicsDivision`, `MaxRoadVertices`, etc. undocumented |
| 27 | **Add plugin examples and tests** | No plugins shipped, no tests |
| 28 | **Port hardcoded paths to CMake finders** | OGRE-Next, MapLibre |

### 34.4 Technical Debt Reduction Order

```
P0 Security (API key, plaintext keys)
    ↓
P0 Architecture (LaneMaker globals, road model diagram)
    ↓
P1 Build/CI (vcpkg, paths, CMake conditions)
    ↓
P1 Persistence (versioning, transactional save)
    ↓
P1 Architecture (plugin wiring, sampleHeight)
    ↓
P2 Testing (CTest registration, coverage gaps, benchmarks)
    ↓
P2 Performance (caching, concurrency, culling)
    ↓
P2 Documentation (constants, contracts, cleanup)
```

---

## 35. Verification Backlog

> These `[UNKNOWN]` items from the analysis need runtime verification before being treated as authoritative.

| Priority | Question | How to Verify | Status |
|----------|----------|---------------|--------|
| P0 | Are API keys stored plaintext in `.ogproj`? | Inspect `Project::toJson()` serialization of `moduleState` | **Confirmed** — plaintext |
| P0 | Is LaneMaker instance-safe? | Runtime test: construct two `MainWindow` instances, verify `World::Instance()` isolation | Open |
| P0 | Does committed GPXZ key still work? | Attempt API call with key | Open (must rotate regardless) |
| P1 | Is `.osm.pbf` supported? | Test `OsmXmlParser` with binary PBF file | Open — likely no (parser uses QXmlStreamReader) |
| P1 | Is HTTPS enforced for all providers? | Trace URLs in `ExportEngine`, `MapViewGL::requestTile`, `SearchBar` | Open |
| P1 | Does `TerrainWorldBridge::sampleHeight()` return real data? | Integration test | **Confirmed** — returns 0 (stub) |
| P1 | Does `test_road_studio_ui` build with `BUILD_TESTS=ON`? | Build with `BUILD_TESTS=ON`, check if target exists | Open — CMake condition mismatch |
| P1 | Do historical test counts (34, 22, 155, 290+, 261) match current? | Run all test executables | Open — source analysis shows discrepancies |
| P1 | Is libtiff version current / secure? | Check vcpkg-installed version | Open |
| P1 | Is libOpenDRIVE parser safe against malformed XML? | Fuzz test with malformed .xodr | Open |
| P2 | Are performance regressions tracked? | Add benchmark suite | **Missing** |
| P2 | Does OpenGL context loss handling exist? | Trigger context loss, observe behavior | Open |
| P2 | Does action replay autosave clean up on crash? | Crash during replay, check for temp files | Open |
| P2 | Does LaneMaker temp file clean up on crash? | Crash during save, check temp dir | Open |
| P2 | Are all DEM provider retry policies correct? | Test each provider with network failures | Open |
| P2 | Is Mapzen DEM still available? | Attempt download | Open |

---

## 36. Change/Test Workflows

> Turn this documentation from a reverse-engineering report into a development operating manual.
> After changing a subsystem, run the corresponding test chain.

### 36.1 Changing RoadV2 (internal)

```
Modify src/engine/road/road_v2.hpp or lane_*.hpp
    ↓
Run: geometry_segment_tests
    ↓
Run: test_osm_pipeline
    ↓
Run: test_road_studio
    ↓
Check: OpenDRIVE export (test_houston_roundtrip)
    ↓
Check: LaneMaker integration (manual: draw road, save, reload)
    ↓
Check: public road_v2.hpp facade is still in sync
```

### 36.2 Changing LaneMaker (MainWidget / MainWindow / MapViewGL)

```
Modify src/engine/lanemaker/ui/ or engine/
    ↓
Verify: global pointer rebinding still correct (showEvent)
    ↓
Run: test_road_studio
    ↓
Run: test_road_studio_ui (offscreen)
    ↓
Manual: switch Road Studio → Train Studio → Road Studio
    ↓
Manual: draw road, save .xodr, reload .xodr
    ↓
Check: ActionManager replay still deterministic
```

### 36.3 Changing LaneConfigWidget

```
Modify src/engine/lanemaker/widgets/LaneConfigWidget.cpp/.h
    ↓
Verify: applyingProfile flag is correct during LoadProfile
    ↓
Verify: signal flow SetOption → OnOptionChange → CheckModified
    ↓
Run: test_road_studio
    ↓
Manual: select profile, modify lane, reset, save preset
    ↓
Manual: open DrawOptionDialog (popup instance), change lanes
    ↓
Check: ActionManager records profile changes correctly
```

### 36.4 Changing OSM Pipeline

```
Modify src/core/osm/*.hpp
    ↓
Run: test_osm_pipeline
    ↓
Run: test_houston_roundtrip (if data available)
    ↓
Manual: import .osm file, check results tabs
    ↓
Check: .xodr export loads in LaneMaker
    ↓
Check: .ogosm save/reload round-trip
```

### 36.5 Changing Terrain Pipeline

```
Modify src/ui/terrain/ or src/core/terrain/
    ↓
Run: test_terrain_pipeline
    ↓
Run: test_geotiff_writer
    ↓
Run: test_gpxz_download (if network + API key available)
    ↓
Manual: select area, export DEM + imagery
    ↓
Check: GeoTIFF opens in QGIS/GDAL
    ↓
Check: 3D Studio loads terrain (OgreWidget::loadTerrain)
    ↓
Check: DemElevationSampler samples correctly
```

### 36.6 Changing World Model

```
Modify src/core/world/*.hpp
    ↓
Run: test_world_model
    ↓
Run: test_world_workflow
    ↓
Run: test_houston_roundtrip (exercises WorldBuilder)
    ↓
Manual: 3D Studio — place actors, save world, reload
    ↓
Check: World::validate() returns no errors
    ↓
Check: undo/redo works (QUndoStack)
```

### 36.7 Changing Project Persistence

```
Modify src/core/project/Project.hpp or ProjectManager.cpp
    ↓
Update: docs/ogproj-schema.json
    ↓
Add: schemaVersion field if not present
    ↓
Add: migration function for old schemas
    ↓
Run: test_world_workflow (exercises project save/load)
    ↓
Manual: create project, save, close, reopen
    ↓
Check: backward compatibility with existing .ogproj files
```

### 36.8 Changing Build System

```
Modify CMakeLists.txt or vcpkg.json
    ↓
Clean build: rm -rf build && cmake -B build ...
    ↓
Build: cmake --build build --target OpenGeoStudio
    ↓
Build all tests: cmake --build build --target test_osm_pipeline test_road_studio ...
    ↓
Run: ctest --output-on-failure
    ↓
Check: CI passes (ci.yml)
    ↓
Check: windeployqt produces runnable deploy/
```

---

## 37. Do Not Touch Rules

> Explicit rules for AI coding agents and developers. Violating these can break the build or cause runtime crashes.

### 37.1 Absolute Prohibitions

```
DO NOT:
  ✗ include the wrong road_v2.hpp
    - Internal: src/engine/road/road_v2.hpp (full LaneSection)
    - Public:  src/engine/road/road_engine/public/road_v2.hpp (placeholder)
    - Rule: OSM headers that use RoadV2 must only be included from .cpp files
    - Risk: LaneSection type mismatch, compile errors

  ✗ instantiate another LaneMaker MainWindow assuming isolated state
    - Globals (g_mainWindow, g_laneConfig, g_mapViewGL) are process-wide
    - World::Instance() is a shared singleton
    - Risk: state corruption between Road Studio and Train Studio

  ✗ directly manipulate LaneMaker globals without checking workspace ownership
    - Globals are rebound on showEvent()
    - Risk: modifying wrong workspace's state

  ✗ modify profile application without checking applyingProfile flag
    - LaneConfigWidget::LoadProfile sets applyingProfile=true
    - Signal blocking must be scoped correctly
    - Risk: unintended action recording, signal loops

  ✗ modify OpenGL lifecycle without checking MapViewGL ownership
    - GL context created on first show, not construction
    - Road loading deferred 2s after workspace switch
    - Risk: GL calls before initialization, crash

  ✗ change RoadV2 without updating public facade
    - Both must stay in sync for class surface
    - Risk: API consumers break

  ✗ use VS 2022 Community vcvars64.bat for builds
    - Causes STL1001: Unexpected compiler version
    - Use VS 2022 BuildTools vcvars64.bat instead

  ✗ add new qDebug()/qWarning() calls in application code
    - Use appLog() from src/core/logger/Logger.hpp
    - LaneMaker spdlog is separate (leave untouched)

  ✗ throw exceptions across core pipeline module boundaries
    - Return Result{success=false, errorMessage} instead
    - Risk: SEH/exception interop issues on MSVC

  ✗ remove Q_OBJECT .cpp entries from CMakeLists.txt
    - AUTOMOC requires .cpp entries for Q_OBJECT headers included from .cpp
    - Risk: MOC generation fails, link errors
```

### 37.2 Require Architectural Review

```
REQUIRES REVIEW BEFORE CHANGING:
  ⚠ LaneMaker global pointer system (g_mainWindow, g_laneConfig, g_mapViewGL)
  ⚠ World::Instance() singleton in LaneMaker
  ⚠ Dual road_v2.hpp layer separation
  ⚠ LaneMaker spdlog usage (leave untouched per AGENTS.md)
  ⚠ ApplicationContext service ownership
  ⚠ EventBus pub/sub pattern
  ⚠ CMake AUTOMOC + .cpp inclusion pattern
  ⚠ MainWindow::showEvent() global rebinding logic
  ⚠ Road::Generate() lane section + junction notification flow
  ⚠ AbstractJunction degeneration logic
```

---

## 38. Developer Recipes

> Step-by-step recipes for common development tasks. Each is 10-20 lines.

### 38.1 How to Add a New Road Profile

```
1. Open src/ui/roadstudio/RoadTypes.hpp
2. Add entry to RoadProfileCatalog::all() (returns QMap<QString, RoadProfile>)
3. Set fields: type, laneWidth, leftLanes, rightLanes, offsets, speedLimit,
   hasSidewalk, hasCurb, surfaceTexture, markingTexture, description
4. LaneConfigWidget::PopulateRoadProfiles() will auto-discover it
5. No test changes needed (catalog is data-driven)
6. Manual test: select new profile in Cross-Section Studio
```

### 38.2 How to Add a New Rail Profile

```
1. Open src/ui/roadstudio/RoadTypes.hpp
2. Add entry to RailProfileCatalog::all() (returns QMap<QString, RailProfile>)
3. Set fields: gauge, trackCount, trackSpacing, ballastWidth, railHeight,
   sleeperLength, maxSpeed, railType, sleeperType
4. LaneConfigWidget::PopulateRailProfiles() will auto-discover it
5. Manual test: switch to Train Studio, select new rail profile
```

### 38.3 How to Add a New Terrain Provider

```
1. Add enum value to DemSource or ImagerySource in src/ui/terrain/TerrainTypes.hpp
2. Add UI entry in ExportPanel (src/ui/terrain/ExportPanel.cpp)
3. Implement download in ExportEngine (src/ui/terrain/ExportEngine.cpp):
   - If tiled: implement mosaic download (startDemMosaic pattern)
   - If area: implement single-request download
   - If local: implement file loading
4. Add API key input if needed (ExportPanel)
5. Add test case to test_terrain_pipeline.cpp
6. Manual test: select area, export with new provider
```

### 38.4 How to Add a New Workspace

```
1. Add workspace definition in WorkspaceManager::registerDefaults()
   (src/core/workspace/WorkspaceManager.cpp)
2. Create workspace widget in src/ui/<name>/<Name>Widget.cpp/.hpp
3. Add widget to QStackedWidget in AppMainWindow::setupCenterWidget()
   (src/app/main.cpp)
4. Add toolbar tab and Alt+N shortcut in AppMainWindow
5. Connect workspace-specific signals
6. Add to CMakeLists.txt OpenGeoStudio sources
7. Manual test: switch to new workspace
```

### 38.5 How to Add a New Export Format

```
1. Add format enum to appropriate types header
2. Implement exporter class (static methods, like OsmExporter)
3. Add UI entry in export dialog/panel
4. Wire export button to exporter
5. Add test case
6. Manual test: export to new format, verify file
```

### 38.6 How to Modify Lane Configuration

```
1. Read LaneConfigWidget.h to understand signal flow
2. Read CrossSectionVisual to understand rendering
3. Modify LaneConfigWidget.cpp:
   - Be careful with applyingProfile flag
   - CheckModified() compares current vs loadedProfile
   - ProfileChanged signal notifies MainWidget
4. Run test_road_studio
5. Manual test: select profile, modify, reset, verify rendering
```

### 38.7 How to Add a PCG Node

```
1. Add node type to PCGNodeType enum (src/core/world/WorldTypes.hpp)
2. Implement evaluation in PCGEngine::evaluateNode() (src/core/world/PCGEngine.hpp)
3. Add filter/transform if needed (passesSlopeFilter, applyRandomTransform patterns)
4. Add test case to test_world_model.cpp
5. Manual test: create PCG graph with new node in 3D Studio
```

### 38.8 How to Add a Plugin

```
1. Create a new CMake project linking plugin_api interface library
2. Inherit from IPlugin:
   class MyPlugin : public QObject, public IPlugin {
       Q_OBJECT
       Q_INTERFACES(IPlugin)
       Q_PLUGIN_METADATA(IID "opengeostudio.plugin/1.0" FILE "myplugin.json")
   public:
       QString id() const override { return "my-plugin"; }
       QString name() const override { return "My Plugin"; }
       QString version() const override { return "1.0.0"; }
       QList<PluginCapability> capabilities() const override { /* ... */ }
   };
3. Build as shared library
4. Place in plugins directory
5. NOTE: Plugin loading is NOT wired into ApplicationContext yet
   - Must add PluginManager creation in ApplicationContext
   - Must call PluginLoader::discoverPlugins() at startup
```

### 38.9 How to Modify RoadV2

```
1. Read docs/ROAD_V2_DUAL_LAYERS.md first
2. Determine which layer to modify:
   - Internal (src/engine/road/road_v2.hpp) — for OSM pipeline, road engine
   - Public (src/engine/road/road_engine/public/road_v2.hpp) — for external API
3. Update BOTH layers to keep class surface in sync
4. Run geometry_segment_tests
5. Run test_osm_pipeline
6. Run test_road_studio
7. Check OpenDRIVE export (test_houston_roundtrip)
8. Check LaneMaker integration (manual: draw, save, reload)
```

### 38.10 How to Add a Resource

```
1. Add file to appropriate .qrc resource file
   (e.g., resources/app_icons.qrc, resources/road_studio/road_studio.qrc)
2. If new .qrc file: add to CMakeLists.txt target sources
3. Use ":/path/to/resource" prefix in code
   e.g., setIcon(QIcon(":/icons/road.svg"))
4. If LaneMaker resource: call Q_INIT_RESOURCE in main.cpp
5. Manual test: verify resource loads in UI
```

---

## 39. Persistence & Versioning Strategy

### 39.1 Current State `[CONFIRMED]` `[RISK]`

Project persistence is **multi-file and non-transactional**:

```
Project/
  ├── Project.ogproj        — JSON (project metadata + terrain state in moduleState)
  ├── Roads/
  │     ├── road.xodr       — OpenDRIVE XML (road network)
  │     └── road.xodr.json  — JSON sidecar (signs, markings, furniture)
  ├── GIS/
  │     └── import.ogosm    — JSON (OSM project)
  ├── Scene/
  │     └── world.json      — JSON (3D world scene)
  └── Terrain/
        ├── *.tif           — GeoTIFF (heightmap, imagery)
        └── manifest.json   — JSON (terrain export manifest)
```

**Risk:** If any file write fails mid-save, the project is left in an inconsistent state. There is no rollback mechanism.

### 39.2 No Schema Versioning `[CONFIRMED]` `[RISK]`

`.ogproj` files have no `schemaVersion` field. Once users have real projects, schema changes will break backward compatibility with no migration path.

### 39.3 Recommended Strategy `[RECOMMENDATION]`

**1. Add schema versioning:**
```json
{
  "schemaVersion": 1,
  "id": "...",
  "name": "...",
  ...
}
```

**2. Implement migration functions:**
```cpp
namespace ProjectMigrations {
    Project migrateV1ToV2(const Project& v1);
    Project migrateV2ToV3(const Project& v2);
    // ...
}
```

**3. Implement transactional save:**
```
Save Transaction
    ↓
Write all files to .tmp extensions
    ↓
Validate all .tmp files
    ↓
Atomic rename: .tmp → final
    ↓
If any step fails: delete .tmp files, original state preserved
```

**4. Add backward-compatibility tests:**
- Save project with schema v1
- Load with current code (should auto-migrate)
- Verify all fields preserved

---

*End of Part 3. This concludes the 3-part reverse-engineering document for OpenGeoStudio-Qt.*

**Document index:**
- Part 1: `docs/REVERSE_ENGINEERING_PART1_ARCHITECTURE.md` (Sections 0–12)
- Part 2: `docs/REVERSE_ENGINEERING_PART2_SYSTEMS.md` (Sections 13–25)
- Part 3: `docs/REVERSE_ENGINEERING_PART3_GUIDE.md` (Sections 26–39)

---

## Appendix A — Verified Findings and Applied Fixes (2026-08-20)

> This appendix records items from the static-analysis baseline that were
> verified against source and subsequently fixed. Items marked **[FIXED]**
> have been implemented and build-verified. Items marked **[VERIFIED]**
> were confirmed in source but not yet fixed.

### A.1 Security — Credentials [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| Hardcoded GPXZ API key in `test_gpxz_download.cpp` | [FIXED] | Replaced with `GPXZ_API_KEY` env var; tests skip if absent |
| Hardcoded GPXZ default in `TerrainTypes.hpp` | [FIXED] | Default changed to empty `QString` |
| Hardcoded GLAD Basic Auth in `ExportEngine.cpp` (2 locations) | [FIXED] | Replaced with `GLAD_USER`/`GLAD_PASSWORD` env vars |
| GPXZ key now has env var fallback in `ExportEngine.cpp` | [FIXED] | `resolveGpxzApiKey()` helper added |

**Action required externally:** The previously committed GPXZ key
(`ak_NgEXLGho_z5TBKb44GCFKIirC`) and GLAD credentials (`glad:ardpas`)
must be revoked/rotated by the project owner. Code remediation is complete.

### A.2 LaneMaker Global State [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| `g_laneConfig` not rebound on `showEvent()` | [FIXED] | `MainWidget::showEvent()` now calls `rebindGlobals()` |
| `MainWidget::instance` not rebound on `showEvent()` | [FIXED] | `MainWindow::showEvent()` now calls `mainWidget->rebindGlobals()` |
| No structured access to LaneMaker global state | [FIXED] | `LaneMakerContext` abstraction added (`lanemaker_context.h/.cpp`) |

**Note:** LaneMaker singletons (`World::Instance()`, `ChangeTracker::Instance()`,
etc.) remain process-wide. The `LaneMakerContext` provides structured access to
the rebound pointer-based globals only. Full singleton isolation is deferred to
Phase B.

### A.3 Build System [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| `BUILD_TESTS` vs `BUILD_TESTING` inconsistency | [FIXED] | `test_road_studio_ui` now uses `BUILD_TESTS` |
| Only 1 test registered with CTest | [FIXED] | All 9 test targets now registered with `add_test()` |
| Hardcoded `D:/git/maplibre-native-qt/install` path | [FIXED] | Replaced with `${QMAPLIBRE_DIR}` CMake variable |
| Hardcoded `D:/git/ogre-next` path | [FIXED] | Changed to `$ENV{OGRE_NEXT_DIR}` CACHE PATH |
| Hardcoded Qt paths (`C:/Qt/6.x.x/...`) | [FIXED] | Removed; uses `${_qt6Core_install_prefix}/bin` |
| CI missing vcpkg toolchain | [FIXED] | `lukka/run-vcpkg@v11` added to `ci.yml` |

### A.4 Project Persistence [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| No `schemaVersion` in `Project` | [FIXED] | `Project::SCHEMA_VERSION = 1` added; `toJson`/`fromJson` updated |
| Non-transactional `ProjectManager::save()` | [FIXED] | Now writes to `.tmp` file, then atomically renames |
| Non-transactional `World::saveToFile()` | [FIXED] | Same temp-file + rename pattern |

### A.5 UI Thread Blocking [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| OSM import blocks UI thread | [FIXED] | `OsmImportDialog::onImport()` now uses `QtConcurrent::run` + `QFutureWatcher` |
| Progress callback used `QApplication::processEvents()` | [FIXED] | Replaced with atomic progress + `QTimer` polling |

**Remaining blocking operations (not yet fixed):**
- `DownloadManager::downloadSync()` uses nested `QEventLoop`
- `DownloadManager::downloadAll()` performs sequential synchronous requests
- `TerrainManager::runPipeline()` runs stages synchronously

### A.6 Terrain World Bridge [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| `TerrainWorldBridge::sampleHeight()` was a stub returning 0 | [FIXED] | Now loads heightmap from `world.settings.heightmapPath`, caches it, and samples bilinearly |

### A.7 Documentation [FIXED]

| Item | Status | Files Changed |
|------|--------|---------------|
| No architecture rules document | [FIXED] | `ARCHITECTURE_RULES.md` created with 15 mandatory rules |

### A.8 Verified But Not Yet Fixed

| Item | Status | Notes |
|------|--------|-------|
| LaneMaker singletons remain process-wide | [VERIFIED] | Phase B: migrate to instance-based ownership |
| `DownloadManager::downloadSync()` blocks UI | [VERIFIED] | Needs async refactor |
| `TerrainManager::runPipeline()` blocks UI | [VERIFIED] | Needs worker thread |
| `test_road_studio` crashes at `Road::SplitRoad` | [VERIFIED] | Pre-existing LaneMaker engine bug; not caused by these changes |
| Credentials may be persisted in `.ogproj` via module state | [VERIFIED] | `TerrainStore::toJson()` persists some API keys; needs sanitization |
| CTest hangs when run from build dir (needs Qt plugins) | [VERIFIED] | Tests pass when run from `deploy/` dir; CI needs `QT_PLUGIN_PATH` |

### A.9 Test Results After All Changes

| Test Target | Result |
|-------------|--------|
| `geometry_segment_tests` | 504/504 pass (5388 assertions) |
| `test_osm_pipeline` | 155/155 pass |
| `test_world_model` | 34/34 pass |
| `test_world_workflow` | 22/22 pass |
| `test_terrain_pipeline` | 28/28 pass |
| `test_road_studio` | Passes up to "Road Split" (pre-existing crash in `LM::Road::SplitRoad`) |
| `test_gpxz_download` | Not run (requires `GPXZ_API_KEY` env var for network tests) |
| `test_geotiff_writer` | Not run (no CMake changes; should pass) |
| `test_houston_roundtrip` | Not run (no changes to OSM export) |
| `test_road_studio_ui` | Not run (headless UI test; needs Qt plugins) |
| `OpenGeoStudio.exe` | Builds and links successfully |
