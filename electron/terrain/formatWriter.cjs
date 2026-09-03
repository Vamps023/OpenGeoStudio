/**
 * Heightmap and albedo format writers.
 *
 * Heightmap formats:
 *   - png      — 16-bit grayscale PNG (normalized)
 *   - r16      — Raw 16-bit little-endian (normalized)
 *   - geotiff  — UInt16 GeoTIFF (normalized)
 *   - dem      — Int16 GeoTIFF (absolute meters)
 *   - float32  — Float32 GeoTIFF (absolute meters)
 *
 * Albedo formats:
 *   - png      — RGB PNG
 *   - geotiff  — RGB GeoTIFF
 */

const fs = require('node:fs/promises')
const sharp = require('sharp')
const { writeGeoTIFF } = require('./geotiff-writer.cjs')

// ─── Heightmap Writers ────────────────────────────────────────

function computeElevationMetadata(elevations) {
  let min = Infinity
  let max = -Infinity
  let hasNoData = false
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i]
    if (!isFinite(v) || isNaN(v)) {
      hasNoData = true
      continue
    }
    if (v < min) min = v
    if (v > max) max = v
  }
  if (min === Infinity) return { min: 0, max: 0, range: 0, hasNoData: true }
  return { min, max, range: max - min, hasNoData }
}

async function writeHeightmapPNG(elevations, width, height, outputPath, metadata) {
  const { min, range } = metadata
  const uint16 = new Uint16Array(width * height)
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i]
    if (isNaN(v) || !isFinite(v)) v = min
    const norm = Math.round(((v - min) / (range || 1)) * 65535)
    uint16[i] = Math.max(0, Math.min(65535, norm))
  }
  await sharp(uint16, { raw: { width, height, channels: 1 } })
    .png({ compressionLevel: 9 })
    .toFile(outputPath)
}

async function writeHeightmapR16(elevations, width, height, outputPath, metadata) {
  const { min, range } = metadata
  const buf = Buffer.allocUnsafe(width * height * 2)
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i]
    if (isNaN(v) || !isFinite(v)) v = min
    const norm = Math.round(((v - min) / (range || 1)) * 65535)
    buf.writeUInt16LE(Math.max(0, Math.min(65535, norm)), i * 2)
  }
  await fs.writeFile(outputPath, buf)
}

async function writeHeightmapGeoTIFFInt16(elevations, width, height, bounds, outputPath, compression, crs) {
  let minElev = Infinity
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i]
    if (isFinite(v) && v < minElev) minElev = v
  }
  if (minElev === Infinity) minElev = 0
  const int16 = new Int16Array(width * height)
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i]
    if (isNaN(v) || !isFinite(v)) v = minElev
    int16[i] = Math.round(Math.max(-32768, Math.min(32767, v)))
  }
  const buf = writeGeoTIFF(int16, {
    width, height, bitsPerSample: 16, sampleFormat: 2, samplesPerPixel: 1,
    photometricInterpretation: 1, bounds, compression: compression || 'none',
    rasterType: 'point', crs: crs || 'EPSG:4326',
  })
  await fs.writeFile(outputPath, buf)
}

async function writeHeightmapGeoTIFFUint16(elevations, width, height, bounds, outputPath, compression, crs) {
  const meta = computeElevationMetadata(elevations)
  const uint16 = new Uint16Array(width * height)
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i]
    if (isNaN(v) || !isFinite(v)) v = meta.min
    const norm = Math.round(((v - meta.min) / (meta.range || 1)) * 65535)
    uint16[i] = Math.max(0, Math.min(65535, norm))
  }
  const buf = writeGeoTIFF(uint16, {
    width, height, bitsPerSample: 16, sampleFormat: 1, samplesPerPixel: 1,
    photometricInterpretation: 1, bounds, compression: compression || 'none',
    rasterType: 'point', crs: crs || 'EPSG:4326',
  })
  await fs.writeFile(outputPath, buf)
}

async function writeHeightmapGeoTIFFFloat32(elevations, width, height, bounds, outputPath, compression, crs) {
  let minElev = Infinity
  for (let i = 0; i < elevations.length; i++) {
    const v = elevations[i]
    if (isFinite(v) && v < minElev) minElev = v
  }
  if (minElev === Infinity) minElev = 0
  const float32 = new Float32Array(width * height)
  for (let i = 0; i < elevations.length; i++) {
    let v = elevations[i]
    if (isNaN(v) || !isFinite(v)) v = minElev
    float32[i] = v
  }
  const buf = writeGeoTIFF(float32, {
    width, height, bitsPerSample: 32, sampleFormat: 3, samplesPerPixel: 1,
    photometricInterpretation: 1, bounds, compression: compression || 'none',
    rasterType: 'point', crs: crs || 'EPSG:4326',
  })
  await fs.writeFile(outputPath, buf)
}

// ─── Albedo Writers ───────────────────────────────────────────

async function writeAlbedoPNG(rgba, width, height, outputPath) {
  await sharp(rgba, { raw: { width, height, channels: 4 } })
    .removeAlpha()
    .png({ compressionLevel: 9 })
    .toFile(outputPath)
}

async function writeAlbedoGeoTIFF(rgba, width, height, bounds, outputPath, compression, crs) {
  const rgb = Buffer.allocUnsafe(width * height * 3)
  for (let i = 0; i < width * height; i++) {
    rgb[i * 3] = rgba[i * 4]
    rgb[i * 3 + 1] = rgba[i * 4 + 1]
    rgb[i * 3 + 2] = rgba[i * 4 + 2]
  }
  const buf = writeGeoTIFF(rgb, {
    width, height, bitsPerSample: 8, sampleFormat: 1, samplesPerPixel: 3,
    photometricInterpretation: 2, bounds, compression: compression || 'none',
    crs: crs || 'EPSG:4326',
  })
  await fs.writeFile(outputPath, buf)
}

// ─── Dispatcher ───────────────────────────────────────────────

async function writeHeightmap(elevations, width, height, bounds, format, outputPath, compression, crs) {
  const meta = computeElevationMetadata(elevations)
  switch (format) {
    case 'png':
      return writeHeightmapPNG(elevations, width, height, outputPath, meta)
    case 'r16':
      return writeHeightmapR16(elevations, width, height, outputPath, meta)
    case 'geotiff':
      return writeHeightmapGeoTIFFUint16(elevations, width, height, bounds, outputPath, compression, crs)
    case 'dem':
      return writeHeightmapGeoTIFFInt16(elevations, width, height, bounds, outputPath, compression, crs)
    case 'float32':
      return writeHeightmapGeoTIFFFloat32(elevations, width, height, bounds, outputPath, compression, crs)
    case 'none':
      return null
    default:
      throw new Error(`Unknown heightmap format: ${format}`)
  }
}

async function writeAlbedo(rgba, width, height, bounds, format, outputPath, compression, crs) {
  switch (format) {
    case 'png':
      return writeAlbedoPNG(rgba, width, height, outputPath)
    case 'geotiff':
      return writeAlbedoGeoTIFF(rgba, width, height, bounds, outputPath, compression, crs)
    case 'none':
      return null
    default:
      throw new Error(`Unknown albedo format: ${format}`)
  }
}

module.exports = {
  writeHeightmap,
  writeAlbedo,
  computeElevationMetadata,
  writeHeightmapPNG,
  writeHeightmapR16,
  writeHeightmapGeoTIFFInt16,
  writeHeightmapGeoTIFFUint16,
  writeHeightmapGeoTIFFFloat32,
  writeAlbedoPNG,
  writeAlbedoGeoTIFF,
}
