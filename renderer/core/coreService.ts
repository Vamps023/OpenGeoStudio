/**
 * Core Service Client — renderer-side bridge to core framework services.
 *
 * This module provides typed wrappers around the IPC channels exposed by
 * coreIpcHandler.ts in the main process. It allows the renderer to interact
 * with JobSystem, NotificationManager, CommandRegistry, SelectionManager,
 * ProjectManager, WorkspaceManager, and ContributionRegistry.
 *
 * When running outside Electron (web dev mode), it provides in-memory stubs.
 */

import {
  JOB_SUBMIT, JOB_CANCEL, JOB_GET, JOB_GET_ALL, JOB_PROGRESS_UPDATE,
  NOTIFICATION_SHOW, NOTIFICATION_DISMISS, NOTIFICATION_GET_ALL, NOTIFICATION_UPDATE,
  COMMAND_EXECUTE, COMMAND_GET_ALL, COMMAND_GET_BY_CATEGORY,
  SELECTION_GET, SELECTION_SELECT, SELECTION_DESELECT, SELECTION_DESELECT_ALL, SELECTION_CHANGED,
  WORKSPACE_GET_ALL, WORKSPACE_ACTIVATE, WORKSPACE_GET_ACTIVE, WORKSPACE_ACTIVATED,
  PROJECT_CREATE, PROJECT_CREATE_WITH_FOLDER, PROJECT_OPEN, PROJECT_SAVE, PROJECT_SAVE_AS, PROJECT_CLOSE, PROJECT_MARK_DIRTY, PROJECT_GET_ACTIVE, PROJECT_CHANGED,
  PROJECT_GET_RECENT, PROJECT_CLEAR_RECENT, PROJECT_GET_SUBFOLDER, PROJECT_GET_EXPORT_PATH,
  PROJECT_AUTOSAVE, PROJECT_SET_RECENT_FILE,
  CONTRIBUTION_GET_PANELS, CONTRIBUTION_GET_TOOLBAR, CONTRIBUTION_GET_NODES, CONTRIBUTION_GET_VALIDATORS,
  PLUGIN_GET_ALL, PLUGIN_RELOAD,
} from '../../shared/ipcChannels-electron';
import type { Job, JobProgress } from '../../core/jobs/job-system';
import type { SelectionItem } from '../../core/selection/selection-manager';
import type { Project, RecentProject } from '../../core/project/project-manager';
import type { Workspace } from '../../core/workspace/workspace-manager';
import type {
  PanelContribution,
  ToolbarContribution,
  NodeGraphContribution,
  ValidatorContribution,
} from '../../core/module/contributions';

// Access ipcRenderer via window.electronAPI (exposed by preload) or via electron import
// In the Vite renderer build, we use a lazy import that works in both contexts
let _ipcRenderer: any = null;
async function getIpcRenderer(): Promise<any> {
  if (_ipcRenderer) return _ipcRenderer;
  // Use the preload-exposed ipc bridge (works with contextIsolation: true, nodeIntegration: false)
  const api = (window as any).electronAPI;
  if (api?.ipc) {
    _ipcRenderer = api.ipc;
    return _ipcRenderer;
  }
  // Fallback: try direct import (only works with nodeIntegration: true)
  try {
    const electron = await import('electron');
    _ipcRenderer = electron.ipcRenderer;
  } catch {
    _ipcRenderer = null;
  }
  return _ipcRenderer;
}

const isElectron = typeof window !== 'undefined' && !!(window as any).electronAPI;

async function invoke(channel: string, ...args: unknown[]): Promise<any> {
  if (!isElectron) return undefined;
  const ipc = await getIpcRenderer();
  if (!ipc) return undefined;
  return ipc.invoke(channel, ...args);
}

async function on(channel: string, callback: (data: any) => void): Promise<() => void> {
  if (!isElectron) return () => {};
  const ipc = await getIpcRenderer();
  if (!ipc) return () => {};
  const handler = (_e: unknown, data: any) => callback(data);
  ipc.on(channel, handler);
  return () => ipc.removeListener(channel, handler);
}

// ─── Job System ──────────────────────────────────────────────

