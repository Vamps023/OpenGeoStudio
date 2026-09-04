import { useEffect, useState } from 'react'
import { Boxes, FolderOpen, HardDrive, Layers, Map as MapIcon, Plus, Route, Trash2 } from 'lucide-react'
import { toast } from 'sonner'

import LogoMark from '@/components/layout/LogoMark'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import {
  Card,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from '@/components/ui/card'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from '@/components/ui/dialog'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { useStore } from '../state/store'

export default function ProjectsPage({ onOpenEditor, onOpenTerrain, onOpenStudio3D }) {
  const projects = useStore((s) => s.projects)
  const createProject = useStore((s) => s.createProject)
  const openProject = useStore((s) => s.openProject)
  const refreshProjects = useStore((s) => s.refreshProjects)
  const deleteProject = useStore((s) => s.deleteProject)
  const workspacePath = useStore((s) => s.workspacePath)
  const [name, setName] = useState('')
  const [dialogOpen, setDialogOpen] = useState(false)
  const [deleteTarget, setDeleteTarget] = useState(null)

  // Load projects from C:\OpenGeoStudio on mount
  useEffect(() => {
    void refreshProjects()
  }, [refreshProjects])

  function handleCreate(event) {
    event.preventDefault()
    if (!name.trim()) return
    createProject(name)
    setName('')
    setDialogOpen(false)
  }

  function openInEditor(id) {
    openProject(id)
    onOpenEditor()
  }

  function openInTerrain(id) {
    openProject(id)
    onOpenTerrain()
  }

  function openInStudio3D(id) {
    openProject(id)
    onOpenStudio3D()
  }

  async function handleDelete() {
    if (!deleteTarget) return
    const projectName = deleteTarget.name
    await deleteProject(deleteTarget.id)
    setDeleteTarget(null)
    toast.success('Project deleted', { description: projectName })
  }

  return (
    <main className="relative min-h-screen bg-background">
      {/* Ambient background */}
      <div
        aria-hidden="true"
        className="pointer-events-none fixed inset-0 bg-[radial-gradient(circle_at_15%_0%,rgb(74_222_128/8%),transparent_28rem),radial-gradient(circle_at_85%_100%,rgb(74_222_128/6%),transparent_28rem)]"
      />

      <div className="relative mx-auto w-full max-w-5xl px-6 py-12">
        <header className="mb-10 flex items-center justify-between gap-4">
          <div className="flex items-center gap-4">
            <LogoMark className="h-12 w-12 rounded-xl" />
            <div>
              <p className="text-[11px] font-bold tracking-[0.2em] text-primary uppercase">
                OpenGeoStudio
              </p>
              <h1 className="text-2xl font-semibold tracking-tight">Projects</h1>
              {workspacePath && (
                <p className="mt-0.5 flex items-center gap-1 text-[11px] text-muted-foreground">
                  <HardDrive className="size-3" />
                  {workspacePath}
                </p>
              )}
            </div>
          </div>

          <Dialog open={dialogOpen} onOpenChange={setDialogOpen}>
            <DialogTrigger asChild>
              <Button>
                <Plus className="size-4" />
                New Project
              </Button>
            </DialogTrigger>
            <DialogContent className="max-w-md">
              <DialogHeader>
                <DialogTitle>Create Project</DialogTitle>
                <DialogDescription>
                  Name your project to start designing a road network.
                </DialogDescription>
              </DialogHeader>
              <form onSubmit={handleCreate} className="grid gap-4">
                <div className="grid gap-2">
                  <Label htmlFor="project-name">Project name</Label>
                  <Input
                    id="project-name"
                    value={name}
                    onChange={(event) => setName(event.target.value)}
                    placeholder="e.g. Highway 40 interchange"
                    autoFocus
                  />
                </div>
                <DialogFooter>
                  <Button type="submit" disabled={!name.trim()}>
                    Create Project
                  </Button>
                </DialogFooter>
              </form>
            </DialogContent>
          </Dialog>
        </header>

        {projects.length === 0 ? (
          <div className="grid place-items-center rounded-xl border border-dashed border-border bg-card/40 py-24 text-center">
            <div className="grid max-w-sm gap-3 justify-items-center">
              <div className="grid size-12 place-items-center rounded-full bg-primary/10">
                <FolderOpen className="size-6 text-primary" />
              </div>
              <h2 className="text-base font-semibold">No projects yet</h2>
              <p className="text-sm text-muted-foreground">
                Create your first project to start drawing roads and working with terrain.
              </p>
              <Button variant="outline" size="sm" className="mt-1" onClick={() => setDialogOpen(true)}>
                <Plus className="size-4" />
                Create a project
              </Button>
            </div>
          </div>
        ) : (
          <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
            {projects.map((project) => (
              <Card
                key={project.id}
                className="group relative gap-4 bg-card/70 py-5 transition-colors hover:border-primary/40"
              >
                <Button
                  size="sm"
                  variant="ghost"
                  className="absolute right-2 top-2 size-7 p-0 text-muted-foreground opacity-0 transition-opacity hover:text-destructive group-hover:opacity-100"
                  onClick={() => setDeleteTarget(project)}
                  title="Delete project"
                >
                  <Trash2 className="size-4" />
                </Button>
                <CardHeader className="gap-2">
                  <div className="flex items-start justify-between gap-2">
                    <CardTitle className="truncate text-base">{project.name}</CardTitle>
                    <Badge variant="success" className="shrink-0">
                      <Route className="size-3" />
                      {project.roads.length} road{project.roads.length === 1 ? '' : 's'}
                    </Badge>
                  </div>
                  <CardDescription>
                    Created {new Date(project.createdAt).toLocaleDateString()}
                  </CardDescription>
                </CardHeader>
                <CardFooter className="gap-2">
                  <Button
                    size="sm"
                    variant="secondary"
                    className="flex-1 text-xs"
                    onClick={() => openInEditor(project.id)}
                  >
                    <Layers className="size-4" />
                    Editor
                  </Button>
                  <Button
                    size="sm"
                    variant="outline"
                    className="flex-1 text-xs"
                    onClick={() => openInTerrain(project.id)}
                  >
                    <MapIcon className="size-4" />
                    Terrain
                  </Button>
                  <Button
                    size="sm"
                    variant="outline"
                    className="flex-1 text-xs"
                    onClick={() => openInStudio3D(project.id)}
                  >
                    <Boxes className="size-4" />
                    3D Studio
                  </Button>
                </CardFooter>
              </Card>
            ))}
          </div>
        )}
      </div>

      {/* Delete confirmation dialog */}
      <Dialog open={!!deleteTarget} onOpenChange={(open) => !open && setDeleteTarget(null)}>
        <DialogContent className="max-w-md">
          <DialogHeader>
            <DialogTitle>Delete Project</DialogTitle>
            <DialogDescription>
              Are you sure you want to delete <strong>{deleteTarget?.name}</strong>?
              This will permanently remove the project folder and all exported terrain files from{' '}
              <code className="rounded bg-muted px-1 text-xs">C:\OpenGeoStudio</code>.
              This action cannot be undone.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter className="gap-2">
            <Button variant="outline" onClick={() => setDeleteTarget(null)}>
              Cancel
            </Button>
            <Button variant="destructive" onClick={handleDelete}>
              <Trash2 className="size-4" />
              Delete Project
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </main>
  )
}
