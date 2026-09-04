import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { TOOL_ITEMS } from './tooling'
import DraftLengthRow from './DraftLengthRow'
import type { EditorConfig, InsertOptions, Tool } from '../state/store'

export interface ToolOptionsPanelProps {
  tool: Tool
  insertOptions: InsertOptions
  config: EditorConfig
  draftLength: number | null
  onInsertOptionsChange: (patch: Partial<InsertOptions>) => void
  onConfigChange: (patch: Partial<EditorConfig>) => void
}

/** TOOL: Insert <function> panel (Stick to Background Terrain + Default Profile). */
export default function ToolOptionsPanel({ tool, insertOptions, config, draftLength, onInsertOptionsChange, onConfigChange }: ToolOptionsPanelProps) {
  return (
    <div className="absolute top-3 left-3 z-10 grid w-56 gap-2 rounded-lg border border-border bg-card/90 p-3 shadow-lg backdrop-blur">
      <h3 className="text-[10px] font-bold tracking-wider text-muted-foreground uppercase">
        TOOL: {TOOL_ITEMS.find((i) => i.tool === tool)?.label}
      </h3>
      <label className="flex items-center gap-2 text-xs">
        <input
          type="checkbox"
          checked={insertOptions.stickToTerrain}
          onChange={(e) => onInsertOptionsChange({ stickToTerrain: e.target.checked })}
        />
        Stick to Background Terrain
      </label>
      <div className="grid gap-1">
        <Label className="text-[11px] text-muted-foreground">Default Profile</Label>
        <select
          className="h-7 rounded-md border border-border bg-background px-2 text-xs"
          value={insertOptions.defaultProfile}
          onChange={(e) => onInsertOptionsChange({ defaultProfile: e.target.value as typeof insertOptions.defaultProfile })}
        >
          <option value="travel">Travel (default)</option>
          <option value="highway">Highway (2+2, shoulders)</option>
          <option value="rural">Rural (shoulders)</option>
          <option value="urban">Urban (sidewalks)</option>
        </select>
      </div>
      {tool === 'draw-clothoid' && (
        <>
          <div className="grid gap-1">
            <Label className="text-[11px] text-muted-foreground">Radius Out (0 = infinite)</Label>
            <Input
              type="number"
              min={0}
              className="h-7 text-xs"
              value={config.clothoidRadiusOut}
              onChange={(e) => onConfigChange({ clothoidRadiusOut: Math.max(0, Number.parseFloat(e.target.value) || 0) })}
            />
          </div>
          <div className="grid grid-cols-2 gap-1">
            <Button size="sm" variant={config.clothoidTurn === 'left' ? 'default' : 'outline'} className="h-7 text-xs" onClick={() => onConfigChange({ clothoidTurn: 'left' })}>
              Left
            </Button>
            <Button size="sm" variant={config.clothoidTurn === 'right' ? 'default' : 'outline'} className="h-7 text-xs" onClick={() => onConfigChange({ clothoidTurn: 'right' })}>
              Right
            </Button>
          </div>
        </>
      )}
      {draftLength !== null && <DraftLengthRow length={draftLength} />}
    </div>
  )
}
