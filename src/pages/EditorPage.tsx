import { useEffect, useMemo, useRef, useState } from 'react'
import { toast } from 'sonner'
import { FileDown, Route, TrainFront, Trash2, Undo2, Redo2 } from 'lucide-react'
import { buildJunctionNetwork } from '../engine/junctions'
import { buildRailwayMesh, buildRoadMesh } from '../engine/mesh'
import { buildRoadSamplers, getLaneSection, validateRoad } from '../engine/roadServices'
import { buildEditorPreviewMeshes, flattenPreviewMeshes } from '../engine/previewMeshes'
import { fitRoadGeometry, nearestPointOnPath, sampledControlPoints } from '../engine/roadGeometry'
import { smoothPolylinePoints } from '../engine/tracks'
import { evaluatePath } from '../engine/geometry'
import type { MeshData } from '../engine/mesh'
import type { Vec2 } from '../engine/types'
import { useStore, uuid } from '../state/store'
import type { RailCrossing, RailPoint, RoadData, RoadGeometryType, Tool } from '../state/store'
import { DEFAULT_RAILWAY } from '../state/store'
import { exportNetworkDefinition } from '../engine/railNetwork'
import { exportOpenDrive } from '../engine/opendriveExport'
import { defaultLaneByType, laneLayout, profileSection, sectionHalfWidth } from '../engine/laneLayout'
import type { XYFunction, PolylineFunction } from '../engine/xyFunctions'
import {
  FUNCTION_LABELS,
  bezierConnector,
  convertPolylineToBezier,
  convertPolylineToClothoidArcs,
  convertPolylineToClothoidSpline,
  convertSplineToFunctions,
  functionEndFrame,
  functionRadiusOut,
  INFINITE_RADIUS,
  invertFunction,
  snapFrame,
  splitFunction,
} from '../engine/xyFunctions'
import {
  appendFunction,
  bindTrackFunctions,
  fitTrackPath,
  insertHandle,
  invertTrack,
  invertTrackFunctions,
  linkTrackFunctions,
  mergeFunctionPair,
  splitTrackFunctions,
  stickTrackToTerrain,
  trackSlices,
  trackStartFrame,
  trackTotalLength,
} from '../engine/tracks'
import type { Frame } from '../engine/xyFunctions'
import {
  allWays,
  authorizationKey,
  buildExitConnector,
  extractWaysFromIntersection,
  findTrackCrossing,
  makeIntersectionData,
  planInterchange,
  resolveTracks,
} from '../engine/intersections'
import { importOpenDrive } from '../engine/opendrive'
import type { IntersectionData } from '../engine/intersections'
import { makeTerrainSampler, getActiveTerrain } from '../terrain/terrainRegistry'
import { buildOverlays } from '../roads/overlays'
import { RoadsContextMenu } from '../roads/RoadsContextMenu'
import type { ContextMenuItem } from '../roads/RoadsContextMenu'
import { ScrollArea } from '@/components/ui/scroll-area'
import { Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { Button } from '@/components/ui/button'
import AppHeader from '@/components/layout/AppHeader'
import RoadViewport from '../viewport/RoadViewport'
import ElevationProfileEditor from '../elevation/ElevationProfileEditor'
import LanesTab from '../lanes/LanesTab'
import PortionProfileEditor from '../lanes/PortionProfileEditor'
import ToolRail from '../editor/ToolRail'
import EditorHeaderActions from '../editor/EditorHeaderActions'
import DraftPointsToolbar from '../editor/DraftPointsToolbar'
import ToolOptionsPanel from '../editor/ToolOptionsPanel'
import SelectionTab from '../editor/sidebar/SelectionTab'
import RoadsTab from '../editor/sidebar/RoadsTab'
import NetworkTab from '../editor/sidebar/NetworkTab'
import TrainTab from '../editor/sidebar/TrainTab'
import SidewalkPanel from '../editor/sidebar/SidewalkPanel'
import type { ToolSpace } from '../editor/ToolRail'
import { useKeyboardShortcuts } from '../editor/useKeyboardShortcuts'
import { distance, nearestJunction, toolHint } from '../editor/tooling'

interface RoadMeshEntry {
  roadId: string
  mesh: MeshData
}

interface ExtendSession {
  roadId: string
  contact: 'start' | 'end'
  start: Vec2
}

export default function EditorPage({ onBack }: { onBack: () => void }) {
  const projects = useStore((state) => state.projects)
  const activeProjectId = useStore((state) => state.activeProjectId)
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
  const setGeoRef = useStore((state) => state.setGeoRef)
  const selection = useStore((state) => state.selection)
  const setSelection = useStore((state) => state.setSelection)
  const editionConstraint = useStore((state) => state.editionConstraint)
  const setEditionConstraint = useStore((state) => state.setEditionConstraint)
  const layers = useStore((state) => state.layers)
  const setLayer = useStore((state) => state.setLayer)
  const insertOptions = useStore((state) => state.insertOptions)
  const setInsertOptions = useStore((state) => state.setInsertOptions)
  const lockedPassageways = useStore((state) => state.lockedPassageways)
  const setLockedPassageways = useStore((state) => state.setLockedPassageways)
  const addIntersection = useStore((state) => state.addIntersection)
  const updateIntersection = useStore((state) => state.updateIntersection)
  const deleteIntersectionById = useStore((state) => state.deleteIntersectionById)
  const insertLaneAt = useStore((state) => state.insertLaneAt)
  const removeLaneAt = useStore((state) => state.removeLaneAt)
  const setSelectedLaneKey = useStore((state) => state.setSelectedLane)
  const undo = useStore((state) => state.undo)
  const redo = useStore((state) => state.redo)
  const canUndo = useStore((state) => state.history.past.length > 0)
  const canRedo = useStore((state) => state.history.future.length > 0)
  const addRailPoint = useStore((state) => state.addRailPoint)
  const removeRailPointById = useStore((state) => state.removeRailPointById)
  const addRailCrossing = useStore((state) => state.addRailCrossing)
  const removeRailCrossingById = useStore((state) => state.removeRailCrossingById)
  const addCatchPoint = useStore((state) => state.addCatchPoint)
  const removeCatchPointById = useStore((state) => state.removeCatchPointById)

  const project = projects.find((item) => item.id === activeProjectId)
  const [draftPoints, setDraftPoints] = useState<Vec2[]>([])
  const [hoverPoint, setHoverPoint] = useState<Vec2 | null>(null)
  const [mode, setMode] = useState<'2d' | '3d'>('2d')
  const [showMap, setShowMap] = useState(false)
  const [menu, setMenu] = useState<{ screen: { x: number; y: number }; world: Vec2 } | null>(null)
  const [contourHandleArmed, setContourHandleArmed] = useState(false)
  // Editor top-level section: Road workspace or Train (railway) workspace.
  // Within Road, a sub-space splits road tools from lane tools.
  const [section, setSection] = useState<'road' | 'train'>('road')
  const [roadSpace, setRoadSpace] = useState<'road' | 'lane'>('road')
  const odrInputRef = useRef<HTMLInputElement>(null)
  // Begin/End Lane gizmo (doc 5.5.5.1.2): click a lane, then an arrow
  const [laneGizmo, setLaneGizmo] = useState<{
    roadId: string
    s: number
    side: 'left' | 'right'
    kind: 'begin' | 'end'
    point: Vec2
    tangent: number
    halfWidth: number
  } | null>(null)
  const dragStartRef = useRef<Vec2 | null>(null)
  const dragSnapRef = useRef<{ roadId: string; contact: 'start' | 'end'; frame: Frame } | null>(null)
  const extendRef = useRef<ExtendSession | null>(null)

  const selectedRoadId = selection.trackIds[0] ?? null
  const selectedRoad = project?.roads.find((road) => road.id === selectedRoadId) ?? null
  const selectedIntersection = (project?.intersections ?? []).find((item) => item.id === selection.intersectionId) ?? null

  const roadSamplers = useMemo(
    () => (project ? buildRoadSamplers(project.roads, false) : { elevation: new Map<string, (s: number) => number>(), banking: new Map<string, (s: number) => number>() }),
    [project],
  )
  const elevationSamplers = roadSamplers.elevation
  const bankingSamplers = roadSamplers.banking
  const junctionNetwork = useMemo(
    () => (project ? buildJunctionNetwork(project.roads, project.suppressedJunctions, elevationSamplers) : null),
    [project, elevationSamplers],
  )
  const junctions = junctionNetwork?.junctions ?? []
  const activeJunctions = junctions.filter((junction) => !junction.suppressed)

  // ─── Preview meshes (built via shared engine service) ──────────────
  // The Editor's 3D preview uses the shared buildEditorPreviewMeshes()
  // service so mesh-building logic is not duplicated in the component.
  const previewMeshes = useMemo(() => {
    if (!project || !junctionNetwork) {
      return { roadMeshes: [], connectingMeshes: [], intersectionWayMeshes: [], railFixtureMeshes: [], terrainMesh: null }
    }
    const terrain = mode === '3d' ? getActiveTerrain() : null
    return buildEditorPreviewMeshes(
      project,
      junctionNetwork,
      roadSamplers,
      layers,
      section === 'train',
      terrain,
    )
  }, [project, junctionNetwork, roadSamplers, layers, section, mode])

  const roadMeshEntries = previewMeshes.roadMeshes
  const connectingMeshes = previewMeshes.connectingMeshes
  const intersectionWayMeshes = previewMeshes.intersectionWayMeshes
  const railFixtureMeshes = previewMeshes.railFixtureMeshes
  const terrain3dMesh = previewMeshes.terrainMesh

  // ─── Draft preview (function-aware) ────────────────────────────────
  const draftSection = useMemo(
    () => (section === 'train'
      ? { left: [], right: [] }
      : profileSection(insertOptions.defaultProfile, config.laneWidth, config.lanesLeft, config.lanesRight)),
    [section, insertOptions.defaultProfile, config.laneWidth, config.lanesLeft, config.lanesRight],
  )
  const draftFnPreview = useMemo<XYFunction | null>(() => {
    const all = hoverPoint && draftPoints.length > 0 ? [...draftPoints, hoverPoint] : draftPoints
    if (all.length < 1) return null
    switch (tool) {
      case 'draw-clothoid': {
        if (all.length < 2) return null
        const [a, b] = all
        const length = Math.max(2, distance(a, b))
        const turnSign = config.clothoidTurn === 'left' ? 1 : -1
        return {
          kind: 'clothoid',
          radiusIn: dragSnapRef.current?.frame && draftPoints.length > 0 ? neighborRadiusIn(dragSnapRef.current.roadId) ?? INFINITE_RADIUS : INFINITE_RADIUS,
          radiusOut: config.clothoidRadiusOut > 0 ? config.clothoidRadiusOut * turnSign : INFINITE_RADIUS,
          length,
        }
      }
      case 'draw-polyline':
        return all.length >= 2 ? { kind: 'polyline', points: all, splineType: 'segment' } : null
      case 'draw-spline':
        return all.length >= 2 ? { kind: 'clothoidSpline', points: all, tolerance: 0.5, symmetryThreshold: 1 } : null
      case 'draw-bezier':
        if (all.length < 2) return null
        return bezierConnector({ x: all[0].x, y: all[0].y, heading: Math.atan2(all[1].y - all[0].y, all[1].x - all[0].x) }, { x: all[all.length - 1].x, y: all[all.length - 1].y, heading: Math.atan2(all[all.length - 1].y - all[all.length - 2].y, all[all.length - 1].x - all[all.length - 2].x) })
      default:
        return null
    }
    function neighborRadiusIn(roadId: string): number | null {
      const road = project?.roads.find((r) => r.id === roadId)
      if (!road?.functions || road.functions.length === 0) return null
      return functionRadiusOut(road.functions[road.functions.length - 1])
    }
  }, [tool, draftPoints, hoverPoint, config.clothoidRadiusOut, config.clothoidTurn, project])

  const draftPath = useMemo(() => {
    if (draftFnPreview) {
      const all = hoverPoint && draftPoints.length > 0 ? [...draftPoints, hoverPoint] : draftPoints
      if (all.length < 1) return null
      const origin = all[0]
      let heading = dragSnapRef.current?.frame.heading ?? 0
      if (!dragSnapRef.current && all.length >= 2) heading = Math.atan2(all[1].y - all[0].y, all[1].x - all[0].x)
      if (draftFnPreview.kind === 'polyline' || draftFnPreview.kind === 'clothoidSpline') {
        return fitTrackPath({
          id: 'draft',
          points: [{ x: origin.x, y: origin.y }],
          functions: [draftFnPreview],
          lanesLeft: 0, lanesRight: 0, laneWidth: 0,
        })
      }
      return fitTrackPath({
        id: 'draft',
        points: [origin, { x: origin.x + Math.cos(heading), y: origin.y + Math.sin(heading) }],
        functions: [draftFnPreview],
        lanesLeft: 0, lanesRight: 0, laneWidth: 0,
      })
    }
    // legacy previews: straight / polyline / arc
    const previewPoints = !hoverPoint || draftPoints.length === 0 ? draftPoints
      : tool === 'draw-polyline' || tool === 'draw-arc' ? [...draftPoints, hoverPoint] : draftPoints
    if (previewPoints.length < 2) return null
    const geometryType: RoadGeometryType = tool === 'draw-arc' && previewPoints.length >= 3 ? 'arc' : tool === 'draw-polyline' ? 'polyline' : 'straight'
    return fitRoadGeometry({ points: previewPoints, geometryType, filletRadius: config.filletRadius })
  }, [draftFnPreview, draftPoints, hoverPoint, tool, config.filletRadius])

  const draftMesh = useMemo(
    () => (draftPath && tool !== 'select' && tool !== 'insert-intersection' && tool !== 'junction' && !tool.startsWith('lane-')
      ? section === 'train'
        ? buildRailwayMesh(draftPath, DEFAULT_RAILWAY)
        : buildRoadMesh(draftPath, draftSection).pavement
      : null),
    [draftPath, draftSection, tool, section],
  )

  const roadLengths = useMemo(() => {
    if (!project) return new Map<string, number>()
    return new Map(project.roads.map((road) => [road.id, fitRoadGeometry(road)?.length ?? 0]))
  }, [project])

  const gizmoMarkers = useMemo<import('../viewport/RoadViewport').OverlayMarker[]>(() => {
    if (!laneGizmo) return []
    return [
      { point: laneGizmo.point, color: '#fb923c', shape: 'circle', size: 1.4 },
      ...gizmoArrows(laneGizmo).map((arrow) => ({
        id: `laneGizmo:${arrow.key}`,
        point: arrow.point,
        color: '#f97316',
        shape: 'arrow' as const,
        heading: arrow.heading,
        size: 5,
      })),
    ]
  }, [laneGizmo])

  // ─── Overlays (axes, arrows, intersections, ways, exits) ───────────
  const overlays = useMemo(() => {
    if (!project) return { lines: [], markers: [], exits: [] }
    const built = buildOverlays({
      project,
      layers,
      selection,
      lockedPassageways,
      selectedTrackStation: selection.trackStation,
    })
    return { lines: built.lines, markers: [...built.markers, ...gizmoMarkers], exits: built.exits }
  }, [project, layers, selection, lockedPassageways, gizmoMarkers])



  // ─── Selected road helpers ─────────────────────────────────────────
  const selectedRoadLength = selectedRoad ? roadLengths.get(selectedRoad.id) ?? 0 : 0

  function selectRoadOnly(roadId: string | null, station?: number) {
    setSelection({ trackIds: roadId ? [roadId] : [], intersectionId: null, trackStation: station ?? null })
  }

  function chooseTool(next: Tool) {
    setTool(next)
    dragStartRef.current = null
    dragSnapRef.current = null
    extendRef.current = null
    setDraftPoints([])
    setHoverPoint(null)
  }

  function switchSection(next: 'road' | 'train') {
    if (next === section) return
    setSection(next)
    chooseTool('select')
  }

  function switchRoadSpace(next: 'road' | 'lane') {
    if (next === roadSpace) return
    setRoadSpace(next)
    chooseTool(next === 'lane' ? 'lane-insert' : 'select')
  }

  // Section drives which tool rail is shown.
  const toolSpace: ToolSpace = section === 'train' ? 'train' : roadSpace

  // ─── Profile presets ("Default Profile") ───────────────────────────

  // ─── Creating function-based roads ─────────────────────────────────
  function startFramePoints(frame: Frame): Vec2[] {
    return [
      { x: frame.x, y: frame.y },
      { x: frame.x + Math.cos(frame.heading), y: frame.y + Math.sin(frame.heading) },
    ]
  }

  function createFunctionRoad(frame: Frame, functions: XYFunction[], nameSuffix = '') {
    if (!project || functions.length === 0) return
    const railway = section === 'train'
    const road: RoadData = {
      id: uuid(),
      name: `${railway ? 'Track' : 'Road'} ${project.roads.length + 1}${nameSuffix}`,
      points: startFramePoints(frame),
      functions,
      lanesLeft: railway ? 0 : draftSection.left.length,
      lanesRight: railway ? 0 : draftSection.right.length,
      laneWidth: config.laneWidth,
      filletRadius: config.filletRadius,
      laneSection: railway ? { left: [], right: [] } : draftSection,
      ...(railway ? { railway: { ...DEFAULT_RAILWAY } } : {}),
    }
    addRoad(road)
    applyStickToTerrain(road)
    selectRoadOnly(road.id)
  }

  function applyStickToTerrain(road: RoadData) {
    if (!insertOptions.stickToTerrain || !project) return
    stickRoadToTerrain(road, 'No background terrain covers this track. Load terrain in the Terrain workspace first.')
  }

  /** Shared altitude + banking picking for "Stick to Background Terrain". */
  function stickRoadToTerrain(road: RoadData, missWarning: string) {
    if (!project) return
    const sampler = makeTerrainSampler(project.geoRef)
    const halfWidth = road.railway ? road.railway.trackbedWidth / 2 : sectionHalfWidth(getLaneSection(road))
    const result = stickTrackToTerrain({ ...road, elevationProfile: undefined }, sampler, halfWidth)
    if (result) {
      updateRoad(road.id, { elevationProfile: result.elevation, bankingProfile: result.banking })
      const maxBankDeg = result.banking.reduce((m, p) => Math.max(m, Math.abs((p.z * 180) / Math.PI)), 0)
      toast.success(`Track stuck to Background Terrain — altitude + banking picked (max cant ${maxBankDeg.toFixed(1)}°)`)
    } else {
      toast.warning(missWarning)
    }
  }

  /**
   * Doc: editing a track connected to an intersection that owns imported
   * (explicit) ways pops a warning that the explicit data will be lost.
   * Returns false when the user cancels; continuing drops the explicit ways.
   */
  function guardExplicitWays(roadId: string): boolean {
    if (!project) return true
    const linked = (project.intersections ?? []).filter(
      (it) => (it.explicitWays?.length ?? 0) > 0 && it.trackEnds.some((e) => e.trackId === roadId),
    )
    if (linked.length === 0) return true
    if (!window.confirm('A connected intersection owns imported (explicit) ways. Continuing will invalidate that explicit data. Continue?')) {
      return false
    }
    for (const it of linked) updateIntersection(it.id, { explicitWays: [] })
    return true
  }

  // ─── OpenDRIVE import (source of explicit ways) ────────────────────
  function applyOdrImport(xml: string) {
    if (!project) return
    const result = importOpenDrive(xml)
    if (!result) {
      toast.error('Invalid OpenDRIVE file (XML parse error).')
      return
    }
    const section = profileSection(insertOptions.defaultProfile, config.laneWidth, config.lanesLeft, config.lanesRight)
    for (const road of result.roads) {
      addRoad({ ...road, laneSection: section, filletRadius: config.filletRadius })
    }
    for (const node of result.intersections) addIntersection(node)
    for (const warning of result.warnings.slice(0, 3)) toast.warning(warning)
    const wayCount = result.intersections.reduce((sum, j) => sum + (j.explicitWays?.length ?? 0), 0)
    toast.success(`OpenDRIVE import: ${result.roads.length} road(s), ${result.intersections.length} junction(s), ${wayCount} explicit way(s)`)
  }

  async function handleOdrImport(event: import('react').ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0]
    if (!file || !project) return
    const text = await file.text()
    event.target.value = ''
    applyOdrImport(text)
  }

  // automation hook for UI verification
  useEffect(() => {
    (window as unknown as Record<string, unknown>).__ogsImportOpenDrive = (xml: string) => applyOdrImport(xml)
  })

  /** Convert a legacy road to an XY-function road (polyline chain). */
  function ensureFunctions(road: RoadData): RoadData {
    if (road.functions && road.functions.length > 0) return road
    const path = fitRoadGeometry(road)
    if (!path) return road
    const samples = sampledControlPoints(path, 0, path.length, 4)
    const start = { x: samples[0].x, y: samples[0].y }
    const heading = Math.atan2(samples[1].y - samples[0].y, samples[1].x - samples[0].x)
    return {
      ...road,
      points: startFramePoints({ x: start.x, y: start.y, heading }),
      functions: [{ kind: 'polyline', points: samples, splineType: 'segment' }],
    }
  }

  /** Endpoint snap candidates from all function roads (auto-attach). */
  function endpointFrames() {
    if (!project) return []
    const out: { roadId: string; contact: 'start' | 'end'; frame: Frame }[] = []
    for (const road of project.roads) {
      if (!road.functions || road.functions.length === 0) continue
      const start = trackStartFrame(road)
      if (!start) continue
      const slices = trackSlices(road)
      if (!slices) continue
      const lastSlice = slices[slices.length - 1]
      const end = functionEndFrame(lastSlice.start, lastSlice.fn)
      out.push({ roadId: road.id, contact: 'start', frame: start })
      out.push({ roadId: road.id, contact: 'end', frame: end })
    }
    // legacy roads: endpoints only, heading along the last/first span
    for (const road of project.roads) {
      if (road.functions && road.functions.length > 0) continue
      const pts = road.points
      if (!pts || pts.length < 2) continue
      out.push({ roadId: road.id, contact: 'start', frame: { x: pts[0].x, y: pts[0].y, heading: Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) } })
      out.push({ roadId: road.id, contact: 'end', frame: { x: pts[pts.length - 1].x, y: pts[pts.length - 1].y, heading: Math.atan2(pts[pts.length - 1].y - pts[pts.length - 2].y, pts[pts.length - 1].x - pts[pts.length - 2].x) } })
    }
    return out
  }

  function snapToEndpoint(point: Vec2) {
    return snapFrame(endpointFrames().map((e) => ({ frame: e.frame, roadId: e.roadId, contact: e.contact })), point, 3)
  }

  // ─── Drawing interactions ──────────────────────────────────────────
  function beginDraftAt(point: Vec2, forcedHeading?: number) {
    const snap = snapToEndpoint(point)
    if (snap) {
      dragSnapRef.current = { roadId: snap.roadId, contact: snap.contact, frame: snap.frame }
      setDraftPoints([{ x: snap.frame.x, y: snap.frame.y }])
      if (forcedHeading === undefined) toast.info('Attached to existing track extremity.')
      return { x: snap.frame.x, y: snap.frame.y }
    }
    dragSnapRef.current = null
    // on-track snap: start exactly on a nearby road axis
    const onTrack = snapToTrack(point)
    if (onTrack) {
      setDraftPoints([onTrack])
      return onTrack
    }
    setDraftPoints([point])
    return point
  }

  function handleDragStart(point: Vec2) {
    if (tool === 'draw-straight') {
      const start = beginDraftAt(point)
      dragStartRef.current = start
      setDraftPoints([start, start])
      return
    }
    if (tool === 'draw-clothoid') {
      const start = beginDraftAt(point)
      dragStartRef.current = start
      setDraftPoints([start, start])
      return
    }
    if (tool === 'extend' || tool === 'move') {
      const session = nearestEndpoint(point)
      extendRef.current = session
      if (session) {
        selectRoadOnly(session.roadId)
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
      const snap = dragSnapRef.current
      dragStartRef.current = null
      dragSnapRef.current = null
      setDraftPoints([])
      if (!start || distance(start, point) < 1) return
      const heading = Math.atan2(point.y - start.y, point.x - start.x)
      const fn: XYFunction = { kind: 'segment', length: distance(start, point) }
      commitFunction(fn, { x: start.x, y: start.y, heading }, snap)
      return
    }
    if (tool === 'draw-clothoid') {
      const start = dragStartRef.current
      const snap = dragSnapRef.current
      dragStartRef.current = null
      dragSnapRef.current = null
      setDraftPoints([])
      if (!start || distance(start, point) < 2) return
      const heading = Math.atan2(point.y - start.y, point.x - start.x)
      const turnSign = config.clothoidTurn === 'left' ? 1 : -1
      let radiusIn = INFINITE_RADIUS
      if (snap) {
        const sourceRoad = project?.roads.find((r) => r.id === snap.roadId)
        if (sourceRoad?.functions && sourceRoad.functions.length > 0) {
          radiusIn = functionRadiusOut(sourceRoad.functions[sourceRoad.functions.length - 1])
        }
      }
      const fn: XYFunction = {
        kind: 'clothoid',
        radiusIn,
        radiusOut: config.clothoidRadiusOut > 0 ? config.clothoidRadiusOut * turnSign : INFINITE_RADIUS,
        length: distance(start, point),
      }
      commitFunction(fn, { x: start.x, y: start.y, heading }, snap)
      return
    }
    if (tool === 'extend' || tool === 'move') {
      const session = extendRef.current
      extendRef.current = null
      setDraftPoints([])
      if (!session || distance(session.start, point) < 1 || !project) return
      const road = project.roads.find((item) => item.id === session.roadId)
      if (!road) return
      if (road.functions && road.functions.length > 0) {
        adjustTrackEnd(road, session.contact, point, tool === 'move')
        return
      }
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

  /** Append the drawn function to a snapped track, or create a new road. */
  function commitFunction(fn: XYFunction, frame: Frame, snap: { roadId: string; contact: 'start' | 'end'; frame: Frame } | null) {
    if (snap) {
      const road = project?.roads.find((r) => r.id === snap.roadId)
      if (road && road.functions && road.functions.length > 0 && snap.contact === 'end') {
        const next = appendFunction(road, fn.kind === 'clothoid' && fn.radiusIn === INFINITE_RADIUS
          ? { ...fn, radiusIn: functionRadiusOut(road.functions[road.functions.length - 1]) }
          : fn)
        updateRoad(road.id, { functions: next })
        selectRoadOnly(road.id)
        toast.success(`${FUNCTION_LABELS[fn.kind]} added to ${road.name}`)
        return
      }
      if (road && road.functions && road.functions.length > 0 && snap.contact === 'start') {
        // attach at start: invert road, append, invert back
        const inverted = invertTrackFunctions(road)
        if (inverted) {
          const appended = [...inverted, fn.kind === 'clothoid' && fn.radiusIn === INFINITE_RADIUS
            ? { ...fn, radiusIn: functionRadiusOut(inverted[inverted.length - 1]) }
            : fn]
          const restored = [...appended].reverse().map((f) => invertFunction(f))
          updateRoad(road.id, { functions: restored })
          selectRoadOnly(road.id)
          toast.success(`${FUNCTION_LABELS[fn.kind]} added to ${road.name} (start)`)
          return
        }
      }
      // legacy road: create a function road starting at its extremity
      createFunctionRoad(frame, [fn])
      return
    }
    createFunctionRoad(frame, [fn])
  }

  /** Extend / Move End on a function-based track (constraint aware). */
  function adjustTrackEnd(road: RoadData, contact: 'start' | 'end', point: Vec2, allowHeading: boolean) {
    if (!guardExplicitWays(road.id)) return
    const slices = trackSlices(road)
    if (!slices || slices.length === 0) return
    if (contact === 'end') {
      const slice = slices[slices.length - 1]
      const functions = [...road.functions!]
      functions[slice.index] = retargetFunction(slice, point, editionConstraint, allowHeading)
      updateRoad(road.id, { functions })
      return
    }
    // contact 'start': work in inverted space, then invert back. The
    // inverted chain starts at the original end frame.
    const inverted = invertTrack(road)
    if (!inverted) return
    const invStartPoints = startFramePoints(inverted.startFrame)
    const slicesInv = trackSlices({ ...road, points: invStartPoints, functions: inverted.functions })
    if (!slicesInv) return
    const slice = slicesInv[slicesInv.length - 1]
    const functions = [...inverted.functions]
    functions[slice.index] = retargetFunction(slice, point, editionConstraint, allowHeading)
    // walk the adjusted inverted chain to find the restored chain's start
    let frame = inverted.startFrame
    for (const s of trackSlices({ ...road, points: invStartPoints, functions }) ?? []) {
      frame = functionEndFrame(s.start, s.fn)
    }
    const restored = [...functions].reverse().map(invertFunction)
    updateRoad(road.id, { functions: restored, points: startFramePoints(frame) })
  }

  function retargetFunction(
    slice: { fn: XYFunction; start: Frame; length: number },
    point: Vec2,
    constraint: string,
    allowHeading: boolean,
  ): XYFunction {
    const fn = slice.fn
    const d = distance({ x: slice.start.x, y: slice.start.y }, point)
    switch (fn.kind) {
      case 'segment': {
        if (!allowHeading) return { ...fn, length: Math.max(0.01, d) }
        return fn // heading changes need a new direction: keep length
      }
      case 'arc': {
        const headingToPoint = Math.atan2(point.y - slice.start.y, point.x - slice.start.x)
        let deflection = headingToPoint - slice.start.heading
        while (deflection > Math.PI) deflection -= Math.PI * 2
        while (deflection < -Math.PI) deflection += Math.PI * 2
        if (constraint === 'fixedLength') {
          // solve radius so that chord matches with fixed arc length
          const len = fn.radius * Math.abs(fn.angle)
          const radius = solveRadiusForChord(d, len) ?? fn.radius
          return { ...fn, radius: Math.max(0.01, radius) }
        }
        // free / fixed radius: adjust sweep
        const angle = 2 * deflection
        return { ...fn, angle }
      }
      case 'clothoid': {
        const curLen = fn.length
        const scale = Math.max(0.05, d / Math.max(1, curLen))
        return { ...fn, length: Math.max(0.5, curLen * scale) }
      }
      case 'polyline':
        return { ...fn, points: [...fn.points.slice(0, -1), point] }
      case 'clothoidSpline':
        return { ...fn, points: [...fn.points.slice(0, -1), point] }
      case 'bezier':
        return { ...fn, p3: point }
    }
  }

  function solveRadiusForChord(chord: number, arcLength: number): number | null {
    // chord = 2 r sin(L / 2r) → solve for r by bisection
    let lo = chord / 2
    let hi = Math.max(chord, arcLength) * 4
    for (let i = 0; i < 60; i++) {
      const mid = (lo + hi) / 2
      const c = 2 * mid * Math.sin(arcLength / (2 * mid))
      if (c < chord) lo = mid
      else hi = mid
    }
    return (lo + hi) / 2
  }

  function handleDragCancel() {
    dragStartRef.current = null
    dragSnapRef.current = null
    extendRef.current = null
    setDraftPoints([])
  }

  // ─── Click interactions ────────────────────────────────────────────
  function handleGroundClick(point: Vec2, event: PointerEvent) {
    if (!project || !junctionNetwork) return
    const additive = event.ctrlKey || event.metaKey || event.shiftKey

    // Begin/End Lane gizmo: click one of the two arrows to commit
    if (laneGizmo && (tool === 'lane-begin' || tool === 'lane-end')) {
      const arrows = gizmoArrows(laneGizmo)
      let picked: 'before' | 'after' | null = null
      let bestDistance = 12
      for (const arrow of arrows) {
        const d = distance(arrow.point, point)
        if (d < bestDistance) {
          bestDistance = d
          picked = arrow.key
        }
      }
      if (picked) {
        commitGizmoLane(laneGizmo, picked)
      } else {
        toast.info('Gizmo cancelled — click a lane to restart.')
      }
      setLaneGizmo(null)
      return
    }

    if (contourHandleArmed && selectedIntersection) {
      updateIntersection(selectedIntersection.id, {
        contourHandles: [...selectedIntersection.contourHandles, point],
      })
      setContourHandleArmed(false)
      toast.success('Contour handle inserted')
      return
    }

    if (tool === 'draw-polyline' || tool === 'draw-spline') {
      if (draftPoints.length === 0) {
        beginDraftAt(point)
      } else {
        const last = draftPoints[draftPoints.length - 1]
        if (!(last && distance(last, point) < 0.1)) setDraftPoints([...draftPoints, point])
      }
      return
    }
    if (tool === 'draw-bezier') {
      if (draftPoints.length === 0) {
        beginDraftAt(point)
      } else {
        const start = dragSnapRef.current?.frame
          ? { ...dragSnapRef.current.frame }
          : { x: draftPoints[0].x, y: draftPoints[0].y, heading: Math.atan2(point.y - draftPoints[0].y, point.x - draftPoints[0].x) }
        const snap = dragSnapRef.current
        const fn = bezierConnector(start, { x: point.x, y: point.y, heading: Math.atan2(point.y - start.y, point.x - start.x) })
        dragSnapRef.current = null
        setDraftPoints([])
        commitFunction(fn, start, snap)
      }
      return
    }
    if (tool === 'draw-arc') {
      const last = draftPoints[draftPoints.length - 1]
      if (draftPoints.length === 0) {
        beginDraftAt(point)
        return
      }
      if (last && distance(last, point) < 0.1) return
      const next = [...draftPoints, point]
      if (next.length === 3) {
        const snap = dragSnapRef.current
        dragSnapRef.current = null
        setDraftPoints([])
        setHoverPoint(null)
        const fitted = fitRoadGeometry({ points: next, geometryType: 'arc', filletRadius: 0 })
        if (fitted && fitted.elements[0]?.type === 'arc') {
          const el = fitted.elements[0]
          const fn: XYFunction = {
            kind: 'arc',
            radius: 1 / Math.abs(el.curvature),
            angle: el.curvature * el.length,
          }
          const heading = Math.atan2(next[1].y - next[0].y, next[1].x - next[0].x)
          commitFunction(fn, { x: next[0].x, y: next[0].y, heading }, snap)
        }
      } else {
        setDraftPoints(next)
      }
      return
    }
    if (tool === 'insert-intersection') {
      const id = `intersection-${(project.intersections ?? []).length + 1}-${uuid().slice(0, 4)}`
      const node = makeIntersectionData(id, point)
      addIntersection(node)
      setSelection({ trackIds: [], intersectionId: id, trackStation: null })
      toast.success('Intersection inserted. Select tracks then Ctrl+L to link.')
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
    if (tool === 'rail-point') {
      createRailPointAt(point)
      return
    }
    if (tool === 'rail-crossing') {
      createRailCrossingAt(point)
      return
    }
    if (tool === 'catch-point') {
      createCatchPointAt(point)
      return
    }

    const hit = nearestRoad(point)
    // intersection picking: nearest node within 10 m wins over roads
    const intersections = project.intersections ?? []
    let nodeHit: IntersectionData | null = null
    let nodeDistance = Number.POSITIVE_INFINITY
    for (const node of intersections) {
      const d = distance(node.position, point)
      if (d < 10 && d < nodeDistance) {
        nodeHit = node
        nodeDistance = d
      }
    }

    if (tool === 'select') {
      if (nodeHit) {
        setSelection({ trackIds: additive ? selection.trackIds : [], intersectionId: nodeHit.id, trackStation: null })
        return
      }
      if (additive && hit) {
        setSelection({
          trackIds: selection.trackIds.includes(hit.road.id)
            ? selection.trackIds.filter((id) => id !== hit.road.id)
            : [...selection.trackIds, hit.road.id],
          // keep the intersection selected so Ctrl+L can link the tracks to it
          intersectionId: selection.intersectionId,
          trackStation: hit.s,
        })
        return
      }
      selectRoadOnly(hit?.road.id ?? null, hit?.s)
      return
    }
    if (tool === 'delete') {
      if (nodeHit) {
        deleteIntersectionById(nodeHit.id)
        toast.success('Intersection deleted')
        return
      }
      if (hit) {
        deleteRoad(hit.road.id)
        if (selection.trackIds.includes(hit.road.id)) {
          setSelection({ ...selection, trackIds: selection.trackIds.filter((id) => id !== hit.road.id) })
        }
      }
      return
    }
    if (tool === 'split' && hit) {
      splitRoadAt(hit.road, hit.s)
      return
    }
    if ((tool === 'lane-begin' || tool === 'lane-end') && hit) {
      const proj = projectOntoRoad(hit.road, point)
      const path = junctionNetwork.paths.get(hit.road.id)
      if (!proj || !path) return
      const sample = evaluatePath(path, proj.s)
      setLaneGizmo({
        roadId: hit.road.id,
        s: proj.s,
        side: proj.t >= 0 ? 'left' : 'right',
        kind: tool === 'lane-begin' ? 'begin' : 'end',
        point,
        tangent: sample.heading,
        halfWidth: sectionHalfWidth(getLaneSection(hit.road)),
      })
      return
    }
    if (tool === 'lane-insert' && hit) {
      insertLaneNearPoint(hit.road, point)
    } else if (tool === 'lane-remove' && hit) {
      removeLaneNearPoint(hit.road, point)
    } else if (tool === 'lane-sidewalk' && hit) {
      addSidewalkCurb(hit.road)
      selectRoadOnly(hit.road.id)
    } else if (tool === 'lane-border' && hit) {
      selectRoadOnly(hit.road.id)
      toast.info('Select a lane border in the Lanes tab to edit its height / offset.')
    }
  }

  function handleGroundHover(point: Vec2) {
    if (draftPoints.length > 0 && ['draw-polyline', 'draw-spline', 'draw-arc', 'draw-bezier'].includes(tool)) {
      setHoverPoint(point)
    }
  }

  // ─── Track operations ──────────────────────────────────────────────
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
      // true chain extremities: for function roads road.points is only the
      // 2-point start frame, so the end must come from the function chain
      for (const contact of ['start', 'end'] as const) {
        const frame = exitFrame(road, contact)
        if (!frame) continue
        const distance = Math.hypot(point.x - frame.x, point.y - frame.y)
        if (!best || distance < best.distance) {
          best = { session: { roadId: road.id, contact, start: { x: frame.x, y: frame.y } }, distance }
        }
      }
    }
    return best && best.distance <= 10 ? best.session : null
  }

  /** Nearest point on any road axis within 4 m (on-track snap). */
  function snapToTrack(point: Vec2): Vec2 | null {
    if (!project || !junctionNetwork) return null
    let best: { point: Vec2; distance: number } | null = null
    for (const road of project.roads) {
      const path = junctionNetwork.paths.get(road.id)
      if (!path) continue
      const nearest = nearestPointOnPath(path, point)
      if (!best || nearest.distance < best.distance) best = { point: nearest.point, distance: nearest.distance }
    }
    return best && best.distance <= 4 ? best.point : null
  }

  /** Station of the function under the context-click on a road. */
  function activeFunctionIndexAt(road: RoadData, station: number | null): number {
    const slices = trackSlices(road)
    if (!slices || station === null) return 0
    for (const slice of slices) {
      if (station >= slice.offset && station <= slice.offset + slice.length) return slice.index
    }
    return slices.length - 1
  }

  /** End frame of a function chain that starts where `road` starts. */
  function chainEndFrame(road: RoadData, functions: XYFunction[]): Frame | null {
    const start = trackStartFrame({ ...road, functions })
    if (!start) return null
    let frame = start
    for (const fn of functions) frame = functionEndFrame(frame, fn)
    return frame
  }

  function splitFunctionAt(road: RoadData, station: number) {
    if (!guardExplicitWays(road.id)) return
    const slices = trackSlices(road)
    if (!slices) return
    for (const slice of slices) {
      if (station > slice.offset + 0.2 && station < slice.offset + slice.length - 0.2) {
        const pieces = splitFunction(slice.start, slice.fn, station - slice.offset)
        if (!pieces) {
          toast.error('Cannot split this function here.')
          return
        }
        const functions = [...road.functions!]
        functions.splice(slice.index, 1, pieces[0], pieces[1])
        updateRoad(road.id, { functions })
        toast.success('Function split in two')
        return
      }
    }
    toast.error('Click closer to the middle of a function to split it.')
  }

  function mergeAtStation(road: RoadData, station: number | null) {
    const functions = road.functions ?? []
    const index = activeFunctionIndexAt(road, station)
    const merged = mergeFunctionPair(functions, index) ?? mergeFunctionPair(functions, Math.max(0, index - 1))
    if (!merged) {
      toast.error('Selected functions cannot be merged (same type and continuous radius required).')
      return
    }
    updateRoad(road.id, { functions: merged })
    toast.success('Functions merged')
  }

  function splitRoadAt(road: RoadData, s: number) {
    const path = fitRoadGeometry(road)
    if (!path || s < 1 || path.length - s < 1) return
    if (road.functions && road.functions.length > 0) {
      const pieces = splitTrackFunctions(road, s)
      if (!pieces) return
      const frame = chainEndFrame(road, pieces.functionsA)
      if (!frame) return
      const first: RoadData = {
        ...road,
        id: uuid(),
        name: `${road.name} A`,
        functions: pieces.functionsA,
      }
      const second: RoadData = {
        ...road,
        id: uuid(),
        name: `${road.name} B`,
        points: startFramePoints(frame),
        functions: pieces.functionsB,
      }
      replaceRoad(road.id, [first, second])
      setSelection({ trackIds: [first.id], intersectionId: null, trackStation: null })
      toast.success('Track split in two (still adjacent)')
      return
    }
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
    setSelection({ trackIds: [first.id], intersectionId: null, trackStation: null })
  }

  function invertSelectedOrientation() {
    const road = selectedRoad
    if (!road) return
    if (!guardExplicitWays(road.id)) return
    if (road.functions && road.functions.length > 0) {
      const inverted = invertTrack(road)
      if (!inverted) return
      updateRoad(road.id, {
        functions: inverted.functions,
        points: startFramePoints(inverted.startFrame),
        lanesLeft: road.lanesRight,
        lanesRight: road.lanesLeft,
        laneSection: road.laneSection ? { left: road.laneSection.right, right: road.laneSection.left } : undefined,
        subNetworkExits: road.subNetworkExits?.map((c) => (c === 'start' ? 'end' : 'start')) as ('start' | 'end')[] | undefined,
      })
    } else {
      updateRoad(road.id, {
        points: [...road.points].reverse(),
        lanesLeft: road.lanesRight,
        lanesRight: road.lanesLeft,
      })
    }
    toast.success('Track orientation inverted')
  }

  function stickSelectedToTerrain() {
    const road = selectedRoad
    if (!road || !project) return
    stickRoadToTerrain(ensureFunctions(road), 'No background terrain loaded. Download terrain in the Terrain workspace first.')
  }

  function linkSelectedTracks() {
    if (!project || selection.trackIds.length !== 2) return
    const [idA, idB] = selection.trackIds
    const a = ensureFunctions(project.roads.find((r) => r.id === idA)!)
    const b = ensureFunctions(project.roads.find((r) => r.id === idB)!)
    // First track imposes its direction; join at A's end and B's nearest end.
    const contactA = contactFromStation(a, selection.trackStation)
    const contactB = nearestContactTo(b, exitFrame(a, contactA))
    const result = linkTrackFunctions(a, b, { contactA, contactB })
    if (!result) {
      toast.error('Link Tracks failed: both tracks must use XY functions.')
      return
    }
    updateRoad(a.id, { functions: result.functions, points: startFramePoints(result.startFrame), name: a.name })
    deleteRoad(b.id)
    setSelection({ trackIds: [a.id], intersectionId: null, trackStation: null })
    toast.success('Tracks linked with a spline connector (first track kept its direction)')
  }

  function contactFromStation(road: RoadData, station: number | null): 'start' | 'end' {
    const length = fitTrackPath(road)?.length ?? trackTotalLength(road)
    if (station === null) return 'end'
    return station < length / 2 ? 'start' : 'end'
  }

  function nearestContactTo(road: RoadData, target: Frame | null): 'start' | 'end' {
    if (!target) return 'end'
    const start = exitFrame(road, 'start')
    const end = exitFrame(road, 'end')
    if (!start || !end) return 'end'
    return distance(start, target) <= distance(end, target) ? 'start' : 'end'
  }

  function bindSelectedTracks() {
    if (!project || selection.trackIds.length !== 2) return
    const [idA, idB] = selection.trackIds
    const a = ensureFunctions(project.roads.find((r) => r.id === idA)!)
    const b = ensureFunctions(project.roads.find((r) => r.id === idB)!)
    const contactA = contactFromStation(a, selection.trackStation)
    const contactB = nearestContactTo(b, exitFrame(a, contactA))
    const result = bindTrackFunctions(a, b, { contactA, contactB })
    if (!result) {
      toast.error('Bind Tracks failed.')
      return
    }
    updateRoad(b.id, { functions: result.functions, points: startFramePoints(result.startFrame) })
    toast.success('Tracks bound — the second track moved onto the first')
  }

  // ─── Conversions ───────────────────────────────────────────────────
  function convertActivePolyline(target: 'bezier' | 'clothoidArc' | 'clothoidSpline') {
    const road = selectedRoad
    if (!road?.functions) return
    if (!guardExplicitWays(road.id)) return
    const slices = trackSlices(road)
    if (!slices) return
    const index = activeFunctionIndexAt(road, selection.trackStation)
    const slice = slices[index]
    if (slice.fn.kind !== 'polyline') {
      toast.error('Select a segment-type polyline first (Polyline Spline: Segment).')
      return
    }
    const fn = slice.fn as PolylineFunction
    const functions = [...road.functions]
    if (target === 'bezier') {
      functions.splice(index, 1, convertPolylineToBezier(slice.start, fn))
      toast.success('Polyline converted to Bezier')
    } else if (target === 'clothoidArc') {
      functions.splice(index, 1, ...convertPolylineToClothoidArcs(fn))
      toast.success('Polyline converted to clothoid arcs + circle segments')
    } else {
      functions.splice(index, 1, convertPolylineToClothoidSpline(fn))
      toast.success('Polyline converted to ClothoidSpline — review the result, it does not pass through the control points')
    }
    updateRoad(road.id, { functions })
  }

  function convertActiveSpline() {
    const road = selectedRoad
    if (!road?.functions) return
    if (!guardExplicitWays(road.id)) return
    const slices = trackSlices(road)
    if (!slices) return
    const index = activeFunctionIndexAt(road, selection.trackStation)
    const slice = slices[index]
    if (slice.fn.kind !== 'clothoidSpline') return
    const functions = [...road.functions]
    functions.splice(index, 1, ...convertSplineToFunctions(slice.start, slice.fn))
    updateRoad(road.id, { functions })
    toast.success('ClothoidSpline converted to clothoid + circle + segment')
  }

  function insertHandleAtStation() {
    const road = selectedRoad
    if (!road?.functions) return
    if (!guardExplicitWays(road.id)) return
    const slices = trackSlices(road)
    const station = selection.trackStation
    if (!slices || station === null) {
      toast.info('Click on the curve where the handle should be inserted first.')
      return
    }
    for (const slice of slices) {
      if (station > slice.offset + 0.2 && station < slice.offset + slice.length - 0.2) {
        const next = insertHandle(road, slice.index, station - slice.offset)
        if (!next) {
          toast.error('Handles can only be inserted on polylines and ClothoidSplines.')
          return
        }
        updateRoad(road.id, { functions: next })
        toast.success('Handle inserted')
        return
      }
    }
  }

  // ─── SubNetwork exits ──────────────────────────────────────────────
  function toggleExit(road: RoadData, contact: 'start' | 'end') {
    const exits = road.subNetworkExits ?? []
    const next = exits.includes(contact)
      ? exits.filter((c) => c !== contact)
      : [...exits, contact]
    updateRoad(road.id, { subNetworkExits: next })
    toast.success(next.includes(contact) ? `Extremity marked as SubNetwork Exit (${contact})` : 'SubNetwork Exit removed')
  }

  function linkExits(method: 'clothoidSpline' | 'segmentCircle') {
    if (!project || selection.trackIds.length !== 2) return
    const roads = selection.trackIds.map((id) => project.roads.find((r) => r.id === id)).filter(Boolean) as RoadData[]
    const exitFrames: { roadId: string; contact: 'start' | 'end'; frame: Frame }[] = []
    for (const road of roads) {
      for (const contact of road.subNetworkExits ?? []) {
        const frame = exitFrame(road, contact)
        if (frame) exitFrames.push({ roadId: road.id, contact, frame })
      }
    }
    if (exitFrames.length !== 2) {
      toast.error('Select exactly two tracks that have marked SubNetwork Exits.')
      return
    }
    const [from, to] = exitFrames
    const functions = buildExitConnector(
      { position: { x: from.frame.x, y: from.frame.y }, heading: from.frame.heading },
      { position: { x: to.frame.x, y: to.frame.y }, heading: to.frame.heading },
      method,
    )
    createExitLinkRoad(from.frame, functions)
    toast.success('Exits linked — the connector track is part of the current sub network')
  }

  function createExitLinkRoad(frame: Frame, functions: XYFunction[]) {
    if (!project) return
    const section = profileSection(insertOptions.defaultProfile, config.laneWidth, config.lanesLeft, config.lanesRight)
    addRoad({
      id: uuid(),
      name: `Exit Link ${project.roads.length + 1}`,
      points: startFramePoints(frame),
      functions,
      lanesLeft: section.left.length,
      lanesRight: section.right.length,
      laneWidth: config.laneWidth,
      filletRadius: config.filletRadius,
      laneSection: section,
    })
  }

  function exitFrame(road: RoadData, contact: 'start' | 'end'): Frame | null {
    if (road.functions && road.functions.length > 0) {
      const start = trackStartFrame(road)
      const slices = trackSlices(road)
      if (!start || !slices) return null
      if (contact === 'start') return start
      const last = slices[slices.length - 1]
      return functionEndFrame(last.start, last.fn)
    }
    const pts = road.points
    if (!pts || pts.length < 2) return null
    return contact === 'start'
      ? { x: pts[0].x, y: pts[0].y, heading: Math.atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) }
      : { x: pts[pts.length - 1].x, y: pts[pts.length - 1].y, heading: Math.atan2(pts[pts.length - 1].y - pts[pts.length - 2].y, pts[pts.length - 1].x - pts[pts.length - 2].x) }
  }

  // ─── Intersections ─────────────────────────────────────────────────
  function linkIntersectionToTracks() {
    const it = selectedIntersection
    if (!it || !project || selection.trackIds.length === 0) return
    const ends = [...it.trackEnds]
    let added = 0
    for (const trackId of selection.trackIds) {
      const road = project.roads.find((r) => r.id === trackId)
      if (!road) continue
      const start = exitFrame(road, 'start')
      const end = exitFrame(road, 'end')
      if (!start || !end) continue
      const contact = distance(start, it.position) <= distance(end, it.position) ? 'start' : 'end'
      if (ends.some((e) => e.trackId === trackId && e.contact === contact)) continue
      ends.push({ trackId, contact })
      added++
    }
    updateIntersection(it.id, { trackEnds: ends })
    if (ends.length < 3) {
      toast.warning('Intersection links at least 3 tracks together. Linking 2 tracks is not recommended — prefer merging them.')
    } else if (added > 0) {
      toast.success(`Linked ${added} track(s) to the intersection`)
    }
  }

  function unlinkIntersection() {
    const it = selectedIntersection
    if (!it) return
    updateIntersection(it.id, { trackEnds: [] })
    toast.success('Intersection unlinked from tracks')
  }

  function detectIntersectionFromSelection() {
    if (!project || selection.trackIds.length !== 2) return
    const [roadA, roadB] = selection.trackIds.map((id) => ensureFunctions(project.roads.find((r) => r.id === id)!))
    const resolved = resolveTracks([roadA, roadB])
    const a = resolved.get(roadA.id)
    const b = resolved.get(roadB.id)
    if (!a || !b) return
    const crossing = findTrackCrossing(a, b)
    if (!crossing) {
      toast.error('The two selected tracks do not cross.')
      return
    }
    const sinAngle = Math.max(0.2, crossing.angle)
    // Trim the arms back from the crossing (like the auto junction cut) so
    // the intersection interior has room for its ways, mesh and contours.
    const trimA = sectionHalfWidth(getLaneSection(roadB)) / sinAngle + 3
    const trimB = sectionHalfWidth(getLaneSection(roadA)) / sinAngle + 3

    function trimArm(road: RoadData, contact: 'start' | 'end', trim: number): RoadData | null {
      const len = fitTrackPath(road)?.length ?? 0
      if (len - trim < 2) return null // too short to keep — dropped as a stub
      if (contact === 'end') {
        const pieces = splitTrackFunctions(road, len - trim)
        if (!pieces) return road
        return { ...road, functions: pieces.functionsA }
      }
      const pieces = splitTrackFunctions(road, trim)
      if (!pieces) return road
      const frame = chainEndFrame(road, pieces.functionsA)
      if (!frame) return road
      return { ...road, functions: pieces.functionsB, points: startFramePoints(frame) }
    }

    const arms: { track: RoadData; contact: 'start' | 'end' }[] = []
    const roadsToReplace: { originalId: string; pieces: RoadData[] }[] = []
    for (const [road, s, trim] of [[roadA, crossing.sA, trimA], [roadB, crossing.sB, trimB]] as const) {
      const pieces = splitTrackFunctions(road, s)
      if (!pieces) {
        toast.error(`Cannot split ${road.name} at the crossing.`)
        return
      }
      const frame = chainEndFrame(road, pieces.functionsA)
      if (!frame) return
      const first: RoadData = { ...road, id: uuid(), name: `${road.name} A`, functions: pieces.functionsA }
      const second: RoadData = { ...road, id: uuid(), name: `${road.name} B`, points: startFramePoints(frame), functions: pieces.functionsB }
      const trimmed = [
        { track: trimArm(first, 'end', trim), contact: 'end' as const },
        { track: trimArm(second, 'start', trim), contact: 'start' as const },
      ].filter((arm): arm is { track: RoadData; contact: 'start' | 'end' } => arm.track !== null)
      // keep original names when a single arm survives (T intersection stubs removed)
      const renamed = trimmed.length === 2 ? trimmed : trimmed.map((arm) => ({ ...arm, track: { ...arm.track, name: road.name } }))
      arms.push(...renamed)
      roadsToReplace.push({ originalId: road.id, pieces: renamed.map((arm) => arm.track) })
    }
    if (arms.length < 3) {
      toast.error('Detect Intersection needs at least 3 remaining arms (two of them were too short).')
      return
    }
    const node = makeIntersectionData(`intersection-${(project.intersections ?? []).length + 1}-${uuid().slice(0, 4)}`, crossing.point)
    node.trackEnds = arms.map((arm) => ({ trackId: arm.track.id, contact: arm.contact }))
    for (const replacement of roadsToReplace) {
      replaceRoad(replacement.originalId, replacement.pieces)
    }
    addIntersection(node)
    // suppress auto junctions that duplicate this explicit intersection
    setTimeout(() => suppressDuplicateAutoJunctions(node.position, arms.map((arm) => arm.track)), 50)
    setSelection({ trackIds: [], intersectionId: node.id, trackStation: null })
    toast.success('Intersection detected: node + links + contours + ways created')
  }

  function suppressDuplicateAutoJunctions(nodePos: Vec2, pieces: RoadData[]) {
    const resolved = resolveTracks(pieces)
    const list = [...resolved.values()]
    for (let i = 0; i < list.length; i++) {
      for (let j = i + 1; j < list.length; j++) {
        const crossing = findTrackCrossing(list[i], list[j])
        if (crossing && distance(crossing.point, nodePos) < 25) {
          suppressJunction([list[i].track.id, list[j].track.id].sort().join('|'))
        }
      }
    }
  }

  function invertAuthorizations() {
    const it = selectedIntersection
    if (!it || !project) return
    const ways = allWays(it, resolveTracks(project.roads))
    const authorizations = { ...it.authorizations }
    for (const way of ways) {
      authorizations[way.key] = !(authorizations[way.key] ?? true)
    }
    updateIntersection(it.id, { authorizations })
    toast.success('Authorisations inverted on intersection')
  }

  function toggleAuthorization(key: string) {
    const it = selectedIntersection
    if (!it) return
    updateIntersection(it.id, {
      authorizations: { ...it.authorizations, [key]: !(it.authorizations[key] ?? true) },
    })
  }

  function extractWays() {
    const it = selectedIntersection
    if (!it || !project) return
    const resolved = resolveTracks(project.roads)
    const result = extractWaysFromIntersection(it, resolved, project.roads.length + 1)
    if (!result) {
      toast.error('No authorized ways to extract from this intersection.')
      return
    }
    const section = profileSection(insertOptions.defaultProfile, config.laneWidth, config.lanesLeft, config.lanesRight)
    for (const track of result.wayTracks) {
      const start = track.functions[0].kind === 'polyline' ? track.functions[0].points[0] : { x: 0, y: 0 }
      const nextPt = track.functions[0].kind === 'polyline' ? track.functions[0].points[1] : start
      addRoad({
        id: track.id,
        name: track.name,
        points: startFramePoints({ x: start.x, y: start.y, heading: Math.atan2(nextPt.y - start.y, nextPt.x - start.x) }),
        functions: track.functions,
        lanesLeft: section.left.length,
        lanesRight: section.right.length,
        laneWidth: config.laneWidth,
        filletRadius: config.filletRadius,
        laneSection: section,
      })
    }
    for (const node of result.extremityIntersections) {
      addIntersection(node)
    }
    deleteIntersectionById(it.id)
    toast.success(`Extracted ${result.wayTracks.length} way track(s) with extremity intersections`)
  }

  function createInterchange() {
    if (!project || selection.trackIds.length !== 2) return
    const main = ensureFunctions(project.roads.find((r) => r.id === selection.trackIds[0])!)
    const secondary = ensureFunctions(project.roads.find((r) => r.id === selection.trackIds[1])!)
    const resolved = resolveTracks([main, secondary])
    const mainRes = resolved.get(main.id)
    const secRes = resolved.get(secondary.id)
    if (!mainRes || !secRes) return
    const plan = planInterchange(mainRes, secRes)
    if (!plan) {
      toast.error('Create Interchange needs two overlapping tracks (entry/exit shape).')
      return
    }
    const [mainS1, mainS2] = [...plan.mainSplits].sort((a, b) => a - b)
    const [secS1, secS2] = [...plan.secondarySplits].sort((a, b) => a - b)
    // split main into 3, secondary into 3 (split far station first)
    const mainPieces = splitAtStations(main, [mainS1, mainS2])
    const secPieces = splitAtStations(secondary, [secS1, secS2])
    if (!mainPieces || !secPieces) {
      toast.error('Interchange splits failed.')
      return
    }
    const [M1, M2, M3] = mainPieces
    const [S1, , S3] = secPieces
    // replace roads; the secondary middle piece becomes the added passageway
    replaceRoad(main.id, [M1, M2, M3])
    replaceRoad(secondary.id, [S1, S3])
    const node = makeIntersectionData(`intersection-${(project.intersections ?? []).length + 1}-${uuid().slice(0, 4)}`, plan.node)
    node.trackEnds = [
      { trackId: M1.id, contact: 'end' },
      { trackId: M3.id, contact: 'start' },
      { trackId: S1.id, contact: 'end' },
      { trackId: S3.id, contact: 'start' },
    ]
    node.authorizations = {
      [authorizationKey({ trackId: S1.id, contact: 'end' }, { trackId: S3.id, contact: 'start' })]: false,
    }
    addIntersection(node)
    setSelection({ trackIds: [], intersectionId: node.id, trackStation: null })
    toast.success('Bifurcation created: tracks split, central intersection added, secondary through denied')
  }

  function splitAtStations(road: RoadData, stations: number[]): RoadData[] | null {
    let remaining: RoadData = road
    const piecesAfterFirst: RoadData[] = []
    let offsetName = 0
    const sorted = [...stations].sort((a, b) => a - b)
    let firstCut: { a: RoadData; b: RoadData } | null = null
    for (const s of sorted) {
      const pieces = splitTrackFunctions(remaining, s - (firstCut ? sorted[0] : 0))
      if (!pieces) return null
      const frame = chainEndFrame(remaining, pieces.functionsA)
      if (!frame) return null
      const a: RoadData = { ...remaining, id: uuid(), name: `${road.name}.${String.fromCharCode(65 + offsetName)}`, functions: pieces.functionsA }
      const b: RoadData = { ...remaining, id: uuid(), name: `${road.name}.${String.fromCharCode(66 + offsetName)}`, points: startFramePoints(frame), functions: pieces.functionsB }
      if (!firstCut) {
        firstCut = { a, b }
        remaining = b
      } else {
        piecesAfterFirst.push(a)
        remaining = b
      }
      offsetName++
    }
    if (!firstCut) return null
    return [firstCut.a, ...piecesAfterFirst, remaining]
  }

  function moveMarker(id: string, point: Vec2) {
    if (id.startsWith('node:')) {
      updateIntersection(id.slice(5), { position: point })
    } else if (id.startsWith('contour:')) {
      const [, intersectionId, indexStr] = id.split(':')
      const it = (project?.intersections ?? []).find((item) => item.id === intersectionId)
      if (!it) return
      const handles = [...it.contourHandles]
      handles[Number.parseInt(indexStr, 10)] = point
      updateIntersection(intersectionId, { contourHandles: handles })
    }
  }

  function moveMarkerEnd(id: string, point: Vec2) {
    moveMarker(id, point)
    if (id.startsWith('node:')) toast.info('Intersection aligned — keep the handle inside the contours.')
  }

  function handleWayDoubleClick(wayKey: string) {
    const [fromKey] = wayKey.split('->')
    const locks = lockedPassageways.includes(fromKey)
      ? lockedPassageways.filter((k) => k !== fromKey)
      : [...lockedPassageways, fromKey].slice(-2)
    setLockedPassageways(locks)
    toast.info(locks.length === 0
      ? 'Passageway lock removed'
      : locks.length === 1
        ? 'Passageway locked — showing only its ways'
        : 'Two passageways locked — showing the way between them')
  }

  // ─── Context menu ──────────────────────────────────────────────────
  function buildContextMenu(world: Vec2): { title: string; items: ContextMenuItem[] } {
    const items: ContextMenuItem[] = []
    const selTracks = selection.trackIds
      .map((id) => project?.roads.find((r) => r.id === id))
      .filter(Boolean) as RoadData[]
    const hit = nearestRoad(world)
    const nodeHit = (project?.intersections ?? []).find((n) => distance(n.position, world) < 10) ?? null
    const activeIt = selectedIntersection ?? nodeHit

    if (activeIt) {
      items.push({ label: 'Link to Tracks', shortcut: 'Ctrl+L', disabled: selTracks.length === 0, onSelect: linkIntersectionToTracks })
      items.push({ label: 'Unlink Intersection From Tracks', disabled: activeIt.trackEnds.length === 0, onSelect: () => { setSelection({ trackIds: selection.trackIds, intersectionId: activeIt.id, trackStation: null }); setTimeout(unlinkIntersection, 0) } })
      items.push({ label: 'Invert Authorisations on Intersection', shortcut: 'Ctrl+Shift+I', disabled: activeIt.trackEnds.length < 2, onSelect: () => { setSelection({ trackIds: selection.trackIds, intersectionId: activeIt.id, trackStation: null }); setTimeout(invertAuthorizations, 0) } })
      items.push({ label: 'Insert Handle (Contour)', onSelect: () => { setSelection({ trackIds: selection.trackIds, intersectionId: activeIt.id, trackStation: null }); setContourHandleArmed(true); toast.info('Click near a contour to place the handle.') } })
      items.push({ label: 'Extract Ways From Intersection', disabled: activeIt.trackEnds.length < 2, onSelect: () => { setSelection({ trackIds: selection.trackIds, intersectionId: activeIt.id, trackStation: null }); setTimeout(extractWays, 0) }, separatorBefore: true })
      items.push({ label: 'Delete Intersection', danger: true, onSelect: () => { deleteIntersectionById(activeIt.id); setSelection({ trackIds: selection.trackIds, intersectionId: null, trackStation: null }) }, separatorBefore: true })
      return { title: 'Intersection', items }
    }

    if (selTracks.length === 2) {
      items.push({ label: 'Link Tracks', onSelect: linkSelectedTracks })
      items.push({ label: 'Bind Tracks', onSelect: bindSelectedTracks })
      items.push({ label: 'Detect Intersection', onSelect: detectIntersectionFromSelection })
      items.push({ label: 'Create Interchange (Bifurcation)', onSelect: createInterchange })
      items.push({ label: 'Link Exits with ClothoidSpline', disabled: !selTracks.every((r) => (r.subNetworkExits ?? []).length > 0), onSelect: () => linkExits('clothoidSpline') })
      items.push({ label: 'Link Exits with Segment + Circle Arc', disabled: !selTracks.every((r) => (r.subNetworkExits ?? []).length > 0), onSelect: () => linkExits('segmentCircle') })
      return { title: `${selTracks[0].name} + ${selTracks[1].name}`, items }
    }

    if (hit) {
      const road = hit.road
      const isFn = !!road.functions && road.functions.length > 0
      items.push({ label: `Select ${road.name}`, onSelect: () => selectRoadOnly(road.id, hit.s) })
      if (isFn) {
        items.push({ label: 'Split Function', onSelect: () => splitFunctionAt(road, hit.s) })
        items.push({ label: 'Merge Functions', onSelect: () => mergeAtStation(road, hit.s) })
        items.push({ label: 'Insert Handle', onSelect: () => { selectRoadOnly(road.id, hit.s); setTimeout(insertHandleAtStation, 0) } })
      }
      items.push({ label: 'Split Track', onSelect: () => splitRoadAt(road, hit.s), separatorBefore: true })
      items.push({ label: 'Invert Track Orientation', onSelect: () => { selectRoadOnly(road.id); setTimeout(invertSelectedOrientation, 0) } })
      items.push({ label: 'Stick Track to Background Terrain', onSelect: () => { selectRoadOnly(road.id); setTimeout(stickSelectedToTerrain, 0) } })
      if (!isFn && (road.geometryType ?? 'polyline') === 'polyline') {
        items.push({ label: 'Smooth Track (Polyline)', onSelect: () => { selectRoadOnly(road.id); setTimeout(smoothSelectedPolyline, 0) } })
      }

      // exits
      const nearStart = distance(hit.road.points[0] ?? { x: 1e9, y: 1e9 }, world) < 12
      const pts = road.points
      const nearEnd = pts.length > 0 && distance(pts[pts.length - 1], world) < 12
      if (nearStart || nearEnd) {
        const contact = nearStart ? 'start' : 'end'
        items.push({
          label: (road.subNetworkExits ?? []).includes(contact) ? 'Unmark SubNetwork Exit' : 'Mark as SubNetwork Exit',
          onSelect: () => toggleExit(road, contact),
        })
      }

      if (isFn && (road.functions as XYFunction[])[activeFunctionIndexAt(road, hit.s)]?.kind === 'polyline') {
        items.push({ label: 'Convert to Bezier', onSelect: () => { selectRoadOnly(road.id, hit.s); setTimeout(() => convertActivePolyline('bezier'), 0) }, separatorBefore: true })
        items.push({ label: 'Convert to Clothoid Arc', onSelect: () => { selectRoadOnly(road.id, hit.s); setTimeout(() => convertActivePolyline('clothoidArc'), 0) } })
        items.push({ label: 'Convert to Clothoid Spline', onSelect: () => { selectRoadOnly(road.id, hit.s); setTimeout(() => convertActivePolyline('clothoidSpline'), 0) } })
      }
      if (isFn && (road.functions as XYFunction[])[activeFunctionIndexAt(road, hit.s)]?.kind === 'clothoidSpline') {
        items.push({ label: 'Convert ClothoidSpline in Clothoid + Circle + Segment', onSelect: () => { selectRoadOnly(road.id, hit.s); setTimeout(convertActiveSpline, 0) }, separatorBefore: true })
      }

      items.push({ label: 'Delete Track', danger: true, onSelect: () => deleteRoad(road.id), separatorBefore: true })
      return { title: road.name, items }
    }

    if (selTracks.length === 1) {
      items.push({ label: 'Invert Track Orientation', onSelect: invertSelectedOrientation })
      items.push({ label: 'Stick Track to Background Terrain', onSelect: stickSelectedToTerrain })
      items.push({ label: 'Mark Start as SubNetwork Exit', onSelect: () => toggleExit(selTracks[0], 'start') })
      items.push({ label: 'Mark End as SubNetwork Exit', onSelect: () => toggleExit(selTracks[0], 'end') })
      items.push({ label: 'Split Track at Mouse', onSelect: () => splitRoadAt(selTracks[0], selection.trackStation ?? (roadLengths.get(selTracks[0].id) ?? 0) / 2) })
      return { title: selTracks[0].name, items }
    }

    items.push({ label: tool === 'insert-intersection' ? 'Left-click to place the intersection node' : 'Nothing under the cursor', disabled: true, onSelect: () => {} })
    return { title: 'Roads', items }
  }

  // ─── Keyboard shortcuts ────────────────────────────────────────────
  useKeyboardShortcuts({
    tool,
    selection,
    dragSnapRef,
    onFinishPointDraft: finishPointDraft,
    onEscape: () => {
      setDraftPoints([])
      setHoverPoint(null)
      dragSnapRef.current = null
      setContourHandleArmed(false)
      setLaneGizmo(null)
    },
    onDeleteSelection: () => {
      if (selection.intersectionId) {
        deleteIntersectionById(selection.intersectionId)
        setSelection({ ...selection, intersectionId: null })
      } else if (selection.trackIds.length > 0) {
        for (const id of selection.trackIds) deleteRoad(id)
        setSelection({ trackIds: [], intersectionId: null, trackStation: null })
      }
    },
    onLinkIntersectionToTracks: linkIntersectionToTracks,
    onInvertAuthorizations: invertAuthorizations,
    onUndo: undo,
    onRedo: redo,
  })

  function finishPointDraft() {
    if (draftPoints.length >= 2) {
      const snap = dragSnapRef.current
      const heading = Math.atan2(draftPoints[1].y - draftPoints[0].y, draftPoints[1].x - draftPoints[0].x)
      const start: Frame = snap?.frame ?? { x: draftPoints[0].x, y: draftPoints[0].y, heading }
      if (tool === 'draw-polyline') {
        dragSnapRef.current = null
        commitFunction({ kind: 'polyline', points: draftPoints, splineType: 'segment' }, start, snap)
      } else if (tool === 'draw-spline') {
        dragSnapRef.current = null
        commitFunction({ kind: 'clothoidSpline', points: draftPoints, tolerance: 0.5, symmetryThreshold: 1 }, start, snap)
      }
    }
    setDraftPoints([])
    setHoverPoint(null)
    dragSnapRef.current = null
  }

  // Insert a new lane next to the one closest to the click point.
  function insertLaneNearPoint(road: RoadData, point: Vec2) {
    const section = getLaneSection(road)
    const proj = projectOntoRoad(road, point)
    if (!proj) return
    const layout = laneLayout(section)
    const { t } = proj
    const side: 'left' | 'right' = t >= 0 ? 'left' : 'right'
    const lanes = section[side]
    let index = lanes.length
    for (let i = 0; i < layout[side].length; i++) {
      const a = layout[side][i].inner
      const b = layout[side][i].outer
      const lo = Math.min(a, b)
      const hi = Math.max(a, b)
      if (t >= lo && t <= hi) { index = i + 1; break }
    }
    insertLaneAt(road.id, side, index, defaultLaneByType('travel'))
    setSelectedLaneKey(`${side}:${index}`)
    selectRoadOnly(road.id)
    toast.success(`Inserted lane at index ${index} on ${side}`)
  }

  // Remove the lane closest to the click point.
  function removeLaneNearPoint(road: RoadData, point: Vec2) {
    const section = getLaneSection(road)
    const proj = projectOntoRoad(road, point)
    if (!proj) return
    const layout = laneLayout(section)
    const { t } = proj
    const side: 'left' | 'right' = t >= 0 ? 'left' : 'right'
    const lanes = section[side]
    for (let i = 0; i < layout[side].length; i++) {
      const a = layout[side][i].inner
      const b = layout[side][i].outer
      const lo = Math.min(a, b)
      const hi = Math.max(a, b)
      if (t >= lo && t <= hi) {
        if (lanes.length <= 1 && (side === 'left' ? section.right.length === 0 : section.left.length === 0)) {
          toast.error('Cannot remove the last lane')
          return
        }
        removeLaneAt(road.id, side, i)
        setSelectedLaneKey(null)
        toast.success(`Removed lane #${i} on ${side}`)
        return
      }
    }
  }

  function addSidewalkCurb(road: RoadData) {
    const section = getLaneSection(road)
    insertLaneAt(road.id, 'right', section.right.length, defaultLaneByType('curb'))
    insertLaneAt(road.id, 'right', section.right.length + 1, defaultLaneByType('sidewalk'))
    insertLaneAt(road.id, 'left', section.left.length, defaultLaneByType('curb'))
    insertLaneAt(road.id, 'left', section.left.length + 1, defaultLaneByType('sidewalk'))
    toast.success('Sidewalks and curbs added on both sides')
  }

  /** The two gizmo arrow positions (doc: arrows toward track start/end). */
  function gizmoArrows(gizmo: NonNullable<typeof laneGizmo>): { key: 'before' | 'after'; point: Vec2; heading: number }[] {
    const nx = -Math.sin(gizmo.tangent)
    const ny = Math.cos(gizmo.tangent)
    const sideSign = gizmo.side === 'left' ? 1 : -1
    const tx = Math.cos(gizmo.tangent)
    const ty = Math.sin(gizmo.tangent)
    const lateral = (gizmo.halfWidth + 4) * sideSign
    return [
      {
        key: 'after',
        point: { x: gizmo.point.x + tx * 8 + nx * lateral, y: gizmo.point.y + ty * 8 + ny * lateral },
        heading: gizmo.tangent,
      },
      {
        key: 'before',
        point: { x: gizmo.point.x - tx * 8 + nx * lateral, y: gizmo.point.y - ty * 8 + ny * lateral },
        heading: gizmo.tangent + Math.PI,
      },
    ]
  }

  function commitGizmoLane(gizmo: NonNullable<typeof laneGizmo>, direction: 'before' | 'after') {
    const road = project?.roads.find((r) => r.id === gizmo.roadId)
    if (!road) return
    const section = getLaneSection(road)
    // 'after' → lane exists from s to the end (grows); 'before' → lane exists
    // from the start to s (shrinks). Both taper over SPEED_LIMIT × 2 s.
    const mode: 'in' | 'out' = direction === 'after' ? 'in' : 'out'
    const index = gizmo.kind === 'begin' ? 0 : section[gizmo.side].length
    const lane = defaultLaneByType('travel')
    insertLaneAt(road.id, gizmo.side, index, lane)
    const tapers = [...(road.tapers ?? [])]
    if (index === 0) {
      for (const t of tapers) {
        if (t.side === gizmo.side) t.index += 1
      }
    }
    tapers.push({
      side: gizmo.side,
      index,
      mode,
      length: (lane.speedLimit / 3.6) * 2,
      startS: mode === 'in' ? gizmo.s : undefined,
      endS: mode === 'out' ? gizmo.s : undefined,
    })
    updateRoad(road.id, { tapers })
    setSelectedLaneKey(`${gizmo.side}:${index}`)
    selectRoadOnly(road.id)
    const length = (lane.speedLimit / 3.6) * 2
    toast.success(`${gizmo.kind === 'begin' ? 'Begin' : 'End'} lane on ${gizmo.side} at ${gizmo.s.toFixed(0)} m — express over ${length.toFixed(1)} m (${lane.speedLimit} km/h × 2 s)`)
  }

  function projectOntoRoad(road: RoadData, point: Vec2) {
    if (!junctionNetwork) return null
    const path = junctionNetwork.paths.get(road.id)
    if (!path) return null
    const nearest = nearestPointOnPath(path, point)
    const sample = evaluatePath(path, nearest.s)
    const dx = point.x - nearest.point.x
    const dy = point.y - nearest.point.y
    // signed lateral offset: positive = left of the axis
    const t = -Math.sin(sample.heading) * dx + Math.cos(sample.heading) * dy
    return { s: nearest.s, t }
  }

  // ─── Rail fixtures (Train section turnout pipeline) ────────────────
  function railwayTracks(): RoadData[] {
    return (project?.roads ?? []).filter((road) => road.railway)
  }

  /**
   * Create a turnout from three SELECTED railway tracks (robust path —
   * freehand drawing cannot reliably produce three extremities at one
   * point because the endpoint snap absorbs diverging draws). Facing,
   * trailing and branch are determined geometrically: the facing track is
   * the one whose extremity lies closest to the other two tracks' nearest
   * extremities, and trailing is the best-aligned continuation.
   */
  function createRailPointFromSelection() {
    if (!project) return
    const tracks = railwayTracks().filter((road) => selection.trackIds.includes(road.id))
    if (tracks.length !== 3) {
      toast.error('Select exactly 3 railway tracks (facing + main + branch) to insert a turnout.')
      return
    }
    const ext = (road: RoadData, contact: 'start' | 'end') => exitFrame(road, contact)
    let best: { facing: number; facingContact: 'start' | 'end'; trailing: number; branch: number; spread: number } | null = null
    for (const f of [0, 1, 2]) {
      for (const fContact of ['start', 'end'] as const) {
        const fFrame = ext(tracks[f], fContact)
        if (!fFrame) continue
        const others = [0, 1, 2].filter((i) => i !== f)
        const aContact = (['start', 'end'] as const).map((c) => ({ c, frame: ext(tracks[others[0]], c) })).filter((x) => x.frame)
        const bContact = (['start', 'end'] as const).map((c) => ({ c, frame: ext(tracks[others[1]], c) })).filter((x) => x.frame)
        let bestSpread = Infinity
        let bestA: 'start' | 'end' = 'start'
        let bestB: 'start' | 'end' = 'start'
        for (const a of aContact) {
          for (const b of bContact) {
            const dA = distance(a.frame!, fFrame)
            const dB = distance(b.frame!, fFrame)
            const between = distance(a.frame!, b.frame!)
            const spread = Math.max(dA, dB, between)
            if (dA <= 15 && dB <= 15 && between <= 15 && spread < bestSpread) {
              bestSpread = spread
              bestA = a.c
              bestB = b.c
            }
          }
        }
        if (bestSpread < Infinity && (!best || bestSpread < best.spread)) {
          best = { facing: f, facingContact: fContact, trailing: others[0], branch: others[1], spread: bestSpread }
          void bestA
          void bestB
        }
      }
    }
    if (!best) {
      toast.error('The 3 selected tracks do not share a common extremity (within 15 m).')
      return
    }
    // trailing (main line) = the non-facing track best aligned with the facing direction
    const travel = best.facingContact === 'end' ? ext(tracks[best.facing], best.facingContact)!.heading : ext(tracks[best.facing], best.facingContact)!.heading + Math.PI
    const other = best.facing === best.trailing ? best.branch : best.trailing
    const outA = ext(tracks[best.trailing], 'start')
    const outB = ext(tracks[best.branch], 'start')
    const align = (frame: Frame | null, contact: 'start' | 'end') => {
      if (!frame) return Infinity
      let d = (contact === 'start' ? frame.heading : frame.heading + Math.PI) - travel
      while (d > Math.PI / 2) d -= Math.PI
      while (d < -Math.PI / 2) d += Math.PI
      return Math.abs(d)
    }
    const [trailingIdx, branchIdx] =
      align(outA, 'start') <= align(outB, 'start')
        ? [best.trailing, best.branch]
        : [best.branch, best.trailing]
    const railPoint: RailPoint = {
      id: uuid(),
      name: `P${(project.railPoints?.length ?? 0) + 1}`,
      facingTrackId: tracks[best.facing].id,
      facingContact: best.facingContact,
      trailingTrackId: tracks[trailingIdx].id,
      branchTrackId: tracks[branchIdx].id,
    }
    addRailPoint(railPoint)
    toast.success(`Turnout ${railPoint.name} created`, {
      description: `${tracks[best.facing].name} → ${tracks[trailingIdx].name} (main) + ${tracks[branchIdx].name} (branch)`,
    })
  }

  function createRailPointAt(point: Vec2) {
    if (!project) return
    const tracks = railwayTracks()
    // facing track: an extremity within 6 m of the click
    let facing: { road: RoadData; contact: 'start' | 'end'; frame: Frame } | null = null
    outer: for (const road of tracks) {
      for (const contact of ['start', 'end'] as const) {
        const frame = exitFrame(road, contact)
        if (frame && distance(frame, point) < 6) {
          facing = { road, contact, frame }
          break outer
        }
      }
    }
    if (!facing) {
      toast.error('Insert Point: click the extremity of the facing track (within 6 m).')
      return
    }
    // connected tracks: extremities within 8 m of the facing tip
    const candidates: { road: RoadData; contact: 'start' | 'end'; frame: Frame }[] = []
    for (const road of tracks) {
      if (road.id === facing.road.id) continue
      for (const contact of ['start', 'end'] as const) {
        const frame = exitFrame(road, contact)
        if (frame && distance(frame, facing.frame) < 15) candidates.push({ road, contact, frame })
      }
    }
    if (candidates.length < 2) {
      toast.error('A turnout needs the facing track end plus two tracks starting there.')
      return
    }
    // trailing (main line) = candidate whose outbound heading continues the
    // facing travel direction best; the next one is the diverging branch
    const travel = facing.contact === 'end' ? facing.frame.heading : facing.frame.heading + Math.PI
    const outbound = (candidate: { contact: 'start' | 'end'; frame: Frame }) =>
      candidate.contact === 'start' ? candidate.frame.heading : candidate.frame.heading + Math.PI
    const alignment = (candidate: { contact: 'start' | 'end'; frame: Frame }) => {
      let d = outbound(candidate) - travel
      while (d > Math.PI / 2) d -= Math.PI
      while (d < -Math.PI / 2) d += Math.PI
      return Math.abs(d)
    }
    const sorted = [...candidates].sort((a, b) => alignment(a) - alignment(b))
    const [trailing, branch] = sorted
    const railPoint: RailPoint = {
      id: uuid(),
      name: `P${(project.railPoints?.length ?? 0) + 1}`,
      facingTrackId: facing.road.id,
      facingContact: facing.contact,
      trailingTrackId: trailing.road.id,
      branchTrackId: branch.road.id,
    }
    addRailPoint(railPoint)
    toast.success(`Turnout ${railPoint.name} created`, {
      description: `${facing.road.name} → ${trailing.road.name} (main) + ${branch.road.name} (branch)`,
    })
  }

  function createRailCrossingAt(point: Vec2) {
    if (!project || !junctionNetwork) return
    const measured = railwayTracks().flatMap((road) => {
      const path = junctionNetwork!.paths.get(road.id)
      if (!path) return []
      const nearest = nearestPointOnPath(path, point)
      return [{ road, s: nearest.s, d: nearest.distance }]
    }).sort((a, b) => a.d - b.d)
    if (measured.length < 2 || measured[1].d > 5) {
      toast.error('Insert Frog / Diamond: click between two crossing tracks.')
      return
    }
    const [a, b] = measured
    const resolved = resolveTracks([a.road, b.road])
    const pathA = resolved.get(a.road.id)
    const pathB = resolved.get(b.road.id)
    if (!pathA || !pathB) return
    const crossing = findTrackCrossing(pathA, pathB)
    if (!crossing) {
      toast.error('Those two tracks do not cross.')
      return
    }
    const angle = Math.min(Math.abs(crossing.angle), Math.PI - Math.abs(crossing.angle))
    const item: RailCrossing = {
      id: uuid(),
      trackAId: a.road.id,
      trackBId: b.road.id,
      sA: crossing.sA,
      sB: crossing.sB,
      position: crossing.point,
      angle,
      kind: angle < 0.5 ? 'diamond' : 'frog',
    }
    addRailCrossing(item)
    toast.success(`${item.kind === 'diamond' ? 'Diamond' : 'Frog'} created`, {
      description: `crossing at ${angle.toFixed(2)} rad — wing rails + guard rails generated`,
    })
  }

  function createCatchPointAt(point: Vec2) {
    let best: { road: RoadData; contact: 'start' | 'end'; frame: Frame; d: number } | null = null
    for (const road of railwayTracks()) {
      for (const contact of ['start', 'end'] as const) {
        const frame = exitFrame(road, contact)
        if (!frame) continue
        const d = distance(frame, point)
        if (d < 6 && (!best || d < best.d)) best = { road, contact, frame, d }
      }
    }
    if (!best) {
      toast.error('Catch Point: click a track extremity (within 6 m).')
      return
    }
    // side from where you clicked relative to the outbound travel direction
    const travel = best.contact === 'start' ? best.frame.heading : best.frame.heading + Math.PI
    const cross = Math.cos(travel) * (point.y - best.frame.y) - Math.sin(travel) * (point.x - best.frame.x)
    const side: 'left' | 'right' = cross >= 0 ? 'left' : 'right'
    addCatchPoint({ id: uuid(), trackId: best.road.id, contact: best.contact, side })
    toast.success(`Catch point added on ${best.road.name}`, { description: `${best.contact} end, blade on the ${side} side` })
  }

  function handleExportXodr() {
    if (!project) return
    const xml = exportOpenDrive(project)
    const blob = new Blob([xml], { type: 'application/xml' })
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `${project.name || 'network'}.xodr`
    anchor.click()
    URL.revokeObjectURL(url)
    toast.success('OpenDRIVE exported', { description: `${project.roads.length} roads as line / arc / spiral geometry` })
  }

  /** Doc 5.5.2.1.13 track smoothing: Chaikin-smooth a legacy polyline track. */
  function smoothSelectedPolyline() {
    const road = selectedRoad
    if (!road) return
    if (road.functions && road.functions.length > 0) {
      toast.error('Smoothing applies to polyline roads (convert to polyline first).')
      return
    }
    if (!road.points || road.points.length < 3) {
      toast.error('Not enough control points to smooth.')
      return
    }
    const smoothed = smoothPolylinePoints(road.points, 2)
    updateRoad(road.id, { points: smoothed, geometryType: 'polyline' })
    toast.success('Track smoothed', { description: `${road.points.length} -> ${smoothed.length} control points` })
  }

  function handleExportNetwork() {
    if (!project) return
    const xml = exportNetworkDefinition(project)
    const blob = new Blob([xml], { type: 'application/xml' })
    const url = URL.createObjectURL(blob)
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `${project.name || 'network'}-track-network.xml`
    anchor.click()
    URL.revokeObjectURL(url)
    toast.success('Network definition exported', {
      description: 'Segments, connections, points, crossings and catch points',
    })
  }

  function renameRoad(name: string) {
    if (selectedRoad) updateRoad(selectedRoad.id, { name })
  }

  function onFunctionsChange(functions: XYFunction[]) {
    if (selectedRoad) updateRoad(selectedRoad.id, { functions })
  }

  const selectionPanelWays = useMemo(() => {
    if (!selectedIntersection || !project) return []
    return allWays(selectedIntersection, resolveTracks(project.roads))
  }, [selectedIntersection, project])

  if (!project) return null

  const dragTools: Tool[] = ['draw-straight', 'draw-clothoid', 'extend', 'move']
  const interaction = dragTools.includes(tool) ? 'drag' : 'click'
  const highlightIds = new Set(selection.trackIds)
  const highlightMeshes = roadMeshEntries.filter((entry) => highlightIds.has(entry.roadId)).map((entry) => entry.mesh)
  const insertToolActive = tool.startsWith('draw-')
  const menuContent = menu ? buildContextMenu(menu.world) : null

  return (
    <div className="flex h-screen min-h-0 flex-col bg-background">
      <AppHeader
        projectName={project.name}
        subtitle={`${project.roads.length} roads · ${activeJunctions.length} junctions · ${(project.intersections ?? []).length} intersections`}
        onBack={onBack}
      >
        <EditorHeaderActions
          mode={mode}
          onModeChange={setMode}
          showMap={showMap}
          onToggleMap={() => setShowMap((v) => !v)}
          importInputRef={odrInputRef}
          onImportClick={() => odrInputRef.current?.click()}
          onImportFile={handleOdrImport}
          onExportXodr={handleExportXodr}
        />
      </AppHeader>

      {/* Section switch: Road workspace vs Train (railway) workspace */}
      <div className="flex h-9 shrink-0 items-center gap-2 border-b border-border bg-card/50 px-3">
        <div className="flex items-center gap-0.5">
          <Button size="sm" variant={section === 'road' ? 'default' : 'ghost'} className="h-7 gap-1.5 px-3 text-xs" onClick={() => switchSection('road')}>
            <Route className="size-3.5" />
            Road
          </Button>
          <Button size="sm" variant={section === 'train' ? 'default' : 'ghost'} className="h-7 gap-1.5 px-3 text-xs" onClick={() => switchSection('train')}>
            <TrainFront className="size-3.5" />
            Train
          </Button>
        </div>
        <span className="text-[11px] text-muted-foreground">
          {section === 'train'
            ? 'Railway track — Straight / Circle Arc / Clothoid (Spiral) with rails, sleepers and ballast'
            : roadSpace === 'lane'
              ? 'Lane editing — select a road, then use the lane tools on its lanes'
              : 'Road design — curves, profiles, intersections'}
        </span>
        {section === 'train' && (
          <Button size="sm" variant="outline" className="h-7 gap-1.5 px-2.5 text-xs" onClick={handleExportNetwork}>
            <FileDown className="size-3.5" />
            Export Network XML
          </Button>
        )}
        <div className="ml-auto flex items-center gap-2">
          <Button size="sm" variant="ghost" className="h-7 px-2 text-xs" disabled={!canUndo} onClick={undo} title="Undo (Ctrl+Z)">
            <Undo2 className="size-3.5" />
          </Button>
          <Button size="sm" variant="ghost" className="h-7 px-2 text-xs" disabled={!canRedo} onClick={redo} title="Redo (Ctrl+Y)">
            <Redo2 className="size-3.5" />
          </Button>
          {section === 'road' && (
            <div className="flex items-center gap-0.5">
              <Button size="sm" variant={roadSpace === 'road' ? 'default' : 'ghost'} className="h-7 px-3 text-xs" onClick={() => switchRoadSpace('road')}>
                Road Tools
              </Button>
              <Button size="sm" variant={roadSpace === 'lane' ? 'default' : 'ghost'} className="h-7 px-3 text-xs" onClick={() => switchRoadSpace('lane')}>
                Lane Tools
              </Button>
            </div>
          )}
        </div>
      </div>

      <div className="flex min-h-0 flex-1">
        <ToolRail tool={tool} space={toolSpace} onChooseTool={chooseTool} />

        {/* Canvas */}
        <div className="relative flex min-w-0 flex-1 flex-col">
          <RoadViewport
            meshes={flattenPreviewMeshes(previewMeshes)}
            highlightMeshes={highlightMeshes}
            draftMesh={draftMesh}
            draftPoints={draftPoints}
            interaction={interaction}
            onDragStart={handleDragStart}
            onDragMove={handleDragMove}
            onDragEnd={handleDragEnd}
            onDragCancel={handleDragCancel}
            onGroundClick={handleGroundClick}
            onGroundHover={handleGroundHover}
            onContextMenu={(world, screen) => setMenu({ world, screen })}
            onMarkerDrag={moveMarker}
            onMarkerDragEnd={moveMarkerEnd}
            onWayDoubleClick={handleWayDoubleClick}
            overlays={overlays}
            mode={mode}
            hint={toolHint(tool)}
            showMap={showMap && mode === '2d'}
            mapCenter={{ lng: project.geoRef?.lng ?? -95.36, lat: project.geoRef?.lat ?? 29.76 }}
            mapScale={project.geoRef?.scale ?? 1}
          />

          {draftPoints.length > 0 && ['draw-polyline', 'draw-spline'].includes(tool) && (
            <DraftPointsToolbar
              count={draftPoints.length}
              onFinish={finishPointDraft}
              onCancel={() => { setDraftPoints([]); setHoverPoint(null); dragSnapRef.current = null }}
            />
          )}

          {/* TOOL: Insert <function> panel (Stick to Background Terrain + Default Profile) */}
          {section === 'road' && insertToolActive && (
            <ToolOptionsPanel
              tool={tool}
              insertOptions={insertOptions}
              config={config}
              draftLength={draftPath?.length ?? null}
              onInsertOptionsChange={setInsertOptions}
              onConfigChange={setConfig}
            />
          )}

          {contourHandleArmed && (
            <div className="absolute top-3 left-1/2 z-10 -translate-x-1/2 rounded-lg border border-orange-500/50 bg-orange-500/10 px-3 py-1.5 text-xs text-orange-300 shadow-lg backdrop-blur">
              Click near a contour to insert the handle · Esc to cancel
            </div>
          )}
        </div>

        {/* Sidebar */}
        <aside className="flex w-72 shrink-0 flex-col border-l border-border bg-card/60">
          {section === 'road' && roadSpace === 'road' ? (
          <Tabs defaultValue="selection" className="flex min-h-0 flex-1 flex-col gap-0">
            <div className="shrink-0 border-b border-border p-3">
              <TabsList className="grid w-full grid-cols-3">
                <TabsTrigger value="selection">Selection</TabsTrigger>
                <TabsTrigger value="roads">Roads</TabsTrigger>
                <TabsTrigger value="network">Network</TabsTrigger>
              </TabsList>
            </div>

            <ScrollArea className="min-h-0 flex-1">
              <TabsContent value="selection" className="grid gap-4 p-4">
                <SelectionTab
                  selectedRoad={selectedRoad}
                  selectedStation={selection.trackStation}
                  roadLength={selectedRoadLength}
                  selectedIntersection={selectedIntersection}
                  ways={selectionPanelWays}
                  linkedTracks={selectedIntersection?.trackEnds ?? []}
                  roadsList={project.roads.map((r) => ({ id: r.id, name: r.name }))}
                  lockedPassageways={lockedPassageways}
                  constraint={editionConstraint}
                  onConstraintChange={setEditionConstraint}
                  onFunctionsChange={onFunctionsChange}
                  onRenameRoad={renameRoad}
                  onSplitFunction={() => selectedRoad && splitFunctionAt(selectedRoad, selection.trackStation ?? selectedRoadLength / 2)}
                  onMergeFunctions={() => selectedRoad && mergeAtStation(selectedRoad, selection.trackStation)}
                  onInvertOrientation={invertSelectedOrientation}
                  onStickToTerrain={stickSelectedToTerrain}
                  onToggleExit={(contact) => selectedRoad && toggleExit(selectedRoad, contact)}
                  onInsertHandle={insertHandleAtStation}
                  onUpdateIntersection={(patch) => selectedIntersection && updateIntersection(selectedIntersection.id, patch)}
                  onToggleAuthorization={toggleAuthorization}
                  onInvertAuthorizations={invertAuthorizations}
                  onAddContourHandle={() => {
                    if (!selectedIntersection) return
                    updateIntersection(selectedIntersection.id, {
                      contourHandles: [...selectedIntersection.contourHandles, selectedIntersection.position],
                    })
                  }}
                  onDeleteContourHandle={(index) => {
                    if (!selectedIntersection) return
                    updateIntersection(selectedIntersection.id, {
                      contourHandles: selectedIntersection.contourHandles.filter((_, i) => i !== index),
                    })
                  }}
                  onMoveContourHandle={(index, point) => {
                    if (!selectedIntersection) return
                    const handles = [...selectedIntersection.contourHandles]
                    handles[index] = point
                    updateIntersection(selectedIntersection.id, { contourHandles: handles })
                  }}
                  onSetMainPath={(a, b) => selectedIntersection && updateIntersection(selectedIntersection.id, { mainPath: [a, b] })}
                  onLockPassageway={(key) => {
                    const locks = lockedPassageways.includes(key)
                      ? lockedPassageways.filter((k) => k !== key)
                      : [...lockedPassageways, key].slice(-2)
                    setLockedPassageways(locks)
                  }}
                  onDeleteIntersection={() => {
                    if (selectedIntersection) {
                      deleteIntersectionById(selectedIntersection.id)
                      setSelection({ ...selection, intersectionId: null })
                    }
                  }}
                  onUnlinkIntersection={unlinkIntersection}
                  config={config}
                  geoRef={project.geoRef}
                  onConfigChange={setConfig}
                  onUpdateRoad={(patch) => selectedRoad && updateRoad(selectedRoad.id, patch)}
                  onGeoRefChange={setGeoRef}
                />
              </TabsContent>

              <TabsContent value="roads" className="grid gap-1.5 p-4">
                <RoadsTab
                  roads={project.roads}
                  selectedIds={selection.trackIds}
                  roadLengths={roadLengths}
                  tool={tool}
                  draftLength={draftPath?.length ?? null}
                  onRoadClick={(roadId, additive) => {
                    if (additive) {
                      setSelection({
                        trackIds: selection.trackIds.includes(roadId)
                          ? selection.trackIds.filter((id) => id !== roadId)
                          : [...selection.trackIds, roadId],
                        // keep the intersection selected so Ctrl+L keeps working
                        intersectionId: selection.intersectionId,
                        trackStation: null,
                      })
                    } else {
                      selectRoadOnly(roadId)
                    }
                  }}
                />
              </TabsContent>

              <TabsContent value="network" className="grid gap-2 p-4">
                <NetworkTab
                  intersections={project.intersections ?? []}
                  junctions={junctions}
                  activeJunctionCount={activeJunctions.length}
                  layers={layers}
                  selectedIntersectionId={selection.intersectionId}
                  selectedRoad={selectedRoad}
                  selectedRoadLength={selectedRoad ? roadLengths.get(selectedRoad.id) ?? 0 : 0}
                  onSelectIntersection={(id) => setSelection({ trackIds: [], intersectionId: id, trackStation: null })}
                  onDeleteIntersection={deleteIntersectionById}
                  onToggleJunction={(junction) => (junction.suppressed ? restoreJunction(junction.key) : suppressJunction(junction.key))}
                  onRegenerateJunctions={regenerateJunctions}
                  onSetLayer={setLayer}
                />
              </TabsContent>
            </ScrollArea>
          </Tabs>
          ) : section === 'road' ? (
          <Tabs defaultValue="lanes" className="flex min-h-0 flex-1 flex-col gap-0">
            <div className="shrink-0 border-b border-border p-3">
              <TabsList className="grid w-full grid-cols-3">
                <TabsTrigger value="lanes">Lanes</TabsTrigger>
                <TabsTrigger value="sidewalks">Sidewalks</TabsTrigger>
                <TabsTrigger value="roads">Roads</TabsTrigger>
              </TabsList>
            </div>
            <ScrollArea className="min-h-0 flex-1">
              <TabsContent value="lanes" className="grid gap-4 p-4">
                {selectedRoad ? (
                  <>
                    <p className="rounded-md border border-border bg-muted/40 px-3 py-2 text-xs text-muted-foreground">
                      Editing <b className="font-medium text-foreground">{selectedRoad.name}</b> — click a lane in the list or on the canvas, then use the lane tools.
                    </p>
                    <LanesTab road={selectedRoad} />
                    <PortionProfileEditor road={selectedRoad} length={roadLengths.get(selectedRoad.id) ?? 0} />
                  </>
                ) : (
                  <div className="grid gap-2 rounded-md border border-dashed border-border/60 p-4 text-center">
                    <p className="text-xs font-medium text-foreground">No road selected</p>
                    <p className="text-xs text-muted-foreground">Pick a road from the Roads tab, or click one on the canvas with the Select tool.</p>
                  </div>
                )}
              </TabsContent>
              <TabsContent value="sidewalks" className="grid gap-4 p-4">
                {selectedRoad ? (
                  <SidewalkPanel road={selectedRoad} />
                ) : (
                  <p className="text-xs text-muted-foreground">Select a road to add sidewalks and curbs along its alignment.</p>
                )}
              </TabsContent>
              <TabsContent value="roads" className="grid gap-1.5 p-4">
                <RoadsTab
                  roads={project.roads}
                  selectedIds={selection.trackIds}
                  roadLengths={roadLengths}
                  tool={tool}
                  draftLength={draftPath?.length ?? null}
                  onRoadClick={(roadId, additive) => {
                    if (additive) {
                      setSelection({
                        trackIds: selection.trackIds.includes(roadId)
                          ? selection.trackIds.filter((id) => id !== roadId)
                          : [...selection.trackIds, roadId],
                        intersectionId: selection.intersectionId,
                        trackStation: null,
                      })
                    } else {
                      selectRoadOnly(roadId)
                    }
                  }}
                />
              </TabsContent>
            </ScrollArea>
          </Tabs>
          ) : (
          <Tabs defaultValue="track" className="flex min-h-0 flex-1 flex-col gap-0">
            <div className="shrink-0 border-b border-border p-3">
              <TabsList className="grid w-full grid-cols-3">
                <TabsTrigger value="track">Track</TabsTrigger>
                <TabsTrigger value="tracks">Tracks</TabsTrigger>
                <TabsTrigger value="fixtures">Fixtures</TabsTrigger>
              </TabsList>
            </div>
            <ScrollArea className="min-h-0 flex-1">
              <TabsContent value="track" className="grid gap-4 p-4">
                <TrainTab
                  selectedRoad={selectedRoad}
                  onUpdateRoad={(patch) => selectedRoad && updateRoad(selectedRoad.id, patch)}
                />
              </TabsContent>
              <TabsContent value="tracks" className="grid gap-1.5 p-4">
                {selection.trackIds.length === 3 && (
                  <Button size="sm" variant="secondary" className="h-7 text-xs" onClick={createRailPointFromSelection}>
                    Insert Turnout (3 selected)
                  </Button>
                )}
                <RoadsTab
                  roads={project.roads.filter((r) => r.railway)}
                  selectedIds={selection.trackIds}
                  roadLengths={roadLengths}
                  tool={tool}
                  draftLength={draftPath?.length ?? null}
                  onRoadClick={(roadId, additive) => {
                    if (additive) {
                      setSelection({
                        trackIds: selection.trackIds.includes(roadId)
                          ? selection.trackIds.filter((id) => id !== roadId)
                          : [...selection.trackIds, roadId],
                        intersectionId: selection.intersectionId,
                        trackStation: null,
                      })
                    } else {
                      selectRoadOnly(roadId)
                    }
                  }}
                />
              </TabsContent>
              <TabsContent value="fixtures" className="grid gap-2 p-4">
                {(project.railPoints ?? []).length === 0 && (project.railCrossings ?? []).length === 0 && (project.catchPoints ?? []).length === 0 ? (
                  <p className="text-xs text-muted-foreground">
                    No fixtures yet. Use the Fixtures tools on the left rail: Insert Point (turnout blade), Insert Frog / Diamond (crossing), Catch Point.
                  </p>
                ) : (
                  <>
                    {(project.railPoints ?? []).map((pt) => {
                      const nameOf = (id: string) => project.roads.find((r) => r.id === id)?.name ?? id.slice(0, 8)
                      return (
                        <div key={pt.id} className="flex items-center gap-2 rounded-lg border border-border bg-muted/40 px-3 py-2 text-xs">
                          <div className="min-w-0 flex-1">
                            <div className="truncate font-medium">{pt.name} — Turnout</div>
                            <div className="truncate text-muted-foreground">main {nameOf(pt.trailingTrackId)} · branch {nameOf(pt.branchTrackId)}</div>
                          </div>
                          <Button size="sm" variant="ghost" className="h-7 px-2" onClick={() => removeRailPointById(pt.id)}>
                            <Trash2 className="size-3.5" />
                          </Button>
                        </div>
                      )
                    })}
                    {(project.railCrossings ?? []).map((crossing) => {
                      const nameOf = (id: string) => project.roads.find((r) => r.id === id)?.name ?? id.slice(0, 8)
                      return (
                        <div key={crossing.id} className="flex items-center gap-2 rounded-lg border border-border bg-muted/40 px-3 py-2 text-xs">
                          <div className="min-w-0 flex-1">
                            <div className="truncate font-medium capitalize">{crossing.kind}</div>
                            <div className="truncate text-muted-foreground">{nameOf(crossing.trackAId)} × {nameOf(crossing.trackBId)} · {crossing.angle.toFixed(2)} rad</div>
                          </div>
                          <Button size="sm" variant="ghost" className="h-7 px-2" onClick={() => removeRailCrossingById(crossing.id)}>
                            <Trash2 className="size-3.5" />
                          </Button>
                        </div>
                      )
                    })}
                    {(project.catchPoints ?? []).map((cp) => {
                      const nameOf = (id: string) => project.roads.find((r) => r.id === id)?.name ?? id.slice(0, 8)
                      return (
                        <div key={cp.id} className="flex items-center gap-2 rounded-lg border border-border bg-muted/40 px-3 py-2 text-xs">
                          <div className="min-w-0 flex-1">
                            <div className="truncate font-medium">Catch Point</div>
                            <div className="truncate text-muted-foreground">{nameOf(cp.trackId)} · {cp.contact}, blade {cp.side}</div>
                          </div>
                          <Button size="sm" variant="ghost" className="h-7 px-2" onClick={() => removeCatchPointById(cp.id)}>
                            <Trash2 className="size-3.5" />
                          </Button>
                        </div>
                      )
                    })}
                  </>
                )}
              </TabsContent>
            </ScrollArea>
          </Tabs>
          )}
        </aside>
      </div>

      {selectedRoad && (
        <footer className="shrink-0 border-t border-border bg-card/60">
          <ElevationProfileEditor
            road={selectedRoad}
            length={roadLengths.get(selectedRoad.id) ?? 0}
            onChange={(profile) => updateRoad(selectedRoad.id, { elevationProfile: profile })}
          />
        </footer>
      )}

      {menu && menuContent && (
        <RoadsContextMenu
          position={menu.screen}
          title={menuContent.title}
          items={menuContent.items}
          onClose={() => setMenu(null)}
        />
      )}
    </div>
  )
}
