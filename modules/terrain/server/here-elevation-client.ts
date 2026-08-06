/**
 * HERE Map Attributes API v8 client.
 *
 * Fetches real road surface elevation data from HERE:
 * - BASIC_HEIGHT_FCn: link endpoint heights (DTM_REF_ZCOORD, DTM_NONREF_ZCOORD)
 * - ADAS_ATTRIB_FCn: per-shape-point heights (HPZ), slopes (SLOPES), curvature, headings
 *
 * HERE provides actual road elevation (including bridges, tunnels, elevated roads),
 * unlike DEM which only gives ground surface.
 *
 * The client matches OSM road centerline points to HERE link geometry
 * by spatial proximity and interpolates elevation from HPZ values.
 */

import * as https from 'https';
import * as http from 'http';
import type { BoundingBox, LatLon } from '../../../core/interfaces';

// ─── HERE Tile ID Calculation ──────────────────────────────────

/**
 * Calculate HERE tile ID from lat/lon at a given level.
 * Uses the quadkey-based encoding from HERE documentation.
 */
export function latLonToHereTileId(lat: number, lon: number, level: number): number {
  const degree = 360 / Math.pow(2, level);
  const tileY = Math.floor((90 + lat) / degree);
  const tileX = Math.floor((180 + lon) / degree);

  // Interleave bits to get quadkey
  let interleaved = '';
  const xBits = tileX.toString(2).padStart(level, '0');
  const yBits = tileY.toString(2).padStart(level, '0');
  for (let i = 0; i < level; i++) {
    interleaved += yBits[i] + xBits[i];
  }

  // Convert interleaved binary to quadkey (base 4)
  const decimalQK = parseInt(interleaved, 2);
  const quadkey = decimalQK.toString(4);

  // Prefix with '1' and parse as base 4 to get tile ID
  return parseInt('1' + quadkey, 4);
}

/**
 * Get all HERE tile IDs covering a bounding box at a given level.
 */
export function getTileIdsForBounds(bounds: BoundingBox, level: number): number[] {
  const tileIds = new Set<number>();
  const degree = 360 / Math.pow(2, level);

  const minTileX = Math.floor((180 + bounds.west) / degree);
  const maxTileX = Math.floor((180 + bounds.east) / degree);
  const minTileY = Math.floor((90 + bounds.south) / degree);
  const maxTileY = Math.floor((90 + bounds.north) / degree);

  for (let ty = minTileY; ty <= maxTileY; ty++) {
    for (let tx = minTileX; tx <= maxTileX; tx++) {
      let interleaved = '';
      const xBits = tx.toString(2).padStart(level, '0');
      const yBits = ty.toString(2).padStart(level, '0');
      for (let i = 0; i < level; i++) {
        interleaved += yBits[i] + xBits[i];
      }
      const decimalQK = parseInt(interleaved, 2);
      const quadkey = decimalQK.toString(4);
      tileIds.add(parseInt('1' + quadkey, 4));
    }
  }

  return Array.from(tileIds);
}

// ─── HERE API Types ────────────────────────────────────────────

interface HERETileResponse {
  Tiles?: HERETile[];
}

interface HERETile {
  Meta: {
    layerName: string;
    tileId: number;
    level: number;
    mapRegion: string;
    mapRelease: string;
  };
  Rows: HERERow[];
}

interface HERERow {
  LINK_ID: string;
  // ADAS_ATTRIB fields
  HPX?: string;  // shape point X coords (1E-7 degrees, relative encoding)
  HPY?: string;  // shape point Y coords (1E-7 degrees, relative encoding)
  HPZ?: string;  // shape point Z coords (centimeters, relative encoding)
  SLOPES?: string;  // slopes per segment (1/1000 radians)
  HEADINGS?: string;
  CURVATURES?: string;
  VERTICAL_FLAGS?: string;
  // BASIC_HEIGHT fields
  DTM_REF_ZCOORD?: string;     // height at ref node (cm)
  DTM_NONREF_ZCOORD?: string;  // height at non-ref node (cm)
  DTM_MIN_HEIGHT?: string;
  DTM_MAX_HEIGHT?: string;
  DTM_AVG_HEIGHT?: string;
  // ROAD_GEOM fields
  GEOMETRY?: string;  // shape points as encoded lat/lon
  // LINK_ATTRIBUTE fields
  BRIDGE?: string;
  TUNNEL?: string;
  FUNCTIONAL_CLASS?: string;
  TRAVEL_DIRECTION?: string;
  // Common
  TOPOLOGY_ID?: string;
  START_OFFSET?: string;
  END_OFFSET?: string;
}

