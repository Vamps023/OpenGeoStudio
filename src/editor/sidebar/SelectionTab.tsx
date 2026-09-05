import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Separator } from '@/components/ui/separator'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import SelectionPanel from '../../roads/SelectionPanel'
import type { SelectionPanelProps } from '../../roads/SelectionPanel'
import { maxAbsGrade } from '../../engine/elevation'
import { clampInt, clampNumber } from '../tooling'
import type { EditorConfig, GeoReference, RoadData } from '../../state/store'

const PROFILE_PRESETS = ['Default', 'Highway', 'Urban', 'Rural', 'Bridge', 'Tunnel', 'Roundabout']

export interface SelectionTabProps extends SelectionPanelProps {
  config: EditorConfig
  geoRef?: GeoReference
  onConfigChange: (patch: Partial<EditorConfig>) => void
  onUpdateRoad: (patch: Partial<RoadData>) => void
  onGeoRefChange: (geo: GeoReference) => void
}

/** Selection tab: selection panel + road properties + Model/New Road parameters + Map Location. */
export default function SelectionTab(props: SelectionTabProps) {
  const { config, geoRef, onConfigChange, onUpdateRoad, onGeoRefChange, ...panelProps } = props
  const selectedRoad = props.selectedRoad
  return (
    <>
      <SelectionPanel {...panelProps} />

      <Separator />

      {/* Road properties for the selected road */}
      {selectedRoad ? (
        <div className="grid gap-3">
          <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Road Properties</h3>
          <div className="grid grid-cols-2 gap-2">
            <div className="grid gap-1.5">
              <Label htmlFor="road-id">Road ID</Label>
              <Input id="road-id" className="text-xs text-muted-foreground" value={selectedRoad.id} readOnly />
            </div>
            <div className="grid gap-1.5">
              <Label htmlFor="road-length">Length (m)</Label>
              <Input id="road-length" className="text-xs text-muted-foreground" value={props.roadLength.toFixed(1)} readOnly />
            </div>
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="road-name">Road name</Label>
            <Input id="road-name" value={selectedRoad.name}
              onChange={(event) => onUpdateRoad({ name: event.target.value })} />
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="road-profile">Road profile / style</Label>
            <Select value={selectedRoad.profileName ?? 'Default'} onValueChange={(v) => onUpdateRoad({ profileName: v === 'Default' ? undefined : v })}>
              <SelectTrigger id="road-profile" className="h-8 text-xs"><SelectValue /></SelectTrigger>
              <SelectContent>
                {PROFILE_PRESETS.map((p) => <SelectItem key={p} value={p}>{p}</SelectItem>)}
              </SelectContent>
            </Select>
          </div>
        </div>
      ) : null}

      <Separator />

      {/* Model parameters for the selected road (lanes etc.) */}
      {selectedRoad ? (
        <div className="grid gap-3">
          <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Model</h3>
          <div className="grid grid-cols-2 gap-2">
            <div className="grid gap-1.5">
              <Label htmlFor="lanes-left">Lanes left</Label>
              <Input id="lanes-left" type="number" min={0} max={6} value={selectedRoad.lanesLeft}
                onChange={(event) => onUpdateRoad({ lanesLeft: clampInt(event.target.value, 0, 6, 0) })} />
            </div>
            <div className="grid gap-1.5">
              <Label htmlFor="lanes-right">Lanes right</Label>
              <Input id="lanes-right" type="number" min={0} max={6} value={selectedRoad.lanesRight}
                onChange={(event) => onUpdateRoad({ lanesRight: clampInt(event.target.value, 0, 6, 0) })} />
            </div>
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="lane-width">Lane width (m)</Label>
            <Input id="lane-width" type="number" min={2} max={5} step={0.25} value={selectedRoad.laneWidth}
              onChange={(event) => onUpdateRoad({ laneWidth: clampNumber(event.target.value, 2, 5, 3.5) })} />
          </div>
          {selectedRoad.elevationProfile && selectedRoad.elevationProfile.length >= 2 && (
            <p className="text-[11px] text-muted-foreground">
              Max grade: <b className="font-medium text-foreground">{(maxAbsGrade(selectedRoad.elevationProfile, props.roadLength) * 100).toFixed(1)}%</b>
            </p>
          )}
          {selectedRoad.functions && selectedRoad.functions.length > 0 && (
            <p className="text-[11px] text-muted-foreground">
              Functions: <b className="font-medium text-foreground">{selectedRoad.functions.length}</b> ·
              Portions: <b className="font-medium text-foreground">{selectedRoad.portions?.length ?? 0}</b>
            </p>
          )}
        </div>
      ) : (
        <div className="grid gap-3">
          <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">New Road</h3>
          <div className="grid grid-cols-2 gap-2">
            <div className="grid gap-1.5">
              <Label htmlFor="new-lanes-left">Lanes left</Label>
              <Input id="new-lanes-left" type="number" min={0} max={6} value={config.lanesLeft}
                onChange={(event) => onConfigChange({ lanesLeft: clampInt(event.target.value, 0, 6, 1) })} />
            </div>
            <div className="grid gap-1.5">
              <Label htmlFor="new-lanes-right">Lanes right</Label>
              <Input id="new-lanes-right" type="number" min={0} max={6} value={config.lanesRight}
                onChange={(event) => onConfigChange({ lanesRight: clampInt(event.target.value, 0, 6, 1) })} />
            </div>
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="new-lane-width">Lane width (m)</Label>
            <Input id="new-lane-width" type="number" min={2} max={5} step={0.25} value={config.laneWidth}
              onChange={(event) => onConfigChange({ laneWidth: clampNumber(event.target.value, 2, 5, 3.5) })} />
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="new-fillet">Corner radius (m)</Label>
            <Input id="new-fillet" type="number" min={5} max={300} step={5} value={config.filletRadius}
              onChange={(event) => onConfigChange({ filletRadius: clampNumber(event.target.value, 5, 300, 50) })} />
          </div>
        </div>
      )}

      <Separator />

      {/* Geographic Reference — for map background alignment */}
      <div className="grid gap-3">
        <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Map Location</h3>
        <p className="text-[11px] leading-relaxed text-muted-foreground">
          Set the geographic center so the satellite map aligns with your roads. The origin (0,0) in world space maps to this lat/lng.
        </p>
        <div className="grid grid-cols-2 gap-2">
          <div className="grid gap-1.5">
            <Label htmlFor="geo-lng">Longitude</Label>
            <Input id="geo-lng" type="number" step={0.0001} value={geoRef?.lng ?? -95.36}
              onChange={(event) => onGeoRefChange({ lng: parseFloat(event.target.value) || 0, lat: geoRef?.lat ?? 29.76, scale: geoRef?.scale ?? 1 })} />
          </div>
          <div className="grid gap-1.5">
            <Label htmlFor="geo-lat">Latitude</Label>
            <Input id="geo-lat" type="number" step={0.0001} value={geoRef?.lat ?? 29.76}
              onChange={(event) => onGeoRefChange({ lng: geoRef?.lng ?? -95.36, lat: parseFloat(event.target.value) || 0, scale: geoRef?.scale ?? 1 })} />
          </div>
        </div>
        <div className="grid gap-1.5">
          <Label htmlFor="geo-scale">Scale (meters per world unit)</Label>
          <Input id="geo-scale" type="number" step={0.1} min={0.1} value={geoRef?.scale ?? 1}
            onChange={(event) => onGeoRefChange({ lng: geoRef?.lng ?? -95.36, lat: geoRef?.lat ?? 29.76, scale: parseFloat(event.target.value) || 1 })} />
        </div>
      </div>
    </>
  )
}
