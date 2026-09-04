import type { LaneType, CirculationWay, VehicleCategory, LaneMarking } from './laneTypes'

export const ALL_LANE_TYPES: LaneType[] = [
  'travel','paved_major','shoulder','hard_shoulder','soft_shoulder',
  'curb','sidewalk','bike','bus','median','parking','embankment',
  'ditch','barrier','land','lane_out','runway','taxiway',
]

export const ALL_CIRCULATIONS: CirculationWay[] = ['forward','backward','both']

export const ALL_MARKINGS: LaneMarking[] = [
  'none','solid','dashed','double-solid','solid-dashed','dashed-solid',
]

export const ALL_VEHICLES: VehicleCategory[] = [
  'car','truck','bus','bike','pedestrian','motorcycle','emergency','airplane',
]

export const LANE_TYPE_META: Record<LaneType, { label: string; color: string; defaultWidth: number; description: string }> = {
  travel:        { label: 'Travel',       color: '#3b4252', defaultWidth: 3.5, description: 'Standard drivable travel lane' },
  shoulder:      { label: 'Shoulder',     color: '#5e81ac', defaultWidth: 1.0, description: 'Emergency / parking shoulder' },
  curb:          { label: 'Curb',         color: '#8fbcbb', defaultWidth: 0.3, description: 'Raised curb / lane_curb' },
  sidewalk:      { label: 'Sidewalk',     color: '#a3be8c', defaultWidth: 1.5, description: 'Pedestrian sidewalk' },
  bike:          { label: 'Bike',         color: '#ebcb8b', defaultWidth: 1.5, description: 'Bicycle lane' },
  bus:           { label: 'Bus',          color: '#d08770', defaultWidth: 3.5, description: 'Bus-only lane' },
  median:        { label: 'Median',       color: '#b48ead', defaultWidth: 0.5, description: 'Central reservation / median' },
  parking:       { label: 'Parking',      color: '#bf616a', defaultWidth: 2.5, description: 'Parking lane' },
  hard_shoulder: { label: 'Hard Shoulder',color: '#4c566a', defaultWidth: 1.0, description: 'Hard (concrete) shoulder' },
  soft_shoulder: { label: 'Soft Shoulder',color: '#a3be8c', defaultWidth: 1.0, description: 'Soft (grass) shoulder' },
  embankment:    { label: 'Embankment',   color: '#d8dee9', defaultWidth: 1.0, description: 'Sloped embankment' },
  ditch:         { label: 'Ditch',        color: '#88c0d0', defaultWidth: 1.0, description: 'Drainage ditch' },
  barrier:       { label: 'Barrier',      color: '#2e3440', defaultWidth: 0.4, description: 'Physical barrier' },
  land:          { label: 'Land',         color: '#eceff4', defaultWidth: 1.0, description: 'Out-of-road land' },
  paved_major:   { label: 'Paved Major',  color: '#5e81ac', defaultWidth: 3.5, description: 'Paved major road' },
  lane_out:      { label: 'Lane Out',     color: '#d08770', defaultWidth: 0.3, description: 'Out-of-lane marker' },
  runway:        { label: 'Runway',       color: '#3b4252', defaultWidth: 30,  description: 'Airport runway' },
  taxiway:       { label: 'Taxiway',      color: '#434c5e', defaultWidth: 20,  description: 'Airport taxiway' },
}

export const CIRCULATION_META: Record<CirculationWay, { label: string; symbol: string }> = {
  forward:  { label: 'Forward',  symbol: '→' },
  backward: { label: 'Backward', symbol: '←' },
  both:     { label: 'Both',     symbol: '↔' },
}

export const VEHICLE_META: Record<VehicleCategory, { label: string; icon: string }> = {
  car:        { label: 'Car',        icon: '🚗' },
  truck:      { label: 'Truck',      icon: '🚚' },
  bus:        { label: 'Bus',        icon: '🚌' },
  bike:       { label: 'Bike',       icon: '🚴' },
  pedestrian: { label: 'Pedestrian', icon: '🚶' },
  motorcycle: { label: 'Motorcycle', icon: '🏍️' },
  emergency:  { label: 'Emergency',  icon: '🚑' },
  airplane:   { label: 'Airplane',   icon: '✈️' },
  all:        { label: 'All',        icon: '✓' },
}

export const MARKING_META: Record<LaneMarking, { label: string; pattern: 'solid' | 'dashed' | 'double' | 'mix' }> = {
  none:           { label: 'None',           pattern: 'solid' },
  solid:          { label: 'Solid',           pattern: 'solid' },
  dashed:         { label: 'Dashed',         pattern: 'dashed' },
  'double-solid': { label: 'Double Solid',   pattern: 'double' },
  'solid-dashed': { label: 'Solid - Dashed', pattern: 'mix' },
  'dashed-solid': { label: 'Dashed - Solid', pattern: 'mix' },
}

