import { useEffect, useMemo, useRef, useState } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import { Map as MapLibreMap, setWorkerUrl } from 'maplibre-gl'
import 'maplibre-gl/dist/maplibre-gl.css'
import maplibreWorkerUrl from 'maplibre-gl/dist/maplibre-gl-worker.mjs?url'
import { Boxes, Layers, Map as MapIcon, Mountain, Route, Save } from 'lucide-react'
import { toast } from 'sonner'

import { buildJunctionNetwork, visibleRoadRanges } from '../engine/junctions'
import { buildConnectingRoadMesh, buildRoadMeshRange } from '../engine/mesh'
import { evaluateElevation, normalizeElevationProfile } from '../engine/elevation'
import { fitRoadGeometry } from '../engine/roadGeometry'
import type { MeshData } from '../engine/mesh'
import { useStore, getLaneSection } from '../state/store'
import AppHeader from '@/components/layout/AppHeader'
import { Button } from '@/components/ui/button'
import { Badge } from '@/components/ui/badge'
import { Separator } from '@/components/ui/separator'

// MapLibre worker URL (same fix as TerrainMap)
setWorkerUrl(new URL(maplibreWorkerUrl, import.meta.url).href)

interface Studio3DPageProps {
  onBack: () => void
}

function roadSection(road: { laneSection?: { left: unknown[]; right: unknown[] }; lanesLeft: number; lanesRight: number; laneWidth: number }) {
  return getLaneSection(road as { laneSection?: never; lanesLeft: number; lanesRight: number; laneWidth: number })
}

// Esri World Imagery raster style (same as TerrainMap)
const ESRI_STYLE = {
  version: 8,
  sources: {
    esri: {
      type: 'raster' as const,
      tiles: ['https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}'],
      tileSize: 256,
      attribution: 'Esri',
      maxzoom: 19,
    },
  },
  layers: [{ id: 'esri', type: 'raster' as const, source: 'esri' }],
}

export default function Studio3DPage({ onBack }: Studio3DPageProps) {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const saveCurrentProject = useStore((s) => s.saveCurrentProject)
  const project = projects.find((p) => p.id === activeProjectId)

  const [heightScale, setHeightScale] = useState(1)
  const [showRoads, setShowRoads] = useState(true)
  const [showMap, setShowMap] = useState(true)
  const [saving, setSaving] = useState(false)

  // ── Build road meshes from project data ──
  const roadMeshes = useMemo<MeshData[]>(() => {
    if (!project || project.roads.length === 0) return []
    const elevationSamplers = new Map<string, (s: number) => number>()
    for (const road of project.roads) {
      const path = fitRoadGeometry(road)
      const length = path?.length ?? 0
      const profile = normalizeElevationProfile(road.elevationProfile, length)
      elevationSamplers.set(road.id, (s) => evaluateElevation(profile, s))
    }
    const junctionNetwork = buildJunctionNetwork(project.roads, project.suppressedJunctions, elevationSamplers)
    if (!junctionNetwork) return []
    const meshes: MeshData[] = []
    for (const road of project.roads) {
      const path = junctionNetwork.paths.get(road.id)
      if (!path) continue
      const section = roadSection(road)
      const cuts = junctionNetwork.cuts.filter((cut) => cut.roadId === road.id)
      for (const range of visibleRoadRanges(path, cuts)) {
        const mesh = buildRoadMeshRange(path, section, range.sStart, range.sEnd, 1, elevationSamplers.get(road.id))
        if (mesh) meshes.push(mesh)
      }
    }
    const activeJunctions = junctionNetwork.junctions.filter((j) => !j.suppressed)
    for (const junction of activeJunctions) {
      for (const connection of junction.connectingRoads) {
        const mesh = buildConnectingRoadMesh(connection.samples, connection.laneCount, connection.laneWidth)
        if (mesh) meshes.push(mesh)
      }
    }
    return meshes
  }, [project])

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

  return (
    <div className="flex h-screen flex-col bg-background">
      <AppHeader projectName={project?.name ?? 'Unknown'} subtitle="3D Studio" onBack={onBack}>
        <Badge variant="outline" className="gap-1">
          <Route className="size-3" />
          {project?.roads.length ?? 0} road{(project?.roads.length ?? 0) === 1 ? '' : 's'}
        </Badge>
        <Separator orientation="vertical" className="h-5" />
        <Button size="sm" variant="secondary" onClick={handleSave} disabled={saving}>
          <Save className="size-4" />
          {saving ? 'Saving…' : 'Save Project'}
        </Button>
      </AppHeader>

      {/* Toolbar */}
      <div className="flex h-10 shrink-0 items-center gap-2 border-b border-border bg-card/50 px-3 text-xs">
        <button
          className={`flex items-center gap-1.5 rounded-md px-2.5 py-1 font-medium transition-colors ${showRoads ? 'bg-primary/15 text-primary' : 'text-muted-foreground hover:bg-muted'}`}
          onClick={() => setShowRoads((v) => !v)}
        >
          <Route className="size-3.5" />
          Roads
        </button>
        <button
          className={`flex items-center gap-1.5 rounded-md px-2.5 py-1 font-medium transition-colors ${showMap ? 'bg-primary/15 text-primary' : 'text-muted-foreground hover:bg-muted'}`}
          onClick={() => setShowMap((v) => !v)}
        >
          <MapIcon className="size-3.5" />
          Map
        </button>
        <Separator orientation="vertical" className="h-5" />
        <label className="flex items-center gap-2 text-muted-foreground">
          Height Scale
          <input
            type="range"
            min={0.1}
            max={10}
            step={0.1}
            value={heightScale}
            onChange={(e) => setHeightScale(parseFloat(e.target.value))}
            className="h-1 w-24 cursor-pointer accent-primary"
          />
          <span className="w-8 tabular-nums text-foreground">{heightScale.toFixed(1)}x</span>
        </label>
      </div>

      {/* 3D Viewport with map background */}
      <Studio3DViewport
        roadMeshes={showRoads ? roadMeshes : []}
        heightScale={heightScale}
        hasRoads={roadMeshes.length > 0}
        showMap={showMap}
      />
    </div>
  )
}

