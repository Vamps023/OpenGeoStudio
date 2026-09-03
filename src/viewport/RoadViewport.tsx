import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import type { MeshData } from '../engine/mesh'
import type { Vec2 } from '../engine/types'

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
  onGroundClick: (point: Vec2) => void
  onGroundHover: (point: Vec2) => void
  mode: '2d' | '3d'
  hint: string
}

interface ViewRefs {
  applyMode: (mode: '2d' | '3d') => void
  roadGroup: THREE.Group
  highlightGroup: THREE.Group
  markerGroup: THREE.Group
}

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
  mode,
  hint,
}: RoadViewportProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const refsRef = useRef<ViewRefs | null>(null)
  const handlersRef = useRef({ interaction, onDragStart, onDragMove, onDragEnd, onDragCancel, onGroundClick, onGroundHover })
  handlersRef.current = { interaction, onDragStart, onDragMove, onDragEnd, onDragCancel, onGroundClick, onGroundHover }

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

    scene.add(new THREE.GridHelper(2000, 200, 0x1e2a3f, 0x121b2c))
    const roadGroup = new THREE.Group()
    const highlightGroup = new THREE.Group()
    const markerGroup = new THREE.Group()
    scene.add(roadGroup, highlightGroup, markerGroup)

    let controls: OrbitControls | null = null
    let activeCamera: THREE.Camera = ortho
    let activeMode: '2d' | '3d' = '2d'

    function attach(camera: THREE.Camera, is2d: boolean) {
      controls?.dispose()
      controls = new OrbitControls(camera, renderer.domElement)
      controls.enableRotate = !is2d
      controls.mouseButtons = is2d
        ? { LEFT: -1, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.PAN }
        : { LEFT: -1, MIDDLE: THREE.MOUSE.DOLLY, RIGHT: THREE.MOUSE.ROTATE }
      controls.update()
      activeCamera = camera
    }
    attach(ortho, true)

    function resize() {
      const width = container.clientWidth
      const height = container.clientHeight
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
    }
    const observer = new ResizeObserver(resize)
    observer.observe(container)

    const raycaster = new THREE.Raycaster()
    const groundPlane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0)
    const hitPoint = new THREE.Vector3()
    let activePointer: number | null = null

    function groundPoint(event: PointerEvent): Vec2 | null {
      const rect = renderer.domElement.getBoundingClientRect()
      const ndc = new THREE.Vector2(
        ((event.clientX - rect.left) / rect.width) * 2 - 1,
        -((event.clientY - rect.top) / rect.height) * 2 + 1,
      )
      raycaster.setFromCamera(ndc, activeCamera)
      if (!raycaster.ray.intersectPlane(groundPlane, hitPoint)) return null
      return { x: hitPoint.x, y: -hitPoint.z }
    }

    function onPointerDown(event: PointerEvent) {
      if (event.button !== 0 || activePointer !== null) return
      const point = groundPoint(event)
      if (!point) return
      if (handlersRef.current.interaction === 'click') {
        handlersRef.current.onGroundClick(point)
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
      if (point) handlersRef.current.onDragEnd(point)
    }

    function onPointerCancel(event: PointerEvent) {
      if (event.pointerId !== activePointer) return
      activePointer = null
      handlersRef.current.onDragCancel()
    }

    const canvas = renderer.domElement
    canvas.addEventListener('pointerdown', onPointerDown)
    canvas.addEventListener('pointermove', onPointerMove)
    canvas.addEventListener('pointerup', onPointerUp)
    canvas.addEventListener('pointercancel', onPointerCancel)

    let animationFrame = 0
    const render = () => {
      animationFrame = requestAnimationFrame(render)
      renderer.render(scene, activeCamera)
    }
    animationFrame = requestAnimationFrame(render)

    refsRef.current = {
      applyMode(next) {
        if (next === activeMode) return
        activeMode = next
        attach(next === '2d' ? ortho : persp, next === '2d')
      },
      roadGroup,
      highlightGroup,
      markerGroup,
    }

    return () => {
      cancelAnimationFrame(animationFrame)
      observer.disconnect()
      canvas.removeEventListener('pointerdown', onPointerDown)
      canvas.removeEventListener('pointermove', onPointerMove)
      canvas.removeEventListener('pointerup', onPointerUp)
      canvas.removeEventListener('pointercancel', onPointerCancel)
      controls?.dispose()
      disposeGroup(roadGroup)
      disposeGroup(highlightGroup)
      disposeGroup(markerGroup)
      renderer.dispose()
      canvas.remove()
      refsRef.current = null
    }
  }, [])

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
    ? new THREE.MeshBasicMaterial({ color: 0x4ade80, wireframe: true, depthTest: false })
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

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    group.remove(child)
    child.geometry?.dispose()
    if (child.material) (child.material as THREE.Material).dispose()
  }
}
