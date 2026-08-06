/**
 * Scene Graph — hierarchical object tree for the 3D viewer and scene management.
 *
 * Each SceneNode has a UUID, transform, properties, and optional children.
 * The scene graph is used by:
 * - 3D Viewer: renders the hierarchy in Babylon.js
 * - Scene Tree panel: shows the hierarchy as a tree
 * - Property Inspector: edits the selected node's properties
 * - Undo/Redo: tracks changes to the graph
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';

// ─── Types ────────────────────────────────────────────────────

export type SceneNodeType =
  | 'root'
  | 'terrain'
  | 'road'
  | 'railway'
  | 'building'
  | 'tree'
  | 'water'
  | 'sign'
  | 'signal'
  | 'group'
  | 'light'
  | 'camera'
  | 'mesh'
  | 'layer';

export interface Transform {
  position: { x: number; y: number; z: number };
  rotation: { x: number; y: number; z: number }; // Euler angles in radians
  scale: { x: number; y: number; z: number };
}

export interface PropertyDef {
  key: string;
  label: string;
  type: 'number' | 'string' | 'boolean' | 'color' | 'vec3' | 'select' | 'file';
  value: unknown;
  min?: number;
  max?: number;
  step?: number;
  options?: string[];
  /** Whether this property is read-only */
  readOnly?: boolean;
  /** Whether changing this property triggers a re-render */
  requiresRebuild?: boolean;
}

export interface SceneNode {
  /** Unique UUID */
  id: string;
  /** Display name */
  name: string;
  /** Node type */
  type: SceneNodeType;
  /** Whether the node is visible in the 3D view */
  visible: boolean;
  /** Whether the node is locked (cannot be selected/edited) */
  locked: boolean;
  /** Local transform relative to parent */
  transform: Transform;
  /** World transform (computed) */
  worldTransform?: Transform;
  /** Editable properties */
  properties: PropertyDef[];
  /** Child node IDs */
  children: string[];
  /** Parent node ID (null for root) */
  parentId: string | null;
  /** Module that created this node */
  moduleId?: string;
  /** Optional metadata */
  metadata?: Record<string, unknown>;
}

// ─── Events ───────────────────────────────────────────────────

export const SCENE_EVENTS = {
  NODE_ADDED: 'scene:node-added',
  NODE_REMOVED: 'scene:node-removed',
  NODE_UPDATED: 'scene:node-updated',
  NODE_RENAMED: 'scene:node-renamed',
  NODE_REPARENTED: 'scene:node-reparented',
  VISIBILITY_CHANGED: 'scene:visibility-changed',
  SELECTION_CHANGED: 'scene:selection-changed',
  CLEARED: 'scene:cleared',
} as const;

// ─── Scene Graph Implementation ───────────────────────────────

export class SceneGraph {
  private nodes = new Map<string, SceneNode>();
  private rootId: string;
  private selectedIds = new Set<string>();

