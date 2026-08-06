import { ipcMain, dialog, BrowserWindow, app } from 'electron';
import * as path from 'path';
import * as fs from 'fs';
import type { BrowserWindow as BrowserWindowType } from 'electron';
import {
  DIALOG_SELECT_FOLDER,
  DIALOG_SELECT_PACKAGE,
  DIALOG_SAVE_PROJECT,
  DIALOG_LOAD_PROJECT,
  DIALOG_NEW_PROJECT,
  DIALOG_IMPORT_FILE,
  DIALOG_GET_DEFAULT_PROJECTS_DIR,
} from '../../shared/ipcChannels-electron';

interface ExportState {
  lastOutputFolder: string | null;
}

export function registerDialogHandlers(
  state: ExportState,
  getMainWindow: () => BrowserWindowType | null
): void {
  ipcMain.handle(DIALOG_SELECT_FOLDER, async () => {
    const parent = BrowserWindow.getFocusedWindow() ?? getMainWindow();
    const opts: Electron.OpenDialogOptions = { properties: ['openDirectory'], title: 'Select Output Folder' };
    const result = parent
      ? await dialog.showOpenDialog(parent, opts)
      : await dialog.showOpenDialog(opts);
    if (result.canceled) return null;
    state.lastOutputFolder = path.resolve(result.filePaths[0]);
    return result.filePaths[0];
  });

  ipcMain.handle(DIALOG_SELECT_PACKAGE, async () => {
    const parent = BrowserWindow.getFocusedWindow() ?? getMainWindow();
    const opts: Electron.OpenDialogOptions = { properties: ['openDirectory'], title: 'Select Terrain Package' };
    const result = parent
      ? await dialog.showOpenDialog(parent, opts)
      : await dialog.showOpenDialog(opts);
    if (result.canceled) return null;
    state.lastOutputFolder = path.resolve(result.filePaths[0]);
    return result.filePaths[0];
  });

  ipcMain.handle(DIALOG_SAVE_PROJECT, async () => {
    const parent = BrowserWindow.getFocusedWindow() ?? getMainWindow();
    const opts: Electron.SaveDialogOptions = {
      defaultPath: 'project.ogproj',
      filters: [
        { name: 'OpenGeoStudio Project', extensions: ['ogproj'] },
        { name: 'GeoTerrain Project', extensions: ['gtp'] },
        { name: 'All Files', extensions: ['*'] },
      ],
    };
    const result = parent
      ? await dialog.showSaveDialog(parent, opts)
      : await dialog.showSaveDialog(opts);
    return result.canceled ? null : result.filePath;
  });

  ipcMain.handle(DIALOG_LOAD_PROJECT, async () => {
    const parent = BrowserWindow.getFocusedWindow() ?? getMainWindow();
    const opts: Electron.OpenDialogOptions = {
      properties: ['openFile'],
      title: 'Open Project',
      filters: [
        { name: 'OpenGeoStudio Project', extensions: ['ogproj'] },
        { name: 'GeoTerrain Project', extensions: ['gtp'] },
        { name: 'All Files', extensions: ['*'] },
      ],
    };
    const result = parent
      ? await dialog.showOpenDialog(parent, opts)
      : await dialog.showOpenDialog(opts);
    return result.canceled ? null : result.filePaths[0];
  });

  // New Project — user selects a parent folder where the project folder will be created
  ipcMain.handle(DIALOG_NEW_PROJECT, async () => {
    const parent = BrowserWindow.getFocusedWindow() ?? getMainWindow();
    const opts: Electron.OpenDialogOptions = {
      properties: ['openDirectory'],
      title: 'Select Location for New Project Folder',
      buttonLabel: 'Create Project Here',
    };
    const result = parent
      ? await dialog.showOpenDialog(parent, opts)
      : await dialog.showOpenDialog(opts);
    return result.canceled ? null : result.filePaths[0];
  });

  // Get default projects directory — Documents/OpenGeoStudio/Projects/
  // Created automatically if it doesn't exist. This is the default location
  // for all new projects so the user doesn't need to pick a folder manually.
  ipcMain.handle(DIALOG_GET_DEFAULT_PROJECTS_DIR, async () => {
    const docsPath = app.getPath('documents');
    const projectsDir = path.join(docsPath, 'OpenGeoStudio', 'Projects');
    try {
      await fs.promises.mkdir(projectsDir, { recursive: true });
    } catch {
      // Fallback to documents path if creation fails
      return docsPath;
    }
    return projectsDir;
  });

  // Import File — generic file importer for DEM (GeoTIFF), imagery (PNG/JPG/TIF), KML, GPX, SHP
  ipcMain.handle(DIALOG_IMPORT_FILE, async (_e, opts: { title?: string; filters?: Array<{ name: string; extensions: string[] }> }) => {
    const parent = BrowserWindow.getFocusedWindow() ?? getMainWindow();
    const dialogOpts: Electron.OpenDialogOptions = {
      properties: ['openFile'],
      title: opts?.title ?? 'Import File',
      filters: opts?.filters ?? [
        { name: 'GeoTIFF DEM', extensions: ['tif', 'tiff'] },
        { name: 'Imagery', extensions: ['png', 'jpg', 'jpeg', 'tif', 'tiff'] },
        { name: 'KML', extensions: ['kml'] },
        { name: 'GPX', extensions: ['gpx'] },
        { name: 'Shapefile', extensions: ['shp'] },
        { name: 'All Files', extensions: ['*'] },
      ],
    };
    const result = parent
      ? await dialog.showOpenDialog(parent, dialogOpts)
      : await dialog.showOpenDialog(dialogOpts);
    return result.canceled ? null : result.filePaths[0];
  });
}
