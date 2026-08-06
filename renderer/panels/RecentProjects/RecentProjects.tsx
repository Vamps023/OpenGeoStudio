/**
 * RecentProjects — Professional Home / Start screen.
 *
 * Sections:
 *   1. Welcome header with logo, version, and active project status
 *   2. Template cards (create new project from template)
 *   3. Quick actions (new, open, import)
 *   4. Pinned projects (always at top)
 *   5. Recent projects (searchable list)
 *   6. System status (modules loaded, plugins, version)
 *
 * Reacts to project changes via useCoreStore.
 */

import React, { useState, useEffect, useCallback, useMemo, useRef } from 'react';
import {
  Clock, FilePlus, FolderOpen, Search, Pin, Star,
  Mountain, FileText,
  Cpu, CheckCircle, Layers, X, Folder,
} from 'lucide-react';
import { useCoreStore } from '../../core/coreStore';
import { useTerrainStore } from '../../core/store';
import { ProjectService } from '../../core/coreService';
import { Dialog } from '../../core/ipc';
import { EmptyState } from '../../components/common/EmptyState';

const APP_VERSION = (typeof __APP_VERSION__ !== 'undefined' ? __APP_VERSION__ : null) || '2.0.0';

// ─── Templates ────────────────────────────────────────────────

const TEMPLATES = [
  { id: 'terrain', name: 'Terrain', icon: Mountain, description: 'DEM, heightmaps, satellite, terrain generation, validation, export', workspace: 'terrain', color: 'text-ok' },
];

// ─── Recent project type ──────────────────────────────────────

interface RecentProject {
  id: string;
  name: string;
  path: string;
  lastModified: string;
  workspace: string;
  pinned: boolean;
}

// ─── Template Card ────────────────────────────────────────────

const TemplateCard: React.FC<{
  template: typeof TEMPLATES[0];
  onClick: () => void;
}> = ({ template, onClick }) => (
  <button
    onClick={onClick}
    className="group flex flex-col gap-3 p-5 bg-surface-panel border border-edge rounded-lg
      hover:border-accent/50 hover:bg-surface-hover transition-all duration-200 text-left
      focus-visible:outline-2 focus-visible:outline-accent"
  >
    <div className={`w-10 h-10 rounded-lg bg-surface-elevated border border-edge flex items-center justify-center ${template.color} group-hover:scale-110 transition-transform`}>
      <template.icon size={22} />
    </div>
    <div>
      <div className="text-sm font-semibold text-fg-primary mb-0.5">{template.name}</div>
      <div className="text-2xs text-fg-secondary leading-relaxed">{template.description}</div>
    </div>
  </button>
);

// ─── Quick Action Button ──────────────────────────────────────

const QuickAction: React.FC<{
  icon: React.ComponentType<{ size?: number; className?: string }>;
  label: string;
  onClick: () => void;
}> = ({ icon: Icon, label, onClick }) => (
  <button
    onClick={onClick}
    className="flex items-center gap-2.5 px-4 py-2.5 bg-surface-panel border border-edge rounded-lg
      hover:border-accent/50 hover:bg-surface-hover transition-all duration-200
      focus-visible:outline-2 focus-visible:outline-accent"
  >
    <Icon size={16} className="text-accent" />
    <span className="text-2xs font-medium text-fg-primary">{label}</span>
  </button>
);

// ─── Project Row ──────────────────────────────────────────────

