/**
 * GLAD (Global Land Analysis and Discovery) data client.
 *
 * Provides two data sources from the UMD GLAD API:
 * 1. SRTM DEM — elevation, slope, and aspect at 30m resolution
 *    URL: https://glad.umd.edu/dataset/srtm_v3/<tile>/dem.tif
 * 2. ARD Imagery — 8-band Landsat composites at 30m resolution
 *    URL: https://glad.umd.edu/dataset/glad_ard2/<lat>/<tile>/<interval>.tif
 *
 * Both use Basic auth with public credentials: glad / ardpas
 *
 * Tile naming: tiles are 1°×1°, named by their center's integer degree values.
 * Example: tile centered at 17.5°E, 52.5°N → "017E_52N"
 * The lat folder is the second half of the tile name (e.g., "52N").
 *
 * Runs in the Electron main process (Node.js).
 */

import * as https from 'https';
import { fromArrayBuffer } from 'geotiff';
import type { GeoBounds } from './types';

// ─── Constants ─────────────────────────────────────────────────

const GLAD_BASE_URL = 'https://glad.umd.edu/dataset/glad_ard2';
const GLAD_SRTM_BASE_URL = 'https://glad.umd.edu/dataset/srtm_v3';
const GLAD_USERNAME = 'glad';
const GLAD_PASSWORD = 'ardpas';

/** GLAD ARD tile size in degrees (1°×1° tiles) */
const TILE_SIZE_DEGREES = 1;

// ─── Tile Naming ───────────────────────────────────────────────

/**
 * Converts a lat/lon to a GLAD tile name.
 * Tiles are named by their center's integer degree values.
 * Example: lon=17.5, lat=52.5 → "017E_52N"
 */
export function latLonToGladTileName(lat: number, lon: number): { tileName: string; latFolder: string } {
  const latInt = Math.floor(lat);
  const lonInt = Math.floor(lon);

  const latDir = latInt >= 0 ? 'N' : 'S';
  const lonDir = lonInt >= 0 ? 'E' : 'W';

  const latStr = String(Math.abs(latInt)).padStart(2, '0');
  const lonStr = String(Math.abs(lonInt)).padStart(3, '0');

  const tileName = `${lonStr}${lonDir}_${latStr}${latDir}`;
  const latFolder = `${latStr}${latDir}`;

  return { tileName, latFolder };
}

/**
 * Returns the list of GLAD tiles needed to cover the given bounds.
 */
export function getGladTilesForBounds(bounds: GeoBounds): Array<{ tileName: string; latFolder: string; lat: number; lon: number }> {
  const minLat = Math.floor(bounds.south);
  const maxLat = Math.floor(bounds.north);
  const minLon = Math.floor(bounds.west);
  const maxLon = Math.floor(bounds.east);

  const tiles: Array<{ tileName: string; latFolder: string; lat: number; lon: number }> = [];

  for (let lat = minLat; lat <= maxLat; lat++) {
    for (let lon = minLon; lon <= maxLon; lon++) {
      const { tileName, latFolder } = latLonToGladTileName(lat, lon);
      tiles.push({ tileName, latFolder, lat, lon });
    }
  }

  return tiles;
}

// ─── Auth ──────────────────────────────────────────────────────

function getAuthHeader(): string {
  return 'Basic ' + Buffer.from(`${GLAD_USERNAME}:${GLAD_PASSWORD}`).toString('base64');
}

function buildGladUrl(latFolder: string, tileName: string, fileName: string): string {
  return `${GLAD_BASE_URL}/${latFolder}/${tileName}/${fileName}`;
}

function buildGladSrtmUrl(tileName: string): string {
  return `${GLAD_SRTM_BASE_URL}/${tileName}/dem.tif`;
}

// ─── HTTP Helper ───────────────────────────────────────────────

/**
 * Downloads a buffer from a URL with Basic auth.
 */
