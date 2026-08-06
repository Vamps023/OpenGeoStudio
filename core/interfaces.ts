/**
 * Core module interfaces for OpenGeoStudio.
 *
 * Every major subsystem (GIS, Terrain, Road Network, Export, etc.)
 * implements these interfaces, enabling dependency injection,
 * plugin extensibility, and testability.
 */

// ─── Module Lifecycle ──────────────────────────────────────────

export interface Module {
  /** Unique module identifier */
  readonly id: string;
  /** Human-readable name */
  readonly name: string;
  /** Semantic version */
  readonly version: string;
  /** Optional description */
  readonly description?: string;
  /** Optional author */
  readonly author?: string;
  /** Called once on app startup. Throw to abort boot. */
  init?(context: AppContext): Promise<void> | void;
  /** Called on app shutdown */
  dispose?(): Promise<void> | void;
}

// ─── Application Context (Dependency Injection root) ──────────

export interface AppContext {
  /** Service registry for runtime lookups */
  services: ServiceRegistry;
  /** Event bus for cross-module communication */
  events: EventBus;
  /** Persistent settings store */
  settings: SettingsStore;
  /** Logger instance */
  logger: Logger;
  /** Worker pool for background processing */
  workers: WorkerPool;
  /** Cache layer (tile, geometry, etc.) */
  cache: CacheLayer;
  /** Job system for background tasks */
  jobs: import('./jobs/job-system').JobSystem;
  /** Command registry for menu/keyboard actions */
  commands: import('./commands/command-system').CommandRegistry;
  /** Undo/redo stack */
  undoRedo: import('./commands/undo-redo').UndoRedoStack;
  /** Notification manager */
  notifications: import('./notifications/notification-system').NotificationManager;
  /** Selection manager */
  selection: import('./selection/selection-manager').SelectionManager;
  /** Project manager */
  project: import('./project/project-manager').ProjectManager;
  /** Project context — central state service (single source of truth for terrain/GIS/scene) */
  projectContext: import('./project/projectContext').ProjectContextImpl;
  /** Workspace manager */
  workspace: import('./workspace/workspace-manager').WorkspaceManager;
  /** Module contributions registry (panels, providers, toolbar items) */
  contributions: import('./module/contributions').ContributionRegistry;
  /** Scene graph (3D object hierarchy) */
  scene: import('./scene/scene-graph').SceneGraph;
  /** Layer system (GIS map layers) */
  layers: import('./layers/layer-system').LayerSystem;
}

// ─── Service Registry ──────────────────────────────────────────

export interface ServiceRegistry {
  register<T>(token: ServiceToken<T>, factory: (ctx: AppContext) => T): void;
  registerInstance<T>(token: ServiceToken<T>, instance: T): void;
  resolve<T>(token: ServiceToken<T>): T;
  resolveOptional<T>(token: ServiceToken<T>): T | undefined;
  has<T>(token: ServiceToken<T>): boolean;
}

export type ServiceToken<T> = string & { readonly __serviceType: T };

export function token<T>(name: string): ServiceToken<T> {
  return name as ServiceToken<T>;
}

// ─── Event Bus ─────────────────────────────────────────────────

export interface EventBus {
  on<T>(event: string, handler: (payload: T) => void): () => void;
  once<T>(event: string, handler: (payload: T) => void): () => void;
  emit<T>(event: string, payload: T): void;
  off(event: string, handler: (...args: unknown[]) => void): void;
}

// ─── Settings Store ────────────────────────────────────────────

export interface SettingsStore {
  get<T>(key: string, defaultValue: T): T;
  set<T>(key: string, value: T): void;
  onChange(key: string, handler: (value: any) => void): () => void;
  getAll(): Record<string, any>;
}

// ─── Logger ────────────────────────────────────────────────────

export interface Logger {
  debug(message: string, ...args: any[]): void;
  info(message: string, ...args: any[]): void;
  warn(message: string, ...args: any[]): void;
  error(message: string, ...args: any[]): void;
  child(scope: string): Logger;
}

// ─── Worker Pool ───────────────────────────────────────────────

export interface WorkerPool {
  /** Submit a task to a worker thread */
  submit<T>(task: WorkerTask): Promise<T>;
  /** Number of active workers */
  readonly activeWorkers: number;
  /** Maximum concurrent workers */
  readonly maxWorkers: number;
}

export interface WorkerTask {
  type: string;
  data: any;
  /** Transferable objects (ArrayBuffers, etc.) */
  transfer?: ArrayBuffer[];
}

// ─── Cache Layer ───────────────────────────────────────────────

export interface CacheLayer {
  get<T>(key: string): T | undefined;
  set<T>(key: string, value: T, ttlMs?: number): void;
  has(key: string): boolean;
  delete(key: string): void;
  clear(): void;
  /** Get or compute — avoids duplicate computation for same key */
  getOrCompute<T>(key: string, factory: () => T, ttlMs?: number): T;
}

// ─── Spatial Types (shared across modules) ─────────────────────

export interface LatLon {
  lat: number;
  lon: number;
}

export interface BoundingBox {
  north: number;
  south: number;
  east: number;
  west: number;
}

export interface Point2D {
  x: number;
  y: number;
}

export interface Point3D {
  x: number;
  y: number;
  z: number;
}

export type CRS = string; // e.g. 'EPSG:32615'

export interface Projection {
  /** Source CRS (e.g. 'EPSG:4326' for WGS84 lat/lon) */
  source: CRS;
  /** Target CRS */
  target: CRS;
  /** Project lat/lon to x/y */
  project(lat: number, lon: number): Point2D;
  /** Inverse project x/y to lat/lon */
  unproject(x: number, y: number): LatLon;
}

// ─── Progress Reporting ────────────────────────────────────────

export interface ProgressReporter {
  report(stage: string, current: number, total: number, message?: string): void;
  isCancelled(): boolean;
}

// ─── Plugin System ─────────────────────────────────────────────

export interface Plugin extends Module {
  /** Plugin capabilities — what this plugin provides */
  readonly provides: PluginCapability[];
}

export type PluginCapability =
  | { type: 'importer'; format: string }
  | { type: 'exporter'; format: string }
  | { type: 'road-generator'; name: string }
  | { type: 'terrain-processor'; name: string }
  | { type: 'validator'; name: string }
  | { type: 'visualization-layer'; name: string }
  | { type: 'tool'; name: string };

export interface PluginManager {
  register(plugin: Plugin): void;
  unregister(pluginId: string): void;
  getAll(): Plugin[];
  getByCapability(capability: PluginCapability['type']): Plugin[];
}
