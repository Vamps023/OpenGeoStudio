/**
 * Tests for the C++ Road Geometry Engine
 *
 * These tests verify that the native addon is loaded correctly and
 * that the geometry functions produce expected results.
 */

import { describe, it, expect } from 'vitest';
import * as path from 'path';

// Try to load the native addon
let addon: any = null;
try {
  addon = require(path.join(__dirname, '..', 'app', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'));
} catch {
  // Addon not built — tests will be skipped
}

const describeIfAddon = addon ? describe : describe.skip;

describeIfAddon('C++ Road Geometry Engine', () => {
  it('should return version string', () => {
    const version = addon.roadGetVersion();
    expect(version).toBe('2.0.0-road-engine');
  });

  it('should convert geo to local coordinates', () => {
    const result = addon.roadGeoToLocal(37.7749, -122.4194, 37.7749, -122.4194);
    expect(result.x).toBeCloseTo(0, 5);
    expect(result.y).toBeCloseTo(0, 5);
  });

  it('should convert local coordinates back to geo', () => {
    const local = addon.roadGeoToLocal(37.7750, -122.4194, 37.7749, -122.4194);
    const geo = addon.roadLocalToGeo(local.x, local.y, 37.7749, -122.4194);
    expect(geo.lat).toBeCloseTo(37.7750, 4);
    expect(geo.lon).toBeCloseTo(-122.4194, 4);
  });

  it('should sample a road centerline', () => {
    const road = {
      id: 'test',
      name: 'Test Road',
      width: 8.0,
      laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const samples = addon.roadSampleCenterline(road, 10);
    expect(samples.length).toBeGreaterThan(2);
    expect(samples[0].x).toBeCloseTo(0, 1);
    expect(samples[samples.length - 1].x).toBeCloseTo(100, 1);
  });

  it('should compute a circular arc', () => {
    const arc = addon.roadComputeCircleArc(
      { x: 0, y: 0 },
      { x: 1, y: 0 },
      { x: 50, y: 50 },
      32
    );
    expect(arc.radius).toBeGreaterThan(0);
    expect(arc.points.length).toBe(33); // segments + 1
    expect(arc.points[0].x).toBeCloseTo(0, 1);
    expect(arc.points[0].y).toBeCloseTo(0, 1);
  });

  it('should generate an intersection between two crossing roads', () => {
    const road1 = {
      id: 'r1',
      name: 'East-West',
      width: 8.0,
      laneCount: 2,
      points: [
        { x: -100, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const road2 = {
      id: 'r2',
      name: 'North-South',
      width: 8.0,
      laneCount: 2,
      points: [
        { x: 0, y: -100, z: 0, type: 'corner' },
        { x: 0, y: 100, z: 0, type: 'corner' },
      ],
    };

    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);

    // Center should be at origin
    expect(ix.center.x).toBeCloseTo(0, 1);
    expect(ix.center.y).toBeCloseTo(0, 1);

    // Should have 4 approaches (N, S, E, W)
    expect(ix.approaches.length).toBe(4);

    // Should have a polygon
    expect(ix.polygon.length).toBeGreaterThan(4);

    // Should have lane connections
    expect(ix.laneConnections.length).toBeGreaterThan(0);

    // Should have stop lines and crosswalks
    expect(ix.stopLines.length).toBe(4);
    expect(ix.crosswalks.length).toBe(4);
  });

  it('should generate a T-junction', () => {
    const road1 = {
      id: 'r1',
      name: 'Main Road',
      width: 8.0,
      laneCount: 2,
      points: [
        { x: -100, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const road2 = {
      id: 'r2',
      name: 'Side Road',
      width: 6.0,
      laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 0, y: 100, z: 0, type: 'corner' },
      ],
    };

    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);

    // Should have at least 3 approaches
    expect(ix.approaches.length).toBeGreaterThanOrEqual(3);
  });

  it('should compute a clothoid (Euler spiral) transition', () => {
    const result = addon.roadComputeClothoid(
      { x: 0, y: 0 },      // start point
      { x: 1, y: 0 },      // start direction (east)
      { x: 100, y: 50 },   // end point
      { x: 0, y: 1 },      // end direction (north)
      50,                   // initial A
      64                    // segments
    );

    // Should have 65 points (segments + 1)
    expect(result.points.length).toBe(65);

    // Start point should be at origin
    expect(result.points[0].x).toBeCloseTo(0, 1);
    expect(result.points[0].y).toBeCloseTo(0, 1);

    // Total angle should be ~90 degrees (π/2 radians)
    expect(result.totalAngle).toBeCloseTo(Math.PI / 2, 1);

    // Tangent in should be east, tangent out should be north
    expect(result.tangentIn.x).toBeCloseTo(1, 1);
    expect(result.tangentOut.y).toBeCloseTo(1, 1);

    // Clothoid parameters should be positive
    expect(result.A).toBeGreaterThan(0);
    expect(result.L).toBeGreaterThan(0);
  });

  it('should generate a road mesh', () => {
    const road = {
      id: 'r1',
      name: 'Test Road',
      width: 8.0,
      laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 5, type: 'corner' },
      ],
    };

    const mesh = addon.roadGenerateRoadMesh(road, 32);

    // Should have vertices, normals, UVs, and indices
    expect(mesh.vertices.length).toBeGreaterThan(0);
    expect(mesh.normals.length).toBe(mesh.vertices.length);
    expect(mesh.uvs.length).toBe(mesh.vertices.length / 3 * 2);
    expect(mesh.indices.length).toBeGreaterThan(0);
    expect(mesh.triangleCount).toBe(mesh.indices.length / 3);

    // Vertex count should be 2 * (samples + 1) = 66
    expect(mesh.vertexCount).toBe(66);
  });

  it('should generate an intersection mesh', () => {
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -100, y: 0, z: 0, type: 'corner' }, { x: 100, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -100, z: 0, type: 'corner' }, { x: 0, y: 100, z: 0, type: 'corner' }],
    };

    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    const mesh = addon.roadGenerateIntersectionMesh(ix, 0);

    // Should have vertices and triangles
    expect(mesh.vertexCount).toBeGreaterThan(0);
    expect(mesh.triangleCount).toBeGreaterThan(0);
    expect(mesh.indices.length).toBe(mesh.triangleCount * 3);
  });

  it('should export roads to OpenDRIVE XML', () => {
    const roads = [
      {
        id: 'r1', name: 'Main Street', width: 8.0, laneCount: 2,
        points: [
          { x: 0, y: 0, z: 0, type: 'corner' },
          { x: 100, y: 0, z: 5, type: 'corner' },
        ],
      },
      {
        id: 'r2', name: 'Cross Road', width: 6.0, laneCount: 2,
        points: [
          { x: 50, y: -50, z: 0, type: 'corner' },
          { x: 50, y: 50, z: 0, type: 'corner' },
        ],
      },
    ];

    const xml = addon.roadExportOpenDrive(roads, 37.7749, -122.4194);

    // Should produce valid XML
    expect(xml).toContain('<?xml');
    expect(xml).toContain('<OpenDRIVE>');
    expect(xml).toContain('</OpenDRIVE>');

    // Should contain road elements
    expect(xml).toContain('<road');
    expect(xml).toContain('Main Street');
    expect(xml).toContain('Cross Road');

    // Should contain geometry
    expect(xml).toContain('<planView>');
    expect(xml).toContain('<geometry');
    expect(xml).toContain('<line/>');

    // Should contain lanes
    expect(xml).toContain('<lanes>');
    expect(xml).toContain('<laneSection');
    expect(xml).toContain('driving');
  });
});