export const JobService = {
  async submit(type: string, title: string, options?: Record<string, unknown>): Promise<string> {
    if (!isElectron) return `job-${Date.now()}`;
    return invoke(JOB_SUBMIT, type, title, options);
  },

  async cancel(jobId: string): Promise<void> {
    if (!isElectron) return;
    return invoke(JOB_CANCEL, jobId);
  },

  async get(jobId: string): Promise<Job | undefined> {
    if (!isElectron) return undefined;
    return invoke(JOB_GET, jobId);
  },

  async getAll(): Promise<Job[]> {
    if (!isElectron) return [];
    return invoke(JOB_GET_ALL);
  },

  onProgressUpdate(callback: (data: { jobId: string; progress: JobProgress }) => void): () => void {
    if (!isElectron) return () => {};
    let unsub: () => void = () => {};
    on(JOB_PROGRESS_UPDATE, callback as any).then((u) => { unsub = u; });
    return () => unsub();
  },
};

// ─── Notifications ───────────────────────────────────────────

export const NotificationService = {
  async show(notif: {
    severity: 'info' | 'success' | 'warning' | 'error';
    title: string;
    message?: string;
    timeout: number;
    actions: Array<{ label: string; handler: () => void }>;
    dismissible: boolean;
  }): Promise<string> {
    if (!isElectron) return `notif-${Date.now()}`;
    return invoke(NOTIFICATION_SHOW, notif);
  },

  async dismiss(id: string): Promise<void> {
    if (!isElectron) return;
    return invoke(NOTIFICATION_DISMISS, id);
  },

  async getAll(): Promise<Array<{ id: string; severity: string; title: string; message?: string }>> {
    if (!isElectron) return [];
    return invoke(NOTIFICATION_GET_ALL);
  },

  onUpdate(callback: (notif: { id: string; severity: string; title: string }) => void): () => void {
    if (!isElectron) return () => {};
    let unsub: () => void = () => {};
    on(NOTIFICATION_UPDATE, callback as any).then((u) => { unsub = u; });
    return () => unsub();
  },
};

// ─── Commands ────────────────────────────────────────────────

export interface CommandInfo {
  id: string;
  label?: string;
  category?: string;
  shortcut?: string;
  icon?: string;
}

export const CommandService = {
  async execute(id: string, args?: Record<string, unknown>): Promise<void> {
    if (!isElectron) return;
    return invoke(COMMAND_EXECUTE, id, args);
  },

  async getAll(): Promise<CommandInfo[]> {
    if (!isElectron) return [];
    return invoke(COMMAND_GET_ALL);
  },

  async getByCategory(category: string): Promise<CommandInfo[]> {
    if (!isElectron) return [];
    return invoke(COMMAND_GET_BY_CATEGORY, category);
  },
};

// ─── Selection ───────────────────────────────────────────────

export const SelectionService = {
  async get(): Promise<SelectionItem[]> {
    if (!isElectron) return [];
    return invoke(SELECTION_GET);
  },

  async select(item: SelectionItem, additive?: boolean): Promise<void> {
    if (!isElectron) return;
    return invoke(SELECTION_SELECT, item, additive);
  },

  async deselect(id: string): Promise<void> {
    if (!isElectron) return;
    return invoke(SELECTION_DESELECT, id);
  },

  async deselectAll(): Promise<void> {
    if (!isElectron) return;
    return invoke(SELECTION_DESELECT_ALL);
  },

  onChanged(callback: (selection: SelectionItem[]) => void): () => void {
    if (!isElectron) return () => {};
    let unsub: () => void = () => {};
    on(SELECTION_CHANGED, callback as any).then((u) => { unsub = u; });
    return () => unsub();
  },
};

// ─── Workspace ───────────────────────────────────────────────

export const WorkspaceService = {
  async getAll(): Promise<Workspace[]> {
    if (!isElectron) return [];
    return invoke(WORKSPACE_GET_ALL);
  },

  async activate(id: string): Promise<void> {
    if (!isElectron) return;
    return invoke(WORKSPACE_ACTIVATE, id);
  },

  async getActive(): Promise<Workspace | undefined> {
    if (!isElectron) return undefined;
    return invoke(WORKSPACE_GET_ACTIVE);
  },

  onActivated(callback: (workspace: Workspace) => void): () => void {
    if (!isElectron) return () => {};
    let unsub: () => void = () => {};
    on(WORKSPACE_ACTIVATED, callback as any).then((u) => { unsub = u; });
    return () => unsub();
  },
};

