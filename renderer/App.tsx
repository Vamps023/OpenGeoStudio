/**
 * OpenGeoStudio — Main Application Component
 *
 * Uses the Panel Registry for dynamic panel loading.
 * Panels are lazy-loaded via React.lazy() and resolved by ID
 * from the registry, not from a hardcoded map.
 *
 * Workspace switching automatically opens/closes panels and
 * restores the saved layout from the panel layout store.
 */

import React, { useState, useEffect, useMemo, Suspense, useCallback, useRef } from 'react';
import {
  Map, Layers, Download, Box, X, Mountain, Home,
  CheckCircle, ListTodo, FileCode, Search, ListTree,
  Settings, Package, Workflow, Terminal, FolderTree, Clock,
  Square, FilePlus, FolderOpen,
  History, Info, BarChart, Save, XCircle,
  MousePointer, ZoomIn, ZoomOut, Network, Maximize,
  RotateCw, FileOutput, Undo, Redo, Play,
} from 'lucide-react';
import { useTerrainStore } from './core/store';
import { useCoreStore } from './core/coreStore';
import { ProjectService } from './core/coreService';
import { Dialog, getIpcBridge, ProjectContextIPC } from './core/ipc';
import { ToastContainer } from './components/Toast/Toast';
import { ErrorBoundary } from './components/ErrorBoundary/ErrorBoundary';
import { DockShell, type DockPanel } from './shell/DockShell';
import { WorkspaceTabs } from './shell/WorkspaceTabs';
import { CommandPalette } from './shell/CommandPalette';
import { Toolbar } from './shell/Toolbar';
// WorkflowBanner removed — pipeline breadcrumb no longer shown
import { SettingsDialog } from './components/SettingsDialog/SettingsDialog';
import { Spinner } from './components/common/Spinner';
import { useKeyboardShortcuts } from './hooks/useKeyboardShortcuts';
import { useUndoRedoBridge } from './core/undoRedoBridge';

import { panelRegistry, type DockPosition } from './registry/panelRegistry';
import './registry/registerPanels'; // Side-effect: registers all panels
import { usePanelLayoutStore, type PanelLayoutState } from './registry/panelLayoutStore';

const APP_VERSION = (typeof __APP_VERSION__ !== 'undefined' ? __APP_VERSION__ : null) || '2.0.0';

// ─── Icon mapping ─────────────────────────────────────────────
// Maps icon name strings from contributions to lucide-react components.
// Only icons actually referenced by module/core contributions are listed.
const ICON_MAP: Record<string, React.ComponentType<{ className?: string; size?: number }>> = {
  Map, Layers, Download, Box, Mountain, Home, CheckCircle, ListTodo,
  FileCode, Search, ListTree, Settings, Package, Workflow, Terminal, FolderTree,
  Clock, Square, FilePlus, FolderOpen, History, Info, BarChart, Save,
  XCircle, MousePointer, ZoomIn, ZoomOut, Network, Maximize, RotateCw, FileOutput,
  Undo, Redo, Play, X,
};

function getIcon(name?: string): React.ComponentType<{ className?: string; size?: number }> | undefined {
  if (!name) return undefined;
  return ICON_MAP[name] ?? ICON_MAP[name.charAt(0).toUpperCase() + name.slice(1)];
}

// ─── Export Progress Overlay ───────────────────────────────────

