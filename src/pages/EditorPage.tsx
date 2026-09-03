import { useEffect, useMemo, useRef, useState } from 'react'
import { buildJunctionNetwork, visibleRoadRanges } from '../engine/junctions'
import { buildConnectingRoadMesh, buildRoadMesh, buildRoadMeshRange } from '../engine/mesh'
import { evaluateElevation, evaluateGrade, normalizeElevationProfile } from '../engine/elevation'
import { fitRoadGeometry, nearestPointOnPath, sampledControlPoints } from '../engine/roadGeometry'
import type { MeshData } from '../engine/mesh'
import type { ElevationPoint } from '../engine/elevation'
import type { Vec2 } from '../engine/types'
import { useStore, uuid } from '../state/store'
import type { RoadData, RoadGeometryType, Tool } from '../state/store'
import RoadViewport from '../viewport/RoadViewport'
import ElevationProfileEditor from '../elevation/ElevationProfileEditor'

interface RoadMeshEntry {
  roadId: string
  mesh: MeshData
}

interface ExtendSession {
  roadId: string
  contact: 'start' | 'end'
  start: Vec2
}

const TOOL_ITEMS: { tool: Tool; label: string }[] = [
  { tool: 'select', label: 'Select' },
  { tool: 'draw-straight', label: 'Straight' },
  { tool: 'draw-polyline', label: 'Polyline' },
  { tool: 'draw-arc', label: 'Arc' },
  { tool: 'move', label: 'Move End' },
  { tool: 'extend', label: 'Extend' },
  { tool: 'split', label: 'Split' },
  { tool: 'delete', label: 'Delete' },
  { tool: 'junction', label: 'Junction' },
]

