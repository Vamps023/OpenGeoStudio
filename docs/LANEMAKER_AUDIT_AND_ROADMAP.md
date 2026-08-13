# LaneMaker-Level Road Editor — Architecture Audit, Gap Analysis & Roadmap

**Date:** 2026-08-13
**Status:** Audit complete, awaiting implementation approval
**Scope:** Transform OpenGeoStudio-Qt Road Studio into a complete LaneMaker-style professional road-network editor while preserving the existing C++ road engine.

---

## Part 1 — Architecture Map

Each module is classified as: **WORKS** / **PARTIAL** / **REFACTOR** / **REUSE** / **REPLACE** / **MISSING**.

### Application Shell

| Module | State | Notes |
|--------|-------|-------|
| `MainWindow` (menu bar, toolbar, status bar, dock widgets, stacked workspaces) | WORKS | `src/app/main.cpp` — full QMainWindow with 4 workspaces, command palette, settings dialog |
| `ApplicationContext` (EventBus, ProjectManager, WorkspaceManager) | WORKS | `src/core/ApplicationContext.cpp` — clean service container |
| `ProjectManager` (CRUD, recent, autosave) | PARTIAL | In-memory recent list only; no `.ogproj` file persistence on disk |
| `WorkspaceManager` | WORKS | 4 workspaces defined with signals |
| `Logger` | WORKS | Scoped logging with console + optional file transport |
| Settings persistence | MISSING | `SettingsDialog` does not save/load to disk |

### UI / Viewport

| Module | State | Notes |
|--------|-------|-------|
| `MapViewportWidget` (QMapLibre wrapper) | WORKS | `src/app/MapViewportWidget.cpp` — Esri imagery via local HTTP server |
| `RoadViewport2D` (MapLibre + overlay) | PARTIAL | `src/ui/roadstudio/RoadViewport2D.cpp` (1391 lines) — renders centerline, edges, lane boundaries, control points, direction handle, staged preview, snap indicator, 18 debug layers. **VIOLATION:** geometry logic embedded in widget (`generateFlexGeometry`, `handleLmRoadClick`) |
| `RoadViewport3D` (OpenGL mesh) | PARTIAL | `src/ui/roadstudio/RoadViewport3D.cpp` (200 lines) — road surface mesh, ground grid, orbit camera. No lane markings, no lane-level coloring, no junction rendering in 3D |
| `TerrainViewport` | WORKS | `src/ui/terrain/TerrainViewport.cpp` — area selection, tile grid |
| `TrainViewport2D` | WORKS | `src/ui/trainstudio/TrainViewport2D.cpp` — track rendering |

### Editor / Interaction

| Module | State | Notes |
|--------|-------|-------|
| Tool framework (`EditorTool` base class) | MISSING | Tools are enum values; logic scattered in viewport mouse handlers |
| `RoadDrawingSession` polymorphic session pattern | MISSING | LaneMaker uses session objects with `Update/Complete/Cancel`. Qt app has inline state in store + viewport |
| Selection system | PARTIAL | Single road / control point selection via store. No multi-select, no gizmo, no handles beyond direction handle |
| Snapping | PARTIAL | Endpoint snapping + grid snap. No magnetic snap to road boundaries, no lane-level snap |
| Gizmos / handles | PARTIAL | Direction handle only. No move/scale/rotate gizmos |
| Context menus | MISSING | No right-click context menus anywhere |
| Keyboard shortcuts | PARTIAL | V/R/L/D/M mode switches, Ctrl+Z/Y, Ctrl+Shift+P. No Space=confirm, Esc=cancel, Del, etc. |
| Measurements | MISSING | No distance/angle measurement tools |

### Scene / World

| Module | State | Notes |
|--------|-------|-------|
| Scene tree (QTreeView of roads/lanes/junctions) | MISSING | Left dock is placeholder QLabel |
| World/scene container | PARTIAL | `RoadStudioStore` holds `std::vector<Road>` but no junction/objects/signals hierarchy |
| Object browser / asset browser | MISSING | No asset library, no object placement palette |

### Road System