function ExportProgressOverlay() {
  const exportProgress = useTerrainStore((s) => s.exportProgress);
  const exportResult = useTerrainStore((s) => s.exportResult);
  const exportStartTime = useTerrainStore((s) => s.exportStartTime);
  const setExportResult = useTerrainStore((s) => s.setExportResult);

  if (!exportProgress && !exportResult) return null;

  return (
    <div className="absolute inset-0 flex items-center justify-center pointer-events-none z-20">
      <div className="pointer-events-auto bg-surface-elevated border border-edge rounded-xl shadow-overlay w-[480px] p-6 space-y-4">
        {!exportProgress && exportResult && (
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-full bg-ok/20 border border-ok/40 flex items-center justify-center">
                <CheckCircle className="w-5 h-5 text-ok" />
              </div>
              <div>
                <div className="text-sm font-semibold text-fg-primary">Export Complete</div>
                <div className="text-2xs text-fg-secondary mt-0.5 max-w-xs truncate">{exportResult}</div>
              </div>
            </div>
            <button onClick={() => setExportResult(null)} className="icon-btn" aria-label="Dismiss">
              <X className="w-4 h-4" />
            </button>
          </div>
        )}

        {exportProgress && (
          <>
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 rounded-full bg-accent/20 border border-accent/40 flex items-center justify-center">
                <Download className="w-5 h-5 text-accent animate-pulse" />
              </div>
              <div className="flex-1">
                <div className="text-sm font-semibold text-fg-primary">Exporting Terrain Package</div>
                <div className="text-2xs text-fg-secondary mt-0.5">{exportProgress.message}</div>
              </div>
              <span className="text-2xl font-bold text-accent tabular-nums">{exportProgress.percent}%</span>
            </div>
            <div className="h-2 bg-surface-base rounded-full overflow-hidden border border-edge">
              <div className="h-full rounded-full transition-all duration-500 bg-accent"
                style={{ width: `${exportProgress.percent}%` }} />
            </div>
            <div className="flex items-center justify-between text-2xs text-fg-secondary">
              <span className="font-medium">
                Tile <span className="text-fg-primary">{exportProgress.current}</span> of <span className="text-fg-primary">{exportProgress.total}</span>
              </span>
              {exportStartTime && exportProgress.percent > 0 && (
                <span className="text-fg-muted">
                  {(() => {
                    const elapsed = Date.now() - exportStartTime;
                    const totalEst = (elapsed / exportProgress.percent) * 100;
                    const remaining = Math.max(0, totalEst - elapsed);
                    if (remaining > 60000) return `~${Math.ceil(remaining / 60000)}m remaining`;
                    if (remaining > 1000) return `~${Math.ceil(remaining / 1000)}s remaining`;
                    return 'Almost done...';
                  })()}
                </span>
              )}
            </div>
            <div className="bg-surface-base rounded-lg p-3 grid grid-cols-5 gap-2 border border-edge">
              {[
                { stage: 'init', label: 'Init' },
                { stage: 'download_dem', label: 'DEM' },
                { stage: 'download_imagery', label: 'Imagery' },
                { stage: 'process_dem', label: 'Process' },
                { stage: 'write', label: 'Write' },
              ].map(({ stage, label }, idx) => {
                const stages = ['init','download_dem','download_imagery','process_dem','write'];
                const currentIdx = stages.indexOf(exportProgress.stage);
                const isDone = idx < currentIdx || exportProgress.stage === 'done';
                const isActive = exportProgress.stage === stage ||
                  (stage === 'process_dem' && (exportProgress.stage === 'process_dem' || exportProgress.stage === 'process_imagery'));
                return (
                  <div key={stage} className="flex flex-col items-center gap-1">
                    <div className={`w-7 h-7 rounded-full flex items-center justify-center text-2xs font-bold transition-all ${
                      isDone ? 'bg-ok/20 text-ok border border-ok/40' :
                      isActive ? 'bg-accent/20 text-accent border border-accent/60 ring-2 ring-accent/20' :
                      'bg-surface-base text-fg-muted border border-edge'
                    }`}>
                      {isDone ? '✓' : idx + 1}
                    </div>
                    <span className={`text-3xs text-center leading-tight ${
                      isDone ? 'text-ok' : isActive ? 'text-accent' : 'text-fg-muted'
                    }`}>{label}</span>
                  </div>
                );
              })}
            </div>
          </>
        )}
      </div>
    </div>
  );
}

// ─── Panel Renderer ───────────────────────────────────────────
// Resolves a panel ID to its lazy component via the Panel Registry.

const PanelRenderer: React.FC<{ panelId: string }> = ({ panelId }) => {
  const LazyComp = useMemo(() => panelRegistry.getComponent(panelId), [panelId]);

  if (!LazyComp) {
    return (
      <div className="flex items-center justify-center h-full p-4 text-fg-muted text-2xs">
        Panel &quot;{panelId}&quot; not found in registry
      </div>
    );
  }

  const Comp = LazyComp;
  return (
    <ErrorBoundary panelId={panelId}>
      <Suspense fallback={
        <div className="flex items-center justify-center h-full">
          <Spinner label="Loading panel…" />
        </div>
      }>
        <Comp />
      </Suspense>
    </ErrorBoundary>
  );
};

