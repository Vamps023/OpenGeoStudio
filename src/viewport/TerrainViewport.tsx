import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/addons/controls/OrbitControls.js'
import type { TerrainMeshData } from '../engine/terrainMesh'

interface Props {
  terrainMesh: TerrainMeshData | null
  heightScale: number
}

export default function TerrainViewport({ terrainMesh, heightScale }: Props) {
  const containerRef = useRef<HTMLDivElement>(null)
  const terrainGroupRef = useRef<THREE.Group | null>(null)
  const heightScaleRef = useRef(heightScale)
  heightScaleRef.current = heightScale

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
    const persp = new THREE.PerspectiveCamera(55, aspect, 0.1, 50000)
    persp.position.set(300, 350, 400)
    persp.lookAt(0, 0, 0)

    const ambient = new THREE.AmbientLight(0x808090, 0.7)
    const directional = new THREE.DirectionalLight(0xffffff, 0.9)
    directional.position.set(200, 400, 150)
    scene.add(ambient, directional)

    scene.add(new THREE.GridHelper(2000, 100, 0x1e2a3f, 0x121b2c))

    const terrainGroup = new THREE.Group()
    scene.add(terrainGroup)
    terrainGroupRef.current = terrainGroup

    const controls = new OrbitControls(persp, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.08

    function resize() {
      const w = container!.clientWidth
      const h = container!.clientHeight
      if (!w || !h) return
      renderer.setSize(w, h)
      persp.aspect = w / h
      persp.updateProjectionMatrix()
    }
    const observer = new ResizeObserver(resize)
    observer.observe(container)

    let frame = 0
    const render = () => {
      frame = requestAnimationFrame(render)
      controls.update()
      renderer.render(scene, persp)
    }
    frame = requestAnimationFrame(render)

    return () => {
      cancelAnimationFrame(frame)
      observer.disconnect()
      controls.dispose()
      disposeGroup(terrainGroup)
      renderer.dispose()
      renderer.domElement.remove()
    }
  }, [])

  useEffect(() => {
    const group = terrainGroupRef.current
    if (!group) return
    disposeGroup(group)
    if (!terrainMesh) return
    const geometry = new THREE.BufferGeometry()
    geometry.setAttribute('position', new THREE.BufferAttribute(terrainMesh.positions, 3))
    geometry.setAttribute('normal', new THREE.BufferAttribute(terrainMesh.normals, 3))
    geometry.setAttribute('color', new THREE.BufferAttribute(terrainMesh.colors, 3))
    geometry.setIndex(new THREE.BufferAttribute(terrainMesh.indices, 1))
    const material = new THREE.MeshStandardMaterial({
      vertexColors: true,
      side: THREE.DoubleSide,
      roughness: 0.9,
      metalness: 0.0,
    })
    const mesh = new THREE.Mesh(geometry, material)
    mesh.scale.y = heightScale
    group.add(mesh)
  }, [terrainMesh, heightScale])

  return (
    <div className="viewport">
      <div ref={containerRef} className="viewport-canvas" />
      {!terrainMesh && <div className="viewport-hint">No terrain loaded. Set bounds and click Download Terrain.</div>}
    </div>
  )
}

function disposeGroup(group: THREE.Group) {
  for (const child of [...group.children]) {
    group.remove(child)
    const mesh = child as THREE.Mesh
    mesh.geometry?.dispose()
    if (mesh.material) (mesh.material as THREE.Material).dispose()
  }
}
