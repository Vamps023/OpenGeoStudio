/**
 * Type-safe IPC wrapper for the Electron API exposed via contextBridge.
 * Provides fallback implementations for development without the native addon.
 */

import type { ElectronAPI, GeoBounds, TerrainProfile, GenerationPlan, JobProgress, HeightmapFormat, AlbedoFormat, ProjectData, DEMSource, ImagerySource, ApiKeys, MaskSettings } from '../../shared/types/terrain';

declare global {
  interface Window {
    electronAPI?: ElectronAPI;
  }
}

const isElectron = (): boolean => !!window.electronAPI;

/**
 * Get the IPC renderer bridge exposed by the preload script.
 * Works with contextIsolation: true and nodeIntegration: false.
 * Returns null if not running in Electron.
 */
export function getIpcBridge(): any | null {
  return (window as any).electronAPI?.ipc ?? null;
}

/**
 * Subscribe to an IPC event from the main process.
 * Returns an unsubscribe function.
 */
export function onIpcEvent(channel: string, listener: (event: unknown, ...args: any[]) => void): () => void {
  const ipc = getIpcBridge();
  if (!ipc) return () => {};
  return ipc.on(channel, listener);
}

export const Native = {
  async getVersion(): Promise<string> {
    if (!isElectron()) return '0.0.0-dev (web)';
    return window.electronAPI!.native.getVersion();
  },

  async planGeneration(bounds: GeoBounds, profile: TerrainProfile): Promise<GenerationPlan> {
    if (!isElectron()) {
      // Mock implementation for web development
      return mockPlanGeneration(bounds, profile);
    }
    return window.electronAPI!.native.planGeneration(bounds, profile);
  },

  async startGeneration(sessionId: string, plan: GenerationPlan): Promise<string> {
    if (!isElectron()) return 'mock-job-' + Date.now();
    return window.electronAPI!.native.startGeneration(sessionId, plan);
  },

  async cancelGeneration(jobId: string): Promise<void> {
    if (!isElectron()) return;
    return window.electronAPI!.native.cancelGeneration(jobId);
  },

  async cancelExport(sessionId: string): Promise<boolean> {
    if (!isElectron()) return false;
    return window.electronAPI!.native.cancelExport(sessionId);
  },

  async getProgress(jobId: string): Promise<JobProgress> {
    if (!isElectron()) return mockProgress(jobId);
    return window.electronAPI!.native.getProgress(jobId);
  },

  async exportPackage(
    sessionId: string,
    outputPath: string,
    preset: string,
    bounds: GeoBounds,
    heightmapFormat: HeightmapFormat,
    albedoFormat: AlbedoFormat,
    heightmapResolution = 1024,
    albedoResolution = 1024,
    imageryZoom = 0,
    demSource: DEMSource = 'aws-terrarium',
    imagerySource: ImagerySource = 'arcgis',
    apiKeys?: ApiKeys,
    tileRow = 0,
    tileCol = 0,
    maskSettings?: MaskSettings,
    downloadDem = true,
    crsSource = 'EPSG:4326',
    gladArdInterval = 920,
  ): Promise<string> {
    if (!isElectron()) {
      console.log('[Mock] Export package:', { sessionId, outputPath, preset, bounds, heightmapFormat, albedoFormat, maskSettings, downloadDem, crsSource });
      return outputPath;
    }
    return window.electronAPI!.native.exportPackage(
      sessionId, outputPath, preset, bounds, heightmapFormat, albedoFormat,
      heightmapResolution, albedoResolution, imageryZoom, demSource, imagerySource, apiKeys, tileRow, tileCol, maskSettings, downloadDem, crsSource, gladArdInterval
    );
  },
};