// ─── HERE Link with decoded geometry ───────────────────────────

export interface HERELink {
  linkId: string;
  /** Shape points with lat/lon and elevation (meters) */
  shapePoints: HEREShapePoint[];
  /** Slopes per segment (radians) */
  slopes: number[];
  /** Bridge flag */
  bridge: boolean;
  /** Tunnel flag */
  tunnel: boolean;
  /** Functional class (1-5) */
  functionalClass: number;
}

export interface HEREShapePoint {
  lat: number;
  lon: number;
  /** Elevation in meters (from HPZ or DTM height) */
  elevation: number;
}

// ─── HERE Elevation Client ─────────────────────────────────────

export class HEREElevationClient {
  private apiKey: string;
  private baseUrl = 'https://smap.hereapi.com/v8/maps/attributes';
  /** Tile level for ADAS data (level 12 = ~0.087° tiles) */
  private readonly adasLevel = 12;
  /** Cache: tileId → parsed links */
  private tileCache = new Map<number, HERELink[]>();

  constructor(apiKey: string) {
    this.apiKey = apiKey;
  }

  /**
   * Fetch all HERE road links with elevation data for a bounding box.
   * Queries both ADAS_ATTRIB and BASIC_HEIGHT layers for all functional classes.
   */
  async fetchLinksForBounds(bounds: BoundingBox): Promise<HERELink[]> {
    const tileIds = getTileIdsForBounds(bounds, this.adasLevel);
    const allLinks = new Map<string, HERELink>();

    // Combine all layers into a single request per tile to minimize API calls
    const allLayers = [
      ...[1, 2, 3, 4, 5].map(fc => `ADAS_ATTRIB_FC${fc}`),
      ...[1, 2, 3, 4, 5].map(fc => `ROAD_GEOM_FC${fc}`),
      ...[1, 2, 3, 4, 5].map(fc => `BASIC_HEIGHT_FC${fc}`),
    ];

    // Process tiles sequentially with delays to respect rate limits
    for (let i = 0; i < tileIds.length; i++) {
      if (i > 0) {
        await new Promise(resolve => setTimeout(resolve, 2000));
      }
      const tileId = tileIds[i];
      const tiles = await this.fetchTile(allLayers, tileId);

      // Split tiles by layer type
      const adasTiles = tiles.filter(t => t.Meta.layerName.startsWith('ADAS_ATTRIB'));
      const geomTiles = tiles.filter(t => t.Meta.layerName.startsWith('ROAD_GEOM'));
      const heightTiles = tiles.filter(t => t.Meta.layerName.startsWith('BASIC_HEIGHT'));

      this.mergeTileData(adasTiles, geomTiles, heightTiles, allLinks);
    }

    return Array.from(allLinks.values());
  }

  /**
   * Sample elevation at a given lat/lon by finding the nearest HERE link.
   * Returns elevation in meters, or null if no link found within tolerance.
   */
  sampleElevation(lat: number, lon: number, links: HERELink[], maxDistance = 15): number | null {
    let bestElevation: number | null = null;
    let bestDist = Infinity;

    for (const link of links) {
      for (const pt of link.shapePoints) {
        const d = this.haversineDistance(lat, lon, pt.lat, pt.lon);
        if (d < bestDist && d <= maxDistance) {
          bestDist = d;
          bestElevation = pt.elevation;
        }
      }
    }

    return bestElevation;
  }

