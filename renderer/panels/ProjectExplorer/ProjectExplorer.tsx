/**
 * Project Explorer — tree view of the project's data hierarchy.
 *
 * Shows: Scene, Assets, Exports, Plugins, Layers, History.
 * Supports: rename, delete, drag-drop, right-click context menu.
 * Reacts to project and scene changes via useCoreStore.
 */

import React, { useState, useEffect, useCallback, useMemo } from 'react';
import {
  FolderTree, File, ChevronRight, ChevronDown,
  MoreVertical, Trash2, Edit3, Plus, Box, Layers, Download,
  Plug, History, Box as Scene,
  Road, Train, Mountain, Building,
} from 'lucide-react';
import { useCoreStore } from '../../core/coreStore';
import { SceneService } from '../../core/sceneService';
import type { SceneNode } from '../../../core/scene/scene-graph';
import { PanelHeader } from '../../components/common/PanelHeader';
import { ContextMenu, type ContextMenuItem } from '../../components/common/ContextMenu';

// ─── Tree node type ───────────────────────────────────────────

interface TreeNode {
  id: string;
  name: string;
  type: 'folder' | 'item';
  icon: React.ComponentType<{ size?: number; className?: string }>;
  children?: TreeNode[];
  data?: any;
  actions?: TreeAction[];
}

interface TreeAction {
  label: string;
  icon: React.ComponentType<{ size?: number }>;
  handler: () => void;
  danger?: boolean;
}

// ─── Main Component ───────────────────────────────────────────

