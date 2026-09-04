import { Settings2 } from 'lucide-react'
import type { LaneDef, CirculationWay, VehicleCategory, LaneMarking } from '../engine/laneTypes'
import { LANE_TYPE_META, CIRCULATION_META, VEHICLE_META, MARKING_META } from '../engine/lanes'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'

export function LaneProperties({ lane, onChange }: { lane: LaneDef; onChange: (patch: Partial<LaneDef>) => void }) {
  return (
    <div className="space-y-2 rounded-md border border-border/60 bg-muted/20 p-3">
      <div className="flex items-center gap-1.5 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
        <Settings2 size={12} />
        Lane properties
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div>
          <Label className="text-[10px]">Type</Label>
          <Select value={lane.type} onValueChange={(v) => onChange({ type: v as typeof lane.type, width: lane.width })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {Object.entries(LANE_TYPE_META).map(([t, m]) => (
                <SelectItem key={t} value={t}>{m.label}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div>
          <Label className="text-[10px]">Name</Label>
          <Input className="h-7 text-xs" value={lane.name ?? ''} onChange={(e) => onChange({ name: e.target.value })} />
        </div>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div>
          <Label className="text-[10px]">Width (m)</Label>
          <Input
            type="number"
            step="0.05"
            min="0"
            max="100"
            className="h-7 text-xs"
            value={lane.width}
            onChange={(e) => onChange({ width: Math.max(0, Math.min(100, parseFloat(e.target.value) || 0)) })}
          />
        </div>
        <div>
          <Label className="text-[10px]">Speed limit (km/h)</Label>
          <Input
            type="number"
            step="5"
            min="0"
            className="h-7 text-xs"
            value={lane.speedLimit}
            onChange={(e) => onChange({ speedLimit: Math.max(0, parseInt(e.target.value, 10) || 0) })}
          />
        </div>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div>
          <Label className="text-[10px]">Circulation way</Label>
          <Select value={lane.circulation} onValueChange={(v) => onChange({ circulation: v as CirculationWay })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {Object.entries(CIRCULATION_META).map(([k, m]) => (
                <SelectItem key={k} value={k}>{m.symbol} {m.label}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div>
          <Label className="text-[10px]">Marking</Label>
          <Select value={lane.marking ?? 'solid'} onValueChange={(v) => onChange({ marking: v as LaneMarking })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {Object.entries(MARKING_META).map(([k, m]) => (
                <SelectItem key={k} value={k}>{m.label}</SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
      </div>
      <div>
        <Label className="text-[10px]">Vehicles authorization</Label>
        <div className="mt-1 grid grid-cols-2 gap-1">
          {Object.entries(VEHICLE_META).map(([key, meta]) => {
            const checked = lane.vehicles.includes(key as VehicleCategory) || lane.vehicles.includes('all')
            return (
              <label key={key} className="flex items-center gap-1.5 rounded border border-border/40 px-2 py-1 text-[11px]">
                <input
                  type="checkbox"
                  checked={checked}
                  onChange={(e) => {
                    if (key === 'all') {
                      onChange({ vehicles: e.target.checked ? ['all'] : [] })
                      return
                    }
                    const set = new Set(lane.vehicles.filter((v) => v !== 'all'))
                    if (e.target.checked) set.add(key as VehicleCategory)
                    else set.delete(key as VehicleCategory)
                    onChange({ vehicles: Array.from(set) })
                  }}
                />
                <span>{meta.icon}</span>
                <span>{meta.label}</span>
              </label>
            )
          })}
        </div>
      </div>
      <div className="grid grid-cols-2 gap-2 border-t border-border/40 pt-2">
        <div>
          <Label className="text-[10px]">Inner border height (m)</Label>
          <Input
            type="number"
            step="0.01"
            min="0"
            className="h-7 text-xs"
            value={lane.borderLeftHeight ?? 0}
            onChange={(e) => onChange({ borderLeftHeight: Math.max(0, parseFloat(e.target.value) || 0) })}
          />
        </div>
        <div>
          <Label className="text-[10px]">Outer border height (m)</Label>
          <Input
            type="number"
            step="0.01"
            min="0"
            className="h-7 text-xs"
            value={lane.borderRightHeight ?? 0}
            onChange={(e) => onChange({ borderRightHeight: Math.max(0, parseFloat(e.target.value) || 0) })}
          />
        </div>
      </div>
    </div>
  )
}
