import { ScrollArea } from '@/components/ui/scroll-area'
import { Button } from '@/components/ui/button'
import { ChevronUp, ChevronDown, Trash2 } from 'lucide-react'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import type { LaneType } from '../engine/laneTypes'
import { LANE_TYPES } from './laneConstants'

interface LaneGroupProps {
  title: string
  count: number
  side: 'left' | 'right'
  renderRow: (index: number) => React.ReactNode
  onInsert: (index: number, type: LaneType) => void
  onRemove: (index: number) => void
  onMoveUp: (index: number) => void
  onMoveDown: (index: number) => void
  selectedKey: string | null
}

export function LaneGroup({ title, count, side, renderRow, onInsert, onRemove, onMoveUp, onMoveDown, selectedKey }: LaneGroupProps) {
  return (
    <div>
      <div className="mb-1 flex items-center justify-between">
        <span className="text-xs font-semibold uppercase tracking-wide text-muted-foreground">
          {title} ({count})
        </span>
        <select
          aria-label="Insert lane"
          className="h-6 rounded border border-input bg-background px-1 text-[10px]"
          defaultValue=""
          onChange={(e) => {
            if (!e.target.value) return
            onInsert(count, e.target.value as LaneType)
            e.currentTarget.value = ''
          }}
        >
          <option value="">+ Insert</option>
          {LANE_TYPES.map((t) => (
            <option key={t} value={t}>{LANE_TYPE_META[t].label}</option>
          ))}
        </select>
      </div>
      <ScrollArea className="max-h-40">
        <div className="flex flex-col gap-1">
          {Array.from({ length: count }, (_, i) => {
            const key = `${side}:${i}`
            const isSelected = selectedKey === key
            return (
              <div key={key} className="flex items-center gap-1">
                <div className="flex-1">{renderRow(i)}</div>
                {isSelected ? (
                  <div className="flex items-center gap-0.5">
                    <Button size="icon" variant="ghost" className="h-5 w-5" onClick={() => onMoveUp(i)} title="Move outward">
                      <ChevronUp className="h-3 w-3" />
                    </Button>
                    <Button size="icon" variant="ghost" className="h-5 w-5" onClick={() => onMoveDown(i)} title="Move inward">
                      <ChevronDown className="h-3 w-3" />
                    </Button>
                    <Button size="icon" variant="ghost" className="h-5 w-5" onClick={() => onRemove(i)} title="Remove lane">
                      <Trash2 className="h-3 w-3 text-destructive" />
                    </Button>
                  </div>
                ) : null}
              </div>
            )
          })}
        </div>
      </ScrollArea>
    </div>
  )
}
