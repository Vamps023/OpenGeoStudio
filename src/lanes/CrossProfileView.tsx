import type { LaneDef, LaneSectionDef } from '../engine/laneTypes'
import { LANE_TYPE_META } from '../engine/laneMetadata'

interface CrossProfileViewProps {
  section: LaneSectionDef
  selectedKey: string | null
  onSelect: (side: 'left' | 'right', index: number) => void
}

export default function CrossProfileView({ section, selectedKey, onSelect }: CrossProfileViewProps) {
  const totalWidth = section.left.reduce((a, l) => a + l.width, 0) + section.right.reduce((a, l) => a + l.width, 0) || 1
  const viewWidth = 360
  const scale = viewWidth / totalWidth
  const leftOrigin = section.right.reduce((a, l) => a + l.width, 0) * scale
  const leftTotal = section.left.reduce((a, l) => a + l.width, 0) * scale

  const leftLanes = section.left
  const rightLanes = section.right

  return (
    <div className="border rounded-md p-2 bg-slate-50 dark:bg-slate-900">
      <div className="text-[10px] text-muted-foreground flex items-center justify-between mb-1">
        <span>← Outer (Left)</span>
        <span className="font-semibold">Cross Profile</span>
        <span>(Right) Outer →</span>
      </div>
      <svg viewBox={`0 0 ${viewWidth} 60`} className="w-full h-16">
        <line x1={leftOrigin} y1="5" x2={leftOrigin} y2="55" stroke="#eab308" strokeWidth="1.5" strokeDasharray="3,3" />
        {renderLanes(leftLanes, leftOrigin - leftTotal, scale, 'left', selectedKey, onSelect, true)}
        {renderLanes(rightLanes, leftOrigin, scale, 'right', selectedKey, onSelect, false)}
      </svg>
      <div className="flex justify-between text-[10px] text-muted-foreground mt-1 px-1">
        <span>L: {leftLanes.length}</span>
        <span>R: {rightLanes.length}</span>
      </div>
    </div>
  )
}

function renderLanes(
  lanes: LaneDef[],
  startX: number,
  scale: number,
  side: 'left' | 'right',
  selectedKey: string | null,
  onSelect: (side: 'left' | 'right', index: number) => void,
  goLeft: boolean,
) {
  let x = startX
  return lanes.map((lane, i) => {
    const w = Math.max(lane.width * scale, 4)
    const rect = (
      <rect
        key={`${side}-${i}`}
        x={goLeft ? x - w : x}
        y={10}
        width={w}
        height={40}
        fill={LANE_TYPE_META[lane.type]?.color ?? '#3b4252'}
        stroke={selectedKey === `${side}:${i}` ? '#fff' : 'rgba(0,0,0,0.2)'}
        strokeWidth={selectedKey === `${side}:${i}` ? 2 : 0.5}
        onClick={() => onSelect(side, i)}
        style={{ cursor: 'pointer' }}
      />
    )
    x = goLeft ? x - w : x + w
    return rect
  })
}
