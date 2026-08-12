# Phase 7: Validation Against Electron Reference

## Overview

This document validates the native C++/Qt OpenGeoStudio application against
the reference Electron/React/TypeScript implementation at `D:/git/OpenGeoStudio`.

The validation covers feature parity, architecture compliance, and runtime
requirements.

## Architecture Compliance

| Requirement | Status | Notes |
|-------------|--------|-------|
| No Electron | ✅ | Native Qt 6 executable |
| No React | ✅ | Qt Widgets UI |
| No Node.js | ✅ | No Node runtime |
| No TypeScript | ✅ | Pure C++20 |
| No Vite | ✅ | CMake + Ninja build |
| No N-API | ✅ | Direct C++ engine calls |
| No JavaScript runtime | ✅ | No JS engine |
| Native Windows .exe | ✅ | `OpenGeoStudio.exe` |
| Qt 6 and C++20 | ✅ | Qt 6.8.0, MSVC 2022, C++20 |
| Portable directory | ✅ | `build/deploy/` with DLLs |
| C++ road engine preserved | ✅ | Header-only engine reused directly |

## Feature Parity Matrix

### Application Shell (Phase 3)

| Feature | Reference | Native | Status |
|---------|-----------|--------|--------|
| Central application context | ApplicationContext (DI) | ApplicationContext (Qt) | ✅ |
| Project CRUD | ProjectManager (TS) | ProjectManager (C++) | ✅ |
| .ogproj loading/saving | JSON schema | JSON schema | ✅ |
| Project folder structure | 14 folders | 14 folders | ✅ |
| Recent projects | Electron store | QSettings | ✅ |
| Workspace switching | React QStackedWidget | QStackedWidget | ✅ |
| Dock widgets | React panels | QDockWidget | ✅ |
| Menu bar | Electron menu | QMenuBar | ✅ |
| Toolbar | React toolbar | QToolBar | ✅ |
| Dark palette | CSS dark theme | Qt dark palette | ✅ |
| Event bus | TS EventBus | Qt EventBus | ✅ |
| Logger | TS winston | Qt Logger | ✅ |

### Road Studio (Phase 4)

| Feature | Reference | Native | Status |
|---------|-----------|--------|--------|
| Road data model | types.ts | RoadTypes.hpp | ✅ |
| Control points | TS ControlPoint | C++ ControlPoint | ✅ |
| Bezier handles | Optional Vec2 | Optional Vec2 | ✅ |
| Segment metadata | SegmentMetadata | (engine types) | ✅ |
| Road profiles | profile field | profile field | ✅ |
| Geo/local conversion | equirectangular | GeoConvert.hpp | ✅ |
| 2D road rendering | Canvas 2D | QPainter | ✅ |
| Satellite imagery | MapLibre GL JS | MapLibre Native Qt | ✅ |
| Select tool | Click + hit test | Click + hit test | ✅ |
| Road drawing tool | LaneMaker 3-click | LaneMaker 3-click | ✅ |
| Endpoint snapping | 12px tolerance | 12px tolerance | ✅ |
| Control point dragging | Mouse drag | Mouse drag | ✅ |
| Road creation | Store action | Store action | ✅ |
| Road deletion | Store action | Store action | ✅ |
| Road property editing | Inspector panel | RoadInspector | ✅ |
| Lane configuration | Inspector | Inspector | ✅ |
| Undo/redo | Zustand stack | Qt stack (50 limit) | ✅ |
| Road engine integration | N-API IPC | Direct C++ calls | ✅ |
| Centerline sampling | roadEngine.sampleCenterline | Road::sampleCenterline | ✅ |
| Edge generation | roadEngine.sampleLeftEdge | Computed from centerline | ⚠️ |
| Lane boundaries | roadEngine.generateLaneBoundaries | geo::generateLaneBoundaries | ✅ |
| Road mesh generation | roadEngine.generateRoadMesh | geo::generateRoadMesh | ✅ |
| Roadmark mesh | roadEngine.generateRoadmarkMesh | Not yet ported | ❌ |
| Junction support | intersection.hpp | Not yet ported | ❌ |
| Debug layers | 14 layers | 9 of 18 layers | ⚠️ |
| 3D mesh viewport | Three.js | QOpenGLWidget | ✅ |
| OpenDRIVE export | roadEngine.exportOpenDrive | Stub (include conflict) | ❌ |
| Persistence | Project state | Project state | ✅ |

### Train Studio (Phase 5)

