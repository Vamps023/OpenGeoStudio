// ─────────────────────────────────────────────────────────────────────
// Shared preview mesh builder for the Editor
//
// Consolidates the road/junction/intersection/rail/terrain mesh-building
// logic that was previously inline in EditorPage.tsx. The Editor calls
// this single function to get all preview meshes for its 3D preview mode.
// 3D Studio has its own render pipeline (with unified junction surfaces,
// markings, asphalt PBR materials) — this is the lightweight Editor
// preview that uses the shared domain model.
//
// No React or Three.js imports.
// ─────────────────────────────────────────────────────────────────────

import { buildRoadMeshRange, buildConnectingRoadMesh, buildRailwayMesh } from './mesh'
import type { MeshData } from './mesh'
import { buildRailFixtureMeshes } from './railFixtures'
import { visibleRoadRanges } from './junctions'
import type { JunctionNetwork } from './junctions'
import { allWays, resolveTracks } from './intersections'
import { getLaneSection, type RoadSamplers } from './roadServices'
import { buildTerrainMeshWorld } from './terrainMesh'
import type { TerrainData } from './terrainMesh'
import type { Project } from '../domain/project'
import type { RoadData } from '../domain/road'

/** A road mesh tagged with its source road ID (for selection/highlighting). */
export interface RoadMeshEntry {
  roadId: string
  mesh: MeshData
}

/** All preview meshes the Editor needs for its 3D preview mode. */
export interface EditorPreviewMeshes {
  roadMeshes: RoadMeshEntry[]
  connectingMeshes: MeshData[]
  intersectionWayMeshes: MeshData[]
  railFixtureMeshes: MeshData[]
  terrainMesh: MeshData | null
}

/** Layer flags controlling which mesh categories are generated. */
export interface PreviewLayerFlags {
  road3dGeneration: boolean
  intersection3dGeneration: boolean
}

/**
 * Build all preview meshes for the Editor's 3D preview mode.
 *
 * This consolidates the mesh-building logic that was previously inline
 * in EditorPage.tsx. The Editor calls this single function instead of
 * building each mesh category with separate useMemo hooks.
 *
 * @param project        the active project
 * @param junctionNetwork  pre-built junction network (paths + cuts + junctions)
 * @param samplers       elevation + banking samplers (from buildRoadSamplers)
 * @param layers         layer flags controlling which categories to build
 * @param railSection    true if the Editor is in the Train (rail) section
 * @param terrain        active terrain data (for 3D terrain underlay), or null
 * @returns              all preview meshes
 */
export function buildEditorPreviewMeshes(
  project: Project,
  junctionNetwork: JunctionNetwork,
  samplers: RoadSamplers,
  layers: PreviewLayerFlags,
  railSection: boolean,
  terrain: TerrainData | null,
): EditorPreviewMeshes {
  // ── Road meshes ──
  const roadMeshes: RoadMeshEntry[] = []
  if (layers.road3dGeneration) {
    for (const road of project.roads) {
      const path = junctionNetwork.paths.get(road.id)
      if (!path) continue
      if (road.railway) {
        const mesh = buildRailwayMesh(path, road.railway, samplers.elevation.get(road.id), samplers.banking.get(road.id))
        if (mesh) roadMeshes.push({ roadId: road.id, mesh })
        continue
      }
      const laneSection = getLaneSection(road)
      const cuts = junctionNetwork.cuts.filter((cut) => cut.roadId === road.id)
      for (const range of visibleRoadRanges(path, cuts)) {
        const result = buildRoadMeshRange(path, laneSection, range.sStart, range.sEnd, 1, samplers.elevation.get(road.id), samplers.banking.get(road.id), road.tapers)
        if (result.pavement) roadMeshes.push({ roadId: road.id, mesh: result.pavement })
      }
    }
  }

  // ── Junction connecting meshes (lightweight — pavement only) ──
  const connectingMeshes: MeshData[] = []
  if (layers.intersection3dGeneration) {
    for (const junction of junctionNetwork.junctions.filter((j) => !j.suppressed)) {
      for (const connection of junction.connectingRoads) {
        const result = buildConnectingRoadMesh(connection.samples, connection.laneCount, connection.laneWidth)
        if (result.pavement) connectingMeshes.push(result.pavement)
      }
    }
  }

  // ── Explicit intersection way meshes ──
  const intersectionWayMeshes: MeshData[] = []
  if (layers.intersection3dGeneration) {
    const resolved = resolveTracks(project.roads)
    for (const intersection of project.intersections ?? []) {
      if (intersection.trackEnds.length < 2) continue
      for (const way of allWays(intersection, resolved)) {
        const result = buildConnectingRoadMesh(way.samples, Math.max(1, way.laneCount), way.laneWidth)
        if (result.pavement) intersectionWayMeshes.push(result.pavement)
      }
    }
  }

  // ── Rail fixture meshes (turnout blades, frogs, guard rails) ──
  const railFixtureMeshes: MeshData[] = railSection
    ? buildRailFixtureMeshes(project)
    : []

  // ── Terrain underlay ──
  let terrainMesh: MeshData | null = null
  if (terrain && project.geoRef) {
    const built = buildTerrainMeshWorld(terrain, project.geoRef)
    if (built) terrainMesh = { positions: built.positions, colors: built.colors, indices: built.indices }
  }

  return { roadMeshes, connectingMeshes, intersectionWayMeshes, railFixtureMeshes, terrainMesh }
}

/** Flatten all preview meshes into a single array for the RoadViewport. */
export function flattenPreviewMeshes(meshes: EditorPreviewMeshes): MeshData[] {
  return [
    ...meshes.roadMeshes.map((e) => e.mesh),
    ...meshes.connectingMeshes,
    ...meshes.intersectionWayMeshes,
    ...meshes.railFixtureMeshes,
    ...(meshes.terrainMesh ? [meshes.terrainMesh] : []),
  ]
}

/** Get the set of road IDs that have preview meshes (for highlighting). */
export function previewRoadIds(meshes: EditorPreviewMeshes): Set<string> {
  return new Set(meshes.roadMeshes.map((e) => e.roadId))
}
