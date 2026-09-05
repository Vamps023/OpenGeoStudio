// ─────────────────────────────────────────────────────────────────────
// GLTF 2.0 / GLB export pipeline
//
// Converts canonical project/domain data into GLTF/GLB format. This is
// the first-class runtime format for 3D Studio (#57) and the basis for
// FBX export (#58).
//
// The exporter builds the same meshes that 3D Studio renders (road
// surfaces, lane markings, junction surfaces, rail tracks, fixtures,
// terrain) but packages them into a self-contained GLB binary file with
// stable object names and geographic metadata.
//
// No React or Three.js imports. Pure data → binary conversion.
//
// GLTF 2.0 spec: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
// ─────────────────────────────────────────────────────────────────────

import { buildRoadMeshRange, buildConnectingRoadMesh, buildRailwayMesh } from './mesh'
import type { MeshData } from './mesh'
import { buildRailFixtureObjects } from './railFixtures'
import { buildJunctionNetwork, buildJunctionSurface, buildJunctionMarkings, visibleRoadRanges } from './junctions'
import { allWays, resolveTracks } from './intersections'
import { buildRoadSamplers, getLaneSection, ROAD_LIFT } from './roadServices'
import { buildTerrainMeshWorld } from './terrainMesh'
import type { TerrainData } from './terrainMesh'
import { stickTrackToTerrain } from './tracks'
import { sectionHalfWidth } from './laneLayout'
import { makeTerrainSampler } from '../terrain/terrainRegistry'
import type { Project } from '../domain/project'
import type { RoadData } from '../domain/road'

// ─── Export object model ────────────────────────────────────────────

/** One named mesh in the export, tagged with its semantic surface type. */
export interface ExportMesh {
  /** Stable object name (e.g. "Road:Main Street", "Junction:JX_3"). */
  name: string
  /** Stable object ID (matches the project domain ID). */
  id: string
  /** Semantic category — drives material assignment in the viewer. */
  category: ExportCategory
  /** The mesh data (positions, colors, indices, optional UVs). */
  mesh: MeshData
}

export type ExportCategory = 'road' | 'marking' | 'junction' | 'intersection' | 'rail' | 'terrain' | 'building'

/** Result of building all export meshes from a project. */
export interface ExportScene {
  meshes: ExportMesh[]
  /** Geographic origin metadata (WGS84 + scale). */
  origin?: { lng: number; lat: number; scale: number }
  /** Schema version of the export pipeline. */
  version: number
}

// ─── Scene builder ──────────────────────────────────────────────────

/** Options controlling which categories are included in the export. */
export interface ExportOptions {
  /** Drape roads without elevation profiles onto the terrain. */
  drape?: boolean
  /** Include background terrain mesh. */
  includeTerrain?: boolean
  /** Terrain data to use (if includeTerrain is true). */
  terrain?: TerrainData | null
}

/**
 * Build all export meshes from a canonical project.
 *
 * This is the same mesh-building logic that 3D Studio uses, but
 * consolidated into a single function that returns named, categorized
 * meshes ready for GLTF encoding.
 */
