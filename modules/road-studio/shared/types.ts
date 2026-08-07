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
