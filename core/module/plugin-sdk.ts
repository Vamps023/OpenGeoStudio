/**
 * Plugin SDK — utilities for plugin developers.
 *
 * This file provides the types and helpers that external plugin
 * developers use to create plugins for OpenGeoStudio.
 */

import type { Plugin, AppContext, PluginCapability } from '../interfaces';

// ─── Plugin Builder ────────────────────────────────────────────

/**
 * Fluent builder for creating plugins.
 * Usage:
 *   export default createPlugin('my-plugin', '1.0.0')
 *     .name('My Plugin')
 *     .description('Does something cool')
 *     .provides({ type: 'export-provider', id: 'my-export' })
 *     .init(async (ctx) => { ctx.logger.info('Hello'); })
 *     .build();
 */
export class PluginBuilder {
  private plugin: { id: string; name: string; version: string; description?: string; author?: string; provides: PluginCapability[]; init?: (ctx: AppContext) => void | Promise<void>; dispose?: () => void | Promise<void> };

  constructor(id: string, version: string) {
    this.plugin = {
      id,
      version,
      name: id,
      provides: [],
    };
  }

  name(name: string): this {
    this.plugin.name = name;
    return this;
  }

  description(desc: string): this {
    this.plugin.description = desc;
    return this;
  }

  author(author: string): this {
    this.plugin.author = author;
    return this;
  }

  provides(capability: PluginCapability): this {
    this.plugin.provides!.push(capability);
    return this;
  }

  init(handler: (ctx: AppContext) => void | Promise<void>): this {
    this.plugin.init = handler;
    return this;
  }

  dispose(handler: () => void | Promise<void>): this {
    this.plugin.dispose = handler;
    return this;
  }

  build(): Plugin {
    return this.plugin as Plugin;
  }
}

export function createPlugin(id: string, version: string): PluginBuilder {
  return new PluginBuilder(id, version);
}

// ─── Plugin Manifest Schema ────────────────────────────────────

export const PLUGIN_MANIFEST_SCHEMA = {
  required: ['id', 'name', 'version', 'main', 'provides'],
  optional: ['description', 'author', 'minAppVersion', 'dependencies', 'enabled'],
} as const;

export function validateManifest(manifest: unknown): { valid: boolean; errors: string[] } {
  const errors: string[] = [];
  if (!manifest || typeof manifest !== 'object') {
    return { valid: false, errors: ['Manifest must be an object'] };
  }
  const m = manifest as Record<string, unknown>;
  for (const field of PLUGIN_MANIFEST_SCHEMA.required) {
    if (!(field in m)) {
      errors.push(`Missing required field: ${field}`);
    }
  }
  if (m.provides && !Array.isArray(m.provides)) {
    errors.push('Field "provides" must be an array');
  }
  return { valid: errors.length === 0, errors };
}
