# Complete Technical Code Review — Plan

**Repository:** OpenGeoStudio-Qt (`d:\git\OpenGeoStudio-Qt`)
**Scope:** Entire codebase (~137K LOC, ~222 files across 8 modules)
**Depth:** Full deep-dive (correctness, safety, performance, security, idioms, architecture, conventions, tests, build)
**Output:** Full structured report with Mermaid diagrams + categorized issue table

---

## 1. Objectives

Perform a systematic technical code review of the entire OpenGeoStudio-Qt native C++/Qt6 codebase to identify:
- **Correctness & safety bugs:** Null dereferences, UB, memory leaks, dangling references, resource leaks, off-by-one, logic errors
- **Performance issues:** Unnecessary copies, O(n²) hot paths, missing reserve(), cache-inefficient access, blocking I/O on UI thread
- **Security & robustness:** Unvalidated inputs, path traversal, integer overflow, API key exposure, exception safety, crash-on-malformed-input
- **Qt/C++20 idiom compliance:** QStringView vs QString, QLatin1String, move semantics, rule-of-five correctness, signal/slot lifetime, AUTOMOC compliance
- **Architecture & design:** Coupling, encapsulation violations, invariants, dependency direction, service lifecycle
- **Convention compliance (per AGENTS.md):** 8 key conventions (Polynomial3 width, header-only OSM include rules, XML whitespace, etc.)
- **Test quality & coverage:** Assertion strength, edge-case coverage, determinism, test isolation, flake risks
- **Build system correctness:** CMake target hygiene, GLOB pitfalls, include paths, optional dependency handling, MSVC flags

---

## 2. Module Inventory & Priority

Review will proceed in priority order (high-risk modules first). Each module lists representative files and focus areas.

### Tier 1 — High Risk / High Impact

| Priority | Module | Representative Files | Key Focus Areas |
|----------|--------|---------------------|-----------------|
| P0 | **Road Engine (RoadV2 + GeometrySegment)** | `road_v2.hpp`, `geometry_segment.hpp`, `geometry_segment_tests.cpp` | Invariant enforcement (rebuildGeometryView), clone correctness, SegmentSequence view invalidation, polymorphic ownership, laneSectionAt linear scan, mutable legacy cache thread-safety |
| P0 | **LaneMaker XODR + libOpenDRIVE** | `xodr/road.cpp`, `xodr/junction.cpp`, `libOpenDRIVE/src/Road.cpp`, `libOpenDRIVE/src/OpenDriveMap.cpp` | Raw pointer ownership, XODR parse error recovery, mesh generation numerical stability, routing graph cycle handling, pugixml lifetime |
| P0 | **OSM Import Pipeline** | `OsmImportPipeline.hpp`, `RoadNetworkBuilder.hpp`, `JunctionDetector.hpp`, `test_osm_pipeline.cpp` | Topology correctness (dead-end, duplicate roads), OSM tag edge cases, CoordinateConverter accuracy, progress callback lifetime, memory use with large .osm files |
| P0 | **LaneMaker UI (road editing)** | `ui/main_window.cpp`, `ui/road_creation.cpp`, `ui/road_modification.cpp`, `ui/road_overlaps.cpp` | Undo/redo state integrity, graphics scene leak, OpenGL context lifetime, action manager state sync, touch controller threading |

### Tier 2 — Medium Risk

| Priority | Module | Representative Files | Key Focus Areas |
|----------|--------|---------------------|-----------------|
| P1 | **Application Shell (main.cpp)** | `main.cpp`, `MapViewportWidget.cpp` | Workspace switch lifecycle, save/load deferred load correctness (OpenGL timing), QTimer single-shot lifetime, dock widget reparenting, command palette action enablement |
| P1 | **Service Layer (ApplicationContext)** | `ApplicationContext.hpp/.cpp`, `ProjectManager.hpp/.cpp`, `WorkspaceManager.hpp/.cpp`, `EventBus.hpp/.cpp` | Service initialization order, EventBus subscriber lifetime (QObject connection scoping), Project dirty state propagation, ogproj JSON round-trip, path handling with spaces/unicode |
| P1 | **World Authoring Model** | `World.hpp`, `WorldBuilder.hpp`, `UndoRedo.hpp`, `test_world_model.cpp`, `test_world_workflow.cpp` | Actor hierarchy removal (recursive child collect correctness), Spline interpolation numerical stability, layer visibility propagation, PCG determinism |
| P1 | **Terrain Pipeline (core + UI)** | `TerrainManager.hpp`, `GISProcessor.hpp`, `DownloadManager.hpp`, `ValidationManager.hpp`, `TerrainStore.cpp`, `ExportEngine.cpp` | Tile grid boundary conditions, DEM NoData propagation, cache key collision, TIFF/PNG write error handling, export progress cancellation, CRS transform edge cases |
| P1 | **LaneMaker Engine (OpenGL + spatial index)** | `engine/gl_buffer_manage.cpp`, `engine/map_view_gl.cpp`, `engine/spatial_indexer.cpp`, `engine/ShaderProgram.cpp` | GPU resource leak (VAO/VBO/FBO), shader compile error fallback, spatial index query correctness, instanced rendering buffer alignment |

### Tier 3 — Lower Risk / Supporting

