/**
 * Service Registry implementation.
 *
 * Lightweight dependency injection container.
 * Supports factory functions and singleton instances.
 */

import type { AppContext, ServiceRegistry, ServiceToken } from '../interfaces';

export class ServiceRegistryImpl implements ServiceRegistry {
  private factories = new Map<string, (ctx: AppContext) => any>();
  private instances = new Map<string, any>();
  private context: AppContext;

  constructor(context: AppContext) {
    this.context = context;
  }

  register<T>(token: ServiceToken<T>, factory: (ctx: AppContext) => T): void {
    this.factories.set(token, factory);
  }

  registerInstance<T>(token: ServiceToken<T>, instance: T): void {
    this.instances.set(token, instance);
  }

  resolve<T>(token: ServiceToken<T>): T {
    // Check for existing instance
    if (this.instances.has(token)) {
      return this.instances.get(token) as T;
    }

    // Create from factory
    const factory = this.factories.get(token);
    if (!factory) {
      throw new Error(`Service not registered: ${token}`);
    }

    const instance = factory(this.context);
    this.instances.set(token, instance);
    return instance as T;
  }

  resolveOptional<T>(token: ServiceToken<T>): T | undefined {
    try {
      return this.resolve(token);
    } catch {
      return undefined;
    }
  }

  has<T>(token: ServiceToken<T>): boolean {
    return this.factories.has(token) || this.instances.has(token);
  }
}
