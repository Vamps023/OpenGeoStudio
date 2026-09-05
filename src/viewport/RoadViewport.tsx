import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import type { MeshData } from '../engine/mesh'
import type { Vec2 } from '../engine/types'

export interface OverlayLine {
  points: Vec2[]
  color: string
  width?: number    // meters (ribbon), default 0.4
  dashed?: boolean
  wayKey?: string   // enables double-click passageway locking
  opacity?: number
}

export interface OverlayMarker {
  id?: string
  point: Vec2
  color: string
  shape: 'circle' | 'ring' | 'arrow' | 'square'
  heading?: number
  size?: number     // meters
  draggable?: boolean
}

export interface ViewportOverlays {
  lines: OverlayLine[]
  markers: OverlayMarker[]
}

interface RoadViewportProps {
  meshes: MeshData[]
  highlightMeshes: MeshData[]
  draftMesh: MeshData | null
  draftPoints: Vec2[]
  interaction: 'drag' | 'click'
  onDragStart: (point: Vec2) => void
  onDragMove: (point: Vec2) => void
  onDragEnd: (point: Vec2) => void
  onDragCancel: () => void
  onGroundClick: (point: Vec2, event: PointerEvent) => void
  onGroundHover: (point: Vec2) => void
  onContextMenu: (point: Vec2, screen: { x: number; y: number }) => void
  onMarkerDrag: (id: string, point: Vec2) => void
  onMarkerDragEnd: (id: string, point: Vec2) => void
  onWayDoubleClick: (wayKey: string) => void
  overlays: ViewportOverlays
  mode: '2d' | '3d'
  hint: string
  showMap: boolean
  mapCenter: { lng: number; lat: number }
  mapScale: number // meters per world unit
}

interface ViewRefs {
  applyMode: (mode: '2d' | '3d') => void
  roadGroup: THREE.Group
  highlightGroup: THREE.Group
  markerGroup: THREE.Group
  overlayGroup: THREE.Group
  pickables: () => THREE.Object3D[]
}

// ─── Slippy tile math ─────────────────────────────────────────
// Standard Web Mercator tile calculations

function lngLatToTile(lng: number, lat: number, zoom: number): { x: number; y: number } {
  const n = Math.pow(2, zoom)
  const x = ((lng + 180) / 360) * n
  const latRad = (lat * Math.PI) / 180
  const y = ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2) * n
  return { x, y }
}

function tileToLngLat(x: number, y: number, zoom: number): { lng: number; lat: number } {
  const n = Math.pow(2, zoom)
  const lng = (x / n) * 360 - 180
  const latRad = Math.atan(Math.sinh(Math.PI * (1 - (2 * y) / n)))
  const lat = (latRad * 180) / Math.PI
  return { lng, lat }
}

function metersPerPixel(lat: number, zoom: number): number {
  return (156543.03392 * Math.cos((lat * Math.PI) / 180)) / Math.pow(2, zoom)
}

// World (meters offset from center) → lng/lat
function worldToGeo(x: number, y: number, centerLng: number, centerLat: number, scale: number): [number, number] {
  const latRad = (centerLat * Math.PI) / 180
  const metersPerDegLat = 111320
  const metersPerDegLng = 111320 * Math.cos(latRad)
  const lat = centerLat + (y * scale) / metersPerDegLat
  const lng = centerLng + (x * scale) / metersPerDegLng
  return [lng, lat]
}

// lng/lat → world (meters offset from center)
function geoToWorld(lng: number, lat: number, centerLng: number, centerLat: number, scale: number): Vec2 {
  const latRad = (centerLat * Math.PI) / 180
  const metersPerDegLat = 111320
  const metersPerDegLng = 111320 * Math.cos(latRad)
  const x = ((lng - centerLng) * metersPerDegLng) / scale
  const y = ((lat - centerLat) * metersPerDegLat) / scale
  return { x, y }
}

