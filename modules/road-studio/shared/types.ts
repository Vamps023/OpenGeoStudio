/**
 * Road Studio — Type Definitions
 *
 * Roads use geographic coordinates (lat/lon) with elevation in meters.
 * Control points support Bezier handles for smooth curve editing,
 * similar to Figma/Illustrator pen tool.
 *
 * NOTE: All geometry computation (sampling, intersection, arcs, clothoids,
 * mesh generation, OpenDRIVE export) is handled by the C++ native engine
 * via the roadEngineClient. This file contains ONLY type definitions
 * and small utility functions (ID generation, coordinate conversion for
 * UI-level operations).
 */

/** A control point on a road — the fundamental editable unit */
export interface ControlPoint {
  id: string;
  /** Latitude (degrees) */
  lat: number;
  /** Longitude (degrees) */
  lon: number;
  /** Elevation in meters */
  z: number;
  /** Bezier handle for the incoming segment (relative offset from point) */
  handleIn: Vec2 | null;
  /** Bezier handle for the outgoing segment (relative offset from point) */
  handleOut: Vec2 | null;
  /** Smooth points have handles, corner points do not */
  type: 'smooth' | 'corner';
}

/** 2D vector in lat/lon space (used for bezier handle offsets) */
export interface Vec2 {
  lat: number;
  lon: number;
}

/** A road — a sequence of control points forming a path */
export interface Road {
  id: string;
  name: string;
  points: ControlPoint[];
  /** Road width in meters */
  width: number;
  /** Number of lanes */
  laneCount: number;
  /** Visual color */
  color: string;
  /** Road surface profile (SCANeR-style) */
  profile: RoadProfile;
  /** Intersection node at the start of the road (null if none) */
  startIntersectionId: string | null;
  /** Intersection node at the end of the road (null if none) */
  endIntersectionId: string | null;
}

/** An intersection node — where two or more roads meet (SCANeR-style) */
export interface Intersection {
  id: string;
  name: string;
  /** Center position (lat/lon) */
  lat: number;
  lon: number;
  /** Elevation in meters */
  z: number;
  /** Road IDs connected to this intersection, with which end (start/end) */
  connections: IntersectionConnection[];
  /** Turn restrictions (which lane-to-lane paths are forbidden) */
  bannedLinks: BannedLink[];
  /** Whether this intersection was auto-detected or manually created */
  autoDetected: boolean;
}

/** A road's connection to an intersection */
export interface IntersectionConnection {
  roadId: string;
  /** Which end of the road connects: 'start' = first point, 'end' = last point */
  end: 'start' | 'end';
}

/** A forbidden lane-to-lane connection at an intersection */
export interface BannedLink {
  fromRoadId: string;
  fromLane: number;
  fromEnd: 'start' | 'end';
  toRoadId: string;
  toLane: number;
  toEnd: 'start' | 'end';
}

/** SCANeR-style road profile — defines road type, surface, and lane config */
export interface RoadProfile {
  /** Profile type based on SCANeR profiles */
  type: 'city_2x1' | 'city_2x2' | 'country_2x1' | 'highway_2x3' | 'custom';
  /** Surface texture name (from SCANeR textures) */
  surfaceTexture: string;
  /** Lane marking texture name */
  markingTexture: string;
  /** Lane width in meters */
  laneWidth: number;
  /** Has sidewalk? */
  hasSidewalk: boolean;
  /** Has curb? */
  hasCurb: boolean;
}

/** Default road profiles based on SCANeR .rndProfile files */
export const ROAD_PROFILES: Record<string, RoadProfile> = {
  city_2x1: {
    type: 'city_2x1',
    surfaceTexture: 'asphalt',
    markingTexture: 'marking',
    laneWidth: 3.5,
    hasSidewalk: true,
    hasCurb: true,
  },
  city_2x2: {
    type: 'city_2x2',
    surfaceTexture: 'asphalt',
    markingTexture: 'marking',
    laneWidth: 3.5,
    hasSidewalk: true,
    hasCurb: true,
  },
  country_2x1: {
    type: 'country_2x1',
    surfaceTexture: 'asphalt',
    markingTexture: 'marking',
    laneWidth: 3.5,
    hasSidewalk: false,
    hasCurb: false,
  },
  highway_2x3: {
    type: 'highway_2x3',
    surfaceTexture: 'asphalt',
    markingTexture: 'marking',
    laneWidth: 3.75,
    hasSidewalk: false,
    hasCurb: false,
  },
  custom: {
    type: 'custom',
    surfaceTexture: 'asphalt',
    markingTexture: 'marking',
    laneWidth: 3.5,
    hasSidewalk: false,
    hasCurb: false,
  },
};

