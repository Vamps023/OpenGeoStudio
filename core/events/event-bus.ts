/**
 * Event Bus implementation.
 *
 * Simple pub/sub for cross-module communication.
 * No circular dependencies — modules communicate via events.
 */

import type { EventBus } from '../interfaces';

type EventHandler = (payload: any) => void;

export class EventBusImpl implements EventBus {
  private handlers = new Map<string, Set<EventHandler>>();

  on<T>(event: string, handler: (payload: T) => void): () => void {
    if (!this.handlers.has(event)) {
      this.handlers.set(event, new Set());
    }
    this.handlers.get(event)!.add(handler as EventHandler);

    // Return unsubscribe function
    return () => this.off(event, handler as EventHandler);
  }

  once<T>(event: string, handler: (payload: T) => void): () => void {
    const wrapper = (payload: T) => {
      this.off(event, wrapper as EventHandler);
      handler(payload);
    };
    return this.on(event, wrapper);
  }

  emit<T>(event: string, payload: T): void {
    const set = this.handlers.get(event);
    if (!set) return;
    for (const handler of set) {
      try {
        handler(payload);
      } catch {
        // no-op
      }
    }
  }

  off(event: string, handler: (...args: unknown[]) => void): void {
    const set = this.handlers.get(event);
    if (set) {
      set.delete(handler as EventHandler);
      if (set.size === 0) {
        this.handlers.delete(event);
      }
    }
  }
}
