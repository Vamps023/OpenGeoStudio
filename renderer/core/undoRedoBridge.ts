/**
 * Undo/Redo Bridge — renderer-side undo-redo stack for terrain operations.
 *
 * The main process has its own UndoRedoStack for main-process operations.
 * This bridge creates a renderer-side stack that handles terrain store
 * changes (bounds selection, tile selection, mask settings, etc.).
 *
 * When the user presses Ctrl+Z, the main process `edit.undo` command
 * emits `undo-redo:undo-requested` via IPC. This bridge listens for that
 * event and calls undo on the renderer-side stack.
 */

import { useEffect } from 'react';
import { useTerrainStore } from './store';
import { getIpcBridge } from './ipc';
import type { GeoBounds, MaskSettings, DEMSource, ImagerySource, ExportPreset, HeightmapFormat, AlbedoFormat, CRSSource } from '../../shared/types/terrain';

// ─── Simple renderer-side undo stack ───────────────────────────

interface UndoAction {
  label: string;
  undo(): void;
  redo(): void;
}

let undoStack: UndoAction[] = [];
let redoStack: UndoAction[] = [];
const MAX_STACK = 100;

const listeners: (() => void)[] = [];

function notifyListeners() {
  for (const l of listeners) l();
}

export function canUndo(): boolean { return undoStack.length > 0; }
export function canRedo(): boolean { return redoStack.length > 0; }
export function getUndoLabel(): string | undefined { return undoStack[undoStack.length - 1]?.label; }
export function getRedoLabel(): string | undefined { return redoStack[redoStack.length - 1]?.label; }

export function pushUndo(action: UndoAction): void {
  undoStack.push(action);
  redoStack = [];
  if (undoStack.length > MAX_STACK) undoStack.shift();
  notifyListeners();
}

export async function undo(): Promise<void> {
  const action = undoStack.pop();
  if (!action) return;
  action.undo();
  redoStack.push(action);
  notifyListeners();
}

export async function redo(): Promise<void> {
  const action = redoStack.pop();
  if (!action) return;
  action.redo();
  undoStack.push(action);
  notifyListeners();
}

export function clearUndoRedo(): void {
  undoStack = [];
  redoStack = [];
  notifyListeners();
}

export function subscribeToUndoRedoChanges(listener: () => void): () => void {
  listeners.push(listener);
  return () => {
    const idx = listeners.indexOf(listener);
    if (idx >= 0) listeners.splice(idx, 1);
  };
}

// ─── Helper: create undo action for a state change ─────────────

function createAction<T>(
  label: string,
  oldValue: T,
  newValue: T,
  apply: (value: T) => void,
): UndoAction {
  return {
    label,
    undo: () => apply(oldValue),
    redo: () => apply(newValue),
  };
}

// ─── Wrapped terrain store setters that push undo actions ──────

export function setBoundsWithUndo(bounds: GeoBounds | null): void {
  const store = useTerrainStore.getState();
  const oldBounds = store.selectedBounds;
  store.setSelectedBounds(bounds);
  if (oldBounds !== bounds) {
    pushUndo(createAction('Set Selection Bounds', oldBounds, bounds, (v) => {
      useTerrainStore.getState().setSelectedBounds(v);
    }));
  }
}

export function toggleTileWithUndo(row: number, col: number): void {
  const store = useTerrainStore.getState();
  const key = `${row},${col}`;
  const hadKey = store.selectedTiles.has(key);
  const oldSet = new Set(store.selectedTiles);
  store.toggleTileSelection(row, col);
  pushUndo({
    label: hadKey ? 'Deselect Tile' : 'Select Tile',
    undo: () => { useTerrainStore.getState().setSelectedTiles(oldSet); },
    redo: () => { useTerrainStore.getState().toggleTileSelection(row, col); },
  });
}

export function setMaskSettingsWithUndo(settings: Partial<MaskSettings>): void {
  const store = useTerrainStore.getState();
  const oldSettings = { ...store.maskSettings };
  store.setMaskSettings(settings);
  pushUndo({
    label: 'Change Mask Settings',
    undo: () => { useTerrainStore.getState().setMaskSettings(oldSettings); },
    redo: () => { useTerrainStore.getState().setMaskSettings(settings); },
  });
}