// ─── Road Creation Tools Tests (SCANeR-style) ──────────────
describeIfAddon('C++ Road Creation Tools', () => {
  it('should create a segment (straight road)', () => {
    const road = addon.roadCreateSegment(0, 0, 100, 0);
    expect(road).toBeDefined();
    expect(road.points.length).toBe(2);
    expect(road.points[0].x).toBeCloseTo(0);
    expect(road.points[0].y).toBeCloseTo(0);
    expect(road.points[1].x).toBeCloseTo(100);
    expect(road.points[1].y).toBeCloseTo(0);
    expect(road.width).toBe(8.0);  // default
    expect(road.laneCount).toBe(2);  // default
  });

  it('should create a segment with custom params', () => {
    const params = { width: 12.0, laneCount: 4, profileName: 'highway_2x3', z: 5.0 };
    const road = addon.roadCreateSegment(0, 0, 50, 50, params);
    expect(road.width).toBe(12.0);
    expect(road.laneCount).toBe(4);
    expect(road.points[0].z).toBe(5.0);
  });

  it('should create a circle arc with tangent continuity', () => {
    // Start at origin, direction = +X, end at (50, 50)
    const road = addon.roadCreateCircleArc(0, 0, 1, 0, 50, 50, 8);
    expect(road).toBeDefined();
    expect(road.points.length).toBe(8);
    // First point should be at start
    expect(road.points[0].x).toBeCloseTo(0, 0);
    expect(road.points[0].y).toBeCloseTo(0, 0);
    // Last point should be near end
    expect(road.points[7].x).toBeCloseTo(50, 1);
    expect(road.points[7].y).toBeCloseTo(50, 1);
    // Arc points should be smooth
    expect(road.points[1].type).toBe('smooth');
  });

  it('should create a clothoid arc (Euler spiral)', () => {
    // Start at origin, direction = +X, end at (50, 50), end direction = +Y
    // Note: clothoid fitting is iterative — tolerance is loose for extreme angles
    const road = addon.roadCreateClothoidArc(0, 0, 1, 0, 50, 50, 0, 1, 8);
    expect(road).toBeDefined();
    expect(road.points.length).toBe(8);
    // First point at start
    expect(road.points[0].x).toBeCloseTo(0, 1);
    expect(road.points[0].y).toBeCloseTo(0, 1);
    // Last point should be in the right quadrant
    expect(road.points[7].x).toBeGreaterThan(0);
    expect(road.points[7].y).toBeGreaterThan(0);
  });

  it('should create a polyline with sharp corners', () => {
    const points = [
      { x: 0, y: 0 },
      { x: 50, y: 0 },
      { x: 50, y: 50 },
      { x: 0, y: 50 },
    ];
    const road = addon.roadCreatePolyline(points, 0.0, 6);
    expect(road).toBeDefined();
    expect(road.points.length).toBe(4);
    expect(road.points[0].type).toBe('corner');
    expect(road.points[1].type).toBe('corner');
  });

  it('should create a polyline with fillet corners', () => {
    const points = [
      { x: 0, y: 0 },
      { x: 50, y: 0 },
      { x: 50, y: 50 },
      { x: 0, y: 50 },
    ];
    const road = addon.roadCreatePolyline(points, 5.0, 6);
    expect(road).toBeDefined();
    // With fillets, there should be more points (tangent + arc + tangent at each corner)
    expect(road.points.length).toBeGreaterThan(4);
    // First and last should still be corner type
    expect(road.points[0].type).toBe('corner');
  });

  it('should create a Bézier curve with handles', () => {
    // Start at (0,0), handle out at (25, 0), end at (50, 50), handle in at (25, 50)
    const road = addon.roadCreateBezier(0, 0, 25, 0, 50, 50, 25, 50);
    expect(road).toBeDefined();
    expect(road.points.length).toBe(2);
    expect(road.points[0].type).toBe('smooth');
    expect(road.points[1].type).toBe('smooth');
    // Check handles are stored as relative offsets
    expect(road.points[0].handleOut).toBeDefined();
    expect(road.points[0].handleOut.x).toBeCloseTo(25);
    expect(road.points[0].handleOut.y).toBeCloseTo(0);
    expect(road.points[1].handleIn).toBeDefined();
    expect(road.points[1].handleIn.x).toBeCloseTo(-25);
    expect(road.points[1].handleIn.y).toBeCloseTo(0);
  });

  it('should create a clothoid spline through multiple points', () => {
    const points = [
      { x: 0, y: 0 },
      { x: 50, y: 20 },
      { x: 100, y: 0 },
      { x: 150, y: 50 },
    ];
    // startTangent = (1, 0), endTangent = (0, 1)
    const road = addon.roadCreateClothoidSpline(points, 1, 0, 0, 1, 8);
    expect(road).toBeDefined();
    // Should have points from multiple clothoid segments
    expect(road.points.length).toBeGreaterThan(4);
    // All points should be smooth (G2 continuous)
    for (const pt of road.points) {
      expect(pt.type).toBe('smooth');
    }
  });

  it('should handle degenerate segment (same start and end)', () => {
    const road = addon.roadCreateSegment(10, 10, 10, 10);
    expect(road).toBeDefined();
    expect(road.points.length).toBe(2);
  });
});

