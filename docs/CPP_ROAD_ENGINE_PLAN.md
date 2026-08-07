# C++ Road Geometry Engine — Implementation Plan

## Current State (What We Have)

### Already Exists
| Component | Status | Location |
|-----------|--------|----------|
| Native addon build system | Working | `app/native/binding.gyp` |
| N-API bindings | Working | `app/native/src/addon.cpp` |
| C++20 compilation | Configured | `binding.gyp` → `/std:c++20` |
| nlohmann/json | Available | `third_party/nlohmann` |
| node-gyp rebuild script | Working | `npm run rebuild:native` |
| Electron native reload | Working | `scripts/rebuild-native-for-electron.cjs` |
| Session/Datasource/Pipeline bridges | Working | `app/native/src/*_bridge.cpp` |

### What Needs to Be Built
| Component | Lines (est.) | Priority |
|-----------|-------------|----------|
| C++ road geometry engine | ~1500 lines | P0 |
| N-API road bridge | ~300 lines | P0 |
| TypeScript wrapper (async) | ~100 lines | P0 |
| Store migration (call C++ instead of TS) | ~200 lines changed | P1 |
| Python utilities (OSM, GDAL) | ~500 lines | P2 |
| Tests | ~400 lines | P1 |

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Renderer (React + TypeScript)                           │
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ RoadToolbar  │  │ RoadViewport │  │ RoadElevation │  │
│  │ (UI buttons) │  │ (MapLibre +  │  │ Editor        │  │
│  │              │  │  Babylon.js) │  │               │  │
│  └──────┬───────┘  └──────┬───────┘  └───────┬───────┘  │
│         │                 │                  │          │
│         └────────┬────────┴──────────────────┘          │
│                  ▼                                       │
│  ┌──────────────────────────────┐                       │
│  │ roadStudioStore (Zustand)    │                       │
│  │                              │                       │
│  │  UI state, selection,       │                       │
│  │  undo/redo, tool mode       │                       │
│  │                              │                       │
│  │  Geometry calls ───────────────► window.roadEngine  │
│  └──────────────────────────────┘        │              │
│                                          │              │
└──────────────────────────────────────────┼──────────────┘
                                           │
                           IPC (preload)   │
                                           │
┌──────────────────────────────────────────┼──────────────┐
│  Electron Main Process                   │              │
│                                          │              │
│  ┌───────────────────────────────────────┴──────────┐   │
│  │  roadEngineHandler (IPC)                         │   │
│  │                                                   │   │
│  │  roadEngine.generateIntersection(road1, road2)  │   │
│  │  roadEngine.computeCircleArc(...)                │   │
│  │  roadEngine.computeClothoid(...)                 │   │
│  │  roadEngine.trimRoads(...)                       │   │
│  │  roadEngine.generateMesh(...)                    │   │
│  │                                                   │   │
│  │  ┌─────────────────────────────────────────┐     │   │
│  │  │  C++ Native Addon (geoterrain_native)   │     │   │
│  │  │                                          │     │   │
│  │  │  road_bridge.cpp                         │     │   │
│  │  │  ├── generateIntersection()              │     │   │
│  │  │  ├── computeCircleArc()                  │     │   │
│  │  │  ├── computeClothoid()                   │     │   │
│  │  │  ├── trimRoads()                         │     │   │
│  │  │  ├── generateMesh()                      │     │   │
│  │  │  ├── generateLaneGraph()                 │     │   │
│  │  │  └── detectIntersections()               │     │   │
│  │  │                                          │     │   │
│  │  │  road/                                   │     │   │
│  │  │  ├── geometry.hpp         (Point, Line)  │     │   │
│  │  │  ├── road.hpp             (Road model)   │     │   │
│  │  │  ├── intersection.hpp     (Junction gen) │     │   │
│  │  │  ├── arc.hpp              (Circular arc) │     │   │
│  │  │  ├── clothoid.hpp         (Euler spiral) │     │   │
│  │  │  ├── fillet.hpp           (Fillet arc)   │     │   │
│  │  │  ├── mesh.hpp             (Tessellation) │     │   │
│  │  │  └── lane_graph.hpp       (Connectivity) │     │   │
│  │  └─────────────────────────────────────────┘     │   │
│  └───────────────────────────────────────────────────┘   │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Python Utilities (optional, P2)                 │    │
│  │  ├── OSM import (osmnx)                          │    │
│  │  ├── GDAL terrain processing                     │    │
│  │  └── GeoTIFF conversion                          │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

