# OpenGeoStudio (Qt)

Native C++20 / Qt 6 desktop application for geospatial road design.

This is the Qt-native rewrite of OpenGeoStudio. The original
Electron/React/TypeScript application lives in the
[`OpenGeoStudio`](../OpenGeoStudio) reference repository and is
used as the behavioral reference for feature-parity validation.

## Architecture

```
OpenGeoStudio-Qt/
├── CMakeLists.txt          ← Top-level build (C++20, Qt 6, windeployqt)
├── vcpkg.json              ← C++ dependency manifest (curl, tiff, libpng, nlohmann-json)
├── src/
│   ├── app/
│   │   └── main.cpp        ← QApplication + MainWindow shell
│   ├── engine/
│   │   ├── road/           ← C++ road geometry engine (header-only, preserved from reference)
│   │   └── lanemaker/      ← LaneMaker + libOpenDRIVE (optional, requires CGAL/Boost/spdlog)
│   └── plugin/
│       └── PluginApi.hpp   ← C++ plugin ABI (QPluginLoader-based)
├── docs/
│   ├── MIGRATION_AUDIT.md  ← Phase 1 audit (architecture map, migration matrix, plan)
│   ├── PLUGIN_ABI.md       ← Plugin system design spec
│   ├── ogproj-schema.json  ← .ogproj JSON Schema (Draft 2020-12)
│   └── baseline-test-results.txt  ← 986-test reference baseline
├── assets/                 ← Application icons
└── .github/workflows/ci.yml  ← CI (CMake + doctest)
```

## Building

### Prerequisites

- **CMake 3.16+**
- **C++20 compiler** (MSVC 2019+, GCC 10+, or Clang 12+)
- **Qt 6** (Core, Gui, Widgets) — install via [Qt Installer](https://www.qt.io/download)
- **Ninja** (recommended generator)

### Build (Windows, MSVC + Ninja)

```bash
# Set Qt path (adjust to your Qt installation)
set CMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64

# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH%

# Build
cmake --build build

# The portable .exe + DLLs are in build/deploy/
```

### Build with LaneMaker (optional)

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_LANEMAKER=ON
```

Requires CGAL, Boost, and spdlog (install via vcpkg).

### Run Tests

```bash
cd build
ctest
```

## License

MIT (application) + Apache-2.0 (libOpenDRIVE, LaneMaker) + MIT (pugixml) + ISC (earcut)

See [LICENSE](LICENSE) for details.
