/**
 * GeoTIFF Writer — Float32, Int16, UInt16 support.
 *
 * Writes TIFF files with proper GeoTIFF tags.
 * Supports compression: none, Deflate (zlib).
 *
 * Byte order: little-endian ("II")
 * Layout: single IFD, single image strip.
 */

const zlib = require('node:zlib')

// ─── TIFF Constants ───────────────────────────────────────────

const TIFF_MAGIC = 42
const BYTE_ORDER_LE = 0x4949

const TYPE_ASCII = 2
const TYPE_SHORT = 3
const TYPE_LONG = 4
const TYPE_DOUBLE = 12

const TAG_IMAGE_WIDTH = 256
const TAG_IMAGE_LENGTH = 257
const TAG_BITS_PER_SAMPLE = 258
const TAG_COMPRESSION = 259
const TAG_PHOTOMETRIC = 262
const TAG_STRIP_OFFSETS = 273
const TAG_SAMPLES_PER_PIXEL = 277
const TAG_ROWS_PER_STRIP = 278
const TAG_STRIP_BYTE_COUNTS = 279
const TAG_PLANAR_CONFIG = 284
const TAG_SAMPLE_FORMAT = 339

const TAG_MODEL_PIXEL_SCALE = 33550
const TAG_MODEL_TIEPOINT = 33922
const TAG_GEO_KEY_DIRECTORY = 34735
const TAG_GEO_DOUBLE_PARAMS = 34736
const TAG_GEO_ASCII_PARAMS = 34737

// ─── Helpers ──────────────────────────────────────────────────

function valueSizeBytes(type) {
  switch (type) {
    case 1:
    case 2:
      return 1
    case 3:
      return 2
    case 4:
    case 11:
      return 4
    case 5:
    case 12:
      return 8
    default:
      return 1
  }
}

function fitsInline(entry) {
  return entry.count * valueSizeBytes(entry.type) <= 4
}

function createIfdEntry(tag, type, values, asciiData, countOverride) {
  return { tag, type, count: countOverride ?? values.length, values, asciiData }
}

// ─── CRS / UTM ────────────────────────────────────────────────

