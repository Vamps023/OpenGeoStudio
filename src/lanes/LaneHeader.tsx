import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import type { LaneType } from '../engine/laneTypes'
import { Trash2, ArrowUp, ArrowDown, MoveHorizontal } from 'lucide-react'

export default function LaneHeader({ side, index, type, siblingsCount, onInsert, onMove, onDelete }: {
  side: 'left' | 'right'; index: number; type: LaneType; siblingsCount: number
  onInsert: (w: 'before' | 'after') => void; onMove: (d: -1 | 1) => void; onDelete: () => void
}) {
  return (
    <div className="flex items-center justify-between">
      <div className="flex items-center gap-2">
        <Badge style={{ background: LANE_TYPE_META[type].color, color: '#fff' }} className="text-[10px]">
          {LANE_TYPE_META[type].label}
        </Badge>
        <span className="text-xs text-muted-foreground">{side} #{index}</span>
      </div>
      <div className="flex gap-1">
        <Button size="icon" variant="ghost" onClick={() => onInsert('before')} title="Insert Before">
          <MoveHorizontal className="w-3 h-3" />
        </Button>
        <Button size="icon" variant="ghost" onClick={() => onMove(-1)} disabled={index === 0} title="Move Outward">
          <ArrowUp className="w-3 h-3" />
        </Button>
        <Button size="icon" variant="ghost" onClick={() => onMove(1)} disabled={index === siblingsCount - 1} title="Move Inward">
          <ArrowDown className="w-3 h-3" />
        </Button>
        <Button size="icon" variant="ghost" onClick={onDelete} title="Delete">
          <Trash2 className="w-3 h-3" />
        </Button>
      </div>
    </div>
  )
}
