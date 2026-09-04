// SELECTION dock-widget (SCANeR Roads tab): XY Function / Track /
// Intersection / Contour Handles tabs.
import type { Vec2 } from '../engine/types'
import type { RoadData, EditionConstraint } from '../state/store'
import type { IntersectionData, IntersectionWay } from '../engine/intersections'
import type { XYFunction, PolylineFunction } from '../engine/xyFunctions'
import { FUNCTION_LABELS, functionLength, supportsConstraints } from '../engine/xyFunctions'
import { trackSlices } from '../engine/tracks'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Separator } from '@/components/ui/separator'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'

export interface SelectionPanelProps {
  selectedRoad: RoadData | null
  selectedStation: number | null
  roadLength: number
  selectedIntersection: IntersectionData | null
  ways: IntersectionWay[]
  linkedTracks: { trackId: string; contact: 'start' | 'end' }[]
  roadsList: { id: string; name: string }[]
  lockedPassageways: string[]
  constraint: EditionConstraint
  onConstraintChange: (c: EditionConstraint) => void
  onFunctionsChange: (functions: XYFunction[]) => void
  onRenameRoad: (name: string) => void
  onSplitFunction: () => void
  onMergeFunctions: () => void
  onInvertOrientation: () => void
  onStickToTerrain: () => void
  onToggleExit: (contact: 'start' | 'end') => void
  onInsertHandle: () => void
  onUpdateIntersection: (patch: Partial<IntersectionData>) => void
  onToggleAuthorization: (key: string) => void
  onInvertAuthorizations: () => void
  onAddContourHandle: () => void
  onDeleteContourHandle: (index: number) => void
  onMoveContourHandle: (index: number, point: Vec2) => void
  onSetMainPath: (a: string, b: string) => void
  onLockPassageway: (key: string) => void
  onDeleteIntersection: () => void
  onUnlinkIntersection: () => void
}

export default function SelectionPanel(props: SelectionPanelProps) {
  const { selectedRoad, selectedIntersection } = props
  const hasTrack = !!selectedRoad
  const hasIntersection = !!selectedIntersection
  return (
    <Tabs defaultValue={hasTrack ? 'function' : hasIntersection ? 'intersection' : 'track'} className="grid gap-3">
      <TabsList className="grid w-full grid-cols-4">
        <TabsTrigger value="function" className="text-[11px]">XY Function</TabsTrigger>
        <TabsTrigger value="track" className="text-[11px]">Track</TabsTrigger>
        <TabsTrigger value="intersection" className="text-[11px]">Intersect.</TabsTrigger>
        <TabsTrigger value="contour" className="text-[11px]">Contours</TabsTrigger>
      </TabsList>
      <TabsContent value="function" className="grid gap-3">
        <XYFunctionTab {...props} />
      </TabsContent>
      <TabsContent value="track" className="grid gap-3">
        <TrackTab {...props} />
      </TabsContent>
      <TabsContent value="intersection" className="grid gap-3">
        <IntersectionTab {...props} />
      </TabsContent>
      <TabsContent value="contour" className="grid gap-3">
        <ContourHandlesTab {...props} />
      </TabsContent>
    </Tabs>
  )
}