// ─── Center View ──────────────────────────────────────────────
// Renders the center panel based on the active workspace layout.

const CenterView: React.FC<{ centerPanelId: string | null; centerType?: string }> = ({ centerPanelId, centerType }) => {
  if (!centerPanelId) {
    return (
      <div className="flex items-center justify-center h-full text-fg-muted text-sm">
        No center view configured
      </div>
    );
  }

  // Special case: map-viewport gets the export overlay
  if (centerPanelId === 'map-viewport') {
    return (
      <div className="w-full h-full relative">
        <PanelRenderer panelId={centerPanelId} />
        <ExportProgressOverlay />
      </div>
    );
  }

  return (
    <div className="w-full h-full">
      <PanelRenderer panelId={centerPanelId} />
    </div>
  );
};

// ─── Build dock panels from registry ──────────────────────────

function buildDockPanels(panelIds: string[], dock: DockPosition): DockPanel[] {
  const panels: DockPanel[] = [];
  for (const id of panelIds) {
    const def = panelRegistry.get(id);
    if (def && def.defaultDock === dock) {
      panels.push({
        id: def.id,
        title: def.name,
        component: () => <PanelRenderer panelId={def.id} />,
        icon: getIcon(def.icon),
      });
    }
  }
  return panels;
}

// ─── Main App ─────────────────────────────────────────────────

