import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { Boxes, Car, ChevronDown, ChevronRight, Eye, EyeOff, Images, List, Lock, Maximize, Mountain, Pause, Play, RotateCcw, Route, Save, Search, Target, Trash2, Unlock } from 'lucide-react'
import { toast } from 'sonner'

import { buildJunctionNetwork, visibleRoadRanges, buildJunctionSurface, buildJunctionMarkings } from '../engine/junctions'
import { buildConnectingRoadMesh, buildRailwayMesh, buildRoadMeshRange } from '../engine/mesh'
import { buildRailFixtureObjects } from '../engine/railFixtures'
import { buildTerrainMeshWorld, type TerrainMeshData } from '../engine/terrainMesh'
import { loadImageryTexture } from '../terrain/imageryTexture'
import { allWays, resolveTracks } from '../engine/intersections'
import { fitRoadGeometry } from '../engine/roadGeometry'
import { samplePath } from '../engine/geometry'
import { stickTrackToTerrain } from '../engine/tracks'
import { sectionHalfWidth } from '../engine/laneLayout'
import { buildRoadSamplers, ROAD_LIFT as SHARED_ROAD_LIFT } from '../engine/roadServices'
import { makeTerrainSampler, getActiveTerrain } from '../terrain/terrainRegistry'
import { buildBuildingMesh, ringCentroid } from '../engine/osmBuildings'
import { generatePcgBuildingMesh, DEFAULT_PCG_CONFIG, type PcgStyle, type PcgDetail } from '../engine/pcgBuildings'
import { getAsphaltTextures, type RoadWear } from '../viewport/asphaltMaterial'
import { buildSimPaths, spawnVehicles, stepSimulation, simulationPoses, type SimVehicle, type SimPath } from '../engine/simulation'
import type { MeshData } from '../engine/mesh'
import { useStore, getLaneSection } from '../state/store'
import type { Project, RoadData } from '../state/store'
import AppHeader from '@/components/layout/AppHeader'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'

interface Studio3DPageProps {
  onBack: () => void
}

function roadSection(road: RoadData) {
  return getLaneSection(road)
}

/** Lift roads above the terrain surface to avoid z-fighting (the decimated
 *  terrain grid interpolates up to ~0.5 m off the true surface).
 *  Uses the shared constant from engine/roadServices. */
const ROAD_LIFT = SHARED_ROAD_LIFT

/** Shared vehicle body geometry (SCANeR-style surrogate vehicle). */
const CAR_GEOMETRY = new THREE.BoxGeometry(4.2, 1.5, 1.9)

// ─── Scene content built from project + registry terrain ───────────

/** One selectable scene object in the 3D Studio outliner. */
export interface StudioRoadObject {
  id: string
  name: string
  kind: 'road' | 'junction' | 'intersection' | 'rail' | 'building'
  mesh: MeshData
  /** semantic surface type — pavement gets the asphalt PBR set, markings get
   *  a plain vertex-colored material so they stay clean white/yellow/green */
  surface?: 'pavement' | 'marking' | 'misc'
  /** OSM tags etc. for the inspector */
  tags?: Record<string, string>
}

/** Build all road meshes (roads + junction connectors + intersection ways +
 *  rail fixtures) in world space, tagged with outliner identity. */
function buildProjectRoadObjects(project: Project, drape: boolean): StudioRoadObject[] {
  if (project.roads.length === 0) return []

  // Drape roads that have no elevation profile yet onto the background terrain
  const sampler = makeTerrainSampler(project.geoRef)
  const effective = project.roads.map((road) => {
    if (!drape || road.elevationProfile?.length) return road
    const result = stickTrackToTerrain({ ...road, elevationProfile: undefined }, sampler, sectionHalfWidth(getLaneSection(road)))
    if (!result) return road
    return { ...road, elevationProfile: result.elevation, bankingProfile: result.banking }
  })

  const samplers = buildRoadSamplers(effective, true)
  const elevationSamplers = samplers.elevation
  const bankingSamplers = samplers.banking

  const junctionNetwork = buildJunctionNetwork(effective, project.suppressedJunctions, elevationSamplers)
  if (!junctionNetwork) return []

  const objects: StudioRoadObject[] = []
  const push = (id: string, name: string, kind: StudioRoadObject['kind'], mesh: MeshData | null, surface?: 'pavement' | 'marking' | 'misc') => {
    if (mesh) objects.push({ id, name, kind, mesh, surface })
  }
  for (const road of effective) {
    const path = junctionNetwork.paths.get(road.id)
    if (!path) continue
    const section = roadSection(road)
    const cuts = junctionNetwork.cuts.filter((cut) => cut.roadId === road.id)
    for (const range of visibleRoadRanges(path, cuts)) {
      const result = buildRoadMeshRange(path, section, range.sStart, range.sEnd, 1, elevationSamplers.get(road.id), bankingSamplers.get(road.id), road.tapers)
      push(road.id, road.name, 'road', result.pavement, 'pavement')
      push(`${road.id}:markings`, `${road.name} (markings)`, 'road', result.markings, 'marking')
    }
  }

  // Unified junction surfaces: one continuous pavement polygon per junction
  // instead of overlapping full-width turning-road strips
  for (const junction of junctionNetwork.junctions.filter((j) => !j.suppressed)) {
    const surface = buildJunctionSurface(junction, elevationSamplers)
    push(`jx:${junction.id}`, `Junction · ${junction.approaches.length} arms`, 'junction', surface.mesh, 'pavement')
    const markings = buildJunctionMarkings(junction)
    push(`jx:${junction.id}:markings`, `Junction markings`, 'junction', markings, 'marking')
  }

  // explicit intersections render their ways just like auto junctions
  const resolved = resolveTracks(effective)
  for (const intersection of project.intersections ?? []) {
    if (intersection.trackEnds.length < 2) continue
    for (const way of allWays(intersection, resolved)) {
      const result = buildConnectingRoadMesh(way.samples, Math.max(1, way.laneCount), way.laneWidth)
      push(`ix:${intersection.id}`, intersection.groundName || 'Intersection', 'intersection', result.pavement, 'pavement')
      push(`ix:${intersection.id}:markings`, `${intersection.groundName || 'Intersection'} (markings)`, 'intersection', result.markings, 'marking')
    }
  }
  // rail fixtures (turnout blades, frogs/diamonds, guard rails, catch points)
  for (const fixture of buildRailFixtureObjects(project)) {
    for (const mesh of fixture.meshes) objects.push({ id: fixture.id, name: fixture.name, kind: 'rail', mesh })
  }
  return objects
}

// ─── Page ──────────────────────────────────────────────────────────

