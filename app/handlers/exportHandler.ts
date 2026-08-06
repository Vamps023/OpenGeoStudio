import { ipcMain, app } from 'electron';
import * as path from 'path';
import type { GeoBounds, MaskSettings, HeightmapFormat, AlbedoFormat, DEMSource, ImagerySource } from '../../shared/types/terrain';
import type { CancellationToken } from '../../modules/export/server/types';
import { NATIVE_EXPORT_PACKAGE, NATIVE_CANCEL_EXPORT } from '../../shared/ipcChannels-electron';
import { executeExport, createCancellationToken } from '../../modules/export/server';
import { validatePath } from '../services/pathValidator';
import { getAppContext } from '../main';

interface ExportState {
  lastOutputFolder: string | null;
  activeExportTokens: Map<string, { token: CancellationToken; cancel: () => void }>;
}

export function registerExportHandlers(state: ExportState): void {
  ipcMain.handle(NATIVE_EXPORT_PACKAGE, async (
    _event: unknown,
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
    apiKeys?: { opentopography?: string; mapbox?: string; maptiler?: string; gpxz?: string; stadia?: string },
    tileRow = 0,
    tileCol = 0,
    maskSettings?: MaskSettings,
    downloadDem = true,
    crsSource = 'EPSG:4326',
    gladArdInterval = 920,
  ) => {
    const resolvedOutputPath = path.resolve(outputPath);
    const allowedBasePaths = [
      app.getPath('userData'),
      app.getPath('documents'),
      app.getPath('desktop'),
      app.getPath('home'),
    ];
    if (state.lastOutputFolder) allowedBasePaths.push(state.lastOutputFolder);
    // Also allow the active project's basePath and recent project basePaths
    const ctx = getAppContext();
    const activeProject = ctx?.project?.getActive();
    if (activeProject?.basePath) {
      allowedBasePaths.push(activeProject.basePath);
    }
    const recent = ctx?.project?.getRecent?.() ?? [];
    for (const r of recent) {
      const filePath = (r as { filePath?: string }).filePath;
      if (filePath) {
        allowedBasePaths.push(path.dirname(filePath));
      }
    }
    if (!validatePath(resolvedOutputPath, allowedBasePaths)) {
      throw new Error('[Security] Output path not within allowed directories');
    }

    const tileOutputPath = path.join(resolvedOutputPath, `tile_${tileRow}_${tileCol}`);
    state.lastOutputFolder = resolvedOutputPath;

    const { token, cancel } = createCancellationToken();
    state.activeExportTokens.set(sessionId, { token, cancel });

    try {
      const result = await executeExport({
        sessionId,
        outputPath: tileOutputPath,
        preset,
        bounds,
        heightmapFormat,
        albedoFormat,
        heightmapSize: heightmapResolution,
        albedoSize: albedoResolution,
        imageryZoom,
        demSource,
        imagerySource,
        opentopographyApiKey: apiKeys?.opentopography,
        mapboxAccessToken: apiKeys?.mapbox,
        maptilerApiKey: apiKeys?.maptiler,
        gpxzApiKey: apiKeys?.gpxz,
        stadiaApiKey: apiKeys?.stadia,
        tileRow,
        tileCol,
        maskSettings,
        downloadDem,
        crsSource,
        gladArdInterval,
        cancellationToken: token,
      });
      state.activeExportTokens.delete(sessionId);
      return result.manifestPath;
    } catch (err) {
      state.activeExportTokens.delete(sessionId);
      throw err;
    }
  });

  ipcMain.handle(NATIVE_CANCEL_EXPORT, async (_event: unknown, sessionId: string) => {
    const entry = state.activeExportTokens.get(sessionId);
    if (entry) {
      entry.cancel();
      state.activeExportTokens.delete(sessionId);
      return true;
    }
    return false;
  });
}

