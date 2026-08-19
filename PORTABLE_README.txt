# OpenGeoStudio — Native C++/Qt Desktop Application

## Overview

OpenGeoStudio is a native Windows desktop application for geospatial
road and track design, built entirely with C++20 and Qt 6.

**No Electron, React, Node.js, TypeScript, or JavaScript runtime required.**

## Features

- **Home Workspace** — Project management, recent projects, templates
- **Terrain Studio** — Area selection on satellite imagery, tile grid
  export with DEM and imagery download
- **Road Studio** — 2D/3D road network design with C++ geometry engine,
  LaneMaker road creation, Cross-Section Studio (profile presets, lane
  configuration, speed/sidewalk/curb metadata), OSM import
- **Train Studio** — 2D railway track editing with rail profile catalog,
  OSM rail import
- **3D Studio** — OGRE-Next 4.x embedded 3D level editor

## System Requirements

- Windows 10/11 (64-bit)
- DirectX 11 or 12 capable GPU
- 100 MB disk space

## Running

Extract the archive and run `OpenGeoStudio.exe`. No installation required.

## Road Studio — Simplified UI

The Road Studio sidebar has two active tools:

- **Road** (R) — Draw new roads
- **View** (Esc) — Select / pan / zoom

The Cross-Section Studio panel provides:
- 14 road profile presets (city, country, highway, industrial, expressway)
- 9 rail profile presets (standard, narrow, broad, high-speed, subway, tram)
- Lane count, width, offset, direction controls
- Speed limit, sidewalk, curb metadata
- Modified-from-profile indicator with Reset and Save-as-Preset

## Architecture

```
OpenGeoStudio.exe
    |
    Qt 6 (Widgets, Network, OpenGL, Svg)
    |
    C++20 Core
    |
    +-- Road Engine (header-only C++, RoadV2/LaneSection)
    +-- LaneMaker (libOpenDRIVE, CGAL, Boost, spdlog)
    +-- MapLibre Native Qt (satellite imagery)
    +-- OGRE-Next 4.x (3D Studio)
    +-- Qt Network (DEM/imagery download)
```

## Building from Source

### Prerequisites

- Visual Studio 2022 Build Tools (MSVC, toolset 143)
- CMake 3.21+
- Ninja
- Qt 6.8.0 (msvc2022_64)
- vcpkg (with manifest mode)
- MapLibre Native Qt (built and installed)
- OGRE-Next 4.x (for 3D Studio)

### Build

```powershell
cmake -S . -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 ^
    -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6

cmake --build build --target OpenGeoStudio
```

The executable and all dependencies are placed in `build/deploy/`.

**CRITICAL:** Always use the Build Tools vcvars64.bat, not the VS Community
one, to avoid an MSVC/STL version mismatch.

## License

MIT (application) + Apache-2.0 (libOpenDRIVE, LaneMaker) + MIT (pugixml) + ISC (earcut)
