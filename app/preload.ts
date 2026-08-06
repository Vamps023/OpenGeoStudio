import { contextBridge, ipcRenderer } from 'electron';
import type {
  GeoBounds,
  TerrainProfile,
  GenerationPlan,
  JobProgress,
  MaskSettings,
  ApiKeys,
} from '../shared/types/terrain';
import {
  NATIVE_GET_VERSION,
  NATIVE_PLAN_GENERATION,
  NATIVE_START_GENERATION,
  NATIVE_CANCEL_GENERATION,
  NATIVE_CANCEL_EXPORT,
  NATIVE_GET_PROGRESS,
  NATIVE_EXPORT_PACKAGE,
  NATIVE_PROGRESS_UPDATE,
  DIALOG_SELECT_FOLDER,
  DIALOG_SELECT_PACKAGE,
  DIALOG_SAVE_PROJECT,
  DIALOG_LOAD_PROJECT,
  DIALOG_NEW_PROJECT,
  DIALOG_IMPORT_FILE,
  DIALOG_GET_DEFAULT_PROJECTS_DIR,
  SETTINGS_GET_API_KEYS,
  SETTINGS_SET_API_KEYS,
  FS_READ_MANIFEST,
  FS_WRITE_MANIFEST,
  FS_SAVE_PROJECT,
  FS_LOAD_PROJECT,
  FS_READ_FILE_BINARY,
  FS_WRITE_FILE_BINARY,
} from '../shared/ipcChannels-electron';

/**
 * Electron API exposed to renderer via contextBridge.
 * All types are imported from the shared types/terrain.ts for single source of truth.
 */
export interface ElectronAPI {
  native: {
    getVersion: () => Promise<string>;
    planGeneration: (bounds: GeoBounds, profile: TerrainProfile) => Promise<GenerationPlan>;
    startGeneration: (sessionId: string, plan: GenerationPlan) => Promise<string>;
    cancelGeneration: (jobId: string) => Promise<void>;
    cancelExport: (sessionId: string) => Promise<boolean>;
    getProgress: (jobId: string) => Promise<JobProgress>;
    exportPackage: (
      sessionId: string,
      outputPath: string,
      preset: string,
      bounds: GeoBounds,
      heightmapFormat: string,
      albedoFormat: string,
      heightmapResolution?: number,
      albedoResolution?: number,
      imageryZoom?: number,
      demSource?: string,
      imagerySource?: string,
      apiKeys?: ApiKeys,
      tileRow?: number,
      tileCol?: number,
      maskSettings?: MaskSettings,
      downloadDem?: boolean,
      crsSource?: string,
      gladArdInterval?: number,
    ) => Promise<string>;
  };
  settings: {
    getApiKeys: () => Promise<ApiKeys>;
    setApiKeys: (apiKeys: ApiKeys) => Promise<boolean>;
  };
  dialog: {
    selectFolder: () => Promise<string | null>;
    selectPackage: () => Promise<string | null>;
    saveProject: () => Promise<string | null>;
    loadProject: () => Promise<string | null>;
    newProject: () => Promise<string | null>;
    getDefaultProjectsDir: () => Promise<string>;
    importFile: (opts?: { title?: string; filters?: Array<{ name: string; extensions: string[] }> }) => Promise<string | null>;
  };
  fs: {
    readManifest: (packagePath: string) => Promise<unknown>;
    writeManifest: (packagePath: string, manifest: object) => Promise<boolean>;
    saveProject: (filePath: string, data: object) => Promise<boolean>;
    loadProject: (filePath: string) => Promise<object | null>;
    readFileBinary: (filePath: string) => Promise<ArrayBuffer>;
    writeFileBinary: (filePath: string, data: ArrayBuffer | Uint8Array | string) => Promise<boolean>;
  };
  onProgressUpdate: (callback: (progress: JobProgress) => void) => () => void;
  /** Generic IPC access for core services (workspace, project, commands, etc.) */
  ipc: {
    invoke: (channel: string, ...args: any[]) => Promise<any>;
    on: (channel: string, listener: (event: unknown, ...args: any[]) => void) => () => void;
    removeListener: (channel: string, listener: (...args: any[]) => void) => void;
  };
}