function App(): React.JSX.Element {
  const [commandPaletteOpen, setCommandPaletteOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [statusCoords, setStatusCoords] = useState<{ lat: number; lon: number } | null>(null);
  const [statusZoom, setStatusZoom] = useState<number | undefined>(undefined);
  const [statusFps] = useState<number | undefined>(undefined);

  const {
    workspaces, activeWorkspace, activateWorkspace, initialize, initialized,
    toolbar, executeCommand, activeProject,
  } = useCoreStore();
  const { getLayout, saveLayout, clearLayout } = usePanelLayoutStore();

  // Initialize core store on mount
  useEffect(() => {
    initialize();
  }, [initialize]);

  // Global keyboard shortcuts
  useKeyboardShortcuts();
  useUndoRedoBridge();

  // Command palette shortcut (Ctrl+Shift+P)
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key === 'P') {
        e.preventDefault();
        setCommandPaletteOpen(o => !o);
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, []);

  // Listen for "open settings" requests from panels (e.g. ExportPanel missing-key warning)
  useEffect(() => {
    const onOpenSettings = () => setSettingsOpen(true);
    window.addEventListener('ogstudio:open-settings', onOpenSettings);
    return () => window.removeEventListener('ogstudio:open-settings', onOpenSettings);
  }, []);

  // Listen for file project events from main process (save-as, open dialogs)
  useEffect(() => {
    const ipc = getIpcBridge();
    if (!ipc) return;
    let unsubs: (() => void)[] = [];
    const onOpen = async () => {
      const filePath = await Dialog.loadProject();
      if (!filePath) return;
      try {
        await useCoreStore.getState().openProject(filePath);
      } catch (err) {
        useTerrainStore.getState().addNotification({
          type: 'error',
          title: 'Open Project Failed',
          message: err instanceof Error ? err.message : 'Could not open project file.',
          timeout: 5000,
        });
      }
    };
    const onSaveAs = async () => {
      const activeProj = useCoreStore.getState().activeProject;
      if (!activeProj) return;
      const filePath = await Dialog.saveProject();
      if (!filePath) return;
      try {
        await ProjectService.saveAs(activeProj, filePath);
      } catch (err) {
        useTerrainStore.getState().addNotification({
          type: 'error',
          title: 'Save As Failed',
          message: err instanceof Error ? err.message : 'Could not save project.',
          timeout: 5000,
        });
      }
    };
    const onSave = async () => {
      // Capture terrain state and save via coreStore (which captures state)
      try {
        await useCoreStore.getState().saveProject();
        useTerrainStore.getState().addNotification({
          type: 'success',
          title: 'Project Saved',
          message: useCoreStore.getState().activeProject?.name ?? 'Project',
          timeout: 2000,
        });
      } catch (err) {
        useTerrainStore.getState().addNotification({
          type: 'error',
          title: 'Save Failed',
          message: err instanceof Error ? err.message : 'Could not save project.',
          timeout: 5000,
        });
      }
    };
    const onClose = async () => {
      // Route through coreStore which has the dirty check
      try {
        await useCoreStore.getState().closeProject();
      } catch (_err) {
        // User cancelled or error — ignore
      }
    };
    const onAutosave = async () => {
      // Capture terrain state and send to main process for autosave
      const activeProj = useCoreStore.getState().activeProject;
      if (!activeProj) return;
      try {
        const terrainState = useTerrainStore.getState().captureTerrainState();
        const projectToSave = {
          ...activeProj,
          moduleState: { ...activeProj.moduleState, terrain: terrainState },
          bounds: (terrainState as any).selectedBounds ?? activeProj.bounds,
        };
        await ProjectService.autosave(projectToSave);
      } catch (err) {
        // Silent — autosave failures shouldn't bother the user
        console.error('Autosave failed:', err);
      }
    };
    unsubs.push(ipc.on('file:open-project-requested', onOpen));
    unsubs.push(ipc.on('file:save-project-as-requested', onSaveAs));
    unsubs.push(ipc.on('file:save-project-requested', onSave));
    unsubs.push(ipc.on('file:close-project-requested', onClose));
    unsubs.push(ipc.on('project:autosave-requested', onAutosave));

    // (GIS/Roads/Railway modules removed)

    // Listen for "Generate Scene" — switch to 3D Scene workspace
    // The command has already verified terrain exists via ProjectContext.
    // The renderer just needs to switch the workspace.
    const onSceneGenerate = () => {
      useCoreStore.getState().activateWorkspace('3d-scene');
    };
    unsubs.push(ipc.on('scene:generate', onSceneGenerate));

    // Listen for "Scene Built" — SceneBuilder has extracted 3D geometry
    // from OSM and sent it to the renderer. Store it so TerrainViewer3D
    // can create Babylon meshes from the geometry arrays.
    const onSceneBuilt = (_event: unknown, data: any) => {
      useTerrainStore.getState().setLiveSceneData(data);
      // Also sync scene data to ProjectContext for persistence
      ProjectContextIPC.syncScene({
        generated: true,
        generatedAt: data?.builtAt ?? new Date().toISOString(),
        sceneData: data,
      }).catch(() => { /* non-fatal */ });
    };
    unsubs.push(ipc.on('scene:built', onSceneBuilt));

    // Listen for "ProjectContext Restored" — fired when a project is opened
    // and ProjectContext state has been loaded from project-context.json.
    // Restore all Zustand stores from the persisted state so the UI shows
    // terrain, GIS, scene, and viewport data immediately without manual imports.
    const onContextRestored = async (_event: unknown, state: any) => {
      if (!state) return;
      // Restore terrain store from ProjectContext state — ALL fields
      if (state.terrain) {
        const t = state.terrain;
        useTerrainStore.getState().setSelectedBounds(t.bounds);
        useTerrainStore.getState().setDEMSource(t.demSource);
        useTerrainStore.getState().setImagerySource(t.imagerySource);
        if (t.crs) useTerrainStore.getState().setCRSSource(t.crs);
        if (t.tileSizeKm) useTerrainStore.getState().setTileSizeKm(t.tileSizeKm);
        if (t.heightmapResolution) useTerrainStore.getState().setHeightmapResolution(t.heightmapResolution);
        if (t.albedoResolution) useTerrainStore.getState().setAlbedoResolution(t.albedoResolution);
        // Restore exported terrain package path if it was generated
        if (t.packagePath) useTerrainStore.getState().setOutputPath(t.packagePath);
        // Reload the manifest from disk so the 3D viewer can render the terrain
        if (t.manifestPath && t.packagePath) {
          try {
            const { FsAPI } = await import('./core/ipc');
            const manifest = await FsAPI.readManifest(t.packagePath);
            if (manifest && !(manifest as any).error) {
              useTerrainStore.getState().setExportedData(manifest as any, t.packagePath);
            } else {
              // Manifest couldn't be read — still set the package path so
              // the viewer can try to load it
              useTerrainStore.getState().setExportedData(null, t.packagePath);
            }
          } catch {
            useTerrainStore.getState().setExportedData(null, t.packagePath);
          }
        }
      }
      // (GIS data restore removed)
      // Restore scene data — the full 3D scene (buildings, roads, signs, railways)
      if (state.scene && state.scene.generated && state.scene.sceneData) {
        useTerrainStore.getState().setLiveSceneData(state.scene.sceneData);
      }
      // Restore viewport (map position, zoom, workspace)
      if (state.viewport) {
        const v = state.viewport;
        if (v.activeWorkspace) {
          useCoreStore.getState().activateWorkspace(v.activeWorkspace);
        }
        // Emit map restore event so MapViewport can fly to the saved center/zoom
        if (v.mapLat != null && v.mapLon != null) {
          window.dispatchEvent(new CustomEvent('ogstudio:map-restore', {
            detail: { lat: v.mapLat, lon: v.mapLon, zoom: v.mapZoom },
          }));
        }
      }
      // Restore layer visibility
      if (state.layerVisibility) {
        const lv = state.layerVisibility;
        useTerrainStore.getState().setBuildingsVisible(lv.buildings ?? true);
        useTerrainStore.getState().setRoadsVisible(lv.roads ?? true);
        useTerrainStore.getState().setSignsVisible(lv.trafficSigns ?? true);
        useTerrainStore.getState().setSatelliteVisible(lv.satellite ?? true);
        useTerrainStore.getState().setDemVisible(lv.dem ?? true);
      }
    };
    unsubs.push(ipc.on('projectContext:restored', onContextRestored));

    return () => { unsubs.forEach(u => u()); };
  }, []);

  // Sync map viewport (center/zoom) to ProjectContext (debounced)
  useEffect(() => {
    let timer: ReturnType<typeof setTimeout> | null = null;
    const onViewportChanged = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      if (!detail) return;
      if (timer) clearTimeout(timer);
      timer = setTimeout(() => {
        ProjectContextIPC.syncViewport({
          mapLat: detail.mapLat,
          mapLon: detail.mapLon,
          mapZoom: detail.mapZoom,
        }).catch(() => { /* non-fatal */ });
      }, 500);
    };
    window.addEventListener('ogstudio:viewport-changed', onViewportChanged);
    return () => {
      window.removeEventListener('ogstudio:viewport-changed', onViewportChanged);
      if (timer) clearTimeout(timer);
    };
  }, []);

  // Sync selectedBounds to ProjectContext so drawn area persists in
  // project-context.json even without explicit Ctrl+S save
  useEffect(() => {
    let lastBounds: string | null = null;
    const unsub = useTerrainStore.subscribe((state) => {
      const b = state.selectedBounds;
      if (!b) return;
      const key = `${b.north},${b.south},${b.east},${b.west}`;
      if (key === lastBounds) return;
      lastBounds = key;
      ProjectContextIPC.syncTerrain({
        bounds: b,
        crs: state.crsSource,
        tileSizeKm: state.tileSizeKm,
        heightmapResolution: state.heightmapResolution,
        albedoResolution: state.albedoResolution,
        demSource: state.demSource,
        imagerySource: state.imagerySource,
      }).catch(() => { /* non-fatal */ });
    });
    return () => unsub();
  }, []);

  // Listen for map cursor coordinates (emitted by MapViewport via a custom event)
  useEffect(() => {
    const onCoords = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      if (detail?.lat != null && detail?.lon != null) {
        setStatusCoords({ lat: detail.lat, lon: detail.lon });
      }
      if (detail?.zoom != null) setStatusZoom(detail.zoom);
    };
    window.addEventListener('ogstudio:map-cursor', onCoords);
    return () => window.removeEventListener('ogstudio:map-cursor', onCoords);
  }, []);

  // Auto-switch to the project's workspace when a project opens (only on first open)
  // We use a ref to track whether we've already auto-switched for this project,
  // so the user can freely navigate to Home and back without being redirected.
  const lastAutoSwitchedProjectId = useRef<string | null>(null);
  useEffect(() => {
    if (!activeProject || !workspaces.length) return;
    // Only auto-switch once per project — when it's first opened.
    // After that, the user can freely navigate to Home and back.
    if (activeWorkspace?.id === 'home' && activeProject.workspaceId &&
        lastAutoSwitchedProjectId.current !== activeProject.id) {
      const targetWs = workspaces.find(w => w.id === activeProject.workspaceId);
      if (targetWs) {
        lastAutoSwitchedProjectId.current = activeProject.id;
        activateWorkspace(targetWs.id);
      }
    }
  }, [activeProject, workspaces, activeWorkspace, activateWorkspace]);

  // Listen for "Open in 3D Scene" events from GIS workspace
  useEffect(() => {
    const onActivate = (e: Event) => {
      const wsId = (e as CustomEvent).detail;
      if (typeof wsId === 'string') {
        activateWorkspace(wsId);
      }
    };
    window.addEventListener('ogstudio:activate-workspace', onActivate);
    return () => window.removeEventListener('ogstudio:activate-workspace', onActivate);
  }, [activateWorkspace]);

  // Reset the ref when the project is closed
  useEffect(() => {
    if (!activeProject) {
      lastAutoSwitchedProjectId.current = null;
    }
  }, [activeProject]);

  // Listen for layout:reset events (from the "Reset Layout" command)
  useEffect(() => {
    const ipc = getIpcBridge();
    if (!ipc) return;
    const onReset = (_e: unknown, data: { workspaceId: string }) => {
      if (data?.workspaceId) {
        clearLayout(data.workspaceId);
      }
    };
    const unsub = ipc.on('layout:reset', onReset);
    return () => { unsub?.(); };
  }, [clearLayout]);

  const ws = activeWorkspace ?? workspaces?.[0];
  const wsLayout = ws?.layout;

  // Try to load saved layout, fall back to workspace defaults
  const savedLayout = ws ? getLayout(ws.id) : null;

  // Determine which panels to show in each dock (memoized to stabilize identity)
  const leftPanelIds = useMemo(
    () => savedLayout?.leftPanels ?? wsLayout?.leftDock ?? [],
    [savedLayout?.leftPanels, wsLayout?.leftDock]
  );
  const rightPanelIds = useMemo(
    () => savedLayout?.rightPanels ?? wsLayout?.rightDock ?? [],
    [savedLayout?.rightPanels, wsLayout?.rightDock]
  );
  const bottomPanelIds = useMemo(
    () => savedLayout?.bottomPanels ?? wsLayout?.bottomDock ?? [],
    [savedLayout?.bottomPanels, wsLayout?.bottomDock]
  );

  // Determine center panel
  const centerType = wsLayout?.center;
  const centerPanelId = savedLayout?.centerPanel ?? (
    centerType === 'map' ? 'map-viewport'
    : centerType === 'table' ? 'recent-projects'
    : null
  );

  // Build dock panel arrays from the registry
  const leftDock = useMemo(() => buildDockPanels(leftPanelIds, 'left'), [leftPanelIds]);
  const rightDock = useMemo(() => buildDockPanels(rightPanelIds, 'right'), [rightPanelIds]);
  const bottomDock = useMemo(() => {
    const docks = buildDockPanels(bottomPanelIds, 'bottom');
    // Always include console if not already there
    if (!docks.find(d => d.id === 'console')) {
      const consoleDef = panelRegistry.get('console');
      if (consoleDef) {
        docks.push({
          id: 'console',
          title: 'Console',
          component: () => <PanelRenderer panelId="console" />,
          icon: getIcon('Terminal'),
        });
      }
    }
    return docks;
  }, [bottomPanelIds]);

  // Layout sizes and collapsed states (memoized to stabilize identity)
  const layoutSizes = useMemo(() => ({
    leftWidth: savedLayout?.leftWidth ?? 260,
    rightWidth: savedLayout?.rightWidth ?? 340,
    bottomHeight: savedLayout?.bottomHeight ?? 220,
  }), [savedLayout?.leftWidth, savedLayout?.rightWidth, savedLayout?.bottomHeight]);
  const collapsedStates = useMemo(() => ({
    left: savedLayout?.leftCollapsed ?? false,
    right: savedLayout?.rightCollapsed ?? false,
    bottom: savedLayout?.bottomCollapsed ?? true, // collapsed by default
  }), [savedLayout?.leftCollapsed, savedLayout?.rightCollapsed, savedLayout?.bottomCollapsed]);

  // Save layout when sizes or collapsed states change
  const handleLayoutChange = useCallback((sizes: { leftWidth: number; rightWidth: number; bottomHeight: number }) => {
    if (!ws || !initialized) return;
    const existing = getLayout(ws.id);
    const layout: PanelLayoutState = {
      leftPanels: leftPanelIds,
      rightPanels: rightPanelIds,
      bottomPanels: bottomPanelIds,
      centerPanel: centerPanelId,
      leftCollapsed: existing?.leftCollapsed ?? collapsedStates.left,
      rightCollapsed: existing?.rightCollapsed ?? collapsedStates.right,
      bottomCollapsed: existing?.bottomCollapsed ?? collapsedStates.bottom,
      leftWidth: sizes.leftWidth,
      rightWidth: sizes.rightWidth,
      bottomHeight: sizes.bottomHeight,
      activeLeft: existing?.activeLeft ?? 0,
      activeRight: existing?.activeRight ?? 0,
      activeBottom: existing?.activeBottom ?? 0,
    };
    saveLayout(ws.id, layout);
  }, [ws, initialized, leftPanelIds, rightPanelIds, bottomPanelIds, centerPanelId, collapsedStates, getLayout, saveLayout]);

  const handleCollapsedChange = useCallback((collapsed: { left: boolean; right: boolean; bottom: boolean }) => {
    if (!ws || !initialized) return;
    const existing = getLayout(ws.id);
    const layout: PanelLayoutState = {
      leftPanels: leftPanelIds,
      rightPanels: rightPanelIds,
      bottomPanels: bottomPanelIds,
      centerPanel: centerPanelId,
      leftCollapsed: collapsed.left,
      rightCollapsed: collapsed.right,
      bottomCollapsed: collapsed.bottom,
      leftWidth: existing?.leftWidth ?? layoutSizes.leftWidth,
      rightWidth: existing?.rightWidth ?? layoutSizes.rightWidth,
      bottomHeight: existing?.bottomHeight ?? layoutSizes.bottomHeight,
      activeLeft: existing?.activeLeft ?? 0,
      activeRight: existing?.activeRight ?? 0,
      activeBottom: existing?.activeBottom ?? 0,
    };
    saveLayout(ws.id, layout);
  }, [ws, initialized, leftPanelIds, rightPanelIds, bottomPanelIds, centerPanelId, layoutSizes, getLayout, saveLayout]);

  const handleResetLayout = useCallback((workspaceId: string) => {
    clearLayout(workspaceId);
  }, [clearLayout]);

  // ─── Top Bar ────────────────────────────────────────────────
  // Hierarchy: [Logo→Home] [Workspace Tabs] ... [Search] [Settings] [Save] [Open]
  // All application commands live in the Command Palette (Ctrl+Shift+P).
  const topBar = (
    <>
      {/* Logo — clickable to return to Home workspace */}
      <button
        onClick={() => activateWorkspace('home')}
        className="flex items-center gap-2 px-2 shrink-0 rounded hover:bg-surface-hover transition-colors"
        aria-label="Go to Home"
        title="Home — Recent Projects"
      >
        <img src="./logo/logo.png" alt="OpenGeoStudio" className="w-5 h-5 rounded object-contain" />
        <span className="text-2xs font-semibold tracking-wide text-fg-primary hidden sm:inline">OpenGeoStudio</span>
        <span className="text-3xs px-1.5 py-0.5 bg-accent/20 text-accent rounded font-mono">v{APP_VERSION}</span>
      </button>

      {/* Workspace Tabs — primary navigation */}
      <div className="flex items-end h-8 ml-2 overflow-x-auto">
        <WorkspaceTabs
          workspaces={(workspaces ?? []).map(w => ({
            ...w,
            icon: getIcon(w.icon) as any,
          }))}
          activeId={ws?.id}
          onSelect={activateWorkspace}
          onResetLayout={handleResetLayout}
        />
      </div>

      {/* Right-aligned global actions */}
      <div className="ml-auto flex items-center gap-0.5">
        {/* Dirty indicator (unsaved changes) */}
        {activeProject?.dirty && (
          <span
            className="w-1.5 h-1.5 rounded-full bg-warn mx-1"
            title="Unsaved changes — press Ctrl+S to save"
          />
        )}
        {/* Command Palette (Search) */}
        <button
          onClick={() => setCommandPaletteOpen(o => !o)}
          className="icon-btn icon-btn-sm"
          aria-label="Command Palette"
          title="Command Palette (Ctrl+Shift+P)"
        >
          <Search className="w-3.5 h-3.5" />
        </button>
        {/* Settings */}
        <button
          onClick={() => setSettingsOpen(true)}
          className="icon-btn icon-btn-sm"
          aria-label="Settings"
          title="Settings"
        >
          <Settings className="w-3.5 h-3.5" />
        </button>
        {/* Save */}
        <button
          onClick={() => executeCommand('file.save-project')}
          className="icon-btn icon-btn-sm"
          aria-label="Save Project"
          title="Save Project (Ctrl+S)"
          disabled={!activeProject}
        >
          <Save className="w-3.5 h-3.5" />
        </button>
        {/* Open */}
        <button
          onClick={() => executeCommand('file.open-project')}
          className="icon-btn icon-btn-sm"
          aria-label="Open Project"
          title="Open Project (Ctrl+O)"
        >
          <FolderOpen className="w-3.5 h-3.5" />
        </button>
      </div>
    </>
  );

  // ─── Contextual Toolbar ─────────────────────────────────────
  // Filter toolbar contributions by the active workspace's tools array
  // This ensures only workspace-relevant tools are visible
  const toolbarNode = useMemo(() => {
    if (!toolbar || toolbar.length === 0) return null;
    const workspaceToolIds = activeWorkspace?.tools ?? [];
    if (workspaceToolIds.length === 0) return null;
    const filteredTools = toolbar.filter(t => workspaceToolIds.includes(t.commandId));
    return filteredTools.length > 0 ? <Toolbar tools={filteredTools} /> : null;
  }, [toolbar, activeWorkspace?.tools]);

  // ─── Loading Screen ─────────────────────────────────────────
  if (!initialized) {
    return (
      <div className="flex flex-col items-center justify-center h-screen bg-surface-base text-fg-secondary gap-4">
        <img src="./logo/logo.png" alt="OpenGeoStudio" className="w-16 h-16 rounded-lg object-contain" />
        <div className="text-center">
          <div className="text-lg font-semibold text-fg-primary mb-1">OpenGeoStudio</div>
          <Spinner label="Loading modules…" />
        </div>
      </div>
    );
  }

  return (
    <>
      {/* Skip to content link for keyboard users */}
      <a href="#main-content" className="skip-link">Skip to content</a>
      <ToastContainer />
      <DockShell
        center={<CenterView centerPanelId={centerPanelId} centerType={centerType} />}
        leftDock={leftDock}
        rightDock={rightDock}
        bottomDock={bottomDock}
        topBar={topBar}
        toolbar={toolbarNode}
        workflowBanner={null}
        layoutSizes={layoutSizes}
        collapsedStates={collapsedStates}
        onLayoutChange={handleLayoutChange}
        onCollapsedChange={handleCollapsedChange}
        statusCoords={statusCoords}
        statusZoom={statusZoom}
        statusFps={statusFps}
      />
      {commandPaletteOpen && (
        <CommandPalette onClose={() => setCommandPaletteOpen(false)} />
      )}
      {settingsOpen && (
        <SettingsDialog onClose={() => setSettingsOpen(false)} />
      )}
    </>
  );
}

export default App;
