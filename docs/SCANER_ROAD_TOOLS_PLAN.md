# SCANeR Road Tools Replication Plan

## Research Summary

### SCANeR Studio Road Tools (from AVSimulation docs + OpenDRIVE spec)

| Tool | Geometry | Curvature | Tangent Continuity |
|------|----------|-----------|-------------------|
| Segment | Straight line | 0 (constant) | G1 with adjacent |
| Circle Arc | Circular arc | Constant (1/R) | G1 with adjacent |
| Clothoid Arc | Euler spiral | Linear (κ = s/A²) | G1 + G2 with adjacent |
| Polyline | Piecewise linear | 0 (discontinuous at vertices) | G0 (corners) |
| Bézier | Cubic Bézier | Variable | G1 if handles aligned |
| Clothoid Spline | Multi-segment clothoid | Continuous (G2) | G2 everywhere |

### Key Mathematics

**Clothoid (Euler Spiral):**
- Curvature: κ(s) = s / A² (linear in arc length)
- A = clothoid parameter (√(R×L))
- Position: x(s) = ∫₀ˢ cos(τ²/2A²) dτ, y(s) = ∫₀ˢ sin(τ²/2A²) dτ
- Fresnel integrals: C(t) = ∫₀ᵗ cos(πu²/2) du, S(t) = ∫₀ᵗ sin(πu²/2) du

**Circle Arc (tangent-continuous):**
- Given start point + start direction + end point
- Center = perpendicular to start direction at distance R
- R = chord / (2 × sin(sweep/2))
- Sweep = 2 × angle between start direction and chord

**G2 Continuity (clothoid spline):**
- Curvature must be continuous at junctions
- Each segment is a clothoid with κ_start and κ_end
- Adjacent segments share curvature at junction

### Current Implementation Gaps

| Feature | Current | SCANeR Expected | Status |
|---------|---------|-----------------|--------|
| Segment tool | Basic line | Straight + tangent continuity | PARTIAL |
| Circle Arc | Implemented in C++ | Tangent-continuous, editable radius | GOOD |
| Clothoid Arc | Implemented in C++ | Fresnel-based, G2 continuity | GOOD |
| Polyline | Not separate tool | Multi-point with corner smoothing | MISSING |
| Bézier | Basic cubic | Handle constraints, tangent preservation | PARTIAL |
| Clothoid Spline | Not implemented | G2 continuous multi-segment | MISSING |
| Road Mesh | Triangle strip | Lanes, sidewalks, UVs, markings | BASIC |
| Intersection | Edge-based | Edge + fillet + lane graph | PARTIAL |
| Snapping | Grid only | Endpoint, road, tangent, intersection | MINIMAL |
| Editing | Move points only | Radius, width, lanes, split, merge | MINIMAL |

## Implementation Plan

### Phase 1: C++ Road Tools Module (`road_tools.hpp`)
- ToolType enum (Segment, CircleArc, ClothoidArc, Polyline, Bezier, ClothoidSpline)
- createSegment(start, end) → Road
- createCircleArc(start, startDir, end) → Road
- createClothoidArc(start, startDir, end, endDir) → Road
- createPolyline(points[]) → Road
- createBezier(points[], handles[]) → Road
- createClothoidSpline(points[], tangentConstraints) → Road

### Phase 2: Clothoid Spline (`clothoid_spline.hpp`)
- Fit G2-continuous clothoid spline through points
- Walton-Meek algorithm for transition spirals
- Curvature continuity at junctions

### Phase 3: Road Geometry Enhancement (`road_geometry.hpp`)
- Lane-level centerlines
- Road edge computation (left, right, lane boundaries)
- Sidewalk/shoulder offsets
- Superelevation (banking in curves)
- UV coordinate generation

### Phase 4: Mesh Enhancement (`mesh.hpp` update)
- Lane boundary lines
- Lane markings (dashed, solid, double)
- Sidewalk meshes
- Intersection surface with proper triangulation
- UV mapping for textures

### Phase 5: Intersection Enhancement (`intersection.hpp` update)
- Edge-based polygon with proper fillet arcs
- Lane graph (lane-to-lane connections)
- Stop lines and crosswalks
- Turn restriction paths

### Phase 6: Editing System (`road_editor.hpp`)
- insertPoint(road, position, index)
- deletePoint(road, index)
- movePoint(road, index, newPosition)
- setRadius(road, segmentIndex, newRadius)
- setWidth(road, newWidth)
- setLaneCount(road, newLaneCount)
- splitRoad(road, position) → [Road, Road]
- mergeRoads(road1, road2) → Road

### Phase 7: Snapping System (`snapping.hpp`)
- snapToEndpoint(point, roads, threshold)
- snapToRoad(point, roads, threshold)
- snapToGrid(point, gridSize)
- snapToTangent(point, direction, roads, threshold)
- snapToIntersection(point, intersections, threshold)

### Phase 8: N-API Bindings + IPC + UI Integration
- Expose all new functions via road_bridge.cpp
- Add IPC channels
- Update roadEngineClient.ts
- Update RoadToolbar with all 6 tools
- Update RoadViewport for each tool's preview
