# Road Engine Migration Plan

> Companion to `docs/ROAD_ENGINE_NEW_ARCHITECTURE.md`.
> This document describes **how to get there** without breaking the live editor.

## Table of Contents

1. [Current Data Flow](#1-current-data-flow)
2. [Call-Site Audit](#2-call-site-audit)
3. [Migration Strategy (Strangler-Fig)](#3-migration-strategy-strangler-fig)
4. [Phase-by-Phase Migration Steps](#4-phase-by-phase-migration-steps)
5. [Rollback Plan](#5-rollback-plan)
6. [Open Questions](#6-open-questions)

---

## 1. Current Data Flow

### 1.1 User Click → Rendered Road (Top-Down 2D)

```
1. User clicks map in RoadViewport (MapLibre)
       │
2. RoadViewport calls store.addControlPoint(roadId, lat, lon)
       │
3. roadStudioStore appends a ControlPoint {lat, lon, z, handleIn/Out, type}
   to road.points[], pushes undo snapshot
       │
4. React re-renders RoadViewport
       │
5. useEffect [roads, refLat, refLon] fires:
   │
   ├── refreshSampleCache():
   │     For each road with ≥2 points:
   │       roadEngineClient.sampleCenterline(road, refLat, refLon, 32)
   │         → toCppRoad(): convert lat/lon → local x/y meters
   │           → IPC → C++ Road.sampleCenterline(32)
   │             → Iterates ControlPoint[], builds bezier segments,
   │               arc-length-distributes 32 samples, returns Point2D[]
   │         ← Cache in sampleCacheRef.current[roadId]
   │
   ├── updateMapRoads():
   │     For each road: use cached samples to build MapLibre GeoJSON LineString
   │     Add road surface layer (width-based), lane markings (laneCount-based),
   │     sidewalks, curbs. Render control points as circles.
   │
   └── (if 3D mode) updateBabylonRoads():
         roadEngineClient.generateRoadMesh(road, refLat, refLon, 64)
           → C++ generateRoadMesh():
               → road.sampleCenterline3D(64)  [uses ControlPoint[].z]
               → road.sampleCenterline(64)    [uses ControlPoint[] for tangents]
               → halfWidth = road.width / 2
               → Miter-joint offset → left/right edge vertices
               → Triangle strip indices, arc-length UVs, up normals
           ← MeshData {vertices, normals, uvs, indices}
         → Babylon.js VertexData → Mesh
```

### 1.2 Road Creation (Tools)

```
User selects tool (line/arc/clothoid/polyline/spline) in RoadToolbar
       │
Tool workflow in roadStudioStore:
  - startNewRoad(lat, lon) → creates Road with 1 ControlPoint
  - User clicks/interacts → addControlPoint() or tool-specific preview
  - For arc/clothoid: roadEngineClient.computeCircleArc/computeClothoid()
      → C++ returns sampled points
      → Store converts to ControlPoint[] (picks N points from samples)
  - finishDrawing() → road has final ControlPoint[]
       │
Same render pipeline as 1.1 fires.
```

### 1.3 Intersection Generation

```
User selects 2 roads → detectIntersection()
       │
roadEngineClient.generateIntersection(road1, road2, refLat, refLon)
       │
toCppRoad() for both → IPC → C++ generateIntersection():
  - Sample both centerlines (ControlPoint[] → Point2D[])
  - Find centerline intersection point
  - Compute angle, trim distances (uses road.width)
  - Build approaches (trimmed centerlines, uses road.width, road.laneCount)
  - generateEdgeBasedPolygon():
      - Left/right boundary lines (uses road.width / 2)
      - Boundary intersections, fillet corners, polygon
  - Generate lane connections (uses road.laneCount)
       │
← GeneratedIntersection {polygon, approaches, corners, trimLines, ...}
       │
Store: generatedIntersections.push(ix)
       │
RoadViewport: render polygon + debug overlays
```

### 1.4 Key Observations

- **ControlPoint[] is the universal road representation** — every C++ function
  that operates on roads starts by calling `road.sampleCenterline()` which
  iterates `points[]` and builds bezier/linear segments internally.
- **road.width is used in 4 places**: mesh generation (halfWidth), intersection
  boundary lines, lane boundary generation, and OpenDRIVE export (laneWidth).
- **road.laneCount is used in 3 places**: lane boundary generation, lane
  markings in viewport, and OpenDRIVE export.
- **The C++ engine never persists state** — every IPC call parses a fresh Road
  from JS, processes it, and returns results. This is ideal for migration
  because we can change the C++ representation without affecting stored data.

---

## 2. Call-Site Audit

### 2.1 C++ Engine — Call Sites Touching ControlPoint[] / width / laneCount

| File | Line(s) | What it touches | Migration Impact |
|------|---------|----------------|-----------------|
| `road.hpp:14-23` | ControlPoint struct | Definition | **Core** — will become legacy |
| `road.hpp:26-53` | Road struct (points, width, laneCount) | Definition | **Core** — will get segments[] + laneSections[] |
| `road.hpp:157-254` | Road::sampleCenterline() | Iterates points[], builds bezier | **Replace** with GeometrySegment::evaluateDS() |
| `road.hpp:259-330` | Road::sampleCenterline3D(), leftEdge(), rightEdge(), length(), bounds() | All use points[] | **Replace** with segment-based versions |
| `mesh.hpp:148` | road.width / 2.0 | halfWidth for mesh | **Replace** with lane boundary offsets |
| `mesh.hpp:261-271` | road.laneCount, road.width | Lane boundary generation | **Replace** with LaneSection queries |
| `intersection.hpp:471-490` | road1.width, road1.laneCount, road2.width, road2.laneCount | Approach construction | **Replace** with lane-aware boundaries |
| `opendrive.hpp:188-214` | road.width, road.laneCount, road.points[] | XML export | **Replace** with segment + lane export |
| `road_tools.hpp` (all) | road.points.push_back(), road.width, road.laneCount | Every tool creates ControlPoint[] | **Replace** with segment creation |
| `road_bridge.cpp:37-60` | parseRoad(): width, laneCount, points[] | JS→C++ deserialization | **Add** segment/lane parsing |
| `road_bridge.cpp:195-217` | roadToJs(): width, laneCount, points[] | C++→JS serialization | **Add** segment/lane serialization |

### 2.2 TypeScript — Call Sites Touching ControlPoint[] / width / laneCount

| File | Line(s) | What it touches | Migration Impact |
|------|---------|----------------|-----------------|
| `types.ts:16-27` | ControlPoint interface | Definition | **Core** — will get segment metadata |
| `types.ts:40-50` | Road interface (points, width, laneCount) | Definition | **Core** — will get segments + laneSections |
| `types.ts:241-251` | detectIntersections() | Uses road.points[0]/[last] for endpoint proximity | **Replace** with road graph |
| `roadEngineClient.ts:65-110` | CppControlPoint, CppRoad, toCppRoad() | Lat/lon → local x/y conversion | **Add** segment/lane conversion |
| `roadStudioStore.ts:317-365` | startNewRoad, addControlPoint | Creates ControlPoint objects | **Add** segment creation alongside |
| `roadStudioStore.ts:367-520` | updateControlPoint, deleteControlPoint, insertControlPoint, splitRoad, mergeRoads | Mutate road.points[] | **Add** segment-aware versions |
| `roadStudioStore.ts:571-811` | finishArc, finishClothoid, finishPolyline, finishSpline | Convert C++ samples → ControlPoint[] | **Replace** with segment creation |
| `RoadViewport.tsx` (many) | road.points, road.width, road.laneCount | Rendering, debug overlays, interaction | **Add** segment-aware rendering |
| `RoadToolbar.tsx:94, 184-195` | selectedRoad.points[selection.pointIndices[0]] | Elevation editor | **Add** segment-aware selection |

### 2.3 Summary: 3 Tiers of Impact

**Tier 1 — Core definitions (must change first):**
- `road.hpp`: ControlPoint, Road struct
- `types.ts`: ControlPoint, Road interface
- `roadEngineClient.ts`: CppRoad, toCppRoad()
- `road_bridge.cpp`: parseRoad(), roadToJs()

**Tier 2 — Consumers (change after core):**
- `road.hpp`: sampleCenterline and all Road methods
- `mesh.hpp`: generateRoadMesh, generateLaneBoundaries
- `intersection.hpp`: generateIntersection
- `opendrive.hpp`: exportOpenDrive
- `road_tools.hpp`: all 6 creation tools
- `roadStudioStore.ts`: all road mutation actions

**Tier 3 — Renderers (change last, can use adapters):**
- `RoadViewport.tsx`: all rendering and interaction
- `RoadToolbar.tsx`: elevation editor

---

## 3. Migration Strategy (Strangler-Fig)

### 3.1 Principle

Build the new `GeometrySegment` / `LaneSection` model **alongside** the existing
`ControlPoint[]` model. The two coexist during migration. An adapter converts
between them. We cut over only when the new model has full parity.

### 3.2 Adapter Pattern

```cpp
// New model (Phase 1+):
namespace geo {
    class GeometrySegment { ... };       // abstract base
    class LineSegment : public GeometrySegment { ... };
    // etc.
    struct LaneSection { ... };
    struct RoadV2 {
        std::vector<std::unique_ptr<GeometrySegment>> segments;
        std::vector<LaneSection> laneSections;
        // ...
    };
}

// Adapter: RoadV2 ↔ Road (legacy)
namespace geo {
    // Convert legacy Road (ControlPoint[]) → RoadV2 (segments)
    RoadV2 roadToV2(const Road& legacy);

    // Convert RoadV2 → legacy Road (for backward compat during migration)
    Road roadFromV2(const RoadV2& v2);

    // Sample using new model (delegates to segments)
    std::vector<Point2D> sampleCenterlineV2(const RoadV2& road, int numSamples);
}
```

### 3.3 Coexistence Strategy

```
Phase 1: Build GeometrySegment + LineSegment (additive, no changes to existing)
Phase 1.5: Build adapter (roadToV2, roadFromV2)
Phase 1.6: Build ArcSegment, SpiralSegment, BezierSegment
Phase 1.7: RoadV2 has full geometry parity with Road
Phase 1.8: C++ internal functions accept RoadV2 (via adapter from Road)
Phase 1.9: Bridge exposes RoadV2 fields (optional in JS, Road still works)
Phase 2: Build LaneSection (additive)
Phase 2.5: RoadV2 has full lane parity with Road.width/laneCount
Phase 2.8: C++ internal functions use LaneSection (via adapter)
Phase 3+: Road (legacy) is deprecated, RoadV2 is primary
Final: Remove Road (legacy), rename RoadV2 → Road
```

### 3.4 Key Invariants During Migration

1. **The live editor never breaks** — `roadStudioStore` always produces
   `Road` (legacy) with `ControlPoint[]`. The C++ engine always accepts
   `Road` (legacy). The adapter handles conversion internally.

2. **Tests always pass** — existing 41 tests test the legacy path. New tests
   test the new path. Both must pass until cutover.

3. **No big-bang cutover** — each phase is independently shippable. If
   Phase 2 has issues, we can ship Phase 1 alone.

4. **Bridge is the seam** — `road_bridge.cpp` is the only place where JS
   meets C++. We can add new IPC channels for RoadV2 without removing
   old ones for Road.

---

## 4. Phase-by-Phase Migration Steps

### Phase 1: Geometry Kernel (Tasks 1.1–1.10)

| Task | What Changes | Legacy Impact | New Code |
|------|-------------|---------------|----------|
| 1.1 GeometrySegment base | Nothing | None | `geometry_segment.hpp` (new file) |
| 1.2 LineSegment | Nothing | None | `geometry_segment.hpp` (add) |
| 1.3 ArcSegment | Nothing (arc.hpp stays) | None | `geometry_segment.hpp` (add) |
| 1.4 SpiralSegment | Nothing (clothoid.hpp stays) | None | `geometry_segment.hpp` (add) |
| 1.5 BezierSegment | Nothing | None | `geometry_segment.hpp` (add) |
| 1.6 Adaptive sampling | Nothing | None | `geometry_segment.hpp` (add) |
| 1.7 s/t coordinate | Nothing | None | `st_coords.hpp` (new file) |
| 1.8 RoadV2 + adapter | Road gets `toV2()` method | Additive | `road_v2.hpp` (new file) |
| 1.9 Bridge updates | parseRoad/roadToJs gain optional V2 fields | Additive | `road_bridge.cpp` (extend) |
| 1.10 TS types | Road gains optional `segments?` field | Additive | `types.ts` (extend) |

**After Phase 1:** Both models coexist. C++ functions can accept either.
Legacy path is unchanged. New path is available but not required.

### Phase 2: Lane Engine (Tasks 2.1–2.8)

| Task | What Changes | Legacy Impact | New Code |
|------|-------------|---------------|----------|
| 2.1 LaneSection/Lane structs | Nothing | None | `lane_engine.hpp` (new file) |
| 2.2 Polynomial lane width | Nothing | None | `lane_engine.hpp` (add) |
| 2.3 Lane boundary generation | Nothing | None | `lane_engine.hpp` (add) |
| 2.4 Lane centerline generation | Nothing | None | `lane_engine.hpp` (add) |
| 2.5 Lane marking generation | Nothing | None | `lane_engine.hpp` (add) |
| 2.6 Lane-level mesh | mesh.hpp gains `generateRoadMeshV2()` | Additive | `mesh.hpp` (extend) |
| 2.7 Debug overlays for lanes | RoadViewport gains lane overlay option | Additive | `RoadViewport.tsx` (extend) |
| 2.8 TS types for lanes | Road gains optional `laneSections?` | Additive | `types.ts` (extend) |

**After Phase 2:** Lane-level geometry available. Legacy width/laneCount
still works (adapter synthesizes LaneSection from width/laneCount).

### Phase 3: Road Graph & Junction (Tasks 3.1–3.7)

| Task | What Changes | Legacy Impact | New Code |
|------|-------------|---------------|----------|
| 3.1 RoadGraph class | Nothing | None | `road_graph.hpp` (new file) |
| 3.2 RoadLink auto-detection | Nothing | None | `road_graph.hpp` (add) |
| 3.3 Junction uses RoadGraph | intersection.hpp gains `generateIntersectionV2()` | Additive | `intersection.hpp` (extend) |
| 3.4 Lane-level junction connections | Nothing | None | `intersection.hpp` (add) |
| 3.5–3.6 T/Y/5-way support | Test cases | None | Tests |
| 3.7 Junction validation | Nothing | None | `validation.hpp` (new file) |

**After Phase 3:** Road graph available. Legacy intersection still works.
New intersection uses graph for approach detection.

### Cutover Phase (after Phase 3)

| Task | What Changes | Legacy Impact |
|------|-------------|---------------|
| Store produces RoadV2 | roadStudioStore uses segments | **Breaking** — requires migration |
| Bridge requires RoadV2 | parseRoad requires segments | **Breaking** |
| Remove legacy Road | Delete ControlPoint, old sampleCenterline | **Breaking** |
| Rename RoadV2 → Road | Final cleanup | **Breaking** |

**Cutover is a single coordinated release.** All tests must pass on both
paths before cutover. After cutover, legacy code is removed.

---

## 5. Rollback Plan

### Before Cutover

- Every phase is independently shippable
- Legacy path is always available
- If a phase introduces a regression, revert that phase's commit
- No data migration needed (stored roads are always TS Road with ControlPoint[])

### After Cutover

- If cutover has issues, revert to last pre-cutover commit
- Stored road data (JSON) is still ControlPoint[]-based
- The adapter (roadToV2) can always reconstruct RoadV2 from legacy data
- No data loss possible — the source of truth is the TypeScript store

### Data Compatibility

- Road JSON files saved before migration: contain `points[]`, `width`, `laneCount`
- Road JSON files saved after migration: contain `segments[]`, `laneSections[]`
  (plus `points[]` for backward compat during transition)
- Loader detects format and uses appropriate path
- No migration script needed — adapter handles it at load time

---

## 6. Decisions (Finalized)

### Q1: unique_ptr with clone() — DECIDED

**Decision:** Use `std::vector<std::unique_ptr<GeometrySegment>>`.

**Gotcha addressed:** The undo/redo system snapshots Road by copying it
(section 6.9 of architecture doc). `vector<unique_ptr>` makes RoadV2
non-copyable by default. Therefore:

- `GeometrySegment` has a `virtual std::unique_ptr<GeometrySegment> clone() const = 0` method
- `RoadV2` has a custom copy constructor that deep-clones all segments
- `RoadV2` has a custom copy assignment operator (same)
- This must be in place **before Phase 1.1 ships** — flag it explicitly or
  undo/redo will break mid-Phase-3 when the store starts producing RoadV2

### Q2: Absolute bezier control points — DECIDED

**Decision:** BezierSegment stores absolute control points (P0, P1, P2, P3).

The editor UI works in relative handle offsets (handleIn/handleOut relative
to position). The adapter (`roadToV2`) converts relative → absolute at the
boundary. The segment itself never sees relative offsets.

### Q3: s/t internal, reserve IPC channel now — DECIDED

**Decision:** s/t coordinates are internal to C++, not exposed to TypeScript.

**Action:** Reserve the IPC channel name `roadFindAtPoint` now (in the
handler, preload, and client) as a no-op stub, so Phase 3+ features (snapping,
"what road am I near") don't require reshaping the bridge later.

### Q4: Store exact segment parameters as metadata — DECIDED

**Decision:** Don't just tag ControlPoints with `segmentType = "arc"`.
Store the **exact construction parameters** (center, radius, startAngle,
sweepAngle for arcs; A, L, kappa0, kappa1 for clothoids) as metadata on
the ControlPoint group.

**Rationale:** If the adapter re-fits an ArcSegment from N sampled points,
there's precision drift — a fitted arc from noisy samples isn't identical
to the arc that generated them. Since the arc/clothoid tools already know
their exact parameters at creation time, the adapter should **reconstruct
the exact segment**, not infer it.

**Implementation:** Add a `SegmentMetadata` variant/struct to ControlPoint
(or to Road as a per-segment array) that carries the exact parameters.
The adapter uses this to create the correct GeometrySegment subclass with
exact parameters, falling back to fitting only if metadata is absent.

### Q5: doctest for C++ geometry math — DECIDED

**Decision:** Add `doctest` (header-only C++ test framework) for pure
geometry-kernel math tests. Keep vitest-via-bridge for integration tests.

**Rationale:** Pure geometry math (evaluateDS, curvature, arc-length tables)
is fast and cheap to unit test in C++ directly. Round-tripping every test
through IPC/JSON adds latency and noise to failures ("is this a JSON
marshaling bug or a math bug?"). doctest is near-zero setup cost (single
header, no build system changes beyond including it) and won't conflict
with vitest.

**Test split:**
- **doctest (C++):** GeometrySegment subclasses, evaluateDS, curvatureDS,
  arc-length parameterization, adaptive sampling, offset curves, fillet math
- **vitest (TS via bridge):** IPC round-trips, mesh output, store behavior,
  intersection generation end-to-end, OpenDRIVE export

---

## Appendix: File Creation/Modification Summary

### New Files (additive, no changes to existing)

| File | Phase | Purpose |
|------|-------|---------|
| `geometry_segment.hpp` | 1.1 | GeometrySegment base + all subclasses |
| `st_coords.hpp` | 1.7 | s/t coordinate system |
| `road_v2.hpp` | 1.8 | RoadV2 struct + adapter functions |
| `lane_engine.hpp` | 2.1 | LaneSection, Lane, lane boundary generation |
| `road_graph.hpp` | 3.1 | RoadGraph, RoadLink, connectivity |
| `validation.hpp` | 3.7 | Geometry validation framework |
| `doctest.h` | 1.1 | Header-only C++ test framework (doctest v2) |
| `geometry_segment_tests.cpp` | 1.2 | C++ unit tests for geometry segments |

### Modified Files (extend, don't break)

| File | Phase | Changes |
|------|-------|---------|
| `road.hpp` | 1.8 | Add `toV2()` method (additive) |
| `mesh.hpp` | 2.6 | Add `generateRoadMeshV2()` (additive) |
| `intersection.hpp` | 3.3 | Add `generateIntersectionV2()` (additive) |
| `opendrive.hpp` | 3+ | Add V2 export path (additive) |
| `road_bridge.cpp` | 1.9 | Add optional V2 fields in parse/serialize |
| `types.ts` | 1.10 | Add optional `segments?` to Road |
| `roadEngineClient.ts` | 1.9 | Add V2 conversion in toCppRoad |
| `roadStudioStore.ts` | Cutover | Switch to V2 (breaking) |
| `RoadViewport.tsx` | Cutover | Switch to V2 rendering (breaking) |

### Files Deleted (cutover only)

| File | What's removed |
|------|---------------|
| `road.hpp` | ControlPoint struct, old Road methods |
| `types.ts` | ControlPoint interface (replaced by segment-based) |
