import { useStore } from '../state/store'
import AppHeader from '@/components/layout/AppHeader'
import TerrainWorkspace from '../terrain/TerrainWorkspace'

/** Standalone Terrain workspace page — the body lives in TerrainWorkspace so
 *  it can also be hosted elsewhere. */
export default function TerrainPage({ onBack }: { onBack: () => void }) {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const project = projects.find((p) => p.id === activeProjectId)
  if (!project) return null
  return (
    <div className="flex h-screen min-h-0 flex-col bg-background">
      <AppHeader projectName={project.name} subtitle="Terrain Workspace" onBack={onBack} />
      <div className="min-h-0 flex-1">
        <TerrainWorkspace />
      </div>
    </div>
  )
}
