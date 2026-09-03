import { useMemo, useRef, useState } from 'react'
import { evaluateElevation, evaluateGrade, normalizeElevationProfile } from '../engine/elevation'
import type { ElevationPoint } from '../engine/elevation'
import type { RoadData } from '../state/store'

interface Props {
  road: RoadData
  length: number
  onChange: (profile: ElevationPoint[]) => void
}

const GRAPH_WIDTH = 260
const GRAPH_HEIGHT = 120
const PADDING = 28

export default function ElevationProfileEditor({ road, length, onChange }: Props) {
  const svgRef = useRef<SVGSVGElement | null>(null)
  const [dragIndex, setDragIndex] = useState<number | null>(null)
  const [hoverStation, setHoverStation] = useState<number | null>(null)

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
    const sampledSteps = 32
    for (let i = 0; i <= sampledSteps; i++) {
      const z = evaluateElevation(profile, (length * i) / sampledSteps)
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
    const innerWidth = GRAPH_WIDTH - PADDING * 2
    const innerHeight = GRAPH_HEIGHT - PADDING * 2
    const zSpan = Math.max(1e-6, bounds.zMax - bounds.zMin)
    const x = (s: number) => PADDING + (length > 0 ? (s / length) * innerWidth : 0)
    const y = (z: number) => PADDING + innerHeight - ((z - bounds.zMin) / zSpan) * innerHeight
    return { x, y, innerWidth, innerHeight }
  }, [bounds, length])

  const curvePath = useMemo(() => {
    if (length <= 0 || profile.length === 0) return ''
    const steps = 64
    const points: string[] = []
    for (let i = 0; i <= steps; i++) {
      const s = (length * i) / steps
      const z = evaluateElevation(profile, s)
      points.push(`${i === 0 ? 'M' : 'L'}${plot.x(s).toFixed(2)},${plot.y(z).toFixed(2)}`)
    }
    return points.join(' ')
  }, [profile, length, plot])

  const grade = useMemo(() => {
    if (hoverStation == null || profile.length < 2) return null
    return evaluateGrade(profile, hoverStation)
  }, [profile, hoverStation])

  function svgToStation(event: React.MouseEvent<SVGSVGElement> | React.PointerEvent<SVGSVGElement>): number | null {
    const svg = svgRef.current
    if (!svg || length <= 0) return null
    const rect = svg.getBoundingClientRect()
    const px = event.clientX - rect.left
    const innerWidth = GRAPH_WIDTH - PADDING * 2
    const fraction = (px - PADDING) / innerWidth
    return Math.max(0, Math.min(length, fraction * length))
  }

  function svgToElevation(event: React.PointerEvent<SVGSVGElement>): number | null {
    const svg = svgRef.current
    if (!svg) return null
    const rect = svg.getBoundingClientRect()
    const py = event.clientY - rect.top
    const innerHeight = GRAPH_HEIGHT - PADDING * 2
    const fraction = 1 - (py - PADDING) / innerHeight
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

  function handleDelete(index: number, event: React.MouseEvent) {
    event.stopPropagation()
    if (index === 0 || index === profile.length - 1) return
    const next = profile.filter((_, i) => i !== index)
    onChange(next)
  }

  if (length <= 0) {
    return (
      <div className="elevation-editor">
        <div className="elevation-side">
          <h4>Elevation Profile · {road.name}</h4>
          <p className="empty-note">Road is too short to edit elevation.</p>
        </div>
      </div>
    )
  }

  return (
    <div className="elevation-editor">
      <svg
        ref={svgRef}
        className="elevation-graph"
        width={GRAPH_WIDTH}
        height={GRAPH_HEIGHT}
        onClick={handleBackgroundClick}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerLeave={() => { setHoverStation(null); handlePointerUp() }}
      >
        <rect x={PADDING} y={PADDING} width={GRAPH_WIDTH - PADDING * 2} height={GRAPH_HEIGHT - PADDING * 2} fill="#0d1117" stroke="#30363d" />
        {curvePath && <path d={curvePath} fill="none" stroke="#58a6ff" strokeWidth={1.5} />}
        {hoverStation != null && (
          <line x1={plot.x(hoverStation)} y1={PADDING} x2={plot.x(hoverStation)} y2={GRAPH_HEIGHT - PADDING} stroke="#30363d" strokeDasharray="3 3" />
        )}
        {profile.map((point, index) => (
          <g key={index} onPointerDown={(e) => handlePointerDown(index, e)}>
            <circle
              cx={plot.x(point.s)}
              cy={plot.y(point.z)}
              r={index === 0 || index === profile.length - 1 ? 4 : 5}
              fill={index === 0 || index === profile.length - 1 ? '#f0883e' : '#58a6ff'}
              stroke="#0d1117"
              strokeWidth={1}
              style={{ cursor: index === 0 || index === profile.length - 1 ? 'ns-resize' : 'move' }}
            />
            {index !== 0 && index !== profile.length - 1 && (
              <circle
                cx={plot.x(point.s)}
                cy={plot.y(point.z)}
                r={9}
                fill="transparent"
                onDoubleClick={(e) => handleDelete(index, e)}
                style={{ cursor: 'pointer' }}
              />
            )}
          </g>
        ))}
        <text x={PADDING} y={PADDING - 8} fill="#8b949e" fontSize={9}>{bounds.zMax.toFixed(1)} m</text>
        <text x={PADDING} y={GRAPH_HEIGHT - PADDING + 14} fill="#8b949e" fontSize={9}>{bounds.zMin.toFixed(1)} m</text>
        <text x={GRAPH_WIDTH - PADDING} y={GRAPH_HEIGHT - PADDING + 14} fill="#8b949e" fontSize={9} textAnchor="end">{length.toFixed(0)} m</text>
      </svg>
      <div className="elevation-side">
        <h4>Elevation Profile · {road.name}</h4>
        <div className="elevation-readout">
          {hoverStation != null && (
            <div className="stat-row">
              <span>Station</span><b>{hoverStation.toFixed(1)} m</b>
            </div>
          )}
          {hoverStation != null && (
            <div className="stat-row">
              <span>Elevation</span><b>{evaluateElevation(profile, hoverStation).toFixed(2)} m</b>
            </div>
          )}
          {grade != null && (
            <div className="stat-row">
              <span>Grade</span><b>{(grade * 100).toFixed(2)}%</b>
            </div>
          )}
        </div>
        <p className="empty-note">Click graph to add a point. Drag to move. Double-click interior points to delete.</p>
      </div>
    </div>
  )
}
