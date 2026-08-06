/**
 * OpenGeoStudio Core Framework — barrel exports.
 *
 * The core/ directory contains the application framework that all modules
 * and plugins depend on. It is the foundational layer below modules/.
 */

// Interfaces and types (re-export, excluding Logger which is re-exported below)
export type {
  Module,
  AppContext,
  ServiceRegistry,
  ServiceToken,
  token,
  EventBus,
  SettingsStore,
  WorkerPool,
  WorkerTask,
  CacheLayer,
  LatLon,
  BoundingBox,
  Point2D,
  Point3D,
  CRS,
  Projection,
  ProgressReporter,
  Plugin,
  PluginCapability,
  PluginManager,
} from './interfaces';

// Provider interfaces (DEM, Imagery, Vector, LiDAR, FileImport)
export * from './providers';

// Dependency Injection
export * from './di/service-registry';

// Event System
export * from './events/event-bus';

// Cache
export * from './cache/cache-layer';

// Module lifecycle & bootstrap
export * from './module/bootstrap';
export * from './module/contributions';
export * from './module/builtin-modules';

// Job System
export * from './jobs/job-system';

// Command System
export * from './commands/command-system';

// Undo/Redo
export * from './commands/undo-redo';

// Notifications
export * from './notifications/notification-system';

// Logger
export * from './logger/logger';

// Configuration
export * from './config/config';

// Preferences
export * from './config/preferences';

// Selection
export * from './selection/selection-manager';

// Project
export * from './project/project-manager';

// Workspace
export * from './workspace/workspace-manager';

// File System abstraction
export * from './filesystem/file-system';

// Module Registry
export * from './module/module-registry';

// Plugin Loader
export * from './module/plugin-loader';

// Scene Graph
export * from './scene/scene-graph';

// Layer System
export * from './layers/layer-system';
