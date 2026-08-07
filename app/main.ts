/**
 * OpenGeoStudio — Electron Main Process
 *
 * Wires up the AppBootstrap (core framework + all modules),
 * registers IPC handlers, and creates the main window.
 */

import { app, BrowserWindow, nativeTheme, session } from 'electron';
import * as path from 'path';
import * as fs from 'fs/promises';

import { createMainWindow } from './windows/mainWindow';
import { registerNativeHandlers } from './handlers/nativeHandler';
import { registerExportHandlers } from './handlers/exportHandler';
import { registerFsHandlers, registerSettingsHandlers } from './handlers/fsHandler';
import { registerDialogHandlers } from './handlers/dialogHandler';
import { registerCoreIpc } from './handlers/coreIpcHandler';
import { registerRoadEngineHandlers } from './handlers/roadEngineHandler';
import type { NativeAddon } from './handlers/nativeHandler';
import type { CancellationToken } from '../modules/export/server/types';
import { AppBootstrap } from '../core/module/bootstrap';
import { registerBuiltinModules } from '../core/module/builtin-modules';
import type { AppContext } from '../core/interfaces';

// ─── Bootstrap (Core Framework + Modules) ───────────────────
let bootstrap: AppBootstrap | null = null;
let appContext: AppContext | null = null;

// ─── Native Addon Loading ────────────────────────────────────
let nativeAddon: NativeAddon | null = null;
(async () => {
  try {
    const addonPath = path.join(__dirname, 'native/geoterrain_native.node');
    try {
      await fs.access(addonPath);
      nativeAddon = require(addonPath) as NativeAddon;
    } catch {
      // no-op
    }
  } catch (err) {
    console.error('[Main] Failed to load native addon:', err);
  }
})();

// ─── Shared State ───────────────────────────────────────────
const exportState = {
  lastOutputFolder: null as string | null,
  activeExportTokens: new Map<string, { token: CancellationToken; cancel: () => void }>(),
};

let mainWindow: BrowserWindow | null = null;

// ─── Global Error Handlers ─────────────────────────────────────
// Prevent EPIPE and other stream errors from crashing the main process.
// These errors commonly occur when the parent terminal closes its pipe
// before the process finishes writing (e.g. during plugin loading).
process.on('uncaughtException', (err: NodeJS.ErrnoException) => {
  if (err?.code === 'EPIPE' || err?.code === 'ERR_STREAM_DESTROYED') {
    // Stream/pipe closed — safe to ignore, don't crash
    return;
  }
  // For all other errors, log and re-throw so they're visible
  console.error('[FATAL] Uncaught exception:', err);
  throw err;
});

process.stdout?.on?.('error', (err: NodeJS.ErrnoException) => {
  if (err?.code === 'EPIPE') return; // ignore
  throw err;
});

process.stderr?.on?.('error', (err: NodeJS.ErrnoException) => {
  if (err?.code === 'EPIPE') return; // ignore
  throw err;
});

// ─── App Lifecycle ────────────────────────────────────────────
app.whenReady().then(async () => {
  nativeTheme.themeSource = 'dark';

  // Set Content Security Policy
  const isDev = !!(process.env.VITE_DEV_SERVER_URL);
  const csp = isDev
    ? "default-src 'self'; script-src 'self' 'unsafe-inline' 'unsafe-eval' http://localhost:5173; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https: http://localhost:5173; connect-src 'self' https: blob: http://localhost:5173 ws://localhost:5173; font-src 'self' data:; worker-src 'self' blob:;"
    : "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https:; connect-src 'self' https: blob:; font-src 'self' data:; worker-src 'self' blob:;";

  session.defaultSession.webRequest.onHeadersReceived((details, callback) => {
    callback({
      responseHeaders: {
        ...details.responseHeaders,
        'Content-Security-Policy': [csp],
      },
    });
  });

  // ─── Initialize Core Framework + Modules ───────────────────
  bootstrap = new AppBootstrap();
  registerBuiltinModules(bootstrap);

  try {
    appContext = await bootstrap.init();
  } catch (err) {
    console.error('[Main] Bootstrap failed:', err);
  }

  mainWindow = await createMainWindow();

  // Register IPC handlers — legacy domain handlers + new core IPC
  registerNativeHandlers(nativeAddon);
  registerExportHandlers(exportState);
  registerFsHandlers(exportState);
  registerSettingsHandlers();
  registerDialogHandlers(exportState, () => mainWindow);
  registerRoadEngineHandlers();

  // Register core service IPC (jobs, notifications, commands, selection, project, workspace)
  if (appContext) {
    registerCoreIpc(appContext);
    // Set up recent projects persistence file path
    const userDataPath = app.getPath('userData');
    const recentFilePath = path.join(userDataPath, 'recent-projects.json');
    bootstrap.getProjectManager().setRecentFilePath(recentFilePath);
  }

  // Load plugins from the plugins directory
  if (appContext && bootstrap) {
    const pluginsDir = path.join(__dirname, '..', '..', 'plugins');
    try {
      await fs.access(pluginsDir);
      await bootstrap.getPluginLoader().loadAll(pluginsDir, appContext);
    } catch {
      // no-op
    }
  }

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createMainWindow().then((win) => { mainWindow = win; });
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

// ─── Security: Prevent navigation to external sites ─────────
app.on('web-contents-created', (_event, contents) => {
  contents.on('will-navigate', (e, url) => {
    const devServerUrl = process.env.VITE_DEV_SERVER_URL || 'http://localhost:5173';
    const allowed = url.startsWith(devServerUrl) || url.startsWith('file://');
    if (!allowed) {
      e.preventDefault();
    }
  });

  contents.setWindowOpenHandler(() => {
    return { action: 'deny' };
  });
});

// ─── Export context for IPC handlers ────────────────────────
export function getAppContext(): AppContext | null {
  return appContext;
}

export function getBootstrap(): AppBootstrap | null {
  return bootstrap;
}