/** One row in the scene outliner. */
export interface OutlinerEntry {
  id: string
  name: string
  kind: 'terrain' | 'road' | 'junction' | 'intersection' | 'rail' | 'building' | 'traffic' | 'helpers'
  /** OSM attributes for the inspector */
  tags?: Record<string, string>
  /** PCG generation info for the inspector */
  pcg?: { mode: string; style: string; seed: number; override: number }
}

export default function Studio3DPage({ onBack }: Studio3DPageProps) {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const saveCurrentProject = useStore((s) => s.saveCurrentProject)
  const setOsmBuildings = useStore((s) => s.setOsmBuildings)
  const deleteOsmBuilding = useStore((s) => s.deleteOsmBuilding)
  const setPcgConfig = useStore((s) => s.setPcgConfig)
  const regeneratePcgBuilding = useStore((s) => s.regeneratePcgBuilding)
  const project = projects.find((p) => p.id === activeProjectId)

  const [heightScale, setHeightScale] = useState(1)
  const [showRoads, setShowRoads] = useState(true)
  const [showTerrain, setShowTerrain] = useState(true)
  const [wireframe, setWireframe] = useState(false)
  const [drape, setDrape] = useState(true)
  const [roadWear, setRoadWear] = useState<RoadWear>('normal')
  const [fitSignal, setFitSignal] = useState(0)
  const [saving, setSaving] = useState(false)

  const terrain = useMemo(() => getActiveTerrain(), [])
  const terrainMesh = useMemo(
    () => (terrain ? buildTerrainMeshWorld(terrain, project?.geoRef) : null),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [terrain],
  )
  const roadObjects = useMemo(
    () => (project ? buildProjectRoadObjects(project, drape) : []),
    [project, drape],
  )
  // OSM buildings (simple extrusion or PCG-generated, per project config)
  const pcgConfig = project?.pcgConfig ?? DEFAULT_PCG_CONFIG
  const buildingObjects = useMemo<StudioRoadObject[]>(() => {
    const buildings = project?.osmBuildings
    if (!buildings?.length || !project?.geoRef) return []
    const sampler = makeTerrainSampler(project.geoRef)
    const out: StudioRoadObject[] = []
    for (const building of buildings) {
      const centroid = ringCentroid(building.ring)
      const baseZ = sampler(centroid.x, centroid.y) ?? 0
      const mesh = pcgConfig.mode === 'pcg'
        ? generatePcgBuildingMesh(building, baseZ, {
            style: pcgConfig.style,
            seed: pcgConfig.seed,
            detail: pcgConfig.detail,
            override: pcgConfig.overrides[building.id] || 0,
          })
        : buildBuildingMesh(building, baseZ)
      if (mesh) out.push({ id: building.id, name: building.name, kind: 'building', mesh, tags: building.tags })
    }
    return out
  }, [project?.osmBuildings, project?.geoRef, pcgConfig])

  // ── Scene outliner state ──
  const [playing, setPlaying] = useState(false)
  const [outlinerOpen, setOutlinerOpen] = useState(true)
  const [selectedIds, setSelectedIds] = useState<string[]>([])
  const [hiddenIds, setHiddenIds] = useState<Record<string, boolean>>({})
  const [lockedIds, setLockedIds] = useState<Record<string, boolean>>({})
  const [focusSignal, setFocusSignal] = useState(0)

  const outlinerEntries = useMemo<OutlinerEntry[]>(() => {
    const entries: OutlinerEntry[] = []
    if (terrainMesh) entries.push({ id: 'terrain', name: 'Terrain surface', kind: 'terrain' })
    for (const object of roadObjects) {
      entries.push({ id: object.id, name: object.name, kind: object.kind })
    }
    for (const building of buildingObjects) {
      entries.push({
        id: building.id,
        name: building.name,
        kind: 'building',
        tags: building.tags,
        pcg: pcgConfig.mode === 'pcg'
          ? { mode: 'pcg', style: pcgConfig.style, seed: pcgConfig.seed, override: pcgConfig.overrides[building.id] || 0 }
          : { mode: 'extrusion', style: '-', seed: 0, override: 0 },
      })
    }
    if (playing) entries.push({ id: 'traffic', name: 'Traffic vehicles', kind: 'traffic' })
    entries.push({ id: 'helpers', name: 'Grid (helper)', kind: 'helpers' })
    return entries
  }, [terrainMesh, roadObjects, buildingObjects, playing])

  // drop selections that no longer exist after project edits
  useEffect(() => {
    const alive = new Set(outlinerEntries.map((entry) => entry.id))
    setSelectedIds((ids) => ids.filter((id) => alive.has(id)))
  }, [outlinerEntries])

  function selectFromViewport(id: string | null, additive: boolean) {
    setSelectedIds((ids) => {
      if (id === null) return additive ? ids : []
      if (additive) return ids.includes(id) ? ids.filter((x) => x !== id) : [...ids, id]
      return ids.includes(id) ? ids : [id]
    })
  }
  function toggleHidden(id: string) {
    setHiddenIds((map) => ({ ...map, [id]: !map[id] }))
  }
  function toggleLocked(id: string) {
    setLockedIds((map) => ({ ...map, [id]: !map[id] }))
  }
  /** hide everything except the given object (outliner isolate) */
  function isolateEntry(id: string) {
    const map: Record<string, boolean> = {}
    for (const entry of outlinerEntries) {
      if (entry.id !== id) map[entry.id] = true
    }
    setHiddenIds(map)
  }
  function updatePcg(patch: Partial<typeof pcgConfig>) {
    setPcgConfig({ ...DEFAULT_PCG_CONFIG, ...pcgConfig, ...patch })
  }
  function regenerateAllPcg() {
    updatePcg({ seed: 1 + Math.floor(Math.random() * 1_000_000), overrides: {} })
  }
  // simulation paths: plan polylines sampled at 2 m per road
  const simPaths = useMemo<SimPath[]>(() => {
    if (!project) return []
    return project.roads.flatMap((road) => {
      const path = fitRoadGeometry(road)
      if (!path) return []
      const samples = samplePath(path, 2).map((sm) => ({ s: sm.s, x: sm.x, y: sm.y, heading: sm.heading }))
      return [{ roadId: road.id, name: road.name, length: path.length, samples }]
    })
  }, [project])

  // traffic simulation (simulation parity): vehicles driving the network
  const [simSpeed, setSimSpeed] = useState(1)
  const [vehicleCount, setVehicleCount] = useState(8)
  // satellite imagery draped onto the terrain surface
  const [imagery, setImagery] = useState(true)
  const [imageryTexture, setImageryTexture] = useState<THREE.CanvasTexture | null>(null)
  useEffect(() => {
    let cancelled = false
    if (!terrain || !imagery) {
      setImageryTexture(null)
      return
    }
    loadImageryTexture(terrain).then((texture) => {
      if (!cancelled) setImageryTexture(texture)
    })
    return () => { cancelled = true }
  }, [terrain, imagery])

  async function handleSave() {
    setSaving(true)
    try {
      await saveCurrentProject()
      toast.success('Project saved', { description: `C:\\OpenGeoStudio\\projects\\${project?.name ?? ''}` })
    } catch (err) {
      toast.error('Save failed', { description: (err as Error).message })
    } finally {
      setSaving(false)
    }
  }

  const hasContent = roadObjects.length > 0 || buildingObjects.length > 0 || !!terrainMesh

  return (
    <div className="flex h-screen flex-col bg-background">
      <AppHeader projectName={project?.name ?? 'Unknown'} subtitle="3D Studio" onBack={onBack}>
        <Badge variant="outline" className="gap-1">
          <Route className="size-3" />
          {project?.roads.length ?? 0} road{(project?.roads.length ?? 0) === 1 ? '' : 's'}
        </Badge>
        {terrain && (
          <Badge variant="outline" className="gap-1">
            <Mountain className="size-3" />
            Terrain
          </Badge>
        )}
        <Separator orientation="vertical" className="h-5" />
        <Button size="sm" variant="secondary" onClick={handleSave} disabled={saving}>
          <Save className="size-4" />
          {saving ? 'Saving…' : 'Save Project'}
        </Button>
      </AppHeader>

      {/* Toolbar */}
      <div className="flex h-10 shrink-0 items-center gap-2 border-b border-border bg-card/50 px-3 text-xs">
        <ToolbarToggle active={showTerrain} onClick={() => setShowTerrain((v) => !v)} icon={<Mountain className="size-3.5" />} label="Terrain" />
        <ToolbarToggle active={showRoads} onClick={() => setShowRoads((v) => !v)} icon={<Route className="size-3.5" />} label="Roads" />
        <ToolbarToggle active={drape} onClick={() => setDrape((v) => !v)} icon={<Route className="size-3.5" />} label="Drape on Terrain" />
        <ToolbarToggle active={imagery} onClick={() => setImagery((v) => !v)} icon={<Images className="size-3.5" />} label="Imagery" />
        <ToolbarToggle active={wireframe} onClick={() => setWireframe((v) => !v)} icon={<Boxes className="size-3.5" />} label="Wireframe" />
        <label className="flex items-center gap-1.5 text-muted-foreground" title="Procedural asphalt material variant">
          Asphalt
          <select
            className="h-6 rounded-md border border-border bg-background px-1 text-xs text-foreground"
            value={roadWear}
            onChange={(e) => setRoadWear(e.target.value as RoadWear)}
          >
            <option value="fresh">Fresh</option>
            <option value="normal">Normal</option>
            <option value="worn">Worn</option>
          </select>
        </label>
        <Separator orientation="vertical" className="h-5" />
        <ToolbarToggle active={playing} onClick={() => setPlaying((v) => !v)} icon={playing ? <Pause className="size-3.5" /> : <Car className="size-3.5" />} label={playing ? 'Pause Traffic' : 'Traffic'} />
        {playing && (
          <>
            <label className="flex items-center gap-1 text-muted-foreground">
              Vehicles
              <input
                type="number"
                min={1}
                max={60}
                className="h-6 w-14 rounded-md border border-border bg-background px-1.5 text-xs text-foreground"
                value={vehicleCount}
                onChange={(e) => setVehicleCount(Math.max(1, Math.min(60, parseInt(e.target.value, 10) || 8)))}
              />
            </label>
            <label className="flex items-center gap-1 text-muted-foreground">
              Speed
              <select
                className="h-6 rounded-md border border-border bg-background px-1 text-xs text-foreground"
                value={simSpeed}
                onChange={(e) => setSimSpeed(parseFloat(e.target.value))}
              >
                <option value={0.25}>0.25x</option>
                <option value={0.5}>0.5x</option>
                <option value={1}>1x</option>
                <option value={2}>2x</option>
                <option value={5}>5x</option>
              </select>
            </label>
          </>
        )}
        {buildingObjects.length > 0 && (
          <>
            <Separator orientation="vertical" className="h-5" />
            <ToolbarToggle
              active={pcgConfig.mode === 'pcg'}
              onClick={() => updatePcg({ mode: pcgConfig.mode === 'pcg' ? 'extrusion' : 'pcg' })}
              icon={<Boxes className="size-3.5" />}
              label="PCG Buildings"
              title="Toggle procedural building generation"
            />
            {pcgConfig.mode === 'pcg' && (
              <>
                <label className="flex items-center gap-1 text-muted-foreground">
                  Style
                  <select
                    className="h-6 rounded-md border border-border bg-background px-1 text-xs text-foreground"
                    value={pcgConfig.style}
                    onChange={(e) => updatePcg({ style: e.target.value as PcgStyle })}
                  >
                    <option value="residential">Residential</option>
                    <option value="commercial">Commercial</option>
                    <option value="industrial">Industrial</option>
                    <option value="generic">Generic</option>
                  </select>
                </label>
                <label className="flex items-center gap-1 text-muted-foreground">
                  Detail
                  <select
                    className="h-6 rounded-md border border-border bg-background px-1 text-xs text-foreground"
                    value={pcgConfig.detail}
                    onChange={(e) => updatePcg({ detail: e.target.value as PcgDetail })}
                  >
                    <option value="low">Low</option>
                    <option value="medium">Medium</option>
                    <option value="high">High</option>
                  </select>
                </label>
                <Button
                  size="sm"
                  variant="ghost"
                  className="h-7 gap-1.5 px-2 text-xs"
                  onClick={() => regenerateAllPcg()}
                  title="New random seed for all buildings"
                >
                  <RotateCcw className="size-3.5" />
                  Regenerate All
                </Button>
              </>
            )}
          </>
        )}
        <Separator orientation="vertical" className="h-5" />
        <label className="flex items-center gap-2 text-muted-foreground">
          Height Scale
          <input
            type="range"
            min={0.1}
            max={5}
            step={0.1}
            value={heightScale}
            onChange={(e) => setHeightScale(parseFloat(e.target.value))}
            className="h-1 w-24 cursor-pointer accent-primary"
          />
          <span className="w-8 tabular-nums text-foreground">{heightScale.toFixed(1)}x</span>
        </label>
        <Separator orientation="vertical" className="h-5" />
        <Button size="sm" variant="ghost" className="h-7 gap-1.5 px-2 text-xs" onClick={() => setFocusSignal((v) => v + 1)} disabled={selectedIds.length === 0} title="Focus selected (F)">
          <Target className="size-3.5" />
          Focus
        </Button>
        <Button size="sm" variant="ghost" className="h-7 gap-1.5 px-2 text-xs" onClick={() => setFitSignal((v) => v + 1)}>
          <Maximize className="size-3.5" />
          Fit View
        </Button>
        <ToolbarToggle active={outlinerOpen} onClick={() => setOutlinerOpen((v) => !v)} icon={<List className="size-3.5" />} label="Outliner" />
        {!terrain && (
          <span className="ml-auto text-[11px] text-muted-foreground">
            No background terrain — download terrain in the Terrain workspace to see it here.
          </span>
        )}
      </div>

      <Studio3DViewport
        roadObjects={roadObjects}
        buildingObjects={buildingObjects}
        terrainMesh={showTerrain ? terrainMesh : null}
        imageryTexture={imagery ? imageryTexture : null}
        heightScale={heightScale}
        wireframe={wireframe}
        roadWear={roadWear}
        fitSignal={fitSignal}
        focusSignal={focusSignal}
        outlinerOpen={outlinerOpen}
        entries={outlinerEntries}
        selectedIds={selectedIds}
        hiddenIds={{ ...hiddenIds, ...(showRoads ? {} : { __ALL_ROADS__: true }) }}
        lockedIds={lockedIds}
        onSelect={(id, additive) => selectFromViewport(id, additive)}
        onToggleHidden={toggleHidden}
        onToggleLocked={toggleLocked}
        onFocus={() => setFocusSignal((v) => v + 1)}
        onDeleteBuilding={deleteOsmBuilding}
        onIsolate={isolateEntry}
        onRegenerateBuilding={regeneratePcgBuilding}
        hasContent={hasContent}
        roadCount={project?.roads.length ?? 0}
        onFit={() => setFitSignal((v) => v + 1)}
        playing={playing}
        simSpeed={simSpeed}
        vehicleCount={vehicleCount}
        simPaths={simPaths}
      />
    </div>
  )
}

