# OpenGeoStudio-Qt — Agent Guide

## Build Environment

**CRITICAL:** The MSVC environment variables (`INCLUDE`, `LIB`) are NOT set in
the default PowerShell session. All builds must be run with the Build Tools
vcvars64.bat first:

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target <target> 2>&1"
```

Using the VS 2022 Community vcvars64.bat will cause a compiler/STL version
mismatch error (`STL1001: Unexpected compiler version`).

## Build Commands

```bash
# Configure (only needed once or after CMakeLists.txt changes)
cmake -B D:\git\OpenGeoStudio-Qt\build -S D:\git\OpenGeoStudio-Qt

# Build a target
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target OpenGeoStudio 2>&1"

# Build and run a test
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target test_osm_pipeline 2>&1 && cd /d D:\git\OpenGeoStudio-Qt\build && test_osm_pipeline.exe 2>&1"
```

## Test Targets

| Target | Description | Expected |
|--------|-------------|----------|
| `test_world_model` | World Authoring model tests | 34/34 pass |
| `test_world_workflow` | World Authoring workflow tests | 22/22 pass |
| `test_osm_pipeline` | OSM import pipeline tests | 155/155 pass |
| `test_road_studio` | Road Studio feature tests | 325 pass (signs, markings, furniture, snapping, measurement, persistence, templates). Road model tests (creation, split, merge, junctions) are skipped by default via `OGS_SKIP_ROAD_MODEL_TESTS=1` due to non-deterministic SEH crashes in LaneMaker engine test mode. |
| `test_road_studio_ui` | Road Studio UI smoke test (headless) | DISABLED in CTest — hangs in `QApplication` construction due to OpenGL/CGAL static initialization conflict when linked against `lanemaker::core`. Builds correctly but cannot run headlessly. |
| `geometry_segment_tests` | Road engine geometry tests | 504 tests, 5388 assertions |
| `test_terrain_pipeline` | Terrain pipeline tests | 28/28 pass |
| `test_houston_roundtrip` | Houston OSM round-trip tests | 56/56 pass |
| `test_geotiff_writer` | GeoTIFF writer tests | WGS84 + UTM metadata checks |
| `test_gpxz_download` | GPXZ elevation API tests | Requires `GPXZ_API_KEY` env var; skips cleanly without it |

## Architecture

- **C++20 / Qt 6 / OGRE-Next 4.x** native desktop application
- **Build system:** CMake + Ninja (Release config)
- **Road engine:** `src/engine/road/` (header-only, RoadV2/LaneSection model)
- **OSM pipeline:** `src/core/osm/` (header-only, OSM → RoadV2 conversion)
- **World model:** `src/core/world/` (World Authoring system)
- **3D Studio:** `src/ui/studio3d/` (OGRE-Next embedded editor)
- **Road Studio:** `src/ui/roadstudio/` (LaneMaker-based road editor)
- **Train Studio:** `src/ui/trainstudio/` (LaneMaker-based rail editor)

## Workspaces (`src/app/main.cpp`)

The main window uses a `QStackedWidget` with 5 pages:

| Index | Workspace | Widget |
|-------|-----------|--------|
| 0 | Home | `HomeWidget` |
| 1 | Terrain Studio | `TerrainStudioWidget` |
| 2 | Road Studio | `RoadStudioWidget` (embeds LaneMaker `MainWindow`) |
| 3 | Train Studio | `TrainStudioWidget` (embeds LaneMaker `MainWindow`) |
| 4 | 3D Studio | `Studio3DWidget` (OGRE-Next) |

**Important:** Both Road Studio and Train Studio embed a LaneMaker `MainWindow`
at construction time. This means two `MainWidget` instances exist simultaneously.
LaneMaker uses global pointers (`g_mainWindow`, `g_laneConfig`, `LM::g_mapViewGL`)
that are rebound on `showEvent()` — only the currently-visible workspace's
instance should be considered authoritative.

## OSM Pipeline (`src/core/osm/`)

| File | Purpose |
|------|---------|
| `OsmTypes.hpp` | OSM data model (Node, Way, Relation, OsmData) |
| `OsmXmlParser.hpp` | XML parser (QXmlStreamReader, no external deps) |
| `CoordinateConverter.hpp` | WGS84 → local meters (equirectangular / UTM) |
| `RoadClassifier.hpp` | OSM highway tags → road classes with defaults |
| `RoadNetworkBuilder.hpp` | OSM ways → RoadV2 roads with topology |
| `JunctionDetector.hpp` | Detect T/X/Y junctions, roundabouts, overpasses |
| `RoadValidator.hpp` | Validate geometry, topology, lanes, junctions |
| `OsmImportPipeline.hpp` | Complete pipeline: parse → project → build → validate |
| `LaneGenerator.hpp` | OSM lane tags → LaneSection (bus, cycle, turn, sidewalk) |
| `RoundaboutGenerator.hpp` | Roundabout geometry + ring road creation |
| `RoadMarkingGenerator.hpp` | Center lines, lane dividers, edge lines, crosswalks |
| `TrafficSignGenerator.hpp` | Stop, yield, speed limit, signals, bus stops |
| `OsmProjectSerializer.hpp` | Save/reload complete OSM project (.ogosm format) |
| `OsmExporter.hpp` | Export to OpenDRIVE (.xodr) and GeoJSON (.geojson) |
| `test_osm_pipeline.cpp` | 35 test cases, 155 assertions |

## OSM Import UI (`src/ui/roadstudio/`)

| File | Purpose |
|------|---------|
| `OsmImportDialog.hpp` | Import dialog: file selection, settings, progress, results |
| `OsmImportDialog.cpp` | MOC implementation for OsmImportDialog |
| `RoadStudioWidget.hpp` | Road Studio with OSM import toolbar button |
| `RoadStudioWidget.cpp` | Road Studio implementation (includes OSM dialog) |
| `RoadTypes.hpp` | Road/rail profile catalogs, road data model types |

## Road Studio UI — Simplified Tool Palette

The Road Studio sidebar (`MainWidget`) has been simplified to only two
active tools:

| Tool | Icon | Shortcut | Purpose |
|------|------|----------|---------|
| **Road** | `road_mode` | `R` | Draw new roads (click to place points) |
| **View** | `view_mode` | `Esc` | Select / pan / zoom (navigation) |

All other tools (roundabout, modify, marking, sign, furniture, snap settings,
measure, destroy, lane mode, draw options) have been removed from the active
UI. The underlying mode code still exists in `main_widget.cpp` but is not
exposed.

Esc always returns to the safe View mode (unless typing in a text field).

## Cross-Section Studio (`LaneConfigWidget`)

The profile selector and lane configuration have been unified into a single
Cross-Section Studio panel inside `LaneConfigWidget`.

### Constructor

```cpp
LaneConfigWidget(bool verticalLayout = false, bool showProfileSelector = true);
```

- `verticalLayout=true` — stacks controls vertically (used in DrawOptionDialog)
- `showProfileSelector=false` — hides the profile combo, metadata fields,
  reset/save buttons. Used by `DrawOptionDialog` to prevent the user from
  changing presets in the popup (which previously caused a crash because the
  dialog's LaneConfigWidget has no `ProfileChanged` connection).

### Two instances

| Instance | Location | `showProfileSelector` | Purpose |
|----------|----------|----------------------|---------|
| Sidebar | `MainWidget` constructor | `true` (default) | Full profile editing |
| Dialog | `DrawOptionDialog` constructor | `false` | Lane count/width editing only |

### Profile System

**Road profiles** (`roads::RoadProfileCatalog` in `RoadTypes.hpp`):
- Keys: `city_2x1`, `city_2x2`, `city_2x3`, `city_oneway_1x2`, `city_oneway_1x3`,
  `country_2x1`, `country_2x2`, `highway_2x2`, `highway_2x3`, `highway_2x4`,
  `industrial_2x1`, `industrial_2x2`, `expressway_2x3`, `expressway_2x4`
- Default: `city_2x1`
- Fallback for unknown key: `custom`

**Rail profiles** (`roads::RailProfileCatalog` in `RoadTypes.hpp`):
- Keys: `single_standard`, `single_narrow`, `single_broad`, `double_standard`,
  `triple_standard`, `quad_standard`, `high_speed`, `subway`, `tram`
- Default: `single_standard`
- Fallback for unknown key: `custom_rail`

Each `RoadProfile` contains: `laneWidth`, `leftLanes`, `rightLanes`,
`leftOffsetX2`, `rightOffsetX2`, `speedLimit`, `hasSidewalk`, `hasCurb`,
`surfaceTexture`, `markingTexture`, `description`.

### Mode Switching

| Method | Shows widget? | Populates profiles? | Used by |
|--------|--------------|--------------------|---------| 
| `GotoRoadMode()` | Yes | Yes (if empty or was rail) | User action |
| `GotoRailMode()` | Yes | Yes (if empty or was road) | User action |
| `GotoLaneMode()` | Yes | No | User action |
| `SetRoadModeOnly()` | No | Yes (if `hasProfileSelector`) | `MainWidget::SetRailMode(false)` during construction |
| `SetRailModeOnly()` | No | Yes (if `hasProfileSelector`) | `MainWidget::SetRailMode(true)` during construction |

The `SetRoadModeOnly`/`SetRailModeOnly` methods exist to avoid calling `show()`
during construction, which would make the LaneConfigWidget visible before the
user picks the Road tool.

### Signals

- `ProfileChanged(const QString& key)` — emitted after `LoadProfile()` completes
- `RoadMetadataChanged(double speed, bool sidewalk, bool curb)` — emitted when
  speed/sidewalk/curb controls change (suppressed during `applyingProfile`)

### Modified-from-profile tracking

- `applyingProfile` guard prevents `CheckModified()` from firing while a preset
  is being loaded
- `modifiedFromProfile` flag tracks whether the user has tweaked away from the
  loaded preset
- `modifiedLabel` shows "Modified from \<profile\>" when true
- `resetButton` reloads the original preset
- `savePresetButton` saves the current config as a new custom preset

## Key Conventions

- Qt 6 API: use `QStringView` (not `QStringRef` which was removed in Qt 6)
- Use `QLatin1String` for `QStringView` comparisons
- XML test strings must not have leading whitespace before `<?xml`
- Road engine `Lane.width` is `Polynomial3`, not `double`
- All OSM core headers are header-only (no .cpp files needed)
- OSM headers that use `RoadV2` must only be included from `.cpp` files in
  the main app (not from headers) to avoid conflict with the public
  placeholder `road_v2.hpp` used by `road_engine.hpp`
- Q_OBJECT headers included from .cpp files need a corresponding .cpp entry
  in CMakeLists.txt for AUTOMOC to process them
- Do NOT add `fprintf(stderr, ...)` debug prints to production source — use
  `appLog()` instead. Temporary diagnostics must be removed before committing.

## LaneMaker Global State

LaneMaker uses several global pointers that are shared across all embedded
instances. These are rebound when a widget becomes visible via
`MainWidget::rebindGlobals()`:

| Global | Set in | Purpose |
|--------|--------|---------|
| `g_mainWindow` | `MainWindow::showEvent()` | Active LaneMaker main window |
| `g_laneConfig` | `MainWidget::rebindGlobals()` (called from `showEvent()`) | Active lane config widget |
| `LM::g_mapViewGL` | `MainWidget::rebindGlobals()` (called from `showEvent()`) | Active OpenGL map view |
| `MainWidget::instance` | `MainWidget::rebindGlobals()` (called from `showEvent()`) | Active MainWidget |

**Hazard:** Since both Road Studio and Train Studio construct their LaneMaker
`MainWindow` at app startup, the globals point to whichever was constructed
last (Train Studio). They are rebound to the correct instance when the user
switches workspaces (via `showEvent()`). Code that uses these globals before
any workspace is shown will access the Train Studio instance.

**Use `LaneMakerContext::current()`** (from `lanemaker_context.h`) to get a
snapshot of the active instance's pointers in a structured way.

**Note:** LaneMaker singletons (`World::Instance()`, `ChangeTracker::Instance()`,
`ActionManager::Instance()`, `SignRegistry::Instance()`, etc.) remain
process-wide and are NOT rebound. Only the pointer-based globals above are
rebound on workspace switch.

## Clean Build from Scratch

```bash
# 1. Remove the build directory
rmdir /S /Q D:\git\OpenGeoStudio-Qt\build