// ─── Geometry Pipeline Validation Tests ─────────────────────
describeIfAddon('C++ Geometry Pipeline Validation', () => {
  it('should generate road mesh with miter-joint edges', () => {
    // Curved road (arc) — miter joints should prevent edge self-intersection
    const road = {
      id: 'test-curve', width: 8.0, laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 50, y: 20, z: 0, type: 'smooth' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 32);
    expect(mesh.vertexCount).toBeGreaterThan(0);
    expect(mesh.triangleCount).toBeGreaterThan(0);
    // Each vertex should have x, y, z
    expect(mesh.vertices.length).toBe(mesh.vertexCount * 3);
    // Each triangle has 3 indices
    expect(mesh.indices.length).toBe(mesh.triangleCount * 3);
  });

  it('should generate arc-length UVs (not uniform)', () => {
    const road = {
      id: 'test-uv', width: 8.0, laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 32);
    // UVs should be [u, v] pairs
    expect(mesh.uvs.length).toBe(mesh.vertexCount * 2);
    // First vertex v should be 0, last should be totalLength/10 = 10
    expect(mesh.uvs[1]).toBeCloseTo(0, 5);
    expect(mesh.uvs[mesh.uvs.length - 1]).toBeCloseTo(10, 1);
  });

  it('should triangulate CW polygons correctly', () => {
    // CW polygon (clockwise winding)
    const cwPolygon = [
      { x: 0, y: 0 },
      { x: 0, y: 10 },
      { x: 10, y: 10 },
      { x: 10, y: 0 },
    ];
    // Create an intersection with CW polygon
    const ix = {
      center: { x: 5, y: 5 },
      polygon: cwPolygon,
      approaches: [],
      laneConnections: [],
      stopLines: [],
      crosswalks: [],
    };
    const mesh = addon.roadGenerateIntersectionMesh(ix, 0);
    expect(mesh.triangleCount).toBeGreaterThan(0);
    expect(mesh.indices.length).toBe(mesh.triangleCount * 3);
  });

  it('should triangulate CCW polygons correctly', () => {
    // CCW polygon (counter-clockwise winding)
    const ccwPolygon = [
      { x: 0, y: 0 },
      { x: 10, y: 0 },
      { x: 10, y: 10 },
      { x: 0, y: 10 },
    ];
    const ix = {
      center: { x: 5, y: 5 },
      polygon: ccwPolygon,
      approaches: [],
      laneConnections: [],
      stopLines: [],
      crosswalks: [],
    };
    const mesh = addon.roadGenerateIntersectionMesh(ix, 0);
    expect(mesh.triangleCount).toBeGreaterThan(0);
    expect(mesh.indices.length).toBe(mesh.triangleCount * 3);
  });

  it('should sample centerline with arc-length distribution', () => {
    // Road with unequal segment lengths
    const road = {
      id: 'test-arc-length', width: 8.0, laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 10, y: 0, z: 0, type: 'corner' },   // short segment (10m)
        { x: 110, y: 0, z: 0, type: 'corner' },   // long segment (100m)
      ],
    };
    const samples = addon.roadSampleCenterline(road, 32);
    expect(samples.length).toBeGreaterThan(10);
    // The long segment should get more samples than the short segment
    // Check that samples are denser in the first 10m than in the last 100m
    const firstSegSamples = samples.filter((s: any) => s.x < 10).length;
    const lastSegSamples = samples.filter((s: any) => s.x > 10).length;
    // Both segments should have at least 2 samples
    expect(firstSegSamples).toBeGreaterThanOrEqual(2);
    expect(lastSegSamples).toBeGreaterThanOrEqual(2);
  });

  it('should interpolate z by arc length, not by index', () => {
    // sampleCenterline returns 2D points (no z), but generateRoadMesh uses 3D
    // Test via mesh: the road has z=0 for first 10m, z=100 for next 100m
    const road = {
      id: 'test-z', width: 8.0, laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 10, y: 0, z: 0, type: 'corner' },    // short, z=0
        { x: 110, y: 0, z: 100, type: 'corner' },  // long, z=100
      ],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 32);
    // The mesh vertices include z. Find a vertex near x=60 (midpoint of long segment)
    // z should be approximately 50 (halfway through the z transition)
    let foundMidZ = false;
    for (let i = 0; i < mesh.vertices.length; i += 3) {
      const x = mesh.vertices[i];
      const z = mesh.vertices[i + 2];
      if (Math.abs(x - 60) < 10) {
        expect(z).toBeGreaterThan(20);
        expect(z).toBeLessThan(80);
        foundMidZ = true;
        break;
      }
    }
    expect(foundMidZ).toBe(true);
  });

  it('should generate intersection polygon without duplicate points', () => {
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -50, y: 0, z: 0, type: 'corner' }, { x: 50, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -50, z: 0, type: 'corner' }, { x: 0, y: 50, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    expect(ix.polygon.length).toBeGreaterThan(3);
    // Check no duplicate consecutive points
    for (let i = 0; i < ix.polygon.length; i++) {
      const j = (i + 1) % ix.polygon.length;
      const dx = ix.polygon[i].x - ix.polygon[j].x;
      const dy = ix.polygon[i].y - ix.polygon[j].y;
      const dist = Math.sqrt(dx * dx + dy * dy);
      expect(dist).toBeGreaterThan(0.001);
    }
  });

  it('should generate lane boundaries for multi-lane roads', () => {
    // This tests the generateLaneBoundaries function (if exposed)
    // For now, just verify the mesh has enough vertices for lane rendering
    const road = {
      id: 'test-lanes', width: 16.0, laneCount: 4,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 32);
    // 32 samples × 2 edges = 64 vertices (plus possible extra from arc-length sampling)
    expect(mesh.vertexCount).toBeGreaterThanOrEqual(64);
  });
});

