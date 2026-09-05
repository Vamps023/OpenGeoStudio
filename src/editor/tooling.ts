import {
  AlertTriangle,
  Circle,
  Footprints,
  GitBranch,
  GitMerge,
  Layers,
  MousePointer2,
  Move,
  MoveDiagonal,
  PenLine,
  Plus,
  Scissors,
  Slash,
  Spline,
  Trash2,
  TrendingUp,
  Waves,
  Waypoints,
  X,
  CornerDownLeft,
  CornerDownRight,
} from 'lucide-react'
import { FUNCTION_COLORS } from '../engine/xyFunctions'
import type { Vec2 } from '../engine/types'
import type { Tool } from '../state/store'
import type { buildJunctionNetwork } from '../engine/junctions'

export type AutoJunction = ReturnType<typeof buildJunctionNetwork>['junctions'][number]

/** Nearest detected auto junction within 15 m, for the Junction toggle tool. */
export function nearestJunction(junctions: AutoJunction[], point: Vec2): AutoJunction | null {
  let best: AutoJunction | null = null
  let bestDistance = Number.POSITIVE_INFINITY
  for (const junction of junctions) {
    const nextDistance = distance(junction.position, point)
    if (nextDistance < bestDistance) {
      best = junction
      bestDistance = nextDistance
    }
  }
  return bestDistance <= 15 ? best : null
}

export interface ToolItem {
  tool: Tool
  label: string
  icon: typeof MousePointer2
  color?: string
}

export const TOOL_ITEMS: ToolItem[] = [
  { tool: 'select', label: 'Select', icon: MousePointer2 },
  { tool: 'draw-straight', label: 'Insert Segment', icon: Slash, color: FUNCTION_COLORS.segment },
  { tool: 'draw-arc', label: 'Insert Circle Arc', icon: Circle, color: FUNCTION_COLORS.arc },
  { tool: 'draw-clothoid', label: 'Insert Clothoid Arc', icon: TrendingUp, color: FUNCTION_COLORS.clothoid },
  { tool: 'draw-polyline', label: 'Insert Polyline', icon: PenLine, color: FUNCTION_COLORS.polyline },
  { tool: 'draw-bezier', label: 'Insert Bezier', icon: Spline, color: FUNCTION_COLORS.bezier },
  { tool: 'draw-spline', label: 'Insert ClothoidSpline', icon: Waves, color: FUNCTION_COLORS.clothoidSpline },
  { tool: 'move', label: 'Move End', icon: Move },
  { tool: 'extend', label: 'Extend', icon: MoveDiagonal },
  { tool: 'split', label: 'Split', icon: Scissors },
  { tool: 'delete', label: 'Delete', icon: Trash2 },
  { tool: 'insert-intersection', label: 'Insert Intersection', icon: Waypoints, color: '#a3e635' },
  { tool: 'junction', label: 'Junction', icon: GitMerge },
  { tool: 'lane-begin', label: 'Begin Lane', icon: CornerDownLeft },
  { tool: 'lane-end', label: 'End Lane', icon: CornerDownRight },
  { tool: 'lane-insert', label: 'Insert Lane', icon: Plus },
  { tool: 'lane-remove', label: 'Remove Lane', icon: Trash2 },
  { tool: 'lane-border', label: 'Edit Border', icon: Layers },
  { tool: 'lane-sidewalk', label: 'Add Sidewalk', icon: Footprints },
  { tool: 'rail-point', label: 'Insert Point (Turnout)', icon: GitBranch, color: '#60a5fa' },
  { tool: 'rail-crossing', label: 'Insert Frog / Diamond', icon: X, color: '#f59e0b' },
  { tool: 'catch-point', label: 'Catch Point', icon: AlertTriangle, color: '#a78bfa' },
]

/** Tools for the Road workspace: road design + network. */
export const ROAD_TOOL_GROUPS: { label: string; tools: Tool[] }[] = [
  { label: 'Select', tools: ['select'] },
  { label: 'Insert curves', tools: ['draw-straight', 'draw-arc', 'draw-clothoid', 'draw-polyline', 'draw-bezier', 'draw-spline'] },
  { label: 'Modify', tools: ['move', 'extend', 'split', 'delete'] },
  { label: 'Network', tools: ['insert-intersection', 'junction'] },
]

