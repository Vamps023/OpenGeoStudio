/**
 * Project Manager — manages the active project (saved state of work).
 *
 * A project contains: map bounds, tile selection, export settings,
 * imported layers, and module-specific state. Projects can be
 * saved/loaded as .ogproj (JSON) files.
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';
import * as path from 'path';

export interface Project {
  /** Unique project ID */
  id: string;
  /** Display name */
  name: string;
  /** File path (if saved) */
  filePath?: string;
  /** Project base folder — all auto-managed subfolders live here */
  basePath?: string;
  /** When created (ISO) */
  createdAt: string;
  /** When last modified (ISO) */
  modifiedAt: string;
  /** Map bounds */
  bounds?: { north: number; south: number; east: number; west: number };
  /** Active workspace ID */
  workspaceId: string;
  /** Module-specific state */
  moduleState: Record<string, unknown>;
  /** Whether the project has unsaved changes */
  dirty: boolean;
}

export interface RecentProject {
  id: string;
  name: string;
  filePath: string;
  lastOpened: string;
}

export interface ProjectManager {
  create(name: string, workspaceId: string): Project;
  /** Create a project with a base folder — auto-creates all subfolders. */
  createWithFolder(name: string, workspaceId: string, basePath: string): Promise<Project>;
  open(filePath: string): Promise<Project>;
  save(project: Project): Promise<void>;
  saveAs(project: Project, filePath: string): Promise<void>;
  close(): void;
  getActive(): Project | undefined;
  markDirty(): void;
  /** Subscribe to project changes */
  onChange(handler: (project: Project | undefined) => void): () => void;
  /** Get recently opened projects */
  getRecent(): RecentProject[];
  /** Clear recent projects list */
  clearRecent(): void;
  /** Enable/disable autosave */
  setAutosave(enabled: boolean, intervalMs?: number): void;
  /** Get a subfolder path within the active project's base folder */
  getSubfolder(name: string): string | null;
}

// ─── Events ────────────────────────────────────────────────────

export const PROJECT_EVENTS = {
  CREATED: 'project:created',
  OPENED: 'project:opened',
  SAVED: 'project:saved',
  CLOSED: 'project:closed',
  CHANGED: 'project:changed',
  AUTOSAVED: 'project:autosaved',
} as const;

// ─── Project Folder Structure ──────────────────────────────────

/** Subfolders auto-created inside a project base folder. */
export const PROJECT_SUBFOLDERS = [
  'Terrain',
  'GIS',
  'Roads',
  'Railway',
  'Scene',
  'Simulation',
  'Infrastructure',
  'Assets',
  'Environment',
  'Validation',
  'Exports',
  'Cache',
  'Temp',
  'Logs',
  'Config',
] as const;

/** Project file extension */
export const PROJECT_FILE_EXT = '.ogproj';

// ─── Implementation ────────────────────────────────────────────

let nextProjectId = 0;

const MAX_RECENT = 20;

export class ProjectManagerImpl implements ProjectManager {
  private active: Project | null = null;
  private changeHandlers = new Set<(p: Project | undefined) => void>();
  private recent: RecentProject[] = [];
  private autosaveEnabled = false;
  private autosaveTimer: ReturnType<typeof setInterval> | null = null;
  private autosaveIntervalMs = 60000; // 1 minute default
  private recentFilePath: string | null = null;

  constructor(
    private events: EventBus,
    private logger: Logger,
  ) {
    // Load recent projects from disk on construction
    this.loadRecentFromDisk();
  }

  /** Set the path for persisting recent projects (called by main process) */
  setRecentFilePath(filePath: string): void {
    this.recentFilePath = filePath;
    this.loadRecentFromDisk();
  }

  private async loadRecentFromDisk(): Promise<void> {
    if (!this.recentFilePath) return;
    try {
      const fs = await import('fs/promises');
      const data = await fs.readFile(this.recentFilePath, 'utf-8');
      const parsed = JSON.parse(data) as RecentProject[];
      if (Array.isArray(parsed)) {
        this.recent = parsed;
        this.logger.info(`Loaded ${parsed.length} recent projects from disk`);
      }
    } catch {
      // File doesn't exist yet — that's fine
    }
  }

  private async saveRecentToDisk(): Promise<void> {
    if (!this.recentFilePath) return;
    try {
      const fs = await import('fs/promises');
      const path = await import('path');
      await fs.mkdir(path.dirname(this.recentFilePath), { recursive: true });
      await fs.writeFile(this.recentFilePath, JSON.stringify(this.recent, null, 2), 'utf-8');
    } catch (err) {
      this.logger.error('Failed to persist recent projects:', err);
    }
  }

  create(name: string, workspaceId: string): Project {
    const now = new Date().toISOString();
    const project: Project = {
      id: `project-${++nextProjectId}`,
      name,
      createdAt: now,
      modifiedAt: now,
      workspaceId,
      moduleState: {},
      dirty: false,
    };
    this.active = project;
    this.events.emit(PROJECT_EVENTS.CREATED, project);
    this.notifyChange();
    return project;
  }

