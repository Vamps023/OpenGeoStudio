/**
 * Workspace Manager — manages workspaces (tool layouts for different tasks).
 *
 * A workspace defines which modules, panels, and tools are active.
 * E.g. "Terrain Export" workspace loads Terrain + GIS + Export modules
 * but not Roads or Railway. This keeps the UI focused and fast.
 */

import type { EventBus } from '../interfaces';

export interface Workspace {
  id: string;
  name: string;
  description: string;
  icon: string;
  /** Module IDs to activate in this workspace */
  modules: string[];
  /** Panels to show (by ID) */
  panels: string[];
  /** Toolbar tools to show (by command ID) */
  tools: string[];
  /** Default panel layout */
  layout?: WorkspaceLayout;
}

export interface WorkspaceLayout {
  /** Center viewport type */
  center: 'map' | 'viewer3d' | 'table' | 'split' | 'road-studio';
  /** Left dock panels */
  leftDock: string[];
  /** Right dock panels */
  rightDock: string[];
  /** Bottom dock panels */
  bottomDock: string[];
  /** Whether the left dock is collapsed */
  leftCollapsed?: boolean;
  rightCollapsed?: boolean;
  bottomCollapsed?: boolean;
}

export interface WorkspaceManager {
  register(workspace: Workspace): void;
  unregister(id: string): void;
  activate(id: string): void;
  getActive(): Workspace | undefined;
  getAll(): Workspace[];
  getById(id: string): Workspace | undefined;
}

// ─── Events ────────────────────────────────────────────────────

export const WORKSPACE_EVENTS = {
  REGISTERED: 'workspace:registered',
  ACTIVATED: 'workspace:activated',
} as const;

// ─── Default Workspaces ────────────────────────────────────────
// Production workflow workspaces — organized around world creation
// tasks, not internal modules.

export const DEFAULT_WORKSPACES: Workspace[] = [
  {
    id: 'home',
    name: 'Home',
    description: 'Start screen — recent projects, quick actions',
    icon: 'Home',
    modules: [],
    panels: ['project-explorer', 'recent-projects'],
    tools: [],
    layout: { center: 'table', leftDock: [], rightDock: [], bottomDock: [] },
  },
  {
    id: 'terrain',
    name: 'Terrain',
    description: 'Select area, download TIFF/PNG heightmaps and satellite imagery',
    icon: 'Mountain',
    modules: ['terrain', 'export'],
    panels: ['map-viewport', 'export-panel', 'job-queue'],
    tools: [],
    layout: {
      center: 'map',
      leftDock: [],
      rightDock: ['export-panel'],
      bottomDock: [],
      bottomCollapsed: true,
    },
  },
  {
    id: 'road-studio',
    name: 'Road Studio',
    description: 'Draw and edit roads in 2D/3D with Bezier pen tool and elevation control',
    icon: 'Road',
    modules: ['road-studio'],
    panels: ['road-studio-viewport'],
    tools: [],
    layout: {
      center: 'road-studio',
      leftDock: [],
      rightDock: [],
      bottomDock: [],
    },
  },
];

// ─── Implementation ────────────────────────────────────────────

export class WorkspaceManagerImpl implements WorkspaceManager {
  private workspaces = new Map<string, Workspace>();
  private activeId: string | null = null;

  constructor(private events: EventBus) {
    for (const ws of DEFAULT_WORKSPACES) {
      this.register(ws);
    }
  }

  register(workspace: Workspace): void {
    this.workspaces.set(workspace.id, workspace);
    this.events.emit(WORKSPACE_EVENTS.REGISTERED, workspace);
  }

  unregister(id: string): void {
    this.workspaces.delete(id);
    if (this.activeId === id) this.activeId = null;
  }

  activate(id: string): void {
    const ws = this.workspaces.get(id);
    if (!ws) return;
    this.activeId = id;
    this.events.emit(WORKSPACE_EVENTS.ACTIVATED, ws);
  }

  getActive(): Workspace | undefined {
    return this.activeId ? this.workspaces.get(this.activeId) : undefined;
  }

  getAll(): Workspace[] { return Array.from(this.workspaces.values()); }
  getById(id: string): Workspace | undefined { return this.workspaces.get(id); }
}
