import { app, safeStorage } from 'electron';
import * as fs from 'fs/promises';
import * as path from 'path';
import type { ApiKeys } from '../../shared/types/terrain';

function getApiKeysPath(): string {
  return path.join(app.getPath('userData'), 'api-keys.enc');
}

function getSettingsPath(): string {
  return path.join(app.getPath('userData'), 'settings.json');
}

async function fileExists(p: string): Promise<boolean> {
  try {
    await fs.access(p);
    return true;
  } catch {
    return false;
  }
}

export async function saveApiKeysSecure(apiKeys: ApiKeys): Promise<boolean> {
  try {
    const serialized = JSON.stringify(apiKeys);
    const keysPath = getApiKeysPath();

    if (safeStorage.isEncryptionAvailable()) {
      const encrypted = safeStorage.encryptString(serialized);
      await fs.writeFile(keysPath, encrypted);
    } else {
      await fs.writeFile(keysPath, serialized, 'utf-8');
    }

    return true;
  } catch {
    return false;
  }
}

export async function getApiKeysSecure(): Promise<ApiKeys> {
  try {
    const keysPath = getApiKeysPath();
    if (!(await fileExists(keysPath))) return {};

    const data = await fs.readFile(keysPath);

    if (safeStorage.isEncryptionAvailable()) {
      const decrypted = safeStorage.decryptString(data);
      return JSON.parse(decrypted);
    } else {
      const text = data.toString('utf-8');
      return JSON.parse(text);
    }
  } catch {
    return {};
  }
}

export async function migrateApiKeysIfNeeded(): Promise<void> {
  try {
    const keysPath = getApiKeysPath();
    const settingsPath = getSettingsPath();
    if (await fileExists(keysPath)) return;
    if (!(await fileExists(settingsPath))) return;

    const data = await fs.readFile(settingsPath, 'utf-8');
    const settings = JSON.parse(data);

    if (settings.apiKeys && Object.keys(settings.apiKeys).length > 0) {
      await saveApiKeysSecure(settings.apiKeys);
      delete settings.apiKeys;
      await fs.writeFile(settingsPath, JSON.stringify(settings, null, 2), 'utf-8');
    }
  } catch {
    // no-op
  }
}
