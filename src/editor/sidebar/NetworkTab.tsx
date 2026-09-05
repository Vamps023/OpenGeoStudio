import { useState } from 'react'
import { Plus, RefreshCw, Trash2 } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Separator } from '@/components/ui/separator'
import type { AutoJunction } from '../tooling'
import type { IntersectionData } from '../../engine/intersections'
import type { ConnectingRoad, JunctionConfiguration, JunctionLaneConnection } from '../../engine/junctions'
import type { LayerFlags, RoadData } from '../../state/store'

export interface NetworkTabProps {
  intersections: IntersectionData[]
  junctions: AutoJunction[]
  roads: RoadData[]
  activeJunctionCount: number
  layers: LayerFlags
  selectedIntersectionId: string | null
  selectedJunctionKey: string | null
  selectedConnectionKey: string | null
  selectedRoad: RoadData | null
  selectedRoadLength: number
  onSelectIntersection: (id: string) => void
  onDeleteIntersection: (id: string) => void
  onSelectJunction: (junction: AutoJunction) => void
  onUpdateJunction: (key: string, patch: Partial<JunctionConfiguration>) => void
  onFocusConnection: (key: string | null) => void
  onToggleJunction: (junction: AutoJunction) => void
  onRegenerateJunctions: () => void
  onSetLayer: (key: keyof LayerFlags, value: boolean) => void
}

const LAYER_ROWS: [keyof LayerFlags, string][] = [
  ['roadLogicalContent', 'Road axes'],
  ['road3dGeneration', 'Road surfaces and markings'],
  ['intersectionLogicalContent', 'Junction nodes and boundaries'],
  ['intersection3dGeneration', 'Junction surfaces and markings'],
  ['wayAxis', 'Connection paths and arrows'],
  ['wayLogicalContents', 'Lane movement ribbons'],
  ['otherSubNetworks', 'Sub-network exits'],
]

export function connectionRow(connection: ConnectingRoad): JunctionLaneConnection | null {
  const lane = connection.laneLinks[0]
  if (!lane || !connection.fromContact || !connection.toContact) return null
  return { ...lane, fromContact: connection.fromContact, toContact: connection.toContact, enabled: connection.authorized !== false }
}

export function junctionConnectionKey(row: JunctionLaneConnection): string {
  return JSON.stringify([row.fromRoadId, row.fromContact, row.fromLaneId, row.toRoadId, row.toContact, row.toLaneId])
}

export function junctionConnectionRows(junction: AutoJunction): JunctionLaneConnection[] {
  return junction.configuration?.connections ?? (junction.connectionOptions ?? junction.connectingRoads)
    .map(connectionRow).filter((row): row is JunctionLaneConnection => row !== null)
}

