/**
 * Undo/Redo — command history with transactional grouping.
 *
 * Each undoable operation produces an UndoAction with do/undo callbacks.
 * Operations can be grouped into transactions (e.g. a drag operation
 * that fires many small changes should be one undo step).
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';

export interface UndoAction {
  /** Display label for the undo/redo menu */
  label: string;
  /** Redo (apply) the action */
  redo(): void | Promise<void>;
  /** Undo (revert) the action */
  undo(): void | Promise<void>;
  /** Optional: merge with previous action of same mergeId */
  mergeId?: string;
}

export interface UndoRedoStack {
  push(action: UndoAction): void;
  undo(): Promise<void>;
  redo(): Promise<void>;
  canUndo(): boolean;
  canRedo(): boolean;
  getUndoLabel(): string | undefined;
  getRedoLabel(): string | undefined;
  clear(): void;
  beginTransaction(label: string): void;
  endTransaction(): void;
}

// ─── Events ────────────────────────────────────────────────────

export const UNDO_REDO_EVENTS = {
  CHANGED: 'undo-redo:changed',
  UNDONE: 'undo-redo:undone',
  REDONE: 'undo-redo:redone',
} as const;

// ─── Implementation ────────────────────────────────────────────

interface Transaction {
  label: string;
  actions: UndoAction[];
}

export class UndoRedoStackImpl implements UndoRedoStack {
  private undoStack: UndoAction[] = [];
  private redoStack: UndoAction[] = [];
  private transaction: Transaction | null = null;
  private maxStack = 100;

  constructor(
    private events: EventBus,
    private logger: Logger,
  ) {}

  push(action: UndoAction): void {
    // If in a transaction, accumulate
    if (this.transaction) {
      this.transaction.actions.push(action);
      return;
    }

    // Try merge with top of undo stack
    if (action.mergeId && this.undoStack.length > 0) {
      const top = this.undoStack[this.undoStack.length - 1];
      if (top.mergeId === action.mergeId) {
        // Replace top with new action (caller should implement merge logic in redo)
        this.undoStack[this.undoStack.length - 1] = action;
        this.redoStack = [];
        this.emitChanged();
        return;
      }
    }

    this.undoStack.push(action);
    this.redoStack = [];
    if (this.undoStack.length > this.maxStack) {
      this.undoStack.shift();
    }
    this.emitChanged();
  }

  async undo(): Promise<void> {
    const action = this.undoStack.pop();
    if (!action) return;
    try {
      await action.undo();
      this.redoStack.push(action);
      this.events.emit(UNDO_REDO_EVENTS.UNDONE, action);
      this.emitChanged();
    } catch (err) {
      this.logger.error('Undo failed:', err);
      this.undoStack.push(action); // restore
    }
  }

  async redo(): Promise<void> {
    const action = this.redoStack.pop();
    if (!action) return;
    try {
      await action.redo();
      this.undoStack.push(action);
      this.events.emit(UNDO_REDO_EVENTS.REDONE, action);
      this.emitChanged();
    } catch (err) {
      this.logger.error('Redo failed:', err);
      this.redoStack.push(action); // restore
    }
  }

  canUndo(): boolean { return this.undoStack.length > 0; }
  canRedo(): boolean { return this.redoStack.length > 0; }

  getUndoLabel(): string | undefined {
    return this.undoStack[this.undoStack.length - 1]?.label;
  }

  getRedoLabel(): string | undefined {
    return this.redoStack[this.redoStack.length - 1]?.label;
  }

  clear(): void {
    this.undoStack = [];
    this.redoStack = [];
    this.transaction = null;
    this.emitChanged();
  }

  beginTransaction(label: string): void {
    if (this.transaction) {
      this.logger.warn('Transaction already in progress, nesting not supported');
      return;
    }
    this.transaction = { label, actions: [] };
  }

  endTransaction(): void {
    if (!this.transaction) return;
    const tx = this.transaction;
    this.transaction = null;
    if (tx.actions.length === 0) return;

    // Create a composite action
    const composite: UndoAction = {
      label: tx.label,
      redo: async () => { for (const a of tx.actions) await a.redo(); },
      undo: async () => { for (let i = tx.actions.length - 1; i >= 0; i--) await tx.actions[i].undo(); },
    };
    this.push(composite);
  }

  private emitChanged(): void {
    this.events.emit(UNDO_REDO_EVENTS.CHANGED, {
      canUndo: this.canUndo(),
      canRedo: this.canRedo(),
      undoLabel: this.getUndoLabel(),
      redoLabel: this.getRedoLabel(),
    });
  }
}
