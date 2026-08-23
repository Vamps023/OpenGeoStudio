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
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake -B D:\git\OpenGeoStudio-Qt\build -S D:\git\OpenGeoStudio-Qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6 2>&1"

# Build the application
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target OpenGeoStudio 2>&1"

# Build everything (app + tests)
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build 2>&1"

# Run all tests
cd /d D:\git\OpenGeoStudio-Qt\build && ctest --output-on-failure --timeout 120

# Create portable ZIP package
cmake --build D:\git\OpenGeoStudio-Qt\build --target package_portable
```

## Version

Single source of truth: `project(OpenGeoStudio VERSION 1.0.0)` in CMakeLists.txt.
Exposed via `OGS_VERSION` compile definition. Used in:
- Application UI (HomeWidget)
- Executable metadata (app.rc)
- NSIS installer (OpenGeoStudio.nsi)
- Portable ZIP package name
- Release CI/CD workflow

## Test Targets

| Target | Description | Expected |
|--------|-------------|----------|
| `geometry_segment_tests` | Road engine geometry tests | 504 tests, 5388 assertions |
| `test_terrain_pipeline` | Terrain pipeline tests | 28/28 pass |
| `test_world_model` | World Authoring model tests | 34/34 pass |
| `test_world_workflow` | World Authoring workflow tests | 22/22 pass |
| `test_crs_system` | CRS system tests | 89/89 pass |
| `test_osm_pipeline` | OSM import pipeline tests | 155/155 pass |
| `test_houston_roundtrip` | Houston OSM round-trip tests | 56/56 pass |
| `test_geotiff_writer` | GeoTIFF writer tests | WGS84 + UTM metadata checks |
| `test_gpxz_download` | GPXZ elevation API tests | Requires `GPXZ_API_KEY`; skips without it |
| `test_road_studio` | Road Studio feature tests | 388 pass (signs, markings, furniture, snapping, measurement, persistence, templates, road creation, multi-lane, lane width, cross-section, mesh generation, junctions). 4 tests catch and ignore LaneMaker SEH exceptions in test mode. |
| `test_road_studio_ui` | Road Studio UI smoke test (headless) | 12/12 pass — tests LaneConfigWidget, profile switching, ActionManager, Preference system. Runs in 0.08s. |

**All 11 CTest targets pass (100%), 0 disabled, 0 skipped.**

CTest uses a generated wrapper batch script (`ogs_test_wrapper.bat`) to set
up the PATH for Qt and QMapLibre DLLs before running each test.

## Architecture

- **C++20 / Qt 6 / OGRE-Next 4.x** native desktop application
- **Build system:** CMake + Ninja (Release config)
- **Version:** 1.0.0 (single source in CMakeLists.txt)
- **CRS:** PROJ-backed via `gis::CRSManager` and `gis::CoordinateTransform`
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

## Road Studio UI — Simplified Tool Palette

Only two tools are exposed:
- **Road** (`R`) — Draw new roads
- **View** (`Esc`) — Select / pan / zoom (navigation)

All other tools are removed from the active UI.

## CRS Architecture

```
User Project CRS
    ↓
CRSManager (PROJ-backed)
    ↓
CoordinateTransform
    ↓
Terrain / Map / OSM / Road / Rail / World
    ↓
Export (GeoTIFF / GeoJSON / XODR)
```

- `gis::CRSDefinition` is the canonical CRS representation
- `gis::CRSManager` provides EPSG lookup and search via proj.db
- `gis::CoordinateTransform` uses PROJ for all coordinate transformations
- Legacy `CrsSpec` is a compatibility wrapper, not the authoritative path

## Distribution

### Portable ZIP
- Target: `package_portable`
- Output: `OpenGeoStudio-1.0.0-Windows-x64.zip` (~41 MB)
- Contains all runtime DLLs, Qt plugins, PROJ database, OGRE resources
- No installation required — extract and run

### NSIS Installer
- Script: `installer/OpenGeoStudio.nsi`
- Output: `OpenGeoStudio-1.0.0-Windows-x64.exe`
- Creates Start Menu shortcuts, optional desktop shortcut
- Supports uninstall via Add/Remove Programs
- Registry entries for version tracking

### Release CI/CD
- Workflow: `.github/workflows/release.yml`
- Triggered by git tags (`v*`)
- Builds, tests, creates ZIP + installer, generates checksums
- Creates GitHub Release with download links

## Logging Conventions

- Use `appLog()` from `src/core/logger/Logger.hpp` for ALL application logging
- LaneMaker uses its own spdlog — leave untouched
- Test executables may use `qDebug()` directly

## Error Handling

- Core pipelines return results with `success` flag + `errorMessage`
- UI reports failures via `QMessageBox::critical`
- Do not throw exceptions across module boundaries

## Environment Variables

| Variable | Purpose | Required |
|----------|---------|----------|
| `GPXZ_API_KEY` | GPXZ elevation API | Only for GPXZ tests |
| `OPENTOPO_API_KEY` | OpenTopography DEM | Only for OpenTopo |
| `MAPBOX_TOKEN` | Mapbox imagery | Only for Mapbox |
| `OGS_SKIP_ROAD_MODEL_TESTS` | Skip road model tests | Set to `1` in CI |
| `QT_QPA_PLATFORM` | Qt platform plugin | `offscreen` for headless |
