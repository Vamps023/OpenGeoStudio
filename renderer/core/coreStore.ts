/**
 * Core Store — Zustand store for core framework state.
 *
 * This store mirrors the state of core services (WorkspaceManager, ProjectManager,
 * JobSystem, SelectionManager, CommandRegistry, ContributionRegistry) on the
 * renderer side. It is populated via IPC from the main process.
 */

import { create } from 'zustand';
import { useTerrainStore } from './store';
import { clearUndoRedo } from './undoRedoBridge';
import type { Job } from '../../core/jobs/job-system';
import type { SelectionItem } from '../../core/selection/selection-manager';
import type { Project } from '../../core/project/project-manager';
import type { Workspace } from '../../core/workspace/workspace-manager';
import type {
  PanelContribution,
  ToolbarContribution,
  NodeGraphContribution,
  ValidatorContribution,
} from '../../core/module/contributions';
import {
  JobService,
  NotificationService,
  CommandService,
  SelectionService,
  WorkspaceService,
  ProjectService,
  ContributionService,
  type CommandInfo,
} from './coreService';
import { ProjectContextIPC } from './ipc';

// Module-level flag to suppress dirty marking during state restoration
let suppressDirtyFlag = false;

interface CoreState {
  // ─── Workspaces ────────────────────────────────────────────
  workspaces: Workspace[];
  activeWorkspace: Workspace | undefined;
  loadWorkspaces: () => Promise<void>;
  activateWorkspace: (id: string) => Promise<void>;

  // ─── Project ───────────────────────────────────────────────
  activeProject: Project | undefined;
  createProject: (name: string, workspaceId: string) => Promise<Project>;
  createProjectWithFolder: (name: string, workspaceId: string, basePath: string) => Promise<Project>;
  openProject: (filePath: string) => Promise<Project>;
  saveProject: () => Promise<void>;
  saveProjectAs: (filePath: string) => Promise<void>;
  closeProject: () => Promise<void>;
  markProjectDirty: () => Promise<void>;
  loadActiveProject: () => Promise<void>;
  getProjectSubfolder: (name: string) => Promise<string | null>;
  getProjectExportPath: () => Promise<string | null>;

  // ─── Jobs ──────────────────────────────────────────────────
  jobs: Job[];
  loadJobs: () => Promise<void>;
  submitJob: (type: string, title: string, options?: Record<string, unknown>) => Promise<string>;
  cancelJob: (jobId: string) => Promise<void>;

  // ─── Selection ─────────────────────────────────────────────
  selection: SelectionItem[];
  loadSelection: () => Promise<void>;
  select: (item: SelectionItem, additive?: boolean) => Promise<void>;
  deselect: (id: string) => Promise<void>;
  deselectAll: () => Promise<void>;

  // ─── Commands ──────────────────────────────────────────────
  commands: CommandInfo[];
  loadCommands: () => Promise<void>;
  executeCommand: (id: string, args?: Record<string, unknown>) => Promise<void>;

  // ─── Contributions ─────────────────────────────────────────
  panels: PanelContribution[];
  toolbar: ToolbarContribution[];
  nodes: NodeGraphContribution[];
  validators: ValidatorContribution[];
  loadContributions: () => Promise<void>;

  // ─── Initialization ────────────────────────────────────────
  initialized: boolean;
  initialize: () => Promise<void>;
}