// Re-export types for convenience
export type { GeoBounds, TerrainProfile, GenerationPlan, JobProgress, MaskSettings, ApiKeys };

const api: ElectronAPI = {
  native: {
    getVersion: () => ipcRenderer.invoke(NATIVE_GET_VERSION),
    planGeneration: (bounds, profile) => ipcRenderer.invoke(NATIVE_PLAN_GENERATION, bounds, profile),
    startGeneration: (sessionId, plan) => ipcRenderer.invoke(NATIVE_START_GENERATION, sessionId, plan),
    cancelGeneration: (jobId) => ipcRenderer.invoke(NATIVE_CANCEL_GENERATION, jobId),
    cancelExport: (sessionId) => ipcRenderer.invoke(NATIVE_CANCEL_EXPORT, sessionId),
    getProgress: (jobId) => ipcRenderer.invoke(NATIVE_GET_PROGRESS, jobId),
    exportPackage: (sessionId, outputPath, preset, bounds, heightmapFormat, albedoFormat, heightmapResolution, albedoResolution, imageryZoom, demSource, imagerySource, apiKeys, tileRow, tileCol, maskSettings, downloadDem, crsSource, gladArdInterval) =>
      ipcRenderer.invoke(NATIVE_EXPORT_PACKAGE, sessionId, outputPath, preset, bounds, heightmapFormat, albedoFormat, heightmapResolution, albedoResolution, imageryZoom, demSource, imagerySource, apiKeys, tileRow, tileCol, maskSettings, downloadDem, crsSource, gladArdInterval),
  },
  settings: {
    getApiKeys: () => ipcRenderer.invoke(SETTINGS_GET_API_KEYS),
    setApiKeys: (apiKeys) => ipcRenderer.invoke(SETTINGS_SET_API_KEYS, apiKeys),
  },
  dialog: {
    selectFolder: () => ipcRenderer.invoke(DIALOG_SELECT_FOLDER),
    selectPackage: () => ipcRenderer.invoke(DIALOG_SELECT_PACKAGE),
    saveProject: () => ipcRenderer.invoke(DIALOG_SAVE_PROJECT),
    loadProject: () => ipcRenderer.invoke(DIALOG_LOAD_PROJECT),
    newProject: () => ipcRenderer.invoke(DIALOG_NEW_PROJECT),
    getDefaultProjectsDir: () => ipcRenderer.invoke(DIALOG_GET_DEFAULT_PROJECTS_DIR),
    importFile: (opts?: { title?: string; filters?: Array<{ name: string; extensions: string[] }> }) =>
      ipcRenderer.invoke(DIALOG_IMPORT_FILE, opts),
  },
  fs: {
    readManifest: (packagePath) => ipcRenderer.invoke(FS_READ_MANIFEST, packagePath),
    writeManifest: (packagePath, manifest) => ipcRenderer.invoke(FS_WRITE_MANIFEST, packagePath, manifest),
    saveProject: (filePath, data) => ipcRenderer.invoke(FS_SAVE_PROJECT, filePath, data),
    loadProject: (filePath) => ipcRenderer.invoke(FS_LOAD_PROJECT, filePath),
    readFileBinary: (filePath) => ipcRenderer.invoke(FS_READ_FILE_BINARY, filePath),
    writeFileBinary: (filePath, data) => ipcRenderer.invoke(FS_WRITE_FILE_BINARY, filePath, data),
  },
  onProgressUpdate: (callback) => {
    const handler = (_event: unknown, progress: JobProgress) => callback(progress);
    ipcRenderer.on(NATIVE_PROGRESS_UPDATE, handler);
    return () => ipcRenderer.removeListener(NATIVE_PROGRESS_UPDATE, handler);
  },
  ipc: {
    invoke: (channel: string, ...args: any[]) => ipcRenderer.invoke(channel, ...args),
    on: (channel: string, listener: (event: unknown, ...args: any[]) => void) => {
      ipcRenderer.on(channel, listener as any);
      return () => ipcRenderer.removeListener(channel, listener as any);
    },
    removeListener: (channel: string, listener: (...args: any[]) => void) => {
      ipcRenderer.removeListener(channel, listener as any);
    },
  },
};

contextBridge.exposeInMainWorld('electronAPI', api);
