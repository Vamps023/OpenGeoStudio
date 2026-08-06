/**
 * Logger — structured logging with scoped child loggers.
 *
 * Supports log levels, scope hierarchies, and optional transport
 * backends (console, file, IPC).
 */

export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

export interface LogEntry {
  level: LogLevel;
  scope: string;
  message: string;
  args: unknown[];
  timestamp: number;
}

export interface LogTransport {
  log(entry: LogEntry): void;
}

export interface Logger {
  debug(message: string, ...args: unknown[]): void;
  info(message: string, ...args: unknown[]): void;
  warn(message: string, ...args: unknown[]): void;
  error(message: string, ...args: unknown[]): void;
  child(scope: string): Logger;
  /** Add a custom transport (e.g. file, IPC) */
  addTransport(transport: LogTransport): void;
  /** Set minimum log level */
  setLevel(level: LogLevel): void;
}

// ─── Console Transport ─────────────────────────────────────────

export class ConsoleTransport implements LogTransport {
  log(entry: LogEntry): void {
    const prefix = `[${entry.scope}]`;
    const consoleFn = entry.level === 'debug' ? console.debug
      : entry.level === 'info' ? console.info
      : entry.level === 'warn' ? console.warn
      : console.error;
    consoleFn(prefix, entry.message, ...entry.args);
  }
}

// ─── Logger Implementation ─────────────────────────────────────

const LEVEL_PRIORITY: Record<LogLevel, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
};

export class LoggerImpl implements Logger {
  private transports: LogTransport[] = [new ConsoleTransport()];
  private minLevel: LogLevel = 'debug';
  private children: LoggerImpl[] = [];

  constructor(private scope: string = 'App') {}

  debug(message: string, ...args: unknown[]): void { this.log('debug', message, args); }
  info(message: string, ...args: unknown[]): void { this.log('info', message, args); }
  warn(message: string, ...args: unknown[]): void { this.log('warn', message, args); }
  error(message: string, ...args: unknown[]): void { this.log('error', message, args); }

  child(scope: string): Logger {
    const child = new LoggerImpl(`${this.scope}:${scope}`);
    child.transports = this.transports; // share transports
    child.minLevel = this.minLevel;
    this.children.push(child);
    return child;
  }

  addTransport(transport: LogTransport): void {
    this.transports.push(transport);
    for (const child of this.children) {
      child.addTransport(transport);
    }
  }

  setLevel(level: LogLevel): void {
    this.minLevel = level;
    for (const child of this.children) {
      child.setLevel(level);
    }
  }

  private log(level: LogLevel, message: string, args: unknown[]): void {
    if (LEVEL_PRIORITY[level] < LEVEL_PRIORITY[this.minLevel]) return;
    const entry: LogEntry = {
      level,
      scope: this.scope,
      message,
      args,
      timestamp: Date.now(),
    };
    for (const transport of this.transports) {
      transport.log(entry);
    }
  }
}

// Singleton root logger
let rootLogger: LoggerImpl | null = null;

export function getLogger(scope?: string): Logger {
  if (!rootLogger) {
    rootLogger = new LoggerImpl('OpenGeoStudio');
  }
  return scope ? rootLogger.child(scope) : rootLogger;
}

export function setRootLogger(logger: LoggerImpl): void {
  rootLogger = logger;
}
