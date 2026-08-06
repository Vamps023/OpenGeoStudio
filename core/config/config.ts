/**
 * Configuration — typed application configuration with schema validation.
 *
 * Manages app-level config (window size, paths, feature flags).
 * For user preferences (theme, language), see preferences.ts.
 */

import type { Logger } from '../logger/logger';

export interface ConfigSchema {
  [key: string]: ConfigField;
}

export interface ConfigField {
  type: 'string' | 'number' | 'boolean' | 'array' | 'object';
  default: unknown;
  description?: string;
  min?: number;
  max?: number;
  enum?: unknown[];
  secret?: boolean;
}

export class Config {
  private values: Record<string, unknown> = {};
  private schema: ConfigSchema;
  private logger: Logger;

  constructor(schema: ConfigSchema, logger: Logger) {
    this.schema = schema;
    this.logger = logger;
    // Apply defaults
    for (const [key, field] of Object.entries(schema)) {
      this.values[key] = field.default;
    }
  }

  get<T>(key: string): T {
    return this.values[key] as T;
  }

  set(key: string, value: unknown): void {
    const field = this.schema[key];
    if (!field) {
      this.logger.warn(`Unknown config key: ${key}`);
      return;
    }
    if (!this.validateValue(field, value)) {
      this.logger.error(`Invalid config value for ${key}:`, value);
      return;
    }
    this.values[key] = value;
  }

  getAll(): Record<string, unknown> {
    return { ...this.values };
  }

  toJSON(): string {
    // Strip secrets
    const safe: Record<string, unknown> = {};
    for (const [key, field] of Object.entries(this.schema)) {
      if (!field.secret) {
        safe[key] = this.values[key];
      }
    }
    return JSON.stringify(safe, null, 2);
  }

  private validateValue(field: ConfigField, value: unknown): boolean {
    if (field.enum && !field.enum.includes(value)) return false;
    if (field.min !== undefined && typeof value === 'number' && value < field.min) return false;
    if (field.max !== undefined && typeof value === 'number' && value > field.max) return false;
    switch (field.type) {
      case 'string': return typeof value === 'string';
      case 'number': return typeof value === 'number';
      case 'boolean': return typeof value === 'boolean';
      case 'array': return Array.isArray(value);
      case 'object': return typeof value === 'object' && value !== null && !Array.isArray(value);
      default: return false;
    }
  }
}

// ─── Default App Config Schema ─────────────────────────────────

export const APP_CONFIG_SCHEMA: ConfigSchema = {
  'app.name': { type: 'string', default: 'OpenGeoStudio', description: 'Application name' },
  'app.version': { type: 'string', default: '1.0.0', description: 'Application version' },
  'app.dataDir': { type: 'string', default: '', description: 'User data directory' },
  'app.cacheDir': { type: 'string', default: '', description: 'Cache directory' },
  'app.maxWorkers': { type: 'number', default: 4, min: 1, max: 32, description: 'Max worker threads' },
  'app.maxConcurrentJobs': { type: 'number', default: 2, min: 1, max: 16, description: 'Max concurrent background jobs' },
  'app.enablePlugins': { type: 'boolean', default: true, description: 'Enable plugin system' },
  'app.enableTelemetry': { type: 'boolean', default: false, description: 'Enable anonymous telemetry' },
  'window.width': { type: 'number', default: 1600, min: 800, max: 4096 },
  'window.height': { type: 'number', default: 1000, min: 600, max: 2160 },
  'window.x': { type: 'number', default: -1, description: 'Window X position (-1 = centered)' },
  'window.y': { type: 'number', default: -1, description: 'Window Y position (-1 = centered)' },
  'window.maximized': { type: 'boolean', default: false },
  'export.defaultFormat': { type: 'string', default: 'png', enum: ['png', 'geotiff', 'r16', 'raw', 'exr'] },
  'export.defaultDemSource': { type: 'string', default: 'aws-terrarium' },
  'export.defaultImagerySource': { type: 'string', default: 'arcgis' },
  'export.tileSizeKm': { type: 'number', default: 5, min: 0.5, max: 50 },
};
