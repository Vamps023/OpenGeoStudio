import { useMemo } from 'react'
import { useStore, getLaneSection } from '../state/store'
import type { RoadData } from '../state/store'
import { LANE_TYPE_META } from '../engine/laneMetadata'
import { circulationLabel } from './LanePropertiesEditor'
import { defaultLaneByType, totalLanes, totalWidth } from '../engine/laneLayout'
import type { LaneType, LaneDef } from '../engine/laneTypes'
import { Button } from '@/components/ui/button'
import { Plus, Trash2, ArrowUp, ArrowDown, Copy } from 'lucide-react'
import { toast } from 'sonner'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'
import CrossProfilePreview from './CrossProfilePreview'
import LaneSelectionPanel from './LaneSelectionPanel'
import BorderSelectionPanel from './BorderSelectionPanel'
import InsertionMenu from './InsertionMenu'

interface LanesTabPanelProps {
  road: RoadData
}

function parseSide(s: string | null) {
  if (!s) return null
  const [side, idx] = s.split(':')
  if ((side !== 'left' && side !== 'right') || idx === undefined) return null
  const n = Number(idx)
  if (!Number.isFinite(n)) return null
  return { side: side as 'left' | 'right', index: n }
}

function parseBorder(s: string | null) {
  if (!s) return null
  const [side, edge] = s.split(':')
  if ((side !== 'left' && side !== 'right') || (edge !== 'inner' && edge !== 'outer')) return null
  return { side: side as 'left' | 'right', edge: edge as 'inner' | 'outer' }
}

export default function LanesTabPanel({ road }: LanesTabPanelProps) {
  const insertLaneAt = useStore((s) => s.insertLaneAt)
  const removeLaneAt = useStore((s) => s.removeLaneAt)
  const updateLaneAt = useStore((s) => s.updateLaneAt)
  const moveLaneAt = useStore((s) => s.moveLaneAt)
  const selectedLaneKey = useStore((s) => s.selectedLaneKey)
  const setSelectedLane = useStore((s) => s.setSelectedLane)
  const setSelectedBorder = useStore((s) => s.setSelectedBorder)
  const selectedBorderKey = useStore((s) => s.selectedBorderKey)

  const section = useMemo(() => getLaneSection(road), [road])
  const tl = totalLanes(section)
  const tw = totalWidth(section)
  const sel = parseSide(selectedLaneKey)
  const borderSel = parseBorder(selectedBorderKey)
  const selectedLane: LaneDef | null = sel ? section[sel.side][sel.index] ?? null : null

  function addLaneBefore(side: 'left' | 'right', type: LaneType) {
    const idx = sel && sel.side === side ? sel.index : section[side].length
    insertLaneAt(road.id, side, idx, defaultLaneByType(type))
    setSelectedLane(`${side}:${idx}`)
    toast.success(`Added ${LANE_TYPE_META[type].label} lane`)
  }

  function addLaneAfter(side: 'left' | 'right', type: LaneType) {
    const idx = sel && sel.side === side ? sel.index + 1 : section[side].length
    insertLaneAt(road.id, side, idx, defaultLaneByType(type))
    setSelectedLane(`${side}:${idx}`)
    toast.success(`Added ${LANE_TYPE_META[type].label} lane`)
  }

  function removeSelected() {
    if (!sel) return
    removeLaneAt(road.id, sel.side, sel.index)
    setSelectedLane(null)
  }

  function moveSelected(direction: -1 | 1) {
    if (!sel) return
    const target = sel.index + direction
    if (target < 0 || target >= section[sel.side].length) return
    moveLaneAt(road.id, sel.side, sel.index, target)
    setSelectedLane(`${sel.side}:${target}`)
  }

  function patchSelected(patch: Partial<LaneDef>) {
    if (!sel) return
    updateLaneAt(road.id, sel.side, sel.index, patch)
  }

  function duplicateSelected() {
    if (!sel) return
    const def = { ...section[sel.side][sel.index], id: (crypto.randomUUID?.() ?? `${Date.now()}-${Math.random()}`) } as LaneDef
    insertLaneAt(road.id, sel.side, sel.index + 1, def)
    setSelectedLane(`${sel.side}:${sel.index + 1}`)
  }

  return (
    <div className="flex min-w-0 flex-col gap-3 text-xs">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <Badge variant="outline">Lanes</Badge>
          <span className="text-muted-foreground">
            {tl} lane{tl === 1 ? '' : 's'} · {tw.toFixed(1)} m wide
          </span>
        </div>
      </div>

      <div className="grid grid-cols-2 gap-2">
        <LaneSideColumn side="left" label="Left" lanes={section.left} selectedKey={selectedLaneKey}
          onSelect={(i) => setSelectedLane(`left:${i}`)}
          onAddBefore={(t) => addLaneBefore('left', t)}
          onAddAfter={(t) => addLaneAfter('left', t)} />
        <LaneSideColumn side="right" label="Right" lanes={section.right} selectedKey={selectedLaneKey}
          onSelect={(i) => setSelectedLane(`right:${i}`)}
          onAddBefore={(t) => addLaneBefore('right', t)}
          onAddAfter={(t) => addLaneAfter('right', t)} />
      </div>

      <Separator />

      <CrossProfilePreview
        section={section}
        selectedLaneKey={selectedLaneKey}
        selectedBorderKey={selectedBorderKey}
        onSelectLane={(side, index) => setSelectedLane(`${side}:${index}`)}
        onSelectBorder={(side, edge) => setSelectedBorder(`${side}:${edge}`)}
      />

      <Separator />

      {selectedLane ? (
        <LaneSelectionPanel
          lane={selectedLane} side={sel!.side} index={sel!.index}
          onChange={patchSelected} onRemove={removeSelected}
          onMove={moveSelected} onDuplicate={duplicateSelected}
          canMoveUp={sel!.index > 0}
          canMoveDown={sel!.index < section[sel!.side].length - 1}
        />
      ) : (
        <p className="text-muted-foreground">Click a lane in the cross-profile above to edit its properties.</p>
      )}

      {borderSel && (
        <BorderSelectionPanel section={section} side={borderSel.side} edge={borderSel.edge} roadId={road.id} />
      )}

      <div className="hidden">
        <Button size="icon" onClick={removeSelected}><Trash2 className="h-3 w-3" /></Button>
        <Button size="icon" onClick={() => moveSelected(-1)}><ArrowUp className="h-3 w-3" /></Button>
        <Button size="icon" onClick={() => moveSelected(1)}><ArrowDown className="h-3 w-3" /></Button>
        <Button size="icon" onClick={duplicateSelected}><Copy className="h-3 w-3" /></Button>
        <Plus className="h-3 w-3" />
      </div>
    </div>
  )
}