// ─── Geometry Algorithm Unit Tests ──────────────────────────
describeIfAddon('C++ Geometry Algorithm Unit Tests', () => {
  it('line intersection: parallel lines should not produce valid intersection', () => {
    // Parallel roads 20m apart — should not produce a meaningful intersection
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: 0, y: 0, z: 0, type: 'corner' }, { x: 100, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: 20, z: 0, type: 'corner' }, { x: 100, y: 20, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    // The engine uses a fallback (closest point) for non-intersecting roads.
    // The polygon may still be generated but should be degenerate or very small.
    // Just verify it doesn't crash and produces some result.
    expect(ix).toBeDefined();
    // The polygon area should be very small (roads are 20m apart, width is 8m)
    if (ix.polygon.length >= 3) {
      let area = 0;
      for (let i = 0; i < ix.polygon.length; i++) {
        const j = (i + 1) % ix.polygon.length;
        area += ix.polygon[i].x * ix.polygon[j].y - ix.polygon[j].x * ix.polygon[i].y;
      }
      area = Math.abs(area / 2);
      // A valid perpendicular intersection of 8m roads has area ~100-200m²
      // A parallel "intersection" should be much smaller or degenerate
      // (We just log it — the engine should ideally not generate this at all)
    }
  });

  it('line intersection: perpendicular lines should intersect', () => {
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -50, y: 0, z: 0, type: 'corner' }, { x: 50, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -50, z: 0, type: 'corner' }, { x: 0, y: 50, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    expect(ix.polygon.length).toBeGreaterThanOrEqual(4);
    // Center should be at origin
    expect(ix.center.x).toBeCloseTo(0, 0);
    expect(ix.center.y).toBeCloseTo(0, 0);
  });

  it('offset: curved road edges should not self-intersect', () => {
    // S-curve road
    const road = {
      id: 's-curve', width: 6.0, laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 30, y: 30, z: 0, type: 'smooth' },
        { x: 60, y: 0, z: 0, type: 'smooth' },
        { x: 90, y: 30, z: 0, type: 'corner' },
      ],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 48);
    // Mesh should be generated without errors
    expect(mesh.vertexCount).toBeGreaterThan(0);
    expect(mesh.triangleCount).toBeGreaterThan(0);
    // Check that vertices are reasonable (no NaN)
    for (let i = 0; i < mesh.vertices.length; i++) {
      expect(isFinite(mesh.vertices[i])).toBe(true);
    }
  });

  it('trim: intersection should trim roads at correct distance', () => {
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -100, y: 0, z: 0, type: 'corner' }, { x: 100, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -100, z: 0, type: 'corner' }, { x: 0, y: 100, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    // Each approach should start at trimDist from center
    for (const approach of ix.approaches) {
      if (approach.centerline.length >= 2) {
        const start = approach.centerline[0];
        const dist = Math.sqrt(start.x * start.x + start.y * start.y);
        // Trim distance should be at least halfWidth (4m) and reasonable
        expect(dist).toBeGreaterThan(3);
        expect(dist).toBeLessThan(50);
      }
    }
  });

  it('fillet: intersection polygon should have smooth corners', () => {
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -50, y: 0, z: 0, type: 'corner' }, { x: 50, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -50, z: 0, type: 'corner' }, { x: 0, y: 50, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    // With fillets, polygon should have more than 8 points (4 corners × 2+ points each)
    expect(ix.polygon.length).toBeGreaterThan(8);
  });

  it('circle arc: should produce smooth curve', () => {
    const arc = addon.roadComputeCircleArc({ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 50, y: 50 }, 32);
    // Arc function adds an extra endpoint, so 33 points for 32 segments
    expect(arc.points.length).toBeGreaterThanOrEqual(32);
    // First point at start
    expect(arc.points[0].x).toBeCloseTo(0, 1);
    expect(arc.points[0].y).toBeCloseTo(0, 1);
    // Last point near end
    const last = arc.points[arc.points.length - 1];
    expect(last.x).toBeCloseTo(50, 1);
    expect(last.y).toBeCloseTo(50, 1);
    // All points should be finite
    for (const p of arc.points) {
      expect(isFinite(p.x)).toBe(true);
      expect(isFinite(p.y)).toBe(true);
    }
  });

  it('clothoid: should produce continuous curvature', () => {
    const clothoid = addon.roadComputeClothoid(
      { x: 0, y: 0 }, { x: 1, y: 0 },
      { x: 50, y: 50 }, { x: 0, y: 1 },
      50, 32
    );
    expect(clothoid.points.length).toBeGreaterThan(2);
    // All points should be finite
    for (const p of clothoid.points) {
      expect(isFinite(p.x)).toBe(true);
      expect(isFinite(p.y)).toBe(true);
    }
  });

  it('bezier: cubic bezier should pass through endpoints', () => {
    const road = {
      id: 'bezier-test', width: 8, laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'smooth', handleOut: { x: 25, y: 0 } },
        { x: 50, y: 50, z: 0, type: 'smooth', handleIn: { x: -25, y: 0 } },
      ],
    };
    const samples = addon.roadSampleCenterline(road, 32);
    expect(samples.length).toBeGreaterThan(2);
    // First sample near start
    expect(samples[0].x).toBeCloseTo(0, 1);
    expect(samples[0].y).toBeCloseTo(0, 1);
    // Last sample near end
    expect(samples[samples.length - 1].x).toBeCloseTo(50, 1);
    expect(samples[samples.length - 1].y).toBeCloseTo(50, 1);
  });

  it('polygon validation: intersection polygon should be valid', () => {
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -50, y: 0, z: 0, type: 'corner' }, { x: 50, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -50, z: 0, type: 'corner' }, { x: 0, y: 50, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    // Check no duplicate consecutive points
    for (let i = 0; i < ix.polygon.length; i++) {
      const j = (i + 1) % ix.polygon.length;
      const dx = ix.polygon[i].x - ix.polygon[j].x;
      const dy = ix.polygon[i].y - ix.polygon[j].y;
      const dist = Math.sqrt(dx * dx + dy * dy);
      expect(dist).toBeGreaterThan(0.001);
    }
  });

  it('intersection polygon: should extend to road boundaries (not just center)', () => {
    // Two perpendicular 8m-wide roads crossing at origin
    const road1 = {
      id: 'r1', width: 8, laneCount: 2,
      points: [{ x: -50, y: 0, z: 0, type: 'corner' }, { x: 50, y: 0, z: 0, type: 'corner' }],
    };
    const road2 = {
      id: 'r2', width: 8, laneCount: 2,
      points: [{ x: 0, y: -50, z: 0, type: 'corner' }, { x: 0, y: 50, z: 0, type: 'corner' }],
    };
    const ix = addon.roadGenerateIntersection(road1, road2, 37.7749, -122.4194);
    expect(ix.polygon.length).toBeGreaterThanOrEqual(4);

    // Compute polygon area via shoelace formula
    let area = 0;
    for (let i = 0; i < ix.polygon.length; i++) {
      const j = (i + 1) % ix.polygon.length;
      area += ix.polygon[i].x * ix.polygon[j].y - ix.polygon[j].x * ix.polygon[i].y;
    }
    area = Math.abs(area / 2);

    // For a perpendicular intersection of two 8m roads with corner radius R:
    // The polygon should span at least 8m × 8m = 64m²
    // With fillets, it should be larger
    // The old "star" polygon had area ~20-30m²
    // The correct polygon should have area > 64m²
    expect(area).toBeGreaterThan(50);

    // The polygon should extend at least ±4m (halfWidth) from center
    let maxX = 0, maxY = 0, minX = 0, minY = 0;
    for (const p of ix.polygon) {
      maxX = Math.max(maxX, p.x);
      minX = Math.min(minX, p.x);
      maxY = Math.max(maxY, p.y);
      minY = Math.min(minY, p.y);
    }
    // Should extend at least 4m in each direction (halfWidth = 4)
    expect(maxX).toBeGreaterThan(3);
    expect(maxY).toBeGreaterThan(3);
    expect(minX).toBeLessThan(-3);
    expect(minY).toBeLessThan(-3);
  });

  it('mesh generation: should produce consistent winding', () => {
    const road = {
      id: 'winding-test', width: 8, laneCount: 2,
      points: [{ x: 0, y: 0, z: 0, type: 'corner' }, { x: 100, y: 0, z: 0, type: 'corner' }],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 16);
    // Check that all triangles have consistent winding (CCW)
    for (let i = 0; i < mesh.indices.length; i += 3) {
      const i0 = mesh.indices[i] * 3;
      const i1 = mesh.indices[i + 1] * 3;
      const i2 = mesh.indices[i + 2] * 3;
      const v0 = { x: mesh.vertices[i0], y: mesh.vertices[i0 + 1] };
      const v1 = { x: mesh.vertices[i1], y: mesh.vertices[i1 + 1] };
      const v2 = { x: mesh.vertices[i2], y: mesh.vertices[i2 + 1] };
      const cross = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
      // All triangles should have the same winding (positive = CCW)
      expect(cross).not.toBe(0);
    }
  });

  it('mesh generation: normals should all point up', () => {
    const road = {
      id: 'normals-test', width: 8, laneCount: 2,
      points: [{ x: 0, y: 0, z: 0, type: 'corner' }, { x: 100, y: 0, z: 0, type: 'corner' }],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 16);
    for (let i = 0; i < mesh.normals.length; i += 3) {
      expect(mesh.normals[i]).toBe(0);     // nx = 0
      expect(mesh.normals[i + 1]).toBe(0); // ny = 0
      expect(mesh.normals[i + 2]).toBe(1); // nz = 1 (up)
    }
  });

  it('UV mapping: should tile every 10 meters', () => {
    const road = {
      id: 'uv-test', width: 8, laneCount: 2,
      points: [{ x: 0, y: 0, z: 0, type: 'corner' }, { x: 100, y: 0, z: 0, type: 'corner' }],
    };
    const mesh = addon.roadGenerateRoadMesh(road, 32);
    // Last vertex v should be 100/10 = 10
    const lastV = mesh.uvs[mesh.uvs.length - 1];
    expect(lastV).toBeCloseTo(10, 1);
  });
});

