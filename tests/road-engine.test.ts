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
});
