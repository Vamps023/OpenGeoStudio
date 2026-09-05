import type { LaneDef, CirculationWay, VehicleCategory, LaneMarking, LaneType } from '../engine/laneTypes'
import { LANE_TYPE_META, VEHICLE_META, MARKING_META } from '../engine/laneMetadata'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Checkbox } from '@/components/ui/checkbox'

const ALL_VEHICLE_KEYS: VehicleCategory[] = ['all','car','truck','bus','bike','pedestrian','motorcycle','emergency','airplane']
const ALL_LANE_TYPES: LaneType[] = [
  'travel','paved_major','shoulder','hard_shoulder','soft_shoulder',
  'curb','sidewalk','bike','bus','median','parking','embankment',
  'ditch','barrier','land','lane_out','runway','taxiway',
]
const ALL_CIRCULATIONS: CirculationWay[] = ['forward','backward','both']
const ALL_MARKINGS: LaneMarking[] = ['none','solid','dashed','double-solid','solid-dashed','dashed-solid']

export function circulationLabel(way: CirculationWay, side: 'left' | 'right') {
  if (way === 'both') return 'Both · bidirectional'
  const towardStart = (side === 'left') === (way === 'forward')
  return `${way === 'forward' ? 'Forward' : 'Backward'} · toward road ${towardStart ? 'start' : 'end'}`
}

interface LanePropertiesEditorProps {
  lane: LaneDef
  side: 'left' | 'right'
  onChange: (patch: Partial<LaneDef>) => void
}

export default function LanePropertiesEditor({ lane, side, onChange }: LanePropertiesEditorProps) {
  const meta = LANE_TYPE_META[lane.type]
  return (
    <div className="space-y-3 rounded border border-border bg-card/50 p-3">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <div className="flex items-center gap-2">
          <span className="inline-block h-3 w-3 rounded" style={{ background: meta.color }} />
          <div className="text-xs font-semibold text-foreground">{lane.name || meta.label} · {side} lane properties</div>
        </div>
        <Badge variant="outline" className="text-[10px]">{meta.label}</Badge>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-muted-foreground">Type of lane</Label>
          <Select value={lane.type} onValueChange={(v) => onChange({ type: v as LaneType, width: LANE_TYPE_META[v as LaneType].defaultWidth })}>
            <SelectTrigger aria-label="Type of lane" className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_LANE_TYPES.map((t) => (
                <SelectItem key={t} value={t}>{LANE_TYPE_META[t].label}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="space-y-1">
          <Label className="text-[10px] uppercase text-muted-foreground">Name</Label>
          <Input aria-label="Lane name" className="h-7 text-xs" value={lane.name ?? ''} onChange={(e) => onChange({ name: e.target.value })} placeholder={meta.label} />
        </div>
      </div>
      <div className="space-y-1">
        <Label className="text-[10px] uppercase text-muted-foreground">Width [m] (0..100)</Label>
        <Input
          aria-label="Lane width in meters" type="number" step={0.05} min={0} max={100}
          className="h-7 text-xs"
          value={lane.width}
          onChange={(e) => onChange({ width: Math.max(0, Math.min(100, Number(e.target.value))) })}
        />
      </div>
      <div className="space-y-1">
        <Label className="text-[10px] uppercase text-muted-foreground">Circulation direction</Label>
        <Select value={lane.circulation} onValueChange={(v) => onChange({ circulation: v as CirculationWay })}>
          <SelectTrigger aria-label="Circulation direction" className="h-7 text-xs"><SelectValue /></SelectTrigger>
          <SelectContent>
            {ALL_CIRCULATIONS.map((c) => (
              <SelectItem key={c} value={c}>{circulationLabel(c, side)}</SelectItem>
            ))}
          </SelectContent>
        </Select>
        <p className="text-[11px] text-muted-foreground">
          Forward follows the normal side direction: left toward road start, right toward road end. Backward reverses it; Both is bidirectional.
        </p>
      </div>
      <div className="space-y-1">
        <Label className="text-[10px] uppercase text-muted-foreground">Speed limit [km/h]</Label>
        <Input
          aria-label="Speed limit in kilometers per hour" type="number" min={0} max={320}
          className="h-7 text-xs"
          value={lane.speedLimit}
          onChange={(e) => onChange({ speedLimit: Math.max(0, Math.min(320, Number(e.target.value))) })}
        />
      </div>
      <div className="space-y-1.5">
        <Label className="text-[10px] uppercase text-muted-foreground">Allowed vehicles</Label>
        <div className="grid grid-cols-2 gap-1.5">
          {ALL_VEHICLE_KEYS.map((v) => {
            const checked = lane.vehicles.includes(v)
            const metaV = VEHICLE_META[v]
            return (
              <label key={v} className={`flex cursor-pointer items-center gap-2 rounded border px-2 py-1 text-[11px] ${
                checked
                  ? 'border-primary bg-primary/10 text-foreground'
                  : 'border-border bg-muted/40 text-muted-foreground hover:border-primary/50'
              }`}>
                <Checkbox
                  checked={checked}
                  onChange={(e) => {
                    const c = e.target.checked
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
        <Label className="text-[10px] uppercase text-muted-foreground">Outer boundary marking</Label>
        <Select value={lane.marking ?? 'solid'} onValueChange={(v) => onChange({ marking: v as LaneMarking })}>
          <SelectTrigger aria-label="Outer boundary marking" className="h-7 text-xs"><SelectValue /></SelectTrigger>
          <SelectContent>
            {ALL_MARKINGS.map((m) => (
              <SelectItem key={m} value={m}>{MARKING_META[m].label}</SelectItem>
            ))}
          </SelectContent>
        </Select>
        <p className="text-[11px] text-muted-foreground">Controls this lane's outer edge, away from the road center. Double patterns are ordered inner stripe, then outer stripe. The green center line is unchanged.</p>
      </div>
      <div className="space-y-2 rounded border border-border bg-muted/40 p-2">
        <Label className="text-[10px] uppercase text-muted-foreground">Borders (off-road feedback settings)</Label>
        <div className="grid grid-cols-2 gap-2">
          {([
            ['borderLeftHeight', 'Left height', 0.01],
            ['borderRightHeight', 'Right height', 0.01],
            ['borderLeftOffset', 'Left offset', 0.05],
            ['borderRightOffset', 'Right offset', 0.05],
          ] as const).map(([key, label, step]) => (
            <div key={key} className="space-y-1">
              <Label className="text-[10px] text-muted-foreground">{label} [m]</Label>
              <Input
                aria-label={`Border ${label.toLowerCase()} in meters`} type="number" step={step} min={0} max={key.endsWith('Height') ? 1 : 2}
                className="h-7 text-xs" value={lane[key] ?? 0}
                onChange={(e) => onChange({ [key]: Math.max(0, Math.min(key.endsWith('Height') ? 1 : 2, Number(e.target.value))) })}
              />
            </div>
          ))}
        </div>
        <p className="text-[11px] text-muted-foreground">
          Left/right borders follow road coordinates, not circulation. Cross section provides road-edge border selection and presets. Feedback settings are metadata; this editor does not simulate haptics.
        </p>
      </div>
    </div>
  )
}