const ProjectRow: React.FC<{
  project: RecentProject;
  isPinned: boolean;
  onOpen: () => void;
  onTogglePin: () => void;
}> = ({ project, isPinned, onOpen, onTogglePin }) => (
  <div
    className="flex items-center gap-3 p-2.5 rounded-md hover:bg-surface-hover transition-colors group cursor-pointer"
    onClick={onOpen}
    role="button"
    tabIndex={0}
    onKeyDown={(e) => { if (e.key === 'Enter') onOpen(); }}
  >
    <FileText size={16} className="text-fg-muted shrink-0" />
    <div className="flex-1 min-w-0">
      <div className="text-2xs font-medium text-fg-primary truncate">{project.name}</div>
      <div className="text-3xs text-fg-muted truncate font-mono">{project.path}</div>
    </div>
    <span className="text-3xs text-fg-muted shrink-0 tabular-nums">{project.lastModified}</span>
    <button
      onClick={(e) => { e.stopPropagation(); onTogglePin(); }}
      className={`p-1 rounded transition-all ${
        isPinned
          ? 'text-warn'
          : 'text-fg-muted opacity-0 group-hover:opacity-100 hover:text-fg-secondary'
      }`}
      aria-label={isPinned ? 'Unpin project' : 'Pin project'}
    >
      {isPinned ? <Star size={12} fill="currentColor" /> : <Pin size={12} />}
    </button>
  </div>
);

// ─── New Project Modal ───────────────────────────────────────

