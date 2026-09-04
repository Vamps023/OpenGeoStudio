import { useState } from 'react'
import { Plus, Trash2, ArrowUp, ArrowDown, ChevronsLeft, ChevronsRight, Layers, ArrowLeftRight, Settings2 } from 'lucide-react'
import type { LaneDef, LaneSectionDef, LaneType, CirculationWay, VehicleCategory, LaneMarking } from '../engine/laneTypes'
import { LANE_TYPE_META, CIRCULATION_META, VEHICLE_META, MARKING_META, totalWidth } from '../engine/lanes'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Separator } from '@/components/ui/separator'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'

export interface LaneEditPaneProps {
  section: LaneSectionDef
  onInsert: (side: 'left' | 'right', index: number, type: LaneType) => void
  onRemove: (side: 'left' | 'right', index: number) => void
  onMove: (side: 'left' | 'right', from: number, to: number) => void
  onSelect: (side: 'left' | 'right', index: number) => void
  onUpdate: (side: 'left' | 'right', index: number, patch: Partial<LaneDef>) => void
  selectedLaneKey: string | null
  selectedLane: LaneDef | null
  parsed: { side: 'left' | 'right'; index: number } | null
}

export function LaneEditPane(props: LaneEditPaneProps) {
  const { section, onInsert, onRemove, onMove, onSelect, onUpdate, selectedLaneKey, selectedLane, parsed } = props
  return (
    <div className="flex h-full flex-col gap-2 overflow-hidden">
      <ScrollArea className="flex-1">
        <div className="space-y-3 p-2">
          <SideBlock side="left" lanes={section.left} onInsert={(idx, t) => onInsert('left', idx, t)} onRemove={(idx) => onRemove('left', idx)} onMove={(f, to) => onMove('left', f, to)} onSelect={(idx) => onSelect('left', idx)} selectedLaneKey={selectedLaneKey} />
          <Separator />
          <SideBlock side="right" lanes={section.right} onInsert={(idx, t) => onInsert('right', idx, t)} onRemove={(idx) => onRemove('right', idx)} onMove={(f, to) => onMove('right', f, to)} onSelect={(idx) => onSelect('right', idx)} selectedLaneKey={selectedLaneKey} />
          <Separator />
          <Summary section={section} />
        </div>
      </ScrollArea>
      {selectedLane && parsed ? (
        <LaneProperties lane={selectedLane} onChange={(patch) => onUpdate(parsed.side, parsed.index, patch)} />
      ) : (
        <div className="rounded-md border border-dashed border-border/60 p-3 text-center text-xs text-muted-foreground">
          Select a lane above to edit its properties.
        </div>
      )}
    </div>
  )
}

function SideBlock(props: {
  side: 'left' | 'right'
  lanes: LaneDef[]
  onInsert: (index: number, type: LaneType) => void
  onRemove: (index: number) => void
  onMove: (from: number, to: number) => void
  onSelect: (index: number) => void
  selectedLaneKey: string | null
}) {
  const { side, lanes, onInsert, onRemove, onMove, onSelect, selectedLaneKey } = props
  return (
    <div className="space-y-1">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-1 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
          <ChevronsLeft className={side === 'left' ? 'opacity-100' : 'opacity-0'} size={12} />
          {side === 'left' ? 'Left side' : 'Right side'} ({lanes.length})
          <ChevronsRight className={side === 'right' ? 'opacity-100' : 'opacity-0'} size={12} />
        </div>
        <InsertLaneMenu onPick={(t) => onInsert(lanes.length, t)} />
      </div>
      <div className="space-y-1">
        {lanes.map((lane, i) => (
          <LaneRow
            key={lane.id ?? `${side}-${i}`}
            lane={lane}
            index={i}
            isSelected={selectedLaneKey === `${side}:${i}`}
            onSelect={() => onSelect(i)}
            onRemove={() => onRemove(i)}
            onMoveUp={() => onMove(i, i - 1)}
            onMoveDown={() => onMove(i, i + 1)}
            canMoveUp={i > 0}
            canMoveDown={i < lanes.length - 1}
          />
        ))}
        {lanes.length === 0 && (
          <div className="rounded-md border border-dashed border-border/60 p-2 text-center text-[11px] text-muted-foreground">
            No lanes on this side
          </div>
        )}
      </div>
      <div className="flex justify-center">
        <InsertLaneMenu small onPick={(t) => onInsert(0, t)} label="Insert at start" />
      </div>
    </div>
  )
}
