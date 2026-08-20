# OpenGeoStudio-Qt — Reverse-Engineering & Developer Guide
## Part 2 of 3: Systems & Algorithms (Sections 13–25)

> **Repository:** `D:\git\OpenGeoStudio-Qt`
> **Analysis date:** 2026-08-19
> **Method:** Static source inspection only — no builds, no test runs, no code modifications.
> **Status:** **Reverse-engineering baseline — not yet runtime-validated.** All findings are from source reading. Test counts, runtime behavior, and `[UNKNOWN]` items require runtime verification before being treated as authoritative.
> **Certainty labels:**
> - `[CONFIRMED]` = verified from source code
> - `[INFERENCE]` = strong inference from evidence but not directly verified
> - `[UNKNOWN]` = not determinable from source
> - `[RISK]` = identified architectural or operational risk
> - `[RECOMMENDATION]` = suggested improvement (not current behavior)

This is Part 2 of a 3-part reverse-engineering document.
- **Part 1** (`docs/REVERSE_ENGINEERING_PART1_ARCHITECTURE.md`): Sections 0–12.
- **Part 2** (this file): Sections 13–25 — Algorithms, Plugin System, API/External Integrations, Database, File/Asset Pipelines, Configuration, Error Handling, Logging, Concurrency, Performance (with Hotspot Map), Testing, Build System, Deployment.
- **Part 3** (`docs/REVERSE_ENGINEERING_PART3_GUIDE.md`): Sections 26–39.

---

## 13. Algorithms

### 13.1 Geometry Generation (Road Engine)

**Algorithm:** Adaptive polyline sampling of road geometry segments.
**Purpose:** Convert analytic geometry (Line, Arc, Spiral, Bezier) into renderable polylines.
**Inputs:** Geometry segment + error tolerance.
**Outputs:** Sampled points along the segment.
**Key constants** (from `geometry_segment.hpp`):
- `MAX_GEOM_ERROR_HORIZONTAL = 0.25` m
- `MAX_GEOM_ERROR_VERTICAL = 0.1` m
- `GEOM_TOLERANCE = 0.2` m
- `MAX_GEOM_LENGTH = 50.0` m
- `MIN_GEOM_LENGTH = 0.1` m
- `ADAPTIVE_MAX_DEPTH = 20`
**Numerical assumptions:** Planar 2D; elevation separate. Positive curvature = left/CCW; negative = right/CW.
**Edge cases:** Zero-length segments, degenerate constant-curvature spirals, floating-point accumulation over 1000+ segments.
**Complexity:** O(n * ADAPTIVE_MAX_DEPTH) per segment where n = output samples.
**Implementation:** `src/engine/road/lane_sampling.hpp`, `geometry_segment.hpp`.
**Test coverage:** Extensive — Phases 2.3-2.4 in `geometry_segment_tests.cpp` (473 doctest cases total).
**Risks:** Floating-point accumulation over very long roads (tested with 1000-segment stress test).

### 13.2 Line/Arc/Spiral Fitting (LaneMaker)

**Algorithm:** Curve fitting from control points to geometry segments.
**Purpose:** Convert user-clicked control points into road geometry (line + arc + spiral sequences).
**Inputs:** Control points (rays from user clicks).
**Outputs:** `odr::RefLine` with Line/Arc/Spiral segments.
**Edge cases:**
- Collinear rays → line (2 control points)
- 90-degree left turn → segment-arc-segment
- U-turn → Bezier fallback
- Degenerate same-point handling
**Implementation:** `src/engine/lanemaker/ui/road_drawing.h` (`RoadCreationSession`).
**Test coverage:** LaneMaker Curve Fitting tests (lines 8465-8663 of `geometry_segment_tests.cpp`).
**Risks:** `[UNKNOWN]` — exact fitting algorithm details not fully traced.

### 13.3 Coordinate Conversion

**Algorithm:** WGS84 lat/lon to local meters.
**Purpose:** Convert geographic coordinates to planar local coordinates for road geometry.
**Inputs:** lat, lon, reference origin.
**Outputs:** x, y in local meters.
**Two modes:**
1. **Equirectangular** (small areas, `CoordinateConverter::equirectangularProject`):
   - X = (lon - refLon) * 111320 * cos(refLat)
   - Y = (lat - refLat) * 110540
   - UTM zone: `floor((lon + 180) / 6) + 1`
2. **UTM** (full WGS84 ellipsoid formulas, `CoordinateConverter::utmProject`):
   - Forward and inverse projections with full ellipsoid math
**Implementation:** `src/core/osm/CoordinateConverter.hpp` (lines 103-227).
**Test coverage:** OSM pipeline tests include coordinate conversion tests.
**Risks:** Equirectangular approximation degrades at high latitudes and large areas.

### 13.4 Lane Generation

**Algorithm:** OSM lane tags to LaneSection.
**Purpose:** Derive lane configuration from OSM tags.
**Inputs:** `OsmData::Way` (with `lanes`, `lanes:forward`, `lanes:backward`, `width`, `cycleway`, `sidewalk`, `turn:lanes` tags) + `RoadClassInfo`.
**Outputs:** `LaneSection` with lane widths and types.
**Priority:**
1. Explicit `lanes` tag
2. `lanes:forward` + `lanes:backward`
3. Oneway roads use `lanes` directly
4. Fallback to class default
**Width:** Explicit `width` tag or class default; clamped to valid range.
**Additional:** Cycleway/sidewalk/shoulder/median processing; turn-lane parsing from `turn:lanes` tag.
**Implementation:** `src/core/osm/LaneGenerator.hpp` (lines 50-352).
**Test coverage:** OSM pipeline tests cover basic two-way, one-way, forward/backward split, bus lanes, cycleways, sidewalk/shoulder/median.

### 13.5 Junction Detection

**Algorithm:** Topology-based junction detection and classification.
**Purpose:** Identify and classify junctions from road network topology.
**Inputs:** RoadV2 roads + topology nodes.
**Outputs:** Junction list with type (T, X, Y, multi-way, roundabout, overpass, staggered).
**Classification:**
- 2 roads → overpass or staggered
- 3 roads → T or Y junction (based on angle)
- 4 roads → X intersection
- 5+ roads → multi-way
**Roundabout detection:** Checks for `junction=roundabout` OSM tag.
**Angular difference:** Normalized to [0, 180] degrees.
**Nearby merge:** Distance-based clustering (default 5m tolerance).
**Implementation:** `src/core/osm/JunctionDetector.hpp` (lines 76-489).
**Test coverage:** OSM pipeline tests.

