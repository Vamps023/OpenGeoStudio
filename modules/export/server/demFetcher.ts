/**
 * DEM fetching: OpenTopography, NASA Earthdata (Copernicus), GPXZ, tile-based (Terrarium/Mapbox)
 */

import sharp from 'sharp';
import { fromArrayBuffer } from 'geotiff';
import {
  type GeoBounds,
  type DEMSource,
  getOpenTopoDemType,
} from './types';
import { downloadBuffer, downloadTileWithRetry } from './downloader';
export { fetchGladSRTMDEM } from './gladClient';

// ─── OpenTopography API ───────────────────────────────────────

export async function fetchOpenTopoDEM(
  bounds: GeoBounds,
  source: DEMSource,
  apiKeyArg?: string
): Promise<{ elevations: Float32Array; width: number; height: number }> {
  const apiKey = (apiKeyArg || process.env.OPENTOPOGRAPHY_API_KEY || '').trim();
  if (!apiKey) {
    throw new Error(
      'OpenTopography API key is required. ' +
      'Add it in the Export panel → API Keys section, or get a free key at https://portal.opentopography.org/myopentopo'
    );
  }

  const demType = getOpenTopoDemType(source);
  const url =
    `https://portal.opentopography.org/API/globaldem?` +
    `demtype=${demType}` +
    `&south=${bounds.south}` +
    `&north=${bounds.north}` +
    `&west=${bounds.west}` +
    `&east=${bounds.east}` +
    `&outputFormat=GTiff` +
    `&API_Key=${encodeURIComponent(apiKey)}`;

  const buffer = await downloadBuffer(url);

  if (buffer.length < 100) {
    throw new Error(`OpenTopography returned suspiciously small response (${buffer.length} bytes): ${buffer.toString('utf-8').substring(0, 200)}`);
  }

  const isTiff =
    (buffer[0] === 0x49 && buffer[1] === 0x49 && buffer[2] === 0x2a && buffer[3] === 0x00) ||
    (buffer[0] === 0x4d && buffer[1] === 0x4d && buffer[2] === 0x00 && buffer[3] === 0x2a);

  if (!isTiff) {
    throw new Error(`OpenTopography did not return a TIFF. Response: ${buffer.toString('utf-8').substring(0, 500)}`);
  }

  const arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer;
  const tiff = await fromArrayBuffer(arrayBuffer);
  const image = await tiff.getImage();
  const width = image.getWidth();
  const height = image.getHeight();
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

  return { elevations, width, height };
}

// ─── NASA Earthdata / Copernicus DEM ──────────────────────────