---

## What Changes (Line by Line)

### Files That DON'T Change (stay TypeScript)
| File | Lines | Why it stays |
|------|-------|-------------|
| `RoadToolbar.tsx` | 286 | Pure UI (buttons, icons) |
| `RoadElevationEditor.tsx` | 441 | Pure UI (chart editor) |
| `RoadStudioWorkspace.tsx` | 146 | Pure UI (layout) |
| `RoadViewport.tsx` (rendering) | ~1500 | MapLibre + Babylon.js rendering |
| `roadStudioStore.ts` (UI state) | ~500 | Selection, undo/redo, tool mode |

### Files That DO Change
| File | Current | After | What changes |
|------|---------|-------|-------------|
| `types.ts` | 1259 lines | ~350 lines | Remove geometry functions, keep only type definitions |
| `roadStudioStore.ts` | 769 lines | ~650 lines | Replace direct geometry calls with `window.roadEngine.*` |
| `RoadViewport.tsx` | 1774 lines | ~1700 lines | Replace `sampleRoad()` calls with C++ mesh data |

### New Files (C++)
| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `app/native/src/road_bridge.cpp` | 300 | N-API bindings for road geometry |
| `app/native/src/road/geometry.hpp` | 150 | Point2D, Line, Vector math |
| `app/native/src/road/road.hpp` | 100 | Road data model (C++ struct) |
| `app/native/src/road/intersection.hpp` | 350 | Edge-based intersection generation |
| `app/native/src/road/arc.hpp` | 150 | Circular arc computation |
| `app/native/src/road/clothoid.hpp` | 200 | Clothoid (Euler spiral) |
| `app/native/src/road/fillet.hpp` | 100 | Fillet arc between two lines |
| `app/native/src/road/mesh.hpp` | 200 | Mesh tessellation |
| `app/native/src/road/lane_graph.hpp` | 150 | Lane connectivity graph |
| `app/handlers/roadEngineHandler.ts` | 100 | IPC handler (Electron main) |
| `renderer/preload/roadEngine.ts` | 50 | Preload bridge (expose to renderer) |
| **Total new C++** | **~1600** | |
| **Total new TS** | **~150** | |

### Summary of Changes

```
BEFORE:                              AFTER:
┌──────────────────────┐             ┌──────────────────────┐
│ types.ts (1259 lines)│             │ types.ts (350 lines) │
│  - Types             │             │  - Types only        │
│  - Geometry (900)    │             │                      │
│  - Arc (150)         │  ──────►    │ ┌──────────────────┐ │
│  - Intersection (400)│             │ │ C++ Engine       │ │
│  - Mesh (200)        │             │ │ (1600 lines)     │ │
│  - Lane graph (150)  │             │ │  - Intersection  │ │
│                      │             │ │  - Arc           │ │
│ roadStudioStore.ts   │             │ │  - Clothoid      │ │
│  - Calls TS geometry │             │ │  - Mesh          │ │
│                      │             │ │  - Lane graph    │ │
│ RoadViewport.tsx     │             │ └──────────────────┘ │
│  - Calls TS geometry │             │                      │
│                      │             │ roadStudioStore.ts   │
│                      │             │  - Calls C++ via IPC │
│                      │             │                      │
│                      │             │ RoadViewport.tsx     │
│                      │             │  - Uses C++ mesh     │
└──────────────────────┘             └──────────────────────┘

Lines of code:
  TypeScript geometry:  900  ──►  0    (moved to C++)
  C++ geometry:           0  ──►  1600 (new)
  TypeScript types:     350  ──►  350  (unchanged)
  TypeScript UI:       3200  ──►  3200 (unchanged)
  IPC bridge:             0  ──►  150  (new)
  ─────────────────────────────────────
  Total:               4450  ──►  5300  (+850 net, but geometry is 50x faster)
```

---

## Phase-by-Phase Plan

### Phase 0: Setup (1 day)

**Goal:** Add road bridge to existing native addon

**Tasks:**
1. Create `app/native/src/road/` directory
2. Create `app/native/src/road_bridge.cpp`
3. Update `binding.gyp` to include new source files
4. Add `getVersion()` test function for road engine
5. Build and verify `node-gyp rebuild` works
6. Test: `require('./build/Release/geoterrain_native.node').roadGetVersion()`

