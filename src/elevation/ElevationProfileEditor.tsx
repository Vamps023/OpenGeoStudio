import { useEffect, useMemo, useRef, useState } from 'react'
import { Plus, Trash2 } from 'lucide-react'
import { evaluateElevation, evaluateGrade, normalizeElevationProfile } from '../engine/elevation'
import type { ElevationPoint } from '../engine/elevation'
import type { RoadData } from '../state/store'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { ScrollArea } from '@/components/ui/scroll-area'

interface Props {
  road: RoadData
  length: number
  onChange: (profile: ElevationPoint[]) => void
}

const GRAPH_HEIGHT = 150
const PAD = { top: 16, right: 16, bottom: 22, left: 44 }
const CURVE_STEPS = 128
const DEFAULT_WIDTH = 680

export default function ElevationProfileEditor({ road, length, onChange }: Props) {
  const wrapRef = useRef<HTMLDivElement | null>(null)
  const svgRef = useRef<SVGSVGElement | null>(null)
  const [dragIndex, setDragIndex] = useState<number | null>(null)
  const [hoverStation, setHoverStation] = useState<number | null>(null)
  const [width, setWidth] = useState(DEFAULT_WIDTH)

  // Track available width so the graph always fills the footer
  useEffect(() => {
    const element = wrapRef.current
    if (!element) return
    const observer = new ResizeObserver((entries) => {
      const next = entries[0]?.contentRect.width ?? 0
      if (next > 0) setWidth(Math.floor(next))
    })
    observer.observe(element)
    return () => observer.disconnect()
  }, [])

  const profile = useMemo(
    () => normalizeElevationProfile(road.elevationProfile, length),
    [road.elevationProfile, length],
  )

  const bounds = useMemo(() => {
    if (profile.length === 0 || length <= 0) return { zMin: -1, zMax: 1 }
    let zMin = Infinity
    let zMax = -Infinity
    for (const point of profile) {
      zMin = Math.min(zMin, point.z)
      zMax = Math.max(zMax, point.z)
    }
    for (let i = 0; i <= CURVE_STEPS; i++) {
      const z = evaluateElevation(profile, (length * i) / CURVE_STEPS)
      zMin = Math.min(zMin, z)
      zMax = Math.max(zMax, z)
    }
    if (!Number.isFinite(zMin) || !Number.isFinite(zMax)) return { zMin: -1, zMax: 1 }
    if (Math.abs(zMax - zMin) < 1) {
      const center = (zMax + zMin) / 2
      return { zMin: center - 0.5, zMax: center + 0.5 }
    }
    const padding = (zMax - zMin) * 0.1
    return { zMin: zMin - padding, zMax: zMax + padding }
  }, [profile, length])

  const plot = useMemo(() => {
    const innerWidth = Math.max(80, width - PAD.left - PAD.right)
    const innerHeight = GRAPH_HEIGHT - PAD.top - PAD.bottom
    const zSpan = Math.max(1e-6, bounds.zMax - bounds.zMin)
    const x = (s: number) => PAD.left + (length > 0 ? (s / length) * innerWidth : 0)
    const y = (z: number) => PAD.top + innerHeight - ((z - bounds.zMin) / zSpan) * innerHeight
    return { x, y, innerWidth, innerHeight }
  }, [bounds, length, width])

  const curvePath = useMemo(() => {
    if (length <= 0 || profile.length === 0) return ''
    const points: string[] = []
    for (let i = 0; i <= CURVE_STEPS; i++) {
      const s = (length * i) / CURVE_STEPS
      const z = evaluateElevation(profile, s)
      points.push(`${i === 0 ? 'M' : 'L'}${plot.x(s).toFixed(2)},${plot.y(z).toFixed(2)}`)
    }
    return points.join(' ')
  }, [profile, length, plot])

  const areaPath = useMemo(() => {
    if (!curvePath) return ''
    const baseY = (PAD.top + plot.innerHeight).toFixed(2)
    return `${curvePath} L${plot.x(length).toFixed(2)},${baseY} L${plot.x(0).toFixed(2)},${baseY} Z`
  }, [curvePath, length, plot])

  const grade = useMemo(() => {
    if (hoverStation == null || profile.length < 2) return null
    return evaluateGrade(profile, hoverStation)
  }, [profile, hoverStation])

  // Summary stats for the header badges
  const stats = useMemo(() => {
    if (length <= 0 || profile.length < 2) return null
    let zMin = Infinity
    let zMax = -Infinity
    let maxGrade = 0
    for (let i = 0; i <= CURVE_STEPS; i++) {
      const s = (length * i) / CURVE_STEPS
      zMin = Math.min(zMin, evaluateElevation(profile, s))
      zMax = Math.max(zMax, evaluateElevation(profile, s))
      maxGrade = Math.max(maxGrade, Math.abs(evaluateGrade(profile, s)))
    }
    return { zMin, zMax, maxGrade }
  }, [profile, length])

  // Grid lines for both axes
  const grid = useMemo(() => {
    const hCount = 4
    const zSpan = bounds.zMax - bounds.zMin
    const horizontal = Array.from({ length: hCount + 1 }, (_, i) => {
      const z = bounds.zMin + (zSpan * i) / hCount
      return { y: plot.y(z), z }
    })
    const vCount = Math.max(2, Math.min(8, Math.floor(plot.innerWidth / 90)))
    const vertical = Array.from({ length: vCount + 1 }, (_, i) => {
      const s = (length * i) / vCount
      return { x: plot.x(s), s }
    })
    return { horizontal, vertical }
  }, [bounds, length, plot])

  function svgToStation(event: React.MouseEvent<SVGSVGElement> | React.PointerEvent<SVGSVGElement>): number | null {
    const svg = svgRef.current
    if (!svg || length <= 0) return null
    const rect = svg.getBoundingClientRect()
    const px = event.clientX - rect.left
    const innerWidth = Math.max(80, rect.width - PAD.left - PAD.right)
    const fraction = (px - PAD.left) / innerWidth
    return Math.max(0, Math.min(length, fraction * length))
  }

  function svgToElevation(event: React.PointerEvent<SVGSVGElement>): number | null {
    const svg = svgRef.current
    if (!svg) return null
    const rect = svg.getBoundingClientRect()
    const py = event.clientY - rect.top
    const innerHeight = Math.max(40, rect.height - PAD.top - PAD.bottom)
    const fraction = 1 - (py - PAD.top) / innerHeight
    const zSpan = Math.max(1e-6, bounds.zMax - bounds.zMin)
    return bounds.zMin + fraction * zSpan
  }

  function handleBackgroundClick(event: React.MouseEvent<SVGSVGElement>) {
    if (dragIndex != null) return
    const s = svgToStation(event)
    const z = svgToElevation(event as unknown as React.PointerEvent<SVGSVGElement>)
    if (s == null || z == null || length <= 0) return
    const next = [...profile, { s, z }].sort((a, b) => a.s - b.s)
    onChange(next)
  }

  function handlePointerDown(index: number, event: React.PointerEvent<SVGGElement>) {
    event.stopPropagation()
    setDragIndex(index)
    ;(event.target as Element).setPointerCapture?.(event.pointerId)
  }

  function handlePointerMove(event: React.PointerEvent<SVGSVGElement>) {
    const s = svgToStation(event)
    if (s == null) return
    setHoverStation(s)
    if (dragIndex == null) return
    const z = svgToElevation(event)
    if (z == null) return
    const next = profile.map((point, index) =>
      index === dragIndex
        ? { s: index === 0 || index === profile.length - 1 ? point.s : Math.max(0, Math.min(length, s)), z }
        : point,
    )
    onChange(next)
  }

  function handlePointerUp() {
    setDragIndex(null)
  }

  function removePoint(index: number) {
    if (index === 0 || index === profile.length - 1) return
    onChange(profile.filter((_, i) => i !== index))
  }

  function handleDelete(index: number, event: React.MouseEvent) {
    event.stopPropagation()
    removePoint(index)
  }

  /** Numeric edit from the points table: patch a point, clamp station, keep sorted. */
  function updatePoint(index: number, patch: Partial<ElevationPoint>) {
    const next = profile.map((point, i) => {
      if (i !== index) return point
      const merged = { ...point, ...patch }
      const isEnd = i === 0 || i === profile.length - 1
      if (isEnd) merged.s = point.s // endpoints stay pinned
      merged.s = Math.max(0, Math.min(length, merged.s))
      return merged
    })
    next.sort((a, b) => a.s - b.s)
    onChange(next)
  }

  /** Insert a point at the midpoint of the widest station gap. */
  function addPoint() {
    if (length <= 0 || profile.length < 2) return
    let bestIndex = 0
    let bestGap = -1
    for (let i = 0; i < profile.length - 1; i++) {
      const gap = profile[i + 1].s - profile[i].s
      if (gap > bestGap) {
        bestGap = gap
        bestIndex = i
      }
    }
    const a = profile[bestIndex]
    const b = profile[bestIndex + 1]
    const s = (a.s + b.s) / 2
    const ratio = b.s === a.s ? 0 : (s - a.s) / (b.s - a.s)
    const z = a.z + (b.z - a.z) * ratio
    const next = [...profile, { s, z }].sort((x, y) => x.s - y.s)
    onChange(next)
  }

  if (length <= 0) {
    return (
      <div className="flex items-center gap-4 px-4 py-3">
        <div className="grid gap-1">
          <h4 className="text-xs font-semibold text-foreground">Elevation Profile · {road.name}</h4>
          <p className="text-xs text-muted-foreground">Road is too short to edit elevation.</p>
        </div>
      </div>
    )
  }

  const gradientId = `elev-fill-${road.id}`
  const hoverZ = hoverStation != null ? evaluateElevation(profile, hoverStation) : null

  return (
    <div className="flex gap-3 px-4 py-3">
      <div className="flex min-w-0 flex-1 flex-col gap-2">
      {/* Header: title + summary badges + live hover readout */}
      <div className="flex flex-wrap items-center gap-x-3 gap-y-1">
        <h4 className="text-xs font-semibold text-foreground">Elevation Profile · {road.name}</h4>
        {stats && (
          <div className="flex items-center gap-1.5 text-[11px]">
            <span className="rounded bg-muted px-1.5 py-0.5 text-muted-foreground">
              {length.toFixed(0)} m
            </span>
            <span className="rounded bg-muted px-1.5 py-0.5 text-muted-foreground">
              {stats.zMin.toFixed(1)} – {stats.zMax.toFixed(1)} m
            </span>
            <span className="rounded bg-muted px-1.5 py-0.5 text-muted-foreground">
              max {(stats.maxGrade * 100).toFixed(1)}% grade
            </span>
          </div>
        )}
        {hoverStation != null && (
          <div className="ml-auto flex items-center gap-3 text-[11px]">
            <span className="text-muted-foreground">
              St <b className="font-medium text-foreground">{hoverStation.toFixed(1)}</b> m
            </span>
            {hoverZ != null && (
              <span className="text-muted-foreground">
                Z <b className="font-medium text-foreground">{hoverZ.toFixed(2)}</b> m
              </span>
            )}
            {grade != null && (
              <span className="text-muted-foreground">
                Grade{' '}
                <b className={`font-medium ${Math.abs(grade) > 0.08 ? 'text-amber-400' : 'text-primary'}`}>
                  {(grade * 100).toFixed(2)}%
                </b>
              </span>
            )}
          </div>
        )}
      </div>

      {/* Graph */}
      <div ref={wrapRef} className="w-full">
        <svg
          ref={svgRef}
          className="block w-full touch-none select-none rounded-lg border border-border bg-[#0b101b]"
          width={width}
          height={GRAPH_HEIGHT}
          onClick={handleBackgroundClick}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onPointerLeave={() => { setHoverStation(null); handlePointerUp() }}
        >
          <defs>
            <linearGradient id={gradientId} x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#4ade80" stopOpacity={0.28} />
              <stop offset="100%" stopColor="#4ade80" stopOpacity={0.02} />
            </linearGradient>
          </defs>

          {/* Plot area */}
          <rect
            x={PAD.left}
            y={PAD.top}
            width={plot.innerWidth}
            height={plot.innerHeight}
            fill="#0d1322"
            stroke="#243044"
          />

          {/* Horizontal gridlines + elevation labels */}
          {grid.horizontal.map(({ y, z }) => (
            <g key={`h-${y.toFixed(1)}`}>
              <line x1={PAD.left} y1={y} x2={PAD.left + plot.innerWidth} y2={y} stroke="#1a2438" strokeWidth={1} />
              <text x={PAD.left - 6} y={y + 3} fill="#64748b" fontSize={9} textAnchor="end">
                {z.toFixed(1)}
              </text>
            </g>
          ))}

          {/* Vertical gridlines + station labels */}
          {grid.vertical.map(({ x, s }) => (
            <g key={`v-${x.toFixed(1)}`}>
              <line x1={x} y1={PAD.top} x2={x} y2={PAD.top + plot.innerHeight} stroke="#1a2438" strokeWidth={1} />
              <text x={x} y={GRAPH_HEIGHT - 6} fill="#64748b" fontSize={9} textAnchor="middle">
                {s >= 1000 ? `${(s / 1000).toFixed(s % 1000 === 0 ? 0 : 1)}km` : `${s.toFixed(0)}m`}
              </text>
            </g>
          ))}
          {/* Area fill + curve */}
          {areaPath && <path d={areaPath} fill={`url(#${gradientId})`} />}
          {curvePath && (
            <path
              d={curvePath}
              fill="none"
              stroke="#4ade80"
              strokeWidth={2}
              strokeLinejoin="round"
              strokeLinecap="round"
            />
          )}

          {/* Hover crosshair + marker */}
          {hoverStation != null && hoverZ != null && (
            <g pointerEvents="none">
              <line
                x1={plot.x(hoverStation)}
                y1={PAD.top}
                x2={plot.x(hoverStation)}
                y2={PAD.top + plot.innerHeight}
                stroke="#3b82f6"
                strokeOpacity={0.55}
                strokeDasharray="3 3"
              />
              <circle
                cx={plot.x(hoverStation)}
                cy={plot.y(hoverZ)}
                r={4}
                fill="#3b82f6"
                stroke="#0b101b"
                strokeWidth={1.5}
              />
            </g>
          )}

          {/* Draggable control points */}
          {profile.map((point, index) => {
            const isEnd = index === 0 || index === profile.length - 1
            return (
              <g key={index} onPointerDown={(e) => handlePointerDown(index, e)}>
                <circle
                  cx={plot.x(point.s)}
                  cy={plot.y(point.z)}
                  r={isEnd ? 4.5 : 5}
                  fill={isEnd ? '#f59e0b' : '#4ade80'}
                  stroke="#0b101b"
                  strokeWidth={1.5}
                  style={{ cursor: isEnd ? 'ns-resize' : 'move' }}
                />
                {!isEnd && (
                  <circle
                    cx={plot.x(point.s)}
                    cy={plot.y(point.z)}
                    r={10}
                    fill="transparent"
                    onDoubleClick={(e) => handleDelete(index, e)}
                    style={{ cursor: 'pointer' }}
                  />
                )}
              </g>
            )
          })}
        </svg>
      </div>

      <p className="text-right text-[11px] text-muted-foreground">
        Click to add a point · drag to move · double-click a point to remove
      </p>
      </div>

      {/* Points table — numeric editing for every point */}
      <div className="flex w-60 shrink-0 flex-col gap-2 border-l border-border pl-3">
        <div className="flex items-center justify-between gap-2">
          <span className="text-[11px] font-bold tracking-wider text-muted-foreground uppercase">
            Points ({profile.length})
          </span>
          <Button size="sm" variant="outline" className="h-6 gap-1 px-2 text-[11px]" onClick={addPoint}>
            <Plus className="size-3" />
            Add
          </Button>
        </div>

        <ScrollArea className="max-h-[168px]">
          <div className="grid gap-1 pr-1">
            {profile.map((point, index) => {
              const isEnd = index === 0 || index === profile.length - 1
              return (
                <div
                  key={index}
                  className={`flex items-center gap-1 rounded-md border px-1.5 py-1 ${
                    isEnd ? 'border-amber-500/30 bg-amber-500/5' : 'border-border bg-muted/30'
                  }`}
                >
                  <span className="w-3.5 shrink-0 text-center text-[10px] text-muted-foreground">
                    {index + 1}
                  </span>
                  <span className="shrink-0 text-[9px] font-semibold text-muted-foreground" title="Station (m)">
                    S
                  </span>
                  <PointInput
                    value={point.s}
                    disabled={isEnd}
                    step={1}
                    className="w-14"
                    disabledTitle="Endpoint station is fixed"
                    onCommit={(s) => updatePoint(index, { s })}
                  />
                  <span className="shrink-0 text-[9px] font-semibold text-muted-foreground" title="Elevation (m)">
                    Z
                  </span>
                  <PointInput
                    value={point.z}
                    step={0.1}
                    className="min-w-0 flex-1"
                    onCommit={(z) => updatePoint(index, { z })}
                  />
                  {isEnd ? (
                    <span className="w-5 shrink-0" />
                  ) : (
                    <Button
                      variant="ghost"
                      size="icon-sm"
                      className="size-5 shrink-0 text-muted-foreground hover:text-destructive"
                      title="Remove point"
                      onClick={() => removePoint(index)}
                    >
                      <Trash2 className="size-3" />
                    </Button>
                  )}
                </div>
              )
            })}
          </div>
        </ScrollArea>

        <p className="text-[10px] leading-snug text-muted-foreground">
          Type a value to set it exactly. Endpoints (amber) keep their station.
        </p>
      </div>
    </div>
  )
}

/** Number input with a local draft so decimals/minus signs type naturally. */
function PointInput({
  value,
  onCommit,
  disabled = false,
  disabledTitle,
  step,
  className,
}: {
  value: number
  onCommit: (next: number) => void
  disabled?: boolean
  disabledTitle?: string
  step?: number
  className?: string
}) {
  const [draft, setDraft] = useState(String(value))

  useEffect(() => {
    setDraft(String(value))
  }, [value])

  return (
    <Input
      type="number"
      step={step}
      disabled={disabled}
      title={disabled ? disabledTitle : undefined}
      className={`h-6 rounded px-1.5 text-[11px] tabular-nums ${className ?? ''}`}
      value={draft}
      onChange={(event) => {
        setDraft(event.target.value)
        const parsed = Number.parseFloat(event.target.value)
        if (Number.isFinite(parsed)) onCommit(parsed)
      }}
      onBlur={() => setDraft(String(value))}
    />
  )
}
