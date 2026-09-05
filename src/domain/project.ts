// ─────────────────────────────────────────────────────────────────────
// Domain Model — Canonical Project
//
// The authoritative project definition consumed by Editor, 3D Studio,
// and the export pipeline. No React or Three.js imports.
//
// The existing `Project` in `state/store.ts` will re-export from here
// so all consumers reference the same canonical definition.
// ─────────────────────────────────────────────────────────────────────

import type { ObjectId } from './types'
import type { RoadData } from './road'
import type { RailPoint, RailCrossing, CatchPoint } from './rail'
import type { GeoReference, WorkingArea } from './terrain'
import type { IntersectionData } from './junction'
import type { OsmBuildingData } from '../engine/osmBuildings'
import type { PcgProjectConfig } from '../engine/pcgBuildings'
import type { StoredTerrain } from '../terrain/terrainCodec'
import type { JunctionConfiguration } from '../engine/junctions'

/** Canonical project definition — the authoritative project data model. */
export interface Project {
  id: ObjectId
  name: string
  createdAt: string
  roads: RoadData[]
  suppressedJunctions: string[]
  junctionConfigurations?: Record<string, JunctionConfiguration>
  /** Explicit intersections (SCANeR Roads tab). */
  intersections?: IntersectionData[]
  geoRef?: GeoReference
  /** Rail fixtures (Train section turnout pipeline). */
  railPoints?: RailPoint[]
  railCrossings?: RailCrossing[]
  catchPoints?: CatchPoint[]
  /** Persisted DEM so a reopened project keeps its terrain. */
  terrain?: StoredTerrain
  /** Persisted working area + tile grid size (Terrain workspace). */
  workingArea?: WorkingArea
  /** Persisted tile keys (`row:col` within the working-area grid). */
  selectedTiles?: string[]
  /** Imported OSM building footprints (world meters, project frame). */
  osmBuildings?: OsmBuildingData[]
  /** OSM import metadata (attribution + refresh semantics). */
  osmImport?: { area: { west: number; south: number; east: number; north: number }; fetchedAt: string; total: number }
  /** PCG building generation settings (style/seed/detail + overrides). */
  pcgConfig?: PcgProjectConfig
}

// ─── Serialization helpers ───────────────────────────────────────────

/** Schema version for the domain project format. Bumped when the
 *  serialized structure changes in a backwards-incompatible way. */
export const PROJECT_SCHEMA_VERSION = 1

/** Wrapper for serialized project data with schema version metadata. */
export interface SerializedProject {
  schemaVersion: number
  project: Project
}

/** Wrap a project for serialization with schema version metadata. */
export function serializeProject(project: Project): SerializedProject {
  return { schemaVersion: PROJECT_SCHEMA_VERSION, project }
}

/** Unwrap a serialized project, validating the schema version.
 *  Returns null if the schema version is unsupported. */
export function deserializeProject(data: unknown): Project | null {
  if (typeof data !== 'object' || data === null) return null
  const wrapper = data as Partial<SerializedProject>
  // Accept both wrapped ({ schemaVersion, project }) and raw project formats
  // for backwards compatibility with pre-domain saves.
  if (wrapper.schemaVersion !== undefined && wrapper.project !== undefined) {
    if (wrapper.schemaVersion > PROJECT_SCHEMA_VERSION) return null
    return wrapper.project
  }
  // Raw project (legacy format)
  const raw = data as Partial<Project>
  if (raw.id !== undefined && raw.roads !== undefined) return raw as Project
  return null
}