async function downloadBufferWithAuth(url: string, authHeader: string): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const urlObj = new URL(url);

    const options: https.RequestOptions = {
      hostname: urlObj.hostname,
      port: urlObj.port || 443,
      path: urlObj.pathname + urlObj.search,
      method: 'GET',
      headers: {
        'Authorization': authHeader,
        'User-Agent': 'GeoTerrain-Studio/2.0.0',
      },
    };

    const req = https.request(options, (res) => {
      if (res.statusCode === 301 || res.statusCode === 302) {
        const location = res.headers.location;
        if (location) {
          downloadBufferWithAuth(location, authHeader).then(resolve).catch(reject);
          return;
        }
      }

      if (res.statusCode !== 200) {
        const chunks: Buffer[] = [];
        res.on('data', (chunk: Buffer) => chunks.push(chunk));
        res.on('end', () => {
          const body = Buffer.concat(chunks).toString('utf-8').substring(0, 500);
          reject(new Error(`GLAD API returned HTTP ${res.statusCode} for URL: ${url}\n${body}`));
        });
        return;
      }

      const chunks: Buffer[] = [];
      res.on('data', (chunk: Buffer) => chunks.push(chunk));
      res.on('end', () => resolve(Buffer.concat(chunks)));
    });

    req.on('error', reject);
    req.setTimeout(120000, () => {
      req.destroy(new Error('GLAD API request timed out'));
    });
    req.end();
  });
}

// ─── SRTM DEM Fetching ─────────────────────────────────────────

/**
 * Fetches SRTM DEM data from GLAD for the given bounds.
 * GLAD provides elevation, slope, and aspect as separate rasters; this fetches the elevation raster.
 * Returns elevation as Float32Array, cropped to the requested bounds.
 */
export async function fetchGladSRTMDEM(
  bounds: GeoBounds,
): Promise<{ elevations: Float32Array; width: number; height: number }> {
  const tiles = getGladTilesForBounds(bounds);

  if (tiles.length === 0) {
    throw new Error('No GLAD tiles found for the selected area');
  }

  const tileData: Array<{
    elevations: Float32Array;
    width: number;
    height: number;
    originLat: number;
    originLon: number;
    pixelWidth: number;
    pixelHeight: number;
  }> = [];

  const authHeader = getAuthHeader();

  for (const tile of tiles) {
    const url = buildGladSrtmUrl(tile.tileName);
    const buffer = await downloadBufferWithAuth(url, authHeader);

    if (buffer.length < 100) {
      throw new Error(`GLAD SRTM tile ${tile.tileName} returned empty response (${buffer.length} bytes)`);
    }

    const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer;
    const tiff = await fromArrayBuffer(arrayBuffer);
    const image = await tiff.getImage();
    const width = image.getWidth();
    const height = image.getHeight();

    const origin = image.getOrigin();
    const resolution = image.getResolution();

    const originLon = origin[0];
    const originLat = origin[1];
    const pixelWidth = Math.abs(resolution[0]);
    const pixelHeight = Math.abs(resolution[1]);

    const rasters = await image.readRasters();
    const raw = rasters[0] as Int16Array | Float32Array | Uint16Array;

    const elevations = new Float32Array(width * height);
    const noDataValue = image.getGDALNoData();

    if (noDataValue !== null && noDataValue !== undefined) {
      for (let i = 0; i < raw.length; i++) {
        const v = raw[i];
        elevations[i] = v === noDataValue ? 0 : v;
      }
    } else {
      for (let i = 0; i < raw.length; i++) {
        elevations[i] = raw[i];
      }
    }

    tileData.push({ elevations, width, height, originLat, originLon, pixelWidth, pixelHeight });
  }

  // Single tile — crop and return
  if (tileData.length === 1) {
    const t = tileData[0];
    return cropGladTile(t.elevations, t.width, t.height, t.originLat, t.originLon, t.pixelWidth, t.pixelHeight, bounds);
  }

  // Multiple tiles — merge then crop
  const minLat = Math.floor(bounds.south);
  const maxLat = Math.floor(bounds.north);
  const minLon = Math.floor(bounds.west);

  const numTilesLat = maxLat - minLat + 1;
  const numTilesLon = Math.floor(bounds.east) - minLon + 1;
  const tileW = tileData[0].width;
  const tileH = tileData[0].height;
  const mergedWidth = numTilesLon * tileW;
  const mergedHeight = numTilesLat * tileH;
  const merged = new Float32Array(mergedWidth * mergedHeight);

  for (const t of tileData) {
    const tileCol = Math.round((t.originLon - minLon) / TILE_SIZE_DEGREES);
    const tileRow = Math.round((maxLat + 1 - t.originLat) / TILE_SIZE_DEGREES);
    const offsetX = tileCol * tileW;
    const offsetY = tileRow * tileH;

    for (let y = 0; y < t.height; y++) {
      for (let x = 0; x < t.width; x++) {
        const dx = offsetX + x;
        const dy = offsetY + y;
        if (dx < mergedWidth && dy < mergedHeight) {
          merged[dy * mergedWidth + dx] = t.elevations[y * t.width + x];
        }
      }
    }
  }

  const mergedOriginLat = maxLat + 1;
  const mergedOriginLon = minLon;
  const pixelW = tileData[0].pixelWidth;
  const pixelH = tileData[0].pixelHeight;

  return cropGladTile(merged, mergedWidth, mergedHeight, mergedOriginLat, mergedOriginLon, pixelW, pixelH, bounds);
}

