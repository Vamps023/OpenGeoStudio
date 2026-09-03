import { useState } from 'react'
import { FolderOpen, Layers, Map as MapIcon, Plus, Route } from 'lucide-react'

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

export default function ProjectsPage({ onOpenEditor, onOpenTerrain }) {
  const projects = useStore((s) => s.projects)
  const createProject = useStore((s) => s.createProject)
  const openProject = useStore((s) => s.openProject)
  const [name, setName] = useState('')
  const [dialogOpen, setDialogOpen] = useState(false)

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
                className="group gap-4 bg-card/70 py-5 transition-colors hover:border-primary/40"
              >
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
                    className="flex-1"
                    onClick={() => openInEditor(project.id)}
                  >
                    <Layers className="size-4" />
                    Road Editor
                  </Button>
                  <Button
                    size="sm"
                    variant="outline"
                    className="flex-1"
                    onClick={() => openInTerrain(project.id)}
                  >
                    <MapIcon className="size-4" />
                    Terrain
                  </Button>
                </CardFooter>
              </Card>
            ))}
          </div>
        )}
      </div>
    </main>
  )
}
