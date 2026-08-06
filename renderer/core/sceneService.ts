/**
 * Scene Service — renderer-side bridge to the SceneGraph in the main process.
 */

import {
  SCENE_GET_ROOT, SCENE_GET_ALL_NODES, SCENE_GET_NODE, SCENE_ADD_NODE,
  SCENE_REMOVE_NODE, SCENE_UPDATE_NODE, SCENE_RENAME_NODE,
  SCENE_SET_VISIBLE, SCENE_SET_LOCKED, SCENE_REPARENT,
  SCENE_SELECT, SCENE_GET_SELECTION, SCENE_NODE_UPDATED,
} from '../../shared/ipcChannels-electron';
import type { SceneNode } from '../../core/scene/scene-graph';

const isElectron = typeof window !== 'undefined' && !!(window as any).electronAPI;

let _ipcRenderer: any = null;
async function getIpcRenderer(): Promise<any> {
  if (_ipcRenderer) return _ipcRenderer;
  const api = (window as any).electronAPI;
  if (api?.ipc) {
    _ipcRenderer = api.ipc;
    return _ipcRenderer;
  }
  try {
    const electron = await import('electron');
    _ipcRenderer = electron.ipcRenderer;
  } catch { _ipcRenderer = null; }
  return _ipcRenderer;
}

async function invoke(channel: string, ...args: unknown[]): Promise<any> {
  if (!isElectron) return undefined;
  const ipc = await getIpcRenderer();
  if (!ipc) return undefined;
  return ipc.invoke(channel, ...args);
}

export const SceneService = {
  async getRoot(): Promise<SceneNode | null> {
    return invoke(SCENE_GET_ROOT);
  },

  async getAllNodes(): Promise<SceneNode[]> {
    return invoke(SCENE_GET_ALL_NODES) ?? [];
  },

  async getNode(id: string): Promise<SceneNode | undefined> {
    return invoke(SCENE_GET_NODE, id);
  },

  async addNode(node: Omit<SceneNode, 'id' | 'children' | 'parentId'>, parentId?: string): Promise<SceneNode> {
    return invoke(SCENE_ADD_NODE, node, parentId);
  },

  async removeNode(id: string): Promise<void> {
    return invoke(SCENE_REMOVE_NODE, id);
  },

  async updateNode(id: string, updates: Partial<SceneNode>): Promise<void> {
    return invoke(SCENE_UPDATE_NODE, id, updates);
  },

  async renameNode(id: string, name: string): Promise<void> {
    return invoke(SCENE_RENAME_NODE, id, name);
  },

  async setVisible(id: string, visible: boolean): Promise<void> {
    return invoke(SCENE_SET_VISIBLE, id, visible);
  },

  async setLocked(id: string, locked: boolean): Promise<void> {
    return invoke(SCENE_SET_LOCKED, id, locked);
  },

  async reparent(id: string, newParentId: string): Promise<void> {
    return invoke(SCENE_REPARENT, id, newParentId);
  },

  async select(id: string, additive?: boolean): Promise<void> {
    return invoke(SCENE_SELECT, id, additive);
  },

  async getSelection(): Promise<SceneNode[]> {
    return invoke(SCENE_GET_SELECTION) ?? [];
  },

  onUpdated(callback: () => void): () => void {
    if (!isElectron) return () => {};
    let unsub: () => void = () => {};
    getIpcRenderer().then((ipc: any) => {
      if (!ipc) return;
      const handler = () => callback();
      ipc.on(SCENE_NODE_UPDATED, handler);
      unsub = () => ipc.removeListener(SCENE_NODE_UPDATED, handler);
    });
    return () => unsub();
  },
};