function ToolbarToggle({ active, onClick, icon, label, title }: { active: boolean; onClick: () => void; icon: ReactNode; label: string; title?: string }) {
  return (
    <button
      className={`flex items-center gap-1.5 rounded-md px-2.5 py-1 font-medium transition-colors ${active ? 'bg-primary/15 text-primary' : 'text-muted-foreground hover:bg-muted'}`}
      onClick={onClick}
      title={title}
    >
      {icon}
      {label}
    </button>
  )
}

// ─── 3D Viewport ───────────────────────────────────────────────────

interface Studio3DViewportProps {
  roadObjects: StudioRoadObject[]
  buildingObjects: StudioRoadObject[]
  terrainMesh: TerrainMeshData | null
  imageryTexture: THREE.CanvasTexture | null
  heightScale: number
  wireframe: boolean
  roadWear: RoadWear
  fitSignal: number
  focusSignal: number
  outlinerOpen: boolean
  entries: OutlinerEntry[]
  selectedIds: string[]
  hiddenIds: Record<string, boolean>
  lockedIds: Record<string, boolean>
  onSelect: (id: string | null, additive: boolean) => void
  onToggleHidden: (id: string) => void
  onToggleLocked: (id: string) => void
  onFocus: () => void
  onDeleteBuilding: (id: string) => void
  onIsolate: (id: string) => void
  onRegenerateBuilding: (id: string) => void
  hasContent: boolean
  roadCount: number
  onFit: () => void
  playing: boolean
  simSpeed: number
  vehicleCount: number
  simPaths: SimPath[]
}

