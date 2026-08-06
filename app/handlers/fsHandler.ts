import { ipcMain, app } from 'electron';
import * as fsPromises from 'fs/promises';
import * as path from 'path';
import { realpath, access, constants, stat } from 'fs/promises';
import type { ApiKeys } from '../../shared/types/terrain';
import { saveApiKeysSecure, getApiKeysSecure, migrateApiKeysIfNeeded } from '../services/apiKeyStore';
import {
  MAX_PATH_LENGTH,
  MAX_FILE_SIZE,
  ALLOWED_EXTENSIONS,
  validatePath,
  resolveRealPath,
  isPathWithinAllowed,
  getAllowedBasePaths,
} from '../services/pathValidator';
import { getAppContext } from '../main';
import {
  SETTINGS_GET_API_KEYS,
  SETTINGS_SET_API_KEYS,
  FS_READ_MANIFEST,
  FS_WRITE_MANIFEST,
  FS_SAVE_PROJECT,
  FS_LOAD_PROJECT,
  FS_READ_FILE_BINARY,
  FS_WRITE_FILE_BINARY,
} from '../../shared/ipcChannels-electron';

interface ExportState {
  lastOutputFolder: string | null;
}

// Helper: get allowed base paths including the active project's basePath
// and basePaths of all recent projects (so users can open exports from any project)
function getAllowedPathsWithProject(app: Electron.App, lastOutputFolder: string | null): string[] {
  const paths = getAllowedBasePaths(app, lastOutputFolder);
  const ctx = getAppContext();
  const activeProject = ctx?.project?.getActive();
  if (activeProject?.basePath) {
    paths.push(activeProject.basePath);
  }
  // Also allow basePaths of all recent projects
  const recent = ctx?.project?.getRecent?.() ?? [];
  for (const r of recent) {
    // Derive basePath from filePath (same convention as project-manager)
    const filePath = (r as { filePath?: string }).filePath;
    if (filePath) {
      const pathMod = require('path') as typeof import('path');
      paths.push(pathMod.dirname(filePath));
    }
  }
  return paths;
}

export function registerSettingsHandlers(): void {
  migrateApiKeysIfNeeded().catch(() => {});

  ipcMain.handle(SETTINGS_GET_API_KEYS, async () => {
    return getApiKeysSecure();
  });

  ipcMain.handle(SETTINGS_SET_API_KEYS, async (_event: unknown, rawApiKeys: unknown): Promise<boolean> => {
    if (typeof rawApiKeys !== 'object' || rawApiKeys === null) {
      return false;
    }

    const apiKeys = rawApiKeys as ApiKeys;

    for (const [, value] of Object.entries(apiKeys)) {
      if (value !== undefined && typeof value !== 'string') {
        return false;
      }
    }

    return saveApiKeysSecure(apiKeys);
  });
}

