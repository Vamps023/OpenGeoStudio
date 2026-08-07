/**
 * Golden Fixture Tests (Phase 1.8.1)
 *
 * Captures and verifies golden data from the legacy C++ road engine.
 * The golden data is committed as JSON and serves as the reference
 * baseline for adapter parity testing in Phase 1.8.4.
 *
 * First run: generates golden data files in tests/golden_data/
 * Subsequent runs: verifies live engine output matches committed golden data
 *
 * To regenerate golden data (e.g. after intentional engine changes):
 *   DELETE_GOLDEN=1 npx vitest run tests/golden-fixture.test.ts
 */

import { describe, it, expect } from 'vitest';
import * as path from 'path';
import * as fs from 'fs';
import {
  getFixtureRoads,
  captureGoldenData,
  saveGoldenData,
  loadGoldenData,
  goldenDataExists,
  getGoldenFilePath,
  type GoldenFixture,
  type GoldenSample,
} from './golden_fixtures';

let addon: any = null;
try {
  addon = require(path.join(__dirname, '..', 'app', 'native', 'road_engine', 'build', 'Release', 'road_engine_native.node'));
} catch {
  // Addon not built
}

const describeIfAddon = addon ? describe : describe.skip;
const REGEN = !!process.env.DELETE_GOLDEN;
const NUM_SAMPLES = 32;

// Tolerances for golden data comparison
const POS_TOL = 1e-6;        // Position: exact (same engine, same input)
const HEADING_TOL = 1e-4;    // Heading: finite difference may have tiny FP variation
const CURVATURE_TOL = 1e-4;  // Curvature: 3-point fit may have FP variation
const LENGTH_TOL = 1e-6;     // Total length: cumulative sum