// ─── XY Function tab ─────────────────────────────────────────────────
function XYFunctionTab(props: SelectionPanelProps) {
  const { selectedRoad, selectedStation } = props
  if (!selectedRoad || !selectedRoad.functions || selectedRoad.functions.length === 0) {
    return (
      <p className="text-xs text-muted-foreground">
        Select a track drawn with XY functions (segment, circle arc, clothoid arc, polyline, Bezier or ClothoidSpline).
      </p>
    )
  }
  const slices = trackSlices(selectedRoad)
  if (!slices) return null
  let active = slices[0]
  if (selectedStation !== null) {
    for (const slice of slices) {
      if (selectedStation >= slice.offset && selectedStation <= slice.offset + slice.length) {
        active = slice
        break
      }
    }
  }
  const fn = active.fn
  const index = active.index
  const replace = (next: XYFunction) => {
    const functions = [...selectedRoad.functions!]
    functions[index] = next
    props.onFunctionsChange(functions)
  }
  const numberField = (label: string, value: number, onChange: (v: number) => void, step = 1, min?: number) => (
    <div className="grid grid-cols-[1fr_5rem] items-center gap-2">
      <Label className="text-[11px] text-muted-foreground">{label}</Label>
      <Input
        type="number"
        step={step}
        min={min}
        value={Number.isFinite(value) ? Number(value.toFixed(3)) : 0}
        onChange={(e) => {
          const parsed = Number.parseFloat(e.target.value)
          if (Number.isFinite(parsed)) onChange(parsed)
        }}
        className="h-7 text-xs"
      />
    </div>
  )

  return (
    <div className="grid gap-3">
      <div className="flex items-center justify-between">
        <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
          XY function
        </h3>
        <Badge variant="muted">{FUNCTION_LABELS[fn.kind]}</Badge>
      </div>
      <div className="grid gap-1.5 rounded-lg border border-border bg-muted/40 p-3">
        {fn.kind === 'segment' && numberField('Length (m)', fn.length, (v) => replace({ ...fn, length: Math.max(0.01, v) }))}
        {fn.kind === 'arc' && (
          <>
            {numberField('Radius (m)', fn.radius, (v) => replace({ ...fn, radius: Math.max(0.01, v) }))}
            {numberField('Angle (deg)', (fn.angle * 180) / Math.PI, (v) => replace({ ...fn, angle: (v * Math.PI) / 180 }), 5)}
            <div className="flex items-center justify-between text-xs">
              <span className="text-muted-foreground">Length</span>
              <b className="font-medium">{functionLength(fn).toFixed(1)} m</b>
            </div>
          </>
        )}
        {fn.kind === 'clothoid' && (
          <>
            {numberField('Radius In (m, 0=∞)', fn.radiusIn, (v) => replace({ ...fn, radiusIn: Math.max(0, v) }))}
            {numberField('Radius Out (m, 0=∞)', fn.radiusOut, (v) => replace({ ...fn, radiusOut: Math.max(0, v) }))}
            {numberField('Length (m)', fn.length, (v) => replace({ ...fn, length: Math.max(0.01, v) }))}
          </>
        )}
        {fn.kind === 'polyline' && (
          <>
            <div className="flex items-center justify-between text-xs">
              <span className="text-muted-foreground">Length</span>
              <b className="font-medium">{functionLength(fn).toFixed(1)} m</b>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-muted-foreground">Number of points</span>
              <b className="font-medium">{fn.points.length}</b>
            </div>
            <div className="grid gap-1.5">
              <Label className="text-[11px] text-muted-foreground">Polyline Spline</Label>
              <select
                className="h-7 rounded-md border border-border bg-background px-2 text-xs"
                value={fn.splineType}
                onChange={(e) => replace({ ...fn, splineType: e.target.value as PolylineFunction['splineType'] })}
              >
                <option value="segment">Segment</option>
                <option value="spline">Spline</option>
                <option value="bezier">Bezier</option>
              </select>
            </div>
          </>
        )}
        {fn.kind === 'bezier' && (
          <div className="grid gap-1 text-xs text-muted-foreground">
            <span>Parametric polynomial curve defined by four points.</span>
            <div className="flex items-center justify-between">
              <span className="text-muted-foreground">Length</span>
              <b className="font-medium text-foreground">{functionLength(fn).toFixed(1)} m</b>
            </div>
          </div>
        )}
        {fn.kind === 'clothoidSpline' && (
          <>
            <div className="flex items-center justify-between text-xs">
              <span className="text-muted-foreground">Length</span>
              <b className="font-medium">{functionLength(fn).toFixed(1)} m</b>
            </div>
            <div className="flex items-center justify-between text-xs">
              <span className="text-muted-foreground">Number of points</span>
              <b className="font-medium">{fn.points.length}</b>
            </div>
            {numberField('Tolerance (m)', fn.tolerance, (v) => replace({ ...fn, tolerance: Math.max(0.01, v) }), 0.1)}
            {numberField('Symmetry threshold', fn.symmetryThreshold, (v) => replace({ ...fn, symmetryThreshold: Math.max(0, v) }), 0.5)}
          </>
        )}
      </div>

      {/* Edition constrain (doc 5.5.4.2.8) */}
      {supportsConstraints(fn) && (
        <div className="grid gap-1.5">
          <Label className="text-[11px] text-muted-foreground">Edition constrain</Label>
          <div className="grid grid-cols-3 gap-1">
            {(['free', 'fixedRadius', 'fixedLength'] as EditionConstraint[]).map((c) => (
              <Button
                key={c}
                size="sm"
                variant={props.constraint === c ? 'default' : 'outline'}
                className="h-7 px-1 text-[10px]"
                onClick={() => props.onConstraintChange(c)}
              >
                {c === 'free' ? 'Free' : c === 'fixedRadius' ? 'Fixed Radius' : 'Fixed Length'}
              </Button>
            ))}
          </div>
        </div>
      )}

      <Separator />
      <div className="grid grid-cols-2 gap-1.5">
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onSplitFunction}>
          Split Function
        </Button>
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onMergeFunctions}>
          Merge Functions
        </Button>
        {(fn.kind === 'polyline' || fn.kind === 'clothoidSpline') && (
          <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onInsertHandle}>
            Insert Handle
          </Button>
        )}
      </div>
    </div>
  )
}