/** Network tab: intersections, auto junctions, layers and the lanes editor. */
export default function NetworkTab(props: NetworkTabProps) {
  const {
    intersections, junctions, roads, activeJunctionCount, layers, selectedIntersectionId,
    selectedJunctionKey, selectedRoad, selectedRoadLength, onSelectIntersection,
    onDeleteIntersection, onToggleJunction, onRegenerateJunctions, onSetLayer,
  } = props
  const selectedJunction = junctions.find((junction) => (junction.configurationKey ?? junction.key) === selectedJunctionKey)
  return (
    <>
      <div className="flex items-center justify-between gap-2">
        <Badge variant="muted">{activeJunctionCount} junctions</Badge>
        <Button size="sm" variant="outline" onClick={onRegenerateJunctions}>
          <RefreshCw className="size-3.5" /> Restore detached
        </Button>
      </div>

      <h3 className="mt-2 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Detected junctions</h3>
      {junctions.length === 0 ? (
        <p className="text-xs text-muted-foreground">Draw crossing roads to create a junction. Select its node on the canvas or choose Configure below.</p>
      ) : (
        <ul className="grid max-h-48 gap-1.5 overflow-y-auto">
          {junctions.map((junction) => (
            <li key={junction.configurationKey ?? junction.id} className={`flex items-center gap-2 rounded-lg border px-3 py-2 ${junction === selectedJunction ? 'border-primary bg-primary/10' : 'border-border bg-muted/40'}`}>
              <button type="button" className="grid min-w-0 flex-1 gap-0.5 text-left text-xs" onClick={() => props.onSelectJunction(junction)} aria-label={`Configure ${junction.configuration?.name || junction.id}`}>
                <span className="truncate font-medium">{junction.configuration?.name || junction.id}</span>
                <span className="text-muted-foreground">{junction.approaches.length} approaches · {junction.connectingRoads.length} movements</span>
                <span className="text-primary">Configure</span>
              </button>
              <Button size="sm" variant={junction.suppressed ? 'default' : 'ghost'} className="h-7 px-2 text-xs" onClick={() => onToggleJunction(junction)}>
                {junction.suppressed ? 'Create' : 'Detach'}
              </Button>
            </li>
          ))}
        </ul>
      )}

      {selectedJunction && <JunctionInspector key={selectedJunction.configurationKey ?? selectedJunction.key} junction={selectedJunction} roads={roads} onUpdate={props.onUpdateJunction} selectedConnectionKey={props.selectedConnectionKey} onFocusConnection={props.onFocusConnection} />}

      <Separator className="my-2" />
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Explicit intersections</h3>
      {intersections.length === 0 ? (
        <p className="text-xs text-muted-foreground">For manual track linking, use Insert Intersection or Detect Intersection. Select an explicit node to edit its authorizations and contours.</p>
      ) : (
        <ul className="grid gap-1.5">
          {intersections.map((node) => (
            <li key={node.id} className="flex items-center gap-2 rounded-lg border border-border bg-muted/40 px-3 py-2">
              <button type="button" className={`grid min-w-0 flex-1 gap-0.5 text-left text-xs ${selectedIntersectionId === node.id ? 'text-primary' : ''}`} onClick={() => onSelectIntersection(node.id)}>
                <span className="truncate font-medium">{node.id}</span>
                <span className="text-muted-foreground">{node.trackEnds.length} tracks · {Object.values(node.authorizations).filter((v) => !v).length} denied</span>
              </button>
              <Button size="sm" variant="ghost" className="h-7 px-2 text-xs" aria-label={`Delete ${node.id}`} onClick={() => onDeleteIntersection(node.id)}><Trash2 className="size-3.5" /></Button>
            </li>
          ))}
        </ul>
      )}

      <Separator className="my-2" />
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Visualization</h3>
      <div className="flex flex-wrap gap-3 text-[11px] text-muted-foreground">
        <span className="text-emerald-400">Allowed</span><span className="text-red-400">Closed</span><span className="text-cyan-400">Selected movement</span>
      </div>
      <div className="grid gap-1.5">
        {LAYER_ROWS.map(([key, label]) => (
          <label key={key} className="flex items-center gap-2 text-xs">
            <input type="checkbox" checked={layers[key]} onChange={(e) => onSetLayer(key, e.target.checked)} />{label}
          </label>
        ))}
      </div>

      {selectedRoad && (
        <>
          <Separator className="my-2" />
          <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Selected road: {selectedRoad.name}</h3>
          <p className="text-xs text-muted-foreground">Switch to the Lanes tab to edit lane widths, types, direction, markings, and cross section for {selectedRoad.name}.</p>
        </>
      )}
    </>
  )
}

