# Road Engine — Phase 1 Architecture & API Reference

> **Status:** Phase 1 Complete (frozen)
> **Tag:** `phase1-complete`
> **Date:** 2025-01-XX

This document describes the Phase 1 road geometry engine architecture,
public API, adapter flow, and migration strategy. It serves as the
authoritative reference for contributors before Phase 2 (Lane Engine)
begins.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Layer Architecture](#2-layer-architecture)
3. [Ownership Model](#3-ownership-model)
4. [Geometry Segments](#4-geometry-segments)
5. [RoadV2](#5-roadv2)
6. [Adapter System](#6-adapter-system)
7. [formatVersion & Migration](#7-formatversion--migration)
8. [Bridge & IPC](#8-bridge--ipc)
9. [TypeScript Types](#9-typescript-types)
10. [Public API Freeze](#10-public-api-freeze)
11. [Performance Baseline](#11-performance-baseline)
12. [Testing](#12-testing)
13. [Technical Debt & Future Work](#13-technical-debt--future-work)

---

## 1. Overview

Phase 1 replaced the legacy flat `ControlPoint[]` road model with a
polymorphic `GeometrySegment`-based architecture (`RoadV2`), while
maintaining full backward compatibility through a bidirectional adapter.

**Key achievement:** The entire application can now work with `RoadV2`
internally, with the legacy `Road` serving only as a serialization and
editing format. The adapter handles conversion transparently.

### Phase 1 Milestones

| Milestone | Description | Status |
|-----------|-------------|--------|
| 1.1–1.7 | Geometry kernel, segment types, RoadV2, SegmentSequence | ✅ |
| 1.8.3a–d | Adapter (exact + legacy), SegmentMetadata, tool updates | ✅ |
| 1.8.4 | Golden parity validation, formatVersion auto-dispatch | ✅ |
| 1.8.5 | Inverse adapter (`roadFromV2`), round-trip verification | ✅ |
| 1.9 | Bridge integration (IPC for RoadV2 functions) | ✅ |
| 1.10 | API freeze, docs, benchmarks | ✅ |

---

## 2. Layer Architecture

```
┌─────────────────────────────────────────────────────┐
│  TypeScript (Renderer)                              │
│  ┌───────────────────────────────────────────────┐  │
│  │  RoadEngineClient                             │  │
│  │  sampleCenterlineV2(), getAdapterReport(),    │  │
│  │  convertFromV2(), createCircleArc(), ...      │  │
│  └──────────────────┬────────────────────────────┘  │
│                     │ IPC                           │
├─────────────────────┼───────────────────────────────┤
│  Main Process       │                               │
│  ┌──────────────────▼────────────────────────────┐  │
│  │  road_bridge.cpp (N-API)                     │  │
│  │  parseRoad() → roadToV2Auto() → RoadV2       │  │
│  │  roadFromV2() → roadToJs()                   │  │
│  └──────────────────┬────────────────────────────┘  │
│                     │                               │
│  ┌──────────────────▼────────────────────────────┐  │
│  │  road_adapter.hpp                            │  │
│  │  roadToV2()       — exact reconstruction     │  │
│  │  roadToV2Legacy() — legacy compatibility     │  │
│  │  roadToV2Auto()   — formatVersion dispatch   │  │
│  │  roadFromV2()     — inverse adapter          │  │
│  └──────────────────┬────────────────────────────┘  │
│                     │                               │
│  ┌──────────────────▼────────────────────────────┐  │
│  │  RoadV2 (owns GeometrySegment[])             │  │
│  │  SegmentSequence (non-owning view)           │  │
│  │  LineSegment, ArcSegment, SpiralSegment,     │  │
│  │  BezierSegment                               │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

---

## 3. Ownership Model

```
RoadV2
├── segments_: vector<unique_ptr<GeometrySegment>>  ← OWNS geometry
├── laneSections_: vector<LaneSection>              ← empty (Phase 2)
├── geometry_: SegmentSequence                      ← NON-OWNING view
└── metadata: id, name, width, laneCount, ...
```

**Critical invariant:** Every mutation that changes `segments_` calls
`rebuildGeometryView()`. This ensures `geometry_` (the
`SegmentSequence`) always points to valid segment pointers.

**Deep copy:** `RoadV2` copy constructor/assignment clones every
segment via `GeometrySegment::clone()`. This is required for
undo/redo snapshot compatibility.

---

## 4. Geometry Segments

### Abstract Base: `GeometrySegment`

```cpp
class GeometrySegment {
public:
    virtual void evaluateDS(double s, double& x, double& y, double& heading) const = 0;
    virtual double curvatureDS(double s) const = 0;
    virtual double length() const = 0;
    virtual GeometryType type() const = 0;
    virtual std::unique_ptr<GeometrySegment> clone() const = 0;

    // Convenience (non-virtual)
    Point2D positionAt(double s) const;
    Vec2 tangentAt(double s) const;
    Vec2 normalAt(double s) const;
    double headingAt(double s) const;
    Point2D startPoint() const;
    Point2D endPoint() const;
};
```

**Contract:** `s ∈ [0, length()]` is arc-length in meters from segment
start. All segments are planar (2D); elevation is handled separately.

### Segment Types

| Type | Parameters | Curvature | Use Case |
|------|-----------|-----------|----------|
| `LineSegment` | p0, p1 | 0 (constant) | Straight roads |
| `ArcSegment` | startPoint, startHeading, curvature, arcLength | κ (constant) | Circular curves |
| `SpiralSegment` | startPoint, startHeading, κ₀, κ₁, length | linear κ₀→κ₁ | Clothoid transitions |
| `BezierSegment` | p0, p1, p2, p3 (absolute) | varies | Free-form curves |

### Curvature Convention

- Positive curvature = left turn (CCW), matching OpenDRIVE
- Negative curvature = right turn (CW)
- `LineSegment` curvature is always 0

---

## 5. RoadV2

```cpp
class RoadV2 {
public:
    // Metadata
    std::string id, name, color, profileName;
    std::string startIntersectionId, endIntersectionId;
    double width = 8.0;
    int laneCount = 2;

    // Segment mutation (encapsulated — always rebuilds view)
    void addSegment(std::unique_ptr<GeometrySegment> seg);
    template<typename SegType, typename... Args>
    SegType& addSegment(Args&&... args);
    void clearSegments();
    void reserveSegments(size_t count);

    // Read-only access
    int numSegments() const;
    const GeometrySegment& segment(int idx) const;
    const SegmentSequence& geometry() const;
    double totalLength() const;

    // Lane sections (Phase 2)
    int numLaneSections() const;
    void addLaneSection(LaneSection section);
};
```

---

## 6. Adapter System

### Forward Adapters (Road → RoadV2)

```
roadToV2Auto(road)
    │
    ├── formatVersion >= 2 → roadToV2()       [exact]
    │                           │
    │                           ├── Line:   metadata → LineSegment
    │                           ├── Bezier: handles → BezierSegment
    │                           ├── Arc:    metadata → ArcSegment
    │                           └── Spiral: metadata → SpiralSegment
    │
    └── formatVersion < 2  → roadToV2Legacy() [compatibility]
                                │
                                ├── corner CPs → LineSegment
                                └── smooth CPs → BezierSegment
```

### Inverse Adapter (RoadV2 → Road)

```
roadFromV2(v2)
    │
    ├── LineSegment   → 2 corner ControlPoints
    ├── BezierSegment → 2 smooth ControlPoints with handles
    ├── ArcSegment    → ControlPoint + SegmentMetadata(kind=Arc)
    ├── SpiralSegment → ControlPoint + SegmentMetadata(kind=Spiral)
    │
    └── Adjacent segments share boundary ControlPoints
```

### Round-trip Invariant

```
roadFromV2(roadToV2(Road))    → always LOSSLESS (Phase 1)
roadToV2(roadFromV2(RoadV2))  → MAY be lossy (Phase 2: LaneSection)
```

### SegmentMetadata

```cpp
struct SegmentMetadata {
    SegmentKind kind;        // Line, Bezier, Arc, Spiral
    int version;
    double startHeading;
    // Arc
    double curvature;
    double arcLength;
    // Spiral
    double curvatureStart;
    double curvatureEnd;
    double segmentLength;
};
```

Stored on `ControlPoint::segmentMeta` (optional). Only the first CP
of a segment carries metadata. This allows exact reconstruction of
non-Bezier geometry from the legacy `Road` format.

---

## 7. formatVersion & Migration

| Version | Description | Adapter Path |
|---------|-------------|--------------|
| 1 | Legacy `ControlPoint[]` (no metadata) | `roadToV2Legacy()` |
| 2 | `ControlPoint[]` + `SegmentMetadata` | `roadToV2()` (exact) |
| Future | Reserved | — |

**Migration strategy:** New roads created by tools (`createCircleArc`,
`createClothoidArc`, etc.) automatically set `formatVersion=2` and
emit `SegmentMetadata`. Old roads loaded from file remain
`formatVersion=1` and use the legacy path. No manual migration needed.

---

## 8. Bridge & IPC

### New IPC Methods (Phase 1.9)

| Method | Purpose |
|--------|---------|
| `roadSampleCenterlineV2(road, numSamples?)` | Samples RoadV2 centerline via `roadToV2Auto()` |
| `roadGetAdapterReport(road)` | Returns diagnostics from conversion |
| `roadConvertFromV2(road)` | Round-trip: Road → RoadV2 → Road |

### Bridge Architecture

The bridge dispatches based on `formatVersion` internally:

```
JS Road → parseRoad() → roadToV2Auto() → RoadV2 → [sample/report/convert]
```

The TS side never needs to know which adapter path is used.

### Serialization

- `parseRoad()` reads: id, name, width, laneCount, formatVersion, points[], segmentMeta
- `roadToJs()` writes: id, name, width, laneCount, formatVersion, points[], segmentMeta

---

## 9. TypeScript Types

All new fields are **optional** — no breaking changes.

```typescript
interface ControlPoint {
  // ... existing fields ...
  segmentMeta?: SegmentMetadata | null;  // NEW (Phase 1.8.3d)
}

interface Road {
  // ... existing fields ...
  formatVersion?: number;  // NEW (1=legacy, 2=with segmentMeta)
}

interface SegmentMetadata {
  kind: 'line' | 'bezier' | 'arc' | 'spiral';
  version: number;
  startHeading: number;
  curvature: number;
  arcLength: number;
  curvatureStart: number;
  curvatureEnd: number;
  segmentLength: number;
}
```

---

## 10. Public API Freeze

The following C++ classes and functions are **FROZEN** as of Phase 1
Complete. Breaking changes require a major version bump.

### Frozen Classes

| Class | File | Notes |
|-------|------|-------|
| `GeometryType` (enum) | `geometry_segment.hpp` | |
| `GeometrySegment` (abstract) | `geometry_segment.hpp` | |
| `LineSegment` | `geometry_segment.hpp` | |
| `ArcSegment` | `geometry_segment.hpp` | |
| `SpiralSegment` | `geometry_segment.hpp` | |
| `BezierSegment` | `geometry_segment.hpp` | |
| `SegmentSequence` | `geometry_segment.hpp` | Non-owning view |
| `RoadV2` | `road_v2.hpp` | |
| `SegmentKind` (enum) | `road.hpp` | |
| `SegmentMetadata` | `road.hpp` | |
| `AdapterReport` | `road_adapter.hpp` | |
| `ReverseAdapterReport` | `road_adapter.hpp` | |

### Frozen Functions

| Function | File |
|----------|------|
| `roadToV2()` | `road_adapter.hpp` |
| `roadToV2Legacy()` | `road_adapter.hpp` |
| `roadToV2Auto()` | `road_adapter.hpp` |
| `roadFromV2()` | `road_adapter.hpp` |

### NOT Frozen (will change in Phase 2)

| Class | Reason |
|-------|--------|
| `LaneSection` | Empty placeholder — Phase 2 will define properly |
| `Road::width` | Will be synthesized from LaneSection in Phase 2 |
| `Road::laneCount` | Will be synthesized from LaneSection in Phase 2 |
| `RoadV2::width` | Same |
| `RoadV2::laneCount` | Same |

---

## 11. Performance Baseline

Measured on Windows, MSVC, release build. 1000 samples per benchmark.

### Line Segments (exact path, formatVersion=2)

| Segments | Convert (ms) | Sample (ms) | Reverse (ms) |
|----------|-------------|-------------|--------------|
| 100 | 0.25 | 0.16 | 0.10 |
| 500 | 5.13 | 0.19 | 0.53 |
| 1000 | 19.75 | 0.20 | 0.94 |

### Bezier Segments (exact path, formatVersion=2)

| Segments | Convert (ms) | Sample (ms) | Reverse (ms) |
|----------|-------------|-------------|--------------|
| 100 | 1.41 | 0.32 | 0.18 |
| 500 | 20.40 | 0.48 | 1.01 |
| 1000 | 75.54 | 0.50 | 2.01 |

### Line Segments (legacy path, formatVersion=1)

| Segments | Convert (ms) | Sample (ms) | Reverse (ms) |
|----------|-------------|-------------|--------------|
| 100 | 0.40 | 0.30 | 0.16 |
| 500 | 6.25 | 0.19 | 0.56 |
| 1000 | 20.21 | 0.32 | 1.29 |

### Key Observations

- **Sampling is O(1) per query** — SegmentSequence does binary search
- **Conversion is O(N)** — linear in segment count
- **Bezier conversion is slower** — arc-length table construction
- **1000-segment road converts in <80ms** — well within interactive frame budget
- **Legacy path is comparable to exact path** — no performance penalty for old roads

---

## 12. Testing

### Test Counts

| Suite | Tests | Assertions |
|-------|-------|------------|
| doctest (C++) | 261 | 2,210 |
| vitest (TS) | 130 | — |
| **Total** | **391** | — |

### Test Categories

| Category | Coverage |
|----------|----------|
| Geometry kernel | Point/Vec math, intersections, offsets, bezier |
| Segment types | Line, Arc, Spiral, Bezier evaluation |
| SegmentSequence | Concatenation, positionAt, headingAt, curvatureAt |
| RoadV2 | Ownership, deep copy, mutation, view rebuild |
| Forward adapter (exact) | Line, Bezier, Arc, Spiral from metadata |
| Forward adapter (legacy) | Corner→Line, Smooth→Bezier |
| Inverse adapter | All segment types → ControlPoints |
| Round-trip | Lossless verification for all types |
| Golden fixtures | 17 tests comparing against golden geometry |
| Stress | 500-segment mixed, 100-segment round-trip |
| Bridge IPC | 17 tests for new V2 bridge functions |
| Information loss | ReverseAdapterReport diagnostics |

---

## 13. Technical Debt & Future Work

### Phase 2 — Lane Engine

- [ ] Define `LaneSection` properly (lane width polynomials, lane count)
- [ ] Replace `RoadV2::width` / `RoadV2::laneCount` with LaneSection-derived
- [ ] Lane boundary generation (left/right edges from centerline + widths)
- [ ] Lane centerlines (per-lane sampling)
- [ ] Lane markings (solid, dashed, double, etc.)
- [ ] Mesh V2 (per-lane mesh generation)

### Phase 3 — Road Graph & Junction

- [ ] `RoadGraph` class (topological network of roads + intersections)
- [ ] Junction generation from graph topology
- [ ] Turn-level routing (lane-to-lane connections)
- [ ] Conflict detection (intersection polygon overlap)

### Phase 4 — GeometryRecognizer

- [ ] Fit legacy ControlPoint[] to Arc/Spiral when no metadata exists
- [ ] Detect circular arcs from point sequences
- [ ] Detect clothoid transitions from curvature profiles
- [ ] Upgrade formatVersion=1 roads to formatVersion=2 automatically

### Phase 5 — Mesh V2

- [ ] Per-lane mesh generation (not just road strip)
- [ ] Variable width along road (from LaneSection polynomials)
- [ ] Superelevation banking
- [ ] LOD (level of detail) for large networks

### Infrastructure

- [ ] CI pipeline (GitHub Actions: doctest + vitest + golden on every PR)
- [ ] Doxygen configuration for HTML API docs
- [ ] Architecture diagrams (rendered from this doc)
- [ ] Benchmark regression tracking

### Cleanup (post-Phase-1 PR)

- [ ] Remove duplicate helper functions in test file
- [ ] Consolidate `makeSmoothCP` / `makeLegacyRoad` test helpers
- [ ] Add `consteval` assertions for compile-time invariant checks
- [ ] Consider `std::variant` alternative for segment storage (performance)
