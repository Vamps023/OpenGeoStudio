# Lane Engine — Phase 2 Architecture & Design

> **Status:** Design Document (pre-implementation)
> **Branch:** `phase2/lane-engine`
**Base:** `phase1-complete` / `v2-road-api-1.0`
> **Depends on:** Phase 1 (RoadV2, GeometrySegment, SegmentSequence)

This document defines the Lane Engine architecture before any code
is written. It follows the same design-first approach that made
Phase 1 successful.

---

## Table of Contents

1. [Goals & Non-Goals](#1-goals--non-goals)
2. [OpenDRIVE Lane Model](#2-opendrive-lane-model)
3. [Data Structures](#3-data-structures)
4. [Lane ID Convention](#4-lane-id-convention)
5. [Width Polynomial](#5-width-polynomial)
6. [Lane Offset Evaluation](#6-lane-offset-evaluation)
7. [Lane Boundary Generation](#7-lane-boundary-generation)
8. [Lane Centerline Generation](#8-lane-centerline-generation)
9. [Lane Markings](#9-lane-markings)
10. [Mesh Generation](#10-mesh-generation)
11. [OpenDRIVE Mapping](#11-opendrive-mapping)
12. [Legacy Compatibility](#12-legacy-compatibility)
13. [Bridge & TypeScript](#13-bridge--typescript)
14. [Milestone Breakdown](#14-milestone-breakdown)
15. [Testing Strategy](#15-testing-strategy)
16. [API Freeze Plan](#16-api-freeze-plan)

---

## 1. Goals & Non-Goals

### Goals

- **Per-lane width** via polynomial (not flat `road.width / laneCount`)
- **Variable lane configuration** along road length (lane sections)
- **Lane boundary generation** (polylines at lane edges)
- **Lane centerline generation** (per-lane driving centerline)
- **Lane markings** (solid, dashed, double, none)
- **Lane-aware mesh** (one strip per lane, not one strip per road)
- **OpenDRIVE lane export** (full lane section XML)
- **Backward compatibility** — legacy `width`/`laneCount` still works

### Non-Goals (deferred to Phase 3+)

- Road graph / connectivity (Phase 3)
- Junction lane connections (Phase 3)
- Superelevation / banking (Phase 5)
- Adaptive sampling (Phase 5)
- OpenDRIVE lane import (Phase 5)
- Lane-level routing / pathfinding (Phase 3)

---

## 2. OpenDRIVE Lane Model

The Lane Engine follows the OpenDRIVE lane model closely, since
OpenDRIVE is the target export format and the most mature
specification for road lane modeling.

### OpenDRIVE Structure

```
<road>
  <lanes>
    <laneSection s="0.0">          ← starts at s=0 along reference line
      <left>
        <lane id="-1" type="driving">
          <width sOffset="0" a="3.5" b="0" c="0" d="0"/>
          <roadMark sOffset="0" type="solid" width="0.15"/>
        </lane>
        <lane id="-2" type="driving">
          <width sOffset="0" a="3.5" b="0" c="0" d="0"/>
          <roadMark sOffset="0" type="dashed" width="0.15"/>
        </lane>
      </left>
      <center>
        <lane id="0" type="border">
          <roadMark sOffset="0" type="broken_broken" width="0.15"/>
        </lane>
      </center>
      <right>
        <lane id="1" type="driving">
          <width sOffset="0" a="3.5" b="0" c="0" d="0"/>
          <roadMark sOffset="0" type="solid" width="0.15"/>
        </lane>
        <lane id="2" type="driving">
          <width sOffset="0" a="3.5" b="0" c="0" d="0"/>
          <roadMark sOffset="0" type="dashed" width="0.15"/>
        </lane>
      </right>
    </laneSection>
  </lanes>
</road>
```

### Key Concepts

1. **LaneSection** — A span of road `[s_start, s_end)` with a fixed
   lane configuration. A road can have multiple lane sections (e.g.,
   a road that widens from 2 to 4 lanes at s=100).

2. **Lane** — A single lane within a lane section. Has an ID, type,
   width polynomial, and road markings.

3. **Lane ID convention** — Center lane is ID 0. Left lanes are
   negative (-1, -2, ...). Right lanes are positive (+1, +2, ...).
   This matches OpenDRIVE exactly.

4. **Width polynomial** — `width(s) = a + b*ds + c*ds² + d*ds³`
   where `ds = s - sOffset`. This allows width to vary smoothly
   along the lane (e.g., for merge lanes, tapering).

5. **Road marking** — Lane boundary visual type (solid, dashed,
   double solid, etc.) with width and color.

---

## 3. Data Structures

### LaneType

```cpp
enum class LaneType {
    Driving,
    Shoulder,
    Sidewalk,
    Border,
    Parking,
    Stop,       // stop/standing lane
    Restricted,
    Biking,
    Tram,
    Bus,
    Taxi,
    HOV,
    Acceleration,
    Deceleration,
    None,       // virtual lane (no physical surface)
};
```

### LaneRoadMarkType

```cpp
enum class LaneRoadMarkType {
    None,
    Solid,
    Dashed,
    SolidSolid,       // double solid
    SolidDashed,      // solid on left, dashed on right
    DashedSolid,
    DashedDashed,
    Broken,           // alias for Dashed
    BrokenBroken,     // alias for DashedDashed
    Curb,
    Edge,
};
```

### LaneRoadMark

```cpp
struct LaneRoadMark {
    double sOffset = 0.0;           // s-offset from lane section start
    LaneRoadMarkType type = LaneRoadMarkType::Dashed;
    std::string color = "white";    // "white", "yellow", "red"
    double width = 0.15;            // marking width in meters
    double laneChange = 0.0;        // -1=no left change, +1=no right, 0=both
};
```

### Polynomial3 (cubic polynomial)

```cpp
struct Polynomial3 {
    double a = 0.0;  // constant term
    double b = 0.0;  // linear term
    double c = 0.0;  // quadratic term
    double d = 0.0;  // cubic term

    // Evaluate at ds (delta-s from sOffset)
    double evaluate(double ds) const {
        return a + b * ds + c * ds * ds + d * ds * ds * ds;
    }

    // Evaluate derivative at ds
    double derivative(double ds) const {
        return b + 2.0 * c * ds + 3.0 * d * ds * ds;
    }
};
```

### Lane

```cpp
struct Lane {
    int id = 0;                              // 0=center, -1,-2=left, +1,+2=right
    LaneType type = LaneType::Driving;
    Polynomial3 width;                       // width as function of ds
    std::vector<LaneRoadMark> roadMarks;     // markings on this lane
    // Phase 3: LaneLink predecessor/successor

    // Convenience: evaluate width at ds
    double widthAt(double ds) const {
        return width.evaluate(ds);
    }
};
```

### LaneSection (replaces empty placeholder)

```cpp
struct LaneSection {
    double startS = 0.0;                     // s-position where section starts
    std::vector<Lane> lanes;                 // all lanes (left, center, right)

    // Access by side
    const Lane* center() const;              // lane with id=0
    std::vector<const Lane*> leftLanes() const;   // id < 0, sorted by |id|
    std::vector<const Lane*> rightLanes() const;  // id > 0, sorted by id

    // Total road width at ds (sum of all lane widths)
    double totalWidthAt(double ds) const;

    // Number of driving lanes
    int drivingLaneCount() const;
};
```

### RoadV2 (extended)

```cpp
class RoadV2 {
    // ... existing Phase 1 fields ...
    std::vector<LaneSection> laneSections_;  // NOW POPULATED

    // Lane section access
    int numLaneSections() const;
    const LaneSection& laneSection(int idx) const;

    // Find lane section at s-position
    const LaneSection* laneSectionAt(double s) const;

    // Add lane section
    void addLaneSection(LaneSection section);

    // Computed from laneSections_ (replaces flat width/laneCount)
    double widthAt(double s) const;          // total road width at s
    int laneCountAt(double s) const;         // driving lane count at s
};
```

---

## 4. Lane ID Convention

Following OpenDRIVE exactly:

```
          ← Left side (oncoming traffic)           Right side (forward traffic) →
          ┌────────┬────────┬────────┬────────┬────────┬────────┐
  ID:     │  -3    │  -2    │  -1    │   0    │  +1    │  +2    │  +3    │
          │shoulder│driving │driving │ center │driving │driving │shoulder│
          └────────┴────────┴────────┴────────┴────────┴────────┘
                                              ↑
                                         Reference line
                                         (RoadV2 centerline)
```

- **ID 0**: Center lane (virtual, width=0, type=Border)
- **Negative IDs**: Left lanes (oncoming direction), sorted by |id|
- **Positive IDs**: Right lanes (forward direction), sorted by id
- **ID magnitude**: Increases outward from center

### Lane offset from reference line

For a lane with ID `n` (positive, right side), the offset of its
**center** from the reference line is:

```
offset(n) = sum(width(i) for i = 1..n) - width(n) / 2
```

For a lane with ID `n` (negative, left side):

```
offset(n) = -(sum(width(i) for i = 1..|n|) - width(|n|) / 2)
```

The **boundary** between lane `n` and `n+1` (right side) is:

```
boundary(n, n+1) = sum(width(i) for i = 1..n)
```

---

## 5. Width Polynomial

Each lane has a cubic polynomial for width:

```
width(ds) = a + b·ds + c·ds² + d·ds³
```

where `ds = s - sOffset` (s-offset from the lane section start).

### Common Cases

| Case | a | b | c | d | Description |
|------|---|---|---|---|-------------|
| Constant width | W | 0 | 0 | 0 | Standard lane |
| Linear taper | W₀ | (W₁-W₀)/L | 0 | 0 | Merge/exit lane |
| Smooth taper | W₀ | 0 | 3·ΔW/L² | -2·ΔW/L³ | Smooth merge (C1) |

### Default (from legacy width/laneCount)

When synthesizing from legacy `RoadV2::width` and `RoadV2::laneCount`:

```cpp
LaneSection synthesizeFromLegacy(double roadWidth, int laneCount) {
    LaneSection ls;
    ls.startS = 0.0;

    double laneWidth = roadWidth / laneCount;
    int rightCount = laneCount / 2;
    int leftCount = laneCount - rightCount;

    // Center lane (virtual)
    Lane center;
    center.id = 0;
    center.type = LaneType::Border;
    center.width = {0, 0, 0, 0};  // zero width
    ls.lanes.push_back(center);

    // Right lanes (positive IDs)
    for (int i = 1; i <= rightCount; i++) {
        Lane lane;
        lane.id = i;
        lane.type = LaneType::Driving;
        lane.width = {laneWidth, 0, 0, 0};  // constant
        ls.lanes.push_back(lane);
    }

    // Left lanes (negative IDs)
    for (int i = 1; i <= leftCount; i++) {
        Lane lane;
        lane.id = -i;
        lane.type = LaneType::Driving;
        lane.width = {laneWidth, 0, 0, 0};
        ls.lanes.push_back(lane);
    }

    return ls;
}
```

---

## 6. Lane Offset Evaluation

Given a `RoadV2` and a position `s` along the reference line, we can
compute the lateral offset of any lane center or boundary.

### Algorithm

```cpp
// Offset of lane boundary between lane n and n+1 (right side)
double laneBoundaryOffset(const LaneSection& ls, double ds, int n) {
    double offset = 0;
    for (int i = 1; i <= n; i++) {
        const Lane* lane = ls.findLane(i);
        if (lane) offset += lane->widthAt(ds);
    }
    return offset;
}

// Offset of lane center (right side, positive ID)
double laneCenterOffset(const LaneSection& ls, double ds, int laneId) {
    double boundary = laneBoundaryOffset(ls, ds, laneId - 1);
    const Lane* lane = ls.findLane(laneId);
    return boundary + lane->widthAt(ds) / 2.0;
}

// For left side (negative ID), mirror the sign
double laneCenterOffsetLeft(const LaneSection& ls, double ds, int laneId) {
    double offset = 0;
    for (int i = 1; i <= -laneId; i++) {
        const Lane* lane = ls.findLane(-i);
        if (lane) offset += lane->widthAt(ds);
    }
    const Lane* lane = ls.findLane(laneId);
    return -(offset - lane->widthAt(ds) / 2.0);
}
```

### World position of lane center

```cpp
Point2D laneCenterWorld(const RoadV2& road, double s, int laneId) {
    const LaneSection* ls = road.laneSectionAt(s);
    double ds = s - ls->startS;
    double t = (laneId > 0)
        ? laneCenterOffset(*ls, ds, laneId)
        : (laneId < 0)
            ? laneCenterOffsetLeft(*ls, ds, laneId)
            : 0.0;  // center lane

    // Get reference line position and normal at s
    Point2D refPos = road.geometry().positionAt(s);
    Vec2 normal = road.geometry().normalAt(s);

    return { refPos.x + normal.x * t, refPos.y + normal.y * t };
}
```

---

## 7. Lane Boundary Generation

Lane boundaries are polylines offset from the reference line.

### Algorithm

```cpp
// Generate all lane boundaries for a road
std::vector<LaneBoundary> generateLaneBoundaries(const RoadV2& road, int numSamples) {
    std::vector<LaneBoundary> boundaries;

    double totalLen = road.totalLength();
    const LaneSection* ls = road.laneSectionAt(0);
    if (!ls) return boundaries;

    // For each boundary offset
    // Right side: between lane 0 and 1, between 1 and 2, etc.
    // Left side: between lane 0 and -1, between -1 and -2, etc.

    int maxRight = ls->maxRightLaneId();
    int maxLeft = ls->maxLeftLaneId();

    // Right boundaries
    for (int n = 0; n < maxRight; n++) {
        LaneBoundary b;
        b.side = "right";
        b.betweenLanes = {n, n + 1};

        for (int i = 0; i < numSamples; i++) {
            double s = totalLen * i / (numSamples - 1);
            const LaneSection* curLs = road.laneSectionAt(s);
            double ds = s - curLs->startS;
            double t = laneBoundaryOffset(*curLs, ds, n);

            Point2D refPos = road.geometry().positionAt(s);
            Vec2 normal = road.geometry().normalAt(s);
            b.points.push_back({
                refPos.x + normal.x * t,
                refPos.y + normal.y * t
            });
        }
        boundaries.push_back(b);
    }

    // Left boundaries (mirror)
    for (int n = 0; n < maxLeft; n++) {
        // Similar, but t is negative
        // ...
    }

    return boundaries;
}
```

### LaneBoundary struct

```cpp
struct LaneBoundary {
    std::string side;               // "left" or "right"
    std::pair<int, int> betweenLanes;  // (inner_id, outer_id)
    std::vector<Point2D> points;    // polyline
    LaneRoadMarkType markType;      // marking on this boundary
};
```

---

## 8. Lane Centerline Generation

Each lane has its own centerline (useful for simulation, path planning,
and visualization).

```cpp
struct LaneCenterline {
    int laneId;
    LaneType type;
    std::vector<Point2D> points;    // centerline polyline
    std::vector<double> sValues;    // corresponding s on reference line
};

std::vector<LaneCenterline> generateLaneCenterlines(const RoadV2& road, int numSamples);
```

---

## 9. Lane Markings

Lane markings are stored per-lane on the `Lane::roadMarks` vector.
Each marking describes the boundary on the **outer side** of the lane
(the side farther from the reference line).

### Marking Types

| Type | Visual | Use Case |
|------|--------|----------|
| `Solid` | ───── | Edge of road, no crossing |
| `Dashed` | - - - - | Lane separation, crossing allowed |
| `SolidSolid` | ═════ | No crossing (double yellow) |
| `SolidDashed` | ═-══- | Cross from dashed side only |
| `DashedSolid` | -═-═- | Cross from dashed side only |
| `Broken` | - - - - | Alias for Dashed |
| `Curb` | ▓▓▓▓▓ | Physical curb |
| `None` | (invisible) | No marking |

### Default markings (from legacy)

- Center line (between left and right): `BrokenBroken` (dashed yellow)
- Outer edge lines: `Solid` (solid white)
- Inner lane lines: `Dashed` (dashed white)

---

## 10. Mesh Generation

Phase 2 mesh generation produces one triangle strip per lane, not
one strip per road. This enables:

- Per-lane materials (asphalt vs shoulder vs sidewalk)
- Per-lane UV mapping
- Lane marking overlays
- Variable width along road

### Algorithm

```cpp
MeshData generateRoadMeshV2(const RoadV2& road, int numSamples) {
    // For each lane section:
    //   For each lane in section:
    //     Generate triangle strip between inner and outer boundary
    //   Concatenate strips into single mesh
    //
    // Each vertex: (x, y, z, nx, ny, nz, u, v)
    // UV: u = lateral position (0=inner, 1=outer), v = arc-length / tile
}
```

### Per-lane mesh properties

| Lane Type | Material | UV Tile |
|-----------|----------|---------|
| Driving | Asphalt | 10m longitudinal |
| Shoulder | Gravel | 10m |
| Sidewalk | Concrete | 2m |
| Border | (none) | — |

---

## 11. OpenDRIVE Mapping

The Lane Engine maps directly to OpenDRIVE XML:

| C++ Struct | OpenDRIVE Element |
|------------|-------------------|
| `LaneSection` | `<laneSection s="...">` |
| `Lane` | `<lane id="..." type="...">` |
| `Polynomial3` | `<width sOffset="..." a="..." b="..." c="..." d="..."/>` |
| `LaneRoadMark` | `<roadMark sOffset="..." type="..." color="..." width="..."/>` |

### Export flow

```
RoadV2 → for each LaneSection:
           <laneSection s="...">
             <left>
               for each lane with id < 0:
                 <lane id="..." type="...">
                   <width .../>
                   <roadMark .../>
                 </lane>
             </left>
             <center>
               <lane id="0" type="border">
                 <width a="0" .../>
                 <roadMark .../>
               </lane>
             </center>
             <right>
               for each lane with id > 0:
                 <lane id="..." type="...">
                   <width .../>
                   <roadMark .../>
                 </lane>
             </right>
           </laneSection>
```

---

## 12. Legacy Compatibility

### Adapter: Legacy → LaneSection

When `RoadV2` has no explicit `LaneSection`s (legacy roads), the
adapter synthesizes one from `width` and `laneCount`:

```cpp
if (road.numLaneSections() == 0) {
    LaneSection ls = synthesizeFromLegacy(road.width, road.laneCount);
    // Use ls for all queries
}
```

### Adapter: LaneSection → Legacy

When converting `RoadV2` → `Road` (via `roadFromV2`), if
`LaneSection`s are present:

1. Sample `widthAt(0)` → `Road::width`
2. `drivingLaneCount()` → `Road::laneCount`
3. Emit warning: "LaneSection data cannot be fully represented in
   legacy Road — width and laneCount are sampled at s=0"

This means `roadFromV2()` with LaneSections is **lossy** (as
documented in Phase 1's `ReverseAdapterReport`).

### formatVersion

| Version | Lane Handling |
|---------|---------------|
| 1 | No LaneSection, legacy width/laneCount |
| 2 | No LaneSection, but SegmentMetadata present |
| 3 (Phase 2) | LaneSection present, width/laneCount synthesized |

**Note:** Phase 2 does NOT bump formatVersion automatically. Roads
created by tools still use formatVersion=2. LaneSections are only
present when explicitly authored. This keeps backward compatibility.

---

## 13. Bridge & TypeScript

### New IPC Methods

| Method | Purpose |
|--------|---------|
| `roadGetLaneSections(road)` | Returns lane section data |
| `roadGenerateLaneBoundaries(road, numSamples?)` | Returns lane boundary polylines |
| `roadGenerateLaneCenterlines(road, numSamples?)` | Returns per-lane centerlines |
| `roadGenerateRoadMeshV2(road, numSamples?)` | Lane-aware mesh |

### TypeScript Types

```typescript
interface LaneType {
  // 'driving' | 'shoulder' | 'sidewalk' | 'border' | ...
}

interface Lane {
  id: number;              // 0=center, negative=left, positive=right
  type: string;
  width: { a: number; b: number; c: number; d: number };
  roadMarks: LaneRoadMark[];
}

interface LaneSection {
  startS: number;
  lanes: Lane[];
}

interface LaneRoadMark {
  sOffset: number;
  type: string;            // 'solid' | 'dashed' | ...
  color: string;           // 'white' | 'yellow'
  width: number;
}

interface Road {
  // ... existing fields ...
  laneSections?: LaneSection[];  // NEW (optional, Phase 2)
}
```

All new fields are **optional** — no breaking changes.

---

## 14. Milestone Breakdown

| Milestone | Goal | Files | Tests |
|-----------|------|-------|-------|
| **2.1** | `Lane`, `LaneSection`, `LaneType`, `LaneRoadMark`, `Polynomial3` data model | `lane_engine.hpp` (new) | Struct creation, default values, lane lookup by ID |
| **2.2** | Polynomial width evaluation | `lane_engine.hpp` | `Polynomial3::evaluate()`, `Lane::widthAt()`, constant/linear/smooth taper |
| **2.3** | Lane offset evaluation from `SegmentSequence` | `lane_engine.hpp` | `laneCenterOffset()`, `laneBoundaryOffset()`, world position |
| **2.4** | Lane boundary generation | `lane_engine.hpp` | `generateLaneBoundaries()`, N-lane road, variable width |
| **2.5** | Lane centerline generation | `lane_engine.hpp` | `generateLaneCenterlines()`, per-lane polylines |
| **2.6** | Lane markings | `lane_engine.hpp` | Default markings, per-lane marking assignment |
| **2.7** | Lane-aware mesh | `mesh.hpp` (extend) | `generateRoadMeshV2()`, per-lane strips, UV mapping |
| **2.8** | TS/Bridge integration | `road_bridge.cpp`, `roadEngineClient.ts`, `types.ts` | IPC round-trip, lane section serialization |

### Dependency Graph

```
2.1 (data model)
  ↓
2.2 (width polynomial)
  ↓
2.3 (offset evaluation)  ← depends on SegmentSequence (Phase 1)
  ↓
2.4 (boundaries)  ──→  2.5 (centerlines)
  ↓                       ↓
2.6 (markings)           │
  ↓                       │
2.7 (mesh)  ←─────────────┘
  ↓
2.8 (bridge/TS)
```

---

## 15. Testing Strategy

### Unit Tests (doctest)

| Category | Test Count (est.) | Key Tests |
|----------|------------------|-----------|
| Polynomial3 | 5 | evaluate, derivative, constant, linear, cubic |
| Lane/LaneSection | 8 | creation, lane lookup, totalWidth, drivingLaneCount |
| Lane offset | 10 | center offset, boundary offset, left/right, multi-lane |
| Lane boundaries | 8 | 2-lane, 4-lane, 6-lane, variable width, taper |
| Lane centerlines | 6 | per-lane, left/right, center lane |
| Lane markings | 5 | defaults, solid, dashed, double, per-lane |
| Legacy synthesis | 5 | width/laneCount → LaneSection, 2/4/6 lanes |
| Round-trip | 5 | LaneSection → legacy → LaneSection (lossy check) |
| Stress | 2 | 1000-segment road with lane sections, no NaN |

### Golden Fixture Tests

Extend `golden_fixtures.json` with lane boundary golden data:
- `lane_boundaries_2lane` — 2-lane road, 1 center boundary
- `lane_boundaries_4lane` — 4-lane road, 3 boundaries
- `lane_boundaries_taper` — merge lane with linear taper

### Bridge Tests (vitest)

| Test | Purpose |
|------|---------|
| `roadGetLaneSections` | Returns lane data through IPC |
| `roadGenerateLaneBoundaries` | Boundary polylines match C++ |
| `roadGenerateLaneCenterlines` | Per-lane centerlines through IPC |
| `roadGenerateRoadMeshV2` | Lane-aware mesh, per-lane strips |
| Lane section serialization | JS → C++ → JS round-trip |

### Compatibility Tests

- Legacy road (no LaneSection) → synthesized LaneSection → correct
  boundaries
- Road with LaneSection → `roadFromV2()` → warning + lossless=false
- `formatVersion=2` road → lane queries work via synthesis

---

## 16. API Freeze Plan

### Frozen at Phase 2 Complete

| Class/Function | File |
|----------------|------|
| `LaneType` (enum) | `lane_engine.hpp` |
| `LaneRoadMarkType` (enum) | `lane_engine.hpp` |
| `LaneRoadMark` (struct) | `lane_engine.hpp` |
| `Polynomial3` (struct) | `lane_engine.hpp` |
| `Lane` (struct) | `lane_engine.hpp` |
| `LaneSection` (struct — replaces placeholder) | `lane_engine.hpp` |
| `LaneBoundary` (struct) | `lane_engine.hpp` |
| `LaneCenterline` (struct) | `lane_engine.hpp` |
| `generateLaneBoundaries()` | `lane_engine.hpp` |
| `generateLaneCenterlines()` | `lane_engine.hpp` |
| `synthesizeFromLegacy()` | `lane_engine.hpp` |

### NOT Frozen (will change in Phase 3+)

| Item | Reason |
|------|--------|
| `LaneLink` | Phase 3 (Road Graph) |
| `RoadGraph` | Phase 3 |
| `generateRoadMeshV2()` | May extend for superelevation (Phase 5) |
| Lane marking rendering | May add more types |

### Migration of LaneSection placeholder

The empty `LaneSection` placeholder in `road_v2.hpp` (Phase 1) will
be replaced by the full `LaneSection` in `lane_engine.hpp`. The
`RoadV2::laneSections_` vector already stores `LaneSection` objects —
we just need to move the struct definition and populate it.

**Compatibility note:** The Phase 1 test
`"1.8.5c: ReverseAdapterReport — LaneSection causes lossless=false"`
adds an empty `LaneSection{}`. After Phase 2.1, this will still work
(empty LaneSection has `startS=0` and no lanes — the adapter will
treat it as "no lane data" and synthesize from width/laneCount).
