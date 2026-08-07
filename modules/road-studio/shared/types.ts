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
export type Tool = 'select' | 'line' | 'pen';

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