**Files changed:**
```
app/native/binding.gyp                    (add road_bridge.cpp to sources)
app/native/src/addon.cpp                  (add InitRoadBridge)
app/native/src/road_bridge.cpp            (new — stub with getVersion)
app/native/src/road/geometry.hpp          (new — Point2D, Vec2, math)
```

**Deliverable:** C++ native addon builds with road bridge stub

---

### Phase 1: Core Geometry (3 days)

**Goal:** Port all geometry functions from TypeScript to C++

**Tasks:**
1. Implement `geometry.hpp` — Point2D, Vector math, line intersection
2. Implement `road.hpp` — Road struct (points, width, lanes)
3. Implement `arc.hpp` — `computeCircleArc()` (port from TS)
4. Implement `fillet.hpp` — `filletArc()` (true circular arc)
5. Implement `intersection.hpp` — `generateIntersection()` (edge-based)
6. Implement `lane_graph.hpp` — `generateLaneConnections()`
7. Expose all via `road_bridge.cpp` N-API bindings
8. Write C++ unit tests

**C++ Functions to implement (mapping from TS):**

| TypeScript function | C++ function | TS lines | C++ lines (est.) |
|---------------------|-------------|----------|-------------------|
| `geoToLocal()` | `geoToLocal()` | 12 | 10 |
| `localToGeo()` | `localToGeo()` | 12 | 10 |
| `sampleRoad()` | `sampleRoad()` | 33 | 30 |
| `segmentIntersection()` | `segmentIntersection()` | 22 | 20 |
| `lineIntersection()` | `lineIntersection()` | 15 | 15 |
| `computeCircleArc()` | `computeCircleArc()` | 150 | 120 |
| `filletArc()` | `filletArc()` | 76 | 60 |
| `computeTangentAt()` | `computeTangentAt()` | 12 | 10 |
| `generateIntersection()` | `generateIntersection()` | 200 | 180 |
| `generateEdgeBasedPolygon()` | `generateEdgeBasedPolygon()` | 100 | 90 |
| `buildApproachCenterline()` | `buildApproachCenterline()` | 50 | 45 |
| `generateLaneConnections()` | `generateLaneConnections()` | 60 | 50 |
| `detectIntersections()` | `detectIntersections()` | 90 | 80 |
| `bezierPoint()` | `bezierPoint()` | 38 | 30 |
| **Total** | | **~900** | **~750** |

**Files changed:**
```
app/native/src/road/geometry.hpp          (Point2D, math)
app/native/src/road/road.hpp              (Road model)
app/native/src/road/arc.hpp               (Circular arc)
app/native/src/road/fillet.hpp            (Fillet arc)
app/native/src/road/intersection.hpp      (Intersection generation)
app/native/src/road/lane_graph.hpp        (Lane connectivity)
app/native/src/road_bridge.cpp            (N-API bindings — full)
app/native/binding.gyp                    (add all road/*.hpp)
```

**Deliverable:** All geometry functions work in C++, callable from Node.js

---

### Phase 2: IPC Bridge (2 days)

**Goal:** Wire C++ engine to Electron renderer

**Tasks:**
1. Create `app/handlers/roadEngineHandler.ts` — IPC handlers
2. Create `renderer/preload/roadEngine.ts` — preload bridge
3. Update `electron preload` to expose `window.roadEngine`
4. Create TypeScript wrapper with same interface as current functions
5. Test: call `window.roadEngine.generateIntersection()` from renderer

**Architecture:**
```
Renderer calls:
  window.roadEngine.generateIntersection(road1, road2)
        │
        ▼
  IPC: 'road:generateIntersection'
        │
        ▼
  roadEngineHandler.ts:
    const result = nativeAddon.roadGenerateIntersection(road1, road2)
    return result
        │
        ▼
  road_bridge.cpp:
    Road r1 = parseRoad(args[0])
    Road r2 = parseRoad(args[1])
    IntersectionResult result = generateIntersection(r1, r2)
    return serializeResult(result)
```

**Files changed:**
```
app/handlers/roadEngineHandler.ts         (new — IPC handlers)
renderer/preload/roadEngine.ts            (new — preload bridge)
renderer/preload/index.ts                 (add roadEngine to contextBridge)
app/main.ts                               (register roadEngineHandler)
```

**Deliverable:** Renderer can call C++ geometry functions via `window.roadEngine.*`

---

### Phase 3: Migrate Store (2 days)

**Goal:** Replace TypeScript geometry calls with C++ calls

