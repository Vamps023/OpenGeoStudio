/**
 * Built-in module registration.
 *
 * This file imports all built-in modules and registers them with the bootstrap.
 * Future modules added to modules/ will be registered here.
 */

import type { AppBootstrap } from './bootstrap';
import { TerrainModule } from '../../modules/terrain/module';
import { ExportModule } from '../../modules/export/module';

/**
 * Register all built-in modules with the bootstrap.
 * Called during app startup before bootstrap.init().
 */
export function registerBuiltinModules(bootstrap: AppBootstrap): void {
  bootstrap.registerModule(TerrainModule);
  bootstrap.registerModule(ExportModule);
}

/** List of all built-in module IDs (for workspace validation etc.) */
export const BUILTIN_MODULE_IDS = [
  'terrain',
  'export',
  'home',
] as const;
