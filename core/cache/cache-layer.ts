/**
 * Cache layer implementation.
 *
 * In-memory cache with TTL support and getOrCompute
 * for avoiding duplicate computations.
 */

import type { CacheLayer } from '../interfaces';

interface CacheEntry {
  value: any;
  expiresAt: number | null;
}

export class CacheLayerImpl implements CacheLayer {
  private cache = new Map<string, CacheEntry>();

  get<T>(key: string): T | undefined {
    const entry = this.cache.get(key);
    if (!entry) return undefined;
    if (entry.expiresAt !== null && Date.now() > entry.expiresAt) {
      this.cache.delete(key);
      return undefined;
    }
    return entry.value as T;
  }

  set<T>(key: string, value: T, ttlMs?: number): void {
    this.cache.set(key, {
      value,
      expiresAt: ttlMs ? Date.now() + ttlMs : null,
    });
  }

  has(key: string): boolean {
    return this.get(key) !== undefined;
  }

  delete(key: string): void {
    this.cache.delete(key);
  }

  clear(): void {
    this.cache.clear();
  }

  getOrCompute<T>(key: string, factory: () => T, ttlMs?: number): T {
    const existing = this.get<T>(key);
    if (existing !== undefined) return existing;
    const value = factory();
    this.set(key, value, ttlMs);
    return value;
  }
}