# 2. Configure with vcpkg + Qt + Ninja
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake -B D:\git\OpenGeoStudio-Qt\build -S D:\git\OpenGeoStudio-Qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6 2>&1"

# 3. Build
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target OpenGeoStudio 2>&1"
```

vcpkg manifest mode auto-installs 120 packages (boost, cgal, curl, gtest,
nlohmann-json, spdlog, tiff, libpng, cereal, etc.) on first configure.

## Road Engine Layout

- `src/engine/road/road_v2.hpp` (root) — INTERNAL full RoadV2 model; LaneSection
  comes from `lane_engine.hpp`. Used by the OSM pipeline and internal road
  engine headers.
- `src/engine/road/road_engine/public/road_v2.hpp` — PUBLIC API facade with a
  self-contained placeholder LaneSection. Used by `road_engine.hpp`.
- Both are intentional and must stay in sync for the RoadV2 class surface.
  See `docs/ROAD_V2_DUAL_LAYERS.md` for the full explanation.

## Logging Conventions

- Use the centralized `Logger` (via `appLog()` from `src/core/logger/Logger.hpp`)
  for ALL application logging. Do NOT add new `qDebug()`/`qWarning()` calls.
- `appLog().debug/info/warn/error(...)` — variadic, accepts QString, numbers,
  and `const char*`.
- `Logger::addFileTransport(path)` enables optional file output (append mode).
- LaneMaker (`src/engine/lanemaker/`) uses its own spdlog — leave untouched to
  preserve embedded engine behavior.
- Test executables may use `qDebug()` directly for their own output.

## Async Terrain Pipeline

The terrain pipeline can run in two modes:

- **Synchronous:** `TerrainManager::runPipeline(config)` — blocks the
  calling thread. Use only from worker threads or tests.
- **Asynchronous:** `TerrainManager::runPipelineAsync(config)` — runs
  on a dedicated `QThread` with an event loop. Progress and completion
  are reported via the same `progress`, `finished`, and `stageResult`
  signals (delivered via queued connections to the UI thread).

The async API uses a `PipelineWorker` QObject that lives on a `QThread`.
The worker calls `TerrainManager::runPipeline()` directly, creating a
temporary `DownloadManager` on the worker thread (to avoid cross-thread
`QNetworkAccessManager` access). The original downloader is restored
after the pipeline finishes.

The worker thread has an event loop so `DownloadManager::downloadSync`
(which uses `QEventLoop`) works correctly.

`DownloadManager` also provides `downloadAllAsync()` for fully
signal-based downloads without `QEventLoop`. This is available for
future use but not yet wired into the pipeline.

The `TerrainPipelinePanel` UI uses `runPipelineAsync()` to avoid
blocking the UI thread during pipeline execution.

## Error Handling Conventions

- Core pipelines return results with a `success` flag + `QString errorMessage`
  (see `OsmImportPipeline::ImportResult`, `RailImportResult`).
- UI reports failures via `QMessageBox::critical` on the import dialogs.
- Terrain manager reports stage results via `StageStatus` + `addResult` and
  emits `finished(bool success, const QString& message)`.
- World model reports issues via `ValidationError` structs returned from
  `validate()`.
- Do not throw exceptions across module boundaries in the core pipelines;
  return `success=false` + `errorMessage` instead.

## Environment Variables

| Variable | Purpose | Required |
|----------|---------|----------|
| `GPXZ_API_KEY` | GPXZ elevation API key | Only for GPXZ tests/downloads |
| `OPENTOPO_API_KEY` | OpenTopography DEM API key | Only for OpenTopo downloads |
| `MAPBOX_TOKEN` | Mapbox imagery/terrain token | Only for Mapbox downloads |
| `GLAD_USERNAME` | GLAD (Global Land Analysis) username | Only for GLAD downloads |
| `GLAD_PASSWORD` | GLAD password | Only for GLAD downloads |
| `OGS_SKIP_ROAD_MODEL_TESTS` | Skip LaneMaker road model tests that cause non-deterministic SEH crashes in test mode | Set to `1` in CI/CTest |
| `QT_QPA_PLATFORM` | Qt platform plugin (set to `offscreen` for headless tests) | Only for UI smoke tests |
| `QT_PLUGIN_PATH` | Path to Qt plugins directory | Only for CTest in CI |