  constructor(
    private events: EventBus,
    private logger: Logger,
  ) {
    // Create root node
    this.rootId = this.generateId();
    const root: SceneNode = {
      id: this.rootId,
      name: 'Scene',
      type: 'root',
      visible: true,
      locked: false,
      transform: { position: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 }, scale: { x: 1, y: 1, z: 1 } },
      properties: [],
      children: [],
      parentId: null,
    };
    this.nodes.set(this.rootId, root);
  }

  /** Generate a UUID v4 */
  private generateId(): string {
    if (typeof crypto !== 'undefined' && crypto.randomUUID) {
      return crypto.randomUUID();
    }
    // Fallback for Node.js without crypto.randomUUID
    return 'node-' + Math.random().toString(36).substring(2) + Date.now().toString(36);
  }

  getRoot(): SceneNode { return this.nodes.get(this.rootId)!; }
  getRootId(): string { return this.rootId; }

  getNode(id: string): SceneNode | undefined { return this.nodes.get(id); }

  getAllNodes(): SceneNode[] { return Array.from(this.nodes.values()); }

  /** Add a node as a child of a parent */
  add(node: Omit<SceneNode, 'id' | 'children' | 'parentId'>, parentId?: string): SceneNode {
    const id = this.generateId();
    const parent = this.nodes.get(parentId ?? this.rootId);
    if (!parent) {
      this.logger.error(`Parent node not found: ${parentId}`);
      throw new Error(`Parent node not found: ${parentId}`);
    }
    const fullNode: SceneNode = {
      ...node,
      id,
      children: [],
      parentId: parent.id,
    };
    this.nodes.set(id, fullNode);
    parent.children.push(id);
    this.events.emit(SCENE_EVENTS.NODE_ADDED, { node: fullNode, parentId: parent.id });
    return fullNode;
  }

  /** Remove a node and all its descendants */
  remove(id: string): void {
    const node = this.nodes.get(id);
    if (!node) return;
    if (id === this.rootId) {
      this.logger.warn('Cannot remove root node');
      return;
    }
    // Remove from parent's children
    if (node.parentId) {
      const parent = this.nodes.get(node.parentId);
      if (parent) {
        parent.children = parent.children.filter(cid => cid !== id);
      }
    }
    // Recursively remove children
    for (const childId of [...node.children]) {
      this.remove(childId);
    }
    this.nodes.delete(id);
    this.selectedIds.delete(id);
    this.events.emit(SCENE_EVENTS.NODE_REMOVED, { id });
  }

  /** Update a node's properties */
  update(id: string, updates: Partial<SceneNode>): void {
    const node = this.nodes.get(id);
    if (!node) return;
    Object.assign(node, updates);
    this.events.emit(SCENE_EVENTS.NODE_UPDATED, { id, updates });
  }

  /** Rename a node */
  rename(id: string, name: string): void {
    const node = this.nodes.get(id);
    if (!node) return;
    node.name = name;
    this.events.emit(SCENE_EVENTS.NODE_RENAMED, { id, name });
  }

  /** Set node visibility */
  setVisible(id: string, visible: boolean): void {
    const node = this.nodes.get(id);
    if (!node) return;
    node.visible = visible;
    this.events.emit(SCENE_EVENTS.VISIBILITY_CHANGED, { id, visible });
  }

  /** Lock/unlock a node */
  setLocked(id: string, locked: boolean): void {
    const node = this.nodes.get(id);
    if (!node) return;
    node.locked = locked;
  }

  /** Move a node to a new parent */
  reparent(id: string, newParentId: string): void {
    const node = this.nodes.get(id);
    if (!node || id === this.rootId) return;
    const newParent = this.nodes.get(newParentId);
    if (!newParent) return;
    // Prevent reparenting to own descendant
    if (this.isDescendant(newParentId, id)) {
      this.logger.warn('Cannot reparent node to its own descendant');
      return;
    }
    // Remove from old parent
    if (node.parentId) {
      const oldParent = this.nodes.get(node.parentId);
      if (oldParent) {
        oldParent.children = oldParent.children.filter(cid => cid !== id);
      }
    }
    // Add to new parent
    node.parentId = newParentId;
    newParent.children.push(id);
    this.events.emit(SCENE_EVENTS.NODE_REPARENTED, { id, newParentId });
  }

  /** Check if `descendantId` is a descendant of `ancestorId` */
  isDescendant(descendantId: string, ancestorId: string): boolean {
    let current = this.nodes.get(descendantId);
    while (current && current.parentId) {
      if (current.parentId === ancestorId) return true;
      current = this.nodes.get(current.parentId);
    }
    return false;
  }

  /** Get all children of a node (one level) */
  getChildren(id: string): SceneNode[] {
    const node = this.nodes.get(id);
    if (!node) return [];
    return node.children.map(cid => this.nodes.get(cid)).filter(Boolean) as SceneNode[];
  }

  /** Get all descendants of a node (recursive) */
  getDescendants(id: string): SceneNode[] {
    const result: SceneNode[] = [];
    const collect = (nid: string) => {
      const node = this.nodes.get(nid);
      if (!node) return;
      for (const cid of node.children) {
        const child = this.nodes.get(cid);
        if (child) {
          result.push(child);
          collect(cid);
        }
      }
    };
    collect(id);
    return result;
  }

  /** Get the path from root to a node */
  getPath(id: string): SceneNode[] {
    const path: SceneNode[] = [];
    let current = this.nodes.get(id);
    while (current) {
      path.unshift(current);
      current = current.parentId ? this.nodes.get(current.parentId) : undefined;
    }
    return path;
  }

  /** Selection */
  select(id: string, additive = false): void {
    if (!additive) this.selectedIds.clear();
    this.selectedIds.add(id);
    this.events.emit(SCENE_EVENTS.SELECTION_CHANGED, this.getSelected());
  }

  selectMany(ids: string[], additive = false): void {
    if (!additive) this.selectedIds.clear();
    for (const id of ids) this.selectedIds.add(id);
    this.events.emit(SCENE_EVENTS.SELECTION_CHANGED, this.getSelected());
  }

  deselect(id: string): void {
    this.selectedIds.delete(id);
    this.events.emit(SCENE_EVENTS.SELECTION_CHANGED, this.getSelected());
  }

  deselectAll(): void {
    this.selectedIds.clear();
    this.events.emit(SCENE_EVENTS.SELECTION_CHANGED, []);
  }

  isSelected(id: string): boolean { return this.selectedIds.has(id); }
  getSelected(): SceneNode[] {
    return Array.from(this.selectedIds).map(id => this.nodes.get(id)).filter(Boolean) as SceneNode[];
  }
  getSelectedIds(): string[] { return Array.from(this.selectedIds); }

  /** Clear all nodes except root */
  clear(): void {
    const root = this.nodes.get(this.rootId);
    if (!root) return;
    // Remove all children of root
    for (const childId of [...root.children]) {
      this.remove(childId);
    }
    this.selectedIds.clear();
    this.events.emit(SCENE_EVENTS.CLEARED, {});
  }

  /** Serialize to JSON */
  serialize(): string {
    return JSON.stringify(this.serializeToObject(), null, 2);
  }

  serializeToObject(): object {
    return {
      rootId: this.rootId,
      nodes: Array.from(this.nodes.values()),
    };
  }

  /** Deserialize from JSON */
  deserialize(json: string): void {
    const data = JSON.parse(json) as { rootId: string; nodes: SceneNode[] };
    this.nodes.clear();
    this.rootId = data.rootId;
    for (const node of data.nodes) {
      this.nodes.set(node.id, node);
    }
    this.selectedIds.clear();
  }
}

// ─── Helper: Default transform ────────────────────────────────

export function defaultTransform(): Transform {
  return {
    position: { x: 0, y: 0, z: 0 },
    rotation: { x: 0, y: 0, z: 0 },
    scale: { x: 1, y: 1, z: 1 },
  };
}
