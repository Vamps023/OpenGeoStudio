/**
 * Plugin Loader — discovers and loads plugins from the plugins/ directory.
 *
 * Plugins are external modules that extend OpenGeoStudio's functionality.
 * Each plugin has a manifest (plugin.json) describing its capabilities.
 * Plugins are loaded dynamically at startup (and can be hot-loaded).
 */

import type { AppContext, Plugin } from '../interfaces';
import type { Logger } from '../interfaces';
import type { EventBus } from '../interfaces';

export interface PluginManifest {
  id: string;
  name: string;
  version: string;
  description: string;
  author: string;
  /** Entry point relative to plugin dir (e.g. "index.js") */
  main: string;
  /** Capabilities this plugin provides */
  provides: Plugin['provides'];
  /** Minimum OpenGeoStudio version required */
  minAppVersion?: string;
  /** Whether the plugin is enabled */
  enabled: boolean;
  /** Dependencies on other plugins */
  dependencies?: string[];
}

export interface LoadedPlugin {
  manifest: PluginManifest;
  plugin: Plugin;
  /** Path to the plugin directory */
  dir: string;
}

export const PLUGIN_EVENTS = {
  LOADED: 'plugin:loaded',
  UNLOADED: 'plugin:unloaded',
  ERROR: 'plugin:error',
} as const;

export class PluginLoader {
  private loaded = new Map<string, LoadedPlugin>();

  constructor(
    private logger: Logger,
    private events: EventBus,
  ) {}

  /** Load a single plugin from a directory */
  async loadPlugin(dir: string, context: AppContext): Promise<LoadedPlugin | null> {
    try {
      // Read manifest
      const manifestPath = `${dir}/plugin.json`;
      const fs = await import('fs/promises');
      const manifestContent = await fs.readFile(manifestPath, 'utf-8');
      const manifest = JSON.parse(manifestContent) as PluginManifest;

      if (!manifest.enabled) {
        this.logger.info(`Plugin disabled, skipping: ${manifest.id}`);
        return null;
      }

      // Check dependencies
      if (manifest.dependencies) {
        for (const dep of manifest.dependencies) {
          if (!this.loaded.has(dep)) {
            this.logger.warn(`Plugin ${manifest.id} requires ${dep}, which is not loaded`);
            return null;
          }
        }
      }

      // Load entry point — try the manifest path, then fallback to .js/.ts
      const path = await import('path');
      const entryPath = path.resolve(dir, manifest.main);
      let module: any;
      try {
        module = await import(entryPath);
      } catch (importErr: any) {
        // If the main file doesn't exist, try alternative extensions
        const ext = path.extname(manifest.main);
        const base = manifest.main.slice(0, -ext.length || undefined);
        const alternatives = ['.js', '.cjs', '.ts'];
        for (const alt of alternatives) {
          if (alt === ext) continue;
          const altPath = path.resolve(dir, base + alt);
          try {
            await fs.access(altPath);
            if (alt === '.ts') {
              this.logger.warn(`Plugin ${manifest.id}: manifest references "${manifest.main}" but only .ts found. TypeScript plugins require pre-compilation. Skipping.`);
              return null;
            }
            module = await import(altPath);
            break;
          } catch {
            // Try next extension
          }
        }
        if (!module) {
          throw importErr;
        }
      }
      const plugin: Plugin = module.default ?? module;

      // Validate plugin interface
      if (!plugin.id || !plugin.name || !plugin.version) {
        this.logger.error(`Invalid plugin manifest in ${dir}: missing required fields`);
        return null;
      }

      // Initialize plugin
      await plugin.init?.(context);

      const loaded: LoadedPlugin = { manifest, plugin, dir };
      this.loaded.set(plugin.id, loaded);
      this.events.emit(PLUGIN_EVENTS.LOADED, loaded);
      this.logger.info(`Plugin loaded: ${plugin.name} v${plugin.version}`);
      return loaded;
    } catch (err) {
      this.logger.error(`Failed to load plugin from ${dir}:`, err);
      this.events.emit(PLUGIN_EVENTS.ERROR, { dir, error: err });
      return null;
    }
  }

  /** Unload a plugin */
  async unloadPlugin(id: string): Promise<void> {
    const loaded = this.loaded.get(id);
    if (!loaded) return;
    try {
      await loaded.plugin.dispose?.();
      this.loaded.delete(id);
      this.events.emit(PLUGIN_EVENTS.UNLOADED, loaded);
      this.logger.info(`Plugin unloaded: ${loaded.plugin.name}`);
    } catch (err) {
      this.logger.error(`Error unloading plugin ${id}:`, err);
    }
  }

  /** Discover plugins in a directory */
  async discoverPlugins(pluginsDir: string): Promise<string[]> {
    try {
      const fs = await import('fs/promises');
      const path = await import('path');
      const entries = await fs.readdir(pluginsDir, { withFileTypes: true });
      const pluginDirs: string[] = [];
      for (const entry of entries) {
        if (entry.isDirectory()) {
          const manifestPath = path.join(pluginsDir, entry.name, 'plugin.json');
          try {
            await fs.access(manifestPath);
            pluginDirs.push(path.join(pluginsDir, entry.name));
          } catch {
            // Not a plugin directory
          }
        }
      }
      return pluginDirs;
    } catch {
      this.logger.warn(`Plugins directory not found: ${pluginsDir}`);
      return [];
    }
  }

  /** Load all plugins from a directory */
  async loadAll(pluginsDir: string, context: AppContext): Promise<void> {
    const dirs = await this.discoverPlugins(pluginsDir);
    for (const dir of dirs) {
      await this.loadPlugin(dir, context);
    }
  }

  /** Get all loaded plugins */
  getLoaded(): LoadedPlugin[] { return Array.from(this.loaded.values()); }
  getLoadedById(id: string): LoadedPlugin | undefined { return this.loaded.get(id); }
}
