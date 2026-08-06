/**
 * Job System — background task management with progress, cancellation, and queueing.
 *
 * Jobs are long-running operations (exports, terrain generation, road extraction)
 * that run off the UI thread. They report progress and can be cancelled.
 */

import type { EventBus } from '../interfaces';
import type { Logger } from '../interfaces';

export type JobStatus = 'pending' | 'running' | 'completed' | 'failed' | 'cancelled';

export interface JobProgress {
  /** 0..1 */
  percentage: number;
  /** Current stage label */
  stage: string;
  /** Optional detailed message */
  message?: string;
  /** Items processed / total items */
  current?: number;
  total?: number;
}

export interface JobOptions {
  /** Priority (higher = runs first) */
  priority?: number;
  /** Max concurrent jobs of this type */
  concurrencyKey?: string;
  /** Timeout in ms (0 = no timeout) */
  timeoutMs?: number;
  /** Retry count on failure */
  retries?: number;
}

export interface JobResult {
  success: boolean;
  data?: unknown;
  error?: string;
}

export interface Job {
  id: string;
  type: string;
  title: string;
  status: JobStatus;
  progress: JobProgress;
  options: JobOptions;
  startedAt?: number;
  completedAt?: number;
  result?: JobResult;
}

export type JobHandler = (job: Job, cancelToken: CancelToken) => Promise<JobResult>;

export interface CancelToken {
  isCancelled(): boolean;
  cancel(): void;
  /** Register cleanup on cancel */
  onCancel(fn: () => void): void;
}

// ─── Cancel Token ──────────────────────────────────────────────

export class CancelTokenImpl implements CancelToken {
  private _cancelled = false;
  private cleanupFns: (() => void)[] = [];

  isCancelled(): boolean { return this._cancelled; }
  cancel(): void {
    if (this._cancelled) return;
    this._cancelled = true;
    for (const fn of this.cleanupFns) {
      try { fn(); } catch { /* ignore */ }
    }
  }
  onCancel(fn: () => void): void { this.cleanupFns.push(fn); }
}

// ─── Events ────────────────────────────────────────────────────

export const JOB_EVENTS = {
  STARTED: 'job:started',
  PROGRESS: 'job:progress',
  COMPLETED: 'job:completed',
  FAILED: 'job:failed',
  CANCELLED: 'job:cancelled',
  REMOVED: 'job:removed',
} as const;

// ─── Job System Implementation ─────────────────────────────────

let nextJobId = 0;

export class JobSystem {
  private jobs = new Map<string, Job>();
  private handlers = new Map<string, JobHandler>();
  private cancelTokens = new Map<string, CancelTokenImpl>();
  private activeByConcurrencyKey = new Map<string, number>();
  private pendingQueue: Job[] = [];
  private maxConcurrent = 2;

  constructor(
    private events: EventBus,
    private logger: Logger,
  ) {}

  /** Register a handler for a job type */
  registerHandler(type: string, handler: JobHandler): void {
    this.handlers.set(type, handler);
  }

  /** Submit a new job */
  submit(type: string, title: string, options: JobOptions = {}): string {
    const id = `job-${++nextJobId}`;
    const job: Job = {
      id,
      type,
      title,
      status: 'pending',
      progress: { percentage: 0, stage: 'Queued' },
      options,
    };
    this.jobs.set(id, job);
    this.pendingQueue.push(job);
    this.pendingQueue.sort((a, b) => (b.options.priority ?? 0) - (a.options.priority ?? 0));
    this.processQueue();
    return id;
  }

  /** Cancel a running or pending job */
  cancel(jobId: string): void {
    const job = this.jobs.get(jobId);
    if (!job) return;

    if (job.status === 'running') {
      const token = this.cancelTokens.get(jobId);
      token?.cancel();
    } else if (job.status === 'pending') {
      job.status = 'cancelled';
      this.pendingQueue = this.pendingQueue.filter(j => j.id !== jobId);
      this.events.emit(JOB_EVENTS.CANCELLED, job);
    }
  }

  /** Remove a job from history (after user dismisses) */
  remove(jobId: string): void {
    this.jobs.delete(jobId);
    this.cancelTokens.delete(jobId);
    this.events.emit(JOB_EVENTS.REMOVED, { jobId });
  }

  getJob(jobId: string): Job | undefined { return this.jobs.get(jobId); }
  getActiveJobs(): Job[] { return Array.from(this.jobs.values()).filter(j => j.status === 'running'); }
  getAll(): Job[] { return Array.from(this.jobs.values()); }
  setMaxConcurrent(n: number): void { this.maxConcurrent = n; }

  /** Update progress for a running job (called by handler) */
  updateProgress(jobId: string, progress: JobProgress): void {
    const job = this.jobs.get(jobId);
    if (!job || job.status !== 'running') return;
    job.progress = progress;
    this.events.emit(JOB_EVENTS.PROGRESS, { jobId, progress });
  }

  // ─── Internal ────────────────────────────────────────────────

  private async processQueue(): Promise<void> {
    const activeCount = this.getActiveJobs().length;
    if (activeCount >= this.maxConcurrent) return;

    const next = this.pendingQueue.shift();
    if (!next) return;

    const concurrencyKey = next.options.concurrencyKey;
    if (concurrencyKey) {
      const active = this.activeByConcurrencyKey.get(concurrencyKey) ?? 0;
      if (active >= 1) {
        // Re-queue and wait
        this.pendingQueue.unshift(next);
        return;
      }
      this.activeByConcurrencyKey.set(concurrencyKey, active + 1);
    }

    const handler = this.handlers.get(next.type);
    if (!handler) {
      next.status = 'failed';
      next.result = { success: false, error: `No handler for job type: ${next.type}` };
      this.events.emit(JOB_EVENTS.FAILED, next);
      return;
    }

    const cancelToken = new CancelTokenImpl();
    this.cancelTokens.set(next.id, cancelToken);

    next.status = 'running';
    next.startedAt = Date.now();
    this.events.emit(JOB_EVENTS.STARTED, next);

    try {
      const result = await handler(next, cancelToken);
      next.result = result;
      next.status = result.success ? 'completed' : 'failed';
      next.completedAt = Date.now();
      this.events.emit(result.success ? JOB_EVENTS.COMPLETED : JOB_EVENTS.FAILED, next);
    } catch (err) {
      next.status = 'failed';
      next.result = { success: false, error: err instanceof Error ? err.message : String(err) };
      next.completedAt = Date.now();
      this.events.emit(JOB_EVENTS.FAILED, next);
      this.logger.error(`Job ${next.id} (${next.type}) failed:`, err);
    } finally {
      this.cancelTokens.delete(next.id);
      if (concurrencyKey) {
        const active = this.activeByConcurrencyKey.get(concurrencyKey) ?? 1;
        this.activeByConcurrencyKey.set(concurrencyKey, Math.max(0, active - 1));
      }
      // Process next in queue
      this.processQueue();
    }
  }
}
