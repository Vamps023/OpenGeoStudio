import { useStore } from '../state/store'
import AppHeader from '@/components/layout/AppHeader'
import OsmBuilderWorkspace from '../osm/OsmBuilderWorkspace'

/** Dedicated OSM Builder workspace page (issue #49): draw a polygon on an
 *  interactive map and download OSM buildings inside it. Hosted as a
 *  standalone workspace alongside Terrain / Editor / 3D Studio. */
export default function OsmBuilderPage({ onBack }: { onBack: () => void }) {
  const projects = useStore((s) => s.projects)
  const activeProjectId = useStore((s) => s.activeProjectId)
  const project = projects.find((p) => p.id === activeProjectId)
  if (!project) return null
  return (
    <div className="flex h-screen min-h-0 flex-col bg-background">
      <AppHeader projectName={project.name} subtitle="OSM Builder" onBack={onBack} />
      <div className="min-h-0 flex-1">
        <OsmBuilderWorkspace />
      </div>
    </div>
  )
}
