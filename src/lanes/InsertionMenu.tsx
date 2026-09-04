import { LANE_TYPE_META, ALL_LANE_TYPES } from '../engine/laneMetadata'
import type { LaneType } from '../engine/laneTypes'
import { Plus } from 'lucide-react'

export default function InsertionMenu({
  onPick,
  label,
}: {
  onPick: (type: LaneType) => void
  label: string
}) {
  return (
    <details className="group">
      <summary className="flex cursor-pointer items-center gap-1 rounded border border-dashed border-border px-2 py-1 text-xs text-muted-foreground hover:bg-muted/40">
        <Plus className="h-3 w-3" /> {label}
      </summary>
      <div className="mt-1 grid grid-cols-2 gap-1 rounded bg-muted/40 p-2">
        {ALL_LANE_TYPES.map((t) => (
          <button
            key={t}
            onClick={(e) => { e.stopPropagation(); onPick(t) }}
            className="flex items-center gap-1 rounded px-1 py-0.5 text-left text-xs hover:bg-primary/20"
            style={{ borderLeft: `2px solid ${LANE_TYPE_META[t].color}` }}
            title={LANE_TYPE_META[t].description}
          >
            {LANE_TYPE_META[t].label}
          </button>
        ))}
      </div>
    </details>
  )
}
