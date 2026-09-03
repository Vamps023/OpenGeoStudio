import { useState } from 'react'
import EditorPage from './pages/EditorPage'
import LoginPage from './pages/LoginPage'
import ProjectsPage from './pages/ProjectsPage'
import TerrainPage from './pages/TerrainPage'
import { useStore } from './state/store'

export default function App() {
  const [loggedIn, setLoggedIn] = useState(false)
  const [workspace, setWorkspace] = useState('projects')
  const activeProjectId = useStore((s) => s.activeProjectId)
  const closeProject = useStore((s) => s.closeProject)

  if (!loggedIn) return <LoginPage onLogin={() => setLoggedIn(true)} />

  if (workspace === 'terrain' && activeProjectId) {
    return (
      <TerrainPage
        onBack={() => {
          closeProject()
          setWorkspace('projects')
        }}
      />
    )
  }

  if (workspace === 'editor' && activeProjectId) {
    return (
      <EditorPage
        onBack={() => {
          closeProject()
          setWorkspace('projects')
        }}
      />
    )
  }

  return (
    <ProjectsPage
      onOpenEditor={() => setWorkspace('editor')}
      onOpenTerrain={() => setWorkspace('terrain')}
    />
  )
}
