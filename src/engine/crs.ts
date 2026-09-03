/**
 * CRS (Coordinate Reference System) helpers.
 *
 * WGS84 lat/lng ↔ UTM easting/northing conversion.
 * Used for GeoTIFF georeferencing and terrain bounds projection.
 */

export interface GeoBounds {
  west: number
  south: number
  east: number
  north: number
}

export interface UTMCoords {
  easting: number
  northing: number
}

/**
 * Get the UTM zone number for a given longitude.
 */
export function utmZoneForLongitude(lng: number): number {
  return Math.min(60, Math.max(1, Math.floor((lng + 180) / 6) + 1))
}

/**
 * Resolve 'auto' CRS to a concrete EPSG code based on bounds centroid.
 * Returns UTM zone EPSG (326xx north / 327xx south) or EPSG:4326.
 */
export function resolveCRS(crsSource: string | undefined, bounds: GeoBounds): string {
  const crs = crsSource || 'EPSG:4326'
  if (crs !== 'auto') return crs
  const centroidLon = (bounds.west + bounds.east) / 2
  const centroidLat = (bounds.north + bounds.south) / 2
  const zone = utmZoneForLongitude(centroidLon)
  return centroidLat >= 0 ? `EPSG:${32600 + zone}` : `EPSG:${32700 + zone}`
}

/**
 * Convert WGS84 lat/lng to UTM easting/northing.
 * Uses transverse Mercator formula (accurate enough for GeoTIFF bounds).
 */
export function latLngToUTM(lat: number, lng: number, zone: number): UTMCoords {
  const a = 6378137.0
  const f = 1 / 298.257223563
  const e2 = 2 * f - f * f
  const k0 = 0.9996
  const lonOrigin = (zone - 1) * 6 - 180 + 3
  const fe = 500000
  const fn = lat < 0 ? 10000000 : 0

  const latRad = (lat * Math.PI) / 180
  const lonRad = (lng * Math.PI) / 180
  const lonOriginRad = (lonOrigin * Math.PI) / 180

  const N = a / Math.sqrt(1 - e2 * Math.sin(latRad) * Math.sin(latRad))
  const T = Math.tan(latRad) * Math.tan(latRad)
  const C = (e2 / (1 - e2)) * Math.cos(latRad) * Math.cos(latRad)
  const A = Math.cos(latRad) * (lonRad - lonOriginRad)
  const M =
    a *
    ((1 - e2 / 4 - (3 * e2 * e2) / 64 - (5 * e2 * e2 * e2) / 256) * latRad -
      ((3 * e2) / 8 + (3 * e2 * e2) / 32 + (45 * e2 * e2 * e2) / 1024) * Math.sin(2 * latRad) +
      ((15 * e2 * e2) / 256 + (45 * e2 * e2 * e2) / 1024) * Math.sin(4 * latRad) -
      ((35 * e2 * e2 * e2) / 3072) * Math.sin(6 * latRad))

  const easting =
    fe +
    k0 *
      N *
      (A +
        ((1 - T + C) * A * A * A) / 6 +
        ((5 - 18 * T + T * T + 72 * C - 58 * 0.00673949674227) * A * A * A * A * A) / 120)
  const northing =
    fn +
    k0 * M +
    k0 *
      N *
      Math.tan(latRad) *
      ((A * A) / 2 +
        ((5 - T + 9 * C + 4 * C * C) * A * A * A * A) / 24 +
        ((61 - 58 * T + T * T + 600 * C - 330 * 0.00673949674227) * A * A * A * A * A * A) / 720)

  return { easting, northing }
}

/**
 * Convert WGS84 bounds to UTM bounds (easting/northing in meters).
 */
export function boundsToUTM(bounds: GeoBounds, zone: number): GeoBounds {
  const nw = latLngToUTM(bounds.north, bounds.west, zone)
  const se = latLngToUTM(bounds.south, bounds.east, zone)
  return { west: nw.easting, north: nw.northing, east: se.easting, south: se.northing }
}

/**
 * Extract EPSG code from a CRS string like "EPSG:4326" or "EPSG:32633".
 */
export function parseEPSG(crs: string): number {
  const match = crs.match(/EPSG:(\d+)/)
  return match ? parseInt(match[1], 10) : 4326
}

/**
 * Check if an EPSG code is a UTM zone (32601–32760).
 */
export function isUTM(epsgCode: number): boolean {
  return epsgCode >= 32601 && epsgCode <= 32760
}

/**
 * Get UTM zone number from EPSG code.
 */
export function utmZoneFromEPSG(epsgCode: number): number {
  if (epsgCode >= 32701) return epsgCode - 32700
  return epsgCode - 32600
}

/**
 * Estimate tile size in meters for given bounds.
 */
export function estimateTileSizeMeters(bounds: GeoBounds): {
  widthM: number
  heightM: number
} {
  const midLatRad = ((bounds.north + bounds.south) * 0.5 * Math.PI) / 180.0
  const metersPerDegLat = 111320.0
  const metersPerDegLon = metersPerDegLat * Math.cos(midLatRad)
  return {
    widthM: Math.max(1.0, (bounds.east - bounds.west) * metersPerDegLon),
    heightM: Math.max(1.0, (bounds.north - bounds.south) * metersPerDegLat),
  }
}
