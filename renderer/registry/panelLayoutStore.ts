/**
 * Panel Layout Store — persists panel layout state per workspace.
 *
 * Remembers: dock position, size, visibility, collapsed state,
 * active tab, and split sizes. Stored in localStorage.
 *
 * This enables workspace switching to restore the exact layout
 * the user had previously configured.
 */

import { create } from 'zustand';

export interface PanelLayoutState {
  /** Which panels are visible in each dock */
  leftPanels: string[];
  rightPanels: string[];
  bottomPanels: string[];
  centerPanel: string | null;
  /** Collapsed state */
  leftCollapsed: boolean;
  rightCollapsed: boolean;
  bottomCollapsed: boolean;
  /** Dock sizes */
  leftWidth: number;
  rightWidth: number;
  bottomHeight: number;
  /** Active tab in each dock */
  activeLeft: number;
  activeRight: number;
  activeBottom: number;
}

export interface PanelLayoutStore {
  /** Layouts keyed by workspace ID */
  layouts: Record<string, PanelLayoutState>;
  /** Save a layout for a workspace */
  saveLayout: (workspaceId: string, layout: PanelLayoutState) => void;
  /** Get layout for a workspace (or null if not saved) */
  getLayout: (workspaceId: string) => PanelLayoutState | null;
  /** Clear layout for a workspace */
  clearLayout: (workspaceId: string) => void;
  /** Clear all layouts */
  clearAll: () => void;
}

const STORAGE_KEY = 'opengeostudio:panel-layouts';

function loadFromStorage(): Record<string, PanelLayoutState> {
  try {
    const data = localStorage.getItem(STORAGE_KEY);
    if (data) return JSON.parse(data);
  } catch { /* ignore */ }
  return {};
}

function saveToStorage(layouts: Record<string, PanelLayoutState>): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(layouts));
  } catch { /* ignore */ }
}

export const usePanelLayoutStore = create<PanelLayoutStore>((set, get) => ({
  layouts: loadFromStorage(),

  saveLayout: (workspaceId, layout) => {
    set(state => {
      const layouts = { ...state.layouts, [workspaceId]: layout };
      saveToStorage(layouts);
      return { layouts };
    });
  },

  getLayout: (workspaceId) => {
    return get().layouts[workspaceId] ?? null;
  },

  clearLayout: (workspaceId) => {
    set(state => {
      const layouts = { ...state.layouts };
      delete layouts[workspaceId];
      saveToStorage(layouts);
      return { layouts };
    });
  },

  clearAll: () => {
    set({ layouts: {} });
    saveToStorage({});
  },
}));
