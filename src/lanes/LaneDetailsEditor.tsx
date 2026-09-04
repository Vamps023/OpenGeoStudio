import type { LaneDef, LaneType, CirculationWay, VehicleCategory, LaneMarking } from '../engine/laneTypes'
import { LANE_TYPE_META, CIRCULATION_META, VEHICLE_META, MARKING_META } from '../engine/laneMetadata'
import LaneHeader from './LaneHeader'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'

const ALL_LANE_TYPES: LaneType[] = [
  'travel', 'shoulder', 'hard_shoulder', 'soft_shoulder', 'curb', 'sidewalk',
  'bike', 'bus', 'median', 'parking', 'embankment', 'ditch', 'barrier',
  'land', 'paved_major', 'lane_out', 'runway', 'taxiway',
]

const ALL_VEHICLES: VehicleCategory[] = ['car', 'truck', 'bus', 'bike', 'pedestrian', 'motorcycle', 'emergency', 'airplane']

interface Props {
  lane: LaneDef
  side: 'left' | 'right'
  index: number
  siblingsCount: number
  onPatch: (patch: Partial<LaneDef>) => void
  onToggleVehicle: (v: VehicleCategory) => void
  onDelete: () => void
  onMove: (delta: -1 | 1) => void
  onInsert: (where: 'before' | 'after') => void
}

export default function LaneDetailsEditor({ lane, side, index, siblingsCount, onPatch, onToggleVehicle, onDelete, onMove, onInsert }: Props) {
  return (
    <ScrollArea className="h-[420px] pr-2">
      <div className="space-y-2">
        <LaneHeader side={side} index={index} type={lane.type} siblingsCount={siblingsCount}
          onInsert={onInsert} onMove={onMove} onDelete={onDelete} />
        <Field label="Name">
          <Input className="h-7 text-xs" value={lane.name ?? ''} onChange={(e) => onPatch({ name: e.target.value })} />
        </Field>
        <Field label="Type of lane">
          <Select value={lane.type} onValueChange={(v) => onPatch({ type: v as LaneType })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_LANE_TYPES.map((t) => <SelectItem key={t} value={t}>{LANE_TYPE_META[t].label}</SelectItem>)}
            </SelectContent>
          </Select>
        </Field>
        <Field label={`Width (m) [${lane.width.toFixed(2)}]`}>
          <NumInput min={0} max={100} step={0.05} value={lane.width} onChange={(v) => onPatch({ width: v })} />
        </Field>
        <Field label="Speed Limit (km/h)">
          <NumInput min={0} max={320} step={5} value={lane.speedLimit} onChange={(v) => onPatch({ speedLimit: v })} />
        </Field>
        <Field label="Circulation way">
          <Select value={lane.circulation} onValueChange={(v) => onPatch({ circulation: v as CirculationWay })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {Object.entries(CIRCULATION_META).map(([k, m]) => <SelectItem key={k} value={k}>{m.symbol} {m.label}</SelectItem>)}
            </SelectContent>
          </Select>
        </Field>
        <Field label="Vehicles authorized">
          <div className="flex flex-wrap gap-1">
            {ALL_VEHICLES.map((v) => (
              <Badge key={v} variant={lane.vehicles.includes(v) ? 'default' : 'outline'}
                className="text-[10px] cursor-pointer" onClick={() => onToggleVehicle(v)}>
                {VEHICLE_META[v].icon} {VEHICLE_META[v].label}
              </Badge>
            ))}
          </div>
        </Field>
        <Separator />
        <Field label="Marking (inner edge)">
          <Select value={lane.marking ?? 'solid'} onValueChange={(v) => onPatch({ marking: v as LaneMarking })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {Object.entries(MARKING_META).map(([k, m]) => <SelectItem key={k} value={k}>{m.label}</SelectItem>)}
            </SelectContent>
          </Select>
        </Field>
        <div className="grid grid-cols-2 gap-2">
          <Field label="Border height (m)">
            <NumInput min={0} max={2} step={0.01} value={lane.borderLeftHeight ?? 0} onChange={(v) => onPatch({ borderLeftHeight: v })} />
          </Field>
          <Field label="Border offset (m)">
            <NumInput min={0} max={2} step={0.01} value={lane.borderLeftOffset ?? 0} onChange={(v) => onPatch({ borderLeftOffset: v })} />
          </Field>
        </div>
      </div>
    </ScrollArea>
  )
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="grid gap-1">
      <Label className="text-[10px]">{label}</Label>
      {children}
    </div>
  )
}

function NumInput({ value, onChange, min, max, step }: { value: number; onChange: (v: number) => void; min: number; max: number; step: number }) {
  return (
    <Input type="number" min={min} max={max} step={step} value={value}
      onChange={(e) => onChange(clamp(Number(e.target.value), min, max))} className="h-7 text-xs" />
  )
}

function clamp(n: number, min: number, max: number) {
  if (Number.isNaN(n)) return min
  return Math.min(max, Math.max(min, n))
}
