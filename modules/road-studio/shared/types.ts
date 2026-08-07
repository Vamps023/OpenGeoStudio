/**
 * Road Studio — Type Definitions
 *
 * Roads use geographic coordinates (lat/lon) with elevation in meters.
 * Control points support Bezier handles for smooth curve editing,
 * similar to Figma/Illustrator pen tool.
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
export type Tool = 'select' | 'line' | 'pen' | 'arc';

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

/** Convert lat/lon to local meters relative to a reference origin */
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

/** Convert local meters back to lat/lon */
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

/** Generate a unique ID */
export function generateId(): string {
  return `cp_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
}

/** Cubic Bezier interpolation between two control points */
export function bezierPoint(
  p0: ControlPoint,
  p1: ControlPoint,
  t: number,
  refLat: number,
  refLon: number
): { x: number; y: number; z: number } {
  const p0Local = geoToLocal(p0.lat, p0.lon, refLat, refLon);
  const p1Local = geoToLocal(p1.lat, p1.lon, refLat, refLon);

  // Handle offsets are in lat/lon — convert to local meters
  const h0Out = p0.handleOut
    ? geoToLocal(p0.lat + p0.handleOut.lat, p0.lon + p0.handleOut.lon, refLat, refLon)
    : { x: p0Local.x + (p1Local.x - p0Local.x) / 3, y: p0Local.y + (p1Local.y - p0Local.y) / 3 };

  const h1In = p1.handleIn
    ? geoToLocal(p1.lat + p1.handleIn.lat, p1.lon + p1.handleIn.lon, refLat, refLon)
    : { x: p1Local.x - (p1Local.x - p0Local.x) / 3, y: p1Local.y - (p1Local.y - p0Local.y) / 3 };

  const mt = 1 - t;
  const x =
    mt * mt * mt * p0Local.x +
    3 * mt * mt * t * h0Out.x +
    3 * mt * t * t * h1In.x +
    t * t * t * p1Local.x;
  const y =
    mt * mt * mt * p0Local.y +
    3 * mt * mt * t * h0Out.y +
    3 * mt * t * t * h1In.y +
    t * t * t * p1Local.y;

  // Linear interpolation for elevation (can be improved with bezier if handles have z)
  const z = p0.z + (p1.z - p0.z) * t;

  return { x, y, z };
}

/** Sample a road into a series of points for mesh generation */
export function sampleRoad(
  road: Road,
  refLat: number,
  refLon: number,
  segmentsPerSpan: number = 16
): Array<{ x: number; y: number; z: number }> {
  const samples: Array<{ x: number; y: number; z: number }> = [];
  if (road.points.length < 2) return samples;

  for (let i = 0; i < road.points.length - 1; i++) {
    const p0 = road.points[i];
    const p1 = road.points[i + 1];
    for (let s = 0; s < segmentsPerSpan; s++) {
      const t = s / segmentsPerSpan;
      samples.push(bezierPoint(p0, p1, t, refLat, refLon));
    }
  }
  // Add final point
  const last = road.points[road.points.length - 1];
  const lastLocal = geoToLocal(last.lat, last.lon, refLat, refLon);
  samples.push({ x: lastLocal.x, y: lastLocal.y, z: last.z });

  return samples;
}

// ═══════════════════════════════════════════════════════════
// INTERSECTION DETECTION
// ═══════════════════════════════════════════════════════════

/** Threshold distance (meters) for detecting endpoint proximity */
export const INTERSECTION_THRESHOLD = 3.0;

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
 * Detect intersections by checking:
 * 1. Endpoint-to-endpoint proximity (two road endpoints within threshold)
 * 2. Endpoint-on-road (a road endpoint near another road's centerline)
 *
 * Returns the list of detected intersections with their connections.
 */
export function detectIntersections(roads: Road[]): Intersection[] {
  const intersections: Intersection[] = [];
  const used: Set<string> = new Set(); // track endpoint pairs already matched

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

      // Check all 4 endpoint combinations (skip same road same endpoint)
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
        // Skip same road, same endpoint
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
          // Use midpoint as intersection center
          const lat = (pair.a.point.lat + pair.b.point.lat) / 2;
          const lon = (pair.a.point.lon + pair.b.point.lon) / 2;
          const z = (pair.a.point.z + pair.b.point.z) / 2;

          // Check if this intersection is near an existing one (merge)
          const existing = intersections.find(
            (ix) => distanceMeters(ix.lat, ix.lon, lat, lon) < INTERSECTION_THRESHOLD * 2
          );

          if (existing) {
            // Add connections if not already present
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

/**
 * Compute the intersection polygon — the area where road surfaces overlap.
 * This is a convex hull of all road edge points near the intersection center.
 */
export function computeIntersectionPolygon(
  intersection: Intersection,
  roads: Road[],
  refLat: number,
  refLon: number
): Array<{ x: number; y: number }> {
  const points: Array<{ x: number; y: number }> = [];
  const ixLocal = geoToLocal(intersection.lat, intersection.lon, refLat, refLon);

  for (const conn of intersection.connections) {
    const road = roads.find((r) => r.id === conn.roadId);
    if (!road || road.points.length < 2) continue;

    const samples = sampleRoad(road, refLat, refLon, 16);
    const halfW = road.width / 2;

    // Find the sample index closest to the intersection
    let closestIdx = 0;
    let closestDist = Infinity;
    for (let i = 0; i < samples.length; i++) {
      const d = Math.sqrt(
        (samples[i].x - ixLocal.x) ** 2 + (samples[i].y - ixLocal.y) ** 2
      );
      if (d < closestDist) { closestDist = d; closestIdx = i; }
    }

    // Take a few samples around the closest point to get edge points
    const range = 3;
    for (let i = Math.max(0, closestIdx - range); i <= Math.min(samples.length - 1, closestIdx + range); i++) {
      const s = samples[i];
      // Compute normal at this sample
      let tx: number, ty: number;
      if (i === 0) { tx = samples[1].x - s.x; ty = samples[1].y - s.y; }
      else if (i === samples.length - 1) { tx = s.x - samples[i - 1].x; ty = s.y - samples[i - 1].y; }
      else { tx = samples[i + 1].x - samples[i - 1].x; ty = samples[i + 1].y - samples[i - 1].y; }
      const len = Math.sqrt(tx * tx + ty * ty) || 1;
      const nx = -ty / len;
      const ny = tx / len;

      // Left and right edge points
      points.push({ x: s.x + nx * halfW, y: s.y + ny * halfW });
      points.push({ x: s.x - nx * halfW, y: s.y - ny * halfW });
    }
  }

  // Compute convex hull of all edge points
  return convexHull(points);
}

/** Compute the convex hull of a set of 2D points (Andrew's monotone chain) */
export function convexHull(points: Array<{ x: number; y: number }>): Array<{ x: number; y: number }> {
  if (points.length < 3) return points;

  // Sort points by x, then y
  const sorted = [...points].sort((a, b) => a.x - b.x || a.y - b.y);

  const cross = (o: { x: number; y: number }, a: { x: number; y: number }, b: { x: number; y: number }) =>
    (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);

  // Build lower hull
  const lower: typeof sorted = [];
  for (const p of sorted) {
    while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) {
      lower.pop();
    }
    lower.push(p);
  }

  // Build upper hull
  const upper: typeof sorted = [];
  for (let i = sorted.length - 1; i >= 0; i--) {
    const p = sorted[i];
    while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) {
      upper.pop();
    }
    upper.push(p);
  }

  // Concatenate (omit last point of each half — it's the first of the other)
  return lower.slice(0, -1).concat(upper.slice(0, -1));
}

// ═══════════════════════════════════════════════════════════
// INTERSECTION GENERATION ALGORITHM
// ═══════════════════════════════════════════════════════════

/** A 2D point in local meters */
export interface Point2D { x: number; y: number; }

/** A line segment in 2D */
export interface Segment2D { p1: Point2D; p2: Point2D; }

/**
 * Find where two line segments intersect.
 * Returns the intersection point, or null if they don't cross.
 */
export function segmentIntersection(
  s1: Segment2D, s2: Segment2D
): Point2D | null {
  const x1 = s1.p1.x, y1 = s1.p1.y;
  const x2 = s1.p2.x, y2 = s1.p2.y;
  const x3 = s2.p1.x, y3 = s2.p1.y;
  const x4 = s2.p2.x, y4 = s2.p2.y;

  const denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
  if (Math.abs(denom) < 1e-10) return null; // parallel

  const t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
  const u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / denom;

  if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
    return {
      x: x1 + t * (x2 - x1),
      y: y1 + t * (y2 - y1),
    };
  }
  return null;
}

/**
 * Find where two road centerlines intersect.
 * Samples both roads and checks all segment pairs.
 * Returns the intersection point in local meters, or null.
 */
export function findCenterlineIntersection(
  road1: Road,
  road2: Road,
  refLat: number,
  refLon: number
): Point2D | null {
  const s1 = sampleRoad(road1, refLat, refLon, 24);
  const s2 = sampleRoad(road2, refLat, refLon, 24);
  if (s1.length < 2 || s2.length < 2) return null;

  for (let i = 0; i < s1.length - 1; i++) {
    for (let j = 0; j < s2.length - 1; j++) {
      const result = segmentIntersection(
        { p1: { x: s1[i].x, y: s1[i].y }, p2: { x: s1[i + 1].x, y: s1[i + 1].y } },
        { p1: { x: s2[j].x, y: s2[j].y }, p2: { x: s2[j + 1].x, y: s2[j + 1].y } }
      );
      if (result) return result;
    }
  }
  return null;
}

/**
 * Find the closest point on a road's centerline to a given point.
 * Returns the sample index and the point itself.
 */
export function closestPointOnRoad(
  road: Road,
  target: Point2D,
  refLat: number,
  refLon: number
): { index: number; point: Point2D; distance: number } | null {
  const samples = sampleRoad(road, refLat, refLon, 24);
  if (samples.length === 0) return null;

  let bestIdx = 0;
  let bestDist = Infinity;
  for (let i = 0; i < samples.length; i++) {
    const d = Math.sqrt((samples[i].x - target.x) ** 2 + (samples[i].y - target.y) ** 2);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }
  return { index: bestIdx, point: { x: samples[bestIdx].x, y: samples[bestIdx].y }, distance: bestDist };
}

/**
 * Compute the tangent direction at a sample point on a road.
 * Returns a normalized direction vector.
 */
export function tangentAtSample(
  samples: Array<{ x: number; y: number; z: number }>,
  index: number
): Point2D {
  let tx: number, ty: number;
  if (index === 0) {
    tx = samples[1].x - samples[0].x;
    ty = samples[1].y - samples[0].y;
  } else if (index === samples.length - 1) {
    tx = samples[index].x - samples[index - 1].x;
    ty = samples[index].y - samples[index - 1].y;
  } else {
    tx = samples[index + 1].x - samples[index - 1].x;
    ty = samples[index + 1].y - samples[index - 1].y;
  }
  const len = Math.sqrt(tx * tx + ty * ty) || 1;
  return { x: tx / len, y: ty / len };
}

/**
 * Compute the normal (perpendicular) at a sample point.
 * Normal = rotate tangent 90° counter-clockwise.
 */
export function normalAtSample(
  samples: Array<{ x: number; y: number; z: number }>,
  index: number
): Point2D {
  const t = tangentAtSample(samples, index);
  return { x: -t.y, y: t.x };
}

/**
 * Generate a fillet (rounded corner) arc between two line segments.
 * Returns a list of points along the arc.
 */
export function filletArc(
  corner: Point2D,
  dirIn: Point2D,   // direction approaching the corner
  dirOut: Point2D,  // direction leaving the corner
  radius: number,
  segments: number = 8
): Point2D[] {
  // The fillet arc connects two points that are `radius` distance from the corner
  // along each direction. We need to find the arc center and sweep angle.

  // Points where the arc starts and ends
  const p1 = { x: corner.x - dirIn.x * radius, y: corner.y - dirIn.y * radius };
  const p2 = { x: corner.x + dirOut.x * radius, y: corner.y + dirOut.y * radius };

  // The arc center is at distance `radius` from both p1 and p2, perpendicular to the directions
  // For a simple approach, we'll interpolate using quadratic bezier through the corner
  const points: Point2D[] = [];
  for (let i = 0; i <= segments; i++) {
    const t = i / segments;
    // Quadratic bezier: B(t) = (1-t)²·P1 + 2(1-t)t·Corner + t²·P2
    const mt = 1 - t;
    points.push({
      x: mt * mt * p1.x + 2 * mt * t * corner.x + t * t * p2.x,
      y: mt * mt * p1.y + 2 * mt * t * corner.y + t * t * p2.y,
    });
  }
  return points;
}

/**
 * Result of generating an intersection between two roads.
 */
export interface GeneratedIntersection {
  /** Center point of the intersection (local meters) */
  center: Point2D;
  /** The polygon outline of the intersection surface (local meters) */
  polygon: Point2D[];
  /** The 4 approach roads (trimmed), or 2 if roads don't cross but just meet */
  approaches: ApproachRoad[];
  /** Lane connectivity graph */
  laneConnections: LaneConnection[];
  /** Stop line positions (one per approach) */
  stopLines: StopLine[];
  /** Crosswalk positions (one per approach) */
  crosswalks: CrosswalkMarking[];
}

/** An approach road — one of the 4 segments after splitting */
export interface ApproachRoad {
  /** Which road this came from */
  roadId: string;
  /** Which side of the intersection: N, S, E, W (or computed) */
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

/**
 * Determine compass direction from intersection center to a point.
 */
function compassDirection(from: Point2D, to: Point2D): 'north' | 'south' | 'east' | 'west' {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  if (Math.abs(dx) > Math.abs(dy)) {
    return dx > 0 ? 'east' : 'west';
  } else {
    return dy > 0 ? 'north' : 'south';
  }
}

/**
 * Generate a complete intersection between two roads.
 *
 * Algorithm:
 * 1. Find where centerlines cross
 * 2. Split each road at the crossing point → 4 approaches
 * 3. Trim road ends to leave space for the junction
 * 4. Generate intersection polygon with rounded corners
 * 5. Generate lane connectivity (straight, left, right)
 * 6. Generate stop lines and crosswalks
 */
export function generateIntersection(
  road1: Road,
  road2: Road,
  refLat: number,
  refLon: number
): GeneratedIntersection | null {
  // Step 1: Find geometric intersection of centerlines
  const center = findCenterlineIntersection(road1, road2, refLat, refLon);
  if (!center) {
    // Fallback: use closest point between the two roads
    const s1 = sampleRoad(road1, refLat, refLon, 24);
    const s2 = sampleRoad(road2, refLat, refLon, 24);
    if (s1.length < 2 || s2.length < 2) return null;

    let minDist = Infinity;
    let bestP: Point2D = { x: 0, y: 0 };
    for (const a of s1) {
      for (const b of s2) {
        const d = Math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2);
        if (d < minDist) { minDist = d; bestP = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }; }
      }
    }
    if (minDist > 50) return null; // too far apart
    return generateIntersectionFromPoint(road1, road2, bestP, refLat, refLon);
  }

  return generateIntersectionFromPoint(road1, road2, center, refLat, refLon);
}

/**
 * Generate intersection from a known center point.
 */
function generateIntersectionFromPoint(
  road1: Road,
  road2: Road,
  center: Point2D,
  refLat: number,
  refLon: number
): GeneratedIntersection | null {
  const s1 = sampleRoad(road1, refLat, refLon, 24);
  const s2 = sampleRoad(road2, refLat, refLon, 24);
  if (s1.length < 2 || s2.length < 2) return null;

  // Step 2: Find closest sample on each road to the intersection center
  let idx1 = 0, idx2 = 0;
  let minDist1 = Infinity, minDist2 = Infinity;
  for (let i = 0; i < s1.length; i++) {
    const d = Math.sqrt((s1[i].x - center.x) ** 2 + (s1[i].y - center.y) ** 2);
    if (d < minDist1) { minDist1 = d; idx1 = i; }
  }
  for (let i = 0; i < s2.length; i++) {
    const d = Math.sqrt((s2[i].x - center.x) ** 2 + (s2[i].y - center.y) ** 2);
    if (d < minDist2) { minDist2 = d; idx2 = i; }
  }

  // Step 3: Trim distance = max road width / 2 + margin
  const maxHalfWidth = Math.max(road1.width, road2.width) / 2;
  const trimDist = maxHalfWidth + Math.min(maxHalfWidth, 5) + 3; // half-width + corner radius + margin

  // Step 4: Build 4 approaches (each road split into 2 halves at the intersection)
  const approaches: ApproachRoad[] = [];

  // Road 1 - approach A (from start to intersection)
  const r1StartApproach: Point2D[] = [];
  for (let i = 0; i <= idx1; i++) {
    const d = Math.sqrt((s1[i].x - center.x) ** 2 + (s1[i].y - center.y) ** 2);
    if (d > trimDist) r1StartApproach.push({ x: s1[i].x, y: s1[i].y });
  }
  // Add trimmed endpoint (at trimDist from center)
  if (r1StartApproach.length > 0) {
    const last = r1StartApproach[r1StartApproach.length - 1];
    const dx = center.x - last.x;
    const dy = center.y - last.y;
    const d = Math.sqrt(dx * dx + dy * dy);
    if (d > 0) {
      r1StartApproach.push({
        x: last.x + (dx / d) * (d - trimDist),
        y: last.y + (dy / d) * (d - trimDist),
      });
    }
  }

  // Road 1 - approach B (from intersection to end)
  const r1EndApproach: Point2D[] = [];
  for (let i = idx1; i < s1.length; i++) {
    const d = Math.sqrt((s1[i].x - center.x) ** 2 + (s1[i].y - center.y) ** 2);
    if (d > trimDist) r1EndApproach.push({ x: s1[i].x, y: s1[i].y });
  }
  if (r1EndApproach.length > 0) {
    const first = r1EndApproach[0];
    const dx = first.x - center.x;
    const dy = first.y - center.y;
    const d = Math.sqrt(dx * dx + dy * dy);
    if (d > 0) {
      r1EndApproach.unshift({
        x: center.x + (dx / d) * trimDist,
        y: center.y + (dy / d) * trimDist,
      });
    }
  }

  // Road 2 - approach C (from start to intersection)
  const r2StartApproach: Point2D[] = [];
  for (let i = 0; i <= idx2; i++) {
    const d = Math.sqrt((s2[i].x - center.x) ** 2 + (s2[i].y - center.y) ** 2);
    if (d > trimDist) r2StartApproach.push({ x: s2[i].x, y: s2[i].y });
  }
  if (r2StartApproach.length > 0) {
    const last = r2StartApproach[r2StartApproach.length - 1];
    const dx = center.x - last.x;
    const dy = center.y - last.y;
    const d = Math.sqrt(dx * dx + dy * dy);
    if (d > 0) {
      r2StartApproach.push({
        x: last.x + (dx / d) * (d - trimDist),
        y: last.y + (dy / d) * (d - trimDist),
      });
    }
  }

  // Road 2 - approach D (from intersection to end)
  const r2EndApproach: Point2D[] = [];
  for (let i = idx2; i < s2.length; i++) {
    const d = Math.sqrt((s2[i].x - center.x) ** 2 + (s2[i].y - center.y) ** 2);
    if (d > trimDist) r2EndApproach.push({ x: s2[i].x, y: s2[i].y });
  }
  if (r2EndApproach.length > 0) {
    const first = r2EndApproach[0];
    const dx = first.x - center.x;
    const dy = first.y - center.y;
    const d = Math.sqrt(dx * dx + dy * dy);
    if (d > 0) {
      r2EndApproach.unshift({
        x: center.x + (dx / d) * trimDist,
        y: center.y + (dy / d) * trimDist,
      });
    }
  }

  // Assign directions and build approach objects
  const z1 = s1[idx1].z;
  const z2 = s2[idx2].z;
  const zAvg = (z1 + z2) / 2;

  if (r1StartApproach.length >= 2) {
    const dir = compassDirection(center, r1StartApproach[r1StartApproach.length - 1]);
    approaches.push({
      roadId: road1.id, direction: dir,
      centerline: r1StartApproach.reverse(), // from intersection outward
      width: road1.width, laneCount: road1.laneCount, z: zAvg,
    });
  }
  if (r1EndApproach.length >= 2) {
    const dir = compassDirection(center, r1EndApproach[0]);
    approaches.push({
      roadId: road1.id, direction: dir,
      centerline: r1EndApproach,
      width: road1.width, laneCount: road1.laneCount, z: zAvg,
    });
  }
  if (r2StartApproach.length >= 2) {
    const dir = compassDirection(center, r2StartApproach[r2StartApproach.length - 1]);
    approaches.push({
      roadId: road2.id, direction: dir,
      centerline: r2StartApproach.reverse(),
      width: road2.width, laneCount: road2.laneCount, z: zAvg,
    });
  }
  if (r2EndApproach.length >= 2) {
    const dir = compassDirection(center, r2EndApproach[0]);
    approaches.push({
      roadId: road2.id, direction: dir,
      centerline: r2EndApproach,
      width: road2.width, laneCount: road2.laneCount, z: zAvg,
    });
  }

  if (approaches.length < 2) return null;

  // Step 5: Generate intersection polygon with rounded corners
  const polygon = generateIntersectionPolygon(approaches, center, trimDist);

  // Step 6: Generate lane connectivity
  const laneConnections = generateLaneConnections(approaches, center);

  // Step 7: Generate stop lines and crosswalks
  const stopLines: StopLine[] = [];
  const crosswalks: CrosswalkMarking[] = [];

  for (const approach of approaches) {
    if (approach.centerline.length < 2) continue;

    // Stop line: perpendicular to road, at trimDist from center
    const edgePoint = approach.centerline[0]; // first point (at intersection edge)
    const tangent = {
      x: approach.centerline[1].x - approach.centerline[0].x,
      y: approach.centerline[1].y - approach.centerline[0].y,
    };
    const tLen = Math.sqrt(tangent.x * tangent.x + tangent.y * tangent.y) || 1;
    const nx = -tangent.y / tLen;
    const ny = tangent.x / tLen;
    const halfW = approach.width / 2;

    stopLines.push({
      approach: approach.direction,
      p1: { x: edgePoint.x + nx * halfW, y: edgePoint.y + ny * halfW },
      p2: { x: edgePoint.x - nx * halfW, y: edgePoint.y - ny * halfW },
    });

    // Crosswalk: 3m before the stop line
    const cwOffset = 3; // meters back from stop line
    const cwCenter = {
      x: edgePoint.x - (tangent.x / tLen) * cwOffset,
      y: edgePoint.y - (tangent.y / tLen) * cwOffset,
    };
    const cwDepth = 2; // crosswalk depth (along road)
    const cwCorners = [
      { x: cwCenter.x + nx * halfW, y: cwCenter.y + ny * halfW },
      { x: cwCenter.x - nx * halfW, y: cwCenter.y - ny * halfW },
      {
        x: cwCenter.x - (tangent.x / tLen) * cwDepth - nx * halfW,
        y: cwCenter.y - (tangent.y / tLen) * cwDepth - ny * halfW,
      },
      {
        x: cwCenter.x - (tangent.x / tLen) * cwDepth + nx * halfW,
        y: cwCenter.y - (tangent.y / tLen) * cwDepth + ny * halfW,
      },
    ];
    crosswalks.push({ approach: approach.direction, corners: cwCorners });
  }

  return {
    center,
    polygon,
    approaches,
    laneConnections,
    stopLines,
    crosswalks,
  };
}

/**
 * Generate the intersection polygon from approach road edges + rounded corners.
 */
/**
 * Generate the intersection polygon from approach road edges + rounded corner fillets.
 *
 * Instead of a convex hull, this builds a proper polygon by:
 * 1. Computing left/right edge points for each approach at the intersection boundary
 * 2. Sorting approaches by angle around the center
 * 3. Connecting adjacent approach edges with fillet arcs (bezier curves)
 *
 * The result is a polygon that follows road edges and has rounded corners,
 * adapting to the angle between roads.
 */
function generateIntersectionPolygon(
  approaches: ApproachRoad[],
  center: Point2D,
  trimDist: number
): Point2D[] {
  if (approaches.length < 2) return [];

  // Step 1: Compute edge points for each approach
  // Each approach has a left edge and right edge at the intersection boundary
  interface ApproachEdges {
    approach: ApproachRoad;
    leftEdge: Point2D;   // left edge point (when looking from center outward)
    rightEdge: Point2D;  // right edge point
    leftInner: Point2D;  // edge point closer to center (at trimDist)
    rightInner: Point2D;
    angle: number;       // angle of approach direction from center
  }

  const approachEdges: ApproachEdges[] = [];

  for (const approach of approaches) {
    if (approach.centerline.length < 2) continue;

    const edge = approach.centerline[0]; // point at intersection boundary
    const tangent = {
      x: approach.centerline[1].x - approach.centerline[0].x,
      y: approach.centerline[1].y - approach.centerline[0].y,
    };
    const tLen = Math.sqrt(tangent.x * tangent.x + tangent.y * tangent.y) || 1;
    const tx = tangent.x / tLen;
    const ty = tangent.y / tLen;
    // Normal (perpendicular, pointing left when looking outward)
    const nx = -ty;
    const ny = tx;
    const halfW = approach.width / 2;

    // Edge points at the boundary (trimDist from center)
    const leftEdge: Point2D = { x: edge.x + nx * halfW, y: edge.y + ny * halfW };
    const rightEdge: Point2D = { x: edge.x - nx * halfW, y: edge.y - ny * halfW };

    // Angle of this approach from center
    const dx = edge.x - center.x;
    const dy = edge.y - center.y;
    const angle = Math.atan2(dy, dx);

    approachEdges.push({
      approach,
      leftEdge,
      rightEdge,
      leftInner: leftEdge,
      rightInner: rightEdge,
      angle,
    });
  }

  if (approachEdges.length < 2) return [];

  // Step 2: Sort approaches by angle around center
  approachEdges.sort((a, b) => a.angle - b.angle);

  // Step 3: Build polygon by connecting edges with fillet arcs
  // For each pair of adjacent approaches, connect the right edge of one
  // to the left edge of the next with a rounded corner
  const polygon: Point2D[] = [];
  const filletRadius = Math.min(
    ...approachEdges.map((ae) => ae.approach.width / 2),
    5 // max 5m radius
  );
  const filletSegments = 6;

  for (let i = 0; i < approachEdges.length; i++) {
    const current = approachEdges[i];
    const next = approachEdges[(i + 1) % approachEdges.length];

    // Add the right edge of current approach
    polygon.push(current.rightEdge);

    // Add fillet arc from current.rightEdge to next.leftEdge
    // The corner is approximately at the intersection of the two road edge lines
    const corner = computeCornerPoint(current.rightEdge, current.approach, next.leftEdge, next.approach, center);
    if (corner) {
      // Compute directions from corner to each edge point
      const dirA = {
        x: current.rightEdge.x - corner.x,
        y: current.rightEdge.y - corner.y,
      };
      const dirB = {
        x: next.leftEdge.x - corner.x,
        y: next.leftEdge.y - corner.y,
      };
      // Normalize
      const lenA = Math.sqrt(dirA.x ** 2 + dirA.y ** 2) || 1;
      const lenB = Math.sqrt(dirB.x ** 2 + dirB.y ** 2) || 1;
      const arc = filletArc(
        corner,
        { x: dirA.x / lenA, y: dirA.y / lenA },
        { x: dirB.x / lenB, y: dirB.y / lenB },
        Math.min(filletRadius, lenA * 0.8, lenB * 0.8),
        filletSegments
      );
      // arc goes from near current.rightEdge to near next.leftEdge
      for (let j = 1; j < arc.length - 1; j++) {
        polygon.push(arc[j]);
      }
    }

    // Add the left edge of next approach
    polygon.push(next.leftEdge);
  }

  return polygon;
}

/**
 * Compute the corner point where two road edge lines meet.
 * This is the intersection of the right edge of approach A and left edge of approach B.
 */
function computeCornerPoint(
  edgeA: Point2D,
  approachA: ApproachRoad,
  edgeB: Point2D,
  approachB: ApproachRoad,
  center: Point2D
): Point2D | null {
  // Direction from center toward edgeA (along road A edge, pointing outward)
  const dirA = {
    x: edgeA.x - center.x,
    y: edgeA.y - center.y,
  };
  const dirB = {
    x: edgeB.x - center.x,
    y: edgeB.y - center.y,
  };

  // The corner is roughly at the midpoint, projected outward
  // For a simple approximation, use the intersection of lines from edgeA and edgeB
  // along their road tangents
  if (approachA.centerline.length < 2 || approachB.centerline.length < 2) {
    return { x: (edgeA.x + edgeB.x) / 2, y: (edgeA.y + edgeB.y) / 2 };
  }

  // Tangent of approach A (outward direction)
  const tA = {
    x: approachA.centerline[1].x - approachA.centerline[0].x,
    y: approachA.centerline[1].y - approachA.centerline[0].y,
  };
  // Tangent of approach B (outward direction)
  const tB = {
    x: approachB.centerline[1].x - approachB.centerline[0].x,
    y: approachB.centerline[1].y - approachB.centerline[0].y,
  };

  // Line A: from edgeA, direction = tangent of A (the edge runs along the road)
  // Line B: from edgeB, direction = tangent of B
  // Find intersection of these two lines
  const result = segmentIntersection(
    { p1: edgeA, p2: { x: edgeA.x + tA.x * 100, y: edgeA.y + tA.y * 100 } },
    { p1: edgeB, p2: { x: edgeB.x + tB.x * 100, y: edgeB.y + tB.y * 100 } }
  );

  if (result) return result;

  // Fallback: midpoint
  return { x: (edgeA.x + edgeB.x) / 2, y: (edgeA.y + edgeB.y) / 2 };
}

/**
 * Generate lane connections between approaches.
 * For each pair of approaches, determine if the connection is straight, left, or right.
 */
function generateLaneConnections(
  approaches: ApproachRoad[],
  center: Point2D
): LaneConnection[] {
  const connections: LaneConnection[] = [];

  for (const from of approaches) {
    for (const to of approaches) {
      if (from.direction === to.direction) continue; // skip same approach

      // Determine turn type based on angle between directions
      const fromDir = {
        x: from.centerline[0].x - center.x,
        y: from.centerline[0].y - center.y,
      };
      const toDir = {
        x: to.centerline[0].x - center.x,
        y: to.centerline[0].y - center.y,
      };

      const fromLen = Math.sqrt(fromDir.x ** 2 + fromDir.y ** 2) || 1;
      const toLen = Math.sqrt(toDir.x ** 2 + toDir.y ** 2) || 1;
      const dot = (fromDir.x * toDir.x + fromDir.y * toDir.y) / (fromLen * toLen);
      const cross = fromDir.x * toDir.y - fromDir.y * toDir.x;

      let type: 'straight' | 'left' | 'right';
      if (dot > 0.3) {
        type = 'straight';
      } else if (cross > 0) {
        type = 'left';
      } else {
        type = 'right';
      }

      // Generate path as a simple spline through the center
      const path: Point2D[] = [
        from.centerline[0],
        { x: center.x, y: center.y },
        to.centerline[0],
      ];

      connections.push({
        fromApproach: from.direction,
        toApproach: to.direction,
        type,
        path,
      });
    }
  }

  return connections;
}

// ═══════════════════════════════════════════════════════════
// CIRCLE ARC TOOL — Constant-radius circular arc with tangent continuity
// ═══════════════════════════════════════════════════════════

/**
 * Compute a circular arc between two points with tangent continuity.
 *
 * Given:
 *  - startPoint: where the arc begins
 *  - startDirection: the heading of the incoming straight (normalized)
 *  - endPoint: where the arc should end (mouse position)
 *
 * Returns:
 *  - center: the circle center
 *  - radius: the arc radius
 *  - startAngle: angle from center to start point
 *  - endAngle: angle from center to end point
 *  - sweep: the angular sweep (positive = left/CCW, negative = right/CW)
 *  - points: sampled points along the arc
 *  - tangentIn: direction at start (matches incoming straight)
 *  - tangentOut: direction at end
 */
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

/**
 * Compute a circular arc from a start point + direction to an end point.
 *
 * The arc starts tangent to the given direction (G1 continuity with the
 * incoming straight), and passes through the end point.
 *
 * Algorithm:
 * 1. The circle center is perpendicular to the start direction, at distance = radius
 * 2. The radius is determined by the end point position
 * 3. The center is on the side that makes the arc pass through the end point
 */
export function computeCircleArc(
  startPoint: Point2D,
  startDirection: Point2D,  // normalized
  endPoint: Point2D,
  segments: number = 32
): CircleArc | null {
  // Vector from start to end
  const dx = endPoint.x - startPoint.x;
  const dy = endPoint.y - startPoint.y;
  const chordLen = Math.sqrt(dx * dx + dy * dy);
  if (chordLen < 0.1) return null;

  // The angle of the chord
  const chordAngle = Math.atan2(dy, dx);

  // The angle of the start direction
  const dirAngle = Math.atan2(startDirection.y, startDirection.x);

  // The angle between the start direction and the chord
  // This determines which side the center is on and the radius
  const halfAngle = (chordAngle - dirAngle) / 2;

  // If halfAngle is 0, the arc is a straight line (infinite radius)
  if (Math.abs(halfAngle) < 1e-6) {
    // Straight line — return a degenerate arc (just the chord)
    const points: Point2D[] = [];
    for (let i = 0; i <= segments; i++) {
      const t = i / segments;
      points.push({
        x: startPoint.x + dx * t,
        y: startPoint.y + dy * t,
      });
    }
    return {
      center: { x: Infinity, y: Infinity },
      radius: Infinity,
      startAngle: 0,
      endAngle: 0,
      sweep: 0,
      points,
      tangentIn: startDirection,
      tangentOut: startDirection,
    };
  }

  // Radius from chord length and sweep angle
  // chord = 2 * r * sin(sweep/2) → r = chord / (2 * sin(sweep/2))
  // The sweep angle = 2 * halfAngle (but we need to determine direction)
  // Actually: the angle between direction and chord = half the sweep angle
  // So sweep = 2 * (chordAngle - dirAngle), but we need to normalize

  // The center is perpendicular to the start direction
  // Perpendicular: rotate direction 90°
  // Left turn (CCW): center is to the left of the direction
  // Right turn (CW): center is to the right

  // Determine turn direction: cross product of direction and chord
  const cross = startDirection.x * dy - startDirection.y * dx;
  const isLeftTurn = cross > 0;

  // The center is at distance r perpendicular to the start direction
  // Perpendicular direction (left = +90°, right = -90°)
  const perpDir = isLeftTurn
    ? { x: -startDirection.y, y: startDirection.x }   // left (CCW)
    : { x: startDirection.y, y: -startDirection.x };   // right (CW)

  // The sweep angle: angle subtended by the arc at the center
  // = 2 * angle between chord and tangent at start
  // The angle between the start direction and the chord = |chordAngle - dirAngle|
  // The inscribed angle theorem: sweep = 2 * (angle between tangent and chord)
  let sweepAngle = 2 * Math.abs(chordAngle - dirAngle);
  // Normalize to [0, 2π]
  if (sweepAngle > Math.PI) sweepAngle = 2 * Math.PI - sweepAngle;

  // Radius
  const radius = chordLen / (2 * Math.sin(sweepAngle / 2));
  if (radius < 0.5 || !isFinite(radius)) {
    // Radius too small or invalid — fallback to straight line
    const points: Point2D[] = [];
    for (let i = 0; i <= segments; i++) {
      const t = i / segments;
      points.push({
        x: startPoint.x + dx * t,
        y: startPoint.y + dy * t,
      });
    }
    return {
      center: { x: startPoint.x + perpDir.x * 1000, y: startPoint.y + perpDir.y * 1000 },
      radius: 1000,
      startAngle: 0,
      endAngle: 0,
      sweep: 0,
      points,
      tangentIn: startDirection,
      tangentOut: { x: dx / chordLen, y: dy / chordLen },
    };
  }

  // Center position
  const center = {
    x: startPoint.x + perpDir.x * radius,
    y: startPoint.y + perpDir.y * radius,
  };

  // Start and end angles (from center)
  const startAngle = Math.atan2(startPoint.y - center.y, startPoint.x - center.x);
  const endAngle = Math.atan2(endPoint.y - center.y, endPoint.x - center.x);

  // Sweep direction
  const sweep = isLeftTurn
    ? normalizeAngle(endAngle - startAngle)   // CCW (positive)
    : -normalizeAngle(startAngle - endAngle);  // CW (negative)

  // Sample points along the arc
  const points: Point2D[] = [];
  const absSweep = Math.abs(sweep);
  for (let i = 0; i <= segments; i++) {
    const t = i / segments;
    const angle = isLeftTurn
      ? startAngle + sweep * t
      : startAngle + sweep * t;
    points.push({
      x: center.x + radius * Math.cos(angle),
      y: center.y + radius * Math.sin(angle),
    });
  }

  // Tangent at end (perpendicular to radius at end point, in direction of travel)
  const tangentOut = isLeftTurn
    ? { x: -Math.sin(endAngle), y: Math.cos(endAngle) }   // CCW
    : { x: Math.sin(endAngle), y: -Math.cos(endAngle) };   // CW

  return {
    center,
    radius,
    startAngle,
    endAngle,
    sweep,
    points,
    tangentIn: startDirection,
    tangentOut,
  };
}

/** Normalize angle to [0, 2π) */
function normalizeAngle(a: number): number {
  while (a < 0) a += 2 * Math.PI;
  while (a >= 2 * Math.PI) a -= 2 * Math.PI;
  return a;
}

/**
 * Convert a CircleArc to control points for a Road.
 * The arc is represented as a series of smooth control points.
 */
export function arcToControlPoints(
  arc: CircleArc,
  refLat: number,
  refLon: number,
  z: number = 0
): ControlPoint[] {
  const points: ControlPoint[] = [];
  for (let i = 0; i < arc.points.length; i++) {
    const geo = localToGeo(arc.points[i].x, arc.points[i].y, refLat, refLon);
    points.push({
      id: generateId(),
      lat: geo.lat,
      lon: geo.lon,
      z,
      handleIn: null,
      handleOut: null,
      type: 'smooth',
    });
  }
  return points;
}
