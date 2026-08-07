/**
 * Golden Fixture Roads for Adapter Parity Testing (Phase 1.8.1)
 *
 * Seven fixture roads that exercise all legacy geometry paths.
 * Each fixture is a road in local coordinates (x, y meters).
 * The golden data (sampled centerline with derived heading/curvature)
 * is captured from the legacy engine and committed as JSON.
 *
 * Fixtures:
 * 1. straight_2pt      — 2 corner points, pure line
 * 2. straight_5pt      — 5 corner points, polyline
 * 3. arc_quarter       — Arc tool output (quarter circle, sampled to CPs)
 * 4. s_clothoid        — Clothoid S-curve (sampled to CPs)
 * 5. bezier_arch       — 4 points with bezier handles (arch shape)
 * 6. mixed_line_bezier — Mix of corner and smooth points
 * 7. tiny_segments     — 30 short segments (0.05-0.2m), alternating headings
 */

import * as path from 'path';
import * as fs from 'fs';

let addon: any = null;
try {
  addon = require(path.join(__dirname, '..', 'app', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'));
} catch {
  // Addon not built
}

// ─── Fixture Road Definitions ──────────────────────────────

export interface FixtureRoad {
  name: string;
  description: string;
  road: any; // Road object in local coordinates
}

function makeCorner(x: number, y: number, z = 0): any {
  return { x, y, z, type: 'corner', handleIn: null, handleOut: null };
}

function makeSmooth(x: number, y: number, handleIn: { x: number; y: number } | null, handleOut: { x: number; y: number } | null, z = 0): any {
  return { x, y, z, type: 'smooth', handleIn, handleOut };
}

/** Generate arc control points using the legacy arc tool */
function makeArcFixture(): any {
  const arc = addon.roadComputeCircleArc(
    { x: 0, y: 0 },      // start point
    { x: 1, y: 0 },      // start direction (east)
    { x: 50, y: 50 },    // end point
    16                    // 16 segments → 17 points
  );
  const points = arc.points.map((p: any) => makeCorner(p.x, p.y));
  return {
    id: 'arc_quarter',
    name: 'Arc Quarter Circle',
    width: 8.0,
    laneCount: 2,
    points,
  };
}

/** Generate clothoid control points using the legacy clothoid tool */
function makeClothoidFixture(): any {
  const result = addon.roadComputeClothoid(
    { x: 0, y: 0 },        // start point
    { x: 1, y: 0 },        // start direction (east)
    { x: 80, y: 20 },      // end point
    { x: 0.8, y: 0.6 },    // end direction
    50,                     // initialA
    16                      // segments → 17 points
  );
  const points = result.points.map((p: any) => makeCorner(p.x, p.y));
  return {
    id: 's_clothoid',
    name: 'S-Clothoid Curve',
    width: 7.0,
    laneCount: 2,
    points,
  };
}

/** Generate tiny segments with alternating headings */
function makeTinySegmentsFixture(): any {
  const points: any[] = [];
  let x = 0, y = 0;
  let heading = 0;
  points.push(makeCorner(x, y));
  for (let i = 0; i < 30; i++) {
    // Alternate heading by ±15° each segment
    heading += (i % 2 === 0 ? 1 : -1) * 15 * Math.PI / 180;
    // Length between 0.05 and 0.2m
    const len = 0.05 + (i % 3) * 0.075;
    x += len * Math.cos(heading);
    y += len * Math.sin(heading);
    points.push(makeCorner(x, y));
  }
  return {
    id: 'tiny_segments',
    name: 'Tiny Segments (30 × 0.05-0.2m)',
    width: 4.0,
    laneCount: 1,
    points,
  };
}

export function getFixtureRoads(): FixtureRoad[] {
  if (!addon) return [];

  return [
    {
      name: 'straight_2pt',
      description: 'Two corner points, pure line',
      road: {
        id: 'straight_2pt',
        name: 'Straight 2pt',
        width: 8.0,
        laneCount: 2,
        points: [
          makeCorner(0, 0),
          makeCorner(100, 0),
        ],
      },
    },
    {
      name: 'straight_5pt',
      description: 'Five corner points, polyline',
      road: {
        id: 'straight_5pt',
        name: 'Straight 5pt',
        width: 8.0,
        laneCount: 2,
        points: [
          makeCorner(0, 0),
          makeCorner(30, 10),
          makeCorner(50, 5),
          makeCorner(70, -5),
          makeCorner(100, 0),
        ],
      },
    },
    {
      name: 'arc_quarter',
      description: 'Arc tool output (quarter circle, sampled to CPs)',
      road: makeArcFixture(),
    },
    {
      name: 's_clothoid',
      description: 'Clothoid S-curve (sampled to CPs)',
      road: makeClothoidFixture(),
    },
    {
      name: 'bezier_arch',
      description: 'Four points with bezier handles (arch shape)',
      road: {
        id: 'bezier_arch',
        name: 'Bezier Arch',
        width: 6.0,
        laneCount: 2,
        points: [
          makeSmooth(0, 0, null, { x: 15, y: 25 }),
          makeSmooth(50, 30, { x: -15, y: -5 }, { x: 15, y: -5 }),
          makeSmooth(100, 30, { x: -15, y: -5 }, { x: 15, y: -25 }),
          makeSmooth(150, 0, { x: -15, y: -25 }, null),
        ],
      },
    },
    {
      name: 'mixed_line_bezier',
      description: 'Mix of corner and smooth points',
      road: {
        id: 'mixed_line_bezier',
        name: 'Mixed Line+Bezier',
        width: 7.0,
        laneCount: 2,
        points: [
          makeCorner(0, 0),                            // line start
          makeCorner(40, 0),                           // line end / bezier start
          makeSmooth(60, 15, { x: -10, y: 0 }, { x: 10, y: 0 }),  // bezier mid
          makeCorner(80, 0),                           // bezier end / line start
          makeCorner(120, 0),                          // line end
        ],
      },
    },
    {
      name: 'tiny_segments',
      description: '30 short segments (0.05-0.2m), alternating headings',
      road: makeTinySegmentsFixture(),
    },
  ];
}

// ─── Golden Data Computation ───────────────────────────────

export interface GoldenSample {
  s: number;          // cumulative arc length (m)
  x: number;          // position x (m)
  y: number;          // position y (m)
  heading: number;    // heading (rad), computed via finite difference
  curvature: number;  // signed curvature (1/m), computed via 3-point fit
}

export interface GoldenFixture {
  name: string;
  description: string;
  numSamples: number;
  roadMetadata: {
    id: string;
    name: string;
    width: number;
    laneCount: number;
    numControlPoints: number;
  };
  samples: GoldenSample[];
  totalLength: number;
}

/**
 * Compute derived geometry (s, heading, curvature) from position samples.
 * The legacy engine only returns {x, y}[] — heading and curvature are
 * computed using finite differences.
 */
function computeDerivedGeometry(positions: { x: number; y: number }[]): GoldenSample[] {
  const n = positions.length;
  if (n < 2) return [];

  const samples: GoldenSample[] = [];

  // Compute cumulative arc length (s)
  const sValues: number[] = [0];
  for (let i = 1; i < n; i++) {
    const dx = positions[i].x - positions[i - 1].x;
    const dy = positions[i].y - positions[i - 1].y;
    sValues.push(sValues[i - 1] + Math.hypot(dx, dy));
  }

  for (let i = 0; i < n; i++) {
    let heading: number;
    let curvature: number;

    if (n === 2) {
      // Only two points — straight line
      heading = Math.atan2(positions[1].y - positions[0].y, positions[1].x - positions[0].x);
      curvature = 0;
    } else if (i === 0) {
      // Forward difference for heading
      heading = Math.atan2(positions[1].y - positions[0].y, positions[1].x - positions[0].x);
      // 3-point curvature using points 0, 1, 2
      curvature = computeCurvature3Pt(positions[0], positions[1], positions[2]);
    } else if (i === n - 1) {
      // Backward difference for heading
      heading = Math.atan2(positions[n - 1].y - positions[n - 2].y, positions[n - 1].x - positions[n - 2].x);
      // 3-point curvature using points n-3, n-2, n-1
      curvature = computeCurvature3Pt(positions[n - 3], positions[n - 2], positions[n - 1]);
    } else {
      // Central difference for heading
      heading = Math.atan2(positions[i + 1].y - positions[i - 1].y, positions[i + 1].x - positions[i - 1].x);
      // 3-point curvature using points i-1, i, i+1
      curvature = computeCurvature3Pt(positions[i - 1], positions[i], positions[i + 1]);
    }

    samples.push({
      s: sValues[i],
      x: positions[i].x,
      y: positions[i].y,
      heading,
      curvature,
    });
  }

  return samples;
}

/**
 * Signed curvature from 3 points using the circumcircle formula.
 * Positive = left turn (CCW), matching our convention.
 * Returns 0 for collinear points.
 */
function computeCurvature3Pt(p0: { x: number; y: number }, p1: { x: number; y: number }, p2: { x: number; y: number }): number {
  const ax = p1.x - p0.x, ay = p1.y - p0.y;
  const bx = p2.x - p1.x, by = p2.y - p1.y;
  const cx = p2.x - p0.x, cy = p2.y - p0.y;

  // Cross product (z-component): determines turn direction
  const cross = ax * by - ay * bx;
  if (Math.abs(cross) < 1e-12) return 0; // collinear

  // Side lengths
  const a = Math.hypot(bx, by); // |p2-p1|
  const b = Math.hypot(cx, cy); // |p2-p0|
  const c = Math.hypot(ax, ay); // |p1-p0|

  // Curvature = 2 * cross / (|a| * |b| * |c|)
  // This is the signed curvature of the circumcircle through the 3 points
  const denom = a * b * c;
  if (denom < 1e-12) return 0;

  return 2 * cross / denom;
}

/**
 * Capture golden data from the legacy engine for all fixture roads.
 */
export function captureGoldenData(numSamples = 32): GoldenFixture[] {
  if (!addon) return [];

  const fixtures = getFixtureRoads();
  const golden: GoldenFixture[] = [];

  for (const fixture of fixtures) {
    const positions = addon.roadSampleCenterline(fixture.road, numSamples);
    const samples = computeDerivedGeometry(positions);
    const totalLength = samples.length > 0 ? samples[samples.length - 1].s : 0;

    golden.push({
      name: fixture.name,
      description: fixture.description,
      numSamples,
      roadMetadata: {
        id: fixture.road.id,
        name: fixture.road.name,
        width: fixture.road.width,
        laneCount: fixture.road.laneCount,
        numControlPoints: fixture.road.points.length,
      },
      samples,
      totalLength,
    });
  }

  return golden;
}

// ─── Golden Data File I/O ──────────────────────────────────

const GOLDEN_DIR = path.join(__dirname, 'golden_data');

export function getGoldenFilePath(fixtureName: string): string {
  return path.join(GOLDEN_DIR, `${fixtureName}.json`);
}

export function saveGoldenData(fixtures: GoldenFixture[]): void {
  if (!fs.existsSync(GOLDEN_DIR)) {
    fs.mkdirSync(GOLDEN_DIR, { recursive: true });
  }
  for (const fixture of fixtures) {
    const filePath = getGoldenFilePath(fixture.name);
    fs.writeFileSync(filePath, JSON.stringify(fixture, null, 2), 'utf-8');
  }
}

export function loadGoldenData(fixtureName: string): GoldenFixture | null {
  const filePath = getGoldenFilePath(fixtureName);
  if (!fs.existsSync(filePath)) return null;
  const content = fs.readFileSync(filePath, 'utf-8');
  return JSON.parse(content) as GoldenFixture;
}

export function goldenDataExists(): boolean {
  const fixtures = getFixtureRoads();
  return fixtures.every(f => fs.existsSync(getGoldenFilePath(f.name)));
}