| Priority | Module | Representative Files | Key Focus Areas |
|----------|--------|---------------------|-----------------|
| P2 | **Terrain UI Widgets** | `TerrainStudioWidget.cpp`, `TerrainViewport.cpp`, `LayerStack.cpp`, `SearchBar.cpp`, `ExportPanel.cpp` | Qt signal/slot connection leaks, stylesheet reapply cost, viewport zoom stack overflow, search bar debounce |
| P2 | **Road Studio Integration** | `RoadStudioWidget.cpp`, `OsmImportDialog.cpp`, `RailOsmImportDialog.cpp`, `GeoConvert.hpp` | LaneMaker MainWindow embedding (parent QWidget lifecycle), import dialog worker thread, coordinate conversion precision |
| P2 | **3D Studio (OGRE-Next)** | `Studio3DWidget.cpp`, `OgreWidget.cpp`, `EditorPanels.cpp` | OGRE root lifecycle, Hlms media path, render window resize, panel data binding |
| P2 | **Map Subsystem** | `XyzTileProvider.hpp`, `TileCache.hpp`, `TileMatrix.hpp`, `CoordinateTransform.hpp` | Tile cache eviction, HTTP request cancellation, CRS WGS84/Web Mercator precision, map tile LOD seam |
| P2 | **Build System** | `CMakeLists.txt`, `vcpkg.json`, `.github/workflows/ci.yml`, `scripts/package.ps1` | GLOB CONFIGURE_DEPENDS omission, include path leak between targets, test discovery, OGRE DLL copy robustness, vcpkg version pins |
| P2 | **Tests (unit + integration)** | All `test_*.cpp` files, `validation.cpp`, `baseline-test-results.txt` | Doctest vs gtest confusion, flaky time-dependent tests, fixture setup/teardown, assertion message quality, disabled tests |

---

## 3. Review Categories & Checklists

For every file reviewed, apply the following structured checklist:

### 3A. Correctness & Memory Safety
- [ ] **Rule of five:** ctor / copy ctor / copy assign / move ctor / move assign / dtor — all correct or `=default`/`=delete` appropriately
- [ ] **Raw pointer / reference lifetime:** No dangling references to stack temporaries; no pointer-to-moved-object; QObject parent/child trees prevent double-free
- [ ] **`unique_ptr` / `shared_ptr` ownership:** No accidental copies; no `.release()` without matching capture; clone() depth matches intent
- [ ] **Bounds checking:** `vector[idx]` vs `.at(idx)`; loop index invariants (off-by-one at begin/end); LaneSection linear scan assumes sorted — is that enforced?
- [ ] **Numeric overflow / precision:** Lat/lon double precision loss near extremes; length calculations near zero; `(void)width` casts in synthesizeFromLegacy — intentional?
- [ ] **Exception safety:** Strong guarantee where needed; no throw in destructors; Qt signal handlers catch exceptions? (MSVC `/EHa` set but verify)
- [ ] **Qt parent-child ownership:** QObject trees correct for all dynamically allocated widgets; no manual `delete widget` where parent owns it
- [ ] **OpenGL resource ownership:** glDeleteBuffers matches glGenBuffers; VAO/VBO never rebound after delete; FBO completeness checked

### 3B. Performance
- [ ] **Unnecessary copies:** Pass-by-value vs `const&` vs `&&`; QString vs QStringView; `vector`/`QJsonObject` deep copies in hot paths
- [ ] **Missing `reserve()`:** Bulk push_back loops without reserve (OSM road network build, geometry import)
- [ ] **Algorithmic complexity:** Nested loops on large collections; laneSectionAt linear scan → binary search?; spatial index used vs brute force
- [ ] **UI thread blocking:** Long operations (OSM parse, DEM download, TIFF write) on GUI thread vs worker thread; `QProgressDialog` used?
- [ ] **Cache efficiency:** Contiguous storage (vector of objects vs vector of unique_ptr of small objects); pointer indirection cost in hot geometry loops
- [ ] **QString/QJsonObject allocation:** Repeated small allocations in parse loops; QLatin1String vs QStringView comparisons per AGENTS.md

### 3C. Security & Robustness
- [ ] **API key handling:** SettingsDialog stores keys in plaintext QSettings?; keys logged in debug output?; OpenTopography/Mapbox/MapTiler key exposure in `.ogproj`?
- [ ] **Path traversal:** Project::basePath + subpath uses `QDir::cleanPath()` / `QFileInfo` canonical? No `../` escape?
- [ ] **Malformed input resilience:** OSM XML with truncated tags, XODR with invalid geometry parameters, GeoTIFF with bad headers — all handle gracefully (no crash, no infinite loop)?
- [ ] **Integer overflow in allocations:** User-provided sizes (tile count, segment count) checked before `reserve()` / `resize()`?
- [ ] **QNetworkRequest security:** HTTPS only? Certificate validation enabled? UserAgent set?
- [ ] **Thread safety:** EventBus, TerrainStore, ProjectManager accessed from multiple threads? Mutable fields (synthesizedLegacy_) guarded?

### 3D. Qt 6 / C++20 Idioms (per AGENTS.md conventions)
- [ ] **QStringView, not QStringRef:** All Qt 6 string handling uses QStringView
- [ ] **QLatin1String for comparisons:** String literals compared via QLatin1String
- [ ] **No leading XML whitespace:** Test strings `<?xml` never preceded by whitespace
- [ ] **Lane.width is Polynomial3, not double:** All lane width storage/access consistent with Polynomial3 type
- [ ] **OSM header include rule:** Headers including RoadV2 only from .cpp files, never from other headers (avoids duplicate road_v2.hpp)
- [ ] **Q_OBJECT AUTOMOC:** Every Q_OBJECT class has a .cpp listed in CMakeLists.txt APP_SOURCES / LANEMAKER_UI_SOURCES etc.
- [ ] **QString::arg vs concatenation:** No `"foo " + bar + " baz"` pattern; uses `.arg()`
- [ ] **`override`/`final` on virtuals:** All Qt overrides marked `override`; no accidental hiding

### 3E. Architecture & Encapsulation
- [ ] **RoadV2 invariant:** `segments_` and `geometry_` always in sync? No non-const accessor to `segments_` bypasses rebuildGeometryView? (`addSegment`/`clearSegments` only mutation