export function registerFsHandlers(state: ExportState): void {
  ipcMain.handle(FS_READ_MANIFEST, async (_event: unknown, rawPackagePath: unknown): Promise<unknown> => {
    if (typeof rawPackagePath !== 'string') {
      return { error: 'Access denied' };
    }

    const packagePath = rawPackagePath;
    if (packagePath.length > MAX_PATH_LENGTH) {
      return { error: 'Access denied' };
    }

    try {
      const absolutePackagePath = path.resolve(packagePath);
      const realPackagePath = await resolveRealPath(absolutePackagePath);
      if (!realPackagePath) {
        return { error: 'Access denied' };
      }

      const manifestPath = path.join(realPackagePath, 'manifest.json');
      const realManifestPath = await resolveRealPath(manifestPath);
      if (!realManifestPath) {
        return { error: 'Access denied' };
      }

      const ext = path.extname(realManifestPath).toLowerCase();
      if (!ALLOWED_EXTENSIONS.includes(ext)) {
        return { error: 'Access denied' };
      }

      try {
        await access(realManifestPath, constants.R_OK);
      } catch {
        return { error: 'Access denied' };
      }

      const fileStats = await stat(realManifestPath);
      if (!fileStats.isFile()) {
        return { error: 'Access denied' };
      }

      if (fileStats.size > MAX_FILE_SIZE) {
        return { error: 'Access denied' };
      }

      const allowedBasePaths = getAllowedPathsWithProject(app, state.lastOutputFolder);
      if (!isPathWithinAllowed(realManifestPath, allowedBasePaths)) {
        return { error: 'Access denied' };
      }

      const data = await fsPromises.readFile(realManifestPath, 'utf-8');

      let manifest: unknown;
      try {
        manifest = JSON.parse(data);
      } catch {
        return { error: 'Invalid manifest format' };
      }

      if (typeof manifest !== 'object' || manifest === null) {
        return { error: 'Invalid manifest format' };
      }

      return manifest;
    } catch {
      return { error: 'Access denied' };
    }
  });

  ipcMain.handle(FS_WRITE_MANIFEST, async (_event: unknown, rawPackagePath: unknown, rawManifest: unknown): Promise<boolean> => {
    if (typeof rawPackagePath !== 'string') {
      throw new Error('Access denied');
    }
    if (typeof rawManifest !== 'object' || rawManifest === null) {
      throw new Error('Access denied');
    }

    const packagePath = rawPackagePath;
    if (packagePath.length > MAX_PATH_LENGTH) {
      throw new Error('Access denied');
    }

    try {
      const absolutePackagePath = path.resolve(packagePath);
      let realPackagePath: string;
      try {
        realPackagePath = await realpath(absolutePackagePath);
      } catch {
        realPackagePath = absolutePackagePath;
      }

      const manifestPath = path.join(realPackagePath, 'manifest.json');
      if (!manifestPath.toLowerCase().endsWith('.json')) {
        throw new Error('Access denied');
      }

      const allowedBasePaths = getAllowedPathsWithProject(app, state.lastOutputFolder);
      if (!isPathWithinAllowed(realPackagePath, allowedBasePaths)) {
        throw new Error('Access denied');
      }

      const serialized = JSON.stringify(rawManifest, null, 2);
      if (serialized.length > MAX_FILE_SIZE) {
        throw new Error('Access denied');
      }

      await fsPromises.mkdir(realPackagePath, { recursive: true });
      await fsPromises.writeFile(manifestPath, serialized, 'utf-8');

      return true;
    } catch {
      throw new Error('Access denied');
    }
  });

  ipcMain.handle(FS_SAVE_PROJECT, async (_event: unknown, filePath: string, data: object) => {
    const allowedBasePaths = getAllowedPathsWithProject(app, state.lastOutputFolder);

    if (!validatePath(filePath, allowedBasePaths)) {
      throw new Error(`[Security] Path validation failed: "${filePath}" is not within allowed directories.`);
    }

    if (!filePath.endsWith('.gtp')) {
      throw new Error(`[Security] Project files must have .gtp extension`);
    }

    try {
      await fsPromises.writeFile(filePath, JSON.stringify(data, null, 2), 'utf-8');
      return true;
    } catch {
      return false;
    }
  });

  ipcMain.handle(FS_LOAD_PROJECT, async (_event: unknown, rawFilePath: unknown): Promise<unknown> => {
    if (typeof rawFilePath !== 'string') {
      return { error: 'Access denied' };
    }

    const filePath = rawFilePath;
    if (filePath.length > MAX_PATH_LENGTH) {
      return { error: 'Access denied' };
    }

    try {
      const absolutePath = path.resolve(filePath);
      const realPath = await resolveRealPath(absolutePath);
      if (!realPath) {
        return { error: 'Access denied' };
      }

      const ext = path.extname(realPath).toLowerCase();
      if (!['.gtp', '.json'].includes(ext)) {
        return { error: 'Access denied' };
      }

      try {
        await access(realPath, constants.R_OK);
      } catch {
        return { error: 'Access denied' };
      }

      const fileStats = await stat(realPath);
      if (!fileStats.isFile()) {
        return { error: 'Access denied' };
      }

      if (fileStats.size > MAX_FILE_SIZE) {
        return { error: 'Access denied' };
      }

      const allowedBasePaths = getAllowedPathsWithProject(app, state.lastOutputFolder);
      if (!isPathWithinAllowed(realPath, allowedBasePaths)) {
        return { error: 'Access denied' };
      }

      const data = await fsPromises.readFile(realPath, 'utf-8');

      let project: unknown;
      try {
        project = JSON.parse(data);
      } catch {
        return { error: 'Invalid project format' };
      }

      if (typeof project !== 'object' || project === null) {
        return { error: 'Invalid project format' };
      }

      return project;
    } catch {
      return { error: 'Access denied' };
    }
  });

  ipcMain.handle(FS_READ_FILE_BINARY, async (_event: unknown, rawFilePath: unknown): Promise<ArrayBuffer> => {
    if (typeof rawFilePath !== 'string') {
      throw new Error('Access denied');
    }

    const filePath = rawFilePath;
    if (filePath.length > MAX_PATH_LENGTH) {
      throw new Error('Access denied');
    }

    try {
      const absolutePath = path.resolve(filePath);
      const realPath = await resolveRealPath(absolutePath);
      if (!realPath) {
        // File doesn't exist — return empty buffer instead of throwing
        // (renderer catches this, but the error log is noisy)
        return new ArrayBuffer(0);
      }

      try {
        await access(realPath, constants.R_OK);
      } catch {
        return new ArrayBuffer(0);
      }

      const fileStats = await stat(realPath);
      if (!fileStats.isFile()) {
        throw new Error('Access denied');
      }

      const MAX_BINARY_SIZE = 100 * 1024 * 1024;
      if (fileStats.size > MAX_BINARY_SIZE) {
        throw new Error('Access denied');
      }

      const allowedBasePaths = getAllowedPathsWithProject(app, state.lastOutputFolder);
      if (!isPathWithinAllowed(realPath, allowedBasePaths)) {
        throw new Error('Access denied');
      }

      const buffer = await fsPromises.readFile(realPath);
      return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer;
    } catch {
      throw new Error('Access denied');
    }
  });

  // Write binary file — used by GLB/GLTF export to save 3D scene to project folder
  ipcMain.handle(FS_WRITE_FILE_BINARY, async (_event: unknown, rawFilePath: unknown, rawData: unknown): Promise<boolean> => {
    if (typeof rawFilePath !== 'string') {
      throw new Error('Access denied');
    }
    const filePath = rawFilePath;
    if (filePath.length > MAX_PATH_LENGTH) {
      throw new Error('Access denied');
    }

    try {
      const absolutePath = path.resolve(filePath);
      const allowedBasePaths = getAllowedPathsWithProject(app, state.lastOutputFolder);
      if (!isPathWithinAllowed(absolutePath, allowedBasePaths)) {
        throw new Error('Access denied');
      }

      // Ensure parent directory exists
      const parentDir = path.dirname(absolutePath);
      await fsPromises.mkdir(parentDir, { recursive: true });

      // Convert data to Buffer
      let buffer: Buffer;
      if (rawData instanceof ArrayBuffer) {
        buffer = Buffer.from(rawData);
      } else if (ArrayBuffer.isView(rawData)) {
        buffer = Buffer.from(rawData.buffer as ArrayBuffer, rawData.byteOffset, rawData.byteLength);
      } else if (typeof rawData === 'string') {
        // Base64-encoded string
        buffer = Buffer.from(rawData, 'base64');
      } else {
        throw new Error('Invalid data type');
      }

      const MAX_WRITE_SIZE = 500 * 1024 * 1024; // 500 MB
      if (buffer.length > MAX_WRITE_SIZE) {
        throw new Error('File too large');
      }

      await fsPromises.writeFile(absolutePath, buffer);
      return true;
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Access denied';
      if (msg === 'Access denied') throw new Error('Access denied');
      throw new Error(`Write failed: ${msg}`);
    }
  });
}