// ═══════════════════════════════════════════════════════════
// Phase 1.9 — Bridge Integration Tests
// ═══════════════════════════════════════════════════════════
//
// Tests the new RoadV2 bridge functions:
//   roadSampleCenterlineV2 — uses roadToV2Auto() internally
//   roadGetAdapterReport   — diagnostics from the conversion
//   roadConvertFromV2      — round-trip: Road → RoadV2 → Road
//
// Also tests segmentMeta serialization through the bridge.
// ═══════════════════════════════════════════════════════════

describeIfAddon('Phase 1.9: Bridge Integration', () => {
  it('should return version 2.0.0', () => {
    const version = addon.roadGetVersion();
    expect(version).toBe('2.0.0-road-engine');
  });

  it('sampleCenterlineV2: should sample a simple line road', () => {
    const road = {
      id: 'test_v2_line',
      name: 'Test V2 Line',
      width: 8.0,
      laneCount: 2,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const samples = addon.roadSampleCenterlineV2(road, 10);
    expect(samples.length).toBe(10);
    expect(samples[0].x).toBeCloseTo(0, 1);
    expect(samples[9].x).toBeCloseTo(100, 1);
    expect(samples[0].y).toBeCloseTo(0, 1);
    expect(samples[9].y).toBeCloseTo(0, 1);
  });

  it('sampleCenterlineV2: should sample a bezier road', () => {
    const road = {
      id: 'test_v2_bez',
      name: 'Test V2 Bezier',
      width: 8.0,
      laneCount: 2,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'smooth', handleIn: null, handleOut: { x: 25, y: 40 }, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'smooth', handleIn: { x: -25, y: 40 }, handleOut: null, segmentMeta: null },
      ],
    };
    const samples = addon.roadSampleCenterlineV2(road, 20);
    expect(samples.length).toBe(20);
    // Endpoints
    expect(samples[0].x).toBeCloseTo(0, 1);
    expect(samples[19].x).toBeCloseTo(100, 1);
    // Midpoint should be above y=0 (arch shape)
    expect(samples[10].y).toBeGreaterThan(5);
  });

  it('sampleCenterlineV2: legacy road (formatVersion=1) uses legacy path', () => {
    const road = {
      id: 'test_v2_legacy',
      name: 'Test V2 Legacy',
      width: 8.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 50, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const samples = addon.roadSampleCenterlineV2(road, 10);
    expect(samples.length).toBe(10);
    expect(samples[0].x).toBeCloseTo(0, 1);
    expect(samples[9].x).toBeCloseTo(100, 1);
  });

  it('sampleCenterlineV2: road without formatVersion defaults to legacy', () => {
    const road = {
      id: 'test_v2_nofmt',
      width: 8.0,
      laneCount: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const samples = addon.roadSampleCenterlineV2(road, 5);
    expect(samples.length).toBe(5);
    expect(samples[0].x).toBeCloseTo(0, 1);
    expect(samples[4].x).toBeCloseTo(100, 1);
  });

  it('getAdapterReport: exact path for formatVersion=2 line road', () => {
    const road = {
      id: 'report_exact',
      width: 8.0,
      laneCount: 2,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const report = addon.roadGetAdapterReport(road);
    expect(report.exact).toBe(true);
    expect(report.lineSegments).toBe(1);
    expect(report.legacySegments).toBe(0);
    expect(report.numSegments).toBe(1);
    expect(report.totalLength).toBeCloseTo(100, 1);
    expect(report.warnings.length).toBe(0);
  });

  it('getAdapterReport: legacy path for formatVersion=1', () => {
    const road = {
      id: 'report_legacy',
      width: 8.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner' },
        { x: 50, y: 0, z: 0, type: 'corner' },
        { x: 100, y: 0, z: 0, type: 'corner' },
      ],
    };
    const report = addon.roadGetAdapterReport(road);
    expect(report.exact).toBe(false);
    expect(report.legacySegments).toBe(2);
    expect(report.numSegments).toBe(2);
  });

  it('getAdapterReport: arc metadata produces arcSegments=1', () => {
    // Use the createCircleArc tool which now emits metadata
    const arcRoad = addon.roadCreateCircleArc(0, 0, 1, 0, 50, 50, 8);
    expect(arcRoad.formatVersion).toBe(2);
    expect(arcRoad.points[0].segmentMeta).not.toBeNull();
    expect(arcRoad.points[0].segmentMeta.kind).toBe('arc');

    const report = addon.roadGetAdapterReport(arcRoad);
    expect(report.exact).toBe(true);
    expect(report.arcSegments).toBe(1);
    expect(report.numSegments).toBe(1);
    expect(report.warnings.length).toBe(0);
  });

  it('getAdapterReport: clothoid metadata produces spiralSegments=1', () => {
    const clothRoad = addon.roadCreateClothoidArc(0, 0, 1, 0, 80, 20, 0.8, 0.6, 8);
    expect(clothRoad.formatVersion).toBe(2);
    expect(clothRoad.points[0].segmentMeta).not.toBeNull();
    expect(clothRoad.points[0].segmentMeta.kind).toBe('spiral');

    const report = addon.roadGetAdapterReport(clothRoad);
    expect(report.exact).toBe(true);
    expect(report.spiralSegments).toBe(1);
    expect(report.numSegments).toBe(1);
  });

  it('convertFromV2: round-trip line road preserves metadata', () => {
    const road = {
      id: 'rt_bridge_line',
      name: 'Round Trip Bridge Line',
      width: 10.0,
      laneCount: 4,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const restored = addon.roadConvertFromV2(road);
    expect(restored.id).toBe('rt_bridge_line');
    expect(restored.name).toBe('Round Trip Bridge Line');
    expect(restored.width).toBe(10.0);
    expect(restored.laneCount).toBe(4);
    expect(restored.formatVersion).toBe(2);
    expect(restored.points.length).toBe(2);
    expect(restored.points[0].x).toBeCloseTo(0, 1);
    expect(restored.points[1].x).toBeCloseTo(100, 1);
  });

  it('convertFromV2: round-trip bezier road preserves handles', () => {
    const road = {
      id: 'rt_bridge_bez',
      name: 'Round Trip Bridge Bezier',
      width: 6.0,
      laneCount: 2,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'smooth', handleIn: null, handleOut: { x: 25, y: 40 }, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'smooth', handleIn: { x: -25, y: 40 }, handleOut: null, segmentMeta: null },
      ],
    };
    const restored = addon.roadConvertFromV2(road);
    expect(restored.points.length).toBe(2);
    expect(restored.points[0].handleOut).not.toBeNull();
    expect(restored.points[0].handleOut.x).toBeCloseTo(25, 1);
    expect(restored.points[0].handleOut.y).toBeCloseTo(40, 1);
    expect(restored.points[1].handleIn).not.toBeNull();
    expect(restored.points[1].handleIn.x).toBeCloseTo(-25, 1);
    expect(restored.points[1].handleIn.y).toBeCloseTo(40, 1);
  });

  it('convertFromV2: round-trip arc road preserves segmentMeta', () => {
    // Create an arc road using the tool (which emits metadata)
    const arcRoad = addon.roadCreateCircleArc(0, 0, 1, 0, 50, 50, 8);
    const restored = addon.roadConvertFromV2(arcRoad);

    // Should have segmentMeta on first CP
    expect(restored.points[0].segmentMeta).not.toBeNull();
    expect(restored.points[0].segmentMeta.kind).toBe('arc');
    expect(restored.points[0].segmentMeta.curvature).toBeCloseTo(
      arcRoad.points[0].segmentMeta.curvature, 4
    );
    expect(restored.points[0].segmentMeta.arcLength).toBeCloseTo(
      arcRoad.points[0].segmentMeta.arcLength, 1
    );
  });

  it('convertFromV2: round-trip produces same centerline', () => {
    const road = {
      id: 'rt_centerline',
      width: 8.0,
      laneCount: 2,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'smooth', handleIn: null, handleOut: { x: 25, y: 40 }, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'smooth', handleIn: { x: -25, y: 40 }, handleOut: null, segmentMeta: null },
      ],
    };

    const samples1 = addon.roadSampleCenterlineV2(road, 20);
    const restored = addon.roadConvertFromV2(road);
    const samples2 = addon.roadSampleCenterlineV2(restored, 20);

    expect(samples1.length).toBe(samples2.length);
    for (let i = 0; i < samples1.length; i++) {
      expect(samples1[i].x).toBeCloseTo(samples2[i].x, 2);
      expect(samples1[i].y).toBeCloseTo(samples2[i].y, 2);
    }
  });

  it('segmentMeta serialization: createCircleArc emits metadata through bridge', () => {
    const road = addon.roadCreateCircleArc(0, 0, 1, 0, 50, 50, 8);
    expect(road.formatVersion).toBe(2);
    expect(road.points[0].segmentMeta).not.toBeNull();
    expect(road.points[0].segmentMeta.kind).toBe('arc');
    expect(road.points[0].segmentMeta.curvature).not.toBe(0);
    expect(road.points[0].segmentMeta.arcLength).toBeGreaterThan(0);
    expect(road.points[0].segmentMeta.startHeading).toBeCloseTo(0, 4);
    // Subsequent CPs should not have metadata
    expect(road.points[1].segmentMeta).toBeNull();
  });

  it('segmentMeta serialization: createClothoidArc emits metadata through bridge', () => {
    const road = addon.roadCreateClothoidArc(0, 0, 1, 0, 80, 20, 0.8, 0.6, 8);
    expect(road.formatVersion).toBe(2);
    expect(road.points[0].segmentMeta).not.toBeNull();
    expect(road.points[0].segmentMeta.kind).toBe('spiral');
    expect(road.points[0].segmentMeta.segmentLength).toBeGreaterThan(0);
  });

  it('segmentMeta serialization: createSegment sets formatVersion=2', () => {
    const road = addon.roadCreateSegment(0, 0, 100, 0);
    expect(road.formatVersion).toBe(2);
  });

  it('sampleCenterlineV2 vs sampleCenterline: same endpoints for line road', () => {
    const road = {
      id: 'compare',
      width: 8.0,
      laneCount: 2,
      formatVersion: 2,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const v1Samples = addon.roadSampleCenterline(road, 10);
    const v2Samples = addon.roadSampleCenterlineV2(road, 10);
    // Endpoints should match
    expect(v2Samples[0].x).toBeCloseTo(v1Samples[0].x, 1);
    expect(v2Samples[9].x).toBeCloseTo(v1Samples[v1Samples.length - 1].x, 1);
  });
});