| Feature | Reference | Native | Status |
|---------|-----------|--------|--------|
| Track data model | types.ts | TrainTypes.hpp | ✅ |
| Track gauge | 1.435m standard | 1.435m standard | ✅ |
| 2D track rendering | Canvas 2D | QPainter | ✅ |
| Select tool | Click + hit test | Click + hit test | ✅ |
| Line tool | Click to add | Click to add | ✅ |
| Arc tool | 2-click workflow | 2-click workflow | ✅ |
| Undo/redo | Zustand stack | Qt stack (50 limit) | ✅ |
| XML export | Oksygen format | Oksygen format (simplified) | ⚠️ |
| OSM railway import | Overpass API | Not yet ported | ❌ |
| XML validation | Schema validation | Not yet ported | ❌ |
| Curve smoothing | Douglas-Peucker | Not yet ported | ❌ |

### Terrain and Export (Phase 6)

| Feature | Reference | Native | Status |
|---------|-----------|--------|--------|
| Area selection | Shift+drag | Shift+drag | ✅ |
| 1:1 square constraint | Yes | Yes | ✅ |
| Tile grid (1/2/4/8 km) | Yes | Yes | ✅ |
| Tile selection | Click toggle | Click toggle | ✅ |
| Heightmap formats | PNG16, R16, GeoTIFF | PNG16 only | ⚠️ |
| Albedo formats | PNG, GeoTIFF | PNG only | ⚠️ |
| DEM sources | 6 sources | 6 sources (URLs) | ✅ |
| Imagery sources | 3 sources | 3 sources | ✅ |
| API key management | Settings store | QSettings/inline | ✅ |
| Export progress | Stage tracking | Progress bar | ✅ |
| Terrain manifest | JSON | JSON | ✅ |
| GeoTIFF writer | Custom TS | Not yet ported | ❌ |
| OSM import | Overpass API | Not yet ported | ❌ |

## Runtime Dependencies

### Native Qt Application

| Dependency | Type | Required |
|------------|------|----------|
| Qt6Core.dll | Qt runtime | ✅ |
| Qt6Gui.dll | Qt runtime | ✅ |
| Qt6Widgets.dll | Qt runtime | ✅ |
| Qt6Network.dll | Qt runtime | ✅ |
| Qt6OpenGL.dll | Qt runtime | ✅ |
| Qt6OpenGLWidgets.dll | Qt runtime | ✅ |
| QMapLibre.dll | MapLibre Native | ✅ |
| QMapLibreWidgets.dll | MapLibre Native | ✅ |
| MSVC runtime | C++ runtime | ✅ |
| D3D11/D3D12 | Windows graphics | ✅ |

**No Node.js, Electron, or JavaScript runtime required.**

## Known Gaps

1. **OpenDRIVE export** — Engine's `opendrive.hpp` has include conflicts with
   root-level headers. Needs include path cleanup.

2. **Roadmark mesh generation** — Not yet called from the engine service.

3. **Junction support** — `intersection.hpp` not yet integrated.

4. **GeoTIFF output** — Requires libtiff integration for proper GeoTIFF writing.

5. **OSM railway import** — Requires Overpass API client (Qt Network).

6. **9 debug layers** — Require deeper engine internals (offset curves, fillet
   arcs, tangent points, trim points, intersection polygon, triangulation,
   vertex normals, UV grid, lane IDs).

7. **Track control point dragging** — Store doesn't expose mutable track access.

8. **XML validation** — Requires XML schema validation.

## Validation Methodology

### Build verification

```powershell
cmake -S D:/git/OpenGeoStudio-Qt -B D:/git/OpenGeoStudio-Qt/build -G Ninja
cmake --build D:/git/OpenGeoStudio-Qt/build
```

Produces: `D:/git/OpenGeoStudio-Qt/build/deploy/OpenGeoStudio.exe`

### Manual verification

1. Launch `OpenGeoStudio.exe`
2. Verify Home workspace shows recent projects
3. Switch to Terrain Studio — verify satellite imagery loads
4. Shift+drag to select area — verify tile grid appears
5. Switch to Road Studio — verify satellite imagery loads
6. Click "Demo Road" — verify road renders with centerline/edges
7. Click 2D/3D toggle — verify 3D mesh viewport
8. Switch to Train Studio — verify satellite imagery loads
9. Use Line tool — verify track creation
10. Verify workspace switching preserves state

### Automated tests

- `geometry_segment_tests` (261 doctest tests, 2210 assertions) — C++ engine
- Additional Road Studio store/viewport tests — TODO

## Conclusion

The native C++/Qt application achieves **core feature parity** with the
reference Electron application for the primary workflows:

- ✅ Project management
- ✅ Road Studio 2D/3D editing with C++ engine
- ✅ Train Studio 2D track editing
- ✅ Terrain area selection and export
- ✅ Satellite imagery rendering
- ✅ No JavaScript/Node.js runtime

The remaining gaps are in advanced features (OpenDRIVE export, GeoTIFF
output, OSM import, junctions, roadmark meshes) that can be addressed
in future iterations without affecting the core architecture.
