import type { LaneType, CirculationWay, VehicleCategory, LaneMarking } from '../engine/laneTypes'

export const LANE_TYPES: LaneType[] = [
  'travel', 'shoulder', 'hard_shoulder', 'soft_shoulder',
  'curb', 'sidewalk', 'bike', 'bus', 'median', 'parking',
  'embankment', 'ditch', 'barrier', 'land', 'paved_major',
  'lane_out', 'runway', 'taxiway',
]

export const CIRCULATION_WAYS: CirculationWay[] = ['forward', 'backward', 'both']

export const VEHICLE_CATEGORIES: VehicleCategory[] = [
  'car', 'truck', 'bus', 'bike', 'pedestrian', 'motorcycle', 'emergency', 'airplane',
]

export const MARKING_KINDS: LaneMarking[] = [
  'none', 'solid', 'dashed', 'double-solid', 'solid-dashed', 'dashed-solid',
]
