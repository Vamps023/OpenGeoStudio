# OpenGeoStudio

Native C++20 / Qt 6 desktop application for geospatial road design.

**This is the primary OpenGeoStudio repository.** The legacy
Electron/React/TypeScript application has been superseded by this
native C++/Qt implementation. No Electron, React, Node.js,
TypeScript, Vite, N-API, or JavaScript runtime is required.

## Features

- **Home** — Project management, recent projects, templates
- **Terrain Studio** — Satellite imagery, area selection, tile grid
  export with DEM and imagery download
- **Road Studio** — 2D/3D road network design with C++ geometry
  engine and LaneMaker integration
- **Train Studio** — 2D railway track editing with line and arc tools

## Architecture

```
OpenGeoStudio-Qt/
├── CMakeLists.txt          ← Top-level build (C++20, Qt 6, windeployqt)
├── vcpkg.json              ← C++ deps (nlohmann-json, curl, tiff, libpng,
│                             spdlog, boost-optional, cgal, gtest)
├── src/
│   ├── app/
│   │   └── main.cpp        ← QApplication + MainWindow shell
│   ├── core/               ← ApplicationContext, ProjectManager,
│   │                         WorkspaceManager, EventBus, Logger
│   ├── engine/
│   │   ├── road/           ← C++ road geometry engine (header-only)
│   │   └── lanemaker/      ← LaneMaker + libOpenDRIVE (CGAL/Boost/spdlog)
│   ├── ui/
│   │   ├── home/           ← Home workspace
│   │   ├── roadstudio/     ← Road Studio (2D/3D, LaneMaker, inspector)
│   │   ├── trainstudio/    ← Train Studio (2D track editing)
│   │   └── terrain/        ← Terrain Studio (selection, export)
│   └── plugin/
│       └── PluginApi.hpp   ← C++ plugin ABI (QPluginLoader-based)
├── docs/
│   ├── MIGRATION_AUDIT.md
│   ├── PHASE_7_VALIDATION.md
│   ├── PHASE_8_WEB_STACK_REMOVAL.md
│   └── ogproj-schema.json
├── scripts/
│   └── package.ps1         ← Build + create portable zip
└── .github/workflows/ci.yml
```

## Building

### Prerequisites

- **CMake 3.21+**
- **Visual Studio 2022** (MSVC)
- **Qt 6.8.0** (msvc2022_64) — Core, Gui, Widgets, Network, OpenGL, OpenGLWidgets
- **Ninja**
- **vcpkg**
- **MapLibre Native Qt** (built and installed)

### Build

```powershell
cmake -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake `
    -DENABLE_LANEMAKER=ON

cmake --build build
```

The portable `.exe` + DLLs are in `build/deploy/`.

### LaneMaker

LaneMaker is **enabled by default** with `-DENABLE_LANEMAKER=ON`.
It provides:
- `ConnectRays` — Composes Line + Arc + Line with G1 continuity
- `FitSpiral` — Euler spiral (clothoid) for G2 continuity
- `FitArcOrLine` — Simple arc or line fitting
- libOpenDRIVE — OpenDRIVE XML parsing and mesh generation
- Junction generation, curve fitting, road operations

Requires CGAL, Boost, spdlog (installed automatically via vcpkg).

### Package

```powershell
.\scripts\package.ps1
```

Creates `dist/OpenGeoStudio-<timestamp>.zip` (portable, no install needed).

## Runtime Requirements

- Windows 10/11 (64-bit)
- DirectX 11 or 12
- ~50 MB disk space
- **No Node.js, Electron, or JavaScript runtime**

## License

MIT (application) + Apache-2.0 (libOpenDRIVE, LaneMaker) + MIT (pugixml) + ISC (earcut)