| Module | State | Notes |
|--------|-------|-------|
| Road data model (`Road`, `ControlPoint`, `SegmentMetadata`) | WORKS | `src/ui/roadstudio/RoadTypes.hpp` — lat/lon points, bezier handles, segment metadata |
| `RoadV2` segment-based model | WORKS | `src/engine/road/road_v2.hpp` — owns geometry segments |
| `RoadAdapter` (Road ↔ RoadV2) | WORKS | `src/engine/road/road_adapter.hpp` — bidirectional, frozen |
| Road creation (multi-click staged) | PARTIAL | Store has `startLmRoad/setLmRoadEnd/setLmRoadEndDir/finishLmRoad`. No polyline multi-segment, no road extension, no road joining |
| Road editing (move points, bezier handles) | PARTIAL | Drag control points in 2D. No split/merge operations in Qt UI |
| Road destruction | PARTIAL | Destroy mode selects road to remove. No s-range partial deletion |
| Road modification | PARTIAL | Modify mode changes lane profile. No s-range partial modification |

### Lane System

| Module | State | Notes |
|--------|-------|-------|
| Lane data model (`Lane`, `LaneSection`, `Polynomial3`) | WORKS | `src/engine/road/lane_engine.hpp` — OpenDRIVE lane ID convention |
| Lane geometry evaluation | WORKS | `src/engine/road/lane_geometry.hpp` — world-space lane center/boundary |
| Lane sampling | WORKS | `src/engine/road/lane_sampling.hpp` — adaptive sampling |
| Lane network (`LaneNetwork`, `LaneCenterline`, `LaneBoundary`) | WORKS | `src/engine/road/lane_network.hpp` — `generateLaneNetwork` |
| LaneConfigWidget (cross-section editor) | PARTIAL | `src/ui/roadstudio/widgets/LaneConfigWidget.cpp` — left/right lane count. No offset, no per-lane width, no lane type |
| Lane creation mode (connecting roads) | MISSING | No `LanesCreationSession` equivalent. Cannot create ramps/splits/connecting roads |
| Lane direction editing | MISSING | No UI to flip lane direction |
| Lane width editing | PARTIAL | Global lane width in toolbar. No per-lane, per-section width |
| Lane type editing | MISSING | No UI for driving/shoulder/sidewalk/bike lane types |

### Junction System

| Module | State | Notes |
|--------|-------|-------|
| `RoadGraph` (network topology) | WORKS | `src/engine/road/road_engine/internal/road_graph.hpp` — nodes, edges, adjacency, endpoint/junction detection |
| `LaneGraph` + `LaneGraphBuilder` | WORKS | `src/engine/road/road_engine/internal/lane_graph.hpp` — lane connectivity, maneuvers, bezier connections |
| `JunctionBuilder` | WORKS | `src/engine/road/road_engine/internal/junction_builder.hpp` — junction mesh + lane stripes |
| `ConstrainedTriangulation` | WORKS | `src/engine/road/road_engine/internal/constrained_triangulation.hpp` — Delaunay for junction meshes |
| Legacy `intersection.hpp` | WORKS | `src/engine/road/intersection.hpp` — edge-based junction with fillet corners |
| Junction UI integration | MISSING | Engine has full junction support but **UI does not call** `RoadGraph::buildFrom`, `LaneGraphBuilder::build`, or `JunctionBuilder::build` |
| Junction visualization (2D) | MISSING | No junction boundary, no connecting road preview, no junction mesh in 2D |
| Junction visualization (3D) | MISSING | No junction mesh in 3D viewport |
| Lane connection editing | MISSING | No UI to edit which lanes connect through a junction |
| Direct junction support | MISSING | No direct lane-to-lane connection UI |

### Geometry

| Module | State | Notes |
|--------|-------|-------|
| `LineSegment`, `ArcSegment`, `SpiralSegment`, `BezierSegment` | WORKS | `src/engine/road/geometry_segment.hpp` — polymorphic, adaptive sampling, arc-length parameterization |
| `SegmentSequence` | WORKS | Binary search, global/local s mapping, continuity validation |
| `arc.hpp` (circle arc, fillet) | WORKS | `src/engine/road/arc.hpp` |
| `clothoid.hpp` (Euler spiral) | WORKS | `src/engine/road/clothoid.hpp` — Fresnel integrals |
| `lanemaker_curve.hpp` | WORKS | `src/engine/road/lanemaker_curve.hpp` — LaneMaker-style curve fitting |
| `skia_arc.hpp` | WORKS | `src/engine/road/skia_arc.hpp` |

### Terrain

| Module | State | Notes |
|--------|-------|-------|
| Terrain export (DEM, GeoTIFF, PNG) | WORKS | `src/ui/terrain/ExportEngine.cpp` — AAIGrid parsing, GeoTIFF writing, tile download |
| Terrain viewport (area selection) | WORKS | `src/ui/terrain/TerrainViewport.cpp` |
| Terrain import into road studio | MISSING | No terrain mesh in road viewport, no elevation-from-terrain |
| Terrain-road interaction | MISSING | No road drape on terrain, no terrain height sampling |