// ─── Project ─────────────────────────────────────────────────

export const ProjectService = {
  async create(name: string, workspaceId: string): Promise<Project> {
    if (!isElectron) return {
      id: `project-${Date.now()}`,
      name,
      createdAt: new Date().toISOString(),
      modifiedAt: new Date().toISOString(),
      workspaceId,
      moduleState: {},
      dirty: false,
    };
    return invoke(PROJECT_CREATE, name, workspaceId);
  },

  async createWithFolder(name: string, workspaceId: string, basePath: string): Promise<Project> {
    if (!isElectron) return {
      id: `project-${Date.now()}`,
      name,
      basePath,
      filePath: `${basePath}/${name.replace(/[^a-zA-Z0-9_-]/g, '_')}.ogproj`,
      createdAt: new Date().toISOString(),
      modifiedAt: new Date().toISOString(),
      workspaceId,
      moduleState: {},
      dirty: false,
    };
    return invoke(PROJECT_CREATE_WITH_FOLDER, name, workspaceId, basePath);
  },

  async open(filePath: string): Promise<Project> {
    if (!isElectron) throw new Error('Cannot open project in web mode');
    return invoke(PROJECT_OPEN, filePath);
  },

  async save(project: Project): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_SAVE, project);
  },

  async saveAs(project: Project, filePath: string): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_SAVE_AS, project, filePath);
  },

  async close(): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_CLOSE);
  },

  async markDirty(): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_MARK_DIRTY);
  },

  async getActive(): Promise<Project | undefined> {
    if (!isElectron) return undefined;
    return invoke(PROJECT_GET_ACTIVE);
  },

  async getRecent(): Promise<RecentProject[]> {
    if (!isElectron) return [];
    return invoke(PROJECT_GET_RECENT);
  },

  async clearRecent(): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_CLEAR_RECENT);
  },

  async getSubfolder(name: string): Promise<string | null> {
    if (!isElectron) return null;
    return invoke(PROJECT_GET_SUBFOLDER, name);
  },

  async getExportPath(): Promise<string | null> {
    if (!isElectron) return null;
    return invoke(PROJECT_GET_EXPORT_PATH);
  },

  async autosave(project: Project): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_AUTOSAVE, project);
  },

  async setRecentFilePath(filePath: string): Promise<void> {
    if (!isElectron) return;
    return invoke(PROJECT_SET_RECENT_FILE, filePath);
  },

  onChanged(callback: (project: Project | undefined) => void): () => void {
    if (!isElectron) return () => {};
    let unsub: () => void = () => {};
    on(PROJECT_CHANGED, callback as any).then((u) => { unsub = u; });
    return () => unsub();
  },
};

// ─── Contributions ───────────────────────────────────────────

export const ContributionService = {
  async getPanels(): Promise<PanelContribution[]> {
    if (!isElectron) return [];
    return invoke(CONTRIBUTION_GET_PANELS);
  },

  async getToolbar(): Promise<ToolbarContribution[]> {
    if (!isElectron) return [];
    return invoke(CONTRIBUTION_GET_TOOLBAR);
  },

  async getNodes(): Promise<NodeGraphContribution[]> {
    if (!isElectron) return [];
    return invoke(CONTRIBUTION_GET_NODES);
  },

  async getValidators(): Promise<ValidatorContribution[]> {
    if (!isElectron) return [];
    return invoke(CONTRIBUTION_GET_VALIDATORS);
  },
};

// ─── Plugins ─────────────────────────────────────────────────

export const PluginService = {
  async getAll(): Promise<Array<{ id: string; name: string; version: string }>> {
    if (!isElectron) return [];
    return invoke(PLUGIN_GET_ALL);
  },

  async reload(pluginId: string): Promise<boolean> {
    if (!isElectron) return false;
    return invoke(PLUGIN_RELOAD, pluginId);
  },
};