export function buildExportScene(project: Project, options: ExportOptions = {}): ExportScene {
  const meshes: ExportMesh[] = []
  const drape = options.drape ?? true

  if (project.roads.length === 0) {
    return { meshes, origin: project.geoRef, version: 1 }
  }

  // Drape roads without elevation onto terrain (same as 3D Studio)
  const sampler = makeTerrainSampler(project.geoRef)
  const effective = project.roads.map((road) => {
    if (!drape || road.elevationProfile?.length) return road
    const result = stickTrackToTerrain({ ...road, elevationProfile: undefined }, sampler, sectionHalfWidth(getLaneSection(road)))
    if (!result) return road
    return { ...road, elevationProfile: result.elevation, bankingProfile: result.banking }
  })

  const samplers = buildRoadSamplers(effective, true)
  const junctionNetwork = buildJunctionNetwork(effective, project.suppressedJunctions, samplers.elevation)
  if (!junctionNetwork) return { meshes, origin: project.geoRef, version: 1 }

  // ── Road surfaces + markings ──
  for (const road of effective) {
    const path = junctionNetwork.paths.get(road.id)
    if (!path) continue
    if (road.railway) {
      const mesh = buildRailwayMesh(path, road.railway, samplers.elevation.get(road.id), samplers.banking.get(road.id))
      if (mesh) meshes.push({ id: road.id, name: `Rail:${road.name}`, category: 'rail', mesh })
      continue
    }
    const section = getLaneSection(road)
    const cuts = junctionNetwork.cuts.filter((cut: { roadId: string; sStart: number; sEnd: number }) => cut.roadId === road.id)
    for (const range of visibleRoadRanges(path, cuts)) {
      const result = buildRoadMeshRange(path, section, range.sStart, range.sEnd, 1, samplers.elevation.get(road.id), samplers.banking.get(road.id), road.tapers)
      if (result.pavement) meshes.push({ id: road.id, name: `Road:${road.name}`, category: 'road', mesh: result.pavement })
      if (result.markings) meshes.push({ id: `${road.id}:markings`, name: `Markings:${road.name}`, category: 'marking', mesh: result.markings })
    }
  }

  // ── Junction surfaces + markings ──
  for (const junction of junctionNetwork.junctions.filter((j: { suppressed: boolean }) => !j.suppressed)) {
    const surface = buildJunctionSurface(junction, samplers.elevation)
    if (surface.mesh) meshes.push({ id: `jx:${junction.id}`, name: `Junction:${junction.id}`, category: 'junction', mesh: surface.mesh })
    const markings = buildJunctionMarkings(junction)
    if (markings) meshes.push({ id: `jx:${junction.id}:markings`, name: `JunctionMarkings:${junction.id}`, category: 'marking', mesh: markings })
  }

  // ── Explicit intersection ways ──
  const resolved = resolveTracks(effective)
  for (const intersection of project.intersections ?? []) {
    if (intersection.trackEnds.length < 2) continue
    for (const way of allWays(intersection, resolved)) {
      const result = buildConnectingRoadMesh(way.samples, Math.max(1, way.laneCount), way.laneWidth)
      if (result.pavement) meshes.push({ id: `ix:${intersection.id}`, name: `Intersection:${intersection.groundName || intersection.id}`, category: 'intersection', mesh: result.pavement })
      if (result.markings) meshes.push({ id: `ix:${intersection.id}:markings`, name: `IntersectionMarkings:${intersection.groundName || intersection.id}`, category: 'marking', mesh: result.markings })
    }
  }

  // ── Rail fixtures ──
  for (const fixture of buildRailFixtureObjects(project)) {
    for (const mesh of fixture.meshes) {
      meshes.push({ id: fixture.id, name: `Fixture:${fixture.name}`, category: 'rail', mesh })
    }
  }

  // ── Terrain (optional) ──
  if (options.includeTerrain && options.terrain && project.geoRef) {
    const terrainMesh = buildTerrainMeshWorld(options.terrain, project.geoRef)
    if (terrainMesh) {
      meshes.push({ id: 'terrain', name: 'Terrain', category: 'terrain', mesh: { positions: terrainMesh.positions, colors: terrainMesh.colors, indices: terrainMesh.indices } })
    }
  }

  return { meshes, origin: project.geoRef, version: 1 }
}

// ─── GLB binary encoder ─────────────────────────────────────────────

/** GLB chunk types. */
const CHUNK_JSON = 0x4e4f534a // 'JSON'
const CHUNK_BIN = 0x004e4942 // 'BIN\0'

/** 4-byte alignment padding. */
function padTo4(n: number): number {
  return (n + 3) & ~3
}

/** Pad a buffer to 4-byte alignment with zeros. */
function padBuffer(buf: Uint8Array): Uint8Array {
  const padded = padTo4(buf.length)
  if (padded === buf.length) return buf
  const out = new Uint8Array(padded)
  out.set(buf)
  return out
}

/**
 * Encode an ExportScene into a GLB 2.0 binary file.
 *
 * The GLB format is:
 *   12-byte header (magic, version, length)
 *   JSON chunk (length + type + JSON)
 *   BIN chunk (length + type + binary data)
 *
 * Each mesh becomes a GLTF node with its own accessor and buffer view.
 * Vertex colors (RGB) are encoded as VEC3 float attributes.
 * Materials are per-category (pavement, marking, rail, terrain).
 */
