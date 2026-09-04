import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { clampNumber } from '../tooling'
import type { RoadData } from '../../state/store'

export interface TrainTabProps {
  selectedRoad: RoadData | null
  onUpdateRoad: (patch: Partial<RoadData>) => void
}

/**
 * Railway track parameters for the selected track (Train workspace).
 * Defaults follow the Track Mesh Builder: gauge 1.435 m, rail 0.075 m,
 * trackbed 3.0 m.
 */
export default function TrainTab({ selectedRoad, onUpdateRoad }: TrainTabProps) {
  if (!selectedRoad) {
    return (
      <p className="text-xs text-muted-foreground">
        Select a track (or draw one with Insert Segment / Circle Arc / Clothoid Arc) to edit its railway parameters.
      </p>
    )
  }
  const railway = selectedRoad.railway
  if (!railway) {
    return (
      <p className="text-xs text-muted-foreground">
        {selectedRoad.name} is a road, not a railway track. Select it from the Tracks list.
      </p>
    )
  }
  const patch = (p: Partial<RoadData['railway']>) =>
    onUpdateRoad({ railway: { ...railway, ...p } })

  return (
    <div className="grid gap-3">
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Railway Track</h3>
      <div className="grid grid-cols-2 gap-2">
        <div className="grid gap-1.5">
          <Label htmlFor="rail-gauge">Gauge (m)</Label>
          <Input id="rail-gauge" type="number" min={0.4} max={2} step={0.005} value={railway.gauge}
            onChange={(e) => patch({ gauge: clampNumber(e.target.value, 0.4, 2, 1.435) })} />
        </div>
        <div className="grid gap-1.5">
          <Label htmlFor="rail-size">Rail size (m)</Label>
          <Input id="rail-size" type="number" min={0.03} max={0.2} step={0.005} value={railway.railSize}
            onChange={(e) => patch({ railSize: clampNumber(e.target.value, 0.03, 0.2, 0.075) })} />
        </div>
      </div>
      <div className="grid gap-1.5">
        <Label htmlFor="rail-trackbed">Trackbed width (m)</Label>
        <Input id="rail-trackbed" type="number" min={1.5} max={8} step={0.1} value={railway.trackbedWidth}
          onChange={(e) => patch({ trackbedWidth: clampNumber(e.target.value, 1.5, 8, 3) })} />
      </div>
      <div className="grid gap-1.5">
        <Label htmlFor="rail-sleeper">Sleeper spacing (m)</Label>
        <Input id="rail-sleeper" type="number" min={0.3} max={1.2} step={0.05} value={railway.sleeperSpacing}
          onChange={(e) => patch({ sleeperSpacing: clampNumber(e.target.value, 0.3, 1.2, 0.65) })} />
      </div>
      <p className="text-[11px] leading-relaxed text-muted-foreground">
        Rails sit at ±(gauge/2 + rail size/2) from the axis. Stick the track to Background Terrain
        (right-click) to pick altitude and cant (superelevation) from the terrain, like the
        frog/turnout pipelines do for network meshes.
      </p>
    </div>
  )
}
