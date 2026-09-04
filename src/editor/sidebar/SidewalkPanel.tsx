import { Trash2 } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Separator } from '@/components/ui/separator'
import { defaultLaneByType } from '../../engine/laneLayout'
import { getLaneSection, useStore } from '../../state/store'
import type { RoadData } from '../../state/store'
import { clampNumber } from '../tooling'

/**
 * Sidewalk Builder (Lane Tools workspace): generate sidewalk + curb on each
 * side of the selected road, adjust sidewalk width, or remove them again.
 * Sidewalks are outer lanes of the road's lane section, so they persist,
 * regenerate with the road mesh, and wrap around junction connecting roads.
 */
export default function SidewalkPanel({ road }: { road: RoadData }) {
  const insertLaneAt = useStore((s) => s.insertLaneAt)
  const removeLaneAt = useStore((s) => s.removeLaneAt)
  const updateLaneAt = useStore((s) => s.updateLaneAt)

  const section = getLaneSection(road)

  function addSide(side: 'left' | 'right') {
    const len = getLaneSection(road)[side].length
    insertLaneAt(road.id, side, len, defaultLaneByType('curb', 0.3))
    // after the curb insert, the sidewalk goes at the new outer end
    insertLaneAt(road.id, side, len + 1, defaultLaneByType('sidewalk', 2))
  }

  function removeSide(side: 'left' | 'right') {
    const lanes = getLaneSection(road)[side]
    const last = lanes[lanes.length - 1]
    if (last?.type === 'sidewalk') removeLaneAt(road.id, side, lanes.length - 1)
    const remaining = getLaneSection(road)[side]
    const last2 = remaining[remaining.length - 1]
    if (last2?.type === 'curb') removeLaneAt(road.id, side, remaining.length - 1)
  }

  function hasSide(side: 'left' | 'right') {
    return section[side].some((l) => l.type === 'sidewalk')
  }

  const walkwayLanes = (['left', 'right'] as const).flatMap((side) =>
    section[side]
      .map((lane, index) => ({ side, index, lane }))
      .filter(({ lane }) => lane.type === 'sidewalk' || lane.type === 'curb'),
  )

  return (
    <div className="grid gap-3">
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Sidewalk Builder</h3>
      {(['left', 'right'] as const).map((side) => (
        <div key={side} className="flex items-center gap-2">
          <span className="w-12 text-xs font-medium capitalize">{side}</span>
          {hasSide(side) ? (
            <Button size="sm" variant="outline" className="h-7 flex-1 text-xs text-destructive" onClick={() => removeSide(side)}>
              <Trash2 className="size-3.5" />
              Remove sidewalk
            </Button>
          ) : (
            <Button size="sm" variant="secondary" className="h-7 flex-1 text-xs" onClick={() => addSide(side)}>
              Add sidewalk + curb
            </Button>
          )}
        </div>
      ))}

      <Separator />

      {walkwayLanes.length === 0 ? (
        <p className="text-xs text-muted-foreground">
          No sidewalks on this road yet. Add one per side above — it generates the curb and walkway strips along the whole alignment.
        </p>
      ) : (
        <div className="grid gap-2">
          <Label className="text-[11px] text-muted-foreground">Walkway widths (m)</Label>
          {walkwayLanes.map(({ side, index, lane }) => (
            <div key={`${side}-${index}`} className="grid grid-cols-[5.5rem_1fr] items-center gap-2">
              <span className="text-xs capitalize text-muted-foreground">
                {side} {lane.type} #{index}
              </span>
              <Input
                type="number"
                min={0.15}
                max={8}
                step={0.25}
                className="h-7 text-xs"
                value={lane.width}
                onChange={(event) =>
                  updateLaneAt(road.id, side, index, { width: clampNumber(event.target.value, 0.15, 8, 2) })
                }
              />
            </div>
          ))}
          <p className="text-[11px] leading-relaxed text-muted-foreground">
            Sidewalks are outer lanes of the road section: they follow every alignment edit, keep their width through
            junctions, and regenerate with the road mesh.
          </p>
        </div>
      )}
    </div>
  )
}