function Studio3DViewport({ roadObjects, buildingObjects, terrainMesh, imageryTexture, heightScale, wireframe, roadWear, fitSignal, focusSignal, outlinerOpen, entries, selectedIds, hiddenIds, lockedIds, onSelect, onToggleHidden, onToggleLocked, onFocus, onDeleteBuilding, onIsolate, onRegenerateBuilding, hasContent, roadCount, onFit, playing, simSpeed, vehicleCount, simPaths }: Studio3DViewportProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const sceneRef = useRef<{
    scene: THREE.Scene
    camera: THREE.PerspectiveCamera
    controls: OrbitControls
    terrainGroup: THREE.Group
    roadGroup: THREE.Group
    buildingGroup: THREE.Group
    simGroup: THREE.Group
    grid: THREE.GridHelper
    renderer: THREE.WebGLRenderer
    /** per-outliner-entry groups with pickable, tagged meshes */
    entryGroups: Map<string, THREE.Group>
  } | null>(null)
  const simRef = useRef<{ vehicles: SimVehicle[]; paths: SimPath[]; last: number } | null>(null)
  // latest props for one-time event listeners (viewport picks / shortcuts)
  const pickRef = useRef({ hiddenIds, lockedIds, onSelect })
  pickRef.current = { hiddenIds, lockedIds, onSelect }
  const keyRef = useRef({ onFocus, onSelect })
  keyRef.current = { onFocus, onSelect }

  // ── One-time scene setup ──
  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setPixelRatio(window.devicePixelRatio)
    renderer.setSize(container.clientWidth, container.clientHeight)
    renderer.domElement.style.display = 'block'
    const suppressMenu = (e: MouseEvent) => e.preventDefault()
    renderer.domElement.addEventListener('contextmenu', suppressMenu)
    container.appendChild(renderer.domElement)

    const scene = new THREE.Scene()
    scene.background = new THREE.Color('#0b1220')
    scene.fog = new THREE.Fog(0x0b1220, 4000, 20000)

    const aspect = container.clientWidth / Math.max(1, container.clientHeight)
    const camera = new THREE.PerspectiveCamera(55, aspect, 0.1, 60000)
    camera.position.set(300, 260, 380)

    const hemisphere = new THREE.HemisphereLight(0xcfd8e8, 0x3a4636, 0.85)
    const sun = new THREE.DirectionalLight(0xfff2dd, 1.35)
    sun.position.set(600, 900, 400)
    scene.add(hemisphere, sun)

    const grid = new THREE.GridHelper(4000, 80, 0x24304a, 0x18233a)
    ;(grid.material as THREE.Material).transparent = true
    ;(grid.material as THREE.Material).opacity = 0.35
    scene.add(grid)

    const terrainGroup = new THREE.Group()
    const roadGroup = new THREE.Group()
    const buildingGroup = new THREE.Group()
    scene.add(terrainGroup, roadGroup, buildingGroup)

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.08
    controls.maxPolarAngle = Math.PI / 2 - 0.02
    controls.screenSpacePanning = false

    const simGroup = new THREE.Group()
    scene.add(simGroup)
    sceneRef.current = { scene, camera, controls, terrainGroup, roadGroup, buildingGroup, simGroup, grid, renderer, entryGroups: new Map() }
    // dev aid: inspect the live scene from the console / automation
    ;(window as unknown as Record<string, unknown>).__ogsScene3d = { scene, camera, terrainGroup, roadGroup, buildingGroup, simGroup, grid }

    // ── Viewport picking: click (no drag) selects the outliner object under the cursor ──
    const raycaster = new THREE.Raycaster()
    const pickDown = { x: 0, y: 0 }
    let pickArmed = false
    const onPickDown = (e: PointerEvent) => {
      if (e.button !== 0) return
      pickDown.x = e.clientX
      pickDown.y = e.clientY
      pickArmed = true
    }
    const onPickUp = (e: PointerEvent) => {
      if (e.button !== 0 || !pickArmed) return
      pickArmed = false
      if (Math.hypot(e.clientX - pickDown.x, e.clientY - pickDown.y) > 5) return // drag, not a pick
      const ref = sceneRef.current
      if (!ref) return
      const rect = renderer.domElement.getBoundingClientRect()
      const ndc = new THREE.Vector2(
        ((e.clientX - rect.left) / rect.width) * 2 - 1,
        -((e.clientY - rect.top) / rect.height) * 2 + 1,
      )
      raycaster.setFromCamera(ndc, ref.camera)
      const hits = raycaster.intersectObjects([ref.terrainGroup, ref.roadGroup, ref.buildingGroup], true)
      const { hiddenIds: hidden, lockedIds: locked, onSelect: select } = pickRef.current
      for (const hit of hits) {
        let node: THREE.Object3D | null = hit.object
        while (node && node.userData.outlinerId === undefined) node = node.parent
        const id = node?.userData.outlinerId as string | undefined
        if (!id) continue
        if (locked[id] || hidden[id]) continue // locked/hidden objects are not pickable
        select(id, e.ctrlKey || e.shiftKey)
        return
      }
      select(null, e.ctrlKey || e.shiftKey) // empty space clears (unless additive)
    }
    renderer.domElement.addEventListener('pointerdown', onPickDown)
    renderer.domElement.addEventListener('pointerup', onPickUp)

    // F = focus selected, Esc = clear selection
    const onShortcut = (e: KeyboardEvent) => {
      const tag = e.target as HTMLElement | null
      if (tag && (tag.tagName === 'INPUT' || tag.tagName === 'SELECT' || tag.tagName === 'TEXTAREA')) return
      if (e.code === 'KeyF') keyRef.current.onFocus()
      if (e.code === 'Escape') keyRef.current.onSelect(null, false)
    }
    window.addEventListener('keydown', onShortcut)

    // WASD fly controls (Unreal-style): move on the ground plane relative to view
    const keys = new Set<string>()
    const keyDown = (e: KeyboardEvent) => {
      const tag = e.target as HTMLElement | null
      if (tag && (tag.tagName === 'INPUT' || tag.tagName === 'SELECT' || tag.tagName === 'TEXTAREA')) return
      keys.add(e.code)
    }
    const keyUp = (e: KeyboardEvent) => keys.delete(e.code)
    window.addEventListener('keydown', keyDown)
    window.addEventListener('keyup', keyUp)
    let flyLast = performance.now()
    const flyTick = () => {
      const now = performance.now()
      const dt = Math.min(0.1, (now - flyLast) / 1000)
      flyLast = now
      if (keys.size === 0) return
      const speed = (keys.has('ShiftLeft') || keys.has('ShiftRight') ? 240 : 80) * dt
      const forward = new THREE.Vector3()
      camera.getWorldDirection(forward)
      forward.y = 0
      if (forward.lengthSq() < 1e-6) forward.set(0, 0, -1)
      forward.normalize()
      const right = new THREE.Vector3().crossVectors(forward, new THREE.Vector3(0, 1, 0)).negate()
      const move = new THREE.Vector3()
      if (keys.has('KeyW')) move.addScaledVector(forward, speed)
      if (keys.has('KeyS')) move.addScaledVector(forward, -speed)
      if (keys.has('KeyA')) move.addScaledVector(right, speed)
      if (keys.has('KeyD')) move.addScaledVector(right, -speed)
      if (keys.has('KeyE') || keys.has('Space')) move.y += speed
      if (keys.has('KeyQ')) move.y -= speed
      if (move.lengthSq() > 0) {
        camera.position.add(move)
        controls.target.add(move)
      }
    }
    const flyInterval = window.setInterval(flyTick, 16)
    ;(window as unknown as Record<string, unknown>).__ogsFlyCleanup = () => {
      window.clearInterval(flyInterval)
      window.removeEventListener('keydown', keyDown)
      window.removeEventListener('keyup', keyUp)
    }

    function resize() {
      const w = container!.clientWidth
      const h = container!.clientHeight
      if (!w || !h) return
      renderer.setSize(w, h)
      camera.aspect = w / h
      camera.updateProjectionMatrix()
    }
    const observer = new ResizeObserver(resize)
    observer.observe(container)

    let frame = 0
    const render = () => {
      frame = requestAnimationFrame(render)
      controls.update()
      renderer.render(scene, camera)
    }
    frame = requestAnimationFrame(render)

    return () => {
      cancelAnimationFrame(frame)
      observer.disconnect()
      controls.dispose()
      disposeGroup(terrainGroup)
      disposeGroup(roadGroup)
      disposeGroup(buildingGroup)
      renderer.domElement.removeEventListener('contextmenu', suppressMenu)
      renderer.domElement.removeEventListener('pointerdown', onPickDown)
      renderer.domElement.removeEventListener('pointerup', onPickUp)
      window.removeEventListener('keydown', onShortcut)
      ;(window as unknown as { __ogsFlyCleanup?: () => void }).__ogsFlyCleanup?.()
      renderer.dispose()
      renderer.domElement.remove()
      sceneRef.current = null
    }
  }, [])

  // ── Populate terrain + road meshes, tagged per outliner entry ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    disposeGroup(ref.terrainGroup)
    disposeGroup(ref.roadGroup)
    // drop only this group's entries — buildings manage their own
    for (const [id, group] of ref.entryGroups) {
      if (group.parent === ref.roadGroup) ref.entryGroups.delete(id)
    }

    const asphalt = getAsphaltTextures(roadWear)
    if (terrainMesh) {
      const mesh = meshFromData(terrainMesh.positions, terrainMesh.normals, terrainMesh.colors, terrainMesh.indices, 0.95, terrainMesh.uvs, imageryTexture)
      mesh.userData.outlinerId = 'terrain'
      ref.terrainGroup.add(mesh)
      ref.grid.visible = false
    } else {
      ref.grid.visible = true
    }
    for (const object of roadObjects) {
      let group = ref.entryGroups.get(object.id)
      if (!group) {
        group = new THREE.Group()
        group.userData.outlinerId = object.id
        ref.roadGroup.add(group)
        ref.entryGroups.set(object.id, group)
      }
      // Pavement gets the dark asphalt PBR set. The albedo texture is already
      // dark asphalt grey, so we do NOT multiply with vertex colors — that
      // would make the road nearly black (0.14 × 0.16 = 0.022). Markings get
      // a plain vertex-colored material so white/yellow/green strips stay
      // clean and are not affected by the asphalt texture.
      if (object.surface === 'marking') {
        const mesh = meshFromData(object.mesh.positions, null, object.mesh.colors, object.mesh.indices, 0.6, undefined, null, true)
        mesh.userData.outlinerId = object.id
        group.add(mesh)
      } else {
        // pavement: dark asphalt PBR texture only (no vertex color multiply)
        const mesh = meshFromData(object.mesh.positions, null, object.mesh.colors, object.mesh.indices, 1.0, object.mesh.uvs, asphalt.map, true, asphalt.roughnessMap, asphalt.normalMap, false)
        mesh.userData.outlinerId = object.id
        group.add(mesh)
      }
    }
  }, [terrainMesh, roadObjects, imageryTexture, roadWear])

  // ── Populate OSM building volumes ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    disposeGroup(ref.buildingGroup)
    for (const [id, group] of ref.entryGroups) {
      if (group.parent === ref.buildingGroup) ref.entryGroups.delete(id)
    }
    for (const object of buildingObjects) {
      let group = ref.entryGroups.get(object.id)
      if (!group) {
        group = new THREE.Group()
        group.userData.outlinerId = object.id
        ref.buildingGroup.add(group)
        ref.entryGroups.set(object.id, group)
      }
      const mesh = meshFromData(object.mesh.positions, null, object.mesh.colors, object.mesh.indices, 0.9)
      mesh.userData.outlinerId = object.id
      group.add(mesh)
    }
  }, [buildingObjects])

  // ── Outliner visibility (eye icons + Roads toolbar toggle) ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    ref.roadGroup.visible = !hiddenIds['__ALL_ROADS__']
    ref.terrainGroup.visible = !hiddenIds['terrain']
    ref.simGroup.visible = !hiddenIds['traffic']
    ref.grid.visible = !hiddenIds['helpers'] && !terrainMesh
    for (const [id, group] of ref.entryGroups) {
      group.visible = !hiddenIds[id]
    }
  }, [hiddenIds, terrainMesh, roadObjects, buildingObjects])

  // ── Selection highlight: green emissive silhouette on selected objects ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    const selected = new Set(selectedIds)
    const paint = (root: THREE.Object3D) => {
      root.traverse((child) => {
        if (!(child instanceof THREE.Mesh)) return
        const material = child.material as THREE.MeshStandardMaterial
        if (selected.has((child.userData.outlinerId ?? root.userData.outlinerId) as string)) {
          material.emissive.set(0x2fd57a)
          material.emissiveIntensity = 0.5
        } else {
          material.emissive.set(0x000000)
          material.emissiveIntensity = 1
        }
      })
    }
    paint(ref.terrainGroup)
    paint(ref.roadGroup)
    paint(ref.buildingGroup)
  }, [selectedIds, terrainMesh, roadObjects, buildingObjects])

  // ── Traffic simulation runtime (SCANeR Simulation parity) ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    disposeGroup(ref.simGroup)
    if (!playing || simPaths.length === 0) {
      simRef.current = null
      return
    }
    const vehicles = spawnVehicles(simPaths, vehicleCount)
    simRef.current = { vehicles, paths: simPaths, last: performance.now() }
    const boxes: THREE.Mesh[] = []
    const color = new THREE.Color()
    for (const vehicle of vehicles) {
      const mesh = new THREE.Mesh(CAR_GEOMETRY, new THREE.MeshStandardMaterial({
        color: color.setHSL((vehicle.id * 0.618) % 1, 0.55, 0.5),
        roughness: 0.4,
        metalness: 0.2,
      }))
      ref.simGroup.add(mesh)
      boxes.push(mesh)
    }
    let last = performance.now()
    const timer = window.setInterval(() => {
      const sim = simRef.current
      if (!sim) return
      const now = performance.now()
      const dt = Math.min(0.25, (now - sim.last) / 1000) * simSpeed
      sim.last = now
      stepSimulation(sim.vehicles, sim.paths, dt)
      const poses = simulationPoses(sim.vehicles, sim.paths)
      poses.forEach((pose, i) => {
        const mesh = boxes[i]
        if (!mesh) return
        mesh.position.set(pose.x, ROAD_LIFT + 0.8, -pose.y)
        mesh.rotation.y = -pose.heading
      })
    }, 33)
    return () => {
      window.clearInterval(timer)
      simRef.current = null
    }
  }, [playing, simPaths, vehicleCount, simSpeed])

    // ── Wireframe + height scale ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    ref.terrainGroup.scale.y = heightScale
    ref.roadGroup.scale.y = heightScale
    ref.buildingGroup.scale.y = heightScale
    for (const group of [ref.terrainGroup, ref.roadGroup, ref.buildingGroup]) {
      group.traverse((child) => {
        if (child instanceof THREE.Mesh) (child.material as THREE.MeshStandardMaterial).wireframe = wireframe
      })
    }
  }, [heightScale, wireframe, terrainMesh, roadObjects, buildingObjects])

  // ── Fit camera to content ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    const box = new THREE.Box3()
    let any = false
    const expand = (positions: Float32Array, stride: number) => {
      const p = new THREE.Vector3()
      for (let i = 0; i + 2 < positions.length; i += 3 * stride) {
        p.set(positions[i], positions[i + 1], positions[i + 2])
        if (!Number.isFinite(p.x) || !Number.isFinite(p.y) || !Number.isFinite(p.z)) continue
        box.expandByPoint(p)
        any = true
      }
    }
    if (terrainMesh) expand(terrainMesh.positions, 96)
    for (const object of roadObjects) expand(object.mesh.positions, 64)
    for (const object of buildingObjects) expand(object.mesh.positions, 8)
    if (!any) return

    const center = box.getCenter(new THREE.Vector3())
    const size = box.getSize(new THREE.Vector3())
    const radius = Math.max(20, Math.max(size.x, size.y, size.z) / 2)
    ref.controls.target.copy(center)
    const dir = new THREE.Vector3(0.65, 0.55, 0.75).normalize()
    ref.camera.position.copy(center).addScaledVector(dir, radius * 2.4)
    ref.camera.far = Math.max(20000, radius * 12)
    ref.camera.updateProjectionMatrix()
    ref.controls.update()
  }, [fitSignal, terrainMesh, roadObjects, buildingObjects])

  // ── Focus selected: frame the active outliner object ──
  const selectionRef = useRef(selectedIds)
  selectionRef.current = selectedIds
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    const ids = selectionRef.current
    if (ids.length === 0) return
    const box = new THREE.Box3()
    let any = false
    const expand = (positions: Float32Array, stride: number) => {
      const p = new THREE.Vector3()
      for (let i = 0; i + 2 < positions.length; i += 3 * stride) {
        p.set(positions[i], positions[i + 1], positions[i + 2])
        if (!Number.isFinite(p.x) || !Number.isFinite(p.y) || !Number.isFinite(p.z)) continue
        box.expandByPoint(p)
        any = true
      }
    }
    for (const id of ids) {
      const group = ref.entryGroups.get(id)
      if (!group) continue
      for (const child of group.children) expand((child as THREE.Mesh).geometry.getAttribute('position').array as Float32Array, 8)
    }
    if (!any) return
    const center = box.getCenter(new THREE.Vector3())
    const size = box.getSize(new THREE.Vector3())
    const radius = Math.max(8, Math.max(size.x, size.y, size.z) / 2)
    ref.controls.target.copy(center)
    // keep the current viewing direction, just change distance
    const dir = ref.camera.position.clone().sub(ref.controls.target).normalize()
    if (!Number.isFinite(dir.x) || dir.lengthSq() < 0.5) dir.set(0.65, 0.55, 0.75).normalize()
    ref.camera.position.copy(center).addScaledVector(dir, radius * 3)
    ref.camera.far = Math.max(20000, radius * 12)
    ref.camera.updateProjectionMatrix()
    ref.controls.update()
  }, [focusSignal])

  return (
    <div className="relative flex-1 overflow-hidden bg-[#0b1220]">
      <div ref={containerRef} className="absolute inset-0" />

      {/* Scene outliner */}
      {outlinerOpen && (
        <SceneOutliner
          entries={entries}
          selectedIds={selectedIds}
          hiddenIds={hiddenIds}
          lockedIds={lockedIds}
          onSelect={onSelect}
          onToggleHidden={onToggleHidden}
          onToggleLocked={onToggleLocked}
          onFocus={onFocus}
          onDeleteBuilding={onDeleteBuilding}
          onIsolate={onIsolate}
          onRegenerateBuilding={onRegenerateBuilding}
        />
      )}

      {/* Stats */}
      {hasContent && (
        <div className="pointer-events-none absolute bottom-3 left-3 flex items-center gap-2 rounded-md border border-border bg-card/80 px-2.5 py-1 text-[11px] text-muted-foreground backdrop-blur">
          <span>{roadCount} roads</span>
          {terrainMesh && <span>· terrain {terrainMesh.width}×{terrainMesh.height}</span>}
        </div>
      )}

      {/* Controls hint */}
      {hasContent && (
        <div className="pointer-events-none absolute bottom-3 right-3 rounded-md border border-border bg-card/80 px-2.5 py-1 text-[11px] text-muted-foreground backdrop-blur">
          Drag orbit · Right-drag pan · Scroll zoom · Click select · F focus · Esc clear
        </div>
      )}

      {/* Empty state */}
      {!hasContent && (
        <div className="pointer-events-none absolute inset-0 grid place-items-center">
          <div className="grid gap-4 justify-items-center text-center">
            <div className="grid size-16 place-items-center rounded-2xl border border-border bg-card/60 backdrop-blur">
              <Boxes className="size-8 text-muted-foreground" />
            </div>
            <div className="grid gap-1">
              <h2 className="text-lg font-semibold text-foreground">3D Studio</h2>
              <p className="max-w-sm text-sm text-muted-foreground">
                Nothing to show yet. Draw roads in the Road Editor and download terrain in the Terrain workspace — both appear here together in 3D.
              </p>
            </div>
            <Button size="sm" variant="outline" className="gap-1.5 pointer-events-auto" onClick={onFit}>
              <RotateCcw className="size-3.5" />
              Reset Camera
            </Button>
          </div>
        </div>
      )}
    </div>
  )
}