// ─── 3D Viewport with MapLibre background ──────────────────────

interface Studio3DViewportProps {
  roadMeshes: MeshData[]
  heightScale: number
  hasRoads: boolean
  showMap: boolean
}

function Studio3DViewport({ roadMeshes, heightScale, hasRoads, showMap }: Studio3DViewportProps) {
  const mapContainerRef = useRef<HTMLDivElement>(null)
  const canvasContainerRef = useRef<HTMLDivElement>(null)
  const mapRef = useRef<MapLibreMap | null>(null)
  const roadGroupRef = useRef<THREE.Group | null>(null)

  // ── MapLibre background map ──
  useEffect(() => {
    const container = mapContainerRef.current
    if (!container || !showMap) return

    const map = new MapLibreMap({
      container,
      style: ESRI_STYLE as any,
      center: [0, 0],
      zoom: 1,
      attributionControl: false,
      dragPan: true,
      scrollZoom: true,
      doubleClickZoom: false,
    })
    mapRef.current = map

    return () => {
      map.remove()
      mapRef.current = null
    }
  }, [showMap])

  // ── Three.js 3D scene ──
  useEffect(() => {
    const container = canvasContainerRef.current
    if (!container) return

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true })
    renderer.setPixelRatio(window.devicePixelRatio)
    renderer.setSize(container.clientWidth, container.clientHeight)
    renderer.setClearColor(0x000000, 0) // transparent so map shows through
    renderer.domElement.style.display = 'block'
    renderer.domElement.style.pointerEvents = 'none' // let map handle pan/zoom
    container.appendChild(renderer.domElement)

    const scene = new THREE.Scene()
    scene.fog = new THREE.Fog(0x0a0e14, 1000, 4000)

    const aspect = container.clientWidth / Math.max(1, container.clientHeight)
    const camera = new THREE.PerspectiveCamera(55, aspect, 0.1, 50000)
    camera.position.set(120, 150, 200)
    camera.lookAt(0, 0, 0)

    // Lighting
    const ambient = new THREE.AmbientLight(0x8090a0, 0.7)
    const directional = new THREE.DirectionalLight(0xffffff, 1.0)
    directional.position.set(200, 400, 150)
    scene.add(ambient, directional)

    // Grid (semi-transparent, only visible when map is off)
    const grid = new THREE.GridHelper(2000, 100, 0x1e2a3f, 0x121b2c)
    ;(grid.material as THREE.Material).transparent = true
    ;(grid.material as THREE.Material).opacity = 0.4
    scene.add(grid)

    // Road group
    const roadGroup = new THREE.Group()
    scene.add(roadGroup)
    roadGroupRef.current = roadGroup

    // Controls (only rotate/zoom the 3D camera, panning is handled by the map)
    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.08
    controls.maxPolarAngle = Math.PI / 2 - 0.05
    controls.enablePan = false
    // Re-enable pointer events only for rotate (right-click) and zoom (scroll)
    renderer.domElement.style.pointerEvents = 'auto'

    function resize() {
      const w = container.clientWidth
      const h = container.clientHeight
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
      disposeGroup(roadGroup)
      renderer.dispose()
      renderer.domElement.remove()
    }
  }, [])

  // Update road meshes
  useEffect(() => {
    const group = roadGroupRef.current
    if (!group) return
    disposeGroup(group)
    for (const mesh of roadMeshes) {
      const geometry = new THREE.BufferGeometry()
      geometry.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3))
      geometry.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 3))
      geometry.setIndex(new THREE.BufferAttribute(mesh.indices, 1))
      geometry.computeVertexNormals()
      const material = new THREE.MeshStandardMaterial({
        vertexColors: true,
        side: THREE.DoubleSide,
        roughness: 0.85,
        metalness: 0.0,
      })
      const threeMesh = new THREE.Mesh(geometry, material)
      threeMesh.scale.y = heightScale
      group.add(threeMesh)
    }
  }, [roadMeshes, heightScale])

  return (
    <div className="relative flex-1 overflow-hidden bg-[#0a0e14]">
      {/* MapLibre background */}
      {showMap && (
        <div ref={mapContainerRef} className="absolute inset-0" />
      )}

      {/* Three.js canvas overlay (transparent) */}
      <div ref={canvasContainerRef} className="absolute inset-0" />

      {/* Empty state */}
      {!hasRoads && (
        <div className="pointer-events-none absolute inset-0 grid place-items-center">
          <div className="grid gap-4 justify-items-center text-center">
            <div className="grid size-16 place-items-center rounded-2xl border border-border bg-card/60 backdrop-blur">
              <Boxes className="size-8 text-muted-foreground" />
            </div>
            <div className="grid gap-1">
              <h2 className="text-lg font-semibold text-foreground">3D Studio</h2>
              <p className="max-w-sm text-sm text-muted-foreground">
                No roads in this project yet. Create roads in the Road Editor, then open 3D Studio to see them in 3D.
              </p>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    group.remove(child)
    child.geometry?.dispose()
    if (child.material) (child.material as THREE.Material).dispose()
  }
}
