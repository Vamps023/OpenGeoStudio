// ─────────────────────────────────────────────────────────────────────
// LaneSelectionPanel
// Per-lane properties editor used in the Lanes Tab cross-section view.
// Mirrors the SCANeR SELECTION dock-widget for a single lane.
// ─────────────────────────────────────────────────────────────────────

import { ArrowDown, ArrowUp, Copy, Trash2 } from 'lucide-react'
import type { LaneDef, LaneType, CirculationWay, VehicleCategory, LaneMarking } from '../engine/laneTypes'
import {
  LANE_TYPE_META,
  VEHICLE_META,
  MARKING_META,
  ALL_VEHICLES,
  ALL_CIRCULATIONS,
  ALL_MARKINGS,
  ALL_LANE_TYPES,
} from '../engine/laneMetadata'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Badge } from '@/components/ui/badge'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'
import { circulationLabel } from './LanePropertiesEditor'
import { MarkingStyleEditor } from './LanesPanel'

interface LaneSelectionPanelProps {
  lane: LaneDef
  side: 'left' | 'right'
  index: number
  onChange: (patch: Partial<LaneDef>) => void
  onRemove: () => void
  onMove: (dir: -1 | 1) => void
  onDuplicate: () => void
  canMoveUp: boolean
  canMoveDown: boolean
}

export default function LaneSelectionPanel({
  lane, side, index, onChange, onRemove, onMove, onDuplicate, canMoveUp, canMoveDown,
}: LaneSelectionPanelProps) {
  const meta = LANE_TYPE_META[lane.type]
  const toggleVehicle = (vehicle: VehicleCategory, checked: boolean) => {
    const vehicles = checked
      ? [...lane.vehicles, vehicle]
      : lane.vehicles.filter((v) => v !== vehicle)
    onChange({ vehicles })
  }
  return (
    <div className="space-y-2 rounded border border-border bg-card/40 p-3">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span className="inline-block h-3 w-3 rounded" style={{ background: meta.color }} />
          <span className="text-sm font-semibold">
            {lane.name ?? meta.label} · {side} #{index}
          </span>
          <Badge variant="outline" className="text-[10px]">{meta.label}</Badge>
        </div>
        <div className="flex items-center gap-1">
          <Button size="icon" variant="ghost" disabled={!canMoveUp} onClick={() => onMove(-1)}>
            <ArrowUp className="h-3 w-3" />
          </Button>
          <Button size="icon" variant="ghost" disabled={!canMoveDown} onClick={() => onMove(1)}>
            <ArrowDown className="h-3 w-3" />
          </Button>
          <Button size="icon" variant="ghost" onClick={onDuplicate}>
            <Copy className="h-3 w-3" />
          </Button>
          <Button size="icon" variant="ghost" onClick={onRemove}>
            <Trash2 className="h-3 w-3" />
          </Button>
        </div>
      </div>
      <Separator />
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Type</Label>
          <Select value={lane.type} onValueChange={(value) => onChange({ type: value as LaneType })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_LANE_TYPES.map((type) => (
                <SelectItem key={type} value={type} className="text-xs">
                  {LANE_TYPE_META[type].label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Width (m)</Label>
          <Input
            type="number"
            min={0.05}
            step={0.05}
            className="h-7 text-xs"
            value={lane.width}
            onChange={(e) => onChange({ width: Math.max(0.05, Number.parseFloat(e.target.value) || 0) })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Name</Label>
          <Input
            className="h-7 text-xs"
            value={lane.name ?? ''}
            placeholder={meta.label}
            onChange={(e) => onChange({ name: e.target.value })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Speed limit (km/h)</Label>
          <Input
            type="number"
            min={0}
            step={5}
            className="h-7 text-xs"
            value={lane.speedLimit}
            onChange={(e) => onChange({ speedLimit: Math.max(0, Number.parseFloat(e.target.value) || 0) })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Circulation direction</Label>
          <Select value={lane.circulation} onValueChange={(value) => onChange({ circulation: value as CirculationWay })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_CIRCULATIONS.map((way) => (
                <SelectItem key={way} value={way} className="text-xs">
                  {circulationLabel(way, side)}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Outer boundary marking</Label>
          <Select value={lane.marking ?? 'solid'} onValueChange={(value) => onChange({ marking: value as LaneMarking })}>
            <SelectTrigger className="h-7 text-xs"><SelectValue /></SelectTrigger>
            <SelectContent>
              {ALL_MARKINGS.map((marking) => (
                <SelectItem key={marking} value={marking} className="text-xs">
                  {MARKING_META[marking].label}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>
      </div>
      <p className="text-[11px] text-muted-foreground">Forward follows the normal side direction: left toward road start, right toward road end. Backward reverses it; Both is bidirectional.</p>
      <p className="text-[11px] text-muted-foreground">Marking controls the outer edge away from the road center. Double patterns are ordered inner stripe, then outer stripe; the green center line is unchanged.</p>
      <MarkingStyleEditor lane={lane} onChange={onChange} />
      <div className="space-y-1">
        <Label className="text-[11px] text-muted-foreground">Allowed vehicles</Label>
        <div className="flex flex-wrap gap-x-3 gap-y-1">
          {ALL_VEHICLES.map((vehicle) => (
            <label key={vehicle} className="flex items-center gap-1 text-xs">
              <input
                type="checkbox"
                checked={lane.vehicles.includes(vehicle)}
                onChange={(e) => toggleVehicle(vehicle, e.target.checked)}
              />
              {VEHICLE_META[vehicle].icon} {VEHICLE_META[vehicle].label}
            </label>
          ))}
        </div>
      </div>
      <Separator />
      <div className="grid grid-cols-2 gap-2">
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Border left height (m)</Label>
          <Input
            type="number"
            step={0.01}
            className="h-7 text-xs"
            value={lane.borderLeftHeight ?? 0}
            onChange={(e) => onChange({ borderLeftHeight: Number.parseFloat(e.target.value) || 0 })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Border right height (m)</Label>
          <Input
            type="number"
            step={0.01}
            className="h-7 text-xs"
            value={lane.borderRightHeight ?? 0}
            onChange={(e) => onChange({ borderRightHeight: Number.parseFloat(e.target.value) || 0 })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Border left offset (m)</Label>
          <Input
            type="number"
            step={0.05}
            className="h-7 text-xs"
            value={lane.borderLeftOffset ?? 0}
            onChange={(e) => onChange({ borderLeftOffset: Number.parseFloat(e.target.value) || 0 })}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-[11px] text-muted-foreground">Border right offset (m)</Label>
          <Input
            type="number"
            step={0.05}
            className="h-7 text-xs"
            value={lane.borderRightOffset ?? 0}
            onChange={(e) => onChange({ borderRightOffset: Number.parseFloat(e.target.value) || 0 })}
          />
        </div>
      </div>
    </div>
  )
}
