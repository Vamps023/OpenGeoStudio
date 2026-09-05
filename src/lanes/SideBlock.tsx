import type { LaneDef, LaneType } from '../engine/laneTypes'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import { circulationLabel } from './LanePropertiesEditor'
import { Button } from '@/components/ui/button'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { ArrowUp, ArrowDown, Copy, Trash2, Plus } from 'lucide-react'

const ALL_LANE_TYPES: LaneType[] = [
  'travel','paved_major','shoulder','hard_shoulder','soft_shoulder',
  'curb','sidewalk','bike','bus','median','parking','embankment',
  'ditch','barrier','land','lane_out','runway','taxiway',
]

interface SideBlockProps {
  side: 'left' | 'right'
  lanes: LaneDef[]
  selectedKey: string | null
  onSelect: (idx: number) => void
  onAdd: (type: LaneType) => void
  onInsertBefore: (idx: number, type?: LaneType) => void
  onInsertAfter: (idx: number, type?: LaneType) => void
  onRemove: (idx: number) => void
  onMove: (idx: number, dir: -1 | 1) => void
  onClone: (idx: number) => void
}

export default function SideBlock({
  side,
  lanes,
  selectedKey,
  onSelect,
  onAdd,
  onInsertBefore,
  onInsertAfter,
  onRemove,
  onMove,
  onClone,
}: SideBlockProps) {
  return (
    <div>
      <div className="mb-1.5 flex items-center justify-between">
        <div className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
          {side === 'left' ? '◀ Left side' : 'Right side ▶'} ({lanes.length})
        </div>
        <Select onValueChange={(value) => onAdd(value as LaneType)}>
          <SelectTrigger className="h-6 w-[110px] text-[10px]">
            <SelectValue placeholder="+ Lane" />
          </SelectTrigger>
          <SelectContent>
            {ALL_LANE_TYPES.map((t) => (
              <SelectItem key={t} value={t}>{LANE_TYPE_META[t].label}</SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>
      <div className="space-y-1">
        {lanes.map((lane, idx) => {
          const key = `${side}:${idx}`
          const isSelected = selectedKey === key
          const meta = LANE_TYPE_META[lane.type]
          return (
            <div
              key={key}
              className={`group flex cursor-pointer items-center gap-2 rounded border px-2 py-1.5 text-xs transition-colors ${
                isSelected
                  ? 'border-primary bg-primary/10 text-foreground'
                  : 'border-border bg-card/50 hover:border-primary/50 hover:bg-muted/40 text-foreground'
              }`}
              onClick={() => onSelect(idx)}
            >
              <span
                className="inline-block h-3.5 w-3.5 shrink-0 rounded"
                style={{ background: meta.color }}
                title={meta.label}
              />
              <div className="flex-1 min-w-0">
                <div className="truncate text-xs font-medium">
                  #{idx} · {lane.name ?? meta.label}
                </div>
                <div className="truncate text-[10px] text-muted-foreground">
                  {lane.width.toFixed(2)} m · {circulationLabel(lane.circulation, side)} · {lane.speedLimit} km/h
                </div>
              </div>
              <div className="flex items-center gap-0.5 opacity-0 transition-opacity group-hover:opacity-100">
                <Tooltip>
                  <TooltipTrigger asChild>
                    <Button size="icon" variant="ghost" className="h-5 w-5" onClick={(e) => { e.stopPropagation(); onMove(idx, -1) }}>
                      <ArrowUp className="h-3 w-3" />
                    </Button>
                  </TooltipTrigger>
                  <TooltipContent>Move toward center</TooltipContent>
                </Tooltip>
                <Tooltip>
                  <TooltipTrigger asChild>
                    <Button size="icon" variant="ghost" className="h-5 w-5" onClick={(e) => { e.stopPropagation(); onMove(idx, +1) }}>
                      <ArrowDown className="h-3 w-3" />
                    </Button>
                  </TooltipTrigger>
                  <TooltipContent>Move toward outer</TooltipContent>
                </Tooltip>
                <Tooltip>
                  <TooltipTrigger asChild>
                    <Button size="icon" variant="ghost" className="h-5 w-5" onClick={(e) => { e.stopPropagation(); onClone(idx) }}>
                      <Copy className="h-3 w-3" />
                    </Button>
                  </TooltipTrigger>
                  <TooltipContent>Duplicate</TooltipContent>
                </Tooltip>
                <Tooltip>
                  <TooltipTrigger asChild>
                    <Button size="icon" variant="ghost" className="h-5 w-5 hover:text-red-400" onClick={(e) => { e.stopPropagation(); onRemove(idx) }}>
                      <Trash2 className="h-3 w-3" />
                    </Button>
                  </TooltipTrigger>
                  <TooltipContent>Remove</TooltipContent>
                </Tooltip>
              </div>
            </div>
          )
        })}
        {lanes.length === 0 && (
          <div className="rounded border border-dashed border-slate-800 bg-slate-900/30 p-2 text-center text-[10px] text-muted-foreground">
            No lanes · use + Lane to add one
          </div>
        )}
      </div>
    </div>
  )
}
