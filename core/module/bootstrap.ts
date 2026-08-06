/**
 * Application bootstrapper.
 *
 * Wires up all modules, services, and providers.
 * This is the composition root — the only place that
 * knows about concrete implementations.
 */

import type {
  AppContext,
  Module,
  SettingsStore,
  Logger,
  WorkerPool,
  PluginManager,
  Plugin,
  PluginCapability,
} from '../interfaces';
import { ServiceRegistryImpl } from '../di/service-registry';
import { EventBusImpl } from '../events/event-bus';
import { CacheLayerImpl } from '../cache/cache-layer';
import { ModuleRegistry } from './module-registry';
import { PluginLoader } from './plugin-loader';
import { ContributionRegistry } from './contributions';
import { registerCoreCommands } from '../commands/core-commands';
import { SceneGraph } from '../scene/scene-graph';
import { LayerSystem, createDefaultLayers } from '../layers/layer-system';
import { WorkspaceManagerImpl } from '../workspace/workspace-manager';
import { NotificationManagerImpl } from '../notifications/notification-system';
import { JobSystem } from '../jobs/job-system';
import { CommandRegistryImpl } from '../commands/command-system';
import { UndoRedoStackImpl } from '../commands/undo-redo';
import { SelectionManagerImpl } from '../selection/selection-manager';
import { ProjectManagerImpl, PROJECT_EVENTS } from '../project/project-manager';
import { ProjectContextImpl } from '../project/projectContext';

// ─── Logger Implementation ─────────────────────────────────────

class ConsoleLogger implements Logger {
  constructor(private scope: string = 'App') {}

  debug(message: string, ...args: any[]): void {
    console.debug(`[${this.scope}] ${message}`, ...args);
  }

  info(message: string, ...args: any[]): void {
    console.info(`[${this.scope}] ${message}`, ...args);
  }

  warn(message: string, ...args: any[]): void {
    console.warn(`[${this.scope}] ${message}`, ...args);
  }

  error(message: string, ...args: any[]): void {
    console.error(`[${this.scope}] ${message}`, ...args);
  }

  child(scope: string): Logger {
    return new ConsoleLogger(`${this.scope}:${scope}`);
  }
}

// ─── Settings Store Implementation ─────────────────────────────

class InMemorySettingsStore implements SettingsStore {
  private settings = new Map<string, any>();
  private watchers = new Map<string, Set<(value: any) => void>>();

  get<T>(key: string, defaultValue: T): T {
    return this.settings.has(key) ? this.settings.get(key) : defaultValue;
  }

  set<T>(key: string, value: T): void {
    this.settings.set(key, value);
    const watchers = this.watchers.get(key);
    if (watchers) {
      for (const w of watchers) w(value);
    }
  }

  onChange(key: string, handler: (value: any) => void): () => void {
    if (!this.watchers.has(key)) {
      this.watchers.set(key, new Set());
    }
    this.watchers.get(key)!.add(handler);
    return () => {
      this.watchers.get(key)?.delete(handler);
    };
  }

  getAll(): Record<string, any> {
    return Object.fromEntries(this.settings);
  }
}

// ─── Plugin Manager Implementation ─────────────────────────────

class PluginManagerImpl implements PluginManager {
  private plugins = new Map<string, Plugin>();

  register(plugin: Plugin): void {
    if (this.plugins.has(plugin.id)) {
      console.warn(`[PluginManager] Plugin already registered: ${plugin.id}`);
      return;
    }
    this.plugins.set(plugin.id, plugin);
  }

  unregister(pluginId: string): void {
    this.plugins.delete(pluginId);
  }

  getAll(): Plugin[] {
    return Array.from(this.plugins.values());
  }

  getByCapability(capability: PluginCapability['type']): Plugin[] {
    return this.getAll().filter(p =>
      p.provides.some(c => c.type === capability)
    );
  }
}

// ─── Worker Pool (not yet implemented) ─────────────────────────
// WorkerPool is part of the AppContext architecture but not yet wired to
// worker_threads. No code currently calls workers.submit(), so this stub
// is a safe no-op placeholder. If a caller is added, this will throw to
// surface the missing implementation rather than silently failing.

class WorkerPoolStub implements WorkerPool {
  get activeWorkers(): number { return 0; }
  get maxWorkers(): number { return 4; }

  async submit<T>(_task: any): Promise<T> {
    throw new Error('WorkerPool not yet implemented — no code should call this');
  }
}

// ─── App Bootstrap ─────────────────────────────────────────────

export class AppBootstrap {
  private moduleRegistry: ModuleRegistry;
  private pluginLoader: PluginLoader;
  private pluginManager = new PluginManagerImpl();
  private context: AppContext | null = null;

  // Core service instances (created during init)
  private workspaceManager!: WorkspaceManagerImpl;
  private notificationManager!: NotificationManagerImpl;
  private jobSystem!: JobSystem;
  private commandRegistry!: CommandRegistryImpl;
  private undoRedoStack!: UndoRedoStackImpl;
  private selectionManager!: SelectionManagerImpl;
  private projectManager!: ProjectManagerImpl;
  private projectContext!: ProjectContextImpl;

  constructor(private logger: Logger = new ConsoleLogger('OpenGeoStudio')) {
    this.moduleRegistry = new ModuleRegistry(this.logger);
    // EventBus will be created in init(); pluginLoader needs it
    this.pluginLoader = new PluginLoader(this.logger, new EventBusImpl());
  }

