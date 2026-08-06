/**
 * Core Commands — built-in application commands.
 *
 * These commands are registered during bootstrap and are always available
 * regardless of which modules are loaded. They cover file operations
 * (new/open/save/close project), undo/redo, validation, and workspace switching.
 *
 * Also registers framework-level panels (property-inspector, asset-browser,
 * node-graph, console, project-explorer, recent-projects, scenario-editor)
 * that are not owned by any specific module.
 */

import type { AppContext } from '../interfaces';

export function registerCoreCommands(context: AppContext): void {
  const { commands, project, undoRedo, workspace, notifications, events, contributions } = context;

  // ─── Framework Panel Registrations ─────────────────────────
  // These panels are not owned by any module, so we register them here.

  contributions.registerPanel({
    id: 'console',
    title: 'Console',
    icon: 'Terminal',
    dock: 'bottom',
    moduleId: 'core',
    component: 'renderer/registry/panels/ConsolePanel/ConsolePanel',
    defaultHeight: 180,
    defaultVisible: true,
  });

  contributions.registerPanel({
    id: 'project-explorer',
    title: 'Project Explorer',
    icon: 'FolderTree',
    dock: 'left',
    moduleId: 'core',
    component: 'renderer/panels/ProjectExplorer/ProjectExplorer',
    defaultWidth: 280,
    defaultVisible: true,
  });

  contributions.registerPanel({
    id: 'recent-projects',
    title: 'Recent Projects',
    icon: 'Clock',
    dock: 'center',
    moduleId: 'core',
    component: 'renderer/panels/RecentProjects/RecentProjects',
    defaultVisible: true,
  });

  // ─── File: Project commands ────────────────────────────────

  commands.register({
    id: 'file.new-project',
    label: 'New Project',
    category: 'File',
    icon: 'FilePlus',
    shortcut: 'Ctrl+N',
    handler: async () => {
      const activeWs = workspace.getActive();
      const p = project.create('Untitled', activeWs?.id ?? 'home');
      notifications.show({
        severity: 'success',
        title: 'Project Created',
        message: p.name,
        timeout: 2000,
        actions: [],
        dismissible: true,
      });
    },
  });

  commands.register({
    id: 'file.open-project',
    label: 'Open Project...',
    category: 'File',
    icon: 'FolderOpen',
    shortcut: 'Ctrl+O',
    handler: async () => {
      // The actual file dialog is handled by the renderer via IPC
      // This command emits an event that the renderer listens for
      events.emit('file:open-project-requested', {});
    },
  });

  commands.register({
    id: 'file.save-project',
    label: 'Save Project',
    category: 'File',
    icon: 'Save',
    shortcut: 'Ctrl+S',
    handler: async () => {
      const p = project.getActive();
      if (!p) {
        notifications.show({
          severity: 'warning',
          title: 'No Active Project',
          message: 'Create or open a project first',
          timeout: 3000,
          actions: [],
          dismissible: true,
        });
        return;
      }
      if (!p.filePath) {
        // No file path — request save-as dialog from renderer
        events.emit('file:save-project-as-requested', {});
        return;
      }
      // Emit event so renderer can capture terrain state before saving
      events.emit('file:save-project-requested', {});
    },
  });

  commands.register({
    id: 'file.save-project-as',
    label: 'Save Project As...',
    category: 'File',
    icon: 'Save',
    shortcut: 'Ctrl+Shift+S',
    handler: async () => {
      const p = project.getActive();
      if (!p) return;
      events.emit('file:save-project-as-requested', {});
    },
  });

  commands.register({
    id: 'file.close-project',
    label: 'Close Project',
    category: 'File',
    icon: 'XCircle',
    shortcut: 'Ctrl+Shift+W',
    handler: async () => {
      // Emit event so renderer can check dirty state and confirm with user
      events.emit('file:close-project-requested', {});
    },
  });

  commands.register({
    id: 'file.recent-projects',
    label: 'Recent Projects',
    category: 'File',
    icon: 'History',
    handler: async () => {
      const recent = project.getRecent();
      if (recent.length === 0) {
        notifications.show({
          severity: 'info',
          title: 'No Recent Projects',
          timeout: 2000,
          actions: [],
          dismissible: true,
        });
        return;
      }
      events.emit('file:show-recent-projects', recent);
    },
  });

  // ─── Edit: Undo/Redo ───────────────────────────────────────
  // The undo/redo commands emit events that the renderer listens for.
  // The renderer maintains its own undo-redo stack for terrain operations
  // (bounds selection, tile selection, mask settings, etc.) because the
  // terrain store lives in the renderer. The main process stack is used
  // for main-process operations (if any).

  commands.register({
    id: 'edit.undo',
    label: 'Undo',
    category: 'Edit',
    icon: 'Undo',
    shortcut: 'Ctrl+Z',
    handler: async () => {
      // Try main process stack first
      if (undoRedo.canUndo()) {
        await undoRedo.undo();
      }
      // Always emit event so renderer can undo its own stack
      events.emit('undo-redo:undo-requested', {});
    },
  });

  commands.register({
    id: 'edit.redo',
    label: 'Redo',
    category: 'Edit',
    icon: 'Redo',
    shortcut: 'Ctrl+Y',
    handler: async () => {
      if (undoRedo.canRedo()) {
        await undoRedo.redo();
      }
      events.emit('undo-redo:redo-requested', {});
    },
  });

  // ─── View: Workspace switching ─────────────────────────────

  for (const ws of workspace.getAll()) {
    commands.register({
      id: `view.workspace.${ws.id}`,
      label: `Workspace: ${ws.name}`,
      category: 'View',
      icon: ws.icon,
      handler: async () => {
        workspace.activate(ws.id);
      },
    });
  }

  // ─── View: Reset Layout ────────────────────────────────────

  commands.register({
    id: 'view.reset-layout',
    label: 'Reset Current Workspace Layout',
    category: 'View',
    icon: 'RotateCw',
    handler: async () => {
      const active = workspace.getActive();
      if (active) {
        events.emit('layout:reset', { workspaceId: active.id });
        notifications.show({
          severity: 'info',
          title: 'Layout Reset',
          message: `Workspace "${active.name}" layout has been reset to defaults.`,
          timeout: 3000,
          actions: [],
          dismissible: true,
        });
      }
    },
  });

  // ─── Help ──────────────────────────────────────────────────

  commands.register({
    id: 'help.about',
    label: 'About OpenGeoStudio',
    category: 'Help',
    icon: 'Info',
    handler: async () => {
      notifications.show({
        severity: 'info',
        title: 'OpenGeoStudio',
        message: 'Modular geospatial terrain studio v1.0.0',
        timeout: 5000,
        actions: [],
        dismissible: true,
      });
    },
  });

}
