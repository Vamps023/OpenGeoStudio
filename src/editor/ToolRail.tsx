import { Separator } from '@/components/ui/separator'
import { Button } from '@/components/ui/button'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { LANE_TOOL_GROUPS, ROAD_TOOL_GROUPS, TOOL_ITEMS, TRAIN_TOOL_GROUPS } from './tooling'
import type { Tool } from '../state/store'

export type ToolSpace = 'road' | 'lane' | 'train'

const GROUPS_BY_SPACE = {
  road: ROAD_TOOL_GROUPS,
  lane: LANE_TOOL_GROUPS,
  train: TRAIN_TOOL_GROUPS,
}

/** Left icon rail listing the tools of the active workspace (road/lane/train). */
export default function ToolRail({ tool, space, onChooseTool }: { tool: Tool; space: ToolSpace; onChooseTool: (tool: Tool) => void }) {
  const groups = GROUPS_BY_SPACE[space]
  return (
    <nav aria-label={`${space} tools`} className="flex w-12 shrink-0 flex-col items-center justify-start gap-0.5 overflow-y-auto border-r border-border bg-card/60 py-3">
      {groups.map((group, groupIndex) => (
        <div key={group.label} className="flex flex-col items-center gap-0.5">
          {groupIndex > 0 && <Separator className="my-2 w-6" />}
          {group.tools.map((toolId) => {
            const item = TOOL_ITEMS.find((entry) => entry.tool === toolId)
            if (!item) return null
            const Icon = item.icon
            const active = tool === item.tool
            return (
              <Tooltip key={item.tool}>
                <TooltipTrigger asChild>
                  <Button
                    variant={active ? 'default' : 'ghost'}
                    size="icon-sm"
                    aria-label={item.label}
                    aria-pressed={active}
                    onClick={() => onChooseTool(item.tool)}
                    className={item.color ? 'relative' : undefined}
                  >
                    <Icon className="size-4" style={item.color && !active ? { color: item.color } : undefined} />
                  </Button>
                </TooltipTrigger>
                <TooltipContent side="right">
                  <span>{item.label}</span>
                  {item.color && <span className="ml-2 inline-block size-2 rounded-full align-middle" style={{ background: item.color }} />}
                </TooltipContent>
              </Tooltip>
            )
          })}
        </div>
      ))}
    </nav>
  )
}