### Objects / Signals / Markings

| Module | State | Notes |
|--------|-------|-------|
| Road mark generation | WORKS | `src/engine/road/road_mark_generator.hpp` — dash patterns, solid/dashed marking geometry |
| Road mesh generation | WORKS | `src/engine/road/road_mesh_generator.hpp` — lane strips, markings |
| Road mark editing UI | MISSING | No UI to edit marking type, color, dash pattern |
| Road mark visualization (2D) | PARTIAL | Centerline/edges/lane boundaries drawn. No dashed/solid distinction, no arrows |
| Road mark visualization (3D) | MISSING | No markings in 3D viewport |
| Signs / signals | MISSING | No sign or signal data model, no placement, no visualization |
| Objects (buildings, trees, poles) | MISSING | No object data model, no placement, no visualization |
| Traffic simulation | MISSING | No vehicle manager, no IDM, no signal phases |

### Import / Export

| Module | State | Notes |
|--------|-------|-------|
| OpenDRIVE export | PARTIAL | `LaneMakerService::exportOpenDrive()` via libOpenDRIVE. Not fully wired to all road features |
| OpenDRIVE import | MISSING | No `.xodr` file import |
| Project save/load (`.ogproj`) | PARTIAL | `ProjectManager` has API but no disk persistence |
| Terrain export (TIFF/PNG) | WORKS | `ExportEngine.cpp` |
| Train XML export | WORKS | `TrainStudioStore.cpp` — Oksyton format |

### Serialization

| Module | State | Notes |
|--------|-------|-------|
| Road JSON serialization | WORKS | `RoadTypes.hpp` — `toJson/fromJson` for Road, ControlPoint |
| Project JSON | PARTIAL | `Project.hpp` has `toJson/fromJson` but not persisted |
| Scene/network serialization | MISSING | No full scene serialization (roads + junctions + objects + signals) |
| Action replay | MISSING | LaneMaker has `ActionManager` for replay testing |

### Validation

| Module | State | Notes |
|--------|-------|-------|
| Engine-level lane validation | WORKS | `src/engine/road/lane_engine.hpp` — `LaneValidation` |
| Network validation UI | MISSING | No validation panel, no error reporting, no "Verify Now" button |
| Geometric validation | MISSING | No overlap detection UI, no continuity check UI |

### Undo / Redo

| Module | State | Notes |
|--------|-------|-------|
| Custom history snapshots | PARTIAL | `RoadStudioStore` has undo/redo stacks (max 50). Inspector changes bypass undo |
| QUndoStack/QUndoCommand | MISSING | Not using Qt's standard undo framework |
| Command pattern | MISSING | No command objects; direct store method calls from UI |

### Rendering

| Module | State | Notes |
|--------|-------|-------|
| 2D road rendering (QPainter overlay) | PARTIAL | Centerline, edges, lane boundaries, control points, direction handle, debug layers. No lane fills, no markings, no junctions |
| 3D road rendering (OpenGL) | PARTIAL | Road surface mesh, ground grid. No lane markings, no lane colors, no junctions, no objects, no lighting improvements |
| Spatial indexing | MISSING | No spatial indexer for ray casting / overlap detection in Qt app (LaneMaker has `SpatialIndexer`) |
| Camera system | PARTIAL | 2D: map zoom/pan. 3D: orbit camera. No ground-plane ray cast, no cursor tracking on road surface |

---

## Part 2 — LaneMaker vs OpenGeoStudio Gap Analysis