### 13.6 Roundabout Generation

**Algorithm:** Least-squares circle fitting + ring road creation.
**Purpose:** Generate roundabout geometry from OSM roundabout ways.
**Inputs:** OSM ways with `junction=roundabout` tag.
**Outputs:** Circular RoadV2 with lanes.
**Steps:**
1. Find roundabout ways
2. Collect connected ways into rings
3. Fit circle using least-squares
4. Detect direction (clockwise vs counter-clockwise)
5. Create ring road with lane sections
**Implementation:** `src/core/osm/RoundaboutGenerator.hpp` (lines 61-489).
**Test coverage:** OSM pipeline tests include roundabout tests.

### 13.7 Road Marking Generation

**Algorithm:** Parametric road marking placement.
**Purpose:** Generate center lines, lane dividers, edge lines, crosswalks, turn arrows.
**Inputs:** RoadV2 roads + junctions.
**Outputs:** Marking objects with points, style, width, color.
**Types:** CENTER_LINE, LANE_DIVIDER, EDGE_LINE, STOP_LINE, YIELD_LINE, CROSSWALK, TURN_ARROW, ROUNDABOUT_ARROW, CHEVRON, BIKE_SYMBOL, BUS_SYMBOL, PARKING_MARKING.
**Implementation:** `src/core/osm/RoadMarkingGenerator.hpp` (lines 97-538).
**Test coverage:** OSM pipeline tests include marking generation tests.

### 13.8 Douglas-Peucker Simplification

**Algorithm:** Perpendicular-distance Douglas-Peucker.
**Purpose:** Simplify OSM way geometry to reduce point count.
**Inputs:** List of (x, y) points + tolerance (default 1.0m).
**Outputs:** Simplified point list.
**Implementation:** `src/core/osm/RoadNetworkBuilder.hpp` (lines 410-462).
**Complexity:** O(n²) worst case, O(n log n) average.

### 13.9 Raster Reprojection

**Algorithm:** Pixel-by-pixel coordinate transform with bilinear sampling.
**Purpose:** Reproject raster between CRS (e.g., WGS84 → UTM).
**Inputs:** Source `RasterGrid` + target CRS + dimensions.
**Outputs:** Reprojected `RasterGrid`.
**Implementation:** `src/core/terrain/GISProcessor.hpp` (lines 122-160).

### 13.10 Polygon/Line Rasterization

**Algorithm:** Scanline polygon fill / Bresenham line with thickness.
**Purpose:** Rasterize vector data (water polygons, road lines) into masks.
**Inputs:** Polygon/line vertices + raster dimensions.
**Outputs:** `ByteRaster` mask.
**Implementation:** `src/core/terrain/GISProcessor.hpp` (lines 248-358).

### 13.11 Terrain Analysis (Slope, Aspect, Hillshade)

**Algorithm:** Horn's formula (slope/aspect), Zevenbergen-Thorne (curvature), Lambertian (hillshade).
**Purpose:** Compute terrain-derived rasters from DEM.
**Inputs:** `RasterGrid` DEM.
**Outputs:** `RasterGrid` (slope degrees, aspect, curvature, hillshade) or `ByteRaster` (classification).
**Implementation:** `src/core/terrain/TerrainAnalyzer.hpp` (lines 22-450).
**Constants:** Hillshade defaults: azimuth=315°, altitude=45°.

### 13.12 Distance Transform (Chamfer 3-4)

**Algorithm:** Two-pass chamfer distance transform.
**Purpose:** Compute distance field from binary mask.
**Inputs:** `ByteRaster` mask.
**Outputs:** `RasterGrid` distance field.
**Implementation:** `src/core/terrain/TerrainAnalyzer.hpp` (lines 396-447).

### 13.13 PCG Graph Evaluation (Kahn Topological Sort)

