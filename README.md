# OpenGeoStudio

Native C++20 / Qt 6 desktop application for geospatial road and track design.

**This is the primary OpenGeoStudio repository.** The legacy
Electron/React/TypeScript application has been superseded by this
native C++/Qt implementation. No Electron, React, Node.js,
TypeScript, Vite, N-API, or JavaScript runtime is required.

## Features

- **Home** — Project management, recent projects, templates
- **Terrain Studio** — Satellite imagery, area selection, tile grid
  export with DEM and imagery download
- **Road Studio** — 2D/3D road network design with C++ geometry
  engine, LaneMaker integration, Cross-Section Studio (profile presets,
  lane configuration, speed/sidewalk/curb metadata)
- **Train Studio** — 2D railway track editing with rail profile catalog
- **3D Studio** — OGRE-Next 4.x embedded 3D level editor

## Architecture

```
OpenGeoStudio-Qt/
├── CMakeLists.txt          ← Top-level build (C++20, Qt 6, windeployqt)
├── vcpkg.json              ← C++ deps (nlohmann-json, curl, tiff, libpng,
│                             spdlog, boost-optional, cgal, gtest, cereal)
├── src/
│   ├── app/
│   │   └── main.cpp        ← QApplication + MainWindow shell (5 workspaces)
│   ├── core/               ← ApplicationContext, ProjectManager,
│   │                         WorkspaceManager, EventBus, Logger
│   ├── engine/
│   │   ├── road/           ← C++ road geometry engine (header-only, RoadV2)
│   │   └── lanemaker/      ← LaneMaker + libOpenDRIVE (CGAL/Boost/spdlog)
│   ├── ui/
│   │   ├── home/           ← Home workspace
│   │   ├── roadstudio/     ← Road Studio (LaneMaker, OSM import, profiles)
│   │   ├── trainstudio/    ← Train Studio (LaneMaker rail mode, OSM import)
│   │   ├── terrain/        ← Terrain Studio (selection, export, layers)
│   │   └── studio3d/       ← 3D Studio (OGRE-Next editor)
│   └── plugin/
│       └── PluginApi.hpp   ← C++ plugin ABI (QPluginLoader-based)
├── docs/
│   ├── CROSS_SECTION_STUDIO.md    ← Profile + lane config architecture
│   ├── ROAD_V2_DUAL_LAYERS.md     ← RoadV2 internal vs public model
│   ├── TERRAIN_PIPELINE_ARCHITECTURE.md
│   └── ogproj-schema.json
├── scripts/
│   └── package.ps1         ← Build + create portable zip
└── .github/workflows/ci.yml
```

## Building

### Prerequisites

- **CMake 3.21+**
- **Visual Studio 2022 Build Tools** (MSVC 14.4x, toolset 143)
- **Qt 6.8.0** (msvc2022_64) — Core, Gui, Widgets, Network, OpenGL, OpenGLWidgets, Svg
- **Ninja**
- **vcpkg** (with manifest mode)
- **MapLibre Native Qt** (built and installed)
- **OGRE-Next 4.x** (for 3D Studio)

### Configure

```powershell
cmake -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 `
    -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6
```

### Build

```powershell
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build build --target OpenGeoStudio 2>&1"
```

The portable `.exe` + DLLs are in `build/deploy/`.

**CRITICAL:** Always use the Build Tools `vcvars64.bat`, not the VS Community
one, to avoid an MSVC/STL version mismatch (`STL1001`).

### Clean Build from Scratch

```powershell
rmdir /S /Q build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 `
    -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6
cmake --build build --target OpenGeoStudio
```

vcpkg manifest mode auto-installs 120 packages on first configure.

### Tests

```powershell
cmake --build build --target test_road_studio
cd build\deploy && test_road_studio.exe
```

| Target | Tests |
|--------|-------|
| `test_world_model` | 34 |
| `test_world_workflow` | 22 |
| `test_osm_pipeline` | 155 |
| `test_road_studio` | 334 |
| `geometry_segment_tests` | 261 |

### LaneMaker

LaneMaker is **enabled by default**. It provides:
- `ConnectRays` — Composes Line + Arc + Line with G1 continuity
- `FitSpiral` — Euler spiral (clothoid) for G2 continuity
- `FitArcOrLine` — Simple arc or line fitting
- libOpenDRIVE — OpenDRIVE XML parsing and mesh generation
- Junction generation, curve fitting, road operations
- Cross-Section Studio — profile presets, lane config, metadata

Requires CGAL, Boost, spdlog (installed automatically via vcpkg).

### Package

```powershell
.\scripts\package.ps1
```

Creates `dist/OpenGeoStudio-<timestamp>.zip` (portable, no install needed).

## Road Studio — Simplified UI

The Road Studio sidebar has been simplified to two active tools:

| Tool | Shortcut | Purpose |
|------|----------|---------|
| **Road** | `R` | Draw new roads |
| **View** | `Esc` | Select / pan / zoom |

The Cross-Section Studio panel (in the sidebar) provides:
- Road/rail profile preset selection (14 road profiles, 9 rail profiles)
- Lane count, width, offset, direction controls
- Speed limit, sidewalk, curb metadata
- Modified-from-profile indicator with Reset and Save-as-Preset

See `docs/CROSS_SECTION_STUDIO.md` for full architecture details.

## Runtime Requirements

- Windows 10/11 (64-bit)
- DirectX 11 or 12
- ~50 MB disk space
- **No Node.js, Electron, or JavaScript runtime**

## License

MIT (application) + Apache-2.0 (libOpenDRIVE, LaneMaker) + MIT (pugixml) + ISC (earcut)