// ─── Track tab ───────────────────────────────────────────────────────
function TrackTab(props: SelectionPanelProps) {
  const { selectedRoad } = props
  if (!selectedRoad) {
    return <p className="text-xs text-muted-foreground">Select a track in the 3D view to see its length and position.</p>
  }
  const exits = selectedRoad.subNetworkExits ?? []
  const functionKinds = (selectedRoad.functions ?? []).map((fn) => FUNCTION_LABELS[fn.kind]).join(' → ') || 'Legacy control points'
  return (
    <div className="grid gap-3">
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Track</h3>
      <div className="grid gap-1.5">
        <Label className="text-[11px] text-muted-foreground">Name</Label>
        <Input
          className="h-7 text-xs"
          value={selectedRoad.name}
          onChange={(e) => props.onRenameRoad(e.target.value)}
        />
      </div>
      <div className="grid gap-1.5 rounded-lg border border-border bg-muted/40 p-3 text-xs">
        <div className="flex items-center justify-between">
          <span className="text-muted-foreground">Length</span>
          <b className="font-medium">{props.roadLength.toFixed(1)} m</b>
        </div>
        <div className="flex items-center justify-between">
          <span className="text-muted-foreground">Functions</span>
          <b className="font-medium">{selectedRoad.functions?.length ?? 0}</b>
        </div>
        {selectedRoad.bankingProfile && selectedRoad.bankingProfile.length > 0 && (
          <div className="flex items-center justify-between">
            <span className="text-muted-foreground">Max banking</span>
            <b className="font-medium">
              {(selectedRoad.bankingProfile.reduce((m, p) => Math.max(m, Math.abs((p.z * 180) / Math.PI)), 0)).toFixed(1)}°
            </b>
          </div>
        )}
        <div className="grid gap-0.5">
          <span className="text-muted-foreground">Sequence</span>
          <span className="leading-snug">{functionKinds}</span>
        </div>
      </div>

      <div className="grid grid-cols-2 gap-1.5">
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onInvertOrientation}>
          Invert Orientation
        </Button>
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onStickToTerrain}>
          Stick to Terrain
        </Button>
      </div>

      <Separator />
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">SubNetwork Exit</h3>
      <p className="text-[11px] leading-relaxed text-muted-foreground">
        Mark a track extremity so it can be connected to another sub network. An arrow indicates the exit.
      </p>
      <div className="grid grid-cols-2 gap-1.5">
        <Button
          size="sm"
          variant={exits.includes('start') ? 'default' : 'outline'}
          className="h-7 text-xs"
          onClick={() => props.onToggleExit('start')}
        >
          {exits.includes('start') ? '✓ Start Exit' : 'Mark Start Exit'}
        </Button>
        <Button
          size="sm"
          variant={exits.includes('end') ? 'default' : 'outline'}
          className="h-7 text-xs"
          onClick={() => props.onToggleExit('end')}
        >
          {exits.includes('end') ? '✓ End Exit' : 'Mark End Exit'}
        </Button>
      </div>
    </div>
  )
}

