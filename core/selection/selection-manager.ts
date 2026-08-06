/**
 * Selection Manager — tracks the current selection (tiles, roads, layers, etc.).
 *
 * Supports single-select, multi-select, and selection by type.
 * Emits events when selection changes.
 */

import type { EventBus } from '../interfaces';

export type SelectionType =
  | 'tile'
  | 'road'
  | 'railway'
  | 'building'
  | 'layer'
  | 'feature'
  | 'none';

export interface SelectionItem {
  id: string;
  type: SelectionType;
  /** Optional display label */
  label?: string;
  /** Optional bounds for viewport focusing */
  bounds?: { north: number; south: number; east: number; west: number };
  /** Arbitrary metadata */
  metadata?: Record<string, unknown>;
}

export interface SelectionManager {
  select(item: SelectionItem, additive?: boolean): void;
  selectMany(items: SelectionItem[], additive?: boolean): void;
  deselect(id: string): void;
  deselectAll(): void;
  isSelected(id: string): boolean;
  getSelection(): SelectionItem[];
  getPrimary(): SelectionItem | undefined;
  getByType(type: SelectionType): SelectionItem[];
  count(): number;
}

// ─── Events ────────────────────────────────────────────────────

export const SELECTION_EVENTS = {
  CHANGED: 'selection:changed',
  CLEARED: 'selection:cleared',
} as const;

// ─── Implementation ────────────────────────────────────────────

export class SelectionManagerImpl implements SelectionManager {
  private items = new Map<string, SelectionItem>();
  private order: string[] = [];

  constructor(private events: EventBus) {}

  select(item: SelectionItem, additive = false): void {
    if (!additive) this.deselectAll();
    if (!this.items.has(item.id)) {
      this.items.set(item.id, item);
      this.order.push(item.id);
    }
    this.emitChanged();
  }

  selectMany(items: SelectionItem[], additive = false): void {
    if (!additive) this.deselectAll();
    for (const item of items) {
      if (!this.items.has(item.id)) {
        this.items.set(item.id, item);
        this.order.push(item.id);
      }
    }
    this.emitChanged();
  }

  deselect(id: string): void {
    if (this.items.delete(id)) {
      this.order = this.order.filter(i => i !== id);
      this.emitChanged();
    }
  }

  deselectAll(): void {
    if (this.items.size === 0) return;
    this.items.clear();
    this.order = [];
    this.events.emit(SELECTION_EVENTS.CLEARED, undefined);
    this.emitChanged();
  }

  isSelected(id: string): boolean { return this.items.has(id); }

  getSelection(): SelectionItem[] {
    return this.order.map(id => this.items.get(id)!).filter(Boolean);
  }

  getPrimary(): SelectionItem | undefined {
    return this.order.length > 0 ? this.items.get(this.order[0]) : undefined;
  }

  getByType(type: SelectionType): SelectionItem[] {
    return this.getSelection().filter(i => i.type === type);
  }

  count(): number { return this.items.size; }

  private emitChanged(): void {
    this.events.emit(SELECTION_EVENTS.CHANGED, this.getSelection());
  }
}