  /**
   * Sample elevation along a road centerline (array of lat/lon points).
   * Matches each point to the nearest HERE link shape point.
   * Returns elevation in meters for each point, or null where no match.
   */
  sampleElevationForRoad(
    centerline: LatLon[],
    links: HERELink[],
    maxDistance = 15,
  ): (number | null)[] {
    return centerline.map(p => this.sampleElevation(p.lat, p.lon, links, maxDistance));
  }

  // ─── Private Methods ──────────────────────────────────────────

  private async fetchTile(layers: string[], tileId: number): Promise<HERETile[]> {
    const layerParam = layers.join(',');
    const url = `${this.baseUrl}?layers=${layerParam}&in=tile:${tileId}&apiKey=${this.apiKey}`;

    const maxRetries = 5;
    for (let attempt = 0; attempt <= maxRetries; attempt++) {
      try {
        const data = await this.httpGet(url);
        const parsed: HERETileResponse = JSON.parse(data);
        return parsed.Tiles || [];
      } catch (err) {
        const msg = (err as Error).message;
        if (msg.includes('HTTP 429') && attempt < maxRetries) {
          const delayMs = 5000 * (attempt + 1);
          await new Promise(resolve => setTimeout(resolve, delayMs));
          continue;
        }
        return [];
      }
    }
    return [];
  }

  private mergeTileData(
    adasTiles: HERETile[],
    geomTiles: HERETile[],
    heightTiles: HERETile[],
    allLinks: Map<string, HERELink>,
  ): void {
    // Build lookup: LINK_ID → row for each layer
    const adasRows = new Map<string, HERERow>();
    const geomRows = new Map<string, HERERow>();
    const heightRows = new Map<string, HERERow>();

    for (const tile of adasTiles) {
      for (const row of tile.Rows) {
        adasRows.set(row.LINK_ID, row);
      }
    }
    for (const tile of geomTiles) {
      for (const row of tile.Rows) {
        geomRows.set(row.LINK_ID, row);
      }
    }
    for (const tile of heightTiles) {
      for (const row of tile.Rows) {
        heightRows.set(row.LINK_ID, row);
      }
    }

    // Merge: use ADAS for shape points + elevation, fall back to BASIC_HEIGHT
    const allLinkIds = new Set<string>([
      ...adasRows.keys(),
      ...geomRows.keys(),
      ...heightRows.keys(),
    ]);

    for (const linkId of allLinkIds) {
      if (allLinks.has(linkId)) continue; // already processed

      const adasRow = adasRows.get(linkId);
      const geomRow = geomRows.get(linkId);
      const heightRow = heightRows.get(linkId);

      let shapePoints: HEREShapePoint[] = [];
      let slopes: number[] = [];
      let bridge = false;
      let tunnel = false;
      let functionalClass = 5;

      if (adasRow) {
        // ADAS has HPX/HPY/HPZ with relative encoding
        shapePoints = this.decodeADASShapePoints(adasRow);
        if (adasRow.SLOPES) {
          slopes = adasRow.SLOPES.split(',').map(s => parseInt(s, 10) / 1000); // 1/1000 rad → rad
        }
      }

      if (shapePoints.length === 0 && heightRow) {
        // Fall back to BASIC_HEIGHT — only has endpoint heights
        const refZ = heightRow.DTM_REF_ZCOORD ? parseInt(heightRow.DTM_REF_ZCOORD, 10) / 100 : 0; // cm → m
        const nonRefZ = heightRow.DTM_NONREF_ZCOORD ? parseInt(heightRow.DTM_NONREF_ZCOORD, 10) / 100 : 0;
        // We don't have geometry from BASIC_HEIGHT, so just store endpoints
        shapePoints = [
          { lat: 0, lon: 0, elevation: refZ },
          { lat: 0, lon: 0, elevation: nonRefZ },
        ];
      }

      // Get geometry from ROAD_GEOM if ADAS didn't have coordinates
      if (adasRow && shapePoints.length > 0 && shapePoints[0].lat === 0 && geomRow) {
        const geomPoints = this.decodeRoadGeom(geomRow);
        if (geomPoints.length === shapePoints.length) {
          // Merge geometry from ROAD_GEOM with elevation from ADAS
          shapePoints = shapePoints.map((sp, i) => ({
            lat: geomPoints[i].lat,
            lon: geomPoints[i].lon,
            elevation: sp.elevation,
          }));
        }
      }

      if (shapePoints.length >= 2) {
        allLinks.set(linkId, {
          linkId,
          shapePoints,
          slopes,
          bridge,
          tunnel,
          functionalClass,
        });
      }
    }
  }