describeIfAddon('Golden Fixtures', () => {
  const fixtureRoads = getFixtureRoads();

  it('should have 7 fixture roads defined', () => {
    expect(fixtureRoads.length).toBe(7);
  });

  it('all fixtures should have valid road structure', () => {
    for (const f of fixtureRoads) {
      expect(f.road.id).toBeTruthy();
      expect(f.road.name).toBeTruthy();
      expect(f.road.width).toBeGreaterThan(0);
      expect(f.road.laneCount).toBeGreaterThan(0);
      expect(f.road.points.length).toBeGreaterThanOrEqual(2);
    }
  });

  // ─── Capture or verify golden data ───

  it('should capture or verify golden data for all fixtures', () => {
    const golden = captureGoldenData(NUM_SAMPLES);
    expect(golden.length).toBe(7);

    if (REGEN || !goldenDataExists()) {
      // First run or regeneration requested — save golden data
      saveGoldenData(golden);
      console.log('[Golden Fixtures] Golden data saved to tests/golden_data/');
      // Verify it was saved
      expect(goldenDataExists()).toBe(true);
      return;
    }

    // Verify golden data exists and matches
    for (const fixture of golden) {
      const saved = loadGoldenData(fixture.name);
      expect(saved).not.toBeNull();
      expect(saved!.name).toBe(fixture.name);
      expect(saved!.numSamples).toBe(fixture.numSamples);
      expect(saved!.samples.length).toBe(fixture.samples.length);
    }
  });

  // ─── Per-fixture detailed comparison ───

  // Generate fixture test cases dynamically
  const goldenData = goldenDataExists() && !REGEN
    ? fixtureRoads.map(f => loadGoldenData(f.name)!)
    : captureGoldenData(NUM_SAMPLES);

  for (let i = 0; i < fixtureRoads.length; i++) {
    const fixture = fixtureRoads[i];
    const golden = goldenData[i];

    if (!golden) continue;

    describe(`${fixture.name}`, () => {
      it('should have correct metadata', () => {
        expect(golden.roadMetadata.id).toBe(fixture.road.id);
        expect(golden.roadMetadata.width).toBe(fixture.road.width);
        expect(golden.roadMetadata.laneCount).toBe(fixture.road.laneCount);
        expect(golden.roadMetadata.numControlPoints).toBe(fixture.road.points.length);
      });

      it('should have samples (count depends on segment distribution)', () => {
        expect(golden.samples.length).toBeGreaterThanOrEqual(2);
        // The legacy engine distributes samples per segment with minimum 2 each.
        // The total may differ from NUM_SAMPLES — that's expected.
      });

      it('first sample should be at s=0', () => {
        expect(golden.samples[0].s).toBeCloseTo(0, 6);
      });

      it('last sample should be at s=totalLength', () => {
        expect(golden.samples[golden.samples.length - 1].s).toBeCloseTo(golden.totalLength, 6);
      });

      it('s values should be monotonically increasing', () => {
        for (let j = 1; j < golden.samples.length; j++) {
          expect(golden.samples[j].s).toBeGreaterThanOrEqual(golden.samples[j - 1].s);
        }
      });

      it('positions should match live engine output', () => {
        const live = captureGoldenData(NUM_SAMPLES);
        const liveFixture = live[i];
        for (let j = 0; j < golden.samples.length; j++) {
          expect(liveFixture.samples[j].x).toBeCloseTo(golden.samples[j].x, 6);
          expect(liveFixture.samples[j].y).toBeCloseTo(golden.samples[j].y, 6);
        }
      });

      it('headings should match live engine output', () => {
        const live = captureGoldenData(NUM_SAMPLES);
        const liveFixture = live[i];
        for (let j = 0; j < golden.samples.length; j++) {
          // Heading may differ by 2π — normalize
          let diff = liveFixture.samples[j].heading - golden.samples[j].heading;
          while (diff > Math.PI) diff -= 2 * Math.PI;
          while (diff < -Math.PI) diff += 2 * Math.PI;
          expect(Math.abs(diff)).toBeLessThan(HEADING_TOL);
        }
      });

      it('curvatures should match live engine output', () => {
        const live = captureGoldenData(NUM_SAMPLES);
        const liveFixture = live[i];
        for (let j = 0; j < golden.samples.length; j++) {
          expect(Math.abs(liveFixture.samples[j].curvature - golden.samples[j].curvature))
            .toBeLessThan(CURVATURE_TOL);
        }
      });

      it('total length should match live engine output', () => {
        const live = captureGoldenData(NUM_SAMPLES);
        const liveFixture = live[i];
        expect(liveFixture.totalLength).toBeCloseTo(golden.totalLength, 6);
      });
    });
  }

  // ─── Fixture-specific sanity checks ───

  describe('fixture sanity checks', () => {
    it('straight_2pt: should be a straight line (curvature ≈ 0)', () => {
      const golden = goldenData[0];
      for (const s of golden.samples) {
        expect(Math.abs(s.curvature)).toBeLessThan(0.001);
      }
    });

    it('straight_2pt: heading should be constant (east)', () => {
      const golden = goldenData[0];
      for (const s of golden.samples) {
        let h = s.heading;
        while (h > Math.PI) h -= 2 * Math.PI;
        while (h < -Math.PI) h += 2 * Math.PI;
        expect(Math.abs(h)).toBeLessThan(0.01);
      }
    });

    it('arc_quarter: should have consistent nonzero curvature', () => {
      const golden = goldenData[2];
      // The arc should have nonzero curvature in the interior samples
      // (sign depends on turn direction — just check magnitude)
      let nonzeroCount = 0;
      for (let j = 5; j < golden.samples.length - 5; j++) {
        if (Math.abs(golden.samples[j].curvature) > 0.0001) nonzeroCount++;
      }
      expect(nonzeroCount).toBeGreaterThan(0);
    });

    it('tiny_segments: total length should be small (< 5m)', () => {
      const golden = goldenData[6];
      expect(golden.totalLength).toBeLessThan(5.0);
    });

    it('tiny_segments: should have 30+ control points', () => {
      const golden = goldenData[6];
      expect(golden.roadMetadata.numControlPoints).toBeGreaterThanOrEqual(30);
    });

    it('bezier_arch: should have nonzero curvature somewhere', () => {
      const golden = goldenData[4];
      const maxCurv = Math.max(...golden.samples.map(s => Math.abs(s.curvature)));
      expect(maxCurv).toBeGreaterThan(0.001);
    });
  });
});
