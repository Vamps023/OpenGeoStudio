/**
 * HTTP download utilities with retry, redirect handling, and bounded parallelism
 */

import * as https from 'https';
import { redactUrl, MAX_CONCURRENT_DOWNLOADS } from './types';

export interface DownloadTask<T> {
  url: string;
  x: number;
  y: number;
  processor: (buffer: Buffer) => Promise<T> | T;
}

export interface DownloadResult<T> {
  success: boolean;
  data?: T;
  x: number;
  y: number;
  error?: string;
}

export async function downloadBuffer(url: string, maxRedirects = 5, timeoutMs = 30000): Promise<Buffer> {
  if (maxRedirects <= 0) {
    throw new Error(`[Download] Too many redirects for ${redactUrl(url)}`);
  }

  return new Promise((resolve, reject) => {
    const req = https.get(url, (res) => {
      if (res.statusCode === 301 || res.statusCode === 302 || res.statusCode === 307 || res.statusCode === 308) {
        const location = res.headers.location;
        if (location) {
          res.resume();
          downloadBuffer(location, maxRedirects - 1, timeoutMs).then(resolve).catch(reject);
          return;
        }
      }

      if (res.statusCode === 429) {
        const retryAfter = res.headers['retry-after'];
        res.resume();
        const err = new Error(`[Download] HTTP 429 (Rate Limited) for ${redactUrl(url)}`) as Error & { retryAfter?: number };
        err.retryAfter = retryAfter ? parseInt(retryAfter, 10) * 1000 : undefined;
        reject(err);
        return;
      }

      if (res.statusCode === 401) {
        res.resume();
        reject(new Error(`[Download] HTTP 401 (Unauthorized) for ${redactUrl(url)} — check API key`));
        return;
      }

      if (res.statusCode !== 200) {
        res.resume();
        reject(new Error(`[Download] HTTP ${res.statusCode} for ${redactUrl(url)}`));
        return;
      }

      const chunks: Buffer[] = [];
      res.on('data', (chunk: Buffer) => chunks.push(chunk));
      res.on('end', () => resolve(Buffer.concat(chunks)));
      res.on('error', reject);
    });

    req.on('error', reject);
    req.setTimeout(timeoutMs, () => {
      req.destroy(new Error(`[Download] Timeout after ${timeoutMs}ms for ${redactUrl(url)}`));
    });
  });
}

export async function downloadTileWithRetry(url: string, retries = 3, timeoutMs = 30000): Promise<Buffer> {
  for (let i = 0; i < retries; i++) {
    try {
      return await downloadBuffer(url, 5, timeoutMs);
    } catch (err) {
      const isRateLimit = (err as Error).message.includes('HTTP 429');
      const retryAfterMs = (err as Error & { retryAfter?: number }).retryAfter;
      if (i === retries - 1) throw err;
      const delay = isRateLimit
        ? (retryAfterMs ?? 1000 * Math.pow(2, i + 2))
        : 500 * Math.pow(2, i);
      await new Promise((r) => setTimeout(r, delay));
    }
  }
  throw new Error('Unreachable');
}

export async function parallelDownload<T>(
  tasks: DownloadTask<T>[],
  maxConcurrency = MAX_CONCURRENT_DOWNLOADS,
  onProgress?: (completed: number, total: number) => void
): Promise<DownloadResult<T>[]> {
  const results: DownloadResult<T>[] = [];
  let completed = 0;
  let index = 0;

  async function worker(): Promise<void> {
    while (index < tasks.length) {
      const taskIndex = index++;
      const task = tasks[taskIndex];

      try {
        const buffer = await downloadTileWithRetry(task.url);
        const data = await task.processor(buffer);
        results[taskIndex] = { success: true, data, x: task.x, y: task.y };
      } catch (err) {
        results[taskIndex] = {
          success: false,
          x: task.x,
          y: task.y,
          error: (err as Error).message,
        };
      }

      completed++;
      if (onProgress) onProgress(completed, tasks.length);
    }
  }

  const workers = Array.from({ length: Math.min(maxConcurrency, tasks.length) }, () => worker());
  await Promise.all(workers);

  return results;
}
