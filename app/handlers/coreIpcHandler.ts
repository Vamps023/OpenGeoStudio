/**
 * Core IPC Handler — bridges core framework services to the renderer.
 *
 * Exposes JobSystem, NotificationManager, CommandRegistry, SelectionManager,
 * ProjectManager, WorkspaceManager, and ContributionRegistry via IPC.
 */

import { ipcMain, BrowserWindow } from 'electron';
import type { AppContext } from '../../core/interfaces';
import {
  JOB_SUBMIT, JOB_CANCEL, JOB_GET, JOB_GET_ALL, JOB_PROGRESS_UPDATE,
  NOTIFICATION_SHOW, NOTIFICATION_DISMISS, NOTIFICATION_GET_ALL,
  COMMAND_EXECUTE, COMMAND_GET_ALL, COMMAND_GET_BY_CATEGORY,
  SELECTION_GET, SELECTION_SELECT, SELECTION_DESELECT, SELECTION_DESELECT_ALL, SELECTION_CHANGED,
  WORKSPACE_GET_ALL, WORKSPACE_ACTIVATE, WORKSPACE_GET_ACTIVE, WORKSPACE_ACTIVATED,
  PROJECT_CREATE, PROJECT_CREATE_WITH_FOLDER, PROJECT_OPEN, PROJECT_SAVE, PROJECT_SAVE_AS, PROJECT_CLOSE, PROJECT_MARK_DIRTY, PROJECT_GET_ACTIVE, PROJECT_CHANGED,
  PROJECT_GET_RECENT, PROJECT_CLEAR_RECENT, PROJECT_GET_SUBFOLDER, PROJECT_GET_EXPORT_PATH,
  PROJECT_AUTOSAVE, PROJECT_SET_RECENT_FILE,
  PROJECT_CONTEXT_SYNC_TERRAIN, PROJECT_CONTEXT_SYNC_SCENE,
  PROJECT_CONTEXT_SYNC_VIEWPORT, PROJECT_CONTEXT_SYNC_ASSETS, PROJECT_CONTEXT_SYNC_LAYER_VISIBILITY,
  PROJECT_CONTEXT_GET_STATE, PROJECT_CONTEXT_GET_STAGE, PROJECT_CONTEXT_RESTORED,
  CONTRIBUTION_GET_PANELS, CONTRIBUTION_GET_TOOLBAR, CONTRIBUTION_GET_NODES, CONTRIBUTION_GET_VALIDATORS,
  PLUGIN_GET_ALL, PLUGIN_RELOAD,
  SCENE_GET_ROOT, SCENE_GET_ALL_NODES, SCENE_GET_NODE, SCENE_ADD_NODE,
  SCENE_REMOVE_NODE, SCENE_UPDATE_NODE, SCENE_RENAME_NODE,
  SCENE_SET_VISIBLE, SCENE_SET_LOCKED, SCENE_REPARENT,
  SCENE_SELECT, SCENE_GET_SELECTION, SCENE_NODE_UPDATED,
} from '../../shared/ipcChannels-electron';
import type { Job } from '../../core/jobs/job-system';
import type { SelectionItem } from '../../core/selection/selection-manager';
import { SCENE_EVENTS } from '../../core/scene/scene-graph';
import { PROJECT_CONTEXT_EVENTS } from '../../core/project/projectContext';