export const useCoreStore = create<CoreState>((set, get) => ({
  // ─── Workspaces ────────────────────────────────────────────
  workspaces: [],
  activeWorkspace: undefined,
  loadWorkspaces: async () => {
    const [all, active] = await Promise.all([
      WorkspaceService.getAll(),
      WorkspaceService.getActive(),
    ]);
    set({ workspaces: all, activeWorkspace: active });
  },
  activateWorkspace: async (id) => {
    await WorkspaceService.activate(id);
    const active = await WorkspaceService.getActive();
    set({ activeWorkspace: active });
    // Sync workspace change to ProjectContext for persistence
    ProjectContextIPC.syncViewport({ activeWorkspace: id }).catch(() => { /* non-fatal */ });
  },

  // ─── Project ───────────────────────────────────────────────
  activeProject: undefined,
  createProject: async (name, workspaceId) => {
    useTerrainStore.getState().resetAll();
    clearUndoRedo();
    try {
      const project = await ProjectService.create(name, workspaceId);
      set({ activeProject: project });
      return project;
    } catch (err) {
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Project Creation Failed',
        message: err instanceof Error ? err.message : 'Could not create project.',
        timeout: 5000,
      });
      throw err;
    }
  },
  createProjectWithFolder: async (name, workspaceId, basePath) => {
    useTerrainStore.getState().resetAll();
    clearUndoRedo();
    try {
      const project = await ProjectService.createWithFolder(name, workspaceId, basePath);
      set({ activeProject: project });
      return project;
    } catch (err) {
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Project Creation Failed',
        message: err instanceof Error ? err.message : 'Could not create project folder.',
        timeout: 5000,
      });
      throw err;
    }
  },
  openProject: async (filePath) => {
    useTerrainStore.getState().resetAll();
    clearUndoRedo();
    suppressDirtyFlag = true;
    try {
      const project = await ProjectService.open(filePath);
      useTerrainStore.getState().restoreTerrainState(project.moduleState?.terrain as Record<string, unknown> | undefined);
      set({ activeProject: project });
      // Re-enable dirty marking after state restoration settles
      setTimeout(() => { suppressDirtyFlag = false; }, 1000);
      return project;
    } catch (err) {
      suppressDirtyFlag = false;
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Open Project Failed',
        message: err instanceof Error ? err.message : 'Could not open project.',
        timeout: 5000,
      });
      throw err;
    }
  },
  saveProject: async () => {
    const { activeProject } = get();
    if (!activeProject) return;
    suppressDirtyFlag = true;
    try {
      const terrainState = useTerrainStore.getState().captureTerrainState();
      const projectToSave: typeof activeProject = {
        ...activeProject,
        moduleState: { ...activeProject.moduleState, terrain: terrainState },
        bounds: (terrainState as { selectedBounds?: typeof activeProject.bounds }).selectedBounds ?? activeProject.bounds,
      };
      await ProjectService.save(projectToSave);
      projectToSave.dirty = false;
      projectToSave.modifiedAt = new Date().toISOString();
      set({ activeProject: projectToSave });
      setTimeout(() => { suppressDirtyFlag = false; }, 500);
    } catch (err) {
      suppressDirtyFlag = false;
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Save Failed',
        message: err instanceof Error ? err.message : 'Could not save project.',
        timeout: 5000,
      });
      throw err;
    }
  },
  saveProjectAs: async (filePath) => {
    const { activeProject } = get();
    if (!activeProject) return;
    suppressDirtyFlag = true;
    try {
      const terrainState = useTerrainStore.getState().captureTerrainState();
      const projectToSave: typeof activeProject = {
        ...activeProject,
        moduleState: { ...activeProject.moduleState, terrain: terrainState },
        bounds: (terrainState as { selectedBounds?: typeof activeProject.bounds }).selectedBounds ?? activeProject.bounds,
      };
      await ProjectService.saveAs(projectToSave, filePath);
      projectToSave.dirty = false;
      projectToSave.modifiedAt = new Date().toISOString();
      set({ activeProject: projectToSave });
      setTimeout(() => { suppressDirtyFlag = false; }, 500);
    } catch (err) {
      suppressDirtyFlag = false;
      useTerrainStore.getState().addNotification({
        type: 'error',
        title: 'Save As Failed',
        message: err instanceof Error ? err.message : 'Could not save project.',
        timeout: 5000,
      });
      throw err;
    }
  },
  closeProject: async () => {
    const { activeProject } = get();
    // Warn user about unsaved changes before closing
    if (activeProject?.dirty) {
      const confirmed = window.confirm(
        `"${activeProject.name}" has unsaved changes.\n\nClose without saving?`
      );
      if (!confirmed) return;
    }
    await ProjectService.close();
    set({ activeProject: undefined });
    useTerrainStore.getState().resetAll();
    clearUndoRedo();
  },
  markProjectDirty: async () => {
    await ProjectService.markDirty();
    const { activeProject } = get();
    if (activeProject) {
      set({ activeProject: { ...activeProject, dirty: true, modifiedAt: new Date().toISOString() } });
    }
  },
  loadActiveProject: async () => {
    const project = await ProjectService.getActive();
    suppressDirtyFlag = true;
    if (project) {
      useTerrainStore.getState().restoreTerrainState(project.moduleState?.terrain as Record<string, unknown> | undefined);
    }
    set({ activeProject: project });
    setTimeout(() => { suppressDirtyFlag = false; }, 500);
  },
  getProjectSubfolder: async (name) => {
    return ProjectService.getSubfolder(name);
  },
  getProjectExportPath: async () => {
    return ProjectService.getExportPath();
  },

  // ─── Jobs ──────────────────────────────────────────────────
  jobs: [],
  loadJobs: async () => {
    const jobs = await JobService.getAll();
    set({ jobs });
  },
  submitJob: async (type, title, options) => {
    const jobId = await JobService.submit(type, title, options);
    await get().loadJobs();
    return jobId;
  },
  cancelJob: async (jobId) => {
    await JobService.cancel(jobId);
    await get().loadJobs();
  },

  // ─── Selection ─────────────────────────────────────────────
  selection: [],
  loadSelection: async () => {
    const selection = await SelectionService.get();
    set({ selection });
  },
  select: async (item, additive) => {
    await SelectionService.select(item, additive);
    await get().loadSelection();
  },
  deselect: async (id) => {
    await SelectionService.deselect(id);
    await get().loadSelection();
  },
  deselectAll: async () => {
    await SelectionService.deselectAll();
    await get().loadSelection();
  },

  // ─── Commands ──────────────────────────────────────────────
  commands: [],
  loadCommands: async () => {
    const commands = await CommandService.getAll();
    set({ commands });
  },
  executeCommand: async (id, args) => {
    try {
      // Commands read project state from ProjectContext (single source of truth).
      // The renderer does NOT inject terrain bounds or other project state into
      // command args. Every caller (toolbar, palette, hotkey, script, plugin)
      // gets the same data from the same place.
      await CommandService.execute(id, args);
    } catch (err: any) {
      // Show error toast via the terrain store's notification system
      const { addNotification } = useTerrainStore.getState();
      addNotification({
        type: 'error',
        title: 'Command Failed',
        message: `${id}: ${err?.message ?? 'Unknown error'}`,
      });
    }
  },

  // ─── Contributions ─────────────────────────────────────────
  panels: [],
  toolbar: [],
  nodes: [],
  validators: [],
  loadContributions: async () => {
    // Use allSettled so one IPC failure doesn't block initialization
    const [panels, toolbar, nodes, validators] = await Promise.all([
      ContributionService.getPanels().catch(() => []),
      ContributionService.getToolbar().catch(() => []),
      ContributionService.getNodes().catch(() => []),
      ContributionService.getValidators().catch(() => []),
    ]);
    set({
      panels: panels ?? [],
      toolbar: toolbar ?? [],
      nodes: nodes ?? [],
      validators: validators ?? [],
    });
  },

  // ─── Initialization ────────────────────────────────────────
  initialized: false,
  initialize: async () => {
    if (get().initialized) return;

    // Use allSettled so a single failure doesn't block the loading screen forever
    const results = await Promise.allSettled([
      get().loadWorkspaces(),
      get().loadActiveProject(),
      get().loadJobs(),
      get().loadSelection(),
      get().loadCommands(),
      get().loadContributions(),
    ]);

    // Log any failures but don't block initialization
    results.forEach((_r, _i) => {
      if (_r.status === 'rejected') {
        // no-op
      }
    });

    // Subscribe to live updates
    JobService.onProgressUpdate(() => { get().loadJobs(); });
    SelectionService.onChanged((selection) => { set({ selection }); });
    WorkspaceService.onActivated((workspace) => { set({ activeWorkspace: workspace }); });
    ProjectService.onChanged((project) => {
      set({ activeProject: project });
      // NOTE: Do NOT restore GIS/terrain state here — onChanged fires on
      // autosave and dirty-flag changes, which would overwrite the user's
      // current editing state with the last-saved snapshot.
      // State is only restored on explicit project open (openProject/loadActiveProject).
    });
    NotificationService.onUpdate(() => { /* notifications handled via store */ });

    // Mark project dirty when terrain editing state changes.
    // Skip transient fields (notifications, exportProgress, exportResult,
    // exportStartTime) that shouldn't trigger a save on their own.
    const editingFields: Array<keyof import('../../shared/types/terrain').AppState> = [
      'selectedBounds', 'selectedPreset', 'heightmapFormat', 'albedoFormat',
      'heightmapResolution', 'albedoResolution', 'demSource', 'imagerySource',
      'imageryZoom', 'crsSource', 'gladArdInterval', 'tileSizeKm', 'tileGrid',
      'selectedTiles', 'maskSettings', 'buildingsVisible', 'roadsVisible',
      'signsVisible', 'satelliteVisible', 'demVisible',
    ];
    let suppressDirty = true;
    suppressDirtyFlag = true;
    useTerrainStore.subscribe((state, prevState) => {
      if (suppressDirty || suppressDirtyFlag) return;
      const changed = editingFields.some(f => state[f] !== prevState[f]);
      if (!changed) return;
      const proj = get().activeProject;
      if (proj && !proj.dirty) get().markProjectDirty();
    });

    // Allow dirty-marking after the initial restore settles
    setTimeout(() => { suppressDirty = false; suppressDirtyFlag = false; }, 500);

    set({ initialized: true });
  },
}));

