import { LANE_TYPE_META } from '../engine/laneMetadata'
import type { LaneDef } from '../engine/laneTypes'

interface CrossProfilePreviewProps {
  section: { left: LaneDef[]; right: LaneDef[] }
  selectedLaneKey: string | null
  selectedBorderKey: string | null
  onSelectLane: (side: 'left' | 'right', index: number) => void
  onSelectBorder: (side: 'left' | 'right', edge: 'inner' | 'outer') => void
}

export default function CrossProfilePreview({
  section, selectedLaneKey, selectedBorderKey, onSelectLane, onSelectBorder,
}: CrossProfilePreviewProps) {
  const W = 600
  const H = 110
  const centerY = H / 2

  const leftW = section.left.reduce((a, l) => a + l.width, 0)
  const rightW = section.right.reduce((a, l) => a + l.width, 0)
  const totalW = Math.max(leftW + rightW, 1)
  const scaleX = (W * 0.95) / totalW
  const cx = W * 0.025 + rightW * scaleX

  let xCursor = cx
  const leftStrips = section.left.map((lane, i) => {
    const innerX = xCursor
    xCursor += lane.width * scaleX
    return { lane, innerX, outerX: xCursor, side: 'left' as const, index: i }
  })
  xCursor = cx
  const rightStrips = section.right.map((lane, i) => {
    xCursor -= lane.width * scaleX
    return { lane, innerX: xCursor, outerX: xCursor + lane.width * scaleX, side: 'right' as const, index: i }
  })

  return (
    <div className="rounded border border-border bg-card/40 p-2">
      <div className="mb-1 flex items-center justify-between">
        <span className="text-xs font-semibold">Cross Profile</span>
        <span className="text-xs text-muted-foreground">Total: {totalW.toFixed(2)} m</span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} className="w-full">
        <line x1={cx} y1={10} x2={cx} y2={H - 10} stroke="#e6b800" strokeWidth={2} strokeDasharray="6 3" />
        <text x={cx} y={8} fontSize="9" textAnchor="middle" fill="#e6b800">Center</text>

        {leftStrips.map((s) => {
          const meta = LANE_TYPE_META[s.lane.type]
          const isSel = selectedLaneKey === `left:${s.index}`
          return (
            <g key={s.lane.id} onClick={() => onSelectLane('left', s.index)} className="cursor-pointer">
              <rect
                x={s.innerX} y={centerY - 18} width={s.outerX - s.innerX} height={36}
                fill={meta.color} opacity={isSel ? 1 : 0.85}
                stroke={isSel ? '#ffffff' : 'none'} strokeWidth={1.5}
              />
              <text x={(s.outerX + s.innerX) / 2} y={centerY - 22} fontSize="9" textAnchor="middle" fill="#fff">
                {meta.label}
              </text>
              <text x={(s.outerX + s.innerX) / 2} y={centerY + 28} fontSize="8" textAnchor="middle" fill="#aaa">
                {s.lane.width.toFixed(1)}m
              </text>
            </g>
          )
        })}

        {rightStrips.map((s) => {
          const meta = LANE_TYPE_META[s.lane.type]
          const isSel = selectedLaneKey === `right:${s.index}`
          return (
            <g key={s.lane.id} onClick={() => onSelectLane('right', s.index)} className="cursor-pointer">
              <rect
                x={s.innerX} y={centerY - 18} width={s.outerX - s.innerX} height={36}
                fill={meta.color} opacity={isSel ? 1 : 0.85}
                stroke={isSel ? '#ffffff' : 'none'} strokeWidth={1.5}
              />
              <text x={(s.innerX + s.outerX) / 2} y={centerY - 22} fontSize="9" textAnchor="middle" fill="#fff">
                {meta.label}
              </text>
              <text x={(s.innerX + s.outerX) / 2} y={centerY + 28} fontSize="8" textAnchor="middle" fill="#aaa">
                {s.lane.width.toFixed(1)}m
              </text>
            </g>
          )
        })}

        {leftW > 0 && (
          <>
            <line
              x1={cx + leftW * scaleX} y1={centerY - 20} x2={cx + leftW * scaleX} y2={centerY + 20}
              stroke={selectedBorderKey === 'left:outer' ? '#fff' : '#888'}
              strokeWidth={selectedBorderKey === 'left:outer' ? 3 : 1.5}
              onClick={() => onSelectBorder('left', 'outer')} className="cursor-pointer"
            />
            <line
              x1={cx} y1={centerY - 20} x2={cx} y2={centerY + 20}
              stroke={selectedBorderKey === 'left:inner' ? '#fff' : '#888'}
              strokeWidth={selectedBorderKey === 'left:inner' ? 3 : 1.5}
              onClick={() => onSelectBorder('left', 'inner')} className="cursor-pointer"
            />
          </>
        )}
        {rightW > 0 && (
          <>
            <line
              x1={cx - rightW * scaleX} y1={centerY - 20} x2={cx - rightW * scaleX} y2={centerY + 20}
              stroke={selectedBorderKey === 'right:outer' ? '#fff' : '#888'}
              strokeWidth={selectedBorderKey === 'right:outer' ? 3 : 1.5}
              onClick={() => onSelectBorder('right', 'outer')} className="cursor-pointer"
            />
            <line
              x1={cx} y1={centerY - 20} x2={cx} y2={centerY + 20}
              stroke={selectedBorderKey === 'right:inner' ? '#fff' : '#888'}
              strokeWidth={selectedBorderKey === 'right:inner' ? 3 : 1.5}
              onClick={() => onSelectBorder('right', 'inner')} className="cursor-pointer"
            />
          </>
        )}
      </svg>
    </div>
  )
}