const TILE_SIZE = 256
const ESRI_TILE_URL = 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile'

export default function RoadViewport({
  meshes,
  highlightMeshes,
  draftMesh,
  draftPoints,
  interaction,
  onDragStart,
  onDragMove,
  onDragEnd,
  onDragCancel,
  onGroundClick,
  onGroundHover,
  onContextMenu,
  onMarkerDrag,
  onMarkerDragEnd,
  onWayDoubleClick,
  overlays,
  mode,
  hint,
  showMap,
  mapCenter,
  mapScale,
}: RoadViewportProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const refsRef = useRef<ViewRefs | null>(null)
  const showMapRef = useRef(showMap)
  showMapRef.current = showMap
  const mapCenterRef = useRef(mapCenter)
  mapCenterRef.current = mapCenter
  const mapScaleRef = useRef(mapScale)
  mapScaleRef.current = mapScale
  const modeRef = useRef(mode)
  modeRef.current = mode
  const handlersRef = useRef({
    interaction, onDragStart, onDragMove, onDragEnd, onDragCancel, onGroundClick, onGroundHover,
    onContextMenu, onMarkerDrag, onMarkerDragEnd, onWayDoubleClick,
  })
  handlersRef.current = {
    interaction, onDragStart, onDragMove, onDragEnd, onDragCancel, onGroundClick, onGroundHover,
    onContextMenu, onMarkerDrag, onMarkerDragEnd, onWayDoubleClick,
  }
  const overlaysRef = useRef(overlays)
  overlaysRef.current = overlays

  useEffect(() => {
    const container = containerRef.current
    if (!container) return

    // ── Three.js setup ──
    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setPixelRatio(window.devicePixelRatio)
    renderer.setSize(container.clientWidth, container.clientHeight)
    renderer.setClearColor(0x0b1220, 1)
    renderer.domElement.style.display = 'block'
    const suppressMenu = (e: MouseEvent) => e.preventDefault()
    renderer.domElement.addEventListener('contextmenu', suppressMenu)
    container.appendChild(renderer.domElement)

    const scene = new THREE.Scene()
    const aspect = container.clientWidth / Math.max(1, container.clientHeight)
    const viewSize = 200
    const ortho = new THREE.OrthographicCamera(
      (-viewSize * aspect) / 2,
      (viewSize * aspect) / 2,
      viewSize / 2,
      -viewSize / 2,
      0.1,
      5000,
    )
    ortho.position.set(0, 500, 0)
    ortho.up.set(0, 0, -1)
    ortho.lookAt(0, 0, 0)
    const persp = new THREE.PerspectiveCamera(55, aspect, 0.1, 10000)
    persp.position.set(80, 90, 120)
    persp.lookAt(0, 0, 0)

    const grid = new THREE.GridHelper(2000, 200, 0x1e2a3f, 0x121b2c)
    ;(grid.material as THREE.Material).transparent = true
    ;(grid.material as THREE.Material).opacity = 0.3
    scene.add(grid)

    // ── Map background plane (textured with composited tiles) ──
    let mapTexture: THREE.CanvasTexture | null = null
    let mapCanvas = document.createElement('canvas')
    mapCanvas.width = 256
    mapCanvas.height = 256
    const mapMaterial = new THREE.MeshBasicMaterial({ transparent: true, opacity: 1, depthWrite: false })
    const mapGeometry = new THREE.PlaneGeometry(1, 1)
    const mapMesh = new THREE.Mesh(mapGeometry, mapMaterial)
    mapMesh.rotation.x = -Math.PI / 2 // lay flat on XZ plane
    mapMesh.position.y = -0.05 // slightly below roads
    mapMesh.renderOrder = -1
    mapMesh.visible = false
    scene.add(mapMesh)

    const roadGroup = new THREE.Group()
    const highlightGroup = new THREE.Group()
    const markerGroup = new THREE.Group()
    const overlayGroup = new THREE.Group()
    scene.add(roadGroup, highlightGroup, markerGroup, overlayGroup)

    let controls: OrbitControls | null = null
    let activeCamera: THREE.Camera = ortho
    let activeMode: '2d' | '3d' = '2d'

    function attach(camera: THREE.Camera, is2d: boolean) {
      controls?.dispose()
      controls = new OrbitControls(camera, renderer.domElement)
      controls.enableRotate = !is2d
      controls.mouseButtons = is2d
        ? { LEFT: null, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN }
        : { LEFT: null, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.ROTATE }
      controls.update()
      activeCamera = camera
    }
    attach(ortho, true)
    roadGroup.position.y = 480 // 2D plan view: ignore road elevation

    // ── Tile fetching + compositing ──
    const tileCache = new Map<string, HTMLImageElement>()
    const pendingTiles = new Set<string>()
    let compositeDirty = false
    let lastTileKey = ''

    function fetchTile(zoom: number, tx: number, ty: number): Promise<HTMLImageElement | null> {
      const key = `${zoom}/${tx}/${ty}`
      if (tileCache.has(key)) return Promise.resolve(tileCache.get(key)!)
      if (pendingTiles.has(key)) return Promise.resolve(null)
      pendingTiles.add(key)

      const url = `${ESRI_TILE_URL}/${zoom}/${ty}/${tx}`
      return new Promise((resolve) => {
        const img = new Image()
        img.crossOrigin = 'anonymous'
        img.onload = () => {
          tileCache.set(key, img)
          pendingTiles.delete(key)
          compositeDirty = true
          resolve(img)
        }
        img.onerror = () => {
          pendingTiles.delete(key)
          resolve(null)
        }
        img.src = url
      })
    }

    function updateMapTiles() {
      if (!showMapRef.current || activeMode !== '2d') return
      const containerW = container!.clientWidth
      const containerH = container!.clientHeight
      if (!containerW || !containerH) return

      const center = mapCenterRef.current
      const scale = mapScaleRef.current || 1
      const target = controls ? controls.target : new THREE.Vector3(0, 0, 0)

      // Camera target in world coords
      const worldCX = target.x
      const worldCY = -target.z // y = -z in our setup

      // Visible world extent
      const zoom = ortho.zoom || 1
      const visW = (viewSize * (containerW / containerH)) / zoom
      const visH = viewSize / zoom

      // World bounds
      const worldLeft = worldCX - visW / 2
      const worldRight = worldCX + visW / 2
      const worldBottom = worldCY - visH / 2
      const worldTop = worldCY + visH / 2

      // Convert to geo
      const [lngL, latB] = worldToGeo(worldLeft, worldBottom, center.lng, center.lat, scale)
      const [lngR, latT] = worldToGeo(worldRight, worldTop, center.lng, center.lat, scale)

      // Determine slippy zoom level
      const centerLat = (latB + latT) / 2
      const mpp = metersPerPixel(centerLat, 10) // base
      const targetMpp = (visH * scale) / containerH
      let slippyZoom = Math.log2(mpp / targetMpp) + 10
      slippyZoom = Math.max(2, Math.min(18, Math.round(slippyZoom)))

      // Get tile range
      const tl = lngLatToTile(lngL, latT, slippyZoom)
      const tr = lngLatToTile(lngR, latB, slippyZoom)
      const txMin = Math.floor(tl.x)
      const txMax = Math.floor(tr.x)
      const tyMin = Math.floor(tl.y)
      const tyMax = Math.floor(tr.y)

      const tileKey = `${slippyZoom}/${txMin}/${tyMin}/${txMax}/${tyMax}`
      if (tileKey === lastTileKey && !compositeDirty) return
      lastTileKey = tileKey

      // Fetch all visible tiles
      const tilesToFetch: { z: number; x: number; y: number }[] = []
      for (let tx = txMin; tx <= txMax; tx++) {
        for (let ty = tyMin; ty <= tyMax; ty++) {
          tilesToFetch.push({ z: slippyZoom, x: tx, y: ty })
        }
      }

      // Fetch and composite
      Promise.all(tilesToFetch.map((t) => fetchTile(t.z, t.x, t.y))).then(() => {
        compositeTiles(slippyZoom, txMin, txMax, tyMin, tyMax, center, scale)
      })
    }

    function compositeTiles(
      zoom: number,
      txMin: number,
      txMax: number,
      tyMin: number,
      tyMax: number,
      center: { lng: number; lat: number },
      scale: number,
    ) {
      const cols = txMax - txMin + 1
      const rows = tyMax - tyMin + 1
      let canvasW = cols * TILE_SIZE
      let canvasH = rows * TILE_SIZE

      // Cap canvas size to avoid WebGL texture limits (max 4096 on most GPUs)
      const MAX_TEX = 2048
      let scaleDown = 1
      if (canvasW > MAX_TEX || canvasH > MAX_TEX) {
        scaleDown = Math.max(canvasW, canvasH) / MAX_TEX
        canvasW = Math.floor(canvasW / scaleDown)
        canvasH = Math.floor(canvasH / scaleDown)
      }

      // Create a fresh canvas + texture each time to avoid glCopySubTexture overflow
      const oldTexture = mapTexture
      mapCanvas = document.createElement('canvas')
      mapCanvas.width = canvasW
      mapCanvas.height = canvasH
      const ctx = mapCanvas.getContext('2d')!
      ctx.fillStyle = '#0b1220'
      ctx.fillRect(0, 0, canvasW, canvasH)

      // Draw tiles
      const tileDrawW = TILE_SIZE / scaleDown
      const tileDrawH = TILE_SIZE / scaleDown
      for (let tx = txMin; tx <= txMax; tx++) {
        for (let ty = tyMin; ty <= tyMax; ty++) {
          const key = `${zoom}/${tx}/${ty}`
          const img = tileCache.get(key)
          if (img) {
            const dx = (tx - txMin) * tileDrawW
            const dy = (ty - tyMin) * tileDrawH
            ctx.drawImage(img, dx, dy, tileDrawW, tileDrawH)
          }
        }
      }

      // Create new texture from the fresh canvas
      mapTexture = new THREE.CanvasTexture(mapCanvas)
      mapTexture.colorSpace = THREE.SRGBColorSpace
      mapTexture.needsUpdate = true
      mapMaterial.map = mapTexture
      mapMaterial.needsUpdate = true

      // Dispose old texture
      if (oldTexture) oldTexture.dispose()

      // Compute world bounds of the composited tile area
      const tlGeo = tileToLngLat(txMin, tyMin, zoom)
      const brGeo = tileToLngLat(txMax + 1, tyMax + 1, zoom)
      const tlWorld = geoToWorld(tlGeo.lng, tlGeo.lat, center.lng, center.lat, scale)
      const brWorld = geoToWorld(brGeo.lng, brGeo.lat, center.lng, center.lat, scale)

      // Position the plane in world coordinates
      const worldMinX = Math.min(tlWorld.x, brWorld.x)
      const worldMaxX = Math.max(tlWorld.x, brWorld.x)
      const worldMinY = Math.min(tlWorld.y, brWorld.y)
      const worldMaxY = Math.max(tlWorld.y, brWorld.y)
      const cx = (worldMinX + worldMaxX) / 2
      const cz = -(worldMinY + worldMaxY) / 2
      const w = worldMaxX - worldMinX
      const h = worldMaxY - worldMinY

      mapMesh.position.set(cx, -0.05, cz)
      mapMesh.scale.set(w, h, 1)
      mapMesh.visible = true

      compositeDirty = false
    }

    function resize() {
      const width = container!.clientWidth
      const height = container!.clientHeight
      if (!width || !height) return
      renderer.setSize(width, height)
      const nextAspect = width / height
      ortho.left = (-viewSize * nextAspect) / 2
      ortho.right = (viewSize * nextAspect) / 2
      ortho.top = viewSize / 2
      ortho.bottom = -viewSize / 2
      ortho.updateProjectionMatrix()
      persp.aspect = nextAspect
      persp.updateProjectionMatrix()
      lastTileKey = '' // force tile refresh
    }
    const observer = new ResizeObserver(resize)
    observer.observe(container)

    // ── Interaction ──
    const raycaster = new THREE.Raycaster()
    const groundPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0)
    const hitPoint = new THREE.Vector3()
    let activePointer: number | null = null
    let draggingMarkerId: string | null = null
    const dragMovedRef = { moved: false }

    function groundPoint(event: { clientX: number; clientY: number }): Vec2 | null {
      const rect = renderer.domElement.getBoundingClientRect()
      const ndc = new THREE.Vector2(
        ((event.clientX - rect.left) / rect.width) * 2 - 1,
        -((event.clientY - rect.top) / rect.height) * 2 + 1,
      )
      raycaster.setFromCamera(ndc, activeCamera)
      if (!raycaster.ray.intersectPlane(groundPlane, hitPoint)) return null
      return { x: hitPoint.x, y: -hitPoint.z }
    }

    /** Nearest draggable overlay marker within a screen-space radius. */
    function pickDraggableMarker(event: { clientX: number; clientY: number }): { id: string } | null {
      const rect = renderer.domElement.getBoundingClientRect()
      const px = event.clientX - rect.left
      const py = event.clientY - rect.top
      let best: { id: string; distance: number } | null = null
      for (const marker of overlaysRef.current.markers) {
        if (!marker.draggable || !marker.id) continue
        const world = new THREE.Vector3(marker.point.x, 0.15, -marker.point.y).project(activeCamera)
        const sx = ((world.x + 1) / 2) * rect.width
        const sy = ((-world.y + 1) / 2) * rect.height
        const distance = Math.hypot(sx - px, sy - py)
        if (!best || distance < best.distance) best = { id: marker.id, distance }
      }
      return best && best.distance <= 14 ? best : null
    }

    function pickWayLine(event: { clientX: number; clientY: number }): string | null {
      const rect = renderer.domElement.getBoundingClientRect()
      const ndc = new THREE.Vector2(
        ((event.clientX - rect.left) / rect.width) * 2 - 1,
        -((event.clientY - rect.top) / rect.height) * 2 + 1,
      )
      raycaster.setFromCamera(ndc, activeCamera)
      raycaster.params.Line = { threshold: 3 }
      const hits = raycaster.intersectObjects(overlayGroup.children, false)
      for (const hit of hits) {
        const wayKey = (hit.object.userData as { wayKey?: string }).wayKey
        if (wayKey) return wayKey
      }
      return null
    }

    function onPointerDown(event: PointerEvent) {
      if (event.button !== 0 || activePointer !== null) return
      // draggable overlay markers win over other tools (select tool only)
      if (handlersRef.current.interaction === 'click' || handlersRef.current.interaction === 'drag') {
        const marker = pickDraggableMarker(event)
        if (marker && handlersRef.current.interaction === 'click') {
          draggingMarkerId = marker.id
          activePointer = event.pointerId
          renderer.domElement.setPointerCapture(event.pointerId)
          event.preventDefault()
          return
        }
      }
      const point = groundPoint(event)
      if (!point) return
      if (handlersRef.current.interaction === 'click') {
        dragMovedRef.moved = false
        activePointer = event.pointerId
        renderer.domElement.setPointerCapture(event.pointerId)
        handlersRef.current.onGroundClick(point, event)
        event.preventDefault()
        return
      }
      activePointer = event.pointerId
      renderer.domElement.setPointerCapture(event.pointerId)
      handlersRef.current.onDragStart(point)
      event.preventDefault()
    }

    function onPointerMove(event: PointerEvent) {
      const point = groundPoint(event)
      if (!point) return
      if (draggingMarkerId && event.pointerId === activePointer) {
        handlersRef.current.onMarkerDrag(draggingMarkerId, point)
        return
      }
      if (activePointer === null) {
        handlersRef.current.onGroundHover(point)
      } else if (event.pointerId === activePointer && handlersRef.current.interaction === 'drag') {
        handlersRef.current.onDragMove(point)
      }
    }

    function onPointerUp(event: PointerEvent) {
      if (event.pointerId !== activePointer) return
      const point = groundPoint(event)
      activePointer = null
      if (renderer.domElement.hasPointerCapture(event.pointerId)) renderer.domElement.releasePointerCapture(event.pointerId)
      if (draggingMarkerId) {
        const id = draggingMarkerId
        draggingMarkerId = null
        if (point) handlersRef.current.onMarkerDragEnd(id, point)
        return
      }
      if (point) handlersRef.current.onDragEnd(point)
    }

    function onPointerCancel(event: PointerEvent) {
      if (event.pointerId !== activePointer) return
      activePointer = null
      draggingMarkerId = null
      handlersRef.current.onDragCancel()
    }

    function onContextMenuEvent(event: MouseEvent) {
      event.preventDefault()
      const rect = renderer.domElement.getBoundingClientRect()
      const synthetic = new PointerEvent('pointerdown', {
        clientX: event.clientX,
        clientY: event.clientY,
      })
      const point = groundPoint(synthetic)
      if (!point) return
      handlersRef.current.onContextMenu(point, { x: event.clientX - rect.left, y: event.clientY - rect.top })
    }

    function onDoubleClick(event: MouseEvent) {
      const wayKey = pickWayLine(event)
      if (wayKey) handlersRef.current.onWayDoubleClick(wayKey)
    }

    const canvas = renderer.domElement
    canvas.addEventListener('pointerdown', onPointerDown)
    canvas.addEventListener('pointermove', onPointerMove)
    canvas.addEventListener('pointerup', onPointerUp)
    canvas.addEventListener('pointercancel', onPointerCancel)
    canvas.addEventListener('contextmenu', onContextMenuEvent)
    canvas.addEventListener('dblclick', onDoubleClick)

    // ── Render loop ──
    let animationFrame = 0
    let frameCount = 0
    const render = () => {
      animationFrame = requestAnimationFrame(render)
      controls?.update()
      // Update map tiles periodically (every 10 frames after movement)
      frameCount++
      if (showMapRef.current && activeMode === '2d' && frameCount % 10 === 0) {
        updateMapTiles()
      }
      renderer.render(scene, activeCamera)
    }
    animationFrame = requestAnimationFrame(render)

    // Also update tiles when controls change
    let lastTargetPos = new THREE.Vector3()
    let lastZoom = 0
    function checkMapUpdate() {
      if (!showMapRef.current || activeMode !== '2d') return
      const target = controls ? controls.target : new THREE.Vector3(0, 0, 0)
      const zoom = ortho.zoom || 1
      if (target.distanceTo(lastTargetPos) > 1 || Math.abs(zoom - lastZoom) > 0.01) {
        lastTargetPos.copy(target)
        lastZoom = zoom
        lastTileKey = '' // force refresh
        updateMapTiles()
      }
    }
    const mapUpdateInterval = setInterval(checkMapUpdate, 200)

    refsRef.current = {
      applyMode(next) {
        if (next === activeMode) return
        activeMode = next
        roadGroup.position.y = next === '2d' ? 480 : 0
        attach(next === '2d' ? ortho : persp, next === '2d')
        const mapActive = next === '2d' && showMapRef.current
        renderer.setClearColor(0x0b1220, mapActive ? 0 : 1)
        mapMesh.visible = mapActive
        if (mapActive) {
          lastTileKey = ''
          updateMapTiles()
        }
      },
      roadGroup,
      highlightGroup,
      markerGroup,
      overlayGroup,
      pickables: () => overlayGroup.children,
    }

    // Initial map update
    if (showMapRef.current) {
      setTimeout(updateMapTiles, 100)
    }

    return () => {
      cancelAnimationFrame(animationFrame)
      clearInterval(mapUpdateInterval)
      observer.disconnect()
      canvas.removeEventListener('pointerdown', onPointerDown)
      canvas.removeEventListener('pointermove', onPointerMove)
      canvas.removeEventListener('pointerup', onPointerUp)
      canvas.removeEventListener('pointercancel', onPointerCancel)
      canvas.removeEventListener('contextmenu', onContextMenuEvent)
      canvas.removeEventListener('dblclick', onDoubleClick)
      controls?.dispose()
      disposeGroup(roadGroup)
      disposeGroup(highlightGroup)
      disposeGroup(markerGroup)
      disposeGroup(overlayGroup)
      mapGeometry.dispose()
      mapMaterial.dispose()
      if (mapTexture) mapTexture.dispose()
      renderer.dispose()
      canvas.remove()
      refsRef.current = null
    }
  }, [])

  // Update when showMap toggles
  useEffect(() => {
    // The refs handle visibility via applyMode, but we need to trigger
    // a mode re-application when showMap changes
    if (refsRef.current) {
      refsRef.current.applyMode(modeRef.current)
    }
  }, [showMap])

  useEffect(() => {
    refsRef.current?.applyMode(mode)
  }, [mode])

  useEffect(() => {
    const refs = refsRef.current
    if (!refs) return
    disposeGroup(refs.roadGroup)
    for (const mesh of meshes) refs.roadGroup.add(toThreeMesh(mesh, false))
    if (draftMesh) refs.roadGroup.add(toThreeMesh(draftMesh, false))
    disposeGroup(refs.highlightGroup)
    for (const mesh of highlightMeshes) refs.highlightGroup.add(toThreeMesh(mesh, true))
    disposeGroup(refs.markerGroup)
    for (const point of draftPoints) refs.markerGroup.add(toMarker(point))
  }, [meshes, highlightMeshes, draftMesh, draftPoints])

  // Overlay rendering (axes, arrows, nodes, contours, ways, exits, handles)
  useEffect(() => {
    const refs = refsRef.current
    if (!refs) return
    disposeGroup(refs.overlayGroup)
    for (const line of overlays.lines) {
      if (line.points.length < 2) continue
      refs.overlayGroup.add(toRibbon(line))
    }
    for (const marker of overlays.markers) {
      refs.overlayGroup.add(toOverlayMarker(marker))
    }
  }, [overlays])

  return (
    <div className="viewport">
      <div ref={containerRef} className="viewport-canvas" />
      <div className="viewport-hint">{hint}</div>
    </div>
  )
}

