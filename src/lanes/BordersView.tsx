import type { LaneSectionDef } from '../engine/laneTypes'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'

interface BordersViewProps {
  section: LaneSectionDef
  selected: { side: 'left' | 'right'; index: number } | null
  onSetHeight: (side: 'left' | 'right', index: number, edge: 'inner' | 'outer', height: number) => void
}

export default function BordersView({ section, selected, onSetHeight }: BordersViewProps) {
  if (!selected) {
    return (
      <p className="text-xs text-muted-foreground py-4 text-center">
        Select a lane in the Cross Profile tab to edit its borders.
      </p>
    )
  }
  const lane = section[selected.side][selected.index]
  return (
    <div className="space-y-2">
      <div className="text-xs font-medium">
        {LANE_TYPE_META[lane.type].label} lane · {selected.side} #{selected.index}
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[10px]">Inner Border Height (m)</Label>
          <Input
            type="number"
            step="0.01"
            min="0"
            max="2"
            value={lane.borderRightHeight ?? 0}
            onChange={(e) => onSetHeight(selected.side, selected.index, 'inner', Number(e.target.value))}
            className="h-7 text-xs"
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[10px]">Outer Border Height (m)</Label>
          <Input
            type="number"
            step="0.01"
            min="0"
            max="2"
            value={lane.borderLeftHeight ?? 0}
            onChange={(e) => onSetHeight(selected.side, selected.index, 'outer', Number(e.target.value))}
            className="h-7 text-xs"
          />
        </div>
      </div>
      <p className="text-[10px] text-muted-foreground">
        Tip: 0 = flat, 0.05 = smooth shoulder, 0.15 = curb, 0.30 = hard shoulder.
        Use the <strong>Lane editing</strong> tool to add shoulders, sidewalks or curbs.
      </p>
    </div>
  )
}