function cropGladTile(
  elevations: Float32Array,
  srcWidth: number,
  srcHeight: number,
  originLat: number,
  originLon: number,
  pixelWidth: number,
  pixelHeight: number,
  bounds: GeoBounds,
): { elevations: Float32Array; width: number; height: number } {
  const startCol = Math.max(0, Math.floor((bounds.west - originLon) / pixelWidth));
  const endCol = Math.min(srcWidth - 1, Math.ceil((bounds.east - originLon) / pixelWidth) - 1);
  const startRow = Math.max(0, Math.floor((originLat - bounds.north) / pixelHeight));
  const endRow = Math.min(srcHeight - 1, Math.ceil((originLat - bounds.south) / pixelHeight) - 1);

  const cropWidth = Math.max(1, endCol - startCol + 1);
  const cropHeight = Math.max(1, endRow - startRow + 1);

  const cropped = new Float32Array(cropWidth * cropHeight);
  for (let y = 0; y < cropHeight; y++) {
    for (let x = 0; x < cropWidth; x++) {
      cropped[y * cropWidth + x] = elevations[(startRow + y) * srcWidth + (startCol + x)];
    }
  }

  return { elevations: cropped, width: cropWidth, height: cropHeight };
}

// ─── ARD Imagery Fetching ──────────────────────────────────────

/**
 * Fetches a GLAD ARD composite (8-band Landsat GeoTIFF) for the given bounds
 * and interval ID. Returns raw band data suitable for NDVI computation or
 * land cover classification.
 *
 * GLAD ARD bands:
 * 0 = blue, 1 = green, 2 = red, 3 = nir,
 * 4 = swir1, 5 = swir2, 6 = thermal, 7 = QA
 *
 * @param bounds - Geographic bounds
 * @param intervalId - 16-day interval ID (e.g., 920)
 * @returns Array of band buffers + dimensions
 */
export async function fetchGladARDImagery(
  bounds: GeoBounds,
  intervalId: number,
): Promise<{
  bands: Float32Array[];
  width: number;
  height: number;
  bounds: GeoBounds;
}> {
  const tiles = getGladTilesForBounds(bounds);

  if (tiles.length === 0) {
    throw new Error('No GLAD tiles found for the selected area');
  }

  const authHeader = getAuthHeader();

  // For single tile
  if (tiles.length === 1) {
    const tile = tiles[0];
    const url = buildGladUrl(tile.latFolder, tile.tileName, `${intervalId}.tif`);
    const buffer = await downloadBufferWithAuth(url, authHeader);

    if (buffer.length < 100) {
      throw new Error(`GLAD ARD tile ${tile.tileName} interval ${intervalId} returned empty response (${buffer.length} bytes)`);
    }

    const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer;
    const tiff = await fromArrayBuffer(arrayBuffer);
    const image = await tiff.getImage();
    const width = image.getWidth();
    const height = image.getHeight();

    const rasters = await image.readRasters();
    const bands: Float32Array[] = [];
    for (let b = 0; b < rasters.length; b++) {
      const raw = rasters[b] as Int16Array | Uint16Array | Float32Array;
      const band = new Float32Array(width * height);
      for (let i = 0; i < raw.length; i++) {
        band[i] = raw[i];
      }
      bands.push(band);
    }

    return { bands, width, height, bounds };
  }

  // Multiple tiles — merge bands
  const tileResults: Array<{
    bands: Float32Array[];
    width: number;
    height: number;
    lat: number;
    lon: number;
  }> = [];

  for (const tile of tiles) {
    const url = buildGladUrl(tile.latFolder, tile.tileName, `${intervalId}.tif`);
    const buffer = await downloadBufferWithAuth(url, authHeader);

    try {

      if (buffer.length < 100) {
        continue;
      }

      const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer;
      const tiff = await fromArrayBuffer(arrayBuffer);
      const image = await tiff.getImage();
      const width = image.getWidth();
      const height = image.getHeight();

      const rasters = await image.readRasters();
      const bands: Float32Array[] = [];
      for (let b = 0; b < rasters.length; b++) {
        const raw = rasters[b] as Int16Array | Uint16Array | Float32Array;
        const band = new Float32Array(width * height);
        for (let i = 0; i < raw.length; i++) {
          band[i] = raw[i];
        }
        bands.push(band);
      }

      tileResults.push({ bands, width, height, lat: tile.lat, lon: tile.lon });
    } catch {
      // no-op
    }
  }

  if (tileResults.length === 0) {
    throw new Error('All GLAD ARD tiles failed to download');
  }

  // Merge tiles
  const minLat = Math.floor(bounds.south);
  const maxLat = Math.floor(bounds.north);
  const minLon = Math.floor(bounds.west);

  const tileW = tileResults[0].width;
  const tileH = tileResults[0].height;
  const numBands = tileResults[0].bands.length;
  const numTilesLat = maxLat - minLat + 1;
  const numTilesLon = Math.floor(bounds.east) - minLon + 1;
  const mergedWidth = numTilesLon * tileW;
  const mergedHeight = numTilesLat * tileH;

  const mergedBands: Float32Array[] = [];
  for (let b = 0; b < numBands; b++) {
    mergedBands.push(new Float32Array(mergedWidth * mergedHeight));
  }

  for (const tr of tileResults) {
    const tileCol = Math.round((tr.lon - minLon) / TILE_SIZE_DEGREES);
    const tileRow = Math.round((maxLat + 1 - tr.lat - 1) / TILE_SIZE_DEGREES);
    const offsetX = tileCol * tileW;
    const offsetY = tileRow * tileH;

    for (let b = 0; b < numBands; b++) {
      for (let y = 0; y < tr.height; y++) {
        for (let x = 0; x < tr.width; x++) {
          const dx = offsetX + x;
          const dy = offsetY + y;
          if (dx < mergedWidth && dy < mergedHeight) {
            mergedBands[b][dy * mergedWidth + dx] = tr.bands[b][y * tr.width + x];
          }
        }
      }
    }
  }

  return { bands: mergedBands, width: mergedWidth, height: mergedHeight, bounds };
}

