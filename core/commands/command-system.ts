/**
 * Command System — typed command registration and execution.
 *
 * Commands are named actions (e.g. "export.run", "map.zoomIn")
 * that can be triggered from menus, keyboard shortcuts, or scripts.
 * Each command has a handler and optional preconditions.
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';

export interface CommandContext {
  /** Arguments passed to the command */
  args: Record<string, unknown>;
  /** Get a service from the DI container */
  getService<T>(token: string): T | undefined;
}

export interface Command {
  id: string;
  /** Display label for menus */
  label?: string;
  /** Category for grouping */
  category?: string;
  /** Keyboard shortcut (e.g. "Ctrl+S") */
  shortcut?: string;
  /** Icon name */
  icon?: string;
  /** Handler function */
  handler: (ctx: CommandContext) => void | Promise<void>;
  /** Predicate to check if command is currently enabled */
  isEnabled?: (ctx: CommandContext) => boolean;
  /** Predicate to check if command is currently visible */
  isVisible?: (ctx: CommandContext) => boolean;
}

export interface CommandRegistry {
  register(command: Command): void;
  unregister(id: string): void;
  execute(id: string, args?: Record<string, unknown>): Promise<void>;
  canExecute(id: string, args?: Record<string, unknown>): boolean;
  getCommand(id: string): Command | undefined;
  getAll(): Command[];
  getByCategory(category: string): Command[];
}

// ─── Events ────────────────────────────────────────────────────

export const COMMAND_EVENTS = {
  REGISTERED: 'command:registered',
  UNREGISTERED: 'command:unregistered',
  EXECUTED: 'command:executed',
} as const;

// ─── Implementation ────────────────────────────────────────────

export class CommandRegistryImpl implements CommandRegistry {
  private commands = new Map<string, Command>();
  private serviceResolver: (token: string) => unknown | undefined;

  constructor(
    private events: EventBus,
    private logger: Logger,
    serviceResolver?: (token: string) => unknown | undefined,
  ) {
    this.serviceResolver = serviceResolver ?? (() => undefined);
  }

  setServiceResolver(resolver: (token: string) => unknown | undefined): void {
    this.serviceResolver = resolver;
  }

  register(command: Command): void {
    if (this.commands.has(command.id)) {
      this.logger.warn(`Command already registered: ${command.id}`);
      return;
    }
    this.commands.set(command.id, command);
    this.events.emit(COMMAND_EVENTS.REGISTERED, command);
  }

  unregister(id: string): void {
    const cmd = this.commands.get(id);
    if (!cmd) return;
    this.commands.delete(id);
    this.events.emit(COMMAND_EVENTS.UNREGISTERED, cmd);
  }

  async execute(id: string, args: Record<string, unknown> = {}): Promise<void> {
    const cmd = this.commands.get(id);
    if (!cmd) {
      this.logger.error(`Command not found: ${id}`);
      return;
    }
    const ctx: CommandContext = {
      args,
      getService: <T>(token: string): T | undefined => this.serviceResolver(token) as T | undefined,
    };
    if (cmd.isEnabled && !cmd.isEnabled(ctx)) {
      this.logger.warn(`Command not enabled: ${id}`);
      return;
    }
    try {
      await cmd.handler(ctx);
      this.events.emit(COMMAND_EVENTS.EXECUTED, { id, args });
    } catch (err) {
      this.logger.error(`Command execution failed: ${id}`, err);
    }
  }

  canExecute(id: string, args: Record<string, unknown> = {}): boolean {
    const cmd = this.commands.get(id);
    if (!cmd) return false;
    if (!cmd.isEnabled) return true;
    const ctx: CommandContext = {
      args,
      getService: <T>(token: string): T | undefined => this.serviceResolver(token) as T | undefined,
    };
    return cmd.isEnabled(ctx);
  }

  getCommand(id: string): Command | undefined { return this.commands.get(id); }
  getAll(): Command[] { return Array.from(this.commands.values()); }

  getByCategory(category: string): Command[] {
    return this.getAll().filter(c => c.category === category);
  }
}