**Tasks:**
1. Update `detectIntersection()` to call `window.roadEngine.generateIntersection()`
2. Update `startArc()` / `updateArcPreview()` / `finishArc()` to call C++ arc
3. Update `recomputeIntersections()` to call C++ detection
4. Update `createIntersectionAtClosestPoint()` to call C++
5. Remove geometry function imports from `types.ts`
6. Keep type definitions in `types.ts` (interfaces only)
7. Test: all tools work identically with C++ backend

**Before (TypeScript):**
```typescript
import { generateIntersection, computeCircleArc } from '../shared/types';

detectIntersection: (roadId1, roadId2) => {
  const generated = generateIntersection(road1, road2, refLat, refLon);
  // ... trim roads, create intersection object
}
```

**After (C++ via IPC):**
```typescript
// No geometry imports — types only
import type { Road, GeneratedIntersection, CircleArc } from '../shared/types';

detectIntersection: async (roadId1, roadId2) => {
  const generated = await window.roadEngine.generateIntersection(
    road1, road2, refLat, refLon
  );
  // ... trim roads, create intersection object (same UI logic)
}
```

**Files changed:**
```
modules/road-studio/client/store/roadStudioStore.ts  (200 lines changed)
modules/road-studio/shared/types.ts                  (remove ~900 lines of geometry)
```

**Deliverable:** All geometry runs in C++, TypeScript only handles UI state

---

### Phase 4: Clothoid (2 days)

**Goal:** Add clothoid (Euler spiral) — impossible in TypeScript, easy in C++

**Tasks:**
1. Implement `clothoid.hpp` — Fresnel integrals, clothoid fitting
2. Add `computeClothoid()` to `road_bridge.cpp`
3. Add "Clothoid" tool button to toolbar
4. Implement clothoid drawing interaction (like arc but with gradual curvature)
5. Test: smooth transitions for highway design

**C++ Clothoid Algorithm:**
```cpp
// Clothoid: curvature varies linearly with arc length
// κ(s) = κ0 + s/A²
// where A = clothoid parameter, s = arc length, κ0 = initial curvature
//
// Position:
// x(s) = ∫₀ˢ cos(κ0·t + t²/(2A²)) dt
// y(s) = ∫₀ˢ sin(κ0·t + t²/(2A²)) dt
//
// Computed via Fresnel integrals:
// C(t) = ∫₀ᵗ cos(πu²/2) du
// S(t) = ∫₀ᵗ sin(πu²/2) du
```

**Files changed:**
```
app/native/src/road/clothoid.hpp          (new — Fresnel integrals)
app/native/src/road_bridge.cpp            (add computeClothoid binding)
app/handlers/roadEngineHandler.ts         (add clothoid IPC handler)
modules/road-studio/client/RoadToolbar.tsx (add Clothoid button)
modules/road-studio/client/store/roadStudioStore.ts (add clothoid state)
modules/road-studio/client/RoadViewport.tsx (add clothoid preview)
modules/road-studio/shared/types.ts       (add Clothoid type)
```

**Deliverable:** Clothoid tool works — smooth curvature transitions

---

### Phase 5: Mesh Tessellation (2 days)

**Goal:** Move mesh generation to C++ for 10x performance

**Tasks:**
1. Implement `mesh.hpp` — polygon triangulation, road mesh generation
2. Add `generateRoadMesh()` to `road_bridge.cpp`
3. Update `RoadViewport.tsx` 3D rendering to use C++ mesh data
4. Return vertex buffer + index buffer via IPC
5. Test: 100k triangle mesh in <5ms

**C++ Mesh Output:**
```cpp
struct MeshData {
    std::vector<float> vertices;  // x, y, z interleaved
    std::vector<float> normals;   // nx, ny, nz
    std::vector<float> uvs;       // u, v
    std::vector<uint32_t> indices; // triangle indices
};
```

**Files changed:**
```
app/native/src/road/mesh.hpp             (new — tessellation)
app/native/src/road_bridge.cpp           (add generateRoadMesh)
app/handlers/roadEngineHandler.ts        (add mesh IPC handler)
modules/road-studio/client/RoadViewport.tsx (use C++ mesh data)
```

**Deliverable:** 3D road meshes generated in C++, rendered in Babylon.js

---

### Phase 6: OpenDRIVE I/O (3 days)

**Goal:** Read and write OpenDRIVE (.xodr) files in C++

**Tasks:**
1. Implement `opendrive.hpp` — OpenDRIVE XML parser/writer
2. Add `exportOpenDrive()` and `importOpenDrive()` to bridge
3. Add export button to toolbar
4. Test: export road network to .xodr, import back