export default function EditorPage({ onBack }: { onBack: () => void }) {
  const projects = useStore((state) => state.projects)
  const activeProjectId = useStore((state) => state.activeProjectId)
  const closeProject = useStore((state) => state.closeProject)
  const addRoad = useStore((state) => state.addRoad)
  const updateRoad = useStore((state) => state.updateRoad)
  const replaceRoad = useStore((state) => state.replaceRoad)
  const deleteRoad = useStore((state) => state.deleteRoad)
  const suppressJunction = useStore((state) => state.suppressJunction)
  const restoreJunction = useStore((state) => state.restoreJunction)
  const regenerateJunctions = useStore((state) => state.regenerateJunctions)
  const tool = useStore((state) => state.tool)
  const setTool = useStore((state) => state.setTool)
  const config = useStore((state) => state.config)
  const setConfig = useStore((state) => state.setConfig)

  const project = projects.find((item) => item.id === activeProjectId)
  const [selectedRoadId, setSelectedRoadId] = useState<string | null>(null)
  const [draftPoints, setDraftPoints] = useState<Vec2[]>([])
  const [hoverPoint, setHoverPoint] = useState<Vec2 | null>(null)
  const [mode, setMode] = useState<'2d' | '3d'>('2d')
  const dragStartRef = useRef<Vec2 | null>(null)
  const extendRef = useRef<ExtendSession | null>(null)

  const selectedRoad = project?.roads.find((road) => road.id === selectedRoadId) ?? null
  const elevationSamplers = useMemo(() => {
    const map = new Map<string, (s: number) => number>()
    if (!project) return map
    for (const road of project.roads) {
      const path = fitRoadGeometry(road)
      const length = path?.length ?? 0
      const profile = normalizeElevationProfile(road.elevationProfile, length)
      map.set(road.id, (s) => evaluateElevation(profile, s))
    }
    return map
  }, [project])
  const junctionNetwork = useMemo(
    () => (project ? buildJunctionNetwork(project.roads, project.suppressedJunctions, elevationSamplers) : null),
    [project, elevationSamplers],
  )
  const junctions = junctionNetwork?.junctions ?? []
  const activeJunctions = junctions.filter((junction) => !junction.suppressed)

  const roadMeshEntries = useMemo<RoadMeshEntry[]>(() => {
    if (!project || !junctionNetwork) return []
    return project.roads.flatMap((road) => {
      const path = junctionNetwork.paths.get(road.id)
      if (!path) return []
      const section = roadSection(road)
      const cuts = junctionNetwork.cuts.filter((cut) => cut.roadId === road.id)
      return visibleRoadRanges(path, cuts).flatMap((range) => {
        const mesh = buildRoadMeshRange(path, section, range.sStart, range.sEnd, 1, elevationSamplers.get(road.id))
        return mesh ? [{ roadId: road.id, mesh }] : []
      })
    })
  }, [project, junctionNetwork])

  const connectingMeshes = useMemo(() => {
    if (!junctionNetwork) return []
    return activeJunctions.flatMap((junction) =>
      junction.connectingRoads.flatMap((connection) => {
        const mesh = buildConnectingRoadMesh(connection.samples, connection.laneCount, connection.laneWidth)
        return mesh ? [mesh] : []
      }),
    )
  }, [junctionNetwork, activeJunctions])

  const previewPoints = useMemo(() => {
    if (!hoverPoint || draftPoints.length === 0) return draftPoints
    if (tool === 'draw-polyline' || tool === 'draw-arc') return [...draftPoints, hoverPoint]
    return draftPoints
  }, [draftPoints, hoverPoint, tool])
  const draftGeometryType: RoadGeometryType = tool === 'draw-arc' && previewPoints.length >= 3
    ? 'arc'
    : tool === 'draw-polyline'
      ? 'polyline'
      : 'straight'
  const draftPath = useMemo(
    () => fitRoadGeometry({ points: previewPoints, geometryType: draftGeometryType, filletRadius: config.filletRadius }),
    [previewPoints, draftGeometryType, config.filletRadius],
  )
  const draftMesh = useMemo(
    () => buildRoadMesh(draftPath, drawingSection(config.lanesLeft, config.lanesRight, config.laneWidth)),
    [draftPath, config],
  )

  const roadLengths = useMemo(() => {
    if (!project) return new Map<string, number>()
    return new Map(project.roads.map((road) => [road.id, fitRoadGeometry(road)?.length ?? 0]))
  }, [project])

  function chooseTool(next: Tool) {
    setTool(next)
    dragStartRef.current = null
    extendRef.current = null
    setDraftPoints([])
    setHoverPoint(null)
  }

  function createRoad(points: Vec2[], geometryType: RoadGeometryType) {
    if (!project || points.length < 2) return
    addRoad({
      id: uuid(),
      name: `Road ${project.roads.length + 1}`,
      points,
      geometryType,
      lanesLeft: config.lanesLeft,
      lanesRight: config.lanesRight,
      laneWidth: config.laneWidth,
      filletRadius: config.filletRadius,
    })
  }

  function nearestRoad(point: Vec2) {
    if (!project || !junctionNetwork) return null
    let best: { road: RoadData; point: Vec2; s: number; distance: number } | null = null
    for (const road of project.roads) {
      const path = junctionNetwork.paths.get(road.id)
      if (!path) continue
      const nearest = nearestPointOnPath(path, point)
      if (!best || nearest.distance < best.distance) best = { road, ...nearest }
    }
    return best && best.distance <= 8 ? best : null
  }

  function nearestEndpoint(point: Vec2): ExtendSession | null {
    if (!project || !junctionNetwork) return null
    let best: { session: ExtendSession; distance: number } | null = null
    for (const road of project.roads) {
      const path = junctionNetwork.paths.get(road.id)
      if (!path) continue
      const candidates: ExtendSession[] = [
        { roadId: road.id, contact: 'start', start: road.points[0] },
        { roadId: road.id, contact: 'end', start: road.points[road.points.length - 1] },
      ]
      for (const session of candidates) {
        const distance = Math.hypot(point.x - session.start.x, point.y - session.start.y)
        if (!best || distance < best.distance) best = { session, distance }
      }
    }
    return best && best.distance <= 10 ? best.session : null
  }

  function handleDragStart(point: Vec2) {
    if (tool === 'draw-straight') {
      dragStartRef.current = point
      setDraftPoints([point, point])
      return
    }
    if (tool === 'extend' || tool === 'move') {
      const session = nearestEndpoint(point)
      extendRef.current = session
      if (session) {
        setSelectedRoadId(session.roadId)
        setDraftPoints([session.start, session.start])
      }
    }
  }

  function handleDragMove(point: Vec2) {
    const start = tool === 'extend' || tool === 'move' ? extendRef.current?.start : dragStartRef.current
    if (start) setDraftPoints([start, point])
  }

  function handleDragEnd(point: Vec2) {
    if (tool === 'draw-straight') {
      const start = dragStartRef.current
      dragStartRef.current = null
      setDraftPoints([])
      if (start && distance(start, point) >= 1) createRoad([start, point], 'straight')
      return
    }
    if (tool === 'extend' || tool === 'move') {
      const session = extendRef.current
      extendRef.current = null
      setDraftPoints([])
      if (!session || distance(session.start, point) < 1 || !project) return
      const road = project.roads.find((item) => item.id === session.roadId)
      if (!road) return
      let points = [...road.points]
      let geometryType: RoadGeometryType = road.geometryType ?? 'polyline'
      if (tool === 'move') {
        if (session.contact === 'start') points[0] = point
        else points[points.length - 1] = point
      } else {
        if (geometryType === 'arc') {
          const path = fitRoadGeometry(road)
          if (!path) return
          points = sampledControlPoints(path, 0, path.length)
          geometryType = 'polyline'
        }
        points = session.contact === 'start' ? [point, ...points] : [...points, point]
      }
      updateRoad(road.id, { points, geometryType })
    }
  }

  function handleDragCancel() {
    dragStartRef.current = null
    extendRef.current = null
    setDraftPoints([])
  }

  function handleGroundClick(point: Vec2) {
    if (!project || !junctionNetwork) return
    if (tool === 'draw-polyline') {
      setDraftPoints((current) => {
        const last = current[current.length - 1]
        return last && distance(last, point) < 0.1 ? current : [...current, point]
      })
      return
    }
    if (tool === 'draw-arc') {
      const last = draftPoints[draftPoints.length - 1]
      if (last && distance(last, point) < 0.1) return
      const next = [...draftPoints, point]
      if (next.length === 3) {
        createRoad(next, 'arc')
        setDraftPoints([])
        setHoverPoint(null)
      } else {
        setDraftPoints(next)
      }
      return
    }
    if (tool === 'junction') {
      const junction = nearestJunction(junctions, point)
      if (junction) {
        if (junction.suppressed) restoreJunction(junction.key)
        else suppressJunction(junction.key)
      }
      return
    }

    const hit = nearestRoad(point)
    if (tool === 'select') {
      setSelectedRoadId(hit?.road.id ?? null)
    } else if (tool === 'delete' && hit) {
      deleteRoad(hit.road.id)
      if (selectedRoadId === hit.road.id) setSelectedRoadId(null)
    } else if (tool === 'split' && hit) {
      splitRoadAt(hit.road, hit.s)
    }
  }

  function handleGroundHover(point: Vec2) {
    if ((tool === 'draw-polyline' || tool === 'draw-arc') && draftPoints.length > 0) {
      setHoverPoint(point)
    }
  }

  function splitRoadAt(road: RoadData, s: number) {
    const path = fitRoadGeometry(road)
    if (!path || s < 1 || path.length - s < 1) return
    const first: RoadData = {
      ...road,
      id: uuid(),
      name: `${road.name} A`,
      points: sampledControlPoints(path, 0, s),
      geometryType: 'polyline',
    }
    const second: RoadData = {
      ...road,
      id: uuid(),
      name: `${road.name} B`,
      points: sampledControlPoints(path, s, path.length),
      geometryType: 'polyline',
    }
    replaceRoad(road.id, [first, second])
    setSelectedRoadId(first.id)
  }

  function finishPolyline() {
    if (draftPoints.length >= 2) createRoad(draftPoints, 'polyline')
    setDraftPoints([])
    setHoverPoint(null)
  }

  useEffect(() => {
    function handleKeyDown(event: KeyboardEvent) {
      if (event.target instanceof HTMLInputElement) return
      if (event.key === 'Enter' && tool === 'draw-polyline') finishPolyline()
      if (event.key === 'Escape' && (tool === 'draw-polyline' || tool === 'draw-arc')) {
        setDraftPoints([])
        setHoverPoint(null)
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  })

  function reverseSelectedRoad() {
    if (!selectedRoad) return
    updateRoad(selectedRoad.id, {
      points: [...selectedRoad.points].reverse(),
      lanesLeft: selectedRoad.lanesRight,
      lanesRight: selectedRoad.lanesLeft,
    })
  }

  if (!project) return null

  const interaction = tool === 'draw-straight' || tool === 'extend' || tool === 'move' ? 'drag' : 'click'
  const highlightMeshes = selectedRoadId
    ? roadMeshEntries.filter((entry) => entry.roadId === selectedRoadId).map((entry) => entry.mesh)
    : []

  return (
    <div className="editor-page">
      <header className="editor-topbar">
        <button type="button" className="btn-ghost" onClick={onBack}>← Projects</button>
        <div className="editor-title">
          <strong>{project.name}</strong>
          <span>{project.roads.length} roads · {activeJunctions.length} junctions</span>
        </div>
        <div className="mode-toggle">
          <button type="button" className={mode === '2d' ? 'active' : ''} onClick={() => setMode('2d')}>2D</button>
          <button type="button" className={mode === '3d' ? 'active' : ''} onClick={() => setMode('3d')}>3D</button>
        </div>
      </header>

      <nav className="map-toolbar" aria-label="Road tools">
        {TOOL_ITEMS.map((item) => (
          <button
            type="button"
            key={item.tool}
            className={tool === item.tool ? 'active' : ''}
            onClick={() => chooseTool(item.tool)}
          >
            {item.label}
          </button>
        ))}
        {tool === 'draw-polyline' && draftPoints.length > 0 && (
          <>
            <button type="button" disabled={draftPoints.length < 2} onClick={finishPolyline}>Finish</button>
            <button type="button" onClick={() => { setDraftPoints([]); setHoverPoint(null) }}>Cancel</button>
          </>
        )}
        <span className="toolbar-spacer" />
        <button type="button" onClick={regenerateJunctions}>Regenerate Junctions</button>
      </nav>

      <div className="editor-body">
        <RoadViewport
          meshes={[...roadMeshEntries.map((entry) => entry.mesh), ...connectingMeshes]}
          highlightMeshes={highlightMeshes}
          draftMesh={draftMesh}
          draftPoints={previewPoints}
          interaction={interaction}
          onDragStart={handleDragStart}
          onDragMove={handleDragMove}
          onDragEnd={handleDragEnd}
          onDragCancel={handleDragCancel}
          onGroundClick={handleGroundClick}
          onGroundHover={handleGroundHover}
          mode={mode}
          hint={toolHint(tool)}
        />

        <aside className="editor-panel">
          {selectedRoad ? (
            <>
              <h3>Selected Road</h3>
              <div className="stat-row"><span>Name</span><b>{selectedRoad.name}</b></div>
              <div className="stat-row"><span>Length</span><b>{(roadLengths.get(selectedRoad.id) ?? 0).toFixed(1)} m</b></div>
              <label className="field">
                <span>Lanes left</span>
                <input
                  type="number"
                  min={0}
                  max={6}
                  value={selectedRoad.lanesLeft}
                  onChange={(event) => updateRoad(selectedRoad.id, { lanesLeft: clampInt(event.target.value, 0, 6, 0) })}
                />
              </label>
              <label className="field">
                <span>Lanes right</span>
                <input
                  type="number"
                  min={0}
                  max={6}
                  value={selectedRoad.lanesRight}
                  onChange={(event) => updateRoad(selectedRoad.id, { lanesRight: clampInt(event.target.value, 0, 6, 0) })}
                />
              </label>
              <label className="field">
                <span>Lane width (m)</span>
                <input
                  type="number"
                  min={2}
                  max={5}
                  step={0.25}
                  value={selectedRoad.laneWidth}
                  onChange={(event) => updateRoad(selectedRoad.id, { laneWidth: clampNumber(event.target.value, 2, 5, 3.5) })}
                />
              </label>
              <button type="button" className="btn-ghost" onClick={reverseSelectedRoad}>Reverse Direction</button>
            </>
          ) : (
            <>
              <h3>New Road</h3>
              <label className="field">
                <span>Lanes left</span>
                <input type="number" min={0} max={6} value={config.lanesLeft} onChange={(event) => setConfig({ lanesLeft: clampInt(event.target.value, 0, 6, 1) })} />
              </label>
              <label className="field">
                <span>Lanes right</span>
                <input type="number" min={0} max={6} value={config.lanesRight} onChange={(event) => setConfig({ lanesRight: clampInt(event.target.value, 0, 6, 1) })} />
              </label>
              <label className="field">
                <span>Lane width (m)</span>
                <input type="number" min={2} max={5} step={0.25} value={config.laneWidth} onChange={(event) => setConfig({ laneWidth: clampNumber(event.target.value, 2, 5, 3.5) })} />
              </label>
              <label className="field">
                <span>Corner radius (m)</span>
                <input type="number" min={5} max={300} step={5} value={config.filletRadius} onChange={(event) => setConfig({ filletRadius: clampNumber(event.target.value, 5, 300, 50) })} />
              </label>
            </>
          )}

          <h3>Tool</h3>
          <p className="empty-note draw-instruction">{toolHint(tool)}</p>
          {draftPath && <div className="stat-row"><span>Draft length</span><b>{draftPath.length.toFixed(1)} m</b></div>}

          <h3>Roads</h3>
          <ul className="road-list">
            {project.roads.map((road) => (
              <li
                key={road.id}
                className={selectedRoadId === road.id ? 'selected' : ''}
                onClick={() => setSelectedRoadId(road.id)}
              >
                <span>{road.name}</span>
                <b>{(roadLengths.get(road.id) ?? 0).toFixed(1)} m</b>
              </li>
            ))}
          </ul>

          <h3>Junctions</h3>
          {junctions.length === 0 ? (
            <p className="empty-note">No road overlaps detected.</p>
          ) : (
            <ul className="junction-list">
              {junctions.map((junction) => (
                <li key={junction.id}>
                  <div><span>{junction.id}</span><b>{junction.connectingRoads.length} connections</b></div>
                  <button
                    type="button"
                    className="btn-ghost"
                    onClick={() => junction.suppressed ? restoreJunction(junction.key) : suppressJunction(junction.key)}
                  >
                    {junction.suppressed ? 'Create' : 'Detach'}
                  </button>
                </li>
              ))}
            </ul>
          )}
        </aside>
      </div>

      {selectedRoad && (
        <footer className="elevation-footer">
          <ElevationProfileEditor
            road={selectedRoad}
            length={roadLengths.get(selectedRoad.id) ?? 0}
            onChange={(profile) => updateRoad(selectedRoad.id, { elevationProfile: profile })}
          />
        </footer>
      )}
    </div>
  )
}

function nearestJunction(junctions: ReturnType<typeof buildJunctionNetwork>['junctions'], point: Vec2) {
  let best = null
  let bestDistance = Number.POSITIVE_INFINITY
  for (const junction of junctions) {
    const nextDistance = distance(junction.position, point)
    if (nextDistance < bestDistance) {
      best = junction
      bestDistance = nextDistance
    }
  }
  return bestDistance <= 15 ? best : null
}

function roadSection(road: RoadData) {
  return drawingSection(road.lanesLeft, road.lanesRight, road.laneWidth)
}

function drawingSection(lanesLeft: number, lanesRight: number, laneWidth: number) {
  return { left: Array(lanesLeft).fill(laneWidth), right: Array(lanesRight).fill(laneWidth) }
}

function toolHint(tool: Tool): string {
  return {
    select: 'Click a road to select it and edit its lanes.',
    'draw-straight': 'Hold left mouse, drag a straight road, and release.',
    'draw-polyline': 'Click each vertex; move to preview; press Enter or Finish when done.',
    'draw-arc': 'Click start and through-point; move to preview; click the endpoint.',
    move: 'Drag an existing road endpoint to a new position.',
    extend: 'Drag from either endpoint of an existing road.',
    split: 'Click an existing road where it should be split.',
    delete: 'Click a road to delete it.',
    junction: 'Click a detected junction to detach or recreate it.',
  }[tool]
}

function clampInt(value: string, min: number, max: number, fallback: number): number {
  const parsed = Number.parseInt(value, 10)
  return Number.isNaN(parsed) ? fallback : Math.max(min, Math.min(max, parsed))
}

function clampNumber(value: string, min: number, max: number, fallback: number): number {
  const parsed = Number.parseFloat(value)
  return Number.isNaN(parsed) ? fallback : Math.max(min, Math.min(max, parsed))
}

function distance(a: Vec2, b: Vec2): number {
  return Math.hypot(a.x - b.x, a.y - b.y)
}
