// ─────────────────────────────────────────────────────────────────────
// Lanes Tab (SCANeR-compatible dock widget)
//
// Mirrors the SCANeR Lanes tab: per-lane editing of type / width / speed
// limit / circulation way / vehicles authorization, plus border heights
// and offsets for the off-the-road feedback effect.
// ─────────────────────────────────────────────────────────────────────

import LanesPanel from './LanesPanel'
import LanesTabPanel from './LanesTabPanel'
import { getLaneSection } from '../state/store'
import type { RoadData } from '../state/store'
import { totalWidth } from '../engine/laneLayout'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'

export { laneKey, parseLaneKey, borderKey, parseBorderKey } from './laneKeys'

interface LanesTabProps {
  road: RoadData
}

/**
 * Backwards-compatible default export.
 * The LanesPanel contains the full editor (side blocks + center summary +
 * lane properties editor). The LanesTabPanel contains the cross-section
 * profile view + insertion menu.
 */
export default function LanesTab({ road }: LanesTabProps) {
  const section = getLaneSection(road)
  return (
    <Tabs defaultValue="properties" className="min-w-0 gap-3 text-foreground">
      <div className="rounded-md border border-border bg-muted/40 p-3">
        <h3 className="text-sm font-semibold">{road.name} · Lane configuration</h3>
        <p className="mt-1 text-xs text-muted-foreground">
          {section.left.length + section.right.length} lanes · {section.left.length} left / {section.right.length} right · {totalWidth(section).toFixed(2)} m total width
        </p>
      </div>
      <TabsList aria-label="Lane configuration views" className="w-full">
        <TabsTrigger value="properties">Properties</TabsTrigger>
        <TabsTrigger value="cross-section">Cross section</TabsTrigger>
      </TabsList>
      <TabsContent value="properties" className="min-w-0">
        <LanesPanel road={road} />
      </TabsContent>
      <TabsContent value="cross-section" className="min-w-0">
        <LanesTabPanel road={road} />
      </TabsContent>
    </Tabs>
  )
}

export { LanesTabPanel }