// ─── Intersection tab ────────────────────────────────────────────────
function IntersectionTab(props: SelectionPanelProps) {
  const { selectedIntersection: it } = props
  if (!it) {
    return <p className="text-xs text-muted-foreground">Select an intersection (yellow point) to edit its parameters and authorizations.</p>
  }
  const authorizedCount = props.ways.filter((w) => w.authorized).length
  return (
    <div className="grid gap-3">
      <div className="flex items-center justify-between">
        <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Intersection</h3>
        <Badge variant="muted">{it.trackEnds.length} tracks</Badge>
      </div>

      <div className="grid gap-1.5 rounded-lg border border-border bg-muted/40 p-3">
        <div className="grid gap-1.5">
          <Label className="text-[11px] text-muted-foreground">Ground name</Label>
          <Input
            className="h-7 text-xs"
            value={it.groundName}
            onChange={(e) => props.onUpdateIntersection({ groundName: e.target.value })}
          />
        </div>
        <label className="flex items-center gap-2 text-xs">
          <input
            type="checkbox"
            checked={it.setSpecificMaterial}
            onChange={(e) => props.onUpdateIntersection({ setSpecificMaterial: e.target.checked })}
          />
          Set specific material
        </label>
        {it.setSpecificMaterial && (
          <div className="grid gap-1.5">
            <Label className="text-[11px] text-muted-foreground">Material name</Label>
            <Input
              className="h-7 text-xs"
              value={it.materialName}
              onChange={(e) => props.onUpdateIntersection({ materialName: e.target.value })}
            />
          </div>
        )}
        <div className="grid grid-cols-2 gap-2">
          {(['x', 'y'] as const).map((axis) => (
            <div key={axis} className="grid gap-1">
              <Label className="text-[11px] text-muted-foreground">Uv{axis}</Label>
              <Input
                type="number"
                step={0.1}
                className="h-7 text-xs"
                value={it.uv[axis]}
                onChange={(e) => props.onUpdateIntersection({ uv: { ...it.uv, [axis]: Number.parseFloat(e.target.value) || 1 } })}
              />
            </div>
          ))}
          {(['offsetX', 'offsetY'] as const).map((axis) => (
            <div key={axis} className="grid gap-1">
              <Label className="text-[11px] text-muted-foreground">Offset {axis.slice(-1)}</Label>
              <Input
                type="number"
                step={0.1}
                className="h-7 text-xs"
                value={it.uv[axis]}
                onChange={(e) => props.onUpdateIntersection({ uv: { ...it.uv, [axis]: Number.parseFloat(e.target.value) || 0 } })}
              />
            </div>
          ))}
        </div>
        <div className="grid gap-1">
          <Label className="text-[11px] text-muted-foreground">Heading (texture rotation, deg)</Label>
          <Input
            type="number"
            step={5}
            className="h-7 text-xs"
            value={(it.uv.heading * 180) / Math.PI}
            onChange={(e) => props.onUpdateIntersection({ uv: { ...it.uv, heading: ((Number.parseFloat(e.target.value) || 0) * Math.PI) / 180 } })}
          />
        </div>
        <label className="flex items-center gap-2 text-xs">
          <input
            type="checkbox"
            checked={it.markings}
            onChange={(e) => props.onUpdateIntersection({ markings: e.target.checked })}
          />
          Draw markings inside the intersection
        </label>
      </div>

      <Separator />

      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Connected tracks</h3>
      <div className="grid gap-1">
        {it.trackEnds.length === 0 && (
          <p className="text-[11px] text-muted-foreground">No tracks linked. Select tracks and press Ctrl+L, or use the context menu.</p>
        )}
        {props.linkedTracks.map((end) => (
          <div key={`${end.trackId}:${end.contact}`} className="flex items-center justify-between rounded-md border border-border bg-muted/40 px-2 py-1 text-xs">
            <span className="truncate">{props.roadsList.find((r) => r.id === end.trackId)?.name ?? end.trackId}</span>
            <span className="text-muted-foreground">{end.contact}</span>
          </div>
        ))}
      </div>
      <div className="grid grid-cols-2 gap-1.5">
        <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onUnlinkIntersection}>
          Unlink Tracks
        </Button>
        <Button size="sm" variant="destructive" className="h-7 text-xs" onClick={props.onDeleteIntersection}>
          Delete
        </Button>
      </div>

      <Separator />

      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
        Authorisations ({authorizedCount}/{props.ways.length})
      </h3>
      <p className="text-[11px] leading-relaxed text-muted-foreground">
        Green = authorized way, red = interdiction. Priority is given to the vehicle coming from the right.
      </p>
      <div className="grid gap-1">
        {props.ways.length === 0 && (
          <p className="text-[11px] text-muted-foreground">Ways appear once at least two tracks are linked.</p>
        )}
        {props.ways.map((way) => {
          const fromKey = `${way.from.trackId}:${way.from.contact}`
          const toKey = `${way.to.trackId}:${way.to.contact}`
          const locked = props.lockedPassageways.includes(fromKey) || props.lockedPassageways.includes(toKey)
          return (
            <div key={way.key} className="grid gap-1 rounded-md border border-border bg-muted/40 px-2 py-1.5 text-xs">
              <div className="flex items-center justify-between gap-2">
                <span className="truncate">
                  {props.roadsList.find((r) => r.id === way.from.trackId)?.name ?? way.from.trackId} ({way.from.contact})
                  {' → '}
                  {props.roadsList.find((r) => r.id === way.to.trackId)?.name ?? way.to.trackId} ({way.to.contact})
                </span>
                <span className={way.authorized ? 'text-emerald-500' : 'text-red-500'}>
                  {way.turn} {way.authorized ? '✓' : '✗'}
                </span>
              </div>
              <div className="flex items-center gap-1">
                <Button
                  size="sm"
                  variant="outline"
                  className="h-6 px-2 text-[10px]"
                  onClick={() => props.onToggleAuthorization(way.key)}
                >
                  Toggle
                </Button>
                <Button
                  size="sm"
                  variant={locked ? 'default' : 'ghost'}
                  className="h-6 px-2 text-[10px]"
                  onClick={() => props.onLockPassageway(fromKey)}
                >
                  Lock {way.from.contact}
                </Button>
                <Button
                  size="sm"
                  variant={locked ? 'default' : 'ghost'}
                  className="h-6 px-2 text-[10px]"
                  onClick={() => props.onLockPassageway(toKey)}
                >
                  Lock {way.to.contact}
                </Button>
              </div>
            </div>
          )
        })}
      </div>
      <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onInvertAuthorizations}>
        Invert Authorisations (Ctrl+Shift+I)
      </Button>

      <Separator />

      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Main path</h3>
      <div className="grid grid-cols-2 gap-1.5">
        {[0, 1].map((slot) => (
          <select
            key={slot}
            className="h-7 rounded-md border border-border bg-background px-1 text-xs"
            value={it.mainPath?.[slot] ?? ''}
            onChange={(e) => {
              const other = slot === 0 ? it.mainPath?.[1] : it.mainPath?.[0]
              if (e.target.value && other && e.target.value !== other) props.onSetMainPath(slot === 0 ? e.target.value : other, slot === 0 ? other : e.target.value)
            }}
          >
            <option value="">— none —</option>
            {it.trackEnds.map((end) => (
              <option key={`${end.trackId}:${end.contact}`} value={end.trackId}>
                {props.roadsList.find((r) => r.id === end.trackId)?.name ?? end.trackId}
              </option>
            ))}
          </select>
        ))}
      </div>
    </div>
  )
}