export function encodeGLB(scene: ExportScene): ArrayBuffer {
  // ── Build binary buffer ──
  // Layout per mesh: [positions (VEC3 float)] [colors (VEC3 float)] [indices (uint32)]
  const binaryParts: { data: Uint8Array; type: 'pos' | 'col' | 'idx' }[] = []
  const meshDescriptors: {
    posOffset: number; posLength: number
    colOffset: number; colLength: number
    idxOffset: number; idxLength: number
    vertexCount: number; indexCount: number
    name: string; category: ExportCategory
  }[] = []

  let offset = 0
  for (const mesh of scene.meshes) {
    const posBytes = new Uint8Array(mesh.mesh.positions.buffer, mesh.mesh.positions.byteOffset, mesh.mesh.positions.byteLength)
    const colBytes = new Uint8Array(mesh.mesh.colors.buffer, mesh.mesh.colors.byteOffset, mesh.mesh.colors.byteLength)
    const idxBytes = new Uint8Array(mesh.mesh.indices.buffer, mesh.mesh.indices.byteOffset, mesh.mesh.indices.byteLength)

    const posPadded = padBuffer(posBytes)
    const colPadded = padBuffer(colBytes)
    const idxPadded = padBuffer(idxBytes)

    const posOffset = offset
    const posLength = posPadded.length
    offset += posLength
    const colOffset = offset
    const colLength = colPadded.length
    offset += colLength
    const idxOffset = offset
    const idxLength = idxPadded.length
    offset += idxLength

    binaryParts.push({ data: posPadded, type: 'pos' })
    binaryParts.push({ data: colPadded, type: 'col' })
    binaryParts.push({ data: idxPadded, type: 'idx' })

    meshDescriptors.push({
      posOffset, posLength,
      colOffset, colLength,
      idxOffset, idxLength,
      vertexCount: mesh.mesh.positions.length / 3,
      indexCount: mesh.mesh.indices.length,
      name: mesh.name,
      category: mesh.category,
    })
  }

  // Concatenate binary buffer
  const binLength = offset
  const binBuffer = new Uint8Array(binLength)
  let binOffset = 0
  for (const part of binaryParts) {
    binBuffer.set(part.data, binOffset)
    binOffset += part.data.length
  }

  // ── Build GLTF JSON ──
  const materials = buildMaterials()
  const bufferViews: object[] = []
  const accessors: object[] = []
  const meshesJson: object[] = []
  const nodes: object[] = []

  for (const desc of meshDescriptors) {
    // Position buffer view
    const posBV = bufferViews.length
    bufferViews.push({ buffer: 0, byteOffset: desc.posOffset, byteLength: desc.posLength, target: 34962 })

    // Color buffer view
    const colBV = bufferViews.length
    bufferViews.push({ buffer: 0, byteOffset: desc.colOffset, byteLength: desc.colLength, target: 34962 })

    // Index buffer view
    const idxBV = bufferViews.length
    bufferViews.push({ buffer: 0, byteOffset: desc.idxOffset, byteLength: desc.idxLength, target: 34963 })

    // Position accessor
    const posAcc = accessors.length
    accessors.push({ bufferView: posBV, componentType: 5126, count: desc.vertexCount, type: 'VEC3', max: [0, 0, 0], min: [0, 0, 0] })

    // Color accessor
    const colAcc = accessors.length
    accessors.push({ bufferView: colBV, componentType: 5126, count: desc.vertexCount, type: 'VEC3' })

    // Index accessor
    const idxAcc = accessors.length
    accessors.push({ bufferView: idxBV, componentType: 5125, count: desc.indexCount, type: 'SCALAR' })

    // Mesh
    const meshIdx = meshesJson.length
    const matIdx = materialIndexForCategory(desc.category, materials)
    meshesJson.push({
      primitives: [{
        attributes: { POSITION: posAcc, COLOR_0: colAcc },
        indices: idxAcc,
        material: matIdx,
      }],
    })

    // Node
    nodes.push({ mesh: meshIdx, name: desc.name })
  }

  const scene0 = { nodes: nodes.map((_, i) => i) }

  const gltfJson = {
    asset: { version: '2.0', generator: 'OpenGeoStudio GLB Exporter' },
    scene: 0,
    scenes: [scene0],
    nodes,
    meshes: meshesJson,
    materials,
    accessors,
    bufferViews,
    buffers: [{ byteLength: binLength }],
    extensions: scene.origin ? {
      // Geographic origin as a custom extension
      OGS_origin: {
        lng: scene.origin.lng,
        lat: scene.origin.lat,
        scale: scene.origin.scale,
      },
    } : undefined,
    extensionsUsed: scene.origin ? ['OGS_origin'] : undefined,
  }

  // ── Encode GLB ──
  const jsonStr = JSON.stringify(gltfJson)
  const jsonBytes = new TextEncoder().encode(jsonStr)
  const jsonPadded = padBuffer(jsonBytes)
  const binPadded = padBuffer(binBuffer)

  const headerLength = 12
  const jsonChunkLength = 8 + jsonPadded.length
  const binChunkLength = 8 + binPadded.length
  const totalLength = headerLength + jsonChunkLength + binChunkLength

  const glb = new ArrayBuffer(totalLength)
  const view = new DataView(glb)
  const u8 = new Uint8Array(glb)

  // Header
  view.setUint32(0, 0x46546c67, true) // 'glTF' magic
  view.setUint32(4, 2, true) // version 2.0
  view.setUint32(8, totalLength, true)

  // JSON chunk
  let pos = 12
  view.setUint32(pos, jsonPadded.length, true); pos += 4
  view.setUint32(pos, CHUNK_JSON, true); pos += 4
  u8.set(jsonPadded, pos); pos += jsonPadded.length

  // BIN chunk
  view.setUint32(pos, binPadded.length, true); pos += 4
  view.setUint32(pos, CHUNK_BIN, true); pos += 4
  u8.set(binPadded, pos)

  return glb
}

