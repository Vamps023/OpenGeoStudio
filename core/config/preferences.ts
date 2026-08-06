/**
 * Preferences — user-level settings (theme, language, UI layout).
 *
 * Unlike Config (app-level), Preferences are user-customizable
 * and persisted across sessions.
 */

export type PrefValue = string | number | boolean | string[] | Record<string, unknown>;

export interface PrefDefinition {
  key: string;
  label: string;
  description?: string;
  type: 'string' | 'number' | 'boolean' | 'select' | 'multiselect' | 'color';
  default: PrefValue;
  options?: { label: string; value: PrefValue }[];
  min?: number;
  max?: number;
  category: string;
}

export interface PreferencesStore {
  get<T extends PrefValue>(key: string, defaultValue?: T): T;
  set(key: string, value: PrefValue): void;
  has(key: string): boolean;
  delete(key: string): void;
  getAll(): Record<string, PrefValue>;
  onChange(key: string, handler: (value: PrefValue) => void): () => void;
  getDefinitions(): PrefDefinition[];
  getCategories(): string[];
}

// ─── Implementation ────────────────────────────────────────────

export class PreferencesImpl implements PreferencesStore {
  private values = new Map<string, PrefValue>();
  private watchers = new Map<string, Set<(value: PrefValue) => void>>();
  private definitions: PrefDefinition[];

  constructor(definitions: PrefDefinition[] = DEFAULT_PREFS) {
    this.definitions = definitions;
    for (const def of definitions) {
      this.values.set(def.key, def.default);
    }
  }

  get<T extends PrefValue>(key: string, defaultValue?: T): T {
    return (this.values.get(key) ?? defaultValue) as T;
  }

  set(key: string, value: PrefValue): void {
    this.values.set(key, value);
    const watchers = this.watchers.get(key);
    if (watchers) {
      for (const w of watchers) w(value);
    }
  }

  has(key: string): boolean { return this.values.has(key); }
  delete(key: string): void { this.values.delete(key); }

  getAll(): Record<string, PrefValue> {
    return Object.fromEntries(this.values);
  }

  onChange(key: string, handler: (value: PrefValue) => void): () => void {
    if (!this.watchers.has(key)) {
      this.watchers.set(key, new Set());
    }
    this.watchers.get(key)!.add(handler);
    return () => { this.watchers.get(key)?.delete(handler); };
  }

  getDefinitions(): PrefDefinition[] { return this.definitions; }

  getCategories(): string[] {
    return [...new Set(this.definitions.map(d => d.category))];
  }
}

// ─── Default Preferences ───────────────────────────────────────

export const DEFAULT_PREFS: PrefDefinition[] = [
  {
    key: 'ui.theme',
    label: 'Theme',
    description: 'Color scheme for the application UI',
    type: 'select',
    default: 'dark',
    options: [
      { label: 'Dark', value: 'dark' },
      { label: 'Light', value: 'light' },
      { label: 'System', value: 'system' },
    ],
    category: 'Appearance',
  },
  {
    key: 'ui.accentColor',
    label: 'Accent Color',
    type: 'color',
    default: '#06b6d4',
    category: 'Appearance',
  },
  {
    key: 'ui.fontSize',
    label: 'Font Size',
    type: 'number',
    default: 14,
    min: 10,
    max: 24,
    category: 'Appearance',
  },
  {
    key: 'ui.layout.dockPosition',
    label: 'Panel Dock Position',
    type: 'select',
    default: 'right',
    options: [
      { label: 'Left', value: 'left' },
      { label: 'Right', value: 'right' },
      { label: 'Bottom', value: 'bottom' },
    ],
    category: 'Appearance',
  },
  {
    key: 'ui.showStatusBar',
    label: 'Show Status Bar',
    type: 'boolean',
    default: true,
    category: 'Appearance',
  },
  {
    key: 'map.defaultZoom',
    label: 'Default Map Zoom',
    type: 'number',
    default: 10,
    min: 1,
    max: 19,
    category: 'Map',
  },
  {
    key: 'map.defaultLocation',
    label: 'Default Location',
    type: 'string',
    default: '0,0',
    category: 'Map',
  },
  {
    key: 'viewer3d.fov',
    label: '3D Viewer FOV',
    type: 'number',
    default: 60,
    min: 20,
    max: 120,
    category: '3D Viewer',
  },
  {
    key: 'viewer3d.antialiasing',
    label: 'Antialiasing',
    type: 'select',
    default: 'msaa4x',
    options: [
      { label: 'None', value: 'none' },
      { label: 'MSAA 2x', value: 'msaa2x' },
      { label: 'MSAA 4x', value: 'msaa4x' },
      { label: 'MSAA 8x', value: 'msaa8x' },
    ],
    category: '3D Viewer',
  },
  {
    key: 'export.defaultOutputDir',
    label: 'Default Export Directory',
    type: 'string',
    default: '',
    category: 'Export',
  },
  {
    key: 'export.overwriteExisting',
    label: 'Overwrite Existing Files',
    type: 'boolean',
    default: false,
    category: 'Export',
  },
  {
    key: 'language',
    label: 'Language',
    type: 'select',
    default: 'en',
    options: [
      { label: 'English', value: 'en' },
      { label: 'Français', value: 'fr' },
      { label: 'Deutsch', value: 'de' },
      { label: 'Español', value: 'es' },
    ],
    category: 'General',
  },
];