  async createWithFolder(name: string, workspaceId: string, basePath: string): Promise<Project> {
    const path = await import('path');
    const fs = await import('fs/promises');

    // Sanitize project name for folder name
    const safeName = name.replace(/[^a-zA-Z0-9_-]/g, '_');
    const projectDir = path.join(basePath, safeName);

    // Create the project base folder
    await fs.mkdir(projectDir, { recursive: true });

    // Create all subfolders
    for (const sub of PROJECT_SUBFOLDERS) {
      await fs.mkdir(path.join(projectDir, sub), { recursive: true });
    }

    const now = new Date().toISOString();
    const project: Project = {
      id: `project-${++nextProjectId}`,
      name,
      basePath: projectDir,
      filePath: path.join(projectDir, `${safeName}${PROJECT_FILE_EXT}`),
      createdAt: now,
      modifiedAt: now,
      workspaceId,
      moduleState: {},
      dirty: false,
    };

    // Save the project file immediately
    const data = JSON.stringify(project, null, 2);
    await fs.writeFile(project.filePath!, data, 'utf-8');

    this.active = project;
    this.addToRecent(project.name, project.filePath!);
    this.events.emit(PROJECT_EVENTS.CREATED, project);
    this.events.emit(PROJECT_EVENTS.SAVED, project);
    this.notifyChange();
    this.logger.info(`Project created at ${projectDir} with ${PROJECT_SUBFOLDERS.length} subfolders`);
    return project;
  }

  getSubfolder(name: string): string | null {
    if (!this.active?.basePath) return null;
    return path.join(this.active.basePath, name);
  }

  async open(filePath: string): Promise<Project> {
    this.logger.info(`Opening project from ${filePath}`);
    try {
      const fs = await import('fs/promises');
      const path = await import('path');
      const data = await fs.readFile(filePath, 'utf-8');
      const parsed = JSON.parse(data) as Project;
      // Validate required fields
      if (!parsed.id || !parsed.name || !parsed.workspaceId) {
        throw new Error('Invalid project file: missing required fields');
      }
      parsed.filePath = filePath;
      parsed.dirty = false;

      // Ensure subfolders exist (in case project was moved or copied)
      if (parsed.basePath) {
        for (const sub of PROJECT_SUBFOLDERS) {
          await fs.mkdir(path.join(parsed.basePath, sub), { recursive: true });
        }
      } else {
        // Legacy project without basePath — derive it from filePath
        parsed.basePath = path.dirname(filePath);
        for (const sub of PROJECT_SUBFOLDERS) {
          await fs.mkdir(path.join(parsed.basePath, sub), { recursive: true });
        }
      }

      this.active = parsed;
      this.addToRecent(parsed.name, filePath);
      this.events.emit(PROJECT_EVENTS.OPENED, parsed);
      this.notifyChange();
      return parsed;
    } catch (err) {
      this.logger.error(`Failed to open project from ${filePath}:`, err);
      throw err;
    }
  }

  async save(project: Project): Promise<void> {
    if (!project.filePath) {
      throw new Error('No file path set for project');
    }
    try {
      const fs = await import('fs/promises');
      project.modifiedAt = new Date().toISOString();
      project.dirty = false;
      const data = JSON.stringify(project, null, 2);
      await fs.writeFile(project.filePath, data, 'utf-8');
      this.addToRecent(project.name, project.filePath);
      this.events.emit(PROJECT_EVENTS.SAVED, project);
      this.notifyChange();
    } catch (err) {
      this.logger.error(`Failed to save project to ${project.filePath}:`, err);
      throw err;
    }
  }

  async saveAs(project: Project, filePath: string): Promise<void> {
    project.filePath = filePath;
    await this.save(project);
  }

  close(): void {
    if (!this.active) return;
    this.events.emit(PROJECT_EVENTS.CLOSED, this.active);
    this.active = null;
    this.notifyChange();
  }

  getActive(): Project | undefined { return this.active ?? undefined; }

  markDirty(): void {
    if (this.active) {
      this.active.dirty = true;
      this.active.modifiedAt = new Date().toISOString();
      this.notifyChange();
    }
  }

  onChange(handler: (project: Project | undefined) => void): () => void {
    this.changeHandlers.add(handler);
    return () => { this.changeHandlers.delete(handler); };
  }

  getRecent(): RecentProject[] {
    return [...this.recent];
  }

  clearRecent(): void {
    this.recent = [];
  }

  setAutosave(enabled: boolean, intervalMs?: number): void {
    this.autosaveEnabled = enabled;
    if (intervalMs) this.autosaveIntervalMs = intervalMs;

    if (this.autosaveTimer) {
      clearInterval(this.autosaveTimer);
      this.autosaveTimer = null;
    }

    if (enabled) {
      this.autosaveTimer = setInterval(() => {
        if (this.active && this.active.dirty && this.active.filePath) {
          // Emit autosave-requested so renderer can capture terrain state
          // The renderer will call back via save() with updated state
          this.events.emit('project:autosave-requested', this.active);
        }
      }, this.autosaveIntervalMs);
    }
  }

  /** Called by renderer after capturing terrain state for autosave */
  async performAutosave(project: Project): Promise<void> {
    try {
      await this.save(project);
      this.events.emit(PROJECT_EVENTS.AUTOSAVED, this.active);
      this.logger.info(`Autosaved project: ${this.active?.name}`);
    } catch (err) {
      this.logger.error('Autosave failed:', err);
    }
  }

  private addToRecent(name: string, filePath: string): void {
    // Remove existing entry for this path
    this.recent = this.recent.filter(r => r.filePath !== filePath);
    // Add to front
    this.recent.unshift({
      id: `recent-${Date.now()}`,
      name,
      filePath,
      lastOpened: new Date().toISOString(),
    });
    // Trim to max
    if (this.recent.length > MAX_RECENT) {
      this.recent = this.recent.slice(0, MAX_RECENT);
    }
    // Persist to disk
    this.saveRecentToDisk();
  }

  private notifyChange(): void {
    this.events.emit(PROJECT_EVENTS.CHANGED, this.active ?? undefined);
    for (const h of this.changeHandlers) h(this.active ?? undefined);
  }
}
