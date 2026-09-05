import { useState } from 'react'
import EditorPage from './pages/EditorPage'
import LoginPage from './pages/LoginPage'
import OsmBuilderPage from './pages/OsmBuilderPage'
import ProjectsPage from './pages/ProjectsPage'
import Studio3DPage from './pages/Studio3DPage'
import TerrainPage from './pages/TerrainPage'
import { Toaster } from './components/ui/sonner'
import { TooltipProvider } from './components/ui/tooltip'
import { useStore } from './state/store'

export default function App() {
  const [loggedIn, setLoggedIn] = useState(false)
  const [workspace, setWorkspace] = useState('projects')
  const activeProjectId = useStore((s) => s.activeProjectId)
  const closeProject = useStore((s) => s.closeProject)

  if (!loggedIn) return <LoginPage onLogin={() => setLoggedIn(true)} />

  if (workspace === 'terrain' && activeProjectId) {
    return (
      <TooltipProvider>
        <TerrainPage
          onBack={() => {
            closeProject()
            setWorkspace('projects')
          }}
        />
        <Toaster position="bottom-right" />
      </TooltipProvider>
    )
  }

  if (workspace === 'studio3d' && activeProjectId) {
    return (
      <TooltipProvider>
        <Studio3DPage
          onBack={() => {
            closeProject()
            setWorkspace('projects')
          }}
        />
        <Toaster position="bottom-right" />
      </TooltipProvider>
    )
  }

  if (workspace === 'editor' && activeProjectId) {
    return (
      <TooltipProvider>
        <EditorPage
          onBack={() => {
            closeProject()
            setWorkspace('projects')
          }}
        />
        <Toaster position="bottom-right" />
      </TooltipProvider>
    )
  }

  if (workspace === 'osmBuilder' && activeProjectId) {
    return (
      <TooltipProvider>
        <OsmBuilderPage
          onBack={() => {
            closeProject()
            setWorkspace('projects')
          }}
        />
        <Toaster position="bottom-right" />
      </TooltipProvider>
    )
  }

  return (
    <TooltipProvider>
      <ProjectsPage
        onOpenEditor={() => setWorkspace('editor')}
        onOpenTerrain={() => setWorkspace('terrain')}
        onOpenStudio3D={() => setWorkspace('studio3d')}
        onOpenOsmBuilder={() => setWorkspace('osmBuilder')}
      />
      <Toaster position="bottom-right" />
    </TooltipProvider>
  )
}