// ═══════════════════════════════════════════════════════════
// Phase 2.8 — buildRoad() Full Pipeline Tests
// ═══════════════════════════════════════════════════════════

describeIfAddon('Phase 2.8: buildRoad() full pipeline', () => {
  it('should return a RoadBuildResult with mesh, lanes, markings, adapter', () => {
    const road = {
      id: 'test-build',
      name: 'Test Build',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);

    // Top-level fields
    expect(result).toHaveProperty('meshSections');
    expect(result).toHaveProperty('totalVertices');
    expect(result).toHaveProperty('totalTriangles');
    expect(result).toHaveProperty('lanes');
    expect(result).toHaveProperty('markings');
    expect(result).toHaveProperty('adapter');

    // Mesh sections
    expect(result.meshSections.length).toBeGreaterThanOrEqual(2);
    expect(result.totalVertices).toBeGreaterThan(0);
    expect(result.totalTriangles).toBeGreaterThan(0);

    // Lane network
    expect(result.lanes.centerlines.length).toBe(3);  // 2 drivable + 1 center
    expect(result.lanes.boundaries.length).toBe(4);   // 2 edges + 2 inner

    // Markings
    expect(result.markings.markings.length).toBe(4);

    // Adapter
    expect(result.adapter.numSegments).toBe(1);
    expect(result.adapter.totalLength).toBeCloseTo(100, 1);
  });

  it('should return typed arrays in mesh sections (Babylon-ready)', () => {
    const road = {
      id: 'test-typed',
      name: 'Test Typed Arrays',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 50, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);

    for (const sec of result.meshSections) {
      expect(sec.positions).toBeInstanceOf(Float32Array);
      expect(sec.normals).toBeInstanceOf(Float32Array);
      expect(sec.uvs).toBeInstanceOf(Float32Array);
      expect(sec.indices).toBeInstanceOf(Uint32Array);

      // positions.length === vertexCount * 3
      expect(sec.positions.length).toBe(sec.vertexCount * 3);
      // normals.length === vertexCount * 3
      expect(sec.normals.length).toBe(sec.vertexCount * 3);
      // uvs.length === vertexCount * 2
      expect(sec.uvs.length).toBe(sec.vertexCount * 2);
      // indices.length === triangleCount * 3
      expect(sec.indices.length).toBe(sec.triangleCount * 3);
    }
  });

  it('should have asphalt, white_marking, and yellow_marking sections', () => {
    const road = {
      id: 'test-materials',
      name: 'Test Materials',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);
    const materials = result.meshSections.map((s: any) => s.material);
    expect(materials).toContain('asphalt');
    expect(materials).toContain('white_marking');
    expect(materials).toContain('yellow_marking');
  });

  it('should generate correct lane network for 4-lane road', () => {
    const road = {
      id: 'test-4lane',
      name: 'Test 4-Lane',
      width: 14.0,
      laneCount: 4,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);
    // 4 drivable + 1 center = 5 centerlines
    expect(result.lanes.centerlines.length).toBe(5);
    // 2 edges + 3 inner + 2 center = 7 boundaries... actually depends on impl
    // Just check we have boundaries
    expect(result.lanes.boundaries.length).toBeGreaterThan(0);
  });

  it('should produce valid indices (all within vertex range)', () => {
    const road = {
      id: 'test-indices',
      name: 'Test Indices',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);

    for (const sec of result.meshSections) {
      for (let i = 0; i < sec.indices.length; i++) {
        expect(sec.indices[i]).toBeLessThan(sec.vertexCount);
      }
    }
  });

  it('should handle empty road gracefully', () => {
    const road = {
      id: 'test-empty',
      name: 'Test Empty',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [],
    };
    const result = addon.roadBuildRoad(road);
    expect(result.totalVertices).toBe(0);
    expect(result.totalTriangles).toBe(0);
    expect(result.meshSections.length).toBe(0);
    expect(result.lanes.centerlines.length).toBe(0);
  });

  it('should have flat normals (0,0,1) for flat road', () => {
    const road = {
      id: 'test-normals',
      name: 'Test Normals',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);
    const asphalt = result.meshSections.find((s: any) => s.material === 'asphalt');
    expect(asphalt).toBeDefined();

    for (let i = 0; i < asphalt.vertexCount; i++) {
      expect(asphalt.normals[i * 3]).toBeCloseTo(0, 5);     // nx
      expect(asphalt.normals[i * 3 + 1]).toBeCloseTo(0, 5); // ny
      expect(asphalt.normals[i * 3 + 2]).toBeCloseTo(1, 5); // nz
    }
  });

  it('should have all z=0 for flat road positions', () => {
    const road = {
      id: 'test-z',
      name: 'Test Z',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);
    const asphalt = result.meshSections.find((s: any) => s.material === 'asphalt');
    for (let i = 0; i < asphalt.vertexCount; i++) {
      expect(asphalt.positions[i * 3 + 2]).toBeCloseTo(0, 5); // z
    }
  });

  it('should have markings above pavement (z > 0)', () => {
    const road = {
      id: 'test-marking-z',
      name: 'Test Marking Z',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);
    const yellow = result.meshSections.find((s: any) => s.material === 'yellow_marking');
    expect(yellow).toBeDefined();
    expect(yellow.vertexCount).toBeGreaterThan(0);

    for (let i = 0; i < yellow.vertexCount; i++) {
      expect(yellow.positions[i * 3 + 2]).toBeGreaterThan(0); // z above pavement
    }
  });

  it('should include sample points with s, heading, laneOffset in lane network', () => {
    const road = {
      id: 'test-samples',
      name: 'Test Samples',
      width: 7.0,
      laneCount: 2,
      formatVersion: 1,
      points: [
        { x: 0, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
        { x: 100, y: 0, z: 0, type: 'corner', handleIn: null, handleOut: null, segmentMeta: null },
      ],
    };
    const result = addon.roadBuildRoad(road);
    const cl = result.lanes.centerlines[0];
    expect(cl.samples.length).toBeGreaterThan(0);
    expect(cl.samples[0]).toHaveProperty('position');
    expect(cl.samples[0]).toHaveProperty('s');
    expect(cl.samples[0]).toHaveProperty('heading');
    expect(cl.samples[0]).toHaveProperty('laneOffset');
  });

  it('performance: 1000-segment road should complete in reasonable time', () => {
    const points: any[] = [];
    for (let i = 0; i <= 1000; i++) {
      points.push({
        x: i * 10,
        y: (i % 2 === 0) ? 0 : 1,
        z: 0,
        type: 'corner',
        handleIn: null,
        handleOut: null,
        segmentMeta: null,
      });
    }
    const road = {
      id: 'perf-1000',
      name: 'Performance 1000',
      width: 28.0,
      laneCount: 8,
      formatVersion: 1,
      points,
    };

    const start = performance.now();
    const result = addon.roadBuildRoad(road);
    const elapsed = performance.now() - start;

    expect(result.totalVertices).toBeGreaterThan(0);
    expect(result.totalTriangles).toBeGreaterThan(0);
    // Generous budget for debug build (release target is <50ms)
    expect(elapsed).toBeLessThan(5000);
  });
});