function latLngToUTM(lat, lng, zone) {
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

function convertBoundsToUTM(bounds, zone) {
  const nw = latLngToUTM(bounds.north, bounds.west, zone)
  const se = latLngToUTM(bounds.south, bounds.east, zone)
  return { west: nw.easting, north: nw.northing, east: se.easting, south: se.northing }
}

// ─── Main Writer ──────────────────────────────────────────────

/**
 * Write a GeoTIFF file.
 *
 * @param {Float32Array|Int16Array|Uint16Array|Buffer} pixelData
 * @param {object} options
 * @param {number} options.width
 * @param {number} options.height
 * @param {8|16|32} options.bitsPerSample
 * @param {1|2|3} options.sampleFormat — 1=uint, 2=sint, 3=float
 * @param {1|3} options.samplesPerPixel
 * @param {1|2} options.photometricInterpretation
 * @param {object} options.bounds — { west, south, east, north }
 * @param {'none'|'deflate'} [options.compression]
 * @param {'area'|'point'} [options.rasterType]
 * @param {string} [options.crs] — e.g. 'EPSG:4326', 'EPSG:32633', 'auto'
 * @returns {Buffer}
 */
function writeGeoTIFF(pixelData, options) {
  const {
    width,
    height,
    bitsPerSample,
    sampleFormat,
    samplesPerPixel,
    photometricInterpretation,
    bounds,
    compression = 'none',
    rasterType = 'area',
    crs = 'EPSG:4326',
  } = options

  const expectedBytes = width * height * samplesPerPixel * (bitsPerSample / 8)
  if (pixelData.byteLength < expectedBytes) {
    throw new Error(
      `GeoTIFF write failed: input data has ${pixelData.byteLength} bytes but expected ${expectedBytes}`,
    )
  }

  const bytesPerSample = bitsPerSample / 8
  const stripByteCount = width * height * samplesPerPixel * bytesPerSample

  // ── Resolve CRS ───────────────────────────────────────────
  let effectiveCRS = crs
  if (crs === 'auto') {
    const centroidLon = (bounds.west + bounds.east) / 2
    const centroidLat = (bounds.north + bounds.south) / 2
    const zone = Math.min(60, Math.max(1, Math.floor((centroidLon + 180) / 6) + 1))
    effectiveCRS = centroidLat >= 0 ? `EPSG:${32600 + zone}` : `EPSG:${32700 + zone}`
  }

  let effectiveBounds = bounds
  const epsgMatch = effectiveCRS.match(/EPSG:(\d+)/)
  const epsgCode = epsgMatch ? parseInt(epsgMatch[1], 10) : 4326
  const isUTM = epsgCode >= 32601 && epsgCode <= 32760
  const utmZone = isUTM ? (epsgCode >= 32701 ? epsgCode - 32700 : epsgCode - 32600) : 0

  if (isUTM && utmZone > 0) {
    effectiveBounds = convertBoundsToUTM(bounds, utmZone)
  }

  const isPoint = rasterType === 'point'
  const pixelWidth = (effectiveBounds.east - effectiveBounds.west) / (isPoint ? width - 1 : width)
  const pixelHeight = (effectiveBounds.south - effectiveBounds.north) / (isPoint ? height - 1 : height)

  if (Math.abs(pixelWidth) < 1e-10 || Math.abs(pixelHeight) < 1e-10) {
    throw new Error(`[GeoTIFF] Invalid pixel scale: width=${pixelWidth}, height=${pixelHeight}.`)
  }

  // ── Build IFD entries ─────────────────────────────────────
  const entries = []
  entries.push(createIfdEntry(TAG_IMAGE_WIDTH, TYPE_LONG, [width]))
  entries.push(createIfdEntry(TAG_IMAGE_LENGTH, TYPE_LONG, [height]))
  entries.push(createIfdEntry(TAG_BITS_PER_SAMPLE, TYPE_SHORT, [bitsPerSample]))
  const compressionCode = compression === 'deflate' ? 32946 : 1
  entries.push(createIfdEntry(TAG_COMPRESSION, TYPE_SHORT, [compressionCode]))
  entries.push(createIfdEntry(TAG_PHOTOMETRIC, TYPE_SHORT, [photometricInterpretation]))
  entries.push(createIfdEntry(TAG_STRIP_OFFSETS, TYPE_LONG, [0]))
  entries.push(createIfdEntry(TAG_SAMPLES_PER_PIXEL, TYPE_SHORT, [samplesPerPixel]))
  entries.push(createIfdEntry(TAG_ROWS_PER_STRIP, TYPE_LONG, [height]))
  entries.push(createIfdEntry(TAG_STRIP_BYTE_COUNTS, TYPE_LONG, [stripByteCount]))
  entries.push(createIfdEntry(TAG_PLANAR_CONFIG, TYPE_SHORT, [1]))
  entries.push(createIfdEntry(TAG_SAMPLE_FORMAT, TYPE_SHORT, [sampleFormat]))

  entries.push(createIfdEntry(TAG_MODEL_PIXEL_SCALE, TYPE_DOUBLE, [Math.abs(pixelWidth), Math.abs(pixelHeight), 0]))
  entries.push(
    createIfdEntry(TAG_MODEL_TIEPOINT, TYPE_DOUBLE, [0, 0, 0, effectiveBounds.west, effectiveBounds.north, 0]),
  )

  const isProjected = isUTM
  const isWebMercator = epsgCode === 3857
  const rasterTypeCode = isPoint ? 2 : 1

  let geoKeys
  let geoDoubleParams = [6378137.0, 298.257223563]
  let citation = 'WGS 84'

  if (isProjected) {
    const zone = epsgCode - (epsgCode >= 32701 ? 32700 : 32600)
    const isNorth = epsgCode < 32701
    citation = `WGS 84 / UTM zone ${zone}${isNorth ? 'N' : 'S'}`
    geoKeys = [
      1, 1, 0, 6,
      1024, 0, 1, 1,
      1025, 0, 1, rasterTypeCode,
      3072, 0, 1, epsgCode,
      3073, 34737, citation.length + 1, 0,
      2057, 34736, 1, 0,
      2059, 34736, 1, 1,
    ]
  } else if (isWebMercator) {
    citation = 'WGS 84 / Pseudo-Mercator'
    geoKeys = [
      1, 1, 0, 6,
      1024, 0, 1, 1,
      1025, 0, 1, rasterTypeCode,
      3072, 0, 1, 3857,
      3073, 34737, citation.length + 1, 0,
      2057, 34736, 1, 0,
      2059, 34736, 1, 1,
    ]
  } else {
    citation = epsgCode === 4326 ? 'WGS 84' : `EPSG:${epsgCode}`
    geoKeys = [
      1, 1, 0, 7,
      1024, 0, 1, 2,
      1025, 0, 1, rasterTypeCode,
      2048, 0, 1, epsgCode,
      2049, 34737, citation.length + 1, 0,
      2054, 0, 1, 9102,
      2057, 34736, 1, 0,
      2059, 34736, 1, 1,
    ]
  }

  entries.push(createIfdEntry(TAG_GEO_KEY_DIRECTORY, TYPE_SHORT, geoKeys))
  entries.push(createIfdEntry(TAG_GEO_DOUBLE_PARAMS, TYPE_DOUBLE, geoDoubleParams))

  const asciiParamsRaw = Buffer.from(`${citation}\0`, 'ascii')
  const asciiParams =
    asciiParamsRaw.length % 2 === 0 ? asciiParamsRaw : Buffer.concat([asciiParamsRaw, Buffer.from([0])])
  entries.push(
    createIfdEntry(TAG_GEO_ASCII_PARAMS, TYPE_ASCII, [asciiParams.length], asciiParams, asciiParams.length),
  )

  entries.sort((a, b) => a.tag - b.tag)

  // ── Calculate layout ──────────────────────────────────────
  const headerSize = 8
  const ifdSize = 2 + entries.length * 12 + 4
  let currentOffset = headerSize + ifdSize

  const inlineValues = new Map()
  const externalBlobs = []

  for (let i = 0; i < entries.length; i++) {
    const entry = entries[i]
    if (fitsInline(entry)) {
      const buf = Buffer.allocUnsafe(4)
      buf.fill(0)
      switch (entry.type) {
        case TYPE_SHORT:
          for (let j = 0; j < entry.count && j < 2; j++) buf.writeUInt16LE(entry.values[j], j * 2)
          break
        case TYPE_LONG:
          buf.writeUInt32LE(entry.values[0], 0)
          break
        case TYPE_ASCII:
          if (entry.asciiData) entry.asciiData.copy(buf, 0, 0, Math.min(entry.asciiData.length, 4))
          break
      }
      inlineValues.set(i, buf.readUInt32LE(0))
    } else {
      const valueBytes = entry.count * valueSizeBytes(entry.type)
      const blobBuf = Buffer.allocUnsafe(valueBytes)
      blobBuf.fill(0)
      switch (entry.type) {
        case TYPE_SHORT:
          for (let j = 0; j < entry.count; j++) blobBuf.writeUInt16LE(entry.values[j], j * 2)
          break
        case TYPE_LONG:
          for (let j = 0; j < entry.count; j++) blobBuf.writeUInt32LE(entry.values[j], j * 4)
          break
        case TYPE_DOUBLE:
          for (let j = 0; j < entry.count; j++) blobBuf.writeDoubleLE(entry.values[j], j * 8)
          break
        case TYPE_ASCII:
          if (entry.asciiData) entry.asciiData.copy(blobBuf, 0, 0, entry.asciiData.length)
          break
      }
      externalBlobs.push({ entryIndex: i, offset: currentOffset, data: blobBuf })
      inlineValues.set(i, currentOffset)
      currentOffset += valueBytes
      if (currentOffset % 2 !== 0) currentOffset++
    }
  }

  const stripOffset = currentOffset
  const stripOffsetsEntryIdx = entries.findIndex((e) => e.tag === TAG_STRIP_OFFSETS)
  if (stripOffsetsEntryIdx >= 0) inlineValues.set(stripOffsetsEntryIdx, stripOffset)

  // ── Serialize pixel data ──────────────────────────────────
  const rawPixelBuf = Buffer.allocUnsafe(stripByteCount)
  if (pixelData instanceof Int16Array || pixelData instanceof Uint16Array || pixelData instanceof Float32Array) {
    Buffer.from(pixelData.buffer, pixelData.byteOffset, pixelData.byteLength).copy(rawPixelBuf)
  } else {
    pixelData.copy(rawPixelBuf)
  }

  const stripData = compression === 'deflate' ? zlib.deflateSync(rawPixelBuf) : rawPixelBuf
  const actualStripByteCount = stripData.length

  const stripByteCountsEntryIdx = entries.findIndex((e) => e.tag === TAG_STRIP_BYTE_COUNTS)
  if (stripByteCountsEntryIdx >= 0) inlineValues.set(stripByteCountsEntryIdx, actualStripByteCount)

  const totalSize = stripOffset + actualStripByteCount
  const file = Buffer.allocUnsafe(totalSize)
  file.fill(0)

  // Header
  file.writeUInt16LE(BYTE_ORDER_LE, 0)
  file.writeUInt16LE(TIFF_MAGIC, 2)
  file.writeUInt32LE(headerSize, 4)

  // IFD
  let pos = headerSize
  file.writeUInt16LE(entries.length, pos)
  pos += 2
  for (let i = 0; i < entries.length; i++) {
    const entry = entries[i]
    file.writeUInt16LE(entry.tag, pos)
    file.writeUInt16LE(entry.type, pos + 2)
    file.writeUInt32LE(entry.count, pos + 4)
    file.writeUInt32LE(inlineValues.get(i) ?? 0, pos + 8)
    pos += 12
  }
  file.writeUInt32LE(0, pos)

  // External blobs
  for (const blob of externalBlobs) blob.data.copy(file, blob.offset)

  // Pixel data
  stripData.copy(file, stripOffset)

  return file
}

module.exports = { writeGeoTIFF, latLngToUTM }
