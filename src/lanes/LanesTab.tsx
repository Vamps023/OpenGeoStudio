// ─────────────────────────────────────────────────────────────────────
// Lanes Tab (SCANeR-compatible dock widget)
//
// Mirrors the SCANeR Lanes tab: per-lane editing of type / width / speed
// limit / circulation way / vehicles authorization, plus border heights
// and offsets for the off-the-road feedback effect.
// ─────────────────────────────────────────────────────────────────────

import LanesPanel from './LanesPanel'
import LanesTabPanel from './LanesTabPanel'
import type { RoadData } from '../state/store'

export { laneKey, parseLaneKey, borderKey, parseBorderKey } from './laneKeys'

interface LanesTabProps {
  road: RoadData
}

/**
 * Backwards-compatible default export.
 * The LanesPanel contains the full editor (side blocks + center summary +
 * lane properties editor). The LanesTabPanel contains the cross-section
 * profile view + insertion menu.
 */
export default function LanesTab({ road }: LanesTabProps) {
  return <LanesPanel road={road} />
}

export { LanesTabPanel }