  /** Register a core module */
  registerModule(module: Module): this {
    this.moduleRegistry.register(module);
    return this;
  }

  /** Register a plugin */
  registerPlugin(plugin: Plugin): this {
    this.pluginManager.register(plugin);
    return this;
  }

  /** Initialize the application — creates context and boots all modules */
  async init(): Promise<AppContext> {
    const logger = this.logger;
    const events = new EventBusImpl();
    const settings = new InMemorySettingsStore();
    const cache = new CacheLayerImpl();
    const workers = new WorkerPoolStub();
    const contributions = new ContributionRegistry(logger);
    const scene = new SceneGraph(events, logger);
    const layers = new LayerSystem(events, logger);
    createDefaultLayers(layers);

    // Create core service instances
    this.workspaceManager = new WorkspaceManagerImpl(events);
    this.notificationManager = new NotificationManagerImpl(events);
    this.jobSystem = new JobSystem(events, logger);
    this.commandRegistry = new CommandRegistryImpl(events, logger);
    this.undoRedoStack = new UndoRedoStackImpl(events, logger);
    this.selectionManager = new SelectionManagerImpl(events);
    this.projectManager = new ProjectManagerImpl(events, logger);
    // Enable autosave by default — writes every 60s when the project has unsaved changes
    this.projectManager.setAutosave(true, 60000);

    // ProjectContext — central state service (single source of truth)
    this.projectContext = new ProjectContextImpl(events, logger);

    // Wire ProjectManager events → ProjectContext sync
    // When a project is created, set its info. When opened, set info AND load
    // persisted state (terrain, GIS, scene, viewport, assets) from disk.
    // When closed, clear ProjectContext.
    events.on(PROJECT_EVENTS.CREATED, (project: any) => {
      this.projectContext.setProject(project.id, project.name, project.basePath ?? '');
    });
    events.on(PROJECT_EVENTS.OPENED, async (project: any) => {
      this.projectContext.setProject(project.id, project.name, project.basePath ?? '');
      // Load persisted ProjectContext state from project-context.json
      // This restores terrain, GIS, scene, viewport, and assets automatically
      if (project.basePath) {
        await this.projectContext.loadFromDisk(project.basePath);
      }
    });
    events.on(PROJECT_EVENTS.CLOSED, () => {
      this.projectContext.clearProject();
    });

    // Re-create plugin loader with the real event bus
    this.pluginLoader = new PluginLoader(logger, events);

    const context: AppContext = {
      services: null as any,
      events,
      settings,
      logger,
      workers,
      cache,
      jobs: this.jobSystem,
      commands: this.commandRegistry,
      undoRedo: this.undoRedoStack,
      notifications: this.notificationManager,
      selection: this.selectionManager,
      project: this.projectManager,
      projectContext: this.projectContext,
      workspace: this.workspaceManager,
      contributions,
      scene,
      layers,
    };

    const registry = new ServiceRegistryImpl(context);
    (context as any).services = registry;
    (registry as any).context = context;

    // Register ProjectContext as a service so commands can resolve it
    registry.registerInstance('projectContext' as any, this.projectContext);

    // Wire command registry's service resolver to the DI container
    this.commandRegistry.setServiceResolver((token: string) => {
      try { return registry.resolveOptional(token as any); } catch { return undefined; }
    });

    this.context = context;

    // Initialize all modules via the registry
    await this.moduleRegistry.initAll(context);

    // Register core commands (file, edit, view, help)
    registerCoreCommands(context);

    // Initialize all plugins
    for (const plugin of this.pluginManager.getAll()) {
      logger.info(`Initializing plugin: ${plugin.name} v${plugin.version}`);
      try {
        await plugin.init?.(context);
      } catch (err) {
        logger.error(`Failed to init plugin ${plugin.id}:`, err);
      }
    }

    // Activate default workspace
    this.workspaceManager.activate('home');

    logger.info(`App initialized: ${this.moduleRegistry.getIds().length} modules, ${this.pluginManager.getAll().length} plugins, ${contributions.getAllPanels().length} panels, ${this.commandRegistry.getAll().length} commands`);
    return context;
  }

  /** Shutdown — dispose all modules in reverse order */
  async shutdown(): Promise<void> {
    if (!this.context) return;
    const logger = this.context.logger;

    // Dispose plugins first
    for (const plugin of this.pluginManager.getAll()) {
      try {
        await plugin.dispose?.();
      } catch (err) {
        logger.error(`Error disposing plugin ${plugin.id}:`, err);
      }
    }

    // Dispose all modules via registry
    await this.moduleRegistry.disposeAll();

    this.context = null;
    logger.info('App shutdown complete');
  }

  getPluginManager(): PluginManager {
    return this.pluginManager;
  }

  getModuleRegistry(): ModuleRegistry { return this.moduleRegistry; }
  getWorkspaceManager(): WorkspaceManagerImpl { return this.workspaceManager; }
  getNotificationManager(): NotificationManagerImpl { return this.notificationManager; }
  getJobSystem(): JobSystem { return this.jobSystem; }
  getCommandRegistry(): CommandRegistryImpl { return this.commandRegistry; }
  getUndoRedoStack(): UndoRedoStackImpl { return this.undoRedoStack; }
  getSelectionManager(): SelectionManagerImpl { return this.selectionManager; }
  getProjectManager(): ProjectManagerImpl { return this.projectManager; }
  getPluginLoader(): PluginLoader { return this.pluginLoader; }
  getContributions(): ContributionRegistry { return this.context?.contributions ?? new ContributionRegistry(this.logger); }
}
