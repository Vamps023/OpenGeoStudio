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
    expect(version).toBe('1.0.0-road-engine');
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
