import { ipcMain } from 'electron';
import type { GeoBounds, TerrainProfile, GenerationPlan } from '../../shared/types/terrain';
import {
  NATIVE_GET_VERSION,
  NATIVE_GET_MEMORY_USAGE,
  NATIVE_PLAN_GENERATION,
  NATIVE_START_GENERATION,
  NATIVE_CANCEL_GENERATION,
  NATIVE_GET_PROGRESS,
} from '../../shared/ipcChannels-electron';

export interface NativeAddon {
  getVersion(): string;
  planGeneration(bounds: GeoBounds, profile: TerrainProfile): GenerationPlan;
  startGeneration(sessionId: string, plan: GenerationPlan): string;
  cancelGeneration(jobId: string): void;
  getProgress(jobId: string): {
    jobId: string;
    state: string;
    overallProgress: number;
    currentTile: string;
    tileProgress: number;
    message: string;
  };
}

export function registerNativeHandlers(nativeAddon: NativeAddon | null): void {
  ipcMain.handle(NATIVE_GET_VERSION, () => {
    return nativeAddon?.getVersion?.() ?? '0.0.0-dev';
  });

  ipcMain.handle(NATIVE_GET_MEMORY_USAGE, () => {
    const mem = process.memoryUsage();
    return Math.round(mem.rss / 1024 / 1024); // RSS in MB
  });

  ipcMain.handle(NATIVE_PLAN_GENERATION, async (_event: unknown, bounds: GeoBounds, profile: TerrainProfile) => {
    if (!nativeAddon) {
      const width = bounds.east - bounds.west;
      const height = bounds.north - bounds.south;
      const tiles: GenerationPlan['tiles'] = [];
      const rows = Math.min(4, Math.max(1, Math.ceil(height * 2)));
      const cols = Math.min(4, Math.max(1, Math.ceil(width * 2)));

      for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
          tiles.push({
            row: r,
            col: c,
            bounds: {
              west: bounds.west + (c / cols) * width,
              east: bounds.west + ((c + 1) / cols) * width,
              south: bounds.south + (r / rows) * height,
              north: bounds.south + ((r + 1) / rows) * height,
            },
          });
        }
      }

      return {
        zoom: 12,
        tiles,
        estimatedMemoryMb: tiles.length * 256,
        estimatedDurationSec: tiles.length * 45,
      };
    }
    return nativeAddon.planGeneration(bounds, profile);
  });

  ipcMain.handle(NATIVE_START_GENERATION, async (_event: unknown, sessionId: string, plan: GenerationPlan) => {
    if (!nativeAddon) {
      return sessionId;
    }
    return nativeAddon.startGeneration(sessionId, plan);
  });

  ipcMain.handle(NATIVE_CANCEL_GENERATION, async (_event: unknown, jobId: string) => {
    if (!nativeAddon) {
      return;
    }
    return nativeAddon.cancelGeneration(jobId);
  });

  ipcMain.handle(NATIVE_GET_PROGRESS, async (_event: unknown, jobId: string) => {
    if (!nativeAddon) {
      return {
        jobId,
        state: 'complete',
        overallProgress: 1.0,
        currentTile: 'chunk_0_0',
        tileProgress: 1.0,
        message: 'Generation complete (mock)',
      };
    }
    return nativeAddon.getProgress(jobId);
  });
}