function toThreeMesh(mesh: MeshData, highlighted: boolean): THREE.Mesh {
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(mesh.positions, 3))
  geometry.setAttribute('color', new THREE.BufferAttribute(mesh.colors, 3))
  geometry.setIndex(new THREE.BufferAttribute(mesh.indices, 1))
  const material = highlighted
    ? new THREE.MeshBasicMaterial({ color: 0xffffff, wireframe: true, depthTest: false })
    : new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide })
  const result = new THREE.Mesh(geometry, material)
  result.renderOrder = highlighted ? 10 : 0
  return result
}

function toMarker(point: Vec2): THREE.Mesh {
  const marker = new THREE.Mesh(
    new THREE.SphereGeometry(0.8, 12, 12),
    new THREE.MeshBasicMaterial({ color: 0x4ade80, depthTest: false }),
  )
  marker.position.set(point.x, 0.08, -point.y)
  marker.renderOrder = 11
  return marker
}

/** Flat ribbon following a polyline — visible "axis" line with real width. */
function toRibbon(line: OverlayLine): THREE.Mesh {
  const width = line.width ?? 0.4
  const y = 0.12
  const pts = line.points
  const n = pts.length
  const positions = new Float32Array(n * 6)
  const colors = new Float32Array(n * 6)
  const color = new THREE.Color(line.color)
  for (let i = 0; i < n; i++) {
    const prev = pts[Math.max(0, i - 1)]
    const next = pts[Math.min(n - 1, i + 1)]
    const dx = next.x - prev.x
    const dy = next.y - prev.y
    const len = Math.hypot(dx, dy) || 1
    const nx = -dy / len
    const ny = dx / len
    const base = i * 6
    positions[base] = pts[i].x + nx * width / 2
    positions[base + 1] = y
    positions[base + 2] = -(pts[i].y + ny * width / 2)
    positions[base + 3] = pts[i].x - nx * width / 2
    positions[base + 4] = y
    positions[base + 5] = -(pts[i].y - ny * width / 2)
    for (let v = 0; v < 2; v++) {
      colors[base + v * 3] = color.r
      colors[base + v * 3 + 1] = color.g
      colors[base + v * 3 + 2] = color.b
    }
  }
  const indices: number[] = []
  for (let i = 0; i < n - 1; i++) {
    const a = i * 2
    indices.push(a, a + 2, a + 1, a + 1, a + 2, a + 3)
  }
  const geometry = new THREE.BufferGeometry()
  geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3))
  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3))
  geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(indices), 1))
  const material = new THREE.MeshBasicMaterial({
    vertexColors: true,
    side: THREE.DoubleSide,
    transparent: (line.opacity ?? 1) < 1,
    opacity: line.opacity ?? 1,
    depthWrite: false,
  })
  const mesh = new THREE.Mesh(geometry, material)
  mesh.renderOrder = line.wayKey ? 5 : 6
  if (line.wayKey) mesh.userData = { wayKey: line.wayKey }
  return mesh
}