export const Dialog = {
  async selectFolder(): Promise<string | null> {
    if (!isElectron()) {
      // In web mode, return null to indicate no folder selected
      return null;
    }
    return window.electronAPI!.dialog.selectFolder();
  },

  async selectPackage(): Promise<string | null> {
    if (!isElectron()) return null;
    return window.electronAPI!.dialog.selectPackage();
  },

  async saveProject(): Promise<string | null> {
    if (!isElectron()) {
      console.log('[Mock] Save project dialog');
      return 'mock-project.gtp';
    }
    return window.electronAPI!.dialog.saveProject();
  },

  async loadProject(): Promise<string | null> {
    if (!isElectron()) {
      console.log('[Mock] Load project dialog');
      return null;
    }
    return window.electronAPI!.dialog.loadProject();
  },

  async newProject(): Promise<string | null> {
    if (!isElectron()) {
      console.log('[Mock] New project folder dialog');
      return 'C:\\Users\\mock\\Documents\\OpenGeoStudio projects';
    }
    return window.electronAPI!.dialog.newProject();
  },

  async getDefaultProjectsDir(): Promise<string> {
    if (!isElectron()) {
      return 'C:\\Users\\mock\\Documents\\OpenGeoStudio\\Projects';
    }
    return window.electronAPI!.dialog.getDefaultProjectsDir();
  },

  async importFile(opts?: { title?: string; filters?: Array<{ name: string; extensions: string[] }> }): Promise<string | null> {
    if (!isElectron()) {
      console.log('[Mock] Import file dialog');
      return null;
    }
    return (window.electronAPI as any).dialog.importFile(opts);
  },
};

export const Settings = {
  async getApiKeys(): Promise<ApiKeys> {
    if (!isElectron()) {
      // Read from localStorage in web mode
      try {
        const stored = localStorage.getItem('geoterrain-api-keys');
        return stored ? JSON.parse(stored) : {};
      } catch {
        return {};
      }
    }
    return window.electronAPI!.settings.getApiKeys();
  },

  async setApiKeys(apiKeys: ApiKeys): Promise<boolean> {
    if (!isElectron()) {
      try {
        localStorage.setItem('geoterrain-api-keys', JSON.stringify(apiKeys));
        return true;
      } catch {
        return false;
      }
    }
    return window.electronAPI!.settings.setApiKeys(apiKeys);
  },
};

export const FsAPI = {
  async readManifest(packagePath: string): Promise<unknown> {
    if (!isElectron()) {
      throw new Error('Cannot read manifest in web mode');
    }
    return window.electronAPI!.fs.readManifest(packagePath);
  },

  async writeManifest(packagePath: string, manifest: object): Promise<boolean> {
    if (!isElectron()) {
      console.log('[Mock] Write manifest:', packagePath, manifest);
      return true;
    }
    return window.electronAPI!.fs.writeManifest(packagePath, manifest);
  },

  async saveProject(filePath: string, data: ProjectData): Promise<boolean> {
    if (!isElectron()) {
      console.log('[Mock] Save project:', filePath, data);
      return true;
    }
    return window.electronAPI!.fs.saveProject(filePath, data);
  },

  async loadProject(filePath: string): Promise<ProjectData | null> {
    if (!isElectron()) {
      console.log('[Mock] Load project:', filePath);
      return null;
    }
    return window.electronAPI!.fs.loadProject(filePath);
  },

  async readFileBinary(filePath: string): Promise<ArrayBuffer> {
    if (!isElectron()) {
      throw new Error('Cannot read binary files in web mode');
    }
    return window.electronAPI!.fs.readFileBinary(filePath);
  },

  async writeFileBinary(filePath: string, data: ArrayBuffer | Uint8Array | string): Promise<boolean> {
    if (!isElectron()) {
      throw new Error('Cannot write binary files in web mode');
    }
    return window.electronAPI!.fs.writeFileBinary(filePath, data);
  },

};

export function onProgressUpdate(callback: (progress: JobProgress) => void): () => void {
  if (!isElectron()) {
    // Mock progress updates for web development
    const interval = setInterval(() => {
      callback(mockProgress('mock-job'));
    }, 1000);
    return () => clearInterval(interval);
  }
  return window.electronAPI!.onProgressUpdate(callback);
}

// ─── ProjectContext Sync ───────────────────────────────────────
// Renderer → main state sync. The renderer pushes terrain/GIS/scene
// state to ProjectContext (main process) after each workflow step.
// Commands read from ProjectContext — no renderer args needed.