/** Tools for the Lane workspace: lane editing on the selected road. */
export const LANE_TOOL_GROUPS: { label: string; tools: Tool[] }[] = [
  { label: 'Select', tools: ['select'] },
  { label: 'Lanes', tools: ['lane-begin', 'lane-end', 'lane-insert', 'lane-remove', 'lane-border', 'lane-sidewalk'] },
]

/** Tools for the Train workspace: railway track design (Straight / Circle / Spiral). */
export const TRAIN_TOOL_GROUPS: { label: string; tools: Tool[] }[] = [
  { label: 'Select', tools: ['select'] },
  { label: 'Insert track', tools: ['draw-straight', 'draw-arc', 'draw-clothoid', 'draw-polyline', 'draw-spline'] },
  { label: 'Modify', tools: ['move', 'extend', 'split', 'delete'] },
  { label: 'Fixtures', tools: ['rail-point', 'rail-crossing', 'catch-point'] },
]

export function toolHint(tool: Tool): string {
  return {
    select: 'Click a track or intersection to select it. Ctrl+click for multi-select. Right-click for actions. Double-click a way to lock its passageway.',
    'draw-straight': 'Insert Segment: hold left mouse, drag, and release. Start near a track extremity to attach automatically.',
    'draw-arc': 'Insert Circle Arc: click start, a through point, then the endpoint.',
    'draw-clothoid': 'Insert Clothoid Arc: drag from start to end. Set radius out and turn direction in the TOOL panel (radius continuity is kept when attaching).',
    'draw-polyline': 'Insert Polyline: click each vertex; Enter or Finish to complete.',
    'draw-bezier': 'Insert Bezier: click the start, then the end — control points keep tangent continuity.',
    'draw-spline': 'Insert ClothoidSpline: click control points; Enter or Finish to complete.',
    move: 'Move End: drag an existing track endpoint to a new position.',
    extend: 'Extend: drag from either endpoint of an existing track. Edition constrain (Free / Fixed Radius / Fixed Length) applies to arcs.',
    split: 'Split: click a track to cut it where you clicked.',
    delete: 'Delete: click a track or intersection to delete it.',
    'insert-intersection': 'Insert Intersection: click to place a yellow node, then select tracks and press Ctrl+L to link.',
    junction: 'Click a detected junction to detach or recreate it.',
    'lane-begin': 'Begin Lane: click a lane — a gizmo appears; click an arrow to start the new lane toward that direction (express lane over speed × 2 s).',
    'lane-end': 'End Lane: click a lane — a gizmo appears; click an arrow to end the lane toward that direction.',
    'lane-insert': 'Click on a lane to select it. The next click inserts a new lane next to it.',
    'lane-remove': 'Click on a lane to remove it.',
    'lane-border': 'Click a road to select it, then drag a border to adjust its height (off-road feedback).',
    'lane-sidewalk': 'Click a road to add a sidewalk and curb on the selected side.',
    'rail-point': 'Insert Point (Turnout): click the facing track end where two tracks diverge — places a tapered switch blade on the branch.',
    'rail-crossing': 'Insert Frog / Diamond: click between two crossing tracks — wing rails and guard rails are generated at the crossing.',
    'catch-point': 'Catch Point: click a track extremity to add a derail blade with stop block (side is picked from where you click).',
  }[tool]
}

export function clampInt(value: string, min: number, max: number, fallback: number): number {
  const parsed = Number.parseInt(value, 10)
  return Number.isNaN(parsed) ? fallback : Math.max(min, Math.min(max, parsed))
}

export function clampNumber(value: string, min: number, max: number, fallback: number): number {
  const parsed = Number.parseFloat(value)
  return Number.isNaN(parsed) ? fallback : Math.max(min, Math.min(max, parsed))
}

export function distance(a: Vec2, b: Vec2): number {
  return Math.hypot(a.x - b.x, a.y - b.y)
}