export function JunctionInspector({ junction, roads, onUpdate, selectedConnectionKey, onFocusConnection }: {
  junction: AutoJunction
  roads: RoadData[]
  onUpdate: (key: string, patch: Partial<JunctionConfiguration>) => void
  selectedConnectionKey: string | null
  onFocusConnection: (key: string | null) => void
}) {
  const key = junction.configurationKey ?? junction.key
  const configuration = junction.configuration ?? {}
  const rows = junctionConnectionRows(junction)
  const roadName = (id: string) => roads.find((road) => road.id === id)?.name ?? id
  const options = (direction: 'incomingLanes' | 'outgoingLanes') => junction.approaches.flatMap((approach) =>
    (approach[direction] ?? []).map((lane) => ({
      value: JSON.stringify([approach.roadId, approach.contact, lane.laneId]),
      roadId: approach.roadId, contact: approach.contact, laneId: lane.laneId,
      label: `${roadName(approach.roadId)} · ${approach.contact} · ${lane.laneId > 0 ? '+' : ''}${lane.laneId} ${lane.name}`,
    })))
  const incoming = options('incomingLanes')
  const outgoing = options('outgoingLanes')
  const [fromValue, setFromValue] = useState('')
  const [toValue, setToValue] = useState('')
  const from = incoming.find((option) => option.value === fromValue) ?? incoming[0]
  const to = outgoing.find((option) => option.value === toValue) ?? outgoing.find((option) => option.roadId !== from?.roadId || option.contact !== from?.contact) ?? outgoing[0]
  const draft: JunctionLaneConnection | null = from && to ? { fromRoadId: from.roadId, fromContact: from.contact, fromLaneId: from.laneId, toRoadId: to.roadId, toContact: to.contact, toLaneId: to.laneId, enabled: true } : null
  const duplicate = !!draft && rows.some((row) => junctionConnectionKey(row) === junctionConnectionKey(draft))
  const setRows = (connections: JunctionLaneConnection[]) => onUpdate(key, { connections })
  const patchRow = (index: number, patch: Partial<JunctionLaneConnection>) => setRows(rows.map((row, i) => i === index ? { ...row, ...patch } : row))
  const selectClass = 'h-8 w-full min-w-0 rounded-md border border-border bg-background px-2 text-xs'
  const choice = (list: typeof incoming, value: string, onChange: (value: string) => void, label: string) => (
    <select aria-label={label} className={selectClass} value={value} onChange={(event) => onChange(event.target.value)}>
      {!list.some((option) => option.value === value) && <option value={value}>Unavailable lane — choose another</option>}
      {list.map((option) => <option key={option.value} value={option.value}>{option.label}</option>)}
    </select>
  )
  const changeEndpoint = (index: number, direction: 'from' | 'to', value: string) => {
    const option = (direction === 'from' ? incoming : outgoing).find((item) => item.value === value)
    if (!option) return
    patchRow(index, direction === 'from' ? { fromRoadId: option.roadId, fromContact: option.contact, fromLaneId: option.laneId } : { toRoadId: option.roadId, toContact: option.contact, toLaneId: option.laneId })
    onFocusConnection(null)
  }
  return (
    <section className="grid gap-3 rounded-lg border border-primary/40 bg-muted/20 p-3" aria-label="Junction configuration">
      <div className="flex items-center justify-between gap-2">
        <h3 className="text-xs font-semibold">Junction configuration</h3>
        <Badge variant="muted">{configuration.connections === undefined ? 'Automatic' : 'Custom'}</Badge>
      </div>
      <label className="grid gap-1 text-xs">Name<Input value={configuration.name ?? ''} placeholder={junction.id} onChange={(event) => onUpdate(key, { name: event.target.value })} /></label>
      <label className="flex items-center gap-2 text-xs"><input type="checkbox" checked={configuration.markings !== false} onChange={(event) => onUpdate(key, { markings: event.target.checked })} />Show junction road markings</label>
      <div className="grid gap-1 text-[11px] text-muted-foreground">
        <span>{incoming.length} entering lanes · {outgoing.length} leaving lanes</span>
        <span>Lane IDs: positive = left side, negative = right side. Contact identifies the road approach.</span>
        <span>Settings are saved with the project and support undo/redo. XODR export of configured junctions is not yet supported.</span>
      </div>
      <details className="rounded border border-border p-2 text-xs">
        <summary className="cursor-pointer font-medium">Approaches ({junction.approaches.length})</summary>
        <ul className="mt-2 grid gap-1.5">{junction.approaches.map((approach) => <li key={`${approach.roadId}:${approach.contact}`}><b>{roadName(approach.roadId)}</b> · {approach.contact}<div className="text-muted-foreground">{approach.incomingLanes?.length ?? 0} in / {approach.outgoingLanes?.length ?? 0} out · station {approach.station.toFixed(1)} m</div></li>)}</ul>
      </details>
      {junction.suppressed && <p className="text-xs text-amber-400">This junction is detached. Choose Create to restore its geometry and configure movements.</p>}
      {!!junction.configurationWarnings?.length && <ul role="alert" className="grid gap-1 rounded border border-amber-500/40 p-2 text-xs text-amber-400">{junction.configurationWarnings.map((warning, index) => <li key={index}>{warning}</li>)}</ul>}
      <fieldset disabled={junction.suppressed} className="grid min-w-0 gap-3 disabled:opacity-50">
        <legend className="mb-2 text-xs font-semibold">Lane-to-lane movements</legend>
        <div className="flex flex-wrap gap-1.5">
          <Button size="sm" variant="outline" className="h-7 text-xs" disabled={!rows.length} onClick={() => setRows(rows.map((row) => ({ ...row, enabled: true })))}>Allow all</Button>
          <Button size="sm" variant="outline" className="h-7 text-xs" disabled={!rows.length} onClick={() => setRows(rows.map((row) => ({ ...row, enabled: false })))}>Close all</Button>
          <Button size="sm" variant="outline" className="h-7 text-xs" onClick={() => { onUpdate(key, { connections: undefined }); onFocusConnection(null) }}>Reset automatic</Button>
        </div>
        {rows.length === 0 && <p className="text-xs text-muted-foreground">No lane movements. Add a connection below or reset to automatic routing.</p>}
        <div className="grid max-h-[28rem] gap-2 overflow-y-auto">
          {rows.map((row, index) => {
            const rowKey = junctionConnectionKey(row)
            const focused = rowKey === selectedConnectionKey
            return (
              <div key={index} className={`grid gap-1.5 rounded border p-2 ${focused ? 'border-cyan-400 bg-cyan-500/10' : 'border-border bg-background/40'}`}>
                <div className="flex items-center gap-2">
                  <label className="flex items-center gap-1.5 text-xs"><input type="checkbox" checked={row.enabled} onChange={(event) => patchRow(index, { enabled: event.target.checked })} />{row.enabled ? 'Allowed' : 'Closed'}</label>
                  <Button size="sm" variant="ghost" className="ml-auto h-6 px-2 text-[11px]" aria-pressed={focused} onClick={() => onFocusConnection(focused ? null : rowKey)}>Highlight</Button>
                  <Button size="sm" variant="ghost" className="h-6 px-1.5" aria-label={`Remove movement ${index + 1}`} onClick={() => { setRows(rows.filter((_, i) => i !== index)); onFocusConnection(null) }}><Trash2 className="size-3" /></Button>
                </div>
                <span className="text-[10px] text-muted-foreground">From entering lane</span>
                {choice(incoming, JSON.stringify([row.fromRoadId, row.fromContact, row.fromLaneId]), (value) => changeEndpoint(index, 'from', value), `Movement ${index + 1} entering lane`)}
                <span className="text-[10px] text-muted-foreground">To leaving lane</span>
                {choice(outgoing, JSON.stringify([row.toRoadId, row.toContact, row.toLaneId]), (value) => changeEndpoint(index, 'to', value), `Movement ${index + 1} leaving lane`)}
              </div>
            )
          })}
        </div>
        <div className="grid gap-2 rounded border border-dashed border-border p-2">
          <span className="text-xs font-medium">Add lane connection</span>
          {choice(incoming, from?.value ?? '', setFromValue, 'New connection entering lane')}
          {choice(outgoing, to?.value ?? '', setToValue, 'New connection leaving lane')}
          <Button size="sm" variant="outline" disabled={!draft || duplicate} onClick={() => { if (draft) { setRows([...rows, draft]); onFocusConnection(junctionConnectionKey(draft)) } }}><Plus className="size-3.5" />{duplicate ? 'Connection already exists' : 'Add connection'}</Button>
        </div>
      </fieldset>
    </section>
  )
}
