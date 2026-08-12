# Phase 2c: Rendering Spike — Decision Document

## Decision: GO with Option A (MapLibre Native Qt)

**Date:** 2026-08-12  
**Status:** Approved

## Summary

Option A (MapLibre Native Qt using `QMapLibre::MapWidget`) is selected as the
rendering architecture for the native Qt application. Option B (QGraphicsView
fallback) will not be implemented at this time.

## What was evaluated

| Criterion                          | Result |
|------------------------------------|--------|
| 2D raster map rendering            | PASS — Esri World Imagery tiles render correctly |
| Map panning                        | PASS — Native drag interaction |
| Map zooming                        | PASS — Scroll wheel zoom |
| Satellite imagery tile loading     | PASS — Esri tiles load over HTTPS |
| Coordinate conversion (geo/screen) | PASS — `coordinateForPixel` / `pixelForCoordinate` available |
| Qt 6 integration                   | PASS — `QRhiWidget` with OpenGL backend |
| Windows deployment                 | PASS — DLLs copied via CMake post-build |
| Runtime dependency handling        | PASS — `windeployqt` + manual MapLibre DLL copy |
| OpenGL reliability                 | PASS — NVIDIA RTX 3060, stable rendering |
| Road overlay compatibility         | READY — `QMapLibre::Map` API supports custom layers |
| Terrain overlay compatibility      | READY — GeoJSON source support available |
| No Electron/JS required            | PASS — Pure C++/Qt |

## Key findings

### 1. Lazy Map initialization
The `QMapLibre::Map` object is created lazily during `QRhiWidget::initialize()`,
not in the `MapWidget` constructor. The style must be set via
`Settings::setStyles()` before `MapWidget` construction. Calling
`setStyleJson()` after initialization via a timer loads style data but does not
trigger the GPU rendering pipeline — resulting in a black screen.

### 2. file:// URLs not supported
MapLibre's network stack cannot load `file://` URLs. The Esri style JSON is
served via a minimal `StyleHttpServer` (QTcpServer on localhost) at
`http://127.0.0.1:<port>/style.json`.

### 3. Background layer required for raster
Raster layers require a `background` layer in the style JSON to render
correctly in MapLibre Native Qt (see
[maplibre/maplibre-native-qt#279](https://github.com/maplibre/maplibre-native-qt/issues/279)).
Without it, the screen is black.

## Architecture

```
MapViewportWidget (QWidget)
    └── QMapLibre::MapWidget (QRhiWidget, OpenGL)
            └── QMapLibre::Map
                    ├── Esri World Imagery raster source
                    ├── Coordinate conversion API
                    └── Custom layer support (for road overlay)
```

## Known risks

1. **OpenGL dependency**: Requires OpenGL drivers. Fallback to software
   rendering is possible via `QT_OPENGL=software` but not tested.
2. **MapLibre DLL deployment**: `windeployqt` does not detect MapLibre DLLs
   automatically. A CMake post-build step copies them manually.
3. **Style server**: The local HTTP server adds a small runtime dependency.
   Future work could bundle the style as a Qt resource and use a custom
   `QFile`-based network handler.

## Files

- `src/app/MapViewportWidget.hpp` — Widget wrapper + StyleHttpServer
- `src/app/MapViewportWidget.cpp` — Implementation with Esri style JSON
- `src/app/main.cpp` — MainWindow with map as central widget
- `CMakeLists.txt` — MapLibre integration and DLL deployment

## Next steps

- Phase 3: Qt application shell (ApplicationContext, ProjectManager,
  WorkspaceManager, .ogproj loading/saving)
- Phase 4: Road Studio (2D road overlay on map, 3D mesh viewport, tools)
