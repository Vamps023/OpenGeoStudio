import { useMemo } from 'react'
import { useStore, getLaneSection } from '../state/store'
import type { RoadData } from '../state/store'
import type { LaneDef, LaneType } from '../engine/laneTypes'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import { defaultLaneByType } from '../engine/laneLayout'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { toast } from 'sonner'
import SideBlock from './SideBlock'
import CenterSummary from './CenterSummary'
import LanePropertiesEditor from './LanePropertiesEditor'
import type { MarkingBehaviour, MarkingStyle } from '../engine/laneTypes'

interface LanesPanelProps {
  road: RoadData
}

const ALL_LANE_TYPES: LaneType[] = [
  'travel','paved_major','shoulder','hard_shoulder','soft_shoulder',
  'curb','sidewalk','bike','bus','median','parking','embankment',
  'ditch','barrier','land','lane_out','runway','taxiway',
]

export default function LanesPanel({ road }: LanesPanelProps) {
  const section = useMemo(() => getLaneSection(road), [road])
  const selectedLaneKey = useStore((s) => s.selectedLaneKey)
  const setSelectedLane = useStore((s) => s.setSelectedLane)
  const insertLaneAt = useStore((s) => s.insertLaneAt)
  const removeLaneAt = useStore((s) => s.removeLaneAt)
  const updateLaneAt = useStore((s) => s.updateLaneAt)
  const moveLaneAt = useStore((s) => s.moveLaneAt)

  const parsed = selectedLaneKey?.match(/^(left|right):(\d+)$/)
  const selSide = parsed?.[1] as 'left' | 'right' | undefined
  const selIndex = parsed ? Number(parsed[2]) : -1
  const selectedLane: LaneDef | undefined =
    selSide && selIndex >= 0 ? section[selSide][selIndex] : undefined

  const onAddLane = (side: 'left' | 'right', type: LaneType = 'travel') => {
    const list = section[side]
    insertLaneAt(road.id, side, list.length, defaultLaneByType(type))
    setSelectedLane(`${side}:${list.length}`)
    toast.success(`Added ${LANE_TYPE_META[type].label} on ${side}`)
  }

  const onInsertBefore = (side: 'left' | 'right', index: number, type: LaneType = 'travel') => {
    insertLaneAt(road.id, side, index, defaultLaneByType(type))
    setSelectedLane(`${side}:${index}`)
    toast.success(`Inserted before #${index}`)
  }

  const onInsertAfter = (side: 'left' | 'right', index: number, type: LaneType = 'travel') => {
    insertLaneAt(road.id, side, index + 1, defaultLaneByType(type))
    setSelectedLane(`${side}:${index + 1}`)
    toast.success(`Inserted after #${index}`)
  }

  const onRemove = (side: 'left' | 'right', index: number) => {
    if (section[side].length <= 1 && (side === 'left' ? section.right.length === 0 : section.left.length === 0)) {
      toast.error('Cannot remove the last lane')
      return
    }
    removeLaneAt(road.id, side, index)
    setSelectedLane(null)
  }

  const onMove = (side: 'left' | 'right', from: number, dir: -1 | 1) => {
    const to = from + dir
    if (to < 0 || to >= section[side].length) return
    moveLaneAt(road.id, side, from, to)
    setSelectedLane(`${side}:${to}`)
  }

  const onUpdate = (side: 'left' | 'right', index: number, patch: Partial<LaneDef>) => {
    updateLaneAt(road.id, side, index, patch)
  }

  const onClone = (side: 'left' | 'right', index: number) => {
    const lane = section[side][index]
    const cloned: LaneDef = { ...lane, id: crypto.randomUUID(), name: (lane.name ?? 'Lane') + ' (copy)' }
    insertLaneAt(road.id, side, index + 1, cloned)
    setSelectedLane(`${side}:${index + 1}`)
  }

  return (
    <div className="flex h-full flex-col">
      <div className="flex items-center justify-between border-b border-slate-800 px-3 py-2">
        <div>
          <div className="text-sm font-semibold text-slate-100">Lanes</div>
          <div className="text-[10px] uppercase tracking-wide text-slate-500">
            {road.name} · {section.left.length + section.right.length} lanes
          </div>
        </div>
        <Select onValueChange={(value) => onAddLane('right', value as LaneType)}>
          <SelectTrigger className="h-7 w-[150px] text-xs">
            <SelectValue placeholder="+ Add right" />
          </SelectTrigger>
          <SelectContent>
            {ALL_LANE_TYPES.map((t) => (
              <SelectItem key={t} value={t}>
                <span className="flex items-center gap-2">
                  <span className="inline-block h-2.5 w-2.5 rounded" style={{ background: LANE_TYPE_META[t].color }} />
                  {LANE_TYPE_META[t].label}
                </span>
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>

      <ScrollArea className="flex-1">
        <div className="space-y-3 p-3">
          <SideBlock
            side="left"
            lanes={section.left}
            selectedKey={selectedLaneKey}
            onSelect={(idx) => setSelectedLane(`left:${idx}`)}
            onAdd={(type) => onAddLane('left', type)}
            onInsertBefore={(idx, type) => onInsertBefore('left', idx, type)}
            onInsertAfter={(idx, type) => onInsertAfter('left', idx, type)}
            onRemove={(idx) => onRemove('left', idx)}
            onMove={(idx, dir) => onMove('left', idx, dir)}
            onClone={(idx) => onClone('left', idx)}
          />

          <Separator />

          <CenterSummary section={section} roadName={road.name} />

          <Separator />

          <SideBlock
            side="right"
            lanes={section.right}
            selectedKey={selectedLaneKey}
            onSelect={(idx) => setSelectedLane(`right:${idx}`)}
            onAdd={(type) => onAddLane('right', type)}
            onInsertBefore={(idx, type) => onInsertBefore('right', idx, type)}
            onInsertAfter={(idx, type) => onInsertAfter('right', idx, type)}
            onRemove={(idx) => onRemove('right', idx)}
            onMove={(idx, dir) => onMove('right', idx, dir)}
            onClone={(idx) => onClone('right', idx)}
          />

          {selectedLane && selSide && selIndex >= 0 ? (
            <>
              <Separator />
              <LanePropertiesEditor
                lane={selectedLane}
                onChange={(patch) => onUpdate(selSide, selIndex, patch)}
              />
              <Separator />
              <Button
                size="sm"
                variant="outline"
                className="h-7 w-full text-xs"
                onClick={() => {
                  const lane2 = selectedLane
                  const half = lane2.width / 2
                  const clone = { ...lane2, id: crypto.randomUUID(), width: half, name: (lane2.name ?? 'Lane') + ' B' }
                  onUpdate(selSide, selIndex, { width: half, name: (lane2.name ?? 'Lane') + ' A' })
                  insertLaneAt(road.id, selSide, selIndex + 1, clone)
                  setSelectedLane(`${selSide}:${selIndex + 1}`)
                  toast.success('Lane split in two')
                }}
              >
                Split Lane
              </Button>
              <div className="grid grid-cols-2 gap-2">
                <div className="grid gap-1">
                  <span className="text-[10px] uppercase tracking-wide text-slate-500">Ground Name</span>
                  <input
                    className="h-7 rounded-md border border-slate-700 bg-slate-900/60 px-2 text-xs text-slate-100"
                    value={selectedLane.groundName ?? 'Default'}
                    onChange={(e) => onUpdate(selSide, selIndex, { groundName: e.target.value })}
                  />
                </div>
                <div className="grid gap-1">
                  <span className="text-[10px] uppercase tracking-wide text-slate-500">Material Name</span>
                  <input
                    className="h-7 rounded-md border border-slate-700 bg-slate-900/60 px-2 text-xs text-slate-100"
                    value={selectedLane.materialName ?? ''}
                    placeholder="None"
                    onChange={(e) => onUpdate(selSide, selIndex, { materialName: e.target.value })}
                  />
                </div>
              </div>
              <MarkingStyleEditor
                lane={selectedLane}
                onChange={(patch) => onUpdate(selSide, selIndex, patch)}
              />
            </>
          ) : (
            <div className="rounded border border-dashed border-slate-700 bg-slate-900/40 p-3 text-center text-xs text-slate-500">
              Select a lane above to edit its properties.
            </div>
          )}
        </div>
      </ScrollArea>
    </div>
  )
}



const BEHAVIOURS: { value: MarkingBehaviour; label: string }[] = [
  { value: 'cannot', label: 'Cannot cross (continuous)' },
  { value: 'pullback', label: 'Pull Back' },
  { value: 'cancross', label: 'Can cross (dotted)' },
]

function MarkingStyleEditor({ lane, onChange }: { lane: LaneDef; onChange: (patch: Partial<LaneDef>) => void }) {
  const style: MarkingStyle = lane.markingStyle ?? {}
  const set = (patch: Partial<MarkingStyle>) => onChange({ markingStyle: { ...style, ...patch } })
  return (
    <div className="rounded border border-slate-700 bg-slate-900/40 p-2">
      <div className="mb-1.5 text-[10px] font-bold uppercase tracking-wide text-slate-500">
        Marking editor
      </div>
      <div className="grid grid-cols-2 gap-1.5">
        <select
          className="h-6 rounded border border-slate-700 bg-slate-900/60 px-1 text-xs text-slate-100"
          value={style.crossLeft ?? 'cannot'}
          onChange={(e) => set({ crossLeft: e.target.value as MarkingBehaviour })}
          title="Behaviour for vehicles coming from the left"
        >
          {BEHAVIOURS.map((b) => <option key={b.value} value={b.value}>Left: {b.label}</option>)}
        </select>
        <select
          className="h-6 rounded border border-slate-700 bg-slate-900/60 px-1 text-xs text-slate-100"
          value={style.crossRight ?? 'cannot'}
          onChange={(e) => set({ crossRight: e.target.value as MarkingBehaviour })}
          title="Behaviour for vehicles coming from the right"
        >
          {BEHAVIOURS.map((b) => <option key={b.value} value={b.value}>Right: {b.label}</option>)}
        </select>
      </div>
      <div className="mt-1.5 flex flex-wrap gap-x-3 gap-y-1 text-[11px] text-slate-300">
        {([
          ['dissuasive', 'Dissuasive'],
          ['destinationSeparation', 'Destination Sep.'],
          ['stopForbidden', 'Stop Forbidden'],
          ['parkingForbidden', 'Parking Forbidden'],
          ['alternate', 'Alternate'],
        ] as const).map(([key, label]) => (
          <label key={key} className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={!!style[key]}
              onChange={(e) => set({ [key]: e.target.checked } as Partial<MarkingStyle>)}
            />
            {label}
          </label>
        ))}
      </div>
      <div className="mt-1.5 grid grid-cols-3 gap-1.5">
        <div className="grid gap-0.5">
          <span className="text-[10px] text-slate-500">Dot length (m)</span>
          <input
            type="number"
            step={0.5}
            min={0}
            className="h-6 rounded border border-slate-700 bg-slate-900/60 px-1 text-xs text-slate-100"
            value={style.dotLength ?? 3}
            onChange={(e) => set({ dotLength: Math.max(0, Number.parseFloat(e.target.value) || 0) })}
          />
        </div>
        <div className="grid gap-0.5">
          <span className="text-[10px] text-slate-500">Total length (m)</span>
          <input
            type="number"
            step={0.5}
            min={0}
            className="h-6 rounded border border-slate-700 bg-slate-900/60 px-1 text-xs text-slate-100"
            value={style.totalLength ?? 9}
            onChange={(e) => set({ totalLength: Math.max(0, Number.parseFloat(e.target.value) || 0) })}
          />
        </div>
        <div className="grid gap-0.5">
          <span className="text-[10px] text-slate-500">Line width (m)</span>
          <input
            type="number"
            step={0.05}
            min={0.05}
            className="h-6 rounded border border-slate-700 bg-slate-900/60 px-1 text-xs text-slate-100"
            value={style.width ?? 0.15}
            onChange={(e) => set({ width: Math.max(0.05, Number.parseFloat(e.target.value) || 0) })}
          />
        </div>
      </div>
    </div>
  )
}