function LaneSideColumn({
  side, label, lanes, selectedKey, onSelect, onAddBefore, onAddAfter,
}: {
  side: 'left' | 'right'
  label: string
  lanes: LaneDef[]
  selectedKey: string | null
  onSelect: (index: number) => void
  onAddBefore: (type: LaneType) => void
  onAddAfter: (type: LaneType) => void
}) {
  return (
    <div className="rounded border border-border bg-card/40 p-2">
      <div className="mb-2 flex items-center justify-between">
        <span className="font-semibold">{label}</span>
        <span className="text-muted-foreground">{lanes.length}</span>
      </div>
      <div>
        <div className="flex flex-col gap-1">
          <InsertionMenu onPick={onAddBefore} label="Insert Before" />
          {lanes.map((lane, idx) => {
            const isSel = selectedKey === `${side}:${idx}`
            const meta = LANE_TYPE_META[lane.type]
            return (
              <button
                key={lane.id}
                onClick={() => onSelect(idx)}
                className={`flex items-center justify-between gap-2 rounded px-2 py-1 text-left text-xs transition ${
                  isSel ? 'bg-primary/20 ring-1 ring-primary' : 'hover:bg-muted/60'
                }`}
                style={{ borderLeft: `3px solid ${meta.color}` }}
              >
                <span className="flex flex-col">
                  <span className="font-medium">{meta.label}</span>
                  <span className="text-muted-foreground">
                    {lane.width.toFixed(2)} m · {lane.speedLimit} km/h · {circulationLabel(lane.circulation, side)}
                  </span>
                </span>
              </button>
            )
          })}
          {lanes.length === 0 && (
            <p className="text-center text-muted-foreground text-xs italic py-2">No lanes</p>
          )}
          <InsertionMenu onPick={onAddAfter} label="Insert After" />
        </div>
      </div>
    </div>
  )
}