export const ProjectContextIPC = {
  /** Push terrain metadata to ProjectContext after terrain generation/export */
  async syncTerrain(metadata: {
    bounds: { north: number; south: number; east: number; west: number };
    crs: string;
    tileSizeKm: number;
    heightmapResolution: number;
    albedoResolution: number;
    packagePath: string | null;
    manifestPath: string | null;
    demSource: string;
    imagerySource: string;
    generatedAt: string;
  }): Promise<boolean> {
    if (!isElectron()) return false;
    return window.electronAPI!.ipc.invoke('projectContext:syncTerrain', metadata);
  },

  /** Push scene state to ProjectContext after scene generation */
  async syncScene(state: {
    generated: boolean;
    scenePath?: string | null;
    glbPath?: string | null;
    generatedAt?: string | null;
    sceneData?: unknown | null;
  }): Promise<boolean> {
    if (!isElectron()) return false;
    return window.electronAPI!.ipc.invoke('projectContext:syncScene', state);
  },

  /** Push viewport state (map position, camera, workspace) */
  async syncViewport(state: {
    mapLat?: number | null;
    mapLon?: number | null;
    mapZoom?: number | null;
    cameraPosition?: { x: number; y: number; z: number } | null;
    cameraTarget?: { x: number; y: number; z: number } | null;
    activeWorkspace?: string | null;
  }): Promise<boolean> {
    if (!isElectron()) return false;
    return window.electronAPI!.ipc.invoke('projectContext:syncViewport', state);
  },

  /** Push placed assets (set dressing) to ProjectContext */
  async syncAssets(assets: unknown[]): Promise<boolean> {
    if (!isElectron()) return false;
    return window.electronAPI!.ipc.invoke('projectContext:syncAssets', assets);
  },

  /** Push layer visibility state to ProjectContext */
  async syncLayerVisibility(state: {
    buildings?: boolean;
    roads?: boolean;
    railways?: boolean;
    trafficSigns?: boolean;
    satellite?: boolean;
    dem?: boolean;
    water?: boolean;
    vegetation?: boolean;
  }): Promise<boolean> {
    if (!isElectron()) return false;
    return window.electronAPI!.ipc.invoke('projectContext:syncLayerVisibility', state);
  },

  /** Get the full ProjectContext state */
  async getState(): Promise<any> {
    if (!isElectron()) return null;
    return window.electronAPI!.ipc.invoke('projectContext:getState');
  },

  /** Get the current workflow stage */
  async getStage(): Promise<string> {
    if (!isElectron()) return 'project';
    return window.electronAPI!.ipc.invoke('projectContext:getStage');
  },
};

// ─── Mock Helpers ─────────────────────────────────────────────

function mockPlanGeneration(bounds: GeoBounds, _profile: TerrainProfile): GenerationPlan {
  const width = bounds.east - bounds.west;
  const height = bounds.north - bounds.south;
  const tiles: GenerationPlan['tiles'] = [];
  // Calculate tiles based on actual area - use smaller multiplier for better performance
  const rows = Math.min(4, Math.max(1, Math.ceil(height * 2)));
  const cols = Math.min(4, Math.max(1, Math.ceil(width * 2)));

  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      tiles.push({
        row: r,
        col: c,
        bounds: {
          west: bounds.west + (c / cols) * width,
          east: bounds.west + ((c + 1) / cols) * width,
          south: bounds.south + (r / rows) * height,
          north: bounds.south + ((r + 1) / rows) * height,
        },
      });
    }
  }

  return {
    zoom: 12,
    tiles,
    estimatedMemoryMb: tiles.length * 256,
    estimatedDurationSec: tiles.length * 45,
  };
}

function mockProgress(jobId: string): JobProgress {
  return {
    jobId,
    state: 'complete',
    overallProgress: 1.0,
    currentTile: 'chunk_0_0',
    tileProgress: 1.0,
    message: 'Generation complete',
  };
}
