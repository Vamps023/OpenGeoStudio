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
| `geometry_segment_tests` | Road engine geometry tests | 261 tests |

## Architecture

- **C++20 / Qt 6 / OGRE-Next 4.x** native desktop application
- **Build system:** CMake + Ninja (Release config)
- **Road engine:** `src/engine/road/` (header-only, RoadV2/LaneSection model)
- **OSM pipeline:** `src/core/osm/` (header-only, OSM → RoadV2 conversion)
- **World model:** `src/core/world/` (World Authoring system)
- **3D Studio:** `src/ui/studio3d/` (OGRE-Next embedded editor)
- **Road Studio:** `src/ui/roadstudio/` (LaneMaker-based road editor)

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

## Logging Conventions

- Use the centralized `Logger` (via `appLog()` from `src/core/logger/Logger.hpp`)
  for ALL application logging. Do NOT add new `qDebug()`/`qWarning()` calls.
- `appLog().debug/info/warn/error(...)` — variadic, accepts QString, numbers,
  and `const char*`.
- `Logger::addFileTransport(path)` enables optional file output (append mode).
- LaneMaker (`src/engine/lanemaker/`) uses its own spdlog — leave untouched to
  preserve embedded engine behavior.
- Test executables may use `qDebug()` directly for their own output.

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
