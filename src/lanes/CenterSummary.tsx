import type { LaneSectionDef } from '../engine/laneTypes'

interface CenterSummaryProps {
  section: LaneSectionDef
  roadName: string
}

export default function CenterSummary({ section, roadName }: CenterSummaryProps) {
  const totalLeft = section.left.reduce((a, l) => a + l.width, 0)
  const totalRight = section.right.reduce((a, l) => a + l.width, 0)
  return (
    <div className="rounded border border-slate-800 bg-slate-900/40 p-2 text-center">
      <div className="text-[10px] uppercase tracking-wider text-slate-500">Axis · {roadName}</div>
      <div className="mt-1 flex items-center justify-around text-xs">
        <div>
          <div className="text-slate-400">Left width</div>
          <div className="font-mono text-slate-200">{totalLeft.toFixed(2)} m</div>
        </div>
        <div className="text-slate-600">|</div>
        <div>
          <div className="text-slate-400">Right width</div>
          <div className="font-mono text-slate-200">{totalRight.toFixed(2)} m</div>
        </div>
        <div className="text-slate-600">|</div>
        <div>
          <div className="text-slate-400">Total</div>
          <div className="font-mono text-slate-200">{(totalLeft + totalRight).toFixed(2)} m</div>
        </div>
      </div>
    </div>
  )
}