| Feature | Current OpenGeoStudio-Qt | LaneMaker Reference | Gap | Implementation | Validation |
|---------|-------------------------|---------------------|-----|----------------|------------|
| **Main window** | QMainWindow, 4 workspaces, menu bar, toolbar, status bar, dock widgets | QWidget, single-purpose road editor, menu bar, status bar | Low — shell is adequate | Reuse existing | Verify menu/dock/toolbar functional |
| **Edit modes** | 5 enum values (V/R/L/D/M), logic in viewport mouse handlers | 5 modes with polymorphic `RoadDrawingSession` subclasses | High — no session abstraction | Create `EditorTool`/`EditSession` base class + subclasses | Test each mode creates correct session |
| **Road creation** | Multi-click staged (start, end, end-dir). Single segment. | Multi-click polyline, direction handle, snapping, curve fitting, elevation | High — no polyline, no extension, no joining | Implement `RoadCreationSession` with staged geometry, snap-to-road, ConnectRays | Test road creation with 1/2/3+ clicks, snapping, extension |
| **Lane creation** | Lane count adjustment only | `LanesCreationSession` — connecting roads, ramps, splits, direct/regular junctions | Critical — completely missing | Implement `LaneCreationSession` using `LaneGraphBuilder` + `JunctionBuilder` | Test lane creation between two roads, verify junction mesh |
| **Road destruction** | Select entire road to delete | `RoadDestroySession` — select s-range, partial deletion, hint polygons | High — no partial deletion | Implement s-range selection + partial delete | Test full and partial deletion |
| **Road modification** | Change lane profile on whole road | `RoadModificationSession` — s-range profile change, lane config per section | High — no s-range modification | Implement s-range selection + `ModifyProfile` | Test profile change on sub-range |
| **Direction handle** | Single direction handle at end | Direction handle at each staged point, interactive rotation | Medium | Extend to per-point handles | Test handle drag updates preview |
| **Snapping** | Endpoint + grid snap | Magnetic snap to road boundaries, endpoint snap, lane snap | High — no magnetic/lane snap | Implement spatial indexer + magnetic snap | Test snap to road boundary, endpoint, lane |
| **Selection** | Single road or control point | Multi-select, gizmo, handles | High | Implement multi-select + gizmo framework | Test multi-select, box select |
| **Junction detection** | Engine has `RoadGraph`/`JunctionBuilder` but UI doesn't call them | Automatic junction detection on road overlap, boundary computation | Critical — engine unused by UI | Wire `RoadGraph::buildFrom` → `LaneGraphBuilder::build` → `JunctionBuilder::build` into store | Test two overlapping roads produce junction |
| **Junction visualization** | None | Junction boundary, connecting roads, cavity rendering, mesh | Critical | Render junction boundary in 2D, junction mesh in 3D | Visual verification |
| **Lane connections** | None | Lane-to-lane connection editing, maneuver types (straight/left/right/U-turn) | Critical | Implement lane connection editor using `LaneGraph` | Test connection editing changes lane graph |
| **Road markings** | Engine generates markings; UI draws lane boundaries only | Section graphics with dashed/solid markings, arrows, per-lane | High | Wire `road_mark_generator` output to 2D/3D rendering | Verify dashed/solid/arrow rendering |
| **Lane types** | All lanes are "driving" | Driving, shoulder, sidewalk, bike, parking | High | Add lane type to lane model + UI | Test type change updates geometry |
| **Lane width** | Global lane width | Per-lane, per-section `Polynomial3` width | High | Wire `Polynomial3` width to UI | Test per-lane width change |
| **Elevation** | `RoadElevationEditor` with tools (Flat, Slope, Bridge, Roller) | Elevation control in road creation session, per-point z | Medium | Integrate elevation editor with road creation | Test elevation profile applied to 3D mesh |
| **Superelevation** | None | Not explicitly in LaneMaker either | Low | Future enhancement | — |
| **Signs / signals** | None | `Signal` class with phase cycles, `VehicleManager` | Critical | Add signal data model + placement + visualization | Test signal placement and phase display |
| **Objects** | None | Not in LaneMaker core | Medium | Future enhancement | — |
| **Traffic simulation** | None | `VehicleManager` with IDM, lane changing, routing | Critical (if sim required) | Future phase | — |
| **Scene tree** | Placeholder QLabel | Not in LaneMaker (single-purpose) | Medium | Implement QTreeView of roads/lanes/junctions | Test tree reflects scene state |
| **Property inspector** | `RoadInspector` — direct data manipulation, bypasses undo | LaneMaker uses session-based editing | High — no undo for inspector | Route inspector changes through command pattern | Test undo after inspector edit |
| **Undo/redo** | Custom snapshots, max 50, inspector bypasses | `ChangeTracker` with `MapChange` (road + junction before/after) | High | Migrate to QUndoStack + QUndoCommand | Test undo/redo for all operations |
| **OpenDRIVE export** | Via `LaneMakerService` | Native via libOpenDRIVE | Low — functional | Verify completeness | Test export → re-import round-trip |
| **OpenDRIVE import** | Missing | File → Open in LaneMaker | High | Implement `.xodr` parser using libOpenDRIVE | Test import of LaneMaker-exported file |
| **Project persistence** | In-memory only | `.xodr` file save/load | High | Implement `.ogproj` disk persistence | Test save → close → reopen |
| **Validation** | Engine-level only | "Verify Now" menu action, `Validation` class | High | Wire validation to UI panel | Test validation reports errors |
| **Spatial indexing** | None | `SpatialIndexer` for ray cast + overlap | High | Implement spatial indexer for hit testing | Test ray cast accuracy |
| **3D rendering** | Road surface mesh only | Quad strips for lanes, markings, junctions, instanced vehicles | High | Add lane markings, lane colors, junction mesh to 3D | Visual verification |
| **Camera** | Orbit camera | Ground-plane ray cast, cursor tracking, road hover info | Medium | Add ray cast + hover info | Test cursor tracks road surface |
| **Context menus** | None | Not prominent in LaneMaker | Low | Future enhancement | — |
| **Measurements** | None | Not in LaneMaker | Low | Future enhancement | — |
| **Action replay** | None | `ActionManager` records/replays all actions | Medium (testing) | Future phase for test automation | — |

