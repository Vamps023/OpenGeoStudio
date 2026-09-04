import { useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { Boxes, Maximize, Mountain, Route, Save, RotateCcw } from 'lucide-react'
import { toast } from 'sonner'

import { buildJunctionNetwork, visibleRoadRanges } from '../engine/junctions'
import { buildConnectingRoadMesh, buildRoadMeshRange } from '../engine/mesh'
import { buildTerrainMeshWorld, type TerrainMeshData } from '../engine/terrainMesh'
import { allWays, resolveTracks } from '../engine/intersections'
import { evaluateElevation, normalizeElevationProfile } from '../engine/elevation'
import { fitRoadGeometry } from '../engine/roadGeometry'
import { stickTrackToTerrain } from '../engine/tracks'
import { sectionHalfWidth } from '../engine/laneLayout'
import { makeTerrainSampler, getActiveTerrain } from '../terrain/terrainRegistry'
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

/** Lift roads slightly above the terrain surface to avoid z-fighting. */
const ROAD_LIFT = 0.2

// ─── Scene content built from project + registry terrain ───────────

/** Build all road meshes (roads + junction connectors + intersection ways) in world space. */
function buildProjectRoadMeshes(project: Project, drape: boolean): MeshData[] {
  if (project.roads.length === 0) return []

  // Drape roads that have no elevation profile yet onto the background terrain
  const sampler = makeTerrainSampler(project.geoRef)
  const effective = project.roads.map((road) => {
    if (!drape || road.elevationProfile?.length) return road
    const result = stickTrackToTerrain({ ...road, elevationProfile: undefined }, sampler, sectionHalfWidth(getLaneSection(road)))
    if (!result) return road
    return { ...road, elevationProfile: result.elevation, bankingProfile: result.banking }
  })

  const elevationSamplers = new Map<string, (s: number) => number>()
  const bankingSamplers = new Map<string, (s: number) => number>()
  for (const road of effective) {
    const path = fitRoadGeometry(road)
    const length = path?.length ?? 0
    const elevation = normalizeElevationProfile(road.elevationProfile, length)
    const banking = normalizeElevationProfile(road.bankingProfile, length)
    elevationSamplers.set(road.id, (s) => evaluateElevation(elevation, s) + ROAD_LIFT)
    bankingSamplers.set(road.id, (s) => evaluateElevation(banking, s))
  }

  const junctionNetwork = buildJunctionNetwork(effective, project.suppressedJunctions, elevationSamplers)
  if (!junctionNetwork) return []

  const meshes: MeshData[] = []
  for (const road of effective) {
    const path = junctionNetwork.paths.get(road.id)
    if (!path) continue
    const section = roadSection(road)
    const cuts = junctionNetwork.cuts.filter((cut) => cut.roadId === road.id)
    for (const range of visibleRoadRanges(path, cuts)) {
      const mesh = buildRoadMeshRange(path, section, range.sStart, range.sEnd, 1, elevationSamplers.get(road.id), bankingSamplers.get(road.id), road.tapers)
      if (mesh) meshes.push(mesh)
    }
  }

  for (const junction of junctionNetwork.junctions.filter((j) => !j.suppressed)) {
    for (const connection of junction.connectingRoads) {
      const mesh = buildConnectingRoadMesh(connection.samples, connection.laneCount, connection.laneWidth)
      if (mesh) meshes.push(mesh)
    }
  }

  // explicit intersections render their ways just like auto junctions
  const resolved = resolveTracks(effective)
  for (const intersection of project.intersections ?? []) {
    if (intersection.trackEnds.length < 2) continue
    for (const way of allWays(intersection, resolved)) {
      const mesh = buildConnectingRoadMesh(way.samples, Math.max(1, way.laneCount), way.laneWidth)
      if (mesh) meshes.push(mesh)
    }
  }
  return meshes
}

// ─── Page ──────────────────────────────────────────────────────────

export default function Studio3DPage({ onBack }: Studio3DPageProps) {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const saveCurrentProject = useStore((s) => s.saveCurrentProject)
  const project = projects.find((p) => p.id === activeProjectId)

  const [heightScale, setHeightScale] = useState(1)
  const [showRoads, setShowRoads] = useState(true)
  const [showTerrain, setShowTerrain] = useState(true)
  const [wireframe, setWireframe] = useState(false)
  const [drape, setDrape] = useState(true)
  const [fitSignal, setFitSignal] = useState(0)
  const [saving, setSaving] = useState(false)

  const terrain = useMemo(() => getActiveTerrain(), [])
  const terrainMesh = useMemo(
    () => (terrain ? buildTerrainMeshWorld(terrain, project?.geoRef) : null),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [terrain],
  )
  const roadMeshes = useMemo(
    () => (project ? buildProjectRoadMeshes(project, drape) : []),
    [project, drape],
  )

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

  const hasContent = roadMeshes.length > 0 || !!terrainMesh

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
        <ToolbarToggle active={wireframe} onClick={() => setWireframe((v) => !v)} icon={<Boxes className="size-3.5" />} label="Wireframe" />
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
        <Button size="sm" variant="ghost" className="h-7 gap-1.5 px-2 text-xs" onClick={() => setFitSignal((v) => v + 1)}>
          <Maximize className="size-3.5" />
          Fit View
        </Button>
        {!terrain && (
          <span className="ml-auto text-[11px] text-muted-foreground">
            No background terrain — download terrain in the Terrain workspace to see it here.
          </span>
        )}
      </div>

      <Studio3DViewport
        roadMeshes={showRoads ? roadMeshes : []}
        terrainMesh={showTerrain ? terrainMesh : null}
        heightScale={heightScale}
        wireframe={wireframe}
        fitSignal={fitSignal}
        hasContent={hasContent}
        roadCount={project?.roads.length ?? 0}
        onFit={() => setFitSignal((v) => v + 1)}
      />
    </div>
  )
}

