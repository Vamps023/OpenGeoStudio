/**
 * Notification System — user-facing toast/alert notifications.
 *
 * Supports severity levels, auto-dismiss, actions, and stacking.
 */

import type { EventBus } from '../interfaces';

export type NotificationSeverity = 'info' | 'success' | 'warning' | 'error';

export interface NotificationAction {
  label: string;
  handler: () => void;
}

export interface Notification {
  id: string;
  severity: NotificationSeverity;
  title: string;
  message?: string;
  /** Auto-dismiss after ms (0 = never) */
  timeout: number;
  actions: NotificationAction[];
  /** Timestamp created */
  createdAt: number;
  /** Whether this notification can be dismissed by user */
  dismissible: boolean;
}

export interface NotificationManager {
  show(notification: Omit<Notification, 'id' | 'createdAt'>): string;
  dismiss(id: string): void;
  dismissAll(): void;
  getAll(): Notification[];
  getById(id: string): Notification | undefined;
}

// ─── Events ────────────────────────────────────────────────────

export const NOTIFICATION_EVENTS = {
  ADDED: 'notification:added',
  DISMISSED: 'notification:dismissed',
  DISMISSED_ALL: 'notification:dismissed_all',
} as const;

// ─── Implementation ────────────────────────────────────────────

let nextId = 0;

export class NotificationManagerImpl implements NotificationManager {
  private notifications = new Map<string, Notification>();
  private timers = new Map<string, NodeJS.Timeout>();

  constructor(private events?: EventBus) {}

  show(notification: Omit<Notification, 'id' | 'createdAt'>): string {
    const id = `notif-${++nextId}`;
    const full: Notification = {
      ...notification,
      id,
      createdAt: Date.now(),
    };
    this.notifications.set(id, full);
    this.events?.emit(NOTIFICATION_EVENTS.ADDED, full);

    if (full.timeout > 0) {
      const timer = setTimeout(() => this.dismiss(id), full.timeout);
      this.timers.set(id, timer);
    }
    return id;
  }

  dismiss(id: string): void {
    const notif = this.notifications.get(id);
    if (!notif) return;
    this.notifications.delete(id);
    const timer = this.timers.get(id);
    if (timer) {
      clearTimeout(timer);
      this.timers.delete(id);
    }
    this.events?.emit(NOTIFICATION_EVENTS.DISMISSED, notif);
  }

  dismissAll(): void {
    for (const timer of this.timers.values()) {
      clearTimeout(timer);
    }
    this.timers.clear();
    this.notifications.clear();
    this.events?.emit(NOTIFICATION_EVENTS.DISMISSED_ALL, undefined);
  }

  getAll(): Notification[] {
    return Array.from(this.notifications.values());
  }

  getById(id: string): Notification | undefined {
    return this.notifications.get(id);
  }
}

// ─── Convenience helpers ───────────────────────────────────────

export function createNotification(
  severity: NotificationSeverity,
  title: string,
  message?: string,
  timeout = 5000,
): Omit<Notification, 'id' | 'createdAt'> {
  return {
    severity,
    title,
    message,
    timeout,
    actions: [],
    dismissible: true,
  };
}