export const ProjectExplorer: React.FC = () => {
  const { activeProject, selection, select } = useCoreStore();
  const [expandedNodes, setExpandedNodes] = useState<Set<string>>(new Set(['scene', 'assets', 'exports', 'layers', 'plugins', 'history']));
  const [contextMenu, setContextMenu] = useState<{ x: number; y: number; node: TreeNode } | null>(null);
  const [renaming, setRenaming] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState('');
  const [sceneNodes, setSceneNodes] = useState<SceneNode[]>([]);

  // Load scene nodes
  useEffect(() => {
    const loadScene = async () => {
      const nodes = await SceneService.getAllNodes();
      setSceneNodes(nodes);
    };
    loadScene();
    const unsub = SceneService.onUpdated(() => loadScene());
    return () => unsub();
  }, []);

  // Close context menu on click anywhere
  useEffect(() => {
    const handler = () => setContextMenu(null);
    if (contextMenu) {
      document.addEventListener('click', handler);
      return () => document.removeEventListener('click', handler);
    }
  }, [contextMenu]);

  const toggleExpand = useCallback((id: string) => {
    setExpandedNodes(prev => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }, []);

  const handleRename = useCallback((node: TreeNode) => {
    setRenaming(node.id);
    setRenameValue(node.name);
  }, []);

  const handleDelete = useCallback((node: TreeNode) => {
    if (node.data?.sceneNodeId) {
      SceneService.removeNode(node.data.sceneNodeId);
    }
  }, []);

  // Build tree structure
  const tree = useMemo<TreeNode>(() => {
    const sceneChildren: TreeNode[] = sceneNodes
      .filter(n => n.type !== 'root')
      .map(n => ({
        id: `scene-node-${n.id}`,
        name: n.name,
        type: 'item' as const,
        icon: n.type === 'road' ? Road : n.type === 'railway' ? Train : n.type === 'terrain' ? Mountain : Box,
        data: { sceneNodeId: n.id },
        actions: [
          { label: 'Rename', icon: Edit3, handler: () => handleRename({ id: `scene-node-${n.id}`, name: n.name, type: 'item', icon: Box, data: { sceneNodeId: n.id } }) },
          { label: 'Delete', icon: Trash2, handler: () => handleDelete({ id: `scene-node-${n.id}`, name: n.name, type: 'item', icon: Box, data: { sceneNodeId: n.id } }), danger: true },
        ],
      }));

    return {
      id: 'root',
      name: activeProject?.name ?? 'Untitled Project',
      type: 'folder',
      icon: FolderTree,
      children: [
        {
          id: 'scene',
          name: 'Scene',
          type: 'folder',
          icon: Scene,
          children: sceneChildren.length > 0 ? sceneChildren : undefined,
          actions: [
            { label: 'Add Node', icon: Plus, handler: () => {} },
          ],
        },
        {
          id: 'layers',
          name: 'Layers',
          type: 'folder',
          icon: Layers,
          children: [
            { id: 'layer-imagery', name: 'Satellite Imagery', type: 'item', icon: Layers },
            { id: 'layer-dem', name: 'DEM', type: 'item', icon: Mountain },
            { id: 'layer-roads', name: 'Roads', type: 'item', icon: Road },
            { id: 'layer-buildings', name: 'Buildings', type: 'item', icon: Building },
          ],
        },
        {
          id: 'assets',
          name: 'Assets',
          type: 'folder',
          icon: Box,
          children: [
            { id: 'asset-models', name: '3D Models', type: 'folder', icon: Box },
            { id: 'asset-textures', name: 'Textures', type: 'folder', icon: File },
            { id: 'asset-materials', name: 'Materials', type: 'folder', icon: File },
          ],
        },
        {
          id: 'exports',
          name: 'Exports',
          type: 'folder',
          icon: Download,
          children: activeProject?.filePath ? [
            { id: 'export-latest', name: activeProject.name, type: 'item', icon: File },
          ] : undefined,
        },
        {
          id: 'plugins',
          name: 'Plugins',
          type: 'folder',
          icon: Plug,
          children: [
            { id: 'plugin-sample', name: 'Sample Terrain Stats', type: 'item', icon: Plug },
          ],
        },
        {
          id: 'history',
          name: 'History',
          type: 'folder',
          icon: History,
          children: undefined,
        },
      ],
    };
  }, [activeProject, sceneNodes, handleRename, handleDelete]);

  // Render tree recursively
  const renderNode = useCallback((node: TreeNode, depth: number): React.ReactNode => {
    const isExpanded = expandedNodes.has(node.id);
    const hasChildren = node.children && node.children.length > 0;
    const isRenaming = renaming === node.id;

    return (
      <div key={node.id}>
        <div
          className={`group flex items-center gap-1 px-1 py-0.5 text-2xs cursor-pointer rounded transition-colors hover:bg-surface-hover ${
            selection.find(s => s.id === node.data?.sceneNodeId) ? 'bg-accent/15' : ''
          }`}
          style={{ paddingLeft: depth * 12 + 4 }}
          onClick={() => {
            if (hasChildren) toggleExpand(node.id);
            if (node.data?.sceneNodeId) {
              select({ id: node.data.sceneNodeId, type: 'scene-node', label: node.name });
            }
          }}
          onContextMenu={(e) => {
            e.preventDefault();
            if (node.actions && node.actions.length > 0) {
              setContextMenu({ x: e.clientX, y: e.clientY, node });
            }
          }}
        >
          {hasChildren ? (
            <button onClick={(e) => { e.stopPropagation(); toggleExpand(node.id); }} className="p-0.5" aria-label="Toggle expand">
              {isExpanded ? <ChevronDown size={10} /> : <ChevronRight size={10} />}
            </button>
          ) : (
            <span className="w-3" />
          )}
          <node.icon size={12} className="text-fg-muted shrink-0" />
          {isRenaming ? (
            <input
              type="text"
              value={renameValue}
              onChange={(e) => setRenameValue(e.target.value)}
              onBlur={() => {
                if (node.data?.sceneNodeId && renameValue.trim()) {
                  SceneService.renameNode(node.data.sceneNodeId, renameValue.trim());
                }
                setRenaming(null);
              }}
              onKeyDown={(e) => {
                if (e.key === 'Enter') (e.target as HTMLInputElement).blur();
                if (e.key === 'Escape') setRenaming(null);
              }}
              autoFocus
              className="flex-1 bg-surface-base text-fg-primary px-1 py-0 rounded border border-accent outline-none text-2xs"
            />
          ) : (
            <span className="flex-1 truncate text-fg-primary">{node.name}</span>
          )}
          {node.actions && node.actions.length > 0 && (
            <button
              onClick={(e) => { e.stopPropagation(); setContextMenu({ x: e.clientX, y: e.clientY, node }); }}
              className="opacity-0 group-hover:opacity-100 p-0.5 hover:bg-surface-hover rounded transition-opacity"
              aria-label="More actions"
            >
              <MoreVertical size={10} className="text-fg-muted" />
            </button>
          )}
        </div>
        {isExpanded && hasChildren && node.children!.map(child => renderNode(child, depth + 1))}
      </div>
    );
  }, [expandedNodes, renaming, renameValue, selection, select, toggleExpand]);

  // Build context menu items
  const contextMenuItems: ContextMenuItem[] = useMemo(() => {
    if (!contextMenu) return [];
    return (contextMenu.node.actions ?? []).map(a => ({
      label: a.label,
      icon: a.icon,
      danger: a.danger,
      onClick: () => { a.handler(); setContextMenu(null); },
    }));
  }, [contextMenu]);

  return (
    <div className="flex flex-col h-full bg-surface-panel">
      <PanelHeader
        icon={FolderTree}
        title="Project Explorer"
        description={activeProject?.name ?? 'Untitled Project'}
        actions={
          <button className="icon-btn icon-btn-sm" aria-label="Add" title="Add">
            <Plus size={12} />
          </button>
        }
      />

      {/* Tree */}
      <div className="flex-1 overflow-auto py-1" role="tree">
        {renderNode(tree, 0)}
      </div>

      {/* Context Menu */}
      {contextMenu && (
        <ContextMenu
          x={contextMenu.x}
          y={contextMenu.y}
          items={contextMenuItems}
          onClose={() => setContextMenu(null)}
        />
      )}
    </div>
  );
};

export default ProjectExplorer;
