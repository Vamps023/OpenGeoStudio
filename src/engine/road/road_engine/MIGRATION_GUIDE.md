# RoadEngine Migration Guide

This guide describes how to consume the RoadEngine C++ core library from an
external Qt/C++ project (or any CMake-based C++ project), independent of the
OpenGeoStudio Electron/N-API bindings.

## 1. Building the Core Library Standalone

The RoadEngine core is currently **header-only**. To use it you only need the
public headers located under:

```
app/native/src/road/road_engine/public/
```

A future CMake target (`road_engine::core`) will package these headers (and any
compiled sources) into an installable library. Until then, consumers can add the
`public/` directory to their include path directly.

### Standalone CMake build (planned)

```bash
cd app/native/src/road/road_engine
cmake -B build -DCMAKE_INSTALL_PREFIX=./install
cmake --build build
cmake --install build
```

This produces:

- `include/road_engine/road_engine.hpp` (and all public headers)
- `lib/road_engine_core.lib` / `libroad_engine_core.a` (once non-header sources exist)
- `lib/cmake/road_engine/RoadEngineConfig.cmake` (for `find_package`)

## 2. Consuming via CMake `find_package`

Once installed (or when the CMake package is available), consume RoadEngine from
your own project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyRoadApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate the installed RoadEngine package
find_package(RoadEngine 1.0.0 REQUIRED)

add_executable(my_road_app src/main.cpp)

# Link against the namespaced target
target_link_libraries(my_road_app PRIVATE road_engine::core)
```

### Include path usage

After linking, include the public API umbrella header:

```cpp
#include <road_engine/road_engine.hpp>
```

This single header pulls in all public types and inline function definitions.

## 3. Version Checking Macros

RoadEngine exposes semantic-version macros that consumers can check at compile
time:

```cpp
#include <road_engine/road_engine.hpp>

static_assert(ROAD_ENGINE_VERSION_MAJOR >= 1, "Requires RoadEngine >= 1.0");
static_assert(ROAD_ENGINE_VERSION_MINOR >= 0, "Requires RoadEngine >= 1.0");
static_assert(ROAD_ENGINE_VERSION_PATCH >= 0, "Requires RoadEngine >= 1.0");

// Runtime version string
const char* ver = road_engine::versionString();  // "1.0.0"
```

| Macro | Description |
|-------|-------------|
| `ROAD_ENGINE_VERSION_MAJOR` | Major version (breaking API changes) |
| `ROAD_ENGINE_VERSION_MINOR` | Minor version (backward-compatible additions) |
| `ROAD_ENGINE_VERSION_PATCH` | Patch version (bug fixes) |
| `ROAD_ENGINE_VERSION_STRING` | Stringified version, e.g. `"1.0.0"` |

Runtime accessors in the `road_engine` namespace:

- `road_engine::versionMajor()`
- `road_engine::versionMinor()`
- `road_engine::versionPatch()`
- `road_engine::versionString()`

## 4. Example CMakeLists.txt for a Consumer Project

```cmake
cmake_minimum_required(VERSION 3.20)
project(LaneMakerIntegration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# Qt (for the UI host application)
find_package(Qt6 COMPONENTS Widgets OpenGL REQUIRED)

# RoadEngine core library
find_package(RoadEngine 1.0.0 REQUIRED)

add_executable(lanemaker
    src/main.cpp
    src/MainWindow.cpp
    src/RoadEditorWidget.cpp
)

target_link_libraries(lanemaker
    PRIVATE
        Qt6::Widgets
        Qt6::OpenGL
        road_engine::core
)
```

## 5. Example C++ Code Using the Engine

```cpp
#include <road_engine/road_engine.hpp>

#include <iostream>
#include <vector>

int main() {
    using namespace geo;

    std::cout << "RoadEngine version: " << road_engine::versionString() << "\n";

    // Create a straight segment
    RoadToolParams params;
    params.width = 7.0;
    params.laneCount = 2;
    params.profileName = "city_2x1";

    Road seg = createSegment({0, 0}, {100, 0}, params);

    // Create a circular arc continuing from the segment end
    Point2D arcStart = seg.points.back().position;
    Point2D arcDir   = tangentAtEnd(seg);
    Road arc = createCircleArc(arcStart, arcDir, {150, 50}, 12, params);

    // Sample the arc centerline
    auto samples = arc.sampleCenterline(32);
    std::cout << "Arc samples: " << samples.size() << "\n";

    // Generate a renderable mesh
    MeshData mesh = generateRoadMesh(arc, 64);
    std::cout << "Mesh vertices: " << mesh.vertices.size() / 3 << "\n";
    std::cout << "Mesh triangles: " << mesh.indices.size() / 3 << "\n";

    // Export to OpenDRIVE XML
    std::string xodr = exportOpenDrive({seg, arc}, 48.0, 11.0);
    std::cout << "OpenDRIVE export length: " << xodr.size() << " bytes\n";

    return 0;
}
```

## 6. Migration Notes for LaneMaker

LaneMaker currently depends on the legacy headers under
`app/native/src/road/`. To migrate to the separated RoadEngine public API:

1. **Replace legacy includes** with the umbrella header:
   ```cpp
   // Before
   #include "geometry.hpp"
   #include "road.hpp"
   #include "road_tools.hpp"
   // After
   #include <road_engine/road_engine.hpp>
   ```

2. **Remove Skia/Qt dependencies** from any code that calls into the engine.
   The public API is pure C++20 with no UI framework dependencies.

3. **Link via CMake** instead of adding source files directly:
   ```cmake
   target_link_libraries(lanemaker PRIVATE road_engine::core)
   ```

4. **Verify version compatibility** using the version macros before using
   newer API features.

5. **Use the type mappings reference** (`type_mappings.md`) when bridging
   between the C++ engine types and any UI-layer representations.
