// ─────────────────────────────────────────────────────────────────────
// Shared road/engine business services
//
// Consolidates the road geometry, elevation, banking, and junction
// network construction logic that was previously duplicated between
// EditorPage.tsx and Studio3DPage.tsx. Both pages (and the future export
// pipeline) now call these shared services instead of rebuilding the
// same sampler maps and junction networks independently.
//
// No React or Three.js imports.
// ─────────────────────────────────────────────────────────────────────

import { fitRoadGeometry } from './roadGeometry'
import { normalizeElevationProfile, evaluateElevation } from './elevation'
import type { ElevationPoint } from './elevation'
import { buildJunctionNetwork } from './junctions'
import type { JunctionNetwork, ElevationSampler } from './junctions'
import { makeDefaultSection, totalWidth } from './laneLayout'
import type { LaneSectionDef } from './laneTypes'
import type { RoadData } from '../domain/road'
import type { Project } from '../domain/project'

/** Lift roads above the terrain surface to avoid z-fighting (the decimated
 *  terrain grid interpolates up to ~0.5 m off the true surface). */
export const ROAD_LIFT = 1.0

/**
 * Returns the effective lane section for a road.
 * If the road has a rich `laneSection` it is used as-is.
 * Otherwise a default travel-lane section is built from the legacy counts.
 *
 * Moved here from state/store.ts so UI-free code (export pipeline, tests)
 * can resolve lane sections without importing the Zustand store.
 */
export function getLaneSection(road: RoadData): LaneSectionDef {
  if (road.laneSection) return road.laneSection
  return makeDefaultSection(road.lanesLeft, road.lanesRight, road.laneWidth)
}

/** Sum of all lane widths on both sides. */
export function getRoadTotalWidth(road: RoadData): number {
  return totalWidth(getLaneSection(road))
}

/** Total lane count on both sides. */
export function getRoadTotalLanes(road: RoadData): number {
  const section = getLaneSection(road)
  return section.left.length + section.right.length
}

// ─── Elevation / banking samplers ───────────────────────────────────

export interface RoadSamplers {
  elevation: Map<string, ElevationSampler>
  banking: Map<string, ElevationSampler>
}

/**
 * Build elevation and banking sampler maps for all roads in a project.
 *
 * Each sampler is a function `(station: number) => number` that returns
 * the elevation (or banking angle) at the given station along the road.
 *
 * @param roads      the roads to build samplers for
 * @param withLift   if true, adds ROAD_LIFT to elevation (for 3D Studio
 *                   rendering). Editor preview uses false.
 * @returns          { elevation, banking } sampler maps keyed by road ID
 */
export function buildRoadSamplers(roads: RoadData[], withLift = false): RoadSamplers {
  const elevation = new Map<string, ElevationSampler>()
  const banking = new Map<string, ElevationSampler>()
  for (const road of roads) {
    const path = fitRoadGeometry(road)
    const length = path?.length ?? 0
    const elevProfile = normalizeElevationProfile(road.elevationProfile, length)
    const bankProfile = normalizeElevationProfile(road.bankingProfile, length)
    const lift = withLift ? ROAD_LIFT : 0
    elevation.set(road.id, (s: number) => evaluateElevation(elevProfile, s) + lift)
    banking.set(road.id, (s: number) => evaluateElevation(bankProfile, s))
  }
  return { elevation, banking }
}

// ─── Junction network ───────────────────────────────────────────────

/**
 * Build the junction network for a project's roads, using the shared
 * elevation samplers.
 *
 * @param project  the project (uses roads + suppressedJunctions)
 * @param withLift if true, elevation samplers include ROAD_LIFT
 * @returns        the junction network (paths, cuts, junctions) or null
 *                 if the project has no roads
 */
export function buildProjectJunctionNetwork(
  project: Project,
  withLift = false,
): { network: JunctionNetwork; samplers: RoadSamplers } | null {
  if (project.roads.length === 0) return null
  const samplers = buildRoadSamplers(project.roads, withLift)
  const network = buildJunctionNetwork(
    project.roads,
    project.suppressedJunctions,
    samplers.elevation,
    project.junctionConfigurations,
  )
  return { network, samplers }
}

// ─── Validation ─────────────────────────────────────────────────────

/** Validation result for a single road. */
export interface RoadValidation {
  roadId: string
  valid: boolean
  errors: string[]
}

/** Validate a single road's data model.
 *  Checks for: non-empty points, valid lane counts, positive lane width,
 *  valid fillet radius, no NaN/Infinity in coordinates. */
export function validateRoad(road: RoadData): RoadValidation {
  const errors: string[] = []
  if (!road.id) errors.push('missing road ID')
  if (!road.name) errors.push('missing road name')
  if (!road.functions && (!road.points || road.points.length < 2))
    errors.push('road needs at least 2 control points or an XY function chain')
  if (road.lanesLeft < 0 || road.lanesRight < 0)
    errors.push(`invalid lane counts: left=${road.lanesLeft}, right=${road.lanesRight}`)
  if (road.laneWidth <= 0 || road.laneWidth > 100)
    errors.push(`invalid lane width: ${road.laneWidth}`)
  if (road.filletRadius < 0)
    errors.push(`invalid fillet radius: ${road.filletRadius}`)
  if (road.points) {
    for (let i = 0; i < road.points.length; i++) {
      const p = road.points[i]
      if (!Number.isFinite(p.x) || !Number.isFinite(p.y))
        errors.push(`point ${i} has NaN/Infinity coordinates`)
    }
  }
  if (road.elevationProfile) {
    for (let i = 0; i < road.elevationProfile.length; i++) {
      const p = road.elevationProfile[i]
      if (!Number.isFinite(p.s) || !Number.isFinite(p.z))
        errors.push(`elevation point ${i} has NaN/Infinity values`)
    }
  }
  return { roadId: road.id, valid: errors.length === 0, errors }
}

/** Validate all roads in a project. Returns all validation results. */
export function validateProjectRoads(project: Project): RoadValidation[] {
  return project.roads.map(validateRoad)
}

/** Check if a project has any invalid roads. */
export function projectHasInvalidRoads(project: Project): boolean {
  return validateProjectRoads(project).some((v) => !v.valid)
}
