import { useState } from 'react'
import { Plus, Trash2, ArrowUp, ArrowDown, Settings2 } from 'lucide-react'
import type { LaneDef, LaneType, CirculationWay, VehicleCategory, LaneMarking } from '../engine/laneTypes'
import { LANE_TYPE_META, CIRCULATION_META, VEHICLE_META, MARKING_META } from '../engine/lanes'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'

export function LaneRow(props: {
  lane: LaneDef
  isSelected: boolean
  onSelect: () => void
  onRemove: () => void
  onMoveUp: () => void
  onMoveDown: () => void
  canMoveUp: boolean
  canMoveDown: boolean
}) {
  const { lane, isSelected, onSelect, onRemove, onMoveUp, onMoveDown, canMoveUp, canMoveDown } = props
  const meta = LANE_TYPE_META[lane.type]
  return (
    <div
      className={
        'flex items-center gap-2 rounded-md border p-1.5 text-xs ' +
        (isSelected ? 'border-primary bg-primary/10' : 'border-border/40 hover:bg-muted/50')
      }
      onClick={onSelect}
      role="button"
    >
      <div className="h-4 w-4 rounded" style={{ background: meta.color }} />
      <div className="flex-1 truncate">
        <div className="font-medium">{lane.name || meta.label}</div>
        <div className="text-[10px] text-muted-foreground">
          {lane.width.toFixed(2)} m · {lane.speedLimit} km/h · {CIRCULATION_META[lane.circulation].symbol}
        </div>
      </div>
      <div className="flex items-center gap-0.5">
        <Button type="button" variant="ghost" size="icon" className="h-6 w-6" disabled={!canMoveUp} onClick={(e) => { e.stopPropagation(); onMoveUp() }} title="Move inward">
          <ArrowUp size={12} />
        </Button>
        <Button type="button" variant="ghost" size="icon" className="h-6 w-6" disabled={!canMoveDown} onClick={(e) => { e.stopPropagation(); onMoveDown() }} title="Move outward">
          <ArrowDown size={12} />
        </Button>
        <Button type="button" variant="ghost" size="icon" className="h-6 w-6" onClick={(e) => { e.stopPropagation(); onRemove() }} title="Remove lane">
          <Trash2 size={12} />
        </Button>
      </div>
    </div>
  )
}

export function InsertLaneMenu({ onPick, label }: { onPick: (t: LaneType) => void; label?: string }) {
  const [open, setOpen] = useState(false)
  return (
    <div className="relative">
      <Button type="button" variant="outline" size="sm" className="h-7 gap-1 px-2" onClick={(e) => { e.stopPropagation(); setOpen((o) => !o) }}>
        <Plus size={12} />
        {label ?? 'Add lane'}
      </Button>
      {open && (
        <div
          className="absolute right-0 z-30 mt-1 w-56 rounded-md border border-border bg-popover p-1 text-xs shadow-lg"
          onMouseLeave={() => setOpen(false)}
        >
          {Object.entries(LANE_TYPE_META).map(([type, meta]) => (
            <button
              key={type}
              className="flex w-full items-center gap-2 rounded px-2 py-1 text-left hover:bg-muted"
              onClick={(e) => { e.stopPropagation(); onPick(type as LaneType); setOpen(false) }}
            >
              <div className="h-3 w-3 rounded" style={{ background: meta.color }} />
              <span className="flex-1">{meta.label}</span>
              <span className="text-muted-foreground">{meta.defaultWidth}m</span>
            </button>
          ))}
        </div>
      )}
    </div>
  )
}