function ToolbarToggle({ active, onClick, icon, label }: { active: boolean; onClick: () => void; icon: ReactNode; label: string }) {
  return (
    <button
      className={`flex items-center gap-1.5 rounded-md px-2.5 py-1 font-medium transition-colors ${active ? 'bg-primary/15 text-primary' : 'text-muted-foreground hover:bg-muted'}`}
      onClick={onClick}
    >
      {icon}
      {label}
    </button>
  )
}

// ─── 3D Viewport ───────────────────────────────────────────────────

interface Studio3DViewportProps {
  roadMeshes: MeshData[]
  terrainMesh: TerrainMeshData | null
  heightScale: number
  wireframe: boolean
  fitSignal: number
  hasContent: boolean
  roadCount: number
  onFit: () => void
}

function Studio3DViewport({ roadMeshes, terrainMesh, heightScale, wireframe, fitSignal, hasContent, roadCount, onFit }: Studio3DViewportProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const sceneRef = useRef<{
    scene: THREE.Scene
    camera: THREE.PerspectiveCamera
    controls: OrbitControls
    terrainGroup: THREE.Group
    roadGroup: THREE.Group
    grid: THREE.GridHelper
    renderer: THREE.WebGLRenderer
  } | null>(null)

  // ── One-time scene setup ──
  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setPixelRatio(window.devicePixelRatio)
    renderer.setSize(container.clientWidth, container.clientHeight)
    renderer.domElement.style.display = 'block'
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
    scene.add(terrainGroup, roadGroup)

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.08
    controls.maxPolarAngle = Math.PI / 2 - 0.02
    controls.screenSpacePanning = false

    sceneRef.current = { scene, camera, controls, terrainGroup, roadGroup, grid, renderer }

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
      renderer.dispose()
      renderer.domElement.remove()
      sceneRef.current = null
    }
  }, [])

  // ── Populate terrain + road meshes ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    disposeGroup(ref.terrainGroup)
    disposeGroup(ref.roadGroup)

    if (terrainMesh) {
      ref.terrainGroup.add(meshFromData(terrainMesh.positions, terrainMesh.normals, terrainMesh.colors, terrainMesh.indices, 0.95))
      ref.grid.visible = false
    } else {
      ref.grid.visible = true
    }
    for (const mesh of roadMeshes) {
      ref.roadGroup.add(meshFromData(mesh.positions, null, mesh.colors, mesh.indices, 0.85))
    }
  }, [terrainMesh, roadMeshes])

  // ── Wireframe + height scale ──
  useEffect(() => {
    const ref = sceneRef.current
    if (!ref) return
    ref.terrainGroup.scale.y = heightScale
    ref.roadGroup.scale.y = heightScale
    for (const group of [ref.terrainGroup, ref.roadGroup]) {
      for (const child of group.children) {
        ((child as THREE.Mesh).material as THREE.MeshStandardMaterial).wireframe = wireframe
      }
    }
  }, [heightScale, wireframe, terrainMesh, roadMeshes])

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
    for (const mesh of roadMeshes) expand(mesh.positions, 64)
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
  }, [fitSignal, terrainMesh, roadMeshes])

  return (
    <div className="relative flex-1 overflow-hidden bg-[#0b1220]">
      <div ref={containerRef} className="absolute inset-0" />

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
          Drag orbit · Right-drag pan · Scroll zoom
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

function meshFromData(
  positions: Float32Array,
  normals: Float32Array | null,
  colors: Float32Array,
  indices: Uint32Array,
  roughness: number,
): THREE.Mesh {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3))
  if (normals) geometry.setAttribute('normal', new THREE.BufferAttribute(normals, 3))
  else geometry.computeVertexNormals()
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3))
  geometry.setIndex(new THREE.BufferAttribute(indices, 1))
  const material = new THREE.MeshStandardMaterial({
    vertexColors: true,
    side: THREE.DoubleSide,
    roughness,
    metalness: 0.0,
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
