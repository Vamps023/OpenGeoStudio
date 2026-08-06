/**
 * Module Registry — tracks all registered modules and their lifecycle.
 *
 * Modules are the top-level organizational unit in OpenGeoStudio.
 * Each module (terrain, roads, gis, etc.) registers services,
 * commands, and UI contributions.
 */

import type { AppContext, Module } from '../interfaces';
import type { Logger } from '../interfaces';

export interface ModuleMetadata {
  id: string;
  name: string;
  version: string;
  description: string;
  author: string;
  /** Whether the module is currently active */
  active: boolean;
  /** Whether the module is a built-in (vs plugin) */
  builtin: boolean;
}

export class ModuleRegistry {
  private modules = new Map<string, Module & { _metadata?: ModuleMetadata }>();
  private context: AppContext | null = null;

  constructor(private logger: Logger) {}

  /** Register a module (does not initialize it) */
  register(mod: Module, metadata?: Partial<ModuleMetadata>): void {
    if (this.modules.has(mod.id)) {
      this.logger.warn(`Module already registered: ${mod.id}`);
      return;
    }
    const full: Module & { _metadata?: ModuleMetadata } = mod;
    full._metadata = {
      id: mod.id,
      name: mod.name,
      version: mod.version,
      description: metadata?.description ?? '',
      author: metadata?.author ?? '',
      active: false,
      builtin: metadata?.builtin ?? true,
    };
    this.modules.set(mod.id, full);
  }

  /** Initialize all registered modules */
  async initAll(context: AppContext): Promise<void> {
    this.context = context;
    for (const [id, mod] of this.modules) {
      try {
        this.logger.info(`Initializing module: ${mod.name} v${mod.version}`);
        await mod.init?.(context);
        if (mod._metadata) mod._metadata.active = true;
      } catch (err) {
        this.logger.error(`Failed to init module ${id}:`, err);
        throw err;
      }
    }
  }

  /** Dispose all modules in reverse order */
  async disposeAll(): Promise<void> {
    const entries = Array.from(this.modules.entries()).reverse();
    for (const [id, mod] of entries) {
      try {
        await mod.dispose?.();
        if (mod._metadata) mod._metadata.active = false;
      } catch (err) {
        this.logger.error(`Error disposing module ${id}:`, err);
      }
    }
    this.context = null;
  }

  /** Get a module by ID */
  get(id: string): Module | undefined { return this.modules.get(id); }

  /** Get metadata for all modules */
  getAllMetadata(): ModuleMetadata[] {
    return Array.from(this.modules.values()).map(m => m._metadata!).filter(Boolean);
  }

  /** Get all registered module IDs */
  getIds(): string[] { return Array.from(this.modules.keys()); }

  /** Check if a module is registered and active */
  isActive(id: string): boolean {
    const mod = this.modules.get(id);
    return !!mod?._metadata?.active;
  }

  /** Get the app context (set during initAll) */
  getContext(): AppContext | null { return this.context; }
}