**Algorithm:** Kahn's algorithm for topological sorting + node evaluation.
**Purpose:** Evaluate procedural content generation graph.
**Inputs:** `PCGGraph` + `PCGContext` (RNG, callbacks).
**Outputs:** `QVariant` (typically point list).
**Steps:**
1. Topological sort (Kahn's algorithm)
2. Evaluate nodes in order
3. Apply filters (slope, mask, random)
4. Apply transforms (random rotation/scale/offset, surface alignment)
**Implementation:** `src/core/world/PCGEngine.hpp` (lines 47-489).
**Cycle detection:** `PCGGraph::hasCycles()` uses DFS.

### 13.14 Catmull-Rom Spline Interpolation

**Algorithm:** Catmull-Rom interpolation.
**Purpose:** Smooth curve through control points for splines.
**Inputs:** `Spline` with control points.
**Outputs:** Sampled 3D points along spline.
**Implementation:** `src/core/world/Spline.hpp` (`SplineEvaluator`, lines 90-168).
**Road mesh:** Samples at 1-meter spacing, computes tangents, generates strip mesh.

### 13.15 Action Replay (Deterministic Recording)

**Algorithm:** Action recording with buffering + deterministic replay.
**Purpose:** Record all user actions for testing, crash recovery, and verification.
**Inputs:** Mouse/keyboard/mode/profile/viewport events.
**Outputs:** Binary history file (cereal serialization).
**Buffering:** Mouse moves and viewport changes are buffered to reduce history size; flushed before key events.
**Implementation:** `src/engine/lanemaker/ui/action_manager.cpp/.h`.

### 13.16 Tile Download (Slippy Map)

**Algorithm:** Slippy-map tile calculation + HTTP download.
**Purpose:** Download satellite imagery tiles for map background.
**Inputs:** Lat/lon center + zoom level.
**Outputs:** Composite map texture.
**Tile cap:** 30 tiles maximum for performance.
**Implementation:** `src/engine/lanemaker/engine/map_view_gl.cpp` (`UpdateMapTiles`, `requestTile`), uses `TileMatrixSet` for coordinate conversion.

### 13.17 GeoTIFF Writing

**Algorithm:** libtiff with custom Unicode-safe I/O callbacks + GeoKey directory.
**Purpose:** Write georeferenced GeoTIFF files.
**Inputs:** Raster data + CRS + geo transform.
**Outputs:** GeoTIFF file with ModelPixelScale, ModelTiepoint, GeoKeyDirectory tags.
**CRS support:** WGS84 (EPSG:4326), Web Mercator (EPSG:3857), UTM (EPSG:326xx/327xx).
**Unicode paths:** Uses `PathHelper::toTiffPath()` to convert to Windows 8.3 short paths because libtiff's `TIFFOpen` uses `fopen()` which cannot handle Unicode.
**Implementation:** `src/ui/terrain/RasterWriter.cpp` (lines 64-176).

---

## 14. Plugin System

### 14.1 Plugin Interface

**File:** `src/plugin/PluginApi.hpp`

**IID:** `opengeostudio.plugin/1.0` (line 138: `Q_DECLARE_INTERFACE(IPlugin, "opengeostudio.plugin/1.0")`)

**IPlugin interface** (lines 100-135):
```cpp
class IPlugin : public QObject {
public:
    virtual ~IPlugin() = default;
    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const { return {}; }
    virtual QString author() const { return {}; }
    virtual QList<PluginCapability> capabilities() const = 0;
    virtual void init(ApplicationContext* ctx) { (void)ctx; }
    virtual void dispose() {}
};
```

**PluginCapability** (lines 41-84):
- Types: Importer, Exporter, RoadGenerator, TerrainProcessor, Validator, VisualizationLayer, Tool
- Each has `type`, `name`, `format` fields
- String conversion methods for serialization

### 14.2 PluginManager

**File:** `src/plugin/PluginApi.hpp` (lines 149-228)

**Methods:**
- `registerPlugin(IPlugin*)` — takes ownership (parent-child)
- `unregisterPlugin(id)` — removes but does not delete
- `allPlugins()`, `pluginsByCapability(type)`, `pluginsByCapability(type, nameOrFormat)`, `pluginById(id)`, `count()`

**Signals:** `pluginRegistered(id)`, `pluginUnregistered(id)`

### 14.3 PluginLoader

**File:** `src/plugin/PluginApi.hpp` (lines 240+)

**Methods:**
- `discoverPlugins(pluginsDir)` — scans directory for shared libraries with matching `Q_PLUGIN_METADATA`

### 14.4 Runtime Discovery Status

**`[CONFIRMED]` NOT WIRED:** `ApplicationContext` (lines 5-10 of `ApplicationContext.cpp`) creates `EventBus`, `ProjectManager`, `WorkspaceManager`, `TerrainStore` — but **NOT** `PluginManager` or `PluginLoader`. No plugins are shipped. The plugin system is a Phase 2a/3 placeholder.

**To create a plugin** (per header comments):
```cpp
class MyPlugin : public QObject, public IPlugin {
    Q_OBJECT
    Q_INTERFACES(IPlugin)
    Q_PLUGIN_METADATA(IID "opengeostudio.plugin/1.0" FILE "myplugin.json")
    // ... implement id(), name(), version(), capabilities()
};
```

### 14.5 Examples/Tests

No plugin examples or plugin-specific tests were found `[CONFIRMED]`.

---

## 15. API/External Integrations

### 15.1 MapLibre Native Qt

**Purpose:** Map viewport for Terrain Studio.
**Integration:** `src/app/MapViewportWidget.cpp` wraps `QMapLibre::MapWidget`.
**Style serving:** `StyleHttpServer` serves inline Esri World Imagery style JSON via local HTTP server (127.0.0.1:random port) because MapLibre cannot load `file://` URLs.
**Default center:** Pune, India (lat=18.52, lon=73.85) at zoom 15.
**Conditional:** `HAVE_MAPLIBRE` define, optional `find_package(QMapLibre)`.
**Offline behavior:** No offline tile cache; requires internet for satellite imagery.
**Security:** Local HTTP server binds to 127.0.0.1 only; CORS headers included.

### 15.2 Qt Network

**Purpose:** HTTP downloads for DEM, imagery, OSM tiles, geocoding.
**Usage:** `QNetworkAccessManager` in `ExportEngine`, `DownloadManager`, `SearchBar`, `MapViewGL`.
**Retry:** `DownloadManager` implements exponential backoff retry.
**Timeout:** ExportEngine uses timeout-based synchronous event-loop downloads.

### 15.3 OSM Input

**Format:** OSM XML (.osm) or binary (.osm.pbf) `[UNKNOWN — .pbf support not verified in parser]`.
**Parser:** `OsmXmlParser` uses `QXmlStreamReader` (no external deps).
**Coordinate system:** WGS84 lat/lon, projected to local meters or UTM.
**Authentication:** None (OSM data is public).
**Rate limiting:** Not applicable for file import; Overpass API not used directly.

### 15.4 DEM/GIS Data Sources

**DEM sources** (from `TerrainTypes.hpp` `DemSource` enum):
- AWS Terrarium (tiled, raster PNG)
- Mapzen (tiled) `[UNKNOWN — availability status]`
- Mapbox (tiled, requires API key)
- Copernicus GLO-30 (1°x1° COG cells)
- OpenTopo (area provider, requires API key)
- GPXZ (area provider, requires API key)
- GLAD (area provider)
- Local file

**Imagery sources:**
- Google (satellite tiles)
- ArcGIS (satellite tiles)
- Mapbox (requires token)
- MapTiler (requires API key)
- GLAD
- Local file

**Authentication:** API keys for OpenTopo, Mapbox, MapTiler, GPXZ, Stadia. Stored in `ExportSettings` and entered via `ExportPanel` or `SettingsDialog`.

### 15.5 OpenDRIVE

**Format:** OpenDRIVE XML (.xodr).
**Parser:** libOpenDRIVE (embedded in `src/engine/lanemaker/libOpenDRIVE/`).
**Export:** `OsmExporter::exportToOpenDrive()` generates .xodr from RoadV2 roads.
**LaneMaker I/O:** `MainWindow::saveToPath()` / `loadFromPath()` use libOpenDRIVE.
**Elevation:** `DemElevationSampler` samples DEM for road elevation profiles during export.

### 15.6 GeoJSON

**Format:** GeoJSON FeatureCollection.
**Export:** `OsmExporter::exportToGeoJson()` generates .geojson from RoadV2 roads.
**Coordinate system:** WGS84 lat/lon (inverse projection from local meters).

### 15.7 OGRE-Next

**Purpose:** 3D rendering for 3D Studio.
**Integration:** `OgreWidget` (QWindow) creates `Ogre::Root`, selects Direct3D11 render system, creates render window with external window handle.
**Assets:** Plugins.cfg, compositor scripts (PSSM shadows), HLMS media (PBS, Unlit).
**Conditional:** `HAVE_OGRE` define, optional OGRE-Next at `D:/git/ogre-next`.

### 15.8 Geocoding (Nominatim)

**Purpose:** Location search in Terrain Studio.
**API:** OpenStreetMap Nominatim geocoding API.
**Rate limiting:** 300ms debounce + 1000ms rate limiting.
**Max results:** 5.
**Implementation:** `src/ui/terrain/SearchBar.hpp`.

### 15.9 External Tile/Imagery Services

**Esri World Imagery:** Used by MapLibre style (inline JSON in `MapViewportWidget.cpp`).
**Google Maps satellite:** Used by LaneMaker map background (`MapViewGL::requestTile`).

### 15.10 Unverified Behavior

- `.osm.pbf` binary format support `[UNKNOWN]`
- Mapzen DEM availability `[UNKNOWN]`
- Exact retry policy for all providers `[UNKNOWN — only DownloadManager retry traced]`
- Rate limiting compliance for Google/Esri tiles `[UNKNOWN]`

---

## 16. Database

**`[CONFIRMED]` No database exists.**

After inspection of the entire repository, no SQL files, migrations, schema files, ORM configuration, or embedded databases were found.

**Persistence is file-based:**
- `.ogproj` — Project metadata (JSON, schema: `docs/ogproj-schema.json`)
- `.xodr` — Road network (OpenDRIVE XML)
- `.xodr.json` — Road annotations sidecar (JSON)
- `.ogosm` — OSM project (JSON)
- `world.json` — World scene (JSON)
- `.tif` / `.png` — Terrain rasters (GeoTIFF/PNG)
- `manifest.json` — Terrain export manifest
- `recent-projects.json` — Recent projects list (AppDataLocation)
- `action_rec__{timestamp}.dat` — Action replay history (cereal binary)

**No database definitions found after inspection.**

---

## 17. File/Asset Pipelines

### 17.1 Qt Resources (.qrc)

| Resource File | Contents | Compiled Into |
|---------------|----------|---------------|
| `resources/app_icons.qrc` | Application icons (SVG/PNG) | OpenGeoStudio executable |
| `resources/road_studio/road_studio.qrc` | Road Studio SVG icons (add, cancel, confirm, etc.) | OpenGeoStudio executable |
| `src/engine/lanemaker/images.qrc` | LaneMaker UI images | LaneMaker static lib → executable |
| `src/engine/lanemaker/shaders.qrc` | LaneMaker GLSL shaders | LaneMaker static lib → executable |

**Initialization:** `main.cpp` lines 867-868 call `Q_INIT_RESOURCE` for LaneMaker resources (required for static library).

### 17.2 SVG/PNG/Icons

**Location:** `resources/road_studio/svg/*.svg`, `resources/app_icons.qrc` referenced icons.
**Purpose:** Tool palette icons, toolbar icons, application icon.
**Consumer:** `MainWidget` tool palette, `AppMainWindow` toolbar.

### 17.3 Shaders

**Location:** LaneMaker `shaders.qrc`.
**Purpose:** OpenGL shaders for road rendering, map background, vehicle instancing.
**Consumer:** `MapViewGL::initializeGL()`.

### 17.4 Compositor Scripts

**Location:** `resources/compositor/OpenGeoStudio.compositor`.
**Purpose:** PSSM (Parallel-Split Shadow Mapping) shadow node for OGRE-Next.
**Consumer:** `OgreWidget` compositor workspace setup.

### 17.5 OGRE Assets

**Location:** OGRE-Next install (`D:/git/ogre-next`), copied to deploy directory at build time.
**Contents:** `Hlms/` (shader media), `ogre/` (plugins.cfg compatibility), `assets/` (rail material, 3D assets), `compositor/` (compositor scripts).
**Consumer:** `OgreWidget` HLMS loading, plugin loading.

### 17.6 DEM/GeoTIFF

**Source:** Downloaded from DEM providers or local file import.
**Processing:** `DemDecoder::decodeAuto()` → `GISProcessor::resample()` → `RasterWriter::writeFloat32GeoTiff()`.
**Consumer:** `OgreWidget::loadTerrain()`, `DemElevationSampler::loadFromGeoTiff()`.

### 17.7 OpenDRIVE (.xodr)

**Source:** LaneMaker save or OSM export.
**Processing:** libOpenDRIVE parsing.
**Consumer:** LaneMaker `MainWindow::loadFromPath()`, `OgreWidget::loadRoads()`.

### 17.8 GeoJSON (.geojson)

**Source:** OSM export.
**Consumer:** External GIS tools.

### 17.9 .ogproj

**Source:** `ProjectManager::save()`.
**Format:** JSON, schema at `docs/ogproj-schema.json`.
**Consumer:** `ProjectManager::open()`.

### 17.10 .ogosm

**Source:** `OsmProjectSerializer::saveToFile()`.
**Format:** JSON (OSM data + roads + junctions + converter).
**Consumer:** `OsmProjectSerializer::loadFromFile()`.

### 17.11 Project Folders

**Created by:** `ProjectManager::createWithFolder()` (15 subfolders):
Terrain, GIS, Roads, Railway, Scene, Simulation, Infrastructure, Assets, Environment, Validation, Exports, Cache, Temp, Logs, Config.

### 17.12 Generated Build/Deployment Files

**Build:** `build/` directory (CMake + Ninja output).
**Deploy:** `build/deploy/` contains:
- `OpenGeoStudio.exe`
- Qt DLLs (via windeployqt)
- OGRE-Next DLLs + media
- QMapLibre DLLs (if enabled)
- `log.txt` (runtime log)

### 17.13 Asset Pipeline Trace

```
Source asset (SVG/PNG/shader/compositor)
  ↓ Qt Resource Compiler (rcc) / OGRE media copy
Compiled into executable / deploy folder
  ↓ Runtime loading
Qt stylesheet / OpenGL shader / OGRE material
  ↓ Rendering
Screen
```

---

## 18. Configuration

### 18.1 CMakeLists.txt

**Location:** `D:/git/OpenGeoStudio-Qt/CMakeLists.txt`
**C++ standard:** C++20 (line 32)
**CMake minimum:** 3.16
**Build type:** Release (default)
**Generator:** Ninja

**Key options:**
- `BUILD_TESTS` (default ON) — enables all test targets
- `HAVE_MAPLIBRE=1` — conditional MapLibre support (if `QMapLibre` found)
- `HAVE_OGRE=1` — conditional OGRE-Next support (if found)

**Compile definitions:**
- `NOMINMAX` — prevents Windows min/max macro conflicts
- `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO` — LaneMaker logging level
- `G_TEST` — Road Studio test mode
- `/EHa` — MSVC structured exception handling (for LaneMaker SEH guards)
- `/bigobj` — increased object file size limit (for large test files)

### 18.2 vcpkg.json

**Location:** `D:/git/OpenGeoStudio-Qt/vcpkg.json`
**Mode:** Manifest mode
**Dependencies:** curl, tiff, libpng, nlohmann-json, spdlog, boost-optional, cgal, gtest, cereal
**Builtin baseline:** `2f1d605400c8727cc00c15797aba796c88ccd523`

**Note:** `gtest` is listed but not used by any test `[CONFIRMED]`.

### 18.3 .gitignore

**Location:** `D:/git/OpenGeoStudio-Qt/.gitignore`
**Patterns:** Build output, temporary files, IDE files, commit message temp files.

### 18.4 CI Workflow

**Location:** `.github/workflows/ci.yml`
**Triggers:** Push/PR to main/master
**Matrix:** Windows (msvc) + Ubuntu (gcc), Qt 6.8.0
**Steps:** Checkout → CMake → Ninja → Install Qt 6 → Configure → Build → ctest
**Configure command:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON`

### 18.5 Qt Resource Files

See Section 17.1.

### 18.6 Runtime Preferences

**UserPreference** (`src/engine/lanemaker/util/preference.h`):
- Global: `extern UserPreference g_preference`
- Includes welcome dialog, verification preferences
- Set by `PreferenceWindow` in LaneMaker

**SettingsDialog** (`main.cpp` lines 68-133):
- API keys: OpenTopography, Mapbox, MapTiler
- Project defaults: default workspace, road width, lane count

### 18.7 Project Schema

**Location:** `docs/ogproj-schema.json`
**Format:** JSON schema for .ogproj files.

### 18.8 Environment Variables

**Required for build:** MSVC environment (via vcvars64.bat)
**Runtime:** `QT_QPA_PLATFORM=offscreen` (for UI tests)

### 18.9 Toolchain Settings

**vcpkg toolchain:** `C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake` (per AGENTS.md)
**Qt prefix path:** `C:/Qt/6.8.0/msvc2022_64` (per AGENTS.md)

---

## 19. Error Handling

### 19.1 Return-Value/Result Structs

**Pattern:** Core pipelines return `Result` structs with `success` flag + `errorMessage`:
- `OsmImportPipeline::Result` — `success`, `errorMessage`, `roads`, `junctions`, `issues`, `stats`
- `RailImportPipeline::Result` — similar
- `RoadNetworkBuilder::Result` — `success`, `errorMessage`, `roads`, `topologyNodes`
- `LaneGenerator::Result` — `success`, `errorMessage`, `laneSections`

### 19.2 QMessageBox

**Usage:** UI reports failures via `QMessageBox::critical` on import dialogs.
**Examples:** `OsmImportDialog` on import failure, `AppMainWindow` on project open/save failure.

### 19.3 Logging

**Application logging:** `appLog().warn/error(...)` from `src/core/logger/Logger.hpp`.
**LaneMaker logging:** spdlog (internal, "leave untouched" per AGENTS.md).

### 19.4 Exceptions

**Core pipelines:** Do NOT throw across module boundaries; return `success=false` + `errorMessage` (per AGENTS.md).
**LaneMaker:** Uses `/EHa` for structured exception handling; `RUN_ROAD_TEST` macro wraps road tests in try-catch for SEH.
**Road destruction:** May raise SEH exceptions in test mode (handled by try-catch).

### 19.5 Assertions

**doctest:** `CHECK` and `REQUIRE` macros in geometry tests.
**Custom tests:** `CHECK` and `VERIFY` macros with pass/fail counters.

### 19.6 Validation Errors

**Road validator:** Returns `Issue` structs with `Severity` (Critical, High, Medium, Low, Info), `category`, `message`, `roadId`, `suggestedFix`.
**World validator:** Returns `ValidationError` structs with `severity`, `category`, `message`, `actorId`, `suggestedFix`.
**Terrain validator:** `ValidationManager` returns `TestResult` list with pass/fail/skip status.

### 19.7 Fallback Behavior

**DEM decode:** `DemDecoder::decodeAuto()` auto-detects format (PNG, TIFF, AAIGrid).
**NoData fill:** `GISProcessor::fillNoData()` fills with mean of valid neighbors.
**Coarse resolution warning:** ExportEngine warns when source resolution is too coarse.

### 19.8 File-Open Failures

**Project open:** `ProjectManager::open()` returns `std::optional<Project>` (empty on failure).
**Road load:** `MainWindow::loadFromPath()` defers if GL not initialized.
**Unicode paths:** LaneMaker copies to temp file before loading (ASCII-safe).

### 19.9 Network Failures

**DownloadManager:** Exponential backoff retry.
**ExportEngine:** Timeout-based synchronous event-loop downloads.
**GPXZ test:** Treats unavailable mountainous data as skip/success.

### 19.10 Parser Failures

**OsmXmlParser:** Captures parser errors with line/column via `QXmlStreamReader::error()`.

### 19.11 Plugin Failures

**PluginLoader:** Metadata validation, version compatibility check (defined but not wired).

### 19.12 Missing/Error-Prone Behavior

- `TerrainWorldBridge::sampleHeight()` is a stub returning 0 `[CONFIRMED]` — world-side height sampling is not functional.
- No global error handler for uncaught exceptions `[UNKNOWN]`.
- OpenGL context loss handling `[UNKNOWN]`.

---

## 20. Logging

### 20.1 Centralized Logger

**File:** `src/core/logger/Logger.hpp`
**Class:** `Logger` (lines 27-104)
**Scope-based:** Each `Logger` instance has a scope string.
**Levels:** Debug, Info, Warn, Error (enum, line 29).
**Methods:** `debug()`, `info()`, `warn()`, `error()` — variadic templates accepting QString, numbers, const char*.
**Child loggers:** `child(subscope)` creates nested scope.
**File transport:** `addFileTransport(path)` enables append-mode file output (static members `s_fileTransport`, `s_fileStream`).

### 20.2 Global Accessor

**Function:** `appLog()` (lines 113-116) — returns static `Logger` instance with scope "app".
**Usage:** `appLog().info("Started OpenGeoStudio...")` in `main.cpp`.

### 20.3 LaneMaker spdlog

**Usage:** LaneMaker uses its own spdlog (via vcpkg).
**Level:** `SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO` (CMake define).
**Convention:** "Leave untouched to preserve embedded engine behavior" (AGENTS.md).

### 20.4 Test Logging

**Convention:** Test executables may use `qDebug()` directly (AGENTS.md).

### 20.5 Conventions (from AGENTS.md)

- Use `appLog()` for ALL application logging.
- Do NOT add new `qDebug()`/`qWarning()` calls.
- `Logger::addFileTransport(path)` enables optional file output.
- LaneMaker spdlog remains untouched.

### 20.6 Destinations

- **Console:** Qt's `qDebug()` (used by Logger internally).
- **File:** `log.txt` next to executable (enabled in `main.cpp` lines 874-878).

### 20.7 Formatting

**Format:** `[timestamp] [level] [scope] message`
**Timestamp:** `QDateTime::currentDateTime()`.

### 20.8 Unsafe/Inconsistent Logging

- LaneMaker spdlog and application Logger are separate systems — no unified log stream `[CONFIRMED]`.
- No log rotation `[CONFIRMED]`.
- No runtime log level configuration `[CONFIRMED]`.

---

## 21. Concurrency

### 21.1 Synchronous Operations

Most of the application is synchronous on the Qt main thread:
- OSM import pipeline (blocking)
- Road drawing/rendering
- World model operations
- Project save/load (except deferred road load)

### 21.2 Asynchronous Operations

**Network downloads:** `QNetworkAccessManager` is asynchronous; `DownloadManager` uses `QEventLoop` for synchronous wait.
**MapLibre rendering:** `QMapLibre::Map::mapChanged` deferred via `QTimer` (MapViewportWidget line 139-140).
**Tile downloads:** `MapViewGL::UpdateMapTiles()` downloads tiles asynchronously.
**Autosave:** `QTimer::timeout` → `onAutosave` (60s interval).
**Deferred road load:** `QTimer` 2s delay after workspace switch (main.cpp line 748-765).

### 21.3 Thread Affinity

**Main thread:** All UI, OpenGL rendering, project management, event processing.
**Network threads:** Qt Network internal threads (managed by `QNetworkAccessManager`).
**No explicit QThread usage found** `[CONFIRMED]` — no `QThread`, `QtConcurrent`, or worker threads were identified in the codebase.

### 21.4 Synchronization

**QEventLoop:** Used by `DownloadManager` for synchronous download wait.
**Signals/slots:** Qt's queued connections for cross-thread communication (implicit).

### 21.5 OpenGL Context/Thread Ownership

**MapViewGL:** `QOpenGLWidget` — context created on main thread, all GL calls on main thread.
**OgreWidget:** `QWindow` — OGRE rendering on main thread.
**Critical:** LaneMaker OpenGL context is not ready until `showEvent()` fires, which is why road loading is deferred.

### 21.6 Shared State / Races

**Global pointers:** `g_mainWindow`, `g_laneConfig`, `g_mapViewGL` are rebound on `showEvent()`. If both Road Studio and Train Studio are visible (not currently possible with QStackedWidget), behavior would be undefined `[CONFIRMED risk]`.
**World singleton:** `World::Instance()` in LaneMaker is a process-wide singleton shared between Road Studio and Train Studio `[CONFIRMED risk]`.
**ActionManager singleton:** Process-wide, shared between workspaces `[CONFIRMED]`.

### 21.7 Reentrancy Hazards

**LaneConfigWidget:** `SetOption()` calls `OnOptionChange()` which calls `CheckModified()` which may record actions. During profile loading (`applyingProfile=true`), this could cause unintended action recording `[INFERENCE]`.
**MainWindow::showEvent():** Rebinds globals — if called re-entrantly, could leave globals in inconsistent state `[INFERENCE]`.

### 21.8 UI-Thread Blocking

**OSM import:** Blocks main thread for large files.
**Terrain export:** Uses async downloads but `DemDecoder` and `RasterWriter` are synchronous.
**Project save:** Synchronous file I/O.

---

## 22. Performance

### 22.1 Identified Bottlenecks and Risks

**OSM parsing:** `QXmlStreamReader` is efficient but large files (e.g., city-scale OSM) will block the UI thread.
**Road catalog construction:** `RoadProfileCatalog::all()` and `RailProfileCatalog::all()` return `QMap` values, meaning repeated calls may reconstruct catalogs `[CONFIRMED]` — potential performance issue but not measured.
**Mesh generation:** `Road::GenerateAllSectionGraphics()` divides roads into 10m segments (`GraphicsDivision = 10`); large networks generate many graphics objects.
**OpenGL rendering:** `MapViewGL::paintGL()` renders all geometry every frame; no frustum culling identified `[INFERENCE]`.
**Terrain raster processing:** `DemDecoder` and `GISProcessor::resample()` are CPU-intensive and synchronous.
**MapLibre rendering:** Tile downloads and compositing.
**Large road networks:** `World::allRoads` is a `std::set`; junction operations iterate over all roads.
**Serialization:** LaneMaker save uses temp file + copy (two I/O operations).
**Synchronous UI operations:** OSM import, terrain export processing, project save/load.
**Repeated repainting:** `refreshAllCustomGraphics()` re-renders all markings/signs/furniture for all roads.
**Global scans:** `refreshObjectTree()` iterates all roads, junctions, signs, markings, furniture.
**Memory ownership:** `std::set<std::shared_ptr<Road>>` in `World` — shared pointer overhead.
**QMap copies:** OSM data structures use `QMap` (sorted, copy-on-write); large datasets may have overhead.

### 22.2 Performance Constants

- `GraphicsDivision = 10` m — road graphics segment length
- `MaxRoadVertices` — permanent buffer size
- `MaxTemporaryVertices` — temporary buffer size
- `NVehicleVariations = 3` — instanced vehicle variations
- Tile cap: 30 tiles for map background

### 22.3 No Benchmarks

No benchmark suite exists `[CONFIRMED]`. Performance tests in `geometry_segment_tests.cpp` (Phase 2.3, 2.5, 2.7, 2.8) measure relative performance but are not formal benchmarks.

### 22.4 Performance Hotspot Map

> Classified by resource type for future optimization work.

| # | Hotspot | Resource | Location | Notes |
|---|---------|----------|----------|-------|
| 1 | OSM import (large files) | CPU | `OsmXmlParser`, `RoadNetworkBuilder` | Blocks UI thread; no worker thread |
| 2 | Road geometry sampling | CPU | `lane_sampling.hpp` | Adaptive sampling; O(n * ADAPTIVE_MAX_DEPTH) per segment |
| 3 | Douglas-Peucker simplification | CPU | `RoadNetworkBuilder::simplifyGeometry` | O(n²) worst case |
| 4 | Terrain reprojection | CPU | `GISProcessor::reprojectRaster` | Pixel-by-pixel bilinear sampling |
| 5 | Polygon/line rasterization | CPU | `GISProcessor::rasterizePolygon/Line` | Scanline fill, Bresenham |
| 6 | DEM decode | CPU | `DemDecoder::decodeAuto` | TIFF/PNG/AAIGrid decoding |
| 7 | Tile downloads | Network | `ExportEngine`, `MapViewGL::requestTile` | HTTP; retry with backoff |
| 8 | MapLibre tile compositing | GPU + Network | `MapViewportWidget` | Satellite tile fetch + render |
| 9 | OpenGL rendering (no culling) | GPU | `MapViewGL::paintGL` | All geometry rendered every frame `[INFERENCE]` |
| 10 | PCG graph evaluation | CPU | `PCGEngine::evaluate` | Kahn topological sort + node eval |
| 11 | World serialization | I/O | `World::saveToFile` | JSON serialization of all actors |
| 12 | Road catalog construction | CPU + Memory | `RoadProfileCatalog::all()`, `RailProfileCatalog::all()` | Returns QMap by value; may reconstruct `[CONFIRMED]` |
| 13 | refreshAllCustomGraphics | CPU + GPU | `MainWidget::refreshAllCustomGraphics` | Re-renders all markings/signs/furniture |
| 14 | refreshObjectTree | CPU | `MainWidget::refreshObjectTree` | Iterates all roads, junctions, signs, markings, furniture |
| 15 | LaneMaker save (temp file + copy) | I/O | `MainWindow::saveToPath` | Two I/O operations for Unicode path workaround |
| 16 | RasterWriter GeoTIFF | I/O + CPU | `RasterWriter::writeFloat32GeoTiff` | libtiff with custom Unicode-safe I/O callbacks |

`[RECOMMENDATION]` Priority optimization order:
1. Move OSM import and terrain processing to worker threads (#1, #4, #6)
2. Add frustum culling to MapViewGL (#9)
3. Cache road/rail profile catalogs (#12)
4. Add formal benchmarks to track regressions

---

## 23. Testing

### 23.1 Test Frameworks

| Framework | Used By | Notes |
|-----------|---------|-------|
| doctest | `geometry_segment_tests.cpp`, `map_tests.cpp` | Header-only, CTest-registered |
| Custom `TEST()` macro | `test_osm_pipeline.cpp`, `test_houston_roundtrip.cpp` | Static registration pattern |
| Custom `VERIFY` macro | `test_world_model.cpp`, `test_world_workflow.cpp`, `test_terrain_pipeline.cpp` | Pass/fail counters |
| Custom `CHECK` macro | `test_road_studio.cpp`, `test_road_studio_ui.cpp` | Pass/fail counters |
| Standalone `main()` | `test_geotiff_writer.cpp`, `test_gpxz_download.cpp` | No formal framework |

### 23.2 Test Targets

| Target | File(s) | Framework | Test Functions/Cases | Assertions | CMake Lines |
|--------|---------|-----------|---------------------|------------|-------------|
| `geometry_segment_tests` | `geometry_segment_tests.cpp` + `map_tests.cpp` | doctest | 504 (473+31) | N/A | 521-544 |
| `test_osm_pipeline` | `test_osm_pipeline.cpp` | Custom TEST() | 35 | ~155 CHECK | 650-666 |
| `test_road_studio` | `test_road_studio.cpp` + `sign_system.cpp` + `cross_section_extender.cpp` | Custom CHECK | 39 defined, 26 invoked | 408 CHECK | 692-715 |
| `test_road_studio_ui` | `test_road_studio_ui.cpp` | Custom CHECK | ~20 checks | ~20 | 719-736 |
| `test_world_model` | `test_world_model.cpp` | Custom VERIFY | 20 | ~34 VERIFY | 608-624 |
| `test_world_workflow` | `test_world_workflow.cpp` | Custom VERIFY | 19 | ~22 VERIFY | 627-647 |
| `test_terrain_pipeline` | `test_terrain_pipeline.cpp` | Custom VERIFY | 26 | 26 VERIFY | 583-605 |
| `test_houston_roundtrip` | `test_houston_roundtrip.cpp` | Custom TEST() | 1 (data-dependent) | N/A | 669-689 |
| `test_geotiff_writer` | `test_geotiff_writer.cpp` + `RasterWriter.cpp` | Standalone | 1 | N/A | 565-580 |
| `test_gpxz_download` | `test_gpxz_download.cpp` | Standalone | 7 groups | N/A | 547-562 |

### 23.3 Test Discovery

**CTest:** `geometry_segment_tests` is registered with CTest (`ci.yml` runs `ctest --output-on-failure`).
**Other tests:** Not registered with CTest `[CONFIRMED]` — must be run manually.

### 23.4 Test Coverage

**Geometry:** Comprehensive — 473 doctest cases covering all segment types, lane engine, sampling, network, marks, mesh, pipeline, and LaneMaker curve fitting.
**OSM pipeline:** 35 functions covering XML parsing, coordinate conversion, classification, network construction, junction detection, validation, serialization, export, lane generation, roundabouts, markings, signs, end-to-end.
**Road Studio:** 39 functions covering sign/marking/furniture registries, road templates, persistence, snapping, measurement, road model, lanes, cross-section, junctions, roundabouts, merge/split/reverse.
**World model:** 20 functions covering actors, serialization, hierarchy, selection, layers, save/load, validation, splines, PCG, undo/redo.
**World workflow:** 19 functions covering full world-authoring workflow.
**Terrain pipeline:** 26 functions covering CRS, DEM, imagery, masks, tiles, validation, cache, reproducibility.

### 23.5 Coverage Gaps

- **Plugin system:** No tests `[CONFIRMED]`.
- **MapLibre integration:** No tests `[CONFIRMED]`.
- **OGRE-Next integration:** No tests (only Houston round-trip exercises WorldBuilder) `[CONFIRMED]`.
- **UI interactions:** Only `test_road_studio_ui` (offscreen smoke test) `[CONFIRMED]`.
- **Concurrency:** No concurrency tests `[CONFIRMED]`.
- **Performance:** No formal benchmarks `[CONFIRMED]`.
- **Error paths:** Limited error-path testing `[INFERENCE]`.

### 23.6 Historical Pass/Fail Results

**From AGENTS.md (historical, NOT revalidated):**
- `test_world_model`: 34/34 pass
- `test_world_workflow`: 22/22 pass
- `test_osm_pipeline`: 155/155 pass
- `test_road_studio`: 290+ pass
- `geometry_segment_tests`: 261 tests

**Discrepancies `[CONFIRMED]`:**
- AGENTS.md says "261 tests" for geometry, but actual source has 504 doctest cases (473 + 31). The "261" likely refers to an older version or a subset count.
- AGENTS.md says "290+ pass" for road studio, but actual source has 408 CHECK assertions across 39 functions (26 invoked). The "290+" likely refers to an older count.
- The "34/34", "22/22", "155/155" values refer to assertion counts, not test-function counts.

**These counts must be treated as historical evidence and revalidated by running the tests.**

### 23.7 Tests Not Built or Not Registered

- `test_road_studio_ui` is under `if(BUILD_TESTING)` (not `if(BUILD_TESTS)`) — potential CMake condition mismatch `[CONFIRMED]`.
- Most test targets are not registered with CTest (only `geometry_segment_tests` is).
- `test_gpxz_download` requires network and a hard-coded API key — may skip/fail in CI.

---

## 24. Build System

### 24.1 Configure Command

```cmd
cmake -B D:\git\OpenGeoStudio-Qt\build -S D:\git\OpenGeoStudio-Qt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64 -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6
```

### 24.2 Build Command

```cmd
cmd /c "call ""C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>&1 && cmake --build D:\git\OpenGeoStudio-Qt\build --target OpenGeoStudio 2>&1"
```

**Critical:** Must use VS 2022 **BuildTools** vcvars64.bat, NOT VS 2022 Community (causes `STL1001: Unexpected compiler version`).

### 24.3 Generator

Ninja (Release configuration).

### 24.4 CMake Options

| Option | Default | Purpose |
|--------|---------|---------|
| `BUILD_TESTS` | ON | Enable all test targets |
| `CMAKE_TOOLCHAIN_FILE` | (must set) | vcpkg toolchain |
| `CMAKE_PREFIX_PATH` | (must set) | Qt install path |
| `Qt6_DIR` | (must set) | Qt CMake config dir |

### 24.5 AUTOMOC/AUTOUIC/AUTORCC

**`[CONFIRMED]` AUTOMOC enabled** — required for Q_OBJECT headers. AGENTS.md notes: "Q_OBJECT headers included from .cpp files need a corresponding .cpp entry in CMakeLists.txt for AUTOMOC to process them."

### 24.6 Generated Files

- MOC files (from AUTOMOC)
- Qt resource files (from RCC)
- Object files (Ninja)
- Deploy directory (POST_BUILD)

### 24.7 Build Targets

See Part 1, Section 3.1 and the subagent report for the full target list.

### 24.8 OGRE-Next Path

**Hardcoded:** `D:/git/ogre-next` (CMakeLists.txt line 388) — not portable `[CONFIRMED]`.

### 24.9 MapLibre Path

**Hint:** `D:/git/maplibre-native-qt/install` (CMakeLists.txt) — not portable `[CONFIRMED]`.

---

## 25. Deployment

### 25.1 windeployqt

**Location:** CMakeLists.txt lines 466-513
**Process:**
1. Find `windeployqt.exe` in Qt bin directory
2. Run as POST_BUILD with flags: `--release`, `--no-translations`, `--no-opengl-sw`, `--no-system-d3d-compiler`, `--compiler-runtime`
3. Copy QMapLibre DLLs (if found)

### 25.2 DLL Copying

**OGRE-Next DLLs** (CMakeLists.txt lines 419-455):
- OgreNextMain.dll, OgreNextHlmsPbs.dll, OgreNextHlmsUnlit.dll, OgreNextPlanarReflections.dll, OgreNextAtmosphere.dll, OgreNextProperty.dll
- RenderSystem_Direct3D11.dll, RenderSystem_GL3Plus.dll, RenderSystem_NULL.dll
- Plugin_ParticleFX.dll, Plugin_ParticleFX2.dll

**OGRE-Next media directories:**
- `Hlms/` — HLMS shader media
- `ogre/` — plugins.cfg compatibility
- `assets/` — rail material, 3D assets
- `compositor/` — compositor scripts

**QMapLibre DLLs:**
- QMapLibre::Core, QMapLibre::Widgets

### 25.3 Deploy Directory

**Location:** `${CMAKE_BINARY_DIR}/deploy` (CMakeLists.txt line 463)
**Contents:**
- OpenGeoStudio.exe
- Qt DLLs (via windeployqt)
- OGRE-Next DLLs + media
- QMapLibre DLLs (if enabled)
- log.txt (runtime)

### 25.4 Portable Package Scripts

**Location:** `scripts/` directory
**Reference:** `PORTABLE_README.txt` documents the portable package.

### 25.5 CI Commands

**File:** `.github/workflows/ci.yml`
**Commands:**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

**Note:** CI does NOT pass `CMAKE_TOOLCHAIN_FILE` for vcpkg `[CONFIRMED]` — CI may fail to find vcpkg dependencies (nlohmann_json, cereal, etc.) unless vcpkg is pre-installed on the runner.

### 25.6 Runtime Requirements

- Windows 10/11 (64-bit)
- Qt 6.8.0 runtime DLLs (deployed by windeployqt)
- MSVC runtime (deployed by `--compiler-runtime`)
- OGRE-Next DLLs (if 3D Studio used)
- QMapLibre DLLs (if MapLibre used)
- Internet access for DEM/imagery/tile downloads and geocoding

### 25.7 Output Folders

- `build/` — CMake build output
- `build/deploy/` — Runnable application
- `C:/OpenGeoStudio/Projects/` — Default project base directory (main.cpp line 787)

---

*End of Part 2. Continue to `docs/REVERSE_ENGINEERING_PART3_GUIDE.md` for Sections 26–34.*