// Expose store on window for debugging/testing
if (typeof window !== 'undefined') {
  (window as unknown as { __CORE_STORE__?: typeof useCoreStore }).__CORE_STORE__ = useCoreStore;
}

// ─── Renderer-Side Terrain Helpers ─────────────────────────────
// These are convenience functions for UI components that need quick access
// to terrain state (e.g., the map viewport auto-fit feature).
//
// NOTE: Commands in the main process do NOT use these helpers.
// Commands read from ProjectContext (the main-process single source of truth).
// These helpers are renderer-only and read from the Zustand terrainStore.

/**
 * Get the current terrain bounding box from the terrain store.
 * Returns null if no bounds have been set (e.g. before terrain generation).
 */
export function getTerrainBounds(): import('../../shared/types/terrain').GeoBounds | null {
  return useTerrainStore.getState().selectedBounds;
}

/**
 * Get the current terrain CRS from the terrain store.
 */
export function getTerrainCRS(): string {
  return useTerrainStore.getState().crsSource;
}

/**
 * Get the current terrain tile size in km.
 */
export function getTerrainTileSizeKm(): number {
  return useTerrainStore.getState().tileSizeKm;
}

/**
 * Check whether terrain has been generated (bounds + exported package exist).
 */
export function isTerrainGenerated(): boolean {
  const state = useTerrainStore.getState();
  return state.selectedBounds != null && state.exportedPackagePath != null;
}

/**
 * Check whether GIS data has been fetched (OSM features exist).
 */
export function isGISLoaded(): boolean {
  return useTerrainStore.getState().osmFeatures != null;
}

/**
 * Check whether a 3D scene has been generated (exported manifest exists).
 */
export function isSceneGenerated(): boolean {
  return useTerrainStore.getState().exportedManifest != null;
}
