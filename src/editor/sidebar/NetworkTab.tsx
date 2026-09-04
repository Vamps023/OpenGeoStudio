import { RefreshCw, Trash2 } from 'lucide-react'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import LanesTab from '../../lanes/LanesTab'
import PortionProfileEditor from '../../lanes/PortionProfileEditor'
import type { AutoJunction } from '../tooling'
import type { IntersectionData } from '../../engine/intersections'
import type { LayerFlags, RoadData } from '../../state/store'

export interface NetworkTabProps {
  intersections: IntersectionData[]
  junctions: AutoJunction[]
  activeJunctionCount: number
  layers: LayerFlags
  selectedIntersectionId: string | null
  selectedRoad: RoadData | null
  selectedRoadLength: number
  onSelectIntersection: (id: string) => void
  onDeleteIntersection: (id: string) => void
  onToggleJunction: (junction: AutoJunction) => void
  onRegenerateJunctions: () => void
  onSetLayer: (key: keyof LayerFlags, value: boolean) => void
}

const LAYER_ROWS: [keyof LayerFlags, string][] = [
  ['roadLogicalContent', 'Road Logical Content (axes)'],
  ['road3dGeneration', 'Road 3D Generation'],
  ['intersectionLogicalContent', 'Intersection Logical Content'],
  ['intersection3dGeneration', 'Intersection 3D Generation'],
  ['wayAxis', 'Way Axis'],
  ['wayLogicalContents', 'Way Logical Contents'],
  ['otherSubNetworks', 'Other Sub Networks (exits)'],
]

/** Network tab: intersections, auto junctions, layers and the lanes editor. */
export default function NetworkTab({
  intersections,
  junctions,
  activeJunctionCount,
  layers,
  selectedIntersectionId,
  selectedRoad,
  selectedRoadLength,
  onSelectIntersection,
  onDeleteIntersection,
  onToggleJunction,
  onRegenerateJunctions,
  onSetLayer,
}: NetworkTabProps) {
  return (
    <>
      <div className="flex items-center justify-between gap-2">
        <Badge variant="muted">{activeJunctionCount} junctions</Badge>
        <Button size="sm" variant="outline" onClick={onRegenerateJunctions}>
          <RefreshCw className="size-3.5" />
          Clean up
        </Button>
      </div>

      <h3 className="mt-2 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Intersections</h3>
      {intersections.length === 0 ? (
        <p className="text-xs text-muted-foreground">
          None yet. Use Insert Intersection, or select two crossing tracks → Detect Intersection.
        </p>
      ) : (
        <ul className="grid gap-1.5">
          {intersections.map((node) => (
            <li key={node.id} className="flex items-center gap-2 rounded-lg border border-border bg-muted/40 px-3 py-2">
              <button
                type="button"
                className={
                  selectedIntersectionId === node.id
                    ? 'grid min-w-0 flex-1 gap-0.5 rounded text-left text-xs text-primary'
                    : 'grid min-w-0 flex-1 gap-0.5 text-left text-xs'
                }
                onClick={() => onSelectIntersection(node.id)}
              >
                <span className="truncate font-medium">{node.id}</span>
                <span className="text-muted-foreground">{node.trackEnds.length} tracks · {Object.values(node.authorizations).filter((v) => !v).length} denied</span>
              </button>
              <Button size="sm" variant="ghost" className="h-7 px-2 text-xs" onClick={() => onDeleteIntersection(node.id)}>
                <Trash2 className="size-3.5" />
              </Button>
            </li>
          ))}
        </ul>
      )}

      <h3 className="mt-2 text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Auto junctions</h3>
      {junctions.length === 0 ? (
        <p className="text-xs text-muted-foreground">No road overlaps detected.</p>
      ) : (
        <ul className="grid gap-1.5">
          {junctions.map((junction) => (
            <li key={junction.id} className="flex items-center gap-2 rounded-lg border border-border bg-muted/40 px-3 py-2">
              <div className="grid min-w-0 flex-1 gap-0.5 text-xs">
                <span className="truncate font-medium">{junction.id}</span>
                <span className="text-muted-foreground">{junction.connectingRoads.length} connections</span>
              </div>
              <Button
                size="sm"
                variant={junction.suppressed ? 'default' : 'ghost'}
                className="h-7 px-2.5 text-xs"
                onClick={() => onToggleJunction(junction)}
              >
                {junction.suppressed ? 'Create' : 'Detach'}
              </Button>
            </li>
          ))}
        </ul>
      )}

      <Separator className="my-2" />

      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Layers</h3>
      <div className="grid gap-1.5">
        {LAYER_ROWS.map(([key, label]) => (
          <label key={key} className="flex items-center gap-2 text-xs">
            <input type="checkbox" checked={layers[key]} onChange={(e) => onSetLayer(key, e.target.checked)} />
            {label}
          </label>
        ))}
      </div>

      <Separator className="my-2" />

      <h3 className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">Lanes</h3>
      {selectedRoad ? (
        <>
          <LanesTab road={selectedRoad} />
          <PortionProfileEditor road={selectedRoad} length={selectedRoadLength} />
        </>
      ) : (
        <p className="text-xs text-muted-foreground">Select a road to edit its lanes.</p>
      )}
    </>
  )
}