---

## Part 3 — Implementation Roadmap

### Guiding Principles

1. **Preserve the engine** — never duplicate road-engine logic; wire UI to existing engine APIs
2. **Extract logic from widgets** — move geometry/interaction logic to controller/service classes
3. **Incremental verification** — build + test + run after each phase
4. **No fake UI** — every control must modify real data
5. **Test the engine integration** — not just compilation, but actual runtime behavior

### Phase 24: Editor Session Framework + Command Pattern

**Goal:** Establish the architectural foundation for LaneMaker-style editing.

**Tasks:**
1. Create `src/ui/roadstudio/editor/EditSession.hpp` — abstract base class with `update(MouseAction)`, `update(KeyPressAction)`, `complete()`, `cancel()`
2. Create `src/ui/roadstudio/editor/EditorController.hpp/.cpp` — manages active session, forwards input, handles confirm/cancel
3. Create `QUndoCommand` subclasses for road operations (`CreateRoadCommand`, `DeleteRoadCommand`, `ModifyRoadCommand`, etc.)
4. Add `QUndoStack` to `RoadStudioStore`, replace custom snapshot system
5. Route `RoadInspector` changes through commands
6. Add Space=confirm, Esc=cancel keyboard handling

**Dependencies:** None (foundation)
**Risk:** Breaking existing undo/redo during migration
**Testing:** Unit tests for command undo/redo; manual test of inspector undo
**Files:** New: `src/ui/roadstudio/editor/` directory; Modified: `RoadStudioStore`, `RoadInspector`, `RoadViewport2D`

### Phase 25: Road Creation Session (Full LaneMaker Workflow)

**Goal:** Reproduce LaneMaker's `RoadCreationSession` with polyline, direction handles, snapping, extension, and joining.

**Tasks:**
1. Implement `RoadCreationSession` — multi-click polyline with staged geometry
2. Add per-point direction handles (not just end)
3. Implement snap-to-existing-road (extend endpoint, join endpoint)
4. Use `LM::ConnectRays` / `FitSpiral` for curve fitting between staged points
5. Add road extension (click past endpoint of existing road)
6. Add road joining (click near endpoint of another road)
7. Add preview boundary/refline graphics
8. Wire to `RoadEngineService` for real geometry

**Dependencies:** Phase 24 (session framework)
**Risk:** Curve fitting edge cases (degenerate inputs, SEH on Windows)
**Testing:** Test 1/2/3+ click roads, snapping, extension, joining; verify geometry matches engine output
**Files:** New: `src/ui/roadstudio/editor/RoadCreationSession.cpp`; Modified: `RoadViewport2D`, `RoadEngineService`

### Phase 26: Junction System Integration

**Goal:** Wire the existing engine junction system to the UI — the single biggest gap.

**Tasks:**
1. Add `RoadGraph::buildFrom(roads)` call to `RoadStudioStore` after every road change
2. Add `LaneGraphBuilder::build(roadGraph)` call
3. Add `JunctionBuilder::build(laneGraph, roads)` call
4. Store junction results in `RoadStudioStore`
5. Render junction boundaries in `RoadViewport2D`
6. Render junction meshes in `RoadViewport3D`
7. Add junction detection on road overlap (automatic)
8. Add "Detect Junctions" toolbar button for manual trigger

**Dependencies:** Phase 25 (road creation produces roads to junction)
**Risk:** Engine API may need adapter layer for UI road types; performance with large networks
**Testing:** Create two roads that overlap → verify junction appears; test junction mesh in 3D
**Files:** Modified: `RoadStudioStore`, `RoadViewport2D`, `RoadViewport3D`, `RoadEngineService`

