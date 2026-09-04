import type { LaneDef, CirculationWay, VehicleCategory, LaneMarking, LaneType } from '../engine/laneTypes'
import { LANE_TYPE_META, CIRCULATION_META, VEHICLE_META, MARKING_META } from '../engine/laneMetadata'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Checkbox } from '@/components/ui/checkbox'

const ALL_VEHICLE_KEYS: VehicleCategory[] = ['car','truck','bus','bike','pedestrian','motorcycle','emergency','airplane']
const ALL_LANE_TYPES: LaneType[] = [
  'travel','paved_major','shoulder','hard_shoulder','soft_shoulder',
  'curb','sidewalk','bike','bus','median','parking','embankment',
  'ditch','barrier','land','lane_out','runway','taxiway',
]
const ALL_CIRCULATIONS: CirculationWay[] = ['forward','backward','both']
const ALL_MARKINGS: LaneMarking[] = ['none','solid','dashed','double-solid','solid-dashed','dashed-solid']

interface LanePropertiesEditorProps {
  lane: LaneDef
  onChange: (patch: Partial<LaneDef>) => void
}

export default function LanePropertiesEditor({ lane, onChange }: LanePropertiesEditorProps) {
  const meta = LANE_TYPE_META[lane.type]
  return (
    <div className="space-y-3 rounded border border-slate-800 bg-slate-900/50 p-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="inline-block h-3 w-3 rounded" style={{ background: meta.color }} />
          <div className="text-xs font-semibold text-slate-100">Lane properties</div>
        </div>
        <Badge variant="outline" className="text-[10px]">{meta.label}</Badge>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-slate-500">Type of lane</Label>
          <Select value={lane.type} onValueChange={(v) => onChange({ type: v as LaneType, width: LANE_TYPE_META[v as LaneType].defaultWidth })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_LANE_TYPES.map((t) => (
                <SelectItem key={t} value={t}>{LANE_TYPE_META[t].label}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-slate-500">Name</Label>
          <Input className="h-7 text-xs" value={lane.name ?? ''} onChange={(e) => onChange({ name: e.target.value })} placeholder={meta.label} />
        </div>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-slate-500">Width [m] (0..100)</Label>
          <Input
            type="number" step={0.05} min={0} max={100}
            className="h-7 text-xs"
            value={lane.width}
            onChange={(e) => onChange({ width: Math.max(0, Math.min(100, Number(e.target.value))) })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-slate-500">Circulation way</Label>
          <Select value={lane.circulation} onValueChange={(v) => onChange({ circulation: v as CirculationWay })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_CIRCULATIONS.map((c) => (
                <SelectItem key={c} value={c}>{CIRCULATION_META[c].symbol} {CIRCULATION_META[c].label}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
      </div>
      <div className="space-y-1">
        <Label className="text-[10px] uppercase text-slate-500">Speed limit [km/h]</Label>
        <Input
          type="number" min={0} max={320}
          className="h-7 text-xs"
          value={lane.speedLimit}
          onChange={(e) => onChange({ speedLimit: Math.max(0, Math.min(320, Number(e.target.value))) })}
        />
      </div>
      <div className="space-y-1.5">
        <Label className="text-[10px] uppercase text-slate-500">Vehicles authorized</Label>
        <div className="grid grid-cols-2 gap-1.5">
          {ALL_VEHICLE_KEYS.map((v) => {
            const checked = lane.vehicles.includes(v)
            const metaV = VEHICLE_META[v]
            return (
              <label key={v} className={`flex cursor-pointer items-center gap-2 rounded border px-2 py-1 text-[11px] ${
                checked
                  ? 'border-blue-500 bg-blue-500/10 text-slate-100'
                  : 'border-slate-800 bg-slate-900/40 text-slate-400 hover:border-slate-700'
              }`}>
                <Checkbox
                  checked={checked}
                  onCheckedChange={(c) => {
                    const next = c ? [...lane.vehicles, v] : lane.vehicles.filter((x) => x !== v)
                    onChange({ vehicles: next })
                  }}
                />
                <span>{metaV.icon}</span>
                <span>{metaV.label}</span>
              </label>
            )
          })}
        </div>
      </div>
      <div className="space-y-1">
        <Label className="text-[10px] uppercase text-slate-500">Marking</Label>
        <Select value={lane.marking ?? 'solid'} onValueChange={(v) => onChange({ marking: v as LaneMarking })}>
          <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
          <SelectContent>
            {ALL_MARKINGS.map((m) => (
              <SelectItem key={m} value={m}>{MARKING_META[m].label}</SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>
      <div className="space-y-2 rounded border border-slate-800 bg-slate-950/40 p-2">
        <Label className="text-[10px] uppercase text-slate-500">Borders (off-road feedback)</Label>
        <div className="grid grid-cols-2 gap-2">
          <div className="space-y-1">
            <Label className="text-[10px] text-slate-500">Inner height [m]</Label>
            <Input
              type="number" step={0.01} min={0} max={1}
              className="h-7 text-xs"
              value={lane.borderLeftHeight ?? 0}
              onChange={(e) => onChange({ borderLeftHeight: Number(e.target.value) })}
            />
          </div>
          <div className="space-y-1">
            <Label className="text-[10px] text-slate-500">Outer height [m]</Label>
            <Input
              type="number" step={0.01} min={0} max={1}
              className="h-7 text-xs"
              value={lane.borderRightHeight ?? 0}
              onChange={(e) => onChange({ borderRightHeight: Number(e.target.value) })}
            />
          </div>
        </div>
        <p className="text-[10px] text-slate-500">
          Non-zero heights give haptic feedback to the driver. Soft 0.05 / Hard 0.10 / Vertical 0.00 (unsafe).
        </p>
      </div>
    </div>
  )
}