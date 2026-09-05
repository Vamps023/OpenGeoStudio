import type { LaneSectionDef } from '../engine/laneTypes'

interface CenterSummaryProps {
  section: LaneSectionDef
  roadName: string
}

export default function CenterSummary({ section, roadName }: CenterSummaryProps) {
  const totalLeft = section.left.reduce((a, l) => a + l.width, 0)
  const totalRight = section.right.reduce((a, l) => a + l.width, 0)
  return (
    <div className="rounded border border-border bg-muted/40 p-2 text-center">
      <div className="text-[10px] uppercase tracking-wider text-muted-foreground">Axis · {roadName}</div>
      <div className="mt-1 flex items-center justify-around text-xs">
        <div>
          <div className="text-muted-foreground">Left width</div>
          <div className="font-mono text-foreground">{totalLeft.toFixed(2)} m</div>
        </div>
        <div className="text-muted-foreground">|</div>
        <div>
          <div className="text-muted-foreground">Right width</div>
          <div className="font-mono text-foreground">{totalRight.toFixed(2)} m</div>
        </div>
        <div className="text-muted-foreground">|</div>
        <div>
          <div className="text-muted-foreground">Total</div>
          <div className="font-mono text-foreground">{(totalLeft + totalRight).toFixed(2)} m</div>
        </div>
      </div>
    </div>
  )
}