export async function fetchNasaEarthdataDEM(
  bounds: GeoBounds,
  _bearerTokenArg?: string
): Promise<{ elevations: Float32Array; width: number; height: number }> {
  const minLat = Math.floor(bounds.south);
  const maxLat = Math.floor(bounds.north);
  const minLon = Math.floor(bounds.west);
  const maxLon = Math.floor(bounds.east);

  const tileData: Array<{
    elevations: Float32Array;
    width: number;
    height: number;
    originLat: number;
    originLon: number;
    pixelWidth: number;
    pixelHeight: number;
  }> = [];

  for (let lat = minLat; lat <= maxLat; lat++) {
    for (let lon = minLon; lon <= maxLon; lon++) {
      const tile = await fetchCopernicusDEMTileWithGeo(lat, lon);
      tileData.push(tile);
    }
  }

  if (tileData.length === 0) {
    throw new Error('No Copernicus DEM tiles found for the selected area');
  }

  if (tileData.length === 1) {
    const t = tileData[0];
    return cropUsingGeoref(t.elevations, t.width, t.height, t.originLat, t.originLon, t.pixelWidth, t.pixelHeight, bounds);
  }

  const numTilesLat = maxLat - minLat + 1;
  const numTilesLon = maxLon - minLon + 1;
  const tileW = tileData[0].width;
  const tileH = tileData[0].height;
  const mergedWidth = numTilesLon * tileW;
  const mergedHeight = numTilesLat * tileH;
  const merged = new Float32Array(mergedWidth * mergedHeight);

  for (const t of tileData) {
    const tileCol = Math.round((t.originLon - minLon) / 1);
    const tileRow = Math.round((maxLat + 1 - t.originLat) / 1);
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

  return cropUsingGeoref(merged, mergedWidth, mergedHeight, mergedOriginLat, mergedOriginLon, pixelW, pixelH, bounds);
}

function cropUsingGeoref(
  elevations: Float32Array,
  srcWidth: number,
  srcHeight: number,
  originLat: number,
  originLon: number,
  pixelWidth: number,
  pixelHeight: number,
  bounds: GeoBounds
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

async function fetchCopernicusDEMTileWithGeo(
  lat: number,
  lon: number
): Promise<{
  elevations: Float32Array;
  width: number;
  height: number;
  originLat: number;
  originLon: number;
  pixelWidth: number;
  pixelHeight: number;
}> {
  const latDir = lat >= 0 ? 'N' : 'S';
  const lonDir = lon >= 0 ? 'E' : 'W';
  const latStr = String(Math.abs(lat)).padStart(2, '0');
  const lonStr = String(Math.abs(lon)).padStart(3, '0');
  const tileName = `Copernicus_DSM_COG_10_${latDir}${latStr}_00_${lonDir}${lonStr}_00_DEM`;

  const url = `https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/${tileName}/${tileName}.tif`;

  const buffer = await downloadTileWithRetry(url, 3, 120000);

  if (buffer.length < 100) {
    throw new Error(`Copernicus DEM tile ${latDir}${latStr}${lonDir}${lonStr} returned empty response`);
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
  const raw = rasters[0] as Float32Array | Int16Array | Uint16Array;

  const elevations = new Float32Array(width * height);
  const noDataValue = image.getGDALNoData();

  if (noDataValue !== null && noDataValue !== undefined) {
    for (let i = 0; i < raw.length; i++) {
      elevations[i] = raw[i] === noDataValue ? 0 : raw[i];
    }
  } else {
    for (let i = 0; i < raw.length; i++) {
      elevations[i] = raw[i];
    }
  }

  return { elevations, width, height, originLat, originLon, pixelWidth, pixelHeight };
}

// ─── GPXZ DEM ─────────────────────────────────────────────────

export async function fetchGPXZDEM(
  bounds: GeoBounds,
  apiKeyArg?: string
): Promise<{ elevations: Float32Array; width: number; height: number }> {
  const apiKey = (apiKeyArg || process.env.GPXZ_API_KEY || '').trim();

  if (!apiKey) {
    throw new Error('GPXZ API key is required. Sign up at https://www.gpxz.io/app/accounts/signup/ to get a free API key.');
  }

  const url = new URL('https://api.gpxz.io/v1/elevation/raster');
  url.searchParams.set('bbox_left', bounds.west.toString());
  url.searchParams.set('bbox_right', bounds.east.toString());
  url.searchParams.set('bbox_bottom', bounds.south.toString());
  url.searchParams.set('bbox_top', bounds.north.toString());
  url.searchParams.set('resolution_m', '5');
  url.searchParams.set('bathymetry', 'false');
  url.searchParams.set('api_key', apiKey);

  const response = await fetch(url.toString(), {
    headers: {
      'User-Agent': 'GeoTerrain-Studio/2.0.0',
      'Accept': 'image/tiff; application=geotiff; profile=cloud-optimized',
    },
  });

  if (!response.ok) {
    if (response.status === 401) {
      throw new Error('GPXZ API key is invalid or expired. Please check your API key in settings.');
    } else if (response.status === 429) {
      const retryAfter = response.headers.get('retry-after');
      throw new Error(`GPXZ rate limit exceeded. Retry after ${retryAfter || 'a few seconds'}. Free tier has 100 requests/day limit.`);
    } else if (response.status === 402) {
      throw new Error('GPXZ quota exceeded. Please upgrade your plan at https://www.gpxz.io/pricing.');
    }

    const errorText = await response.text().catch(() => 'Unknown error');
    throw new Error(`GPXZ API error (${response.status}): ${errorText}`);
  }

  const buffer = Buffer.from(await response.arrayBuffer());

  const tiff = await fromArrayBuffer(buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength));
  const image = await tiff.getImage();
  const rasters = await image.readRasters();

  const elevations = rasters[0] as Float32Array;

  let min = Infinity;
  let max = -Infinity;
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i];
    if (v < min) min = v;
    if (v > max) max = v;
  }

  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i];
    if (v < -1000) {
      continue;
    }
  }

  return {
    elevations,
    width: image.getWidth(),
    height: image.getHeight(),
  };
}

// ─── Tile-based DEM Decode (Terrarium, Mapbox) ────────────────

function decodeTerrariumElevation(r: number, g: number, b: number): number {
  return r * 256 + g + b / 256 - 32768;
}

function decodeMapboxTerrainRGB(r: number, g: number, b: number): number {
  return -10000 + ((r * 256 * 256 + g * 256 + b) * 0.1);
}

export async function decodeDEMTile(buffer: Buffer, source: DEMSource): Promise<Float32Array> {
  const { data, info } = await sharp(buffer)
    .raw()
    .ensureAlpha()
    .toBuffer({ resolveWithObject: true });

  const w = info.width;
  const h = info.height;
  const elevations = new Float32Array(w * h);

  const decoder = source === 'mapbox-terrain-rgb' ? decodeMapboxTerrainRGB : decodeTerrariumElevation;

  for (let i = 0; i < w * h; i++) {
    const r = data[i * 4 + 0];
    const g = data[i * 4 + 1];
    const b = data[i * 4 + 2];
    elevations[i] = decoder(r, g, b);
  }

  return elevations;
}
