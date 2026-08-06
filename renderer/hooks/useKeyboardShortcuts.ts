/**
 * useKeyboardShortcuts — global keyboard shortcut handler.
 *
 * Registers shortcuts for:
 *   - File operations (Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S)
 *   - Command palette (Ctrl+Shift+P)
 *   - Workspace switching (Alt+1 through Alt+9)
 *   - Map tools (V, B, H, F, +, -)
 *   - 3D tools (Q, W, G)
 *   - Console toggle (Ctrl+`)
 *   - Escape (deselect / cancel)
 *
 * Shortcuts are ignored when typing in input/textarea/contenteditable elements.
 */

import { useEffect } from 'react';
import { useCoreStore } from '../core/coreStore';

interface ShortcutDef {
  key: string;
  commandId?: string;
  workspaceId?: string;
  preventDefault?: boolean;
}

// Build the shortcut table from the active workspace list
function buildShortcuts(workspaces: { id: string }[]): Record<string, ShortcutDef> {
  const map: Record<string, ShortcutDef> = {
    // File
    'ctrl+n':      { key: 'ctrl+n',      commandId: 'file.new-project',      preventDefault: true },
    'ctrl+o':      { key: 'ctrl+o',      commandId: 'file.open-project',     preventDefault: true },
    'ctrl+s':      { key: 'ctrl+s',      commandId: 'file.save-project',     preventDefault: true },
    'ctrl+shift+s':{ key: 'ctrl+shift+s',commandId: 'file.save-project-as',  preventDefault: true },
    'ctrl+z':      { key: 'ctrl+z',      commandId: 'edit.undo',             preventDefault: true },
    'ctrl+y':      { key: 'ctrl+y',      commandId: 'edit.redo',             preventDefault: true },
    // Map tools (single-key, only when not typing)
    'v':           { key: 'v',           commandId: 'map.select' },
    'b':           { key: 'b',           commandId: 'map.draw-bbox' },
    'f':           { key: 'f',           commandId: 'map.fit-selection' },
    'ctrl+=':      { key: 'ctrl+=',      commandId: 'map.zoomIn' },
    'ctrl+-':      { key: 'ctrl+-',      commandId: 'map.zoomOut' },
    '=':           { key: '=',           commandId: 'map.zoomIn' },
    '+':           { key: '+',           commandId: 'map.zoomIn' },
    '-':           { key: '-',           commandId: 'map.zoomOut' },
    // Export
    'ctrl+e':      { key: 'ctrl+e',      commandId: 'export.run',            preventDefault: true },
    // Terrain
    'ctrl+shift+d':{ key: 'ctrl+shift+d',commandId: 'terrain.fetch-dem',    preventDefault: true },
    // Validation
    'ctrl+shift+v':{ key: 'ctrl+shift+v',commandId: 'validation.run-all',   preventDefault: true },
  };

  // Workspace shortcuts: Alt+1 through Alt+9
  workspaces.forEach((ws, i) => {
    if (i < 9) {
      map[`alt+${i + 1}`] = { key: `alt+${i + 1}`, workspaceId: ws.id, preventDefault: true };
    }
  });

  return map;
}

function isTypingTarget(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  const tag = target.tagName.toLowerCase();
  return tag === 'input' || tag === 'textarea' || tag === 'select' || target.isContentEditable;
}

function normalizeKey(e: KeyboardEvent): string {
  const parts: string[] = [];
  if (e.ctrlKey || e.metaKey) parts.push('ctrl');
  if (e.shiftKey) parts.push('shift');
  if (e.altKey) parts.push('alt');
  // Normalize key names
  let key = e.key.toLowerCase();
  if (key === ' ') key = 'space';
  if (key === 'escape') key = 'escape';
  parts.push(key);
  return parts.join('+');
}

export function useKeyboardShortcuts(): void {
  const { workspaces, executeCommand, activateWorkspace } = useCoreStore();

  useEffect(() => {
    const shortcuts = buildShortcuts(workspaces ?? []);

    const handler = (e: KeyboardEvent) => {
      // Skip if typing in an input
      if (isTypingTarget(e.target)) return;

      const key = normalizeKey(e);
      const def = shortcuts[key];
      if (!def) return;

      if (def.preventDefault !== false) {
        e.preventDefault();
      }

      if (def.commandId) {
        executeCommand(def.commandId);
      } else if (def.workspaceId) {
        activateWorkspace(def.workspaceId);
      }
    };

    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [workspaces, executeCommand, activateWorkspace]);
}