  /**
   * Decode ADAS shape points from HPX/HPY/HPZ relative encoding.
   * HPX/HPY are in 1E-7 degrees (relative), HPZ is in centimeters (relative).
   */
  private decodeADASShapePoints(row: HERERow): HEREShapePoint[] {
    if (!row.HPX || !row.HPY) return [];

    const hpx = row.HPX.split(',').map(v => parseInt(v || '0', 10));
    const hpy = row.HPY.split(',').map(v => parseInt(v || '0', 10));
    const hpz = row.HPZ ? row.HPZ.split(',').map(v => parseInt(v || '0', 10)) : [];

    const points: HEREShapePoint[] = [];
    let cumLat = 0, cumLon = 0, cumZ = 0;

    for (let i = 0; i < hpx.length; i++) {
      // First value is absolute, subsequent are relative
      if (i === 0) {
        cumLat = hpy[i] / 1e7;
        cumLon = hpx[i] / 1e7;
        cumZ = hpz[i] ? hpz[i] / 100 : 0; // cm → m
      } else {
        cumLat += hpy[i] / 1e7;
        cumLon += hpx[i] / 1e7;
        cumZ += hpz[i] ? hpz[i] / 100 : 0;
      }
      points.push({ lat: cumLat, lon: cumLon, elevation: cumZ });
    }

    return points;
  }

  /**
   * Decode ROAD_GEOM shape points.
   * GEOMETRY field uses similar relative encoding.
   */
  private decodeRoadGeom(row: HERERow): { lat: number; lon: number }[] {
    if (!row.GEOMETRY) return [];

    // HERE ROAD_GEOM uses "lat,lon" pairs separated by commas
    // First pair is absolute, subsequent are relative
    // Values are in 1E-5 degrees
    const parts = row.GEOMETRY.split(',').map(v => parseInt(v || '0', 10));
    const points: { lat: number; lon: number }[] = [];

    let cumLat = 0, cumLon = 0;
    for (let i = 0; i + 1 < parts.length; i += 2) {
      if (i === 0) {
        cumLat = parts[i] / 1e5;
        cumLon = parts[i + 1] / 1e5;
      } else {
        cumLat += parts[i] / 1e5;
        cumLon += parts[i + 1] / 1e5;
      }
      points.push({ lat: cumLat, lon: cumLon });
    }

    return points;
  }

  private haversineDistance(lat1: number, lon1: number, lat2: number, lon2: number): number {
    const R = 6371000; // Earth radius in meters
    const dLat = (lat2 - lat1) * Math.PI / 180;
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const a = Math.sin(dLat / 2) ** 2 +
              Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
              Math.sin(dLon / 2) ** 2;
    return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  }

  private httpGet(url: string): Promise<string> {
    return new Promise((resolve, reject) => {
      const lib = url.startsWith('https') ? https : http;
      const req = lib.get(url, { timeout: 30000 }, (res) => {
        if (res.statusCode && res.statusCode >= 400) {
          reject(new Error(`HTTP ${res.statusCode}`));
          return;
        }
        let data = '';
        res.on('data', chunk => data += chunk);
        res.on('end', () => resolve(data));
      });
      req.on('error', reject);
      req.on('timeout', () => { req.destroy(); reject(new Error('Request timeout')); });
    });
  }
}
