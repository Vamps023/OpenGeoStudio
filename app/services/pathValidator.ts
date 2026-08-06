import * as path from 'path';
import { realpath } from 'fs/promises';

export const MAX_PATH_LENGTH = 4096;
export const MAX_FILE_SIZE = 10 * 1024 * 1024;
export const MAX_BINARY_SIZE = 100 * 1024 * 1024;
export const ALLOWED_EXTENSIONS = ['.json'];

export function validatePath(requestedPath: string, allowedBasePaths: string[]): boolean {
  if (requestedPath.includes('\0')) return false;

  const resolved = path.resolve(requestedPath);

  for (const basePath of allowedBasePaths) {
    if (!basePath) continue;
    const resolvedBase = path.resolve(basePath) + path.sep;

    if (process.platform === 'win32') {
      if (
        resolved.toLowerCase().startsWith(resolvedBase.toLowerCase()) ||
        resolved.toLowerCase() === resolvedBase.slice(0, -1).toLowerCase()
      ) {
        return true;
      }
    } else {
      if (resolved.startsWith(resolvedBase) || resolved === resolvedBase.slice(0, -1)) {
        return true;
      }
    }
  }

  return false;
}

export async function resolveRealPath(p: string): Promise<string | null> {
  try {
    return await realpath(p);
  } catch {
    return null;
  }
}

export function isPathWithinAllowed(realPath: string, allowedBasePaths: string[]): boolean {
  const parentDir = path.dirname(realPath);
  return allowedBasePaths.some((basePath) => {
    const realBasePath = path.resolve(basePath);
    return parentDir.startsWith(realBasePath + path.sep) || parentDir === realBasePath;
  });
}

export function getAllowedBasePaths(app: Electron.App, lastOutputFolder: string | null): string[] {
  const paths = [
    app.getPath('userData'),
    app.getPath('documents'),
    app.getPath('desktop'),
    app.getPath('home'),
  ];
  if (lastOutputFolder) paths.push(lastOutputFolder);
  return paths;
}