### Phase 27: Lane Creation Session (Connecting Roads)

**Goal:** Implement LaneMaker's `LanesCreationSession` for creating connecting roads between existing roads.

**Tasks:**
1. Implement `LaneCreationSession` — click source road, click destination road
2. Validate snap compatibility (lane count matching)
3. Determine connection type (direct vs regular junction)
4. Generate connecting road geometry using `LaneGraph` maneuvers
5. Render connecting road preview
6. Create junction on confirm

**Dependencies:** Phase 26 (junction system)
**Risk:** Complex lane matching logic; direct vs regular junction distinction
**Testing:** Test lane creation between two roads with matching/mismatched lane counts
**Files:** New: `src/ui/roadstudio/editor/LaneCreationSession.cpp`

### Phase 28: Road Destroy + Modify Sessions (S-Range)

**Goal:** Upgrade destroy and modify modes to support s-range partial operations.

**Tasks:**
1. Implement `RoadDestroySession` — click road, drag to select s-range, hint polygon preview
2. Implement `RoadModificationSession` — click road, drag to select s-range, change lane config for range only
3. Add s-range selection visualization in 2D
4. Wire to `Road::SplitRoad` / `Road::ModifyProfile` equivalents in engine

**Dependencies:** Phase 24 (session framework)
**Risk:** S-range parameterization requires accurate arc-length mapping
**Testing:** Test partial deletion, partial profile change
**Files:** New: `src/ui/roadstudio/editor/RoadDestroySession.cpp`, `RoadModificationSession.cpp`

### Phase 29: Road Markings Visualization

**Goal:** Render real road markings (dashed, solid, arrows) from engine output.

**Tasks:**
1. Wire `road_mark_generator` output to `RoadEngineService`
2. Render dashed/solid markings in `RoadViewport2D` (distinct from lane boundaries)
3. Render markings as textured strips in `RoadViewport3D`
4. Add lane-level color fills in 2D (asphalt per lane)
5. Add directional arrows at junctions

**Dependencies:** Phase 26 (junction system for arrows)
**Risk:** Marking geometry may need UV mapping for 3D
**Testing:** Visual verification of dashed/solid/arrow rendering
**Files:** Modified: `RoadEngineService`, `RoadViewport2D`, `RoadViewport3D`

### Phase 30: Lane Type + Per-Lane Width Editing

**Goal:** Support LaneMaker-level lane configuration.

**Tasks:**
1. Extend `LaneConfigWidget` with lane type selection (driving, shoulder, sidewalk, bike)
2. Add per-lane width editing using `Polynomial3`
3. Add per-section lane config (different configs at different s-ranges)
4. Wire to `Lane` model in engine
5. Update lane geometry + mesh on config change

**Dependencies:** Phase 28 (modify session for per-section changes)
**Risk:** Polynomial3 UI complexity
**Testing:** Test lane type change updates geometry; test per-lane width change
**Files:** Modified: `LaneConfigWidget`, `RoadEngineService`, `RoadInspector`

### Phase 31: Scene Tree + Project Persistence

**Goal:** Professional project management.

**Tasks:**
1. Implement `SceneTreeWidget` (QTreeView) — roads, lanes, junctions, objects hierarchy
2. Wire tree selection to store selection
3. Implement `.ogproj` file save/load with full scene serialization
4. Implement settings persistence (API keys, preferences)
5. Implement recent projects file persistence
6. Add "Save", "Save As", "Open" to File menu

**Dependencies:** Phase 26 (junctions in scene)
**Risk:** Serialization format design
**Testing:** Test save → close → reopen preserves all roads/junctions
**Files:** New: `src/ui/roadstudio/SceneTreeWidget.cpp`; Modified: `ProjectManager`, `RoadStudioStore`

### Phase 32: OpenDRIVE Import + Validation UI

**Goal:** Round-trip OpenDRIVE and validate networks.

**Tasks:**
1. Implement `.xodr` import using libOpenDRIVE parser
2. Convert imported roads to UI road model
3. Add "Import OpenDRIVE" to File menu
4. Wire `LaneValidation` to validation panel
5. Add "Verify Now" action (Edit menu)
6. Add error/warning list panel
7. Add overlap detection using `LM::DetectOverlaps`

