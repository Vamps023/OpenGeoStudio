/**
 * File System — abstraction over Node.js fs for both main and renderer.
 *
 * In the main process, uses real fs. In the renderer, delegates to IPC.
 * This allows modules to be isomorphic (run in either process).
 */

export interface FileSystem {
  read(path: string): Promise<Buffer>;
  readText(path: string): Promise<string>;
  write(path: string, data: Buffer | string): Promise<void>;
  exists(path: string): Promise<boolean>;
  stat(path: string): Promise<FileStat>;
  mkdir(path: string, recursive?: boolean): Promise<void>;
  remove(path: string): Promise<void>;
  readdir(path: string): Promise<DirEntry[]>;
  copy(src: string, dest: string): Promise<void>;
  move(src: string, dest: string): Promise<void>;
  /** Get the user's home directory */
  getHomeDir(): string;
  /** Join path segments */
  join(...segments: string[]): string;
  /** Resolve a path to absolute */
  resolve(path: string): string;
}

export interface FileStat {
  size: number;
  isFile: boolean;
  isDirectory: boolean;
  createdAt: number;
  modifiedAt: number;
}

export interface DirEntry {
  name: string;
  path: string;
  isFile: boolean;
  isDirectory: boolean;
  size: number;
}

// ─── Node.js Implementation (main process) ─────────────────────

import * as fs from 'fs/promises';
import * as nodePath from 'path';

export class NodeFileSystem implements FileSystem {
  private fs = fs;
  private path = nodePath;

  async read(path: string): Promise<Buffer> {
    return this.fs.readFile(path);
  }

  async readText(path: string): Promise<string> {
    return this.fs.readFile(path, 'utf-8');
  }

  async write(path: string, data: Buffer | string): Promise<void> {
    await this.fs.writeFile(path, data);
  }

  async exists(path: string): Promise<boolean> {
    try {
      await this.fs.access(path);
      return true;
    } catch {
      return false;
    }
  }

  async stat(path: string): Promise<FileStat> {
    const s = await this.fs.stat(path);
    return {
      size: s.size,
      isFile: s.isFile(),
      isDirectory: s.isDirectory(),
      createdAt: s.birthtimeMs,
      modifiedAt: s.mtimeMs,
    };
  }

  async mkdir(path: string, recursive = true): Promise<void> {
    await this.fs.mkdir(path, { recursive });
  }

  async remove(path: string): Promise<void> {
    await this.fs.rm(path, { recursive: true, force: true });
  }

  async readdir(path: string): Promise<DirEntry[]> {
    const entries = await this.fs.readdir(path, { withFileTypes: true });
    const result: DirEntry[] = [];
    for (const entry of entries) {
      const fullPath = this.path.join(path, entry.name);
      let size = 0;
      if (entry.isFile()) {
        try {
          const s = await this.fs.stat(fullPath);
          size = s.size;
        } catch { /* ignore */ }
      }
      result.push({
        name: entry.name,
        path: fullPath,
        isFile: entry.isFile(),
        isDirectory: entry.isDirectory(),
        size,
      });
    }
    return result;
  }

  async copy(src: string, dest: string): Promise<void> {
    await this.fs.copyFile(src, dest);
  }

  async move(src: string, dest: string): Promise<void> {
    await this.fs.rename(src, dest);
  }

  getHomeDir(): string {
    return this.path.resolve(process.env.HOME || process.env.USERPROFILE || '.');
  }

  join(...segments: string[]): string {
    return this.path.join(...segments);
  }

  resolve(path: string): string {
    return this.path.resolve(path);
  }
}