/** Available editing tools */
/** Available editing tools (SCANeR-style) */
export type Tool =
  | 'select'
  | 'line'        // Segment (straight road)
  | 'pen'         // Bézier (cubic with handles)
  | 'arc'         // Circle Arc (constant radius)
  | 'clothoid'    // Clothoid Arc (Euler spiral transition)
  | 'polyline'    // Polyline (multi-point, optional fillet)
  | 'spline';     // Clothoid Spline (G2-continuous multi-segment)

/** Selection state */
export interface Selection {
  roadId: string | null;
  /** Selected control point indices within the road */
  pointIndices: number[];
  /** Selected handle: 'in' | 'out' | null */
  handle: 'in' | 'out' | null;
}

/** Undo/redo snapshot */
export interface HistorySnapshot {
  roads: Road[];
  description: string;
  timestamp: number;
}

// ═══════════════════════════════════════════════════════════
// UTILITY FUNCTIONS (non-geometric — kept for UI-level use)
// ═══════════════════════════════════════════════════════════

/** Generate a unique ID */
export function generateId(): string {
  return `cp_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
}

/**
 * Convert lat/lon to local meters relative to a reference origin.
 * This is a simple equirectangular projection used for UI-level
 * coordinate conversion. For heavy geometry computation, use the
 * C++ engine via roadEngineClient.
 */
export function geoToLocal(
  lat: number,
  lon: number,
  refLat: number,
  refLon: number
): { x: number; y: number } {
  const x = (lon - refLon) * Math.cos((refLat * Math.PI) / 180) * 111320;
  const y = (lat - refLat) * 110540;
  return { x, y };
}

/**
 * Convert local meters back to lat/lon.
 * Inverse of geoToLocal.
 */
export function localToGeo(
  x: number,
  y: number,
  refLat: number,
  refLon: number
): { lat: number; lon: number } {
  const lat = y / 110540 + refLat;
  const lon = x / (Math.cos((refLat * Math.PI) / 180) * 111320) + refLon;
  return { lat, lon };
}

/** Distance between two lat/lon points in meters (haversine approximation) */
export function distanceMeters(
  lat1: number, lon1: number,
  lat2: number, lon2: number
): number {
  const dLat = (lat2 - lat1) * 110540;
  const dLon = (lon2 - lon1) * 111320 * Math.cos((lat1 * Math.PI) / 180);
  return Math.sqrt(dLat * dLat + dLon * dLon);
}

/**
 * Detect intersections by checking endpoint proximity.
 * This is a lightweight utility (not heavy geometry) — it only checks
 * if road endpoints are close enough to form an intersection.
 * The actual intersection geometry is generated by the C++ engine.
 */
export function detectIntersections(roads: Road[]): Intersection[] {
  const intersections: Intersection[] = [];
  const used: Set<string> = new Set();

  for (let i = 0; i < roads.length; i++) {
    const roadA = roads[i];
    if (roadA.points.length < 1) continue;

    const aStart = roadA.points[0];
    const aEnd = roadA.points[roadA.points.length - 1];

    for (let j = i; j < roads.length; j++) {
      const roadB = roads[j];
      if (roadB.points.length < 1) continue;

      const bStart = roadB.points[0];
      const bEnd = roadB.points[roadB.points.length - 1];

      const pairs: Array<{
        a: { point: typeof aStart; end: 'start' | 'end' };
        b: { point: typeof bStart; end: 'start' | 'end' };
      }> = [
        { a: { point: aStart, end: 'start' }, b: { point: bStart, end: 'start' } },
        { a: { point: aStart, end: 'start' }, b: { point: bEnd, end: 'end' } },
        { a: { point: aEnd, end: 'end' }, b: { point: bStart, end: 'start' } },
        { a: { point: aEnd, end: 'end' }, b: { point: bEnd, end: 'end' } },
      ];

      for (const pair of pairs) {
        if (i === j && pair.a.end === pair.b.end) continue;

        const key = `${roadA.id}:${pair.a.end}↔${roadB.id}:${pair.b.end}`;
        const reverseKey = `${roadB.id}:${pair.b.end}↔${roadA.id}:${pair.a.end}`;
        if (used.has(key) || used.has(reverseKey)) continue;

        const dist = distanceMeters(
          pair.a.point.lat, pair.a.point.lon,
          pair.b.point.lat, pair.b.point.lon
        );

        if (dist <= INTERSECTION_THRESHOLD) {
          used.add(key);
          const lat = (pair.a.point.lat + pair.b.point.lat) / 2;
          const lon = (pair.a.point.lon + pair.b.point.lon) / 2;
          const z = (pair.a.point.z + pair.b.point.z) / 2;

          const existing = intersections.find(
            (ix) => distanceMeters(ix.lat, ix.lon, lat, lon) < INTERSECTION_THRESHOLD * 2
          );

          if (existing) {
            const connA = { roadId: roadA.id, end: pair.a.end };
            const connB = { roadId: roadB.id, end: pair.b.end };
            if (!existing.connections.some(c => c.roadId === connA.roadId && c.end === connA.end)) {
              existing.connections.push(connA);
            }
            if (!existing.connections.some(c => c.roadId === connB.roadId && c.end === connB.end)) {
              existing.connections.push(connB);
            }
          } else {
            intersections.push({
              id: `ix_${generateId()}`,
              name: `Intersection ${intersections.length + 1}`,
              lat, lon, z,
              connections: [
                { roadId: roadA.id, end: pair.a.end },
                { roadId: roadB.id, end: pair.b.end },
              ],
              bannedLinks: [],
              autoDetected: true,
            });
          }
        }
      }
    }
  }

  return intersections;
}

// ═══════════════════════════════════════════════════════════
// GEOMETRY TYPES (used by C++ engine results)
// ═══════════════════════════════════════════════════════════

/** A 2D point in local meters */
export interface Point2D { x: number; y: number; }

/** A line segment in 2D */
export interface Segment2D { p1: Point2D; p2: Point2D; }

/** Threshold distance (meters) for detecting endpoint proximity */
export const INTERSECTION_THRESHOLD = 3.0;

// ─── Generated Intersection Types ──────────────────────────

/** A fully generated intersection with polygon, approaches, and markings */
export interface GeneratedIntersection {
  /** Center point of the intersection (local meters) */
  center: Point2D;
  /** The intersection boundary polygon (local meters) */
  polygon: Point2D[];
  /** The 2-4 road approaches meeting at this intersection */
  approaches: ApproachRoad[];
  /** Lane-to-lane connections through the intersection */
  laneConnections: LaneConnection[];
  /** Stop lines for each approach */
  stopLines: StopLine[];
  /** Crosswalk markings for each approach */
  crosswalks: CrosswalkMarking[];
}

/** A road approaching an intersection */
export interface ApproachRoad {
  /** Which road this approach comes from */
  roadId: string;
  /** Compass direction from intersection center */
  direction: 'north' | 'south' | 'east' | 'west';
  /** The centerline points of this approach (from intersection outward) */
  centerline: Point2D[];
  /** Road width */
  width: number;
  /** Number of lanes */
  laneCount: number;
  /** Elevation at the intersection edge */
  z: number;
}

/** A lane-to-lane connection through the intersection */
export interface LaneConnection {
  fromApproach: string;  // direction
  toApproach: string;    // direction
  type: 'straight' | 'left' | 'right';
  /** Centerline of the turning path (spline) */
  path: Point2D[];
}

/** A stop line marking */
export interface StopLine {
  approach: string;
  /** Two endpoints of the stop line (perpendicular to road) */
  p1: Point2D;
  p2: Point2D;
}

/** A crosswalk marking */
export interface CrosswalkMarking {
  approach: string;
  /** Four corners of the crosswalk rectangle */
  corners: Point2D[];
}

// ─── Circle Arc Types ──────────────────────────────────────

/** A circular arc result from the arc tool */
export interface CircleArc {
  center: Point2D;
  radius: number;
  startAngle: number;
  endAngle: number;
  sweep: number; // radians, positive = CCW (left), negative = CW (right)
  points: Point2D[];
  tangentIn: Point2D;  // direction at arc start
  tangentOut: Point2D; // direction at arc end
}
