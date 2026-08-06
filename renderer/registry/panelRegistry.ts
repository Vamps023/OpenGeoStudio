/**
 * Panel Registry — renderer-side registry for panel components.
 *
 * Each panel registers itself with an ID, a lazy-loaded component,
 * and metadata (icon, dock, workspace, etc.). The DockShell queries
 * this registry to dynamically build the dock UI.
 *
 * Panels are registered via `registerPanel()` at module load time,
 * and the registry provides `getComponent()` for lazy loading.
 *
 * This replaces the hardcoded PanelComponents map in App.tsx.
 */

import React from 'react';

// ─── Types ────────────────────────────────────────────────────

export type DockPosition = 'left' | 'right' | 'bottom' | 'center' | 'floating';

export interface PanelDefinition {
  /** Unique panel ID (matches PanelContribution.id from main process) */
  id: string;
  /** Display name */
  name: string;
  /** Icon name (lucide-react) */
  icon: string;
  /** Default dock position */
  defaultDock: DockPosition;
  /** Default width in pixels (for left/right docks) */
  defaultWidth?: number;
  /** Default height in pixels (for bottom dock) */
  defaultHeight?: number;
  /** Whether this panel is shown by default */
  defaultVisible?: boolean;
  /** Workspaces where this panel is available (empty = all) */
  workspaces?: string[];
  /** Lazy-loaded React component */
  component: React.LazyExoticComponent<React.ComponentType<any>>;
  /** Optional props to pass to the component */
  props?: Record<string, unknown>;
  /** Whether this panel can be closed by the user */
  closeable?: boolean;
  /** Whether this panel can be floated */
  floatable?: boolean;
  /** Minimum width */
  minWidth?: number;
  /** Minimum height */
  minHeight?: number;
}

export interface InspectorDefinition {
  /** Unique inspector ID */
  id: string;
  /** Scene node type this inspector handles */
  nodeType: string;
  /** Display name */
  name: string;
  /** Lazy-loaded React component */
  component: React.LazyExoticComponent<React.ComponentType<any>>;
  /** Priority (higher = preferred for the same node type) */
  priority?: number;
}

// ─── Panel Registry ───────────────────────────────────────────

class PanelRegistryImpl {
  private panels = new Map<string, PanelDefinition>();
  private inspectors = new Map<string, InspectorDefinition[]>();

  /** Register a panel */
  register(definition: PanelDefinition): void {
    if (this.panels.has(definition.id)) {
      return;
    }
    this.panels.set(definition.id, definition);
  }

  /** Unregister a panel */
  unregister(id: string): void {
    this.panels.delete(id);
  }

  /** Get a panel definition by ID */
  get(id: string): PanelDefinition | undefined {
    return this.panels.get(id);
  }

  /** Get the lazy component for a panel */
  getComponent(id: string): React.LazyExoticComponent<React.ComponentType<any>> | undefined {
    return this.panels.get(id)?.component;
  }

  /** Get all registered panels */
  getAll(): PanelDefinition[] {
    return Array.from(this.panels.values());
  }

  /** Get panels for a specific dock position */
  getByDock(dock: DockPosition): PanelDefinition[] {
    return this.getAll().filter(p => p.defaultDock === dock);
  }

  /** Get panels available in a workspace */
  getForWorkspace(workspaceId: string): PanelDefinition[] {
    return this.getAll().filter(p =>
      !p.workspaces || p.workspaces.length === 0 || p.workspaces.includes(workspaceId)
    );
  }

  /** Check if a panel is registered */
  has(id: string): boolean {
    return this.panels.has(id);
  }

  // ─── Inspector Registry ─────────────────────────────────────

  /** Register an inspector for a scene node type */
  registerInspector(def: InspectorDefinition): void {
    const existing = this.inspectors.get(def.nodeType) ?? [];
    existing.push(def);
    // Sort by priority (highest first)
    existing.sort((a, b) => (b.priority ?? 0) - (a.priority ?? 0));
    this.inspectors.set(def.nodeType, existing);
  }

  /** Get the best inspector for a scene node type */
  getInspector(nodeType: string): InspectorDefinition | undefined {
    const list = this.inspectors.get(nodeType);
    return list?.[0];
  }

  /** Get all inspectors for a scene node type */
  getInspectorsForType(nodeType: string): InspectorDefinition[] {
    return this.inspectors.get(nodeType) ?? [];
  }

  /** Get all registered inspectors */
  getAllInspectors(): InspectorDefinition[] {
    return Array.from(this.inspectors.values()).flat();
  }
}

// Singleton instance
export const panelRegistry = new PanelRegistryImpl();

// ─── Helper: Register a panel from a lazy import ──────────────

export function registerPanel(
  id: string,
  name: string,
  icon: string,
  defaultDock: DockPosition,
  loader: () => Promise<{ default: React.ComponentType<any> } | React.ComponentType<any>>,
  options?: Partial<Omit<PanelDefinition, 'id' | 'name' | 'icon' | 'defaultDock' | 'component'>>,
): void {
  panelRegistry.register({
    id,
    name,
    icon,
    defaultDock,
    component: React.lazy(loader),
    ...options,
  });
}