export function registerCoreIpc(ctx: AppContext): void {
  const { jobs, notifications, commands, selection, project, projectContext, workspace, contributions, scene, events } = ctx;

  // ─── Safe Broadcast Helper ───────────────────────────────────
  // Sends an IPC message to all renderer windows, skipping destroyed
  // windows. Prevents EPIPE crashes when windows close mid-send.
  function broadcast(channel: string, data: unknown): void {
    try {
      for (const win of BrowserWindow.getAllWindows()) {
        if (!win.isDestroyed() && !win.webContents.isDestroyed()) {
          win.webContents.send(channel, data);
        }
      }
    } catch { /* ignore EPIPE / stream closed */ }
  }

  // ─── Console Log Forwarding ──────────────────────────────────
  // Intercept console.log/warn/error/debug and forward to renderer.
  // All writes are wrapped in try/catch to prevent EPIPE crashes when
  // the parent process or renderer window closes a pipe/stream early.
  const originalLog = console.log.bind(console);
  const originalWarn = console.warn.bind(console);
  const originalError = console.error.bind(console);
  const originalDebug = console.debug.bind(console);

  function safeWrite(fn: (...a: any[]) => void, args: any[]): void {
    try { fn(...args); } catch { /* EPIPE or stream closed — ignore */ }
  }

  function forwardToRenderer(level: string, args: any[]) {
    try {
      const message = args.map(a => typeof a === 'string' ? a : typeof a === 'object' ? JSON.stringify(a) : String(a)).join(' ');
      const entry = {
        level,
        message,
        timestamp: new Date().toISOString(),
        source: 'main',
      };
      broadcast('console:log', entry);
    } catch { /* ignore */ }
  }

  console.log = (...args: any[]) => { safeWrite(originalLog, args); forwardToRenderer('info', args); };
  console.warn = (...args: any[]) => { safeWrite(originalWarn, args); forwardToRenderer('warn', args); };
  console.error = (...args: any[]) => { safeWrite(originalError, args); forwardToRenderer('error', args); };
  console.debug = (...args: any[]) => { safeWrite(originalDebug, args); forwardToRenderer('debug', args); };

  // ─── Job System IPC ───────────────────────────────────────
  ipcMain.handle(JOB_SUBMIT, (_e, type: string, title: string, options?: Record<string, unknown>) => {
    return jobs.submit(type, title, options ?? {});
  });

  ipcMain.handle(JOB_CANCEL, (_e, jobId: string) => {
    jobs.cancel(jobId);
  });

  ipcMain.handle(JOB_GET, (_e, jobId: string): Job | undefined => {
    return jobs.getJob(jobId);
  });

  ipcMain.handle(JOB_GET_ALL, (): Job[] => {
    return jobs.getAll();
  });

  // ─── Notification IPC ─────────────────────────────────────
  ipcMain.handle(NOTIFICATION_SHOW, (_e, notif: Parameters<typeof notifications.show>[0]) => {
    return notifications.show(notif);
  });

  ipcMain.handle(NOTIFICATION_DISMISS, (_e, id: string) => {
    notifications.dismiss(id);
  });

  ipcMain.handle(NOTIFICATION_GET_ALL, () => {
    return notifications.getAll();
  });

  // ─── Command IPC ──────────────────────────────────────────
  ipcMain.handle(COMMAND_EXECUTE, async (_e, id: string, args?: Record<string, unknown>) => {
    try {
      await commands.execute(id, args ?? {});
    } catch (err) {
      console.error(`[coreIpcHandler] COMMAND_EXECUTE '${id}' failed:`, err);
      throw err;
    }
  });

  ipcMain.handle(COMMAND_GET_ALL, () => {
    return commands.getAll().map(c => ({
      id: c.id, label: c.label, category: c.category, shortcut: c.shortcut, icon: c.icon,
    }));
  });

  ipcMain.handle(COMMAND_GET_BY_CATEGORY, (_e, category: string) => {
    return commands.getByCategory(category).map(c => ({
      id: c.id, label: c.label, category: c.category, shortcut: c.shortcut, icon: c.icon,
    }));
  });

  // ─── Selection IPC ────────────────────────────────────────
  ipcMain.handle(SELECTION_GET, (): SelectionItem[] => {
    return selection.getSelection();
  });

  ipcMain.handle(SELECTION_SELECT, (_e, item: SelectionItem, additive?: boolean) => {
    selection.select(item, additive);
  });

  ipcMain.handle(SELECTION_DESELECT, (_e, id: string) => {
    selection.deselect(id);
  });

  ipcMain.handle(SELECTION_DESELECT_ALL, () => {
    selection.deselectAll();
  });

  // ─── Workspace IPC ────────────────────────────────────────
  ipcMain.handle(WORKSPACE_GET_ALL, () => {
    return workspace.getAll();
  });

  ipcMain.handle(WORKSPACE_ACTIVATE, (_e, id: string) => {
    workspace.activate(id);
  });

  ipcMain.handle(WORKSPACE_GET_ACTIVE, () => {
    return workspace.getActive();
  });

  // ─── Project IPC ──────────────────────────────────────────
  ipcMain.handle(PROJECT_CREATE, (_e, name: string, workspaceId: string) => {
    return project.create(name, workspaceId);
  });

  ipcMain.handle(PROJECT_CREATE_WITH_FOLDER, async (_e, name: string, workspaceId: string, basePath: string) => {
    return project.createWithFolder(name, workspaceId, basePath);
  });

  ipcMain.handle(PROJECT_OPEN, async (_e, filePath: string) => {
    return project.open(filePath);
  });

  ipcMain.handle(PROJECT_SAVE, async (_e, p: Parameters<typeof project.save>[0]) => {
    await project.save(p);
  });

  ipcMain.handle(PROJECT_SAVE_AS, async (_e, p: Parameters<typeof project.saveAs>[0], filePath: string) => {
    await project.saveAs(p, filePath);
  });

  ipcMain.handle(PROJECT_CLOSE, () => {
    project.close();
  });

  ipcMain.handle(PROJECT_MARK_DIRTY, () => {
    project.markDirty();
  });

  ipcMain.handle(PROJECT_GET_ACTIVE, () => {
    return project.getActive();
  });

  ipcMain.handle(PROJECT_GET_RECENT, () => {
    return project.getRecent();
  });

  ipcMain.handle(PROJECT_CLEAR_RECENT, () => {
    project.clearRecent();
  });

  ipcMain.handle(PROJECT_GET_SUBFOLDER, (_e, name: string) => {
    return project.getSubfolder(name);
  });

  ipcMain.handle(PROJECT_GET_EXPORT_PATH, () => {
    return project.getSubfolder('Exports');
  });

  ipcMain.handle(PROJECT_AUTOSAVE, (_e, projectData: any) => {
    return (project as any).performAutosave(projectData);
  });

  ipcMain.handle(PROJECT_SET_RECENT_FILE, (_e, filePath: string) => {
    (project as any).setRecentFilePath(filePath);
  });

  // ─── ProjectContext IPC ─────────────────────────────────────
  // Renderer → main state sync. The renderer pushes terrain/GIS/scene
  // state to ProjectContext after each workflow step. Commands read
  // from ProjectContext — no renderer args needed.

  ipcMain.handle(PROJECT_CONTEXT_SYNC_TERRAIN, (_e, metadata: any) => {
    projectContext.syncTerrain(metadata);
    return true;
  });

  // GIS sync removed

  ipcMain.handle(PROJECT_CONTEXT_SYNC_SCENE, (_e, state: any) => {
    projectContext.syncScene(state);
    return true;
  });

  ipcMain.handle(PROJECT_CONTEXT_SYNC_VIEWPORT, (_e, state: any) => {
    projectContext.syncViewport(state);
    return true;
  });

  ipcMain.handle(PROJECT_CONTEXT_SYNC_ASSETS, (_e, assets: any) => {
    projectContext.syncAssets(assets);
    return true;
  });

  ipcMain.handle(PROJECT_CONTEXT_SYNC_LAYER_VISIBILITY, (_e, state: any) => {
    projectContext.syncLayerVisibility(state);
    return true;
  });

  ipcMain.handle(PROJECT_CONTEXT_GET_STATE, () => {
    return projectContext.getState();
  });

  ipcMain.handle(PROJECT_CONTEXT_GET_STAGE, () => {
    return projectContext.getWorkflowStage();
  });

  // Broadcast ProjectContext restored event to renderer when state is loaded from disk
  events.on(PROJECT_CONTEXT_EVENTS.RESTORED, (state: any) => {
    broadcast(PROJECT_CONTEXT_RESTORED, state);
  });

  // ─── Contribution IPC ─────────────────────────────────────
  // NOTE: Contributions contain function properties (validate, execute, etc.)
  // that cannot be cloned via Electron's structured clone algorithm.
  // We must strip functions before sending over IPC.

  ipcMain.handle(CONTRIBUTION_GET_PANELS, () => {
    return contributions.getAllPanels();
  });

  ipcMain.handle(CONTRIBUTION_GET_TOOLBAR, () => {
    return contributions.getToolbar();
  });

  ipcMain.handle(CONTRIBUTION_GET_NODES, () => {
    // Strip the execute function — it cannot cross IPC
    return contributions.getAllNodes().map(n => ({
      type: n.type,
      label: n.label,
      category: n.category,
      moduleId: n.moduleId,
      inputs: n.inputs,
      outputs: n.outputs,
    }));
  });

  ipcMain.handle(CONTRIBUTION_GET_VALIDATORS, () => {
    // Strip the validate function — it cannot cross IPC
    return contributions.getAllValidators().map(v => ({
      id: v.id,
      name: v.name,
      moduleId: v.moduleId,
      targetType: v.targetType,
    }));
  });

  // ─── Plugin IPC ───────────────────────────────────────────
  ipcMain.handle(PLUGIN_GET_ALL, () => {
    // Return loaded plugins from the plugin loader
    return [];
  });

  ipcMain.handle(PLUGIN_RELOAD, async (_e, _pluginId: string) => {
    // Plugin reload will be implemented with the plugin loader
    return false;
  });

  // ─── Scene Graph IPC ───────────────────────────────────────
  ipcMain.handle(SCENE_GET_ROOT, () => {
    return scene.getRoot();
  });

  ipcMain.handle(SCENE_GET_ALL_NODES, () => {
    return scene.getAllNodes();
  });

  ipcMain.handle(SCENE_GET_NODE, (_e, id: string) => {
    return scene.getNode(id);
  });

  ipcMain.handle(SCENE_ADD_NODE, (_e, node: any, parentId?: string) => {
    return scene.add(node, parentId);
  });

  ipcMain.handle(SCENE_REMOVE_NODE, (_e, id: string) => {
    scene.remove(id);
  });

  ipcMain.handle(SCENE_UPDATE_NODE, (_e, id: string, updates: any) => {
    scene.update(id, updates);
  });

  ipcMain.handle(SCENE_RENAME_NODE, (_e, id: string, name: string) => {
    scene.rename(id, name);
  });

  ipcMain.handle(SCENE_SET_VISIBLE, (_e, id: string, visible: boolean) => {
    scene.setVisible(id, visible);
  });

  ipcMain.handle(SCENE_SET_LOCKED, (_e, id: string, locked: boolean) => {
    scene.setLocked(id, locked);
  });

  ipcMain.handle(SCENE_REPARENT, (_e, id: string, newParentId: string) => {
    scene.reparent(id, newParentId);
  });

  ipcMain.handle(SCENE_SELECT, (_e, id: string, additive?: boolean) => {
    scene.select(id, additive);
  });

  ipcMain.handle(SCENE_GET_SELECTION, () => {
    return scene.getSelected();
  });

  // Broadcast scene updates to renderer
  for (const evt of Object.values(SCENE_EVENTS)) {
    events.on(evt, (data: any) => {
      // Forward to renderer via the SCENE_NODE_UPDATED channel
      // The BrowserWindow is accessed via the events context
      broadcast(SCENE_NODE_UPDATED, { event: evt, data });
    });
  }

  // Broadcast job progress to renderer
  events.on('job:progress', (data: any) => broadcast(JOB_PROGRESS_UPDATE, data));

  // Broadcast selection changes to renderer
  events.on('selection:changed', (data: any) => broadcast(SELECTION_CHANGED, data));

  // Broadcast workspace activation to renderer
  events.on('workspace:activated', (data: any) => broadcast(WORKSPACE_ACTIVATED, data));

  // Broadcast project changes to renderer
  events.on('project:changed', (data: any) => broadcast(PROJECT_CHANGED, data));

  // Broadcast scene events to renderer (SceneBuilder → TerrainViewer3D)
  for (const evtName of ['scene:generate', 'scene:built', 'scene:build-failed']) {
    events.on(evtName, (data: any) => broadcast(evtName, data));
  }

  // Broadcast map tool/zoom events to renderer
  for (const evtName of ['map:tool', 'map:zoom', 'map:fit-selection']) {
    events.on(evtName, (data: any) => broadcast(evtName, data));
  }

  // Broadcast file project events to renderer (for file dialogs and state capture)
  for (const evtName of ['file:open-project-requested', 'file:save-project-as-requested', 'file:save-project-requested', 'file:close-project-requested', 'file:show-recent-projects']) {
    events.on(evtName, (data: any) => broadcast(evtName, data));
  }

  // Broadcast autosave request to renderer (so it can capture terrain state)
  events.on('project:autosave-requested', (data: any) => broadcast('project:autosave-requested', data));

  // Broadcast undo/redo request events to renderer
  for (const evtName of ['undo-redo:undo-requested', 'undo-redo:redo-requested']) {
    events.on(evtName, (data: any) => broadcast(evtName, data));
  }
}
