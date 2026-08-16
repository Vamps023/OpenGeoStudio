# Road Engine — New Architecture Design

> Inspired by esmini's RoadManager, OpenDRIVE concepts, and professional road editors
> (SCANeR Studio, RoadRunner, Bentley OpenRoads, Autodesk Civil 3D).
> All code is original — no esmini code is copied.

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [esmini Architecture Analysis](#2-esmini-architecture-analysis)
3. [Comparison with Our Current Implementation](#3-comparison-with-our-current-implementation)
4. [New Architecture Design](#4-new-architecture-design)
5. [esmini-to-Ours Mapping](#5-esmini-to-ours-mapping)
6. [Subsystem Specifications](#6-subsystem-specifications)
7. [Implementation Roadmap](#7-implementation-roadmap)
8. [Class Diagrams](#8-class-diagrams)
9. [Data Flow Diagrams](#9-data-flow-diagrams)

---

## 1. Executive Summary

### Goal

Redesign the road engine to produce mathematically correct road geometry,
junctions, and meshes comparable to professional road-authoring tools.

### Key Findings from esmini Analysis

| esmini Strength | Our Current Status | Action |
|----------------|-------------------|--------|
| Polymorphic geometry (Line/Arc/Spiral/Poly3/ParamPoly3) | Flat control points with bezier | **Adopt polymorphic geometry** |
| Reference line + lane sections | Single centerline + width | **Add lane sections** |
| Adaptive sampling (curvature-based) | Fixed sample count | **Implement adaptive sampling** |
| Dual coordinate storage (world + road s/t) | Geo (lat/lon) only | **Add s/t coordinate system** |
| Explicit junction connections | Implicit intersection detection | **Add junction graph** |
| Lane-level connectivity (LaneLink) | No lane links | **Add lane links** |
| Polynomial-based attributes (width, elevation) | Fixed width | **Add polynomial attributes** |
| Separate road data from mesh generation | Mixed concerns | **Separate concerns** |
| Dijkstra pathfinding | No pathfinding | **Add road graph pathfinding** |
| AABB spatial indexing | Linear search | **Add spatial index** |

### What We Keep

- React/Electron architecture (UI separation)
- C++ header-only geometry kernel (no external deps)
- MapLibre 2D + Babylon.js 3D dual rendering
- Zustand state management
- OpenDRIVE export
- Debug mode with construction overlays

---

## 2. esmini Architecture Analysis

### 2.1 Overall Architecture

```
┌─────────────────────────────────────────────────┐
│                  Applications                     │
│  (esmini, odrviewer, RoadManagerCLI)             │
├─────────────────────────────────────────────────┤
│                   Libraries                       │
│  (esminiLib, esminiRMLib, esminiJS, esminiROS)   │
├─────────────────────────────────────────────────┤
│                    Modules                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ RoadMgr  │ │ Scenario │ │ Viewer   │         │
│  │ (571KB)  │ │ Engine   │ │ Base     │         │
│  └──────────┘ └──────────┘ └──────────┘         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ CommonMini│ │Controllers│ │PlayerBase│        │
│  │ (math)   │ │          │ │          │         │
│  └──────────┘ └──────────┘ └──────────┘         │
├─────────────────────────────────────────────────┤
│                 externals                         │
│  (OSG, TinyXML2, GoogleTest, pugixml)            │
└─────────────────────────────────────────────────┘
```

### 2.2 RoadManager Key Classes

```
Geometry (abstract)
├── Line          — straight segment
├── Arc           — constant curvature
├── Spiral        — linear curvature change (clothoid)
├── Poly3         — cubic polynomial v=f(u)
└── ParamPoly3    — parametric cubic (u(p), v(p))

Road
├── geometry[]        — reference line segments
├── lane_section[]    — lane configurations along road
├── elevation[]       — vertical profile (polynomial)
├── super_elevation[] — banking (polynomial)
├── lane_offset[]     — reference line lateral shift
├── link[]            — predecessor/successor connections
└── signals[], objects[]

LaneSection
└── lane[]            — left (+id), center (0), right (-id)

Lane
├── width[]           — polynomial width vs s
├── road_mark[]       — lane markings
├── link              — predecessor/successor lane
└── type              — driving/shoulder/sidewalk/parking

Junction
├── connection[]      — incoming→connecting road
│   └── lane_link[]   — lane-to-lane mapping
└── controller[]      — traffic signal controller

Position
├── World coords (x, y, z, h, p, r)
├── Track coords (road_id, s, t)
└── Lane coords (road_id, lane_id, s, offset)
```

### 2.3 Key Algorithms

| Algorithm | Purpose | Complexity |
|-----------|---------|------------|
| EvaluateDS(ds) | Position at distance ds along geometry | O(1) per segment |
| Adaptive sampling | Curvature-based point generation | O(n) with refinement |
| XYZ2TrackPos | World → road coordinate conversion | O(roads × points) |
| Track2XYZ | Road → world coordinate conversion | O(log segments) |
| Dijkstra pathfinding | Shortest path through road network | O(E + V log V) |
| Ear-clipping triangulation | Polygon → triangles | O(n²) |
| Fresnel integrals | Clothoid spiral computation | O(1) (polynomial approx) |
| AABB tree intersection | Spatial collision detection | O(log n) query |

### 2.4 Mesh Generation (ViewerBase/roadgeom.cpp)

- Triangle strips between adjacent lane boundaries
- Adaptive sampling with error tolerance (0.25m horizontal, 0.1m vertical)
- World-space UV mapping (texture repeats every 2m)
- OSG SmoothingVisitor for normals
- Material per lane type (asphalt, concrete, grass, border)
- Polygon offsets to prevent z-fighting
- LOD: 3000m for entities, 500m for road features
- **No special junction mesh** — junctions handled as regular roads

---

## 3. Comparison with Our Current Implementation

### 3.1 Architecture Comparison

| Aspect | esmini | OpenGeoStudio (current) | Gap |
|--------|--------|------------------------|-----|
| Geometry model | Polymorphic classes (Line/Arc/Spiral/Poly3) | Flat ControlPoint[] with bezier handles | **Major** |
| Road representation | Geometry segments + LaneSections | ControlPoint[] + width + laneCount | **Major** |
| Lane model | Per-lane width (polynomial), type, markings | Single width / laneCount | **Major** |
| Junction model | Explicit Connection[] with LaneLink[] | GeneratedIntersection (polygon + approaches) | **Moderate** |
| Coordinate system | World (XYZ) + Road (s,t) + Lane (id,s,offset) | Geo (lat/lon) + Local (x,y) | **Major** |
| Sampling | Adaptive (curvature-based, 0.01m tolerance) | Fixed count (32 samples) | **Moderate** |
| Mesh generation | Triangle strips, lane-based, OSG | Triangle strip, road-level, custom | **Moderate** |
| Spatial index | AABB tree | None (linear search) | **Moderate** |
| Pathfinding | Dijkstra + lane-independent router | None | **Minor** (not needed for editor) |
| Rendering | OpenSceneGraph (C++) | MapLibre (2D) + Babylon.js (3D) | **Different** (not a gap) |
| File format | OpenDRIVE (full read/write) | OpenDRIVE (export only) | **Moderate** |
| Road editing | Not an editor (viewer/simulator) | Interactive editing (add/move/delete points) | **Our advantage** |
| Real-time preview | Not applicable | Yes (arc/clothoid/polyline preview) | **Our advantage** |
| Debug visualization | OSI point visualization | Construction overlays (14 layers) | **Our advantage** |

### 3.2 Missing Systems

| Missing System | Impact | Priority |
|---------------|--------|----------|
| **Polymorphic geometry** | Can't represent arcs/clothoids as native primitives | P0 |
| **Lane sections** | Can't change lane config along road | P0 |
| **s/t coordinate system** | Can't query position along road | P0 |
| **Lane-level geometry** | No per-lane boundaries/markings | P1 |
| **Superelevation** | No realistic curve banking | P1 |
| **Road graph (connectivity)** | No road-to-road navigation | P1 |
| **Spatial index** | Slow queries for large networks | P1 |
| **Adaptive sampling** | Fixed sampling is inefficient | P1 |
| **Polynomial attributes** | Can't vary width/elevation smoothly | P2 |
| **OpenDRIVE import** | Can't load existing road networks | P2 |
| **Lane markings** | No visual lane lines | P2 |
| **Validation** | No geometric quality checks | P2 |

### 3.3 Incorrect Architecture

| Issue | Current | Correct |
|-------|---------|---------|
| Road as ControlPoint[] | Bezier-only representation | Geometry segments (Line/Arc/Spiral) |
| Intersection as polygon | Generated from centerline | Generated from road boundaries + graph |
| Fixed sample count | 32 samples always | Adaptive based on curvature |
| No s-coordinate | Position = index into points | Position = (road_id, s, t) |
| Mesh = road-level | One strip per road | One strip per lane |
| No road connectivity | Roads are independent | Road graph with links |

### 3.4 Duplicate/Legacy Code

| Code | Location | Action |
|------|----------|--------|
| TypeScript geometry (geoToLocal, etc.) | types.ts | Keep for UI, delegate to C++ |
| Old arc preview (TS) | RoadViewport.tsx | Replace with C++ preview |
| Manual edge computation | RoadViewport.tsx (debug) | Move to C++ engine |
| Intersection detection (TS) | types.ts (detectIntersections) | Replace with C++ road graph |

---

## 4. New Architecture Design

### 4.1 Overview

```
React UI (RoadViewport, RoadToolbar, RoadStudioWorkspace)
      │
      ▼
Electron IPC (roadEngineHandler.ts, roadEngineClient.ts)
      │
      ▼
Road Engine (C++ — app/native/src/road/)
      │
      ├── Geometry Kernel       (geometry.hpp)
      ├── Road Graph             (road_graph.hpp)     ← NEW
      ├── Lane Engine            (lane_engine.hpp)    ← NEW
      ├── Junction Engine        (intersection.hpp)
      ├── Mesh Generator         (mesh.hpp)
      ├── Spatial Index          (spatial_index.hpp)  ← NEW
      ├── Undo/Redo              (history.hpp)        ← NEW
      └── OpenDRIVE I/O          (opendrive.hpp)
```

### 4.2 Design Principles

1. **Polymorphic geometry** — Each road segment is a Geometry subclass (Line, Arc, Spiral, Bezier)
2. **Reference line + lanes** — Road = reference line + lane sections (not just width)
3. **s/t coordinate system** — Every position on a road has (road_id, s, t)
4. **Road graph** — Roads connect via RoadLinks (predecessor/successor/junction)
5. **Junction = graph node** — Junctions connect roads, compute trim/fillet/polygon
6. **Separation of concerns** — Road data ≠ Mesh generation ≠ Rendering
7. **Adaptive sampling** — Curvature-based, not fixed count
8. **Original code** — No esmini code copied; algorithms inspired by OpenDRIVE spec

### 4.3 New C++ Header Structure

```
app/native/src/road/
├── geometry.hpp           — Math kernel (existing, extend)
├── road.hpp               — Road data model (existing, refactor)
├── road_graph.hpp         — Road network graph (NEW)
├── lane_engine.hpp        — Lane generation and boundaries (NEW)
├── arc.hpp                — Arc geometry (existing)
├── clothoid.hpp           — Clothoid/spiral (existing)
├── intersection.hpp       — Junction generation (existing, refactor)
├── mesh.hpp               — Mesh tessellation (existing, extend)
├── spatial_index.hpp      — AABB tree / grid index (NEW)
├── history.hpp            — Undo/redo state management (NEW)
├── opendrive.hpp          — OpenDRIVE I/O (existing, extend for import)
├── road_tools.hpp         — Road creation tools (existing)
└── validation.hpp         — Geometry validation (NEW)
```

### 4.4 Key New Classes

```cpp
// ─── Polymorphic Geometry (replaces flat ControlPoint[]) ───
class GeometrySegment {
public:
    virtual void evaluateDS(double ds, double& x, double& y, double& h) const = 0;
    virtual double curvatureDS(double ds) const = 0;
    virtual double length() const = 0;
    virtual GeometryType type() const = 0;
};

class LineSegment : public GeometrySegment { ... };
class ArcSegment : public GeometrySegment { ... };
class SpiralSegment : public GeometrySegment { ... };  // clothoid
class BezierSegment : public GeometrySegment { ... };

// ─── Lane Section (replaces flat width/laneCount) ───
struct LaneSection {
    double startS;                    // s-value where section starts
    std::vector<Lane> lanes;          // left (+), center (0), right (-)
};

struct Lane {
    int id;                           // +1, +2, ... (left), -1, -2, ... (right)
    LaneType type;                    // driving, shoulder, sidewalk, etc.
    Polynomial width;                 // width as function of s
    std::vector<LaneRoadMark> marks;  // lane markings
    LaneLink link;                    // predecessor/successor lane
};

// ─── Road Graph (replaces independent roads) ───
class RoadGraph {
    std::map<std::string, RoadNode> nodes_;   // road_id → node
    std::vector<RoadLink> links_;             // connections
public:
    void addRoad(const Road& road);
    void addLink(const RoadLink& link);
    Road* findRoadAtS(const std::string& roadId, double s);
    std::vector<RoadLink> getConnections(const std::string& roadId);
};

struct RoadLink {
    std::string fromRoadId;
    std::string toRoadId;
    ContactPoint fromPoint;  // START or END
    ContactPoint toPoint;
    std::string junctionId;  // if connection via junction
};

// ─── Spatial Index (replaces linear search) ───
class SpatialIndex {
public:
    void insert(const std::string& roadId, const BoundingBox2D& bbox);
    std::vector<std::string> query(const Point2D& point);
    std::vector<std::string> query(const BoundingBox2D& bbox);
};

// ─── Validation ───
struct ValidationReport {
    bool isValid;
    std::vector<ValidationIssue> issues;
};

struct ValidationIssue {
    ValidationLevel level;  // error, warning, info
    std::string code;       // "MIN_RADIUS", "SELF_INTERSECT", etc.
    std::string message;
    std::string roadId;
    double s;                // position on road
};
```

---

## 5. esmini-to-Ours Mapping

| esmini Concept | Our Equivalent | Status | Action |
|----------------|---------------|--------|--------|
| **RoadManager** | C++ Engine (road_bridge.cpp) | Partial | Rebuild with new architecture |
| **Geometry (abstract)** | GeometrySegment (NEW) | Missing | Implement polymorphic geometry |
| **Line** | LineSegment (NEW) | Missing | Implement |
| **Arc** | ArcSegment (NEW) | Partial (arc.hpp) | Refactor to GeometrySegment |
| **Spiral (clothoid)** | SpiralSegment (NEW) | Partial (clothoid.hpp) | Refactor to GeometrySegment |
| **Poly3** | — | Not needed | Skip (we use Bezier) |
| **ParamPoly3** | BezierSegment (NEW) | Partial (bezier in geometry.hpp) | Refactor to GeometrySegment |
| **Road** | Road (road.hpp) | Partial | Add geometry[], lane_section[] |
| **LaneSection** | LaneSection (NEW) | Missing | Implement |
| **Lane** | Lane (NEW) | Missing | Implement with polynomial width |
| **LaneWidth** | Polynomial (NEW) | Missing | Implement |
| **LaneRoadMark** | LaneRoadMark (NEW) | Missing | Implement |
| **LaneLink** | LaneLink (NEW) | Missing | Implement |
| **RoadLink** | RoadLink (NEW) | Missing | Implement |
| **Junction** | GeneratedIntersection (existing) | Partial | Refactor to graph node |
| **Connection** | LaneConnection (existing) | Partial | Add lane-level mapping |
| **JunctionLaneLink** | — | Missing | Implement |
| **Position** | — | Missing | Implement s/t coordinate system |
| **OSIPoints** | Sample cache (TS) | Partial | Move to C++ with adaptive sampling |
| **RoadPath (Dijkstra)** | — | Missing | Implement (for connectivity checks) |
| **LaneIndependentRouter** | — | Not needed | Skip (editor, not simulator) |
| **RoadGeom (mesh)** | mesh.hpp | Partial | Extend to lane-level mesh |
| **ViewerBase (rendering)** | RoadViewport.tsx | Different | Keep our MapLibre+Babylon approach |
| **CommonMini (math)** | geometry.hpp | Partial | Extend with missing functions |
| **odrSpiral (Fresnel)** | clothoid.hpp | Partial | Already have Simpson's rule |
| **AABBTree** | — | Missing | Implement spatial index |
| **OpenDRIVE parsing** | opendrive.hpp (export only) | Partial | Add import (XML parsing) |
| **Adaptive sampling** | — | Missing | Implement curvature-based sampling |
| **Superelevation** | — | Missing | Implement polynomial banking |
| **ElevationProfile** | ControlPoint.z | Partial | Add polynomial elevation |
| **LaneOffset** | — | Missing | Implement lateral reference shift |
| **Material/Friction** | RoadProfile | Partial | Extend profile system |
| **Traffic signals** | — | Missing | Future (not P0) |
| **Objects (buildings, barriers)** | — | Missing | Future (not P0) |
| **Tunnel** | — | Missing | Future (not P0) |

---

## 6. Subsystem Specifications

### 6.1 Geometry Kernel

**Purpose:** Mathematical foundation for all road geometry.

**Current:** `geometry.hpp` with Point2D, Vec2, line/segment intersection, offset, bezier.

**New additions:**
- `GeometrySegment` abstract base class with `evaluateDS()`, `curvatureDS()`, `length()`
- `LineSegment`, `ArcSegment`, `SpiralSegment`, `BezierSegment` implementations
- Arc-length parameterization for all segment types
- Curvature evaluation at any point
- Tangent and normal evaluation at any point
- Adaptive sampling with error tolerance

**Algorithms:**
- Line: linear interpolation (trivial)
- Arc: circular arc with constant curvature
- Spiral: Fresnel integrals (already in clothoid.hpp)
- Bezier: de Casteljau with arc-length lookup table
- Adaptive sampling: binary search refinement based on tangent deviation

### 6.2 Road Model

**Purpose:** Represent roads as sequences of geometry segments with lane sections.

**Current:** `Road` with `ControlPoint[]`, `width`, `laneCount`.

**New:**
```cpp
class Road {
    std::string id, name;
    std::vector<GeometrySegment*> segments;  // reference line
    std::vector<LaneSection> laneSections;    // lane configuration
    std::vector<ElevationProfile> elevation;  // vertical profile
    std::vector<SuperElevation> superelevation; // banking
    RoadLink predecessor, successor;          // connectivity
    // ...
};
```

**Migration:** Existing ControlPoint-based roads are converted to GeometrySegment sequences:
- Two corner points → LineSegment
- Arc tool → ArcSegment
- Clothoid tool → SpiralSegment
- Bezier handles → BezierSegment

### 6.3 Lane Engine

**Purpose:** Generate per-lane boundaries, centerlines, and markings.

**Current:** No per-lane geometry (only road-level width).

**New:**
- For each LaneSection, compute lane boundaries from reference line + lane widths
- Lane width as cubic polynomial: w(s) = a + bs + cs² + ds³
- Lane boundaries = offset curves of reference line
- Lane centerlines = midpoint between adjacent boundaries
- Lane markings (dashed, solid, double) generated along boundaries
- Lane types: driving, shoulder, sidewalk, parking, bike, bus

**Algorithm:**
1. Evaluate reference line at sample points (adaptive)
2. For each sample, compute cumulative lane widths
3. Offset reference line by cumulative width → lane boundary
4. Generate mesh strip between adjacent boundaries

### 6.4 Junction Engine

**Purpose:** Generate intersection polygons, fillets, and meshes from road boundaries.

**Current:** `intersection.hpp` with boundary-based polygon generation (recently fixed).

**New:**
- Junction as graph node in RoadGraph
- Approaches derived from RoadLink connections (not manual detection)
- Trim distances computed from road widths, angles, corner radii
- Polygon built from road boundary intersections + fillet arcs
- Lane connections mapped through junction
- Stop lines and crosswalks generated per approach
- Support for X, T, Y, and 5-way intersections

**Algorithm (already implemented, needs refinement):**
1. Collect approaches from RoadGraph connections
2. Compute left/right boundary lines per approach
3. Sort approaches by angle (CCW)
4. For each adjacent pair: find boundary intersection, compute fillet
5. Build polygon from trim points + fillet arcs
6. Validate polygon (self-intersection, winding, duplicates)
7. Triangulate polygon
8. Generate lane connections

### 6.5 Mesh Generator

**Purpose:** Convert road and junction geometry into triangle meshes for rendering.

**Current:** `mesh.hpp` with road-level triangle strips and ear-clipping triangulation.

**New:**
- **Lane-level mesh:** One triangle strip per lane (not per road)
- **Adaptive sampling:** Curvature-based vertex density
- **UV mapping:** Arc-length along road + lateral across lanes
- **Normals:** Computed from road surface (including superelevation)
- **Lane markings:** Separate mesh with z-offset
- **Sidewalks/curbs:** Additional mesh strips at road edges
- **Junction mesh:** Triangulated polygon with lane connection overlays

### 6.6 Spatial Index

**Purpose:** Fast spatial queries for large road networks.

**Current:** Linear search through all roads.

**New:** Grid-based spatial index (simpler than AABB tree, sufficient for editor):
- Divide bounding box into grid cells
- Each cell stores road IDs that intersect it
- Point query: O(1) cell lookup + O(k) roads in cell
- Box query: O(cells in box) + O(k) roads

### 6.7 Road Graph

**Purpose:** Model road network connectivity for junction generation and validation.

**Current:** Roads are independent; intersections detected by endpoint proximity.

**New:**
```cpp
class RoadGraph {
    std::map<std::string, RoadNode> nodes_;
    std::vector<RoadLink> links_;
    std::map<std::string, JunctionNode> junctions_;
public:
    void addRoad(const Road& road);
    void addLink(const RoadLink& link);
    void addJunction(const JunctionNode& junction);
    std::vector<RoadLink> getOutgoingLinks(const std::string& roadId);
    std::vector<std::string> getConnectedRoads(const std::string& roadId);
    JunctionNode* getJunction(const std::string& roadId);
};
```

### 6.8 Validation

**Purpose:** Check road network for geometric and topological errors.

**New checks:**
- Minimum radius (AASHTO standards)
- Self-intersection (road crosses itself)
- Duplicate roads (same geometry)
- Disconnected network (orphan roads)
- Invalid lane configuration (missing lanes)
- Sharp angles (below minimum)
- Overlapping roads

### 6.9 History (Undo/Redo)

**Purpose:** Track state changes for undo/redo.

**Current:** TypeScript-only undo/redo with road snapshots.

**New:** Move to C++ for performance with large networks:
- Snapshot-based (serialize road state)
- Command pattern (record action + inverse)
- Coalescing (merge consecutive drag operations)

---

## 7. Implementation Roadmap

### Phase 1: Geometry Kernel Refactor (P0)

**Goal:** Replace flat ControlPoint model with polymorphic geometry segments.

| Task | Description | Effort |
|------|-------------|--------|
| 1.1 | Implement `GeometrySegment` abstract base | Small |
| 1.2 | Implement `LineSegment` | Small |
| 1.3 | Refactor `arc.hpp` → `ArcSegment` | Medium |
| 1.4 | Refactor `clothoid.hpp` → `SpiralSegment` | Medium |
| 1.5 | Implement `BezierSegment` with arc-length lookup | Medium |
| 1.6 | Add adaptive sampling (curvature-based) | Medium |
| 1.7 | Add s/t coordinate system | Medium |
| 1.8 | Migrate Road to use `GeometrySegment[]` | Large |
| 1.9 | Update bridge to expose new geometry | Medium |
| 1.10 | Update TypeScript types | Medium |

### Phase 2: Lane Engine (P0)

**Goal:** Generate per-lane geometry, boundaries, and markings.

| Task | Description | Effort |
|------|-------------|--------|
| 2.1 | Implement `LaneSection` and `Lane` structs | Small |
| 2.2 | Implement polynomial lane width | Small |
| 2.3 | Implement lane boundary generation (offset curves) | Medium |
| 2.4 | Implement lane centerline generation | Small |
| 2.5 | Implement lane marking generation | Medium |
| 2.6 | Update mesh generator for lane-level mesh | Large |
| 2.7 | Update debug overlays for lane boundaries | Medium |
| 2.8 | Update TypeScript types for lanes | Medium |

### Phase 3: Road Graph & Junction Refinement (P1)

**Goal:** Model road connectivity; improve junction generation.

| Task | Description | Effort |
|------|-------------|--------|
| 3.1 | Implement `RoadGraph` class | Medium |
| 3.2 | Implement `RoadLink` auto-detection | Medium |
| 3.3 | Refactor junction to use RoadGraph | Large |
| 3.4 | Add lane-level junction connections | Medium |
| 3.5 | Support T-junctions and Y-junctions | Medium |
| 3.6 | Support 5-way intersections | Medium |
| 3.7 | Add junction validation | Small |

### Phase 4: Mesh & Rendering (P1)

**Goal:** Lane-level mesh generation with proper UVs, normals, markings.

| Task | Description | Effort |
|------|-------------|--------|
| 4.1 | Lane-level triangle strip mesh | Medium |
| 4.2 | Adaptive mesh sampling | Medium |
| 4.3 | Arc-length UV mapping | Small |
| 4.4 | Superelevation normals | Medium |
| 4.5 | Lane marking mesh (dashed/solid/double) | Medium |
| 4.6 | Sidewalk/curb mesh | Medium |
| 4.7 | Junction mesh with lane connections | Large |

### Phase 5: Spatial Index & Performance (P1)

**Goal:** Fast queries for large road networks.

| Task | Description | Effort |
|------|-------------|--------|
| 5.1 | Implement grid-based spatial index | Medium |
| 5.2 | Use index for intersection detection | Small |
| 5.3 | Use index for point-in-road queries | Small |
| 5.4 | Use index for snapping | Small |
| 5.5 | Benchmark with 100+ roads | Small |

### Phase 6: Validation & Editing (P2)

**Goal:** Geometric validation and parametric editing.

| Task | Description | Effort |
|------|-------------|--------|
| 6.1 | Implement validation framework | Medium |
| 6.2 | Add minimum radius check | Small |
| 6.3 | Add self-intersection check | Medium |
| 6.4 | Add connectivity check | Small |
| 6.5 | Implement parametric editing (radius, width) | Large |
| 6.6 | Implement road split/merge | Medium |
| 6.7 | Implement advanced snapping | Medium |

### Phase 7: OpenDRIVE Import (P2)

**Goal:** Load existing OpenDRIVE files.

| Task | Description | Effort |
|------|-------------|--------|
| 7.1 | Add XML parser (pugixml or tinyxml2) | Medium |
| 7.2 | Parse OpenDRIVE road elements | Large |
| 7.3 | Parse geometry (line, arc, spiral, poly3) | Medium |
| 7.4 | Parse lane sections and lane widths | Medium |
| 7.5 | Parse junctions and connections | Medium |
| 7.6 | Parse elevation and superelevation | Small |
| 7.7 | Convert to internal Road model | Medium |

### Phase 8: Polish & Testing (P2)

**Goal:** Comprehensive tests and documentation.

| Task | Description | Effort |
|------|-------------|--------|
| 8.1 | Unit tests for all geometry types | Medium |
| 8.2 | Unit tests for lane generation | Medium |
| 8.3 | Unit tests for junction generation | Medium |
| 8.4 | Unit tests for mesh generation | Medium |
| 8.5 | Integration tests (full pipeline) | Medium |
| 8.6 | Performance benchmarks | Small |
| 8.7 | Update documentation | Medium |

---

## 8. Class Diagrams

### 8.1 Geometry Hierarchy

```
         ┌─────────────────────┐
         │  GeometrySegment    │  (abstract)
         │─────────────────────│
         │ + evaluateDS()      │
         │ + curvatureDS()     │
         │ + length()          │
         │ + type()            │
         │ + sampleAdaptive()  │
         └─────────┬───────────┘
                   │
    ┌──────────────┼──────────────┬──────────────┐
    │              │              │              │
┌───▼────┐  ┌─────▼─────┐  ┌─────▼─────┐  ┌─────▼─────┐
│  Line  │  │   Arc    │  │  Spiral   │  │  Bezier   │
│Segment │  │ Segment  │  │ Segment   │  │ Segment   │
└────────┘  └──────────┘  └───────────┘  └───────────┘
```

### 8.2 Road Model

```
┌──────────────────────────────────────────┐
│                  Road                     │
│──────────────────────────────────────────│
│ id: string                               │
│ name: string                             │
│ segments: GeometrySegment[]              │
│ laneSections: LaneSection[]              │
│ elevation: ElevationProfile[]            │
│ superelevation: SuperElevation[]         │
│ predecessor: RoadLink                    │
│ successor: RoadLink                      │
│──────────────────────────────────────────│
│ + totalLength()                          │
│ + evaluateAtS(s) → Point3D               │
│ + getLaneSection(s) → LaneSection        │
│ + getLaneWidth(s, laneId) → double       │
│ + sampleAdaptive(tolerance) → Point3D[]  │
└──────────────────────────────────────────┘
                    │
                    │ 1..*
    ┌───────────────┼───────────────┐
    │               │               │
┌───▼──────┐  ┌─────▼──────┐  ┌────▼───────┐
│LaneSection│  │Elevation   │  │SuperElev   │
│──────────│  │Profile     │  │            │
│startS    │  │──────────│  │──────────│
│lanes[]   │  │s, a,b,c,d│  │s, a,b,c,d│
└────┬─────┘  └──────────┘  └──────────┘
     │ 1..*
┌────▼─────┐
│   Lane   │
│──────────│
│ id       │
│ type     │
│ width    │
│ marks[]  │
│ link     │
└──────────┘
```

### 8.3 Road Graph

```
┌─────────────────────────────────────┐
│            RoadGraph                 │
│─────────────────────────────────────│
│ nodes_: map<roadId, RoadNode>       │
│ links_: vector<RoadLink>            │
│ junctions_: map<junctionId, Junction>│
│─────────────────────────────────────│
│ + addRoad(road)                     │
│ + addLink(link)                     │
│ + addJunction(junction)             │
│ + getConnections(roadId) → Link[]   │
│ + getJunction(roadId) → Junction    │
│ + findRoadAtPoint(point) → Road     │
└─────────────────────────────────────┘
          │
    ┌─────┴──────┐
    │            │
┌───▼────┐  ┌───▼──────┐
│RoadNode│  │ Junction  │
│────────│  │  Node     │
│roadId  │  │──────────│
│road*   │  │approaches│
│links[] │  │polygon   │
└────────┘  │corners   │
            │laneConns │
            └──────────┘
```

### 8.4 Junction Construction

```
Centerlines
      │
      ▼
┌──────────────┐
│ Generate     │
│ Road         │
│ Boundaries   │  (left + right offset lines)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Compute      │
│ Boundary     │  (left(i) ∩ right(i+1) for each pair)
│ Intersections│
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Compute      │
│ Trim         │  (halfW + R × tan(θ/2))
│ Distances    │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Generate     │
│ Tangent      │  (points at distance R from corner)
│ Points       │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Generate     │
│ Fillet       │  (arc center + polar arc points)
│ Arcs         │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Build        │
│ Junction     │  (leftTrim → arc → rightTrim → cross road)
│ Polygon      │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Validate     │  (self-intersection, winding, duplicates)
│ Polygon      │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Triangulate  │  (ear-clipping or constrained Delaunay)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Generate     │  (vertices, normals, UVs, indices)
│ Mesh         │
└──────────────┘
```

---

## 9. Data Flow Diagrams

### 9.1 Road Creation Flow

```
User clicks (lat, lon)
      │
      ▼
roadEngineClient.createSegment()  (IPC)
      │
      ▼
roadEngineHandler → road_bridge.cpp
      │
      ▼
createSegment() in road_tools.hpp
      │
      ▼
Road { segments: [LineSegment], laneSections: [default] }
      │
      ▼
Return to client → Store → React re-render
      │
      ▼
RoadViewport: sample centerline → render MapLibre layer
```

### 9.2 Junction Generation Flow

```
User selects two roads → detectIntersection()
      │
      ▼
roadEngineClient.generateIntersection()  (IPC)
      │
      ▼
roadEngineHandler → road_bridge.cpp
      │
      ▼
generateIntersection() in intersection.hpp
      │
      ├── 1. Sample centerlines (adaptive)
      ├── 2. Find centerline intersection
      ├── 3. Compute angle between roads
      ├── 4. Compute trim distances
      ├── 5. Build approaches (trimmed centerlines)
      ├── 6. Generate edge-based polygon
      │   ├── 6a. Compute left/right boundary lines
      │   ├── 6b. Sort approaches by angle (CCW)
      │   ├── 6c. For each pair: boundary intersection + fillet
      │   └── 6d. Build polygon: leftTrim → arc → rightTrim → cross
      ├── 7. Validate polygon
      ├── 8. Generate lane connections
      └── 9. Generate stop lines + crosswalks
      │
      ▼
Return GeneratedIntersection (with construction debug data)
      │
      ▼
Store → React re-render
      │
      ▼
RoadViewport: render polygon + debug overlays
```

### 9.3 Mesh Generation Flow

```
Road changes (add/move/delete point)
      │
      ▼
Store update → useEffect
      │
      ▼
refreshSampleCache() → roadEngineClient.sampleCenterline()
      │
      ▼
C++ engine: Road.sampleCenterline(adaptive)
      │
      ▼
Cache samples in RoadViewport
      │
      ▼
updateMapRoads() → render MapLibre layers
      │
      ▼
updateBabylonRoads() → roadEngineClient.generateRoadMesh()
      │
      ▼
C++ engine: generateRoadMesh()
      ├── Sample centerline (adaptive)
      ├── Compute left/right edges (miter joints)
      ├── Generate lane boundaries
      ├── Create triangle strip vertices
      ├── Compute UVs (arc-length + lateral)
      ├── Compute normals (up + superelevation)
      └── Generate indices
      │
      ▼
Return MeshData → Babylon.js VertexData → Mesh
```

### 9.4 Editing Flow (Future)

```
User drags control point
      │
      ▼
RoadViewport: mousemove → updateControlPoint()
      │
      ▼
Store: update road → pushHistory()
      │
      ▼
useEffect: roads changed
      │
      ├── refreshSampleCache() → C++ sample centerline
      ├── updateMapRoads() → MapLibre re-render
      ├── updateBabylonRoads() → C++ generate mesh → Babylon
      └── recomputeIntersections() → C++ generate junction
      │
      ▼
All views update in real-time
```

---

## Appendix A: esmini File Sizes

| File | Size | Purpose |
|------|------|---------|
| RoadManager.hpp | 215 KB | All road class definitions (monolithic) |
| RoadManager.cpp | 574 KB | All road class implementations |
| CommonMini.hpp | 55 KB | Math/geometry utilities |
| CommonMini.cpp | 94 KB | Math implementations |
| roadgeom.hpp | 12 KB | Road mesh generation header |
| roadgeom.cpp | 177 KB | Road mesh generation implementation |
| viewer.hpp | 35 KB | OSG viewer header |
| viewer.cpp | 194 KB | OSG viewer implementation |
| odrSpiral.cpp | 6 KB | Fresnel integral implementation |

**Lesson:** esmini's monolithic headers are hard to maintain. Our architecture uses separate headers per subsystem.

## Appendix B: Key Constants from esmini

| Constant | Value | Purpose |
|----------|-------|---------|
| GEOM_TOLERANCE | 0.2 m | Min distance between vertices |
| MAX_GEOM_ERROR_HORIZONTAL | 0.25 m | Max horizontal error from sampling |
| MAX_GEOM_ERROR_VERTICAL | 0.1 m | Max vertical error |
| MAX_GEOM_LENGTH | 50 m | Max segment length |
| MIN_GEOM_LENGTH | 0.1 m | Min segment length |
| TEXTURE_SCALE | 2.0 m | Road texture repeat distance |
| ROADMARK_TEXTURE_SCALE | 3.0 m | Roadmark texture repeat |
| LOD_DIST | 3000 m | Entity LOD distance |
| LOD_DIST_ROAD_FEATURES | 500 m | Road feature LOD distance |
| POLYGON_OFFSET_ROADMARKS | 1.0 | Z-fighting prevention |
| POLYGON_OFFSET_SIDEWALK | 2.0 | Z-fighting prevention |

## Appendix C: OpenDRIVE Coverage Comparison

| OpenDRIVE Feature | esmini | OpenGeoStudio | Priority |
|-------------------|--------|--------------|----------|
| road | ✅ | ✅ | — |
| planView/geometry/line | ✅ | ✅ | — |
| planView/geometry/arc | ✅ | ✅ | — |
| planView/geometry/spiral | ✅ | ✅ | — |
| planView/geometry/poly3 | ✅ | ❌ | P2 |
| planView/geometry/paramPoly3 | ✅ | ❌ | P2 |
| elevationProfile | ✅ | ⚠️ (per-point z) | P1 |
| lateralProfile/superelevation | ✅ | ❌ | P1 |
| lanes/laneSection | ✅ | ❌ | P0 |
| lanes/lane/width | ✅ | ⚠️ (fixed) | P0 |
| lanes/lane/roadMark | ✅ | ❌ | P1 |
| lanes/lane/link | ✅ | ❌ | P1 |
| junction | ✅ | ⚠️ (partial) | P1 |
| junction/connection | ✅ | ⚠️ (partial) | P1 |
| junction/connection/laneLink | ✅ | ❌ | P1 |
| objects/object | ✅ | ❌ | P3 |
| signals/signal | ✅ | ❌ | P3 |
| railroad/switch | ✅ | ❌ | P3 |