function toOverlayMarker(marker: OverlayMarker): THREE.Object3D {
  const size = marker.size ?? 1.6
  const color = new THREE.Color(marker.color)
  const material = new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.95 })
  let geometry: THREE.BufferGeometry
  if (marker.shape === 'arrow') {
    // flat triangle pointing +X, rotated by heading; double-sided so it is
    // visible from the top-down camera regardless of winding
    geometry = new THREE.BufferGeometry()
    geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array([
      size * 1.4, 0.14, 0,
      -size * 0.6, 0.14, size * 0.8,
      -size * 0.6, 0.14, -size * 0.8,
    ]), 3))
    geometry.computeVertexNormals()
    const arrowMaterial = new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.95, side: THREE.DoubleSide })
    const mesh = new THREE.Mesh(geometry, arrowMaterial)
    mesh.position.set(marker.point.x, 0, -marker.point.y)
    mesh.rotation.y = -(marker.heading ?? 0)
    mesh.renderOrder = 7
    return mesh
  }
  if (marker.shape === 'ring') {
    geometry = new THREE.RingGeometry(size * 0.7, size, 24)
  } else if (marker.shape === 'square') {
    geometry = new THREE.PlaneGeometry(size * 1.4, size * 1.4)
  } else {
    geometry = new THREE.CircleGeometry(size, 24)
  }
  const mesh = new THREE.Mesh(geometry, material)
  mesh.rotation.x = -Math.PI / 2
  mesh.position.set(marker.point.x, 0.14, -marker.point.y)
  mesh.renderOrder = 7
  return mesh
}

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    group.remove(child)
    const mesh = child as THREE.Mesh
    mesh.geometry?.dispose()
    if (mesh.material) (mesh.material as THREE.Material).dispose()
  }
}