**Files changed:**
```
app/native/src/road/opendrive.hpp        (new — XML read/write)
app/native/src/road_bridge.cpp           (add export/import)
app/handlers/roadEngineHandler.ts        (add OpenDRIVE IPC)
modules/road-studio/client/RoadToolbar.tsx (add export button)
```

**Deliverable:** Full OpenDRIVE round-trip (export → import)

---

### Phase 7: Python Utilities (optional, 3 days)

**Goal:** Add Python for OSM import and GIS preprocessing

**Tasks:**
1. Create `road_engine_python/` project
2. Implement OSM → road network converter (using osmnx)
3. Implement GeoTIFF/DEM processing (using GDAL)
4. Set up Python sidecar (ZeroMQ or subprocess)
5. Add "Import OSM" button to toolbar

**Files changed:**
```
road_engine_python/server.py             (new — Python sidecar)
road_engine_python/osm_import.py         (new — OSM converter)
road_engine_python/dem_processor.py      (new — DEM processing)
app/handlers/pythonEngineHandler.ts      (new — Python IPC)
modules/road-studio/client/RoadToolbar.tsx (add Import OSM button)
```

**Deliverable:** OSM data can be imported as road networks

---

## Timeline

| Phase | Duration | Cumulative | Deliverable |
|-------|----------|------------|-------------|
| 0: Setup | 1 day | 1 day | C++ builds with road stub |
| 1: Core Geometry | 3 days | 4 days | All geometry in C++ |
| 2: IPC Bridge | 2 days | 6 days | Renderer calls C++ |
| 3: Migrate Store | 2 days | 8 days | TS geometry removed |
| 4: Clothoid | 2 days | 10 days | Clothoid tool works |
| 5: Mesh Tessellation | 2 days | 12 days | 3D mesh from C++ |
| 6: OpenDRIVE I/O | 3 days | 15 days | .xodr export/import |
| 7: Python Utilities | 3 days | 18 days | OSM import |

**Total: ~15-18 working days (3-4 weeks)**

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| C++ build issues on Windows | Medium | High | Already have working build system |
| N-API serialization overhead | Low | Medium | Use JSON (nlohmann) — fast enough |
| Async IPC complexity | Medium | Low | Make store actions async |
| Clothoid math complexity | Medium | Low | Use known Fresnel formulas |
| OpenDRIVE spec complexity | High | Medium | Start with basic format, expand later |
| Team C++ skills | Unknown | High | Start with simple functions, build up |

---

## What Stays in TypeScript (Forever)

| Component | Why |
|-----------|-----|
| React UI components | React is the best UI framework |
| MapLibre 2D rendering | GPU-accelerated, no C++ needed |
| Babylon.js 3D rendering | GPU-accelerated, no C++ needed |
| Zustand store (UI state) | Selection, undo/redo, tool mode |
| Mouse/keyboard handlers | DOM events, not geometry |
| Road preview (lightweight) | Simple line drawing for preview |

## What Moves to C++ (Permanently)

| Component | Why |
|-----------|-----|
| Intersection generation | Complex geometry, needs precision |
| Circular arc computation | Math-heavy, benefits from C++ speed |
| Clothoid (Euler spiral) | Requires Fresnel integrals — impractical in TS |
| Road trimming/splitting | Geometry operations |
| Mesh tessellation | 100k+ triangles — needs C++ speed |
| Lane connectivity graph | Graph algorithms |
| OpenDRIVE I/O | XML parsing + geometry conversion |
| Batch intersection detection | 1000+ roads — needs C++ speed |

## What Uses Python (Optional)

| Component | Why |
|-----------|-----|
| OSM import | osmnx library is Python-only |
| GeoTIFF/DEM processing | GDAL Python bindings |
| Terrain analysis | scipy, numpy |
| AI/ML utilities | Python ecosystem |

---

## First Step (Tomorrow)

Start with **Phase 0: Setup** — just get the C++ road bridge stub building:

1. Create `app/native/src/road/geometry.hpp` with `Point2D` struct
2. Create `app/native/src/road_bridge.cpp` with `getVersion()` stub
3. Update `binding.gyp` to add `road_bridge.cpp`
4. Update `addon.cpp` to call `InitRoadBridge()`
5. Run `npm run rebuild:native`
6. Test: `node -e "console.log(require('./build/Release/geoterrain_native.node').roadGetVersion())"`

This takes ~2 hours and proves the pipeline works before writing any real geometry.
