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
  LaneMaker road creation, control point editing, debug layers
- **Train Studio** — 2D railway track editing with line and arc tools

## System Requirements

- Windows 10/11 (64-bit)
- DirectX 11 or 12 capable GPU
- 100 MB disk space

## Running

Extract the archive and run `OpenGeoStudio.exe`. No installation required.

## Architecture

```
OpenGeoStudio.exe
    |
    Qt 6 (Widgets, Network, OpenGL)
    |
    C++20 Core
    |
    +-- Road Engine (header-only C++)
    +-- MapLibre Native Qt (satellite imagery)
    +-- Qt Network (DEM/imagery download)
```

## Building from Source

### Prerequisites

- Visual Studio 2022 (MSVC)
- CMake 3.21+
- Ninja
- Qt 6.8.0 (msvc2022_64)
- vcpkg
- MapLibre Native Qt (built and installed)

### Build

```powershell
cmake -S . -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The executable and all dependencies are placed in `build/deploy/`.

## License

MIT