**Dependencies:** Phase 31 (project persistence)
**Risk:** OpenDRIVE → UI model conversion may lose information
**Testing:** Test LaneMaker-exported `.xodr` import; test validation reports errors
**Files:** New: `src/ui/roadstudio/ValidationPanel.cpp`; Modified: `LaneMakerService`, `RoadStudioStore`

### Phase 33: 3D Viewport Enhancement

**Goal:** Professional 3D road visualization matching LaneMaker quality.

**Tasks:**
1. Add lane-level color distinction in 3D (different shades per lane type)
2. Add road markings as textured quads in 3D
3. Add junction mesh rendering in 3D
4. Add ground-plane ray cast for cursor tracking
5. Add road hover info (road ID, lane, s-position)
6. Improve lighting (directional + ambient + shadows)
7. Add skybox or gradient background

**Dependencies:** Phase 29 (markings), Phase 26 (junctions)
**Risk:** OpenGL complexity, performance with large networks
**Testing:** Visual verification; FPS test with 50+ roads
**Files:** Modified: `RoadViewport3D`

### Phase 34: Signs, Signals, Objects (Future)

**Goal:** Add traffic elements.

**Tasks:**
1. Add signal data model (phase, cycle, controlled lanes)
2. Add signal placement UI
3. Add signal visualization (3D box + light)
4. Add sign data model + placement
5. Add object data model + placement
6. Add to scene tree

**Dependencies:** Phase 31 (scene tree)
**Risk:** Scope expansion; may defer if not required
**Testing:** Test signal placement and phase display
**Files:** New: `src/ui/roadstudio/SignalModel.hpp`, `ObjectModel.hpp`

### Phase 35: Spatial Indexing + Performance (Future)

**Goal:** Large-network performance.

**Tasks:**
1. Implement spatial indexer (R-tree or quadtree) for ray casting
2. Use for hit testing in 2D/3D
3. Use for overlap detection
4. Implement level-of-detail for 3D rendering
5. Implement culling for off-screen roads

**Dependencies:** Phase 33 (3D enhancement)
**Risk:** Performance tuning complexity
**Testing:** FPS test with 200+ roads
**Files:** New: `src/engine/road/spatial_index.hpp`

---

## Part 4 — Reuse vs Refactor vs Replace

### Reuse As-Is