/**
 * Computes NDVI from GLAD ARD bands.
 * Band 3 = Near-Infrared (NIR), Band 2 = Red
 * NDVI = (NIR - Red) / (NIR + Red)
 */
export function computeNDVI(bands: Float32Array[], width: number, height: number): Float32Array {
  if (bands.length < 4) {
    throw new Error('GLAD ARD data must have at least 4 bands for NDVI computation');
  }

  const nir = bands[3];
  const red = bands[2];
  const ndvi = new Float32Array(width * height);

  for (let i = 0; i < width * height; i++) {
    const nirVal = nir[i];
    const redVal = red[i];
    const denom = nirVal + redVal;
    ndvi[i] = denom > 0 ? (nirVal - redVal) / denom : 0;
  }

  return ndvi;
}

/**
 * Converts NDVI values to a vegetation mask buffer (0-255 grayscale).
 * NDVI > 0.5 → dense vegetation (255)
 * NDVI 0.3-0.5 → moderate vegetation (180)
 * NDVI 0.1-0.3 → sparse vegetation (100)
 * NDVI < 0.1 → non-vegetation (0)
 */
export function ndviToVegetationMask(ndvi: Float32Array): Buffer {
  const mask = Buffer.alloc(ndvi.length);

  for (let i = 0; i < ndvi.length; i++) {
    const v = ndvi[i];
    if (v > 0.5) mask[i] = 255;
    else if (v > 0.3) mask[i] = 180;
    else if (v > 0.1) mask[i] = 100;
    else mask[i] = 0;
  }

  return mask;
}

/**
 * Converts GLAD ARD bands to an RGB composite buffer (for albedo/imagery output).
 * Uses bands 4 (SWIR1), 3 (NIR), 2 (Red) for a false-color vegetation composite.
 * Values are scaled to 0-255 range.
 */
export function gladBandsToRGB(bands: Float32Array[], width: number, height: number): Buffer {
  if (bands.length < 5) {
    throw new Error('GLAD ARD data must have at least 5 bands for RGB composite');
  }

  const swir1 = bands[4];
  const nir = bands[3];
  const red = bands[2];

  const rgb = Buffer.alloc(width * height * 3);

  for (let i = 0; i < width * height; i++) {
    // Scale reflectance values (typically 0-10000) to 0-255
    rgb[i * 3] = Math.min(255, Math.max(0, Math.round(swir1[i] / 40)));
    rgb[i * 3 + 1] = Math.min(255, Math.max(0, Math.round(nir[i] / 40)));
    rgb[i * 3 + 2] = Math.min(255, Math.max(0, Math.round(red[i] / 40)));
  }

  return rgb;
}
