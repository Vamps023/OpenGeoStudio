/**
 * Stadia Maps Elevation API client.
 *
 * Free tier: 200,000 credits/month, no credit card required.
 * https://docs.stadiamaps.com/elevation/
 *
 * Provides ground-level elevation for any point on Earth.
 * Uses SRTM and Copernicus DEM data sources.
 *
 * For bridge/tunnel/layer handling, OSM tags are used to adjust
 * the ground elevation (already handled in road-network-builder.ts).
 *
 * Get a free API key at https://client.stadiamaps.com/signup/
 */

import * as https from 'https';

export type StadiaElevationLookup = (lat: number, lon: number) => number | null;

export class StadiaElevationClient {
  private readonly apiKey?: string;
  private readonly endpoint = 'https://api.stadiamaps.com/elevation/v1';

  constructor(apiKey?: string) {
    this.apiKey = apiKey?.trim() || undefined;
  }

  /**
   * Fetch elevation for an array of lat/lon points.
   * Stadia Maps accepts up to 500 points per request via the `shape` parameter.
   * Returns a map of "lat,lon" → elevation (meters).
   */
  async fetchElevations(
    points: Array<{ lat: number; lon: number }>,
  ): Promise<Map<string, number>> {
    const result = new Map<string, number>();
    if (points.length === 0) return result;

    const BATCH_SIZE = 500;
    for (let i = 0; i < points.length; i += BATCH_SIZE) {
      const batch = points.slice(i, i + BATCH_SIZE);
      try {
        const elevations = await this.fetchBatch(batch);
        for (let j = 0; j < batch.length && j < elevations.length; j++) {
          const key = `${batch[j].lat.toFixed(7)},${batch[j].lon.toFixed(7)}`;
          result.set(key, elevations[j]);
        }
      } catch {
        // no-op
      }
      if (i + BATCH_SIZE < points.length) {
        await new Promise(resolve => setTimeout(resolve, 200));
      }
    }

    return result;
  }

  private async fetchBatch(
    points: Array<{ lat: number; lon: number }>,
  ): Promise<number[]> {
    const body = JSON.stringify({
      shape: points.map(p => ({ lat: p.lat, lon: p.lon })),
    });

    const url = new URL(this.endpoint);
    if (this.apiKey) {
      url.searchParams.set('api_key', this.apiKey);
    }

    return new Promise((resolve, reject) => {
      const options: https.RequestOptions = {
        hostname: url.hostname,
        path: url.pathname + url.search,
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Content-Length': Buffer.byteLength(body),
        },
      };

      const req = https.request(options, (res) => {
        let data = '';
        res.on('data', (chunk: Buffer) => (data += chunk.toString()));
        res.on('end', () => {
          if (res.statusCode === 200) {
            try {
              const parsed = JSON.parse(data);
              // Response format: { shape: [...], height: [z1, z2, ...] }
              if (Array.isArray(parsed.height)) {
                resolve(parsed.height as number[]);
              } else if (Array.isArray(parsed.elevation)) {
                resolve(parsed.elevation as number[]);
              } else {
                resolve(points.map(() => 0));
              }
            } catch (e) {
              reject(new Error(`Failed to parse Stadia response: ${(e as Error).message}`));
            }
          } else if (res.statusCode === 429) {
            reject(new Error('Stadia Maps rate limit reached (429)'));
          } else {
            reject(new Error(`Stadia Maps API error: ${res.statusCode} ${data.substring(0, 200)}`));
          }
        });
      });

      req.on('error', reject);
      req.setTimeout(30000, () => {
        req.destroy(new Error('Stadia Maps request timed out'));
      });
      req.write(body);
      req.end();
    });
  }

  /**
   * Build an elevation lookup function for all centerline points in a set of roads.
   * Returns a function that can be called with (lat, lon) to get elevation.
   * Uses a grid-based nearest-point search for robust matching.
   */
  async buildElevationLookup(
    roads: Array<{ centerline: Array<{ lat: number; lon: number }> }>,
  ): Promise<(lat: number, lon: number) => number | null> {
    const allPoints: Array<{ lat: number; lon: number }> = [];
    for (const road of roads) {
      for (const pt of road.centerline) {
        allPoints.push({ lat: pt.lat, lon: pt.lon });
      }
    }

    const elevMap = await this.fetchElevations(allPoints);

    // Build a grid index for fast nearest-point lookup
    // Grid cell size ~0.001 degrees (~111m)
    const GRID_SIZE = 0.001;
    const grid = new Map<string, Array<{ lat: number; lon: number; z: number }>>();

    for (const pt of allPoints) {
      const key = `${Math.round(pt.lat / GRID_SIZE)},${Math.round(pt.lon / GRID_SIZE)}`;
      const z = elevMap.get(`${pt.lat.toFixed(7)},${pt.lon.toFixed(7)}`);
      if (z !== undefined && Number.isFinite(z)) {
        if (!grid.has(key)) grid.set(key, []);
        grid.get(key)!.push({ lat: pt.lat, lon: pt.lon, z });
      }
    }

    return (lat: number, lon: number): number | null => {
      // Try exact match first
      const exactKey = `${lat.toFixed(7)},${lon.toFixed(7)}`;
      const exactZ = elevMap.get(exactKey);
      if (exactZ !== undefined && Number.isFinite(exactZ)) return exactZ;

      // Grid-based nearest-point search
      const gridLat = Math.round(lat / GRID_SIZE);
      const gridLon = Math.round(lon / GRID_SIZE);
      let bestDist = Infinity;
      let bestZ: number | null = null;

      // Search in 3x3 grid cells around the target
      for (let dLat = -1; dLat <= 1; dLat++) {
        for (let dLon = -1; dLon <= 1; dLon++) {
          const key = `${gridLat + dLat},${gridLon + dLon}`;
          const cell = grid.get(key);
          if (!cell) continue;
          for (const pt of cell) {
            const dist = (pt.lat - lat) ** 2 + (pt.lon - lon) ** 2;
            if (dist < bestDist) {
              bestDist = dist;
              bestZ = pt.z;
            }
          }
        }
      }

      return bestZ;
    };
  }
}
