// Backwards-compatible re-export.
// The new lane system lives in `laneTypes.ts`, `laneMetadata.ts`, and `laneLayout.ts`.

export type { LaneSectionDef, LaneDef } from './laneTypes'
export type { LaneStrip, LaneLayout } from './laneLayout'
export * from './laneLayout'
export * from './laneMetadata'
export * from './laneTypes'
