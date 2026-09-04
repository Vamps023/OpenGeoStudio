// ─────────────────────────────────────────────────────────────────────
// SCANeR-compatible Lane Type System
// Mirrors the ELaneType enum and lane definition from SCANeR studio
// ─────────────────────────────────────────────────────────────────────

export type LaneType =
  | 'travel'        // Standard drivable travel lane
  | 'shoulder'      // Soft/hard shoulder (emergency / parking)
  | 'curb'          // Curb / lane_curb - raised edge
  | 'sidewalk'      // Pedestrian sidewalk
  | 'bike'          // Bicycle lane
  | 'bus'           // Bus-only lane
  | 'median'        // Central reservation / median
  | 'parking'       // Parking lane
  | 'hard_shoulder' // Hard shoulder (concrete)
  | 'soft_shoulder' // Soft shoulder (grass/gravel)
  | 'embankment'    // Sloped edge
  | 'ditch'         // Drainage ditch
  | 'barrier'       // Physical barrier
  | 'land'          // Out-of-road land
  | 'paved_major'   // Paved major road
  | 'lane_out'      // Out-of-lane marker
  | 'runway'        // Airport runway
  | 'taxiway'       // Airport taxiway

export type CirculationWay = 'forward' | 'backward' | 'both'

export type VehicleCategory =
  | 'car'
  | 'truck'
  | 'bus'
  | 'bike'
  | 'pedestrian'
  | 'motorcycle'
  | 'emergency'
  | 'airplane'
  | 'all'

export type LaneMarking = 'none' | 'solid' | 'dashed' | 'double-solid' | 'solid-dashed' | 'dashed-solid'

// ─── Per-lane definition (SCANeR ELaneType parity) ─────────────────────
export interface LaneDef {
  id: string
  type: LaneType
  width: number                    // [m] 0..100
  speedLimit: number               // [km/h]
  circulation: CirculationWay      // forward / backward / both
  vehicles: VehicleCategory[]      // allowed vehicle types
  // Optional borders (heights for off-road feedback)
  borderLeftHeight?: number        // [m] raised left border
  borderRightHeight?: number       // [m] raised right border
  borderLeftOffset?: number        // [m] horizontal offset of left border
  borderRightOffset?: number       // [m] horizontal offset of right border
  // Marking visual
  marking?: LaneMarking
  // Marking behaviour + 3D representation (SCANeR marking editor)
  markingStyle?: MarkingStyle
  // SCANeR SELECTION fields
  groundName?: string              // Ground Name of the passageway
  materialName?: string            // Material Name of the passageway
  // Decoration (textures, sidewalks etc.)
  texture?: string                 // texture id or path
  // Optional name
  name?: string
}

export type MarkingBehaviour = 'cannot' | 'pullback' | 'cancross'

export interface MarkingStyle {
  crossLeft?: MarkingBehaviour     // behaviour for vehicles coming from the left
  crossRight?: MarkingBehaviour
  dissuasive?: boolean             // crossing is not recommended
  destinationSeparation?: boolean  // separates 2 lanes with different directions
  stopForbidden?: boolean
  parkingForbidden?: boolean
  alternate?: boolean              // alternate marking when 2 markings fit
  dotLength?: number               // [m] 3D dashed-line dot length
  totalLength?: number             // [m] 3D pattern total length
  width?: number                   // [m] 3D line width
}

export interface LaneSectionDef {
  // Lanes ordered from center outward on each side.
  // Left side: index 0 is innermost (next to center line)
  // Right side: index 0 is innermost (next to center line)
  left: LaneDef[]
  right: LaneDef[]
}
