import { Separator } from '@/components/ui/separator'
import { FUNCTION_LABELS } from '../../engine/xyFunctions'
import DraftLengthRow from '../DraftLengthRow'
import { toolHint } from '../tooling'
import type { RoadData, Tool } from '../../state/store'

export interface RoadsTabProps {
  roads: RoadData[]
  selectedIds: string[]
  roadLengths: Map<string, number>
  tool: Tool
  draftLength: number | null
  onRoadClick: (roadId: string, additive: boolean) => void
}

/** Roads tab: flat list of tracks with function-chain summary and lengths. */
export default function RoadsTab({ roads, selectedIds, roadLengths, tool, draftLength, onRoadClick }: RoadsTabProps) {
  return (
    <>
      {roads.length === 0 ? (
        <p className="text-xs text-muted-foreground">No roads yet. Pick an insert tool and drag on the canvas.</p>
      ) : (
        <ul className="grid gap-1">
          {roads.map((road) => (
            <li key={road.id} className="flex items-center gap-1">
              <button
                type="button"
                className={
                  selectedIds.includes(road.id)
                    ? 'flex min-w-0 flex-1 items-center justify-between rounded-md border border-primary/50 bg-primary/10 px-3 py-2 text-left text-xs'
                    : 'flex min-w-0 flex-1 items-center justify-between rounded-md border border-transparent px-3 py-2 text-left text-xs hover:bg-accent'
                }
                onClick={(event) => onRoadClick(road.id, event.ctrlKey || event.metaKey)}
              >
                <span className="truncate font-medium">
                  {road.name}
                  {road.functions && road.functions.length > 0 && (
                    <span className="ml-1 text-[10px] text-muted-foreground">
                      ({(road.functions ?? []).map((f) => FUNCTION_LABELS[f.kind][0]).join('')})
                    </span>
                  )}
                </span>
                <span className="ml-2 shrink-0 text-muted-foreground">
                  {(roadLengths.get(road.id) ?? 0).toFixed(1)} m
                </span>
              </button>
            </li>
          ))}
        </ul>
      )}
      <Separator className="my-2" />
      <p className="text-[11px] leading-relaxed text-muted-foreground">{toolHint(tool)}</p>
      {draftLength !== null && <DraftLengthRow length={draftLength} />}
    </>
  )
}