const NewProjectModal: React.FC<{
  template: typeof TEMPLATES[0];
  onClose: () => void;
  onCreate: (name: string, basePath: string) => Promise<void>;
}> = ({ template, onClose, onCreate }) => {
  const [projectName, setProjectName] = useState('');
  const [basePath, setBasePath] = useState('');
  const [creating, setCreating] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  // Load default projects directory on mount
  useEffect(() => {
    (async () => {
      try {
        const dir = await Dialog.getDefaultProjectsDir();
        setBasePath(dir);
      } catch {
        setBasePath('');
      }
    })();
  }, []);

  // Focus the input when modal opens
  useEffect(() => {
    setTimeout(() => inputRef.current?.focus(), 50);
  }, []);

  // Auto-generate project name from template (user can edit)
  useEffect(() => {
    if (!projectName) setProjectName(template.name);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- only set default when template changes, not on every keystroke
  }, [template]);

  const safeName = projectName.replace(/[^a-zA-Z0-9_-]/g, '_');
  const previewPath = basePath ? `${basePath}\\${safeName}` : '';

  const handleBrowse = useCallback(async () => {
    const folder = await Dialog.newProject();
    if (folder) setBasePath(folder);
  }, []);

  const handleCreate = useCallback(async () => {
    const name = projectName.trim();
    if (!name) { setError('Please enter a project name'); return; }
    if (!basePath) { setError('Could not determine projects directory'); return; }
    setCreating(true);
    setError(null);
    try {
      await onCreate(name, basePath);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to create project');
    } finally {
      setCreating(false);
    }
  }, [projectName, basePath, onCreate]);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && !creating) {
      e.preventDefault();
      handleCreate();
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      onClose();
    }
  }, [handleCreate, creating, onClose]);

  return (
    <div
      className="fixed inset-0 z-[100] flex items-center justify-center bg-black/60 backdrop-blur-sm"
      onClick={onClose}
      onKeyDown={handleKeyDown}
    >
      <div
        className="w-full max-w-md bg-surface-panel border border-edge rounded-lg shadow-2xl"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between px-5 py-4 border-b border-edge">
          <div className="flex items-center gap-3">
            <div className={`w-8 h-8 rounded-lg bg-surface-elevated border border-edge flex items-center justify-center ${template.color}`}>
              <template.icon size={18} />
            </div>
            <div>
              <h2 className="text-sm font-semibold text-fg-primary">New {template.name} Project</h2>
              <p className="text-3xs text-fg-secondary">{template.description}</p>
            </div>
          </div>
          <button
            onClick={onClose}
            className="p-1 rounded text-fg-muted hover:text-fg-primary hover:bg-surface-hover transition-colors"
            aria-label="Close"
          >
            <X size={16} />
          </button>
        </div>

        {/* Body */}
        <div className="px-5 py-4 space-y-4">
          {/* Project Name */}
          <div>
            <label className="block text-3xs font-medium text-fg-secondary mb-1.5">
              Project Name
            </label>
            <input
              ref={inputRef}
              type="text"
              value={projectName}
              onChange={(e) => setProjectName(e.target.value)}
              placeholder="My Terrain Project"
              className="w-full px-3 py-2 bg-surface-base border border-edge rounded-md text-2xs text-fg-primary
                placeholder-fg-muted outline-none focus:border-accent transition-colors"
            />
          </div>

          {/* Location */}
          <div>
            <label className="block text-3xs font-medium text-fg-secondary mb-1.5">
              Location
            </label>
            <div className="flex items-center gap-2">
              <div className="flex-1 flex items-center gap-2 px-3 py-2 bg-surface-base border border-edge rounded-md min-h-[34px]">
                <Folder size={14} className="text-fg-muted shrink-0" />
                <span className="text-3xs text-fg-secondary font-mono truncate">
                  {basePath || 'Loading…'}
                </span>
              </div>
              <button
                onClick={handleBrowse}
                className="px-3 py-2 bg-surface-elevated border border-edge rounded-md text-3xs text-fg-primary
                  hover:border-accent/50 hover:bg-surface-hover transition-colors shrink-0"
              >
                Browse
              </button>
            </div>
          </div>

          {/* Preview path */}
          {previewPath && (
            <div className="px-3 py-2 bg-surface-elevated border border-edge rounded-md">
              <div className="text-3xs text-fg-muted mb-0.5">Project will be created at:</div>
              <div className="text-3xs text-fg-secondary font-mono break-all">{previewPath}</div>
            </div>
          )}

          {/* Error */}
          {error && (
            <div className="px-3 py-2 bg-error/10 border border-error/30 rounded-md">
              <span className="text-3xs text-error">{error}</span>
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="flex items-center justify-end gap-2 px-5 py-4 border-t border-edge">
          <button
            onClick={onClose}
            className="px-4 py-2 text-2xs font-medium text-fg-secondary hover:text-fg-primary transition-colors"
          >
            Cancel
          </button>
          <button
            onClick={handleCreate}
            disabled={creating || !projectName.trim() || !basePath}
            className="px-4 py-2 bg-accent text-white text-2xs font-medium rounded-md
              hover:bg-accent/90 disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
          >
            {creating ? 'Creating…' : 'Create Project'}
          </button>
        </div>
      </div>
    </div>
  );
};

// ─── Main Component ───────────────────────────────────────────

export const RecentProjects: React.FC = () => {
  const { createProjectWithFolder, activateWorkspace, activeProject, workspaces, panels, validators } = useCoreStore();
  const [recent, setRecent] = useState<RecentProject[]>([]);
  const [query, setQuery] = useState('');
  const [pinnedOnly, setPinnedOnly] = useState(false);
  const [pinned, setPinned] = useState<Set<string>>(new Set());
  const [modalTemplate, setModalTemplate] = useState<typeof TEMPLATES[0] | null>(null);

  // Load real recent projects from ProjectManager via IPC
  const loadRecent = useCallback(async () => {
    try {
      const recents = await ProjectService.getRecent();
      setRecent((recents ?? []).map(r => ({
        id: r.id,
        name: r.name,
        path: r.filePath,
        lastModified: r.lastOpened,
        workspace: 'terrain',
        pinned: false,
      })));
    } catch {
      setRecent([]);
    }
  }, []);

  useEffect(() => { loadRecent(); }, [loadRecent]);

  const filtered = useMemo(() => {
    let result = recent;
    if (pinnedOnly) result = result.filter(p => pinned.has(p.id));
    if (query.trim()) {
      const q = query.toLowerCase();
      result = result.filter(p => p.name.toLowerCase().includes(q) || p.path.toLowerCase().includes(q));
    }
    return [...result].sort((a, b) => {
      const aP = pinned.has(a.id);
      const bP = pinned.has(b.id);
      if (aP !== bP) return aP ? -1 : 1;
      return b.lastModified.localeCompare(a.lastModified);
    });
  }, [recent, query, pinnedOnly, pinned]);

  const pinnedProjects = filtered.filter(p => pinned.has(p.id));
  const unpinnedProjects = filtered.filter(p => !pinned.has(p.id));

  const togglePin = useCallback((id: string) => {
    setPinned(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }, []);

  // Open the new project modal when a template is clicked
  const handleNewProject = useCallback((template: typeof TEMPLATES[0]) => {
    setModalTemplate(template);
  }, []);

  // Actually create the project — called from the modal
  const handleCreateProject = useCallback(async (name: string, basePath: string) => {
    if (!modalTemplate) return;
    try {
      await createProjectWithFolder(name, modalTemplate.workspace, basePath);
      await activateWorkspace(modalTemplate.workspace);
      await loadRecent();
      setModalTemplate(null);
    } catch (err) {
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Project Creation Failed',
        message: err instanceof Error ? err.message : 'Could not create the project folder.',
        timeout: 5000,
      });
      throw err; // re-throw so modal shows the error
    }
  }, [modalTemplate, createProjectWithFolder, activateWorkspace, loadRecent]);

  const handleOpenProject = useCallback(async (project: RecentProject) => {
    try {
      await useCoreStore.getState().openProject(project.path);
      await activateWorkspace(project.workspace);
      await loadRecent();
    } catch (err) {
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Open Project Failed',
        message: err instanceof Error ? err.message : 'Could not open the project.',
        timeout: 5000,
      });
    }
  }, [activateWorkspace, loadRecent]);

  const handleOpenFile = useCallback(async () => {
    try {
      const filePath = await Dialog.loadProject();
      if (!filePath) return;
      await useCoreStore.getState().openProject(filePath);
      await loadRecent();
    } catch (err) {
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Open File Failed',
        message: err instanceof Error ? err.message : 'Could not open the file.',
        timeout: 5000,
      });
    }
  }, [loadRecent]);

  const moduleCount = workspaces?.length ?? 0;
  const panelCount = panels?.length ?? 0;
  const validatorCount = validators?.length ?? 0;

  return (
    <div className="flex flex-col h-full bg-surface-base overflow-auto">
      {/* ─── Welcome Header ──────────────────────────────────── */}
      <div className="px-8 py-8 border-b border-edge bg-gradient-to-b from-surface-panel to-surface-base">
        <div className="flex items-center gap-4 mb-2">
          <img src="./logo/logo.png" alt="OpenGeoStudio" className="w-12 h-12 rounded-lg" />
          <div>
            <h1 className="text-xl font-bold text-fg-primary">OpenGeoStudio</h1>
            <p className="text-2xs text-fg-secondary">Modular Geospatial Terrain Studio</p>
          </div>
          <span className="ml-auto text-3xs px-2 py-1 bg-accent/20 text-accent rounded font-mono">
            v{APP_VERSION}
          </span>
        </div>
        {activeProject && (
          <div className="mt-3 flex items-center gap-2 text-2xs text-accent">
            <CheckCircle size={14} />
            <span>Active project: <strong className="font-semibold">{activeProject.name}</strong></span>
          </div>
        )}
      </div>

      <div className="px-8 py-6 space-y-8 max-w-[900px] mx-auto w-full">
        {/* ─── Templates ────────────────────────────────────── */}
        <section>
          <h2 className="text-2xs font-semibold uppercase tracking-wider text-fg-secondary mb-3">
            Create New Project
          </h2>
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-3">
            {TEMPLATES.map(t => (
              <TemplateCard key={t.id} template={t} onClick={() => handleNewProject(t)} />
            ))}
          </div>
        </section>

        {/* ─── Quick Actions ────────────────────────────────── */}
        <section>
          <h2 className="text-2xs font-semibold uppercase tracking-wider text-fg-secondary mb-3">
            Quick Actions
          </h2>
          <div className="flex flex-wrap gap-2">
            <QuickAction icon={FilePlus} label="New Project" onClick={() => handleNewProject(TEMPLATES[0])} />
            <QuickAction icon={FolderOpen} label="Open File…" onClick={handleOpenFile} />
          </div>
        </section>

        {/* ─── Pinned Projects ──────────────────────────────── */}
        {pinnedProjects.length > 0 && (
          <section>
            <h2 className="flex items-center gap-2 text-2xs font-semibold uppercase tracking-wider text-fg-secondary mb-3">
              <Star size={12} className="text-warn" fill="currentColor" />
              Pinned
            </h2>
            <div className="space-y-0.5">
              {pinnedProjects.map(project => (
                <ProjectRow
                  key={project.id}
                  project={project}
                  isPinned={true}
                  onOpen={() => handleOpenProject(project)}
                  onTogglePin={() => togglePin(project.id)}
                />
              ))}
            </div>
          </section>
        )}

        {/* ─── Recent Projects ──────────────────────────────── */}
        <section>
          <div className="flex items-center justify-between mb-3">
            <h2 className="flex items-center gap-2 text-2xs font-semibold uppercase tracking-wider text-fg-secondary">
              <Clock size={12} />
              Recent Projects
            </h2>
            {recent.length > 0 && (
              <div className="flex items-center gap-2">
                <div className="flex items-center gap-1.5 px-2.5 py-1 bg-surface-panel border border-edge rounded-md">
                  <Search size={12} className="text-fg-muted" />
                  <input
                    type="text"
                    value={query}
                    onChange={(e) => setQuery(e.target.value)}
                    placeholder="Search…"
                    className="bg-transparent text-2xs text-fg-primary placeholder-fg-muted outline-none w-32"
                    aria-label="Search projects"
                  />
                </div>
                <button
                  onClick={() => setPinnedOnly(p => !p)}
                  className={`icon-btn icon-btn-sm ${pinnedOnly ? 'icon-btn-active' : ''}`}
                  aria-label="Show pinned only"
                  title="Pinned only"
                >
                  <Pin size={12} />
                </button>
              </div>
            )}
          </div>
          {unpinnedProjects.length === 0 && pinnedProjects.length === 0 ? (
            <div className="rounded-lg border border-dashed border-edge p-8">
              <EmptyState
                icon={FileText}
                title="No projects yet"
                description="Create a new project from a template above, or open an existing .ogproj file."
              />
            </div>
          ) : unpinnedProjects.length === 0 ? (
            <div className="text-2xs text-fg-muted py-4 text-center">No recent projects match your search</div>
          ) : (
            <div className="space-y-0.5">
              {unpinnedProjects.map(project => (
                <ProjectRow
                  key={project.id}
                  project={project}
                  isPinned={false}
                  onOpen={() => handleOpenProject(project)}
                  onTogglePin={() => togglePin(project.id)}
                />
              ))}
            </div>
          )}
        </section>

        {/* ─── System Status ────────────────────────────────── */}
        <section>
          <h2 className="text-2xs font-semibold uppercase tracking-wider text-fg-secondary mb-3">
            System Status
          </h2>
          <div className="grid grid-cols-2 lg:grid-cols-4 gap-3">
            <StatusCard icon={Layers} label="Workspaces" value={moduleCount} />
            <StatusCard icon={Box} label="Panels" value={panelCount} />
            <StatusCard icon={CheckCircle} label="Validators" value={validatorCount} />
            <StatusCard icon={Cpu} label="Version" value={APP_VERSION} isText />
          </div>
        </section>
      </div>

      {/* ─── New Project Modal ────────────────────────────────── */}
      {modalTemplate && (
        <NewProjectModal
          template={modalTemplate}
          onClose={() => setModalTemplate(null)}
          onCreate={handleCreateProject}
        />
      )}
    </div>
  );
};

// ─── Status Card ──────────────────────────────────────────────

const StatusCard: React.FC<{
  icon: React.ComponentType<{ size?: number; className?: string }>;
  label: string;
  value: number | string;
  isText?: boolean;
}> = ({ icon: Icon, label, value, isText }) => (
  <div className="flex items-center gap-3 p-3 bg-surface-panel border border-edge rounded-lg">
    <div className="w-8 h-8 rounded bg-surface-elevated border border-edge flex items-center justify-center">
      <Icon size={14} className="text-accent" />
    </div>
    <div>
      <div className="text-3xs text-fg-muted uppercase tracking-wider">{label}</div>
      <div className="text-sm font-semibold text-fg-primary tabular-nums">
        {isText ? value : value}
      </div>
    </div>
  </div>
);

export default RecentProjects;