export function setTileSizeWithUndo(sizeKm: number): void {
  const store = useTerrainStore.getState();
  const oldSize = store.tileSizeKm;
  store.setTileSizeKm(sizeKm);
  pushUndo(createAction('Change Tile Size', oldSize, sizeKm, (v) => {
    useTerrainStore.getState().setTileSizeKm(v);
  }));
}

export function setDEMSourceWithUndo(source: DEMSource): void {
  const store = useTerrainStore.getState();
  const oldSource = store.demSource;
  store.setDEMSource(source);
  pushUndo(createAction('Change DEM Source', oldSource, source, (v) => {
    useTerrainStore.getState().setDEMSource(v);
  }));
}

export function setImagerySourceWithUndo(source: ImagerySource): void {
  const store = useTerrainStore.getState();
  const oldSource = store.imagerySource;
  store.setImagerySource(source);
  pushUndo(createAction('Change Imagery Source', oldSource, source, (v) => {
    useTerrainStore.getState().setImagerySource(v);
  }));
}

export function setPresetWithUndo(preset: ExportPreset): void {
  const store = useTerrainStore.getState();
  const oldPreset = store.selectedPreset;
  const oldHm = store.heightmapFormat;
  const oldAlb = store.albedoFormat;
  const oldHmRes = store.heightmapResolution;
  const oldAlbRes = store.albedoResolution;
  store.setSelectedPreset(preset);
  pushUndo({
    label: 'Change Engine Preset',
    undo: () => {
      const s = useTerrainStore.getState();
      s.setSelectedPreset(oldPreset);
      s.setHeightmapFormat(oldHm);
      s.setAlbedoFormat(oldAlb);
      s.setHeightmapResolution(oldHmRes);
      s.setAlbedoResolution(oldAlbRes);
    },
    redo: () => {
      const s = useTerrainStore.getState();
      s.setSelectedPreset(preset);
    },
  });
}

export function setHeightmapFormatWithUndo(format: HeightmapFormat): void {
  const store = useTerrainStore.getState();
  const old = store.heightmapFormat;
  store.setHeightmapFormat(format);
  pushUndo(createAction('Change Heightmap Format', old, format, (v) => {
    useTerrainStore.getState().setHeightmapFormat(v);
  }));
}

export function setAlbedoFormatWithUndo(format: AlbedoFormat): void {
  const store = useTerrainStore.getState();
  const old = store.albedoFormat;
  store.setAlbedoFormat(format);
  pushUndo(createAction('Change Albedo Format', old, format, (v) => {
    useTerrainStore.getState().setAlbedoFormat(v);
  }));
}

export function setCRSWithUndo(crs: CRSSource): void {
  const store = useTerrainStore.getState();
  const old = store.crsSource;
  store.setCRSSource(crs);
  pushUndo(createAction('Change CRS', old, crs, (v) => {
    useTerrainStore.getState().setCRSSource(v);
  }));
}

export function selectAllTilesWithUndo(): void {
  const store = useTerrainStore.getState();
  const oldSet = new Set(store.selectedTiles);
  store.selectAllTiles();
  pushUndo({
    label: 'Select All Tiles',
    undo: () => { useTerrainStore.getState().setSelectedTiles(oldSet); },
    redo: () => { useTerrainStore.getState().selectAllTiles(); },
  });
}

export function deselectAllTilesWithUndo(): void {
  const store = useTerrainStore.getState();
  const oldSet = new Set(store.selectedTiles);
  store.deselectAllTiles();
  pushUndo({
    label: 'Deselect All Tiles',
    undo: () => { useTerrainStore.getState().setSelectedTiles(oldSet); },
    redo: () => { useTerrainStore.getState().deselectAllTiles(); },
  });
}

// ─── Hook: listens for undo/redo requests from main process ────

export function useUndoRedoBridge(): void {
  useEffect(() => {
    const ipc = getIpcBridge();
    if (!ipc) return;
    const unsubs: (() => void)[] = [];
    unsubs.push(ipc.on('undo-redo:undo-requested', () => { undo(); }));
    unsubs.push(ipc.on('undo-redo:redo-requested', () => { redo(); }));
    return () => { unsubs.forEach(u => u()); };
  }, []);
}