- `src/engine/road/` — entire engine (geometry, lanes, junctions, mesh, marks, OpenDRIVE)
- `src/engine/lanemaker/` — LaneMaker library integration
- `src/core/` — ApplicationContext, EventBus, ProjectManager API, WorkspaceManager, Logger
- `src/app/MapViewportWidget` — map viewport
- `src/ui/home/HomeWidget` — home screen
- `src/ui/terrain/` — terrain studio (complete)
- `src/ui/trainstudio/` — train studio (complete)
- `src/ui/roadstudio/RoadTypes.hpp` — data types
- `src/ui/roadstudio/GeoConvert.hpp` — coordinate conversion
- `src/ui/roadstudio/RoadEngineService` — engine bridge
- `src/ui/roadstudio/LaneMakerService` — LaneMaker bridge
- `src/ui/roadstudio/RoadViewport3D` — 3D viewport (extend, don't rewrite)

### Refactor

- `src/ui/roadstudio/RoadStudioStore` — migrate to QUndoStack, add junction/lane graph state
- `src/ui/roadstudio/RoadViewport2D` — extract geometry logic to controller, keep rendering
- `src/ui/roadstudio/widgets/RoadInspector` — route through command pattern
- `src/ui/roadstudio/widgets/RoadElevationEditor` — extract computation to service
- `src/ui/roadstudio/widgets/LaneConfigWidget` — extend with lane types, per-lane width
- `src/app/main.cpp` — add scene tree to left dock, add File menu save/load

### Replace

- Custom undo/redo snapshots → QUndoStack + QUndoCommand
- Inline tool logic in viewport → EditSession subclasses
- Placeholder QLabel in left dock → SceneTreeWidget

### Create New

- `src/ui/roadstudio/editor/EditSession.hpp` — session base class
- `src/ui/roadstudio/editor/EditorController.hpp/.cpp` — session manager
- `src/ui/roadstudio/editor/RoadCreationSession.cpp`
- `src/ui/roadstudio/editor/LaneCreationSession.cpp`
- `src/ui/roadstudio/editor/RoadDestroySession.cpp`
- `src/ui/roadstudio/editor/RoadModificationSession.cpp`
- `src/ui/roadstudio/SceneTreeWidget.hpp/.cpp`
- `src/ui/roadstudio/ValidationPanel.hpp/.cpp`
- `src/engine/road/spatial_index.hpp` (future)

---

## Part 5 — Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Breaking existing undo/redo during QUndoStack migration | High | High | Phase 24 first; keep snapshot system as fallback until commands proven |
| Engine API mismatch with UI road types | Medium | High | Use existing `RoadAdapter` for conversion; add adapter tests |
| SEH crashes from ConnectRays on degenerate inputs | Medium | Critical | Keep SEH guard; fall back to FitArcOrLine; add input validation |
| Junction builder performance with large networks | Medium | Medium | Profile early; add spatial indexing in Phase 35 if needed |
| 3D rendering performance with markings + junctions | Medium | Medium | Use instanced rendering; LOD in Phase 35 |
| OpenDRIVE import losing information | High | Medium | Round-trip test with LaneMaker-exported files |
| Scope creep into traffic simulation | Medium | High | Defer signs/signals/traffic to Phase 34+; focus on road editing first |
| Inspector changes not undoable | High | Medium | Phase 24 routes all changes through commands |

---

## Part 6 — Testing Strategy

### Existing Baseline

- 473 doctest test cases, 5308 assertions (engine geometry, lanes, adapter)
- All currently passing

### New Tests Required

| Area | Test Type | Framework |
|------|-----------|-----------|
| EditSession lifecycle | Unit | doctest |
| QUndoCommand undo/redo | Unit | doctest |
| Road creation (1/2/3+ clicks, snap, extend, join) | Integration | doctest + manual |
| Junction detection (overlap → junction) | Integration | doctest |
| Junction mesh generation | Integration | doctest |
| Lane creation (connecting roads) | Integration | doctest + manual |
| S-range destroy/modify | Integration | doctest |
| Road marking generation | Unit | doctest |
| Lane type/width changes | Unit | doctest |
| Scene tree reflects state | Integration | manual |
| Project save/load round-trip | Integration | doctest |
| OpenDRIVE import/export round-trip | Integration | doctest |
| Validation reports errors | Unit | doctest |
| 3D rendering (visual) | Manual | visual inspection |

### Verification Procedure (Each Phase)

1. `cmake --build build` — must succeed
2. `./build/geometry_segment_tests` — all tests pass
3. `./build/OpenGeoStudio` — app launches
4. Manual functional test of new feature
5. No regression in existing features
6. Commit only after all pass

---

## Part 7 — Milestone Summary

| Phase | Name | Key Deliverable | Estimated Effort |
|-------|------|-----------------|-----------------|
| 24 | Editor Session Framework | EditSession base + QUndoStack | Foundation |
| 25 | Road Creation Session | Full LaneMaker road creation | High |
| 26 | Junction System Integration | Engine junctions wired to UI | High |
| 27 | Lane Creation Session | Connecting roads + junctions | High |
| 28 | Destroy + Modify (S-Range) | Partial road operations | Medium |
| 29 | Road Markings Visualization | Dashed/solid/arrows in 2D+3D | Medium |
| 30 | Lane Type + Width Editing | Per-lane, per-section config | Medium |
| 31 | Scene Tree + Persistence | Project save/load, tree view | Medium |
| 32 | OpenDRIVE Import + Validation | Round-trip + error reporting | Medium |
| 33 | 3D Viewport Enhancement | Professional 3D visualization | Medium |
| 34 | Signs, Signals, Objects | Traffic elements (future) | Low priority |
| 35 | Spatial Indexing + Performance | Large-network support (future) | Low priority |

---

## Conclusion

The OpenGeoStudio-Qt road engine is **substantially complete** — it has geometry segments, lane systems, lane graphs, junction builders, mesh generators, road mark generators, and OpenDRIVE support. The engine is not the bottleneck.

The **critical gap is UI integration**: the engine's junction system (`RoadGraph`, `LaneGraphBuilder`, `JunctionBuilder`) is not called by the UI at all. The editor lacks the session-based interaction model that makes LaneMaker feel professional. Undo/redo bypasses the inspector. There is no scene tree, no project persistence, no OpenDRIVE import, and no validation UI.

The roadmap prioritizes:
1. **Foundation** (Phase 24) — session framework + command pattern
2. **Core editing** (Phases 25-28) — full road/lane/junction creation and editing
3. **Visualization** (Phases 29, 33) — markings and 3D quality
4. **Professional features** (Phases 30-32) — lane config, persistence, validation
5. **Future** (Phases 34-35) — traffic elements, performance

Each phase builds on the previous, preserves existing functionality, and is verified by build + test + run before proceeding.
