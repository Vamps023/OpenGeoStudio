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
  ROAD_GET_VERSION,
  ROAD_GENERATE_INTERSECTION,
  ROAD_COMPUTE_CIRCLE_ARC,
  ROAD_SAMPLE_CENTERLINE,
  ROAD_GEO_TO_LOCAL,
  ROAD_LOCAL_TO_GEO,
  ROAD_COMPUTE_CLOTHOID,
  ROAD_GENERATE_ROAD_MESH,
  ROAD_GENERATE_INTERSECTION_MESH,
  ROAD_EXPORT_OPENDRIVE,
  ROAD_CREATE_SEGMENT,
  ROAD_CREATE_CIRCLE_ARC,
  ROAD_CREATE_CLOTHOID_ARC,
  ROAD_CREATE_POLYLINE,
  ROAD_CREATE_BEZIER,
  ROAD_CREATE_CLOTHOID_SPLINE,
  ROAD_SAMPLE_CENTERLINE_V2,
  ROAD_GET_ADAPTER_REPORT,
  ROAD_CONVERT_FROM_V2,
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
  /** C++ Road Geometry Engine — all road math runs in native code */
  roadEngine: {
    getVersion: () => Promise<string>;
    generateIntersection: (road1: unknown, road2: unknown, refLat: number, refLon: number) => Promise<unknown>;
    computeCircleArc: (startPoint: { x: number; y: number }, startDirection: { x: number; y: number }, endPoint: { x: number; y: number }, segments?: number) => Promise<unknown>;
    computeClothoid: (startPoint: { x: number; y: number }, startDirection: { x: number; y: number }, endPoint: { x: number; y: number }, endDirection: { x: number; y: number }, initialA?: number, segments?: number) => Promise<unknown>;
    generateRoadMesh: (road: unknown, numSamples?: number) => Promise<unknown>;
    generateIntersectionMesh: (intersection: unknown, z?: number) => Promise<unknown>;
    exportOpenDrive: (roads: unknown[], refLat: number, refLon: number) => Promise<string>;
    sampleCenterline: (road: unknown, numSamples?: number) => Promise<unknown>;
    geoToLocal: (lat: number, lon: number, refLat: number, refLon: number) => Promise<{ x: number; y: number }>;
    localToGeo: (x: number, y: number, refLat: number, refLon: number) => Promise<{ lat: number; lon: number }>;
    // Road creation tools (SCANeR-style)
    createSegment: (sx: number, sy: number, ex: number, ey: number, params?: unknown) => Promise<unknown>;
    createCircleArc: (sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, numCPs?: number, params?: unknown) => Promise<unknown>;
    createClothoidArc: (sx: number, sy: number, dx: number, dy: number, ex: number, ey: number, edx: number, edy: number, numCPs?: number, params?: unknown) => Promise<unknown>;
    createPolyline: (points: unknown[], filletR?: number, filletSegs?: number, params?: unknown) => Promise<unknown>;
    createBezier: (sx: number, sy: number, hox: number, hoy: number, ex: number, ey: number, hix: number, hiy: number, params?: unknown) => Promise<unknown>;
    createClothoidSpline: (points: unknown[], stx: number, sty: number, etx: number, ety: number, segsPerSpan?: number, params?: unknown) => Promise<unknown>;
    // Phase 1.9 — RoadV2 bridge integration
    sampleCenterlineV2: (road: unknown, numSamples?: number) => Promise<unknown>;
    getAdapterReport: (road: unknown) => Promise<unknown>;
    convertFromV2: (road: unknown) => Promise<unknown>;
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
  roadEngine: {
    getVersion: () => ipcRenderer.invoke(ROAD_GET_VERSION),
    generateIntersection: (road1, road2, refLat, refLon) =>
      ipcRenderer.invoke(ROAD_GENERATE_INTERSECTION, road1, road2, refLat, refLon),
    computeCircleArc: (startPoint, startDirection, endPoint, segments) =>
      ipcRenderer.invoke(ROAD_COMPUTE_CIRCLE_ARC, startPoint, startDirection, endPoint, segments),
    computeClothoid: (startPoint, startDirection, endPoint, endDirection, initialA, segments) =>
      ipcRenderer.invoke(ROAD_COMPUTE_CLOTHOID, startPoint, startDirection, endPoint, endDirection, initialA, segments),
    generateRoadMesh: (road, numSamples) =>
      ipcRenderer.invoke(ROAD_GENERATE_ROAD_MESH, road, numSamples),
    generateIntersectionMesh: (intersection, z) =>
      ipcRenderer.invoke(ROAD_GENERATE_INTERSECTION_MESH, intersection, z),
    exportOpenDrive: (roads, refLat, refLon) =>
      ipcRenderer.invoke(ROAD_EXPORT_OPENDRIVE, roads, refLat, refLon),
    sampleCenterline: (road, numSamples) =>
      ipcRenderer.invoke(ROAD_SAMPLE_CENTERLINE, road, numSamples),
    geoToLocal: (lat, lon, refLat, refLon) =>
      ipcRenderer.invoke(ROAD_GEO_TO_LOCAL, lat, lon, refLat, refLon),
    localToGeo: (x, y, refLat, refLon) =>
      ipcRenderer.invoke(ROAD_LOCAL_TO_GEO, x, y, refLat, refLon),
    // Road creation tools (SCANeR-style)
    createSegment: (sx, sy, ex, ey, params) =>
      ipcRenderer.invoke(ROAD_CREATE_SEGMENT, sx, sy, ex, ey, params),
    createCircleArc: (sx, sy, dx, dy, ex, ey, numCPs, params) =>
      ipcRenderer.invoke(ROAD_CREATE_CIRCLE_ARC, sx, sy, dx, dy, ex, ey, numCPs, params),
    createClothoidArc: (sx, sy, dx, dy, ex, ey, edx, edy, numCPs, params) =>
      ipcRenderer.invoke(ROAD_CREATE_CLOTHOID_ARC, sx, sy, dx, dy, ex, ey, edx, edy, numCPs, params),
    createPolyline: (points, filletR, filletSegs, params) =>
      ipcRenderer.invoke(ROAD_CREATE_POLYLINE, points, filletR, filletSegs, params),
    createBezier: (sx, sy, hox, hoy, ex, ey, hix, hiy, params) =>
      ipcRenderer.invoke(ROAD_CREATE_BEZIER, sx, sy, hox, hoy, ex, ey, hix, hiy, params),
    createClothoidSpline: (points, stx, sty, etx, ety, segsPerSpan, params) =>
      ipcRenderer.invoke(ROAD_CREATE_CLOTHOID_SPLINE, points, stx, sty, etx, ety, segsPerSpan, params),
    // Phase 1.9 — RoadV2 bridge integration
    sampleCenterlineV2: (road, numSamples) =>
      ipcRenderer.invoke(ROAD_SAMPLE_CENTERLINE_V2, road, numSamples),
    getAdapterReport: (road) =>
      ipcRenderer.invoke(ROAD_GET_ADAPTER_REPORT, road),
    convertFromV2: (road) =>
      ipcRenderer.invoke(ROAD_CONVERT_FROM_V2, road),
  },
};

contextBridge.exposeInMainWorld('electronAPI', api);