// ─── Contour Handles tab (doc 5.5.4.4.13) ────────────────────────────
function ContourHandlesTab(props: SelectionPanelProps) {
  const { selectedIntersection: it } = props
  if (!it) {
    return <p className="text-xs text-muted-foreground">Select an intersection to edit its contour handles.</p>
  }
  return (
    <div className="grid gap-3">
      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Contour Handles</h3>
      <div className="grid gap-1.5">
        <Label className="text-[11px] text-muted-foreground">Interpolation</Label>
        <select
          className="h-7 rounded-md border border-border bg-background px-2 text-xs"
          value={it.contourInterpolation}
          onChange={(e) => props.onUpdateIntersection({ contourInterpolation: e.target.value as 'linear' | 'smooth' })}
        >
          <option value="smooth">Smooth (curve)</option>
          <option value="linear">Linear (poly-line)</option>
        </select>
      </div>
      <div className="grid gap-1">
        {it.contourHandles.length === 0 && (
          <p className="text-[11px] text-muted-foreground">
            No handles. Click “Insert Handle” below, then drag the square handle in the view.
          </p>
        )}
        {it.contourHandles.map((handle, index) => (
          <div key={index} className="grid grid-cols-[2rem_1fr_1fr_2rem] items-center gap-1 text-xs">
            <span className="text-muted-foreground">#{index + 1}</span>
            <Input
              type="number"
              step={0.5}
              className="h-6 text-xs"
              value={Number(handle.x.toFixed(2))}
              onChange={(e) => {
                const x = Number.parseFloat(e.target.value)
                if (Number.isFinite(x)) props.onMoveContourHandle(index, { x, y: handle.y })
              }}
            />
            <Input
              type="number"
              step={0.5}
              className="h-6 text-xs"
              value={Number(handle.y.toFixed(2))}
              onChange={(e) => {
                const y = Number.parseFloat(e.target.value)
                if (Number.isFinite(y)) props.onMoveContourHandle(index, { x: handle.x, y })
              }}
            />
            <Button size="sm" variant="ghost" className="h-6 px-1 text-xs" onClick={() => props.onDeleteContourHandle(index)}>
              ✕
            </Button>
          </div>
        ))}
      </div>
      <Button size="sm" variant="outline" className="h-7 text-xs" onClick={props.onAddContourHandle}>
        + Insert Handle
      </Button>
      <p className="text-[11px] leading-relaxed text-muted-foreground">
        Handles are control points on the intersection contour. Keep the intersection handle inside the square for valid contours.
      </p>
    </div>
  )
}
