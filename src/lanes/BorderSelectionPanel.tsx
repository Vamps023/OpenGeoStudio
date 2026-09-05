import { useStore } from '../state/store'
import type { LaneSectionDef } from '../engine/laneTypes'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { toast } from 'sonner'

interface BorderSelectionPanelProps {
  section: LaneSectionDef
  side: 'left' | 'right'
  edge: 'inner' | 'outer'
  roadId: string
}

const TIP_SOFT = 'Smooth shoulder (≈ 5 cm)'
const TIP_HARD = 'Hard shoulder (≈ 10 cm)'
const TIP_VERTICAL = 'Vertical shoulder (unsafe — no horizontal offset)'

/**
 * Per-border editor (SCANeR "Border offsets" feature).
 * Adjusts the height of the inner/outer border on the innermost / outermost
 * lane of the side. The border's height is what creates the haptic feedback
 * when the driver leaves the road.
 */
export default function BorderSelectionPanel({ section, side, edge, roadId }: BorderSelectionPanelProps) {
  const setLaneBorder = useStore((s) => s.setLaneBorder)

  // Inner border of `side` lives on the lane at index 0 (closest to center).
  // Outer border of `side` lives on the lane at the last index.
  const idx = edge === 'inner' ? 0 : section[side].length - 1
  const lane = section[side][idx]
  if (!lane) return null

  const heightKey = side === 'left'
    ? (edge === 'inner' ? 'borderRightHeight' : 'borderLeftHeight')
    : (edge === 'inner' ? 'borderLeftHeight'  : 'borderRightHeight')
  const offsetKey = side === 'left'
    ? (edge === 'inner' ? 'borderRightOffset' : 'borderLeftOffset')
    : (edge === 'inner' ? 'borderLeftOffset'  : 'borderRightOffset')

  const height = (lane as unknown as Record<string, number | undefined>)[heightKey] ?? 0
  const offset = (lane as unknown as Record<string, number | undefined>)[offsetKey] ?? 0

  function applyHeight(value: number) {
    setLaneBorder(roadId, side, idx, edge, value)
    toast.success(`${LANE_TYPE_META[lane.type].label} ${side} ${edge} border = ${value.toFixed(2)} m`)
  }
  function applyOffset(value: number) {
    setLaneBorder(roadId, side, idx, edge, height, value)
  }

  return (
    <div className="space-y-2 rounded border border-border bg-card/40 p-3">
      <div className="flex items-center justify-between">
        <div className="text-xs font-semibold text-foreground">
          Border · {side} · {edge}
        </div>
        <Badge variant="outline" className="text-[10px]">{LANE_TYPE_META[lane.type].label} #{idx}</Badge>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-muted-foreground">Height (m)</Label>
          <Input
            type="number" min={0} max={1} step={0.01}
            className="h-7 text-xs"
            value={height}
            onChange={(e) => applyHeight(Math.max(0, Math.min(1, Number(e.target.value))))}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-muted-foreground">Offset (m)</Label>
          <Input
            type="number" min={0} max={2} step={0.01}
            className="h-7 text-xs"
            value={offset}
            onChange={(e) => applyOffset(Math.max(0, Math.min(2, Number(e.target.value))))}
          />
        </div>
      </div>
      <div className="flex flex-wrap gap-1.5 text-[10px]">
        <button
          type="button"
          onClick={() => applyHeight(0.05)}
          className="rounded border border-border bg-muted px-2 py-0.5 hover:bg-accent"
          title={TIP_SOFT}
        >Soft 5cm</button>
        <button
          type="button"
          onClick={() => applyHeight(0.1)}
          className="rounded border border-border bg-muted px-2 py-0.5 hover:bg-accent"
          title={TIP_HARD}
        >Hard 10cm</button>
        <button
          type="button"
          onClick={() => applyHeight(0.0)}
          className="rounded border border-border bg-muted px-2 py-0.5 hover:bg-accent"
          title={TIP_VERTICAL}
        >Vertical 0cm (unsafe)</button>
      </div>
      <p className="text-[10px] text-muted-foreground">
        Border heights and offsets are off-road feedback settings for simulation/export.
        This editor does not simulate steering-wheel feedback. Use the presets for quick setup.
      </p>
    </div>
  )
}