// ─── Scene Outliner panel ──────────────────────────────────────────

const OUTLINER_CATEGORIES: { kind: OutlinerEntry['kind']; label: string }[] = [
  { kind: 'terrain', label: 'Terrain' },
  { kind: 'road', label: 'Roads' },
  { kind: 'junction', label: 'Junctions' },
  { kind: 'intersection', label: 'Intersections' },
  { kind: 'rail', label: 'Rail fixtures' },
  { kind: 'building', label: 'OSM Buildings' },
  { kind: 'traffic', label: 'Traffic' },
  { kind: 'helpers', label: 'Helpers' },
]

function SceneOutliner({ entries, selectedIds, hiddenIds, lockedIds, onSelect, onToggleHidden, onToggleLocked, onFocus, onDeleteBuilding, onIsolate, onRegenerateBuilding }: {
  entries: OutlinerEntry[]
  selectedIds: string[]
  hiddenIds: Record<string, boolean>
  lockedIds: Record<string, boolean>
  onSelect: (id: string | null, additive: boolean) => void
  onToggleHidden: (id: string) => void
  onToggleLocked: (id: string) => void
  onFocus: () => void
  onDeleteBuilding: (id: string) => void
  onIsolate: (id: string) => void
  onRegenerateBuilding: (id: string) => void
}) {
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({})
  const [showTags, setShowTags] = useState(false)
  const activeId = selectedIds.length > 0 ? selectedIds[selectedIds.length - 1] : null
  const activeEntry = activeId ? entries.find((entry) => entry.id === activeId) : undefined
  const needle = query.trim().toLowerCase()
  const filtered = needle ? entries.filter((entry) => entry.name.toLowerCase().includes(needle)) : entries

  return (
    <div className="absolute right-3 top-3 z-10 flex max-h-[72%] w-64 flex-col overflow-hidden rounded-lg border border-border bg-card/90 text-xs shadow-lg backdrop-blur">
      <div className="flex items-center gap-2 border-b border-border px-2.5 py-2">
        <Search className="size-3.5 shrink-0 text-muted-foreground" />
        <input
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          placeholder="Search scene…"
          className="h-5 w-full bg-transparent text-xs text-foreground outline-none placeholder:text-muted-foreground"
        />
        {selectedIds.length > 0 && (
          <button
            className="shrink-0 text-[11px] text-muted-foreground hover:text-foreground"
            onClick={() => onSelect(null, false)}
            title="Clear selection (Esc)"
          >
            Clear
          </button>
        )}
      </div>
      <div className="overflow-y-auto py-1">
        {OUTLINER_CATEGORIES.map(({ kind, label }) => {
          const items = filtered.filter((entry) => entry.kind === kind)
          if (items.length === 0) return null
          const isCollapsed = !!collapsed[kind]
          return (
            <div key={kind}>
              <button
                className="flex w-full items-center gap-1 px-2.5 py-1.5 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground hover:text-foreground"
                onClick={() => setCollapsed((map) => ({ ...map, [kind]: !map[kind] }))}
              >
                {isCollapsed ? <ChevronRight className="size-3" /> : <ChevronDown className="size-3" />}
                {label}
                <span className="ml-auto font-normal normal-case tabular-nums">{items.length}</span>
              </button>
              {!isCollapsed && items.map((entry) => {
                const isSelected = selectedIds.includes(entry.id)
                const isHidden = !!hiddenIds[entry.id]
                const isLocked = !!lockedIds[entry.id]
                return (
                  <div
                    key={entry.id}
                    className={`group flex items-center gap-1 px-2.5 py-1 ${isSelected ? 'bg-primary/15 text-primary' : 'text-foreground hover:bg-muted/60'} ${isHidden ? 'opacity-50' : ''}`}
                  >
                    <button
                      className="shrink-0 text-muted-foreground hover:text-foreground"
                      title={isHidden ? 'Show object' : 'Hide object'}
                      onClick={() => onToggleHidden(entry.id)}
                    >
                      {isHidden ? <EyeOff className="size-3.5" /> : <Eye className="size-3.5" />}
                    </button>
                    <button
                      className={`min-w-0 flex-1 truncate text-left ${entry.id === activeId ? 'font-semibold' : ''}`}
                      title={entry.name}
                      onClick={(e) => onSelect(entry.id, e.ctrlKey || e.shiftKey)}
                      onDoubleClick={onFocus}
                    >
                      {entry.name}
                    </button>
                    <button
                      className={`shrink-0 hover:text-foreground ${isLocked ? 'text-amber-400' : 'text-muted-foreground opacity-0 group-hover:opacity-100'}`}
                      title={isLocked ? 'Unlock (allow viewport picking)' : 'Lock (exclude from viewport picking)'}
                      onClick={() => onToggleLocked(entry.id)}
                    >
                      {isLocked ? <Lock className="size-3.5" /> : <Unlock className="size-3.5" />}
                    </button>
                  </div>
                )
              })}
            </div>
          )
        })}
        {filtered.length === 0 && (
          <p className="px-3 py-4 text-center text-[11px] text-muted-foreground">No matching objects</p>
        )}
      </div>

      {/* Inspector for the active object (generation info, OSM attributes, actions) */}
      {activeEntry?.tags && (
        <div className="border-t border-border">
          {activeEntry.pcg && (
            <div className="flex items-center justify-between gap-2 border-b border-border px-2.5 py-1.5 text-[10px] text-muted-foreground">
              <span>
                {activeEntry.pcg.mode === 'pcg'
                  ? `PCG · ${activeEntry.pcg.style} · seed ${activeEntry.pcg.seed}${activeEntry.pcg.override ? ` · regen #${activeEntry.pcg.override}` : ''}`
                  : 'Simple extrusion'}
              </span>
              {activeEntry.pcg.mode === 'pcg' && (
                <button
                  className="rounded border border-border px-1.5 py-0.5 text-[10px] text-muted-foreground hover:text-foreground"
                  onClick={() => activeId && onRegenerateBuilding(activeId)}
                  title="Regenerate this building with a new variation"
                >
                  Regenerate
                </button>
              )}
            </div>
          )}
          <button
            className="flex w-full items-center justify-between px-2.5 py-1.5 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground hover:text-foreground"
            onClick={() => setShowTags((v) => !v)}
          >
            OSM Attributes
            <span className="font-normal normal-case">{showTags ? 'hide' : 'show'}</span>
          </button>
          {showTags && (
            <div className="grid max-h-36 gap-0.5 overflow-y-auto px-2.5 pb-2">
              {Object.entries(activeEntry.tags).map(([key, value]) => (
                <div key={key} className="flex items-baseline justify-between gap-2 text-[10px]">
                  <span className="min-w-0 truncate text-muted-foreground" title={key}>{key}</span>
                  <span className="max-w-[55%] truncate text-right text-foreground" title={value}>{value}</span>
                </div>
              ))}
              <div className="mt-1.5 flex gap-1.5">
                <button
                  className="flex-1 rounded border border-border px-1.5 py-0.5 text-[10px] text-muted-foreground hover:text-foreground"
                  onClick={() => activeId && onIsolate(activeId)}
                  title="Hide all other objects"
                >
                  Isolate
                </button>
                <button
                  className="flex-1 rounded border border-destructive/40 px-1.5 py-0.5 text-[10px] text-destructive hover:bg-destructive/10"
                  onClick={() => activeId && onDeleteBuilding(activeId)}
                  title="Delete this building from the project"
                >
                  Delete
                </button>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  )
}

function meshFromData(
  positions: Float32Array,
  normals: Float32Array | null,
  colors: Float32Array,
  indices: Uint32Array,
  roughness: number,
  uvs?: Float32Array,
  map?: THREE.Texture | null,
  /** pull the surface toward the camera so draped roads win the depth test against the terrain */
  draped?: boolean,
  roughnessMap?: THREE.Texture | null,
  normalMap?: THREE.Texture | null,
  /** multiply vertex colors AND the albedo map (roads: markings stay geometry-aligned) */
  vertexColorsWithMap?: boolean,
): THREE.Mesh {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3))
  if (normals) geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3))
  else geometry.computeVertexNormals()
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3))
  if (uvs) geometry.setAttribute('uv', new THREE.BufferAttribute(uvs, 2))
  geometry.setIndex(new THREE.BufferAttribute(indices, 1))
  const material = new THREE.MeshStandardMaterial({
    vertexColors: !map || !!vertexColorsWithMap,
    map: map ?? null,
    roughnessMap: roughnessMap ?? null,
    normalMap: normalMap ?? null,
    normalScale: new THREE.Vector2(0.55, 0.55),
    side: THREE.DoubleSide,
    roughness,
    metalness: 0.0,
    polygonOffset: !!draped,
    polygonOffsetFactor: draped ? -4 : 0,
    polygonOffsetUnits: draped ? -4 : 0,
  })
  return new THREE.Mesh(geometry, material)
}

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    group.remove(child)
    const mesh = child as THREE.Mesh
    mesh.geometry?.dispose()
    if (mesh.material) (mesh.material as THREE.Material).dispose()
  }
}
