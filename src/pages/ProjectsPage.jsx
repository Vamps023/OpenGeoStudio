import { useState } from 'react'
import { useStore } from '../state/store'

export default function ProjectsPage({ onOpenEditor, onOpenTerrain }) {
  const projects = useStore((s) => s.projects)
  const createProject = useStore((s) => s.createProject)
  const openProject = useStore((s) => s.openProject)
  const [name, setName] = useState('')

  function handleCreate(event) {
    event.preventDefault()
    if (!name.trim()) return
    createProject(name)
    setName('')
  }

  function openInEditor(id) {
    openProject(id)
    onOpenEditor()
  }

  function openInTerrain(id) {
    openProject(id)
    onOpenTerrain()
  }

  return (
    <main className="projects-page">
      <section className="projects-panel">
        <header className="projects-header">
          <div className="logo-mark" aria-hidden="true">
            <span />
            <span />
            <span />
          </div>
          <div>
            <p className="eyebrow">OpenGeoStudio</p>
            <h1>Projects</h1>
          </div>
        </header>

        <form className="project-form" onSubmit={handleCreate}>
          <label>
            <span>Project name</span>
            <input
              type="text"
              value={name}
              onChange={(event) => setName(event.target.value)}
              placeholder="e.g. Highway 40 interchange"
              autoFocus
            />
          </label>
          <button type="submit">Create Project</button>
        </form>

        <h2 className="recent-title">Recent Projects</h2>
        {projects.length === 0 ? (
          <p className="empty-note">No projects yet. Create your first one above.</p>
        ) : (
          <ul className="project-list">
            {projects.map((project) => (
              <li key={project.id}>
                <div className="project-info">
                  <span className="project-name">{project.name}</span>
                  <span className="project-date">
                    {project.roads.length} road{project.roads.length === 1 ? '' : 's'} ·{' '}
                    {new Date(project.createdAt).toLocaleDateString()}
                  </span>
                </div>
                <div className="project-actions">
                  <button type="button" onClick={() => openInEditor(project.id)}>Road Editor</button>
                  <button type="button" onClick={() => openInTerrain(project.id)}>Terrain</button>
                </div>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  )
}