// ─── Materials ──────────────────────────────────────────────────────

interface GltfMaterial {
  name: string
  pbrMetallicRoughness: {
    baseColorFactor: [number, number, number, number]
    metallicFactor: number
    roughnessFactor: number
  }
  doubleSided?: boolean
}

function buildMaterials(): GltfMaterial[] {
  return [
    { name: 'Asphalt', pbrMetallicRoughness: { baseColorFactor: [0.18, 0.20, 0.23, 1], metallicFactor: 0, roughnessFactor: 0.9 }, doubleSided: true },
    { name: 'Marking', pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], metallicFactor: 0, roughnessFactor: 0.6 }, doubleSided: true },
    { name: 'Rail', pbrMetallicRoughness: { baseColorFactor: [0.5, 0.52, 0.55, 1], metallicFactor: 0.8, roughnessFactor: 0.3 } },
    { name: 'Terrain', pbrMetallicRoughness: { baseColorFactor: [0.4, 0.5, 0.3, 1], metallicFactor: 0, roughnessFactor: 1.0 }, doubleSided: true },
    { name: 'Building', pbrMetallicRoughness: { baseColorFactor: [0.7, 0.7, 0.7, 1], metallicFactor: 0, roughnessFactor: 0.8 } },
  ]
}

function materialIndexForCategory(category: ExportCategory, _materials: GltfMaterial[]): number {
  switch (category) {
    case 'road':
    case 'junction':
    case 'intersection':
      return 0 // Asphalt
    case 'marking':
      return 1 // Marking
    case 'rail':
      return 2 // Rail
    case 'terrain':
      return 3 // Terrain
    case 'building':
      return 4 // Building
    default:
      return 0
  }
}

// ─── Convenience: build + encode in one call ────────────────────────

/**
 * Build the export scene from a project and encode it as a GLB file.
 * This is the main entry point for the export pipeline.
 */
export function exportProjectToGLB(project: Project, options: ExportOptions = {}): ArrayBuffer {
  const scene = buildExportScene(project, options)
  return encodeGLB(scene)
}
