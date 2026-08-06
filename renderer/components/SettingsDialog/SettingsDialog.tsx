/**
 * SettingsDialog — centralized settings center.
 *
 * Two categories:
 *   - API Keys (global, application-wide, encrypted via safeStorage)
 *   - Project  (current project metadata — name, workspace, paths)
 *
 * Opened from the top-right Settings (⚙) button in the top bar.
 * Replaces the per-panel API Key UI that used to live in ExportPanel.
 */
import React, { useEffect, useState } from 'react';
import { X, Key, FolderTree, Eye, EyeOff, Check, ExternalLink } from 'lucide-react';
import { Settings as SettingsIPC } from '@renderer/core/ipc';
import { useCoreStore } from '@renderer/core/coreStore';
import type { ApiKeys } from '@types/terrain';

type Page = 'api-keys' | 'project';

const API_KEY_FIELDS: Array<{
  key: keyof ApiKeys;
  label: string;
  placeholder: string;
  link: string;
  linkLabel: string;
}> = [
  { key: 'opentopography', label: 'OpenTopography', placeholder: 'Paste OpenTopography API key', link: 'https://portal.opentopography.org/myopentopo', linkLabel: 'Get free key' },
  { key: 'mapbox', label: 'Mapbox Access Token', placeholder: 'pk.eyJ...', link: 'https://account.mapbox.com/access-tokens/', linkLabel: 'Get free token' },
  { key: 'maptiler', label: 'MapTiler API Key', placeholder: 'Paste MapTiler API key', link: 'https://cloud.maptiler.com/account/keys/', linkLabel: 'Get free key' },
  { key: 'gpxz', label: 'GPXZ API Key', placeholder: 'ak_...', link: 'https://www.gpxz.io/app/accounts/signup/', linkLabel: 'Get free key' },
  { key: 'stadia', label: 'Stadia Maps API Key', placeholder: 'Stadia Maps elevation API key', link: 'https://client.stadiamaps.com/signup/', linkLabel: 'Get free key' },
];

export const SettingsDialog: React.FC<{ onClose: () => void }> = ({ onClose }) => {
  const [page, setPage] = useState<Page>('api-keys');

  // ─── API Keys state ──────────────────────────────────────────
  const [apiKeys, setApiKeys] = useState<ApiKeys>({});
  const [showApiKeys, setShowApiKeys] = useState(false);
  const [apiKeysSaved, setApiKeysSaved] = useState(false);

  useEffect(() => {
    SettingsIPC.getApiKeys().then(setApiKeys).catch(() => {});
  }, []);

  const handleSaveApiKeys = async () => {
    const ok = await SettingsIPC.setApiKeys(apiKeys);
    if (ok) {
      setApiKeysSaved(true);
      setTimeout(() => setApiKeysSaved(false), 2000);
      // Notify panels (ExportPanel, WorkflowWizard, etc.) that keys changed
      window.dispatchEvent(new CustomEvent('ogstudio:api-keys-changed'));
    }
  };

  const configuredCount = (['opentopography', 'mapbox', 'maptiler', 'gpxz', 'stadia'] as const)
    .filter(k => apiKeys[k]).length;

  // ─── Project state ───────────────────────────────────────────
  const { activeProject } = useCoreStore();

  // Close on Escape
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  return (
    <div
      className="fixed inset-0 z-[300] flex items-center justify-center bg-black/50 backdrop-blur-sm"
      onClick={onClose}
    >
      <div
        className="flex w-[720px] max-w-[92vw] h-[520px] max-h-[88vh] bg-surface-panel border border-edge rounded-xl shadow-overlay overflow-hidden"
        onClick={(e) => e.stopPropagation()}
        role="dialog"
        aria-label="Settings"
      >
        {/* Left navigation */}
        <nav className="w-44 shrink-0 border-r border-edge bg-surface-elevated p-2 flex flex-col gap-1">
          <h2 className="text-2xs font-semibold uppercase tracking-wider text-fg-muted px-2 py-1.5 mb-1">
            Settings
          </h2>
          <button
            onClick={() => setPage('api-keys')}
            className={`flex items-center gap-2 px-2.5 py-2 rounded text-xs transition-colors text-left ${
              page === 'api-keys' ? 'bg-accent/15 text-accent' : 'text-fg-secondary hover:bg-surface-hover hover:text-fg-primary'
            }`}
          >
            <Key className="w-3.5 h-3.5" />
            API Keys
            {configuredCount > 0 && (
              <span className="ml-auto text-[10px] text-accent">{configuredCount}/5</span>
            )}
          </button>
          <button
            onClick={() => setPage('project')}
            className={`flex items-center gap-2 px-2.5 py-2 rounded text-xs transition-colors text-left ${
              page === 'project' ? 'bg-accent/15 text-accent' : 'text-fg-secondary hover:bg-surface-hover hover:text-fg-primary'
            }`}
          >
            <FolderTree className="w-3.5 h-3.5" />
            Project
          </button>
        </nav>

        {/* Right content */}
        <div className="flex-1 flex flex-col min-w-0">
          {/* Header */}
          <div className="flex items-center justify-between px-5 py-3 border-b border-edge shrink-0">
            <h3 className="text-sm font-semibold text-fg-primary">
              {page === 'api-keys' ? 'API Keys' : 'Project Settings'}
            </h3>
            <button onClick={onClose} className="icon-btn icon-btn-sm" aria-label="Close">
              <X className="w-4 h-4" />
            </button>
          </div>

          {/* Body */}
          <div className="flex-1 overflow-y-auto p-5">
            {page === 'api-keys' && (
              <div className="space-y-4">
                <p className="text-xs text-fg-secondary leading-relaxed">
                  API keys are stored encrypted on your device using Electron's safeStorage.
                  They are never uploaded. All modules (Terrain, GIS, Roads, Railway, Export)
                  read from this single location.
                </p>
                <div className="space-y-3">
                  {API_KEY_FIELDS.map((f) => (
                    <div key={f.key} className="space-y-1">
                      <label className="text-xs text-fg-secondary flex items-center justify-between">
                        {f.label}
                        <a
                          href={f.link}
                          target="_blank"
                          rel="noopener"
                          className="text-accent hover:underline text-[10px] flex items-center gap-0.5"
                        >
                          {f.linkLabel}
                          <ExternalLink className="w-2.5 h-2.5" />
                        </a>
                      </label>
                      <input
                        type={showApiKeys ? 'text' : 'password'}
                        value={apiKeys[f.key] || ''}
                        onChange={(e) => setApiKeys({ ...apiKeys, [f.key]: e.target.value })}
                        placeholder={f.placeholder}
                        className="w-full bg-surface-hover border border-edge rounded text-xs py-1.5 px-2 text-fg-primary"
                      />
                    </div>
                  ))}
                </div>
                <div className="flex items-center gap-3 pt-1">
                  <button
                    onClick={() => setShowApiKeys(!showApiKeys)}
                    className="flex items-center gap-1 text-xs text-fg-secondary hover:text-fg-primary"
                  >
                    {showApiKeys ? <EyeOff className="w-3 h-3" /> : <Eye className="w-3 h-3" />}
                    {showApiKeys ? 'Hide' : 'Show'}
                  </button>
                  <button
                    onClick={handleSaveApiKeys}
                    className="ml-auto flex items-center gap-1 px-3 py-1.5 bg-accent hover:bg-accent/90 text-fg-primary text-xs rounded"
                  >
                    {apiKeysSaved ? <><Check className="w-3 h-3" /> Saved</> : 'Save Keys'}
                  </button>
                </div>
              </div>
            )}

            {page === 'project' && (
              <div className="space-y-5">
                {activeProject ? (
                  <>
                    <div className="space-y-1">
                      <label className="text-xs text-fg-secondary">Project Name</label>
                      <input
                        type="text"
                        value={activeProject.name}
                        readOnly
                        className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
                      />
                    </div>
                    <div className="space-y-1">
                      <label className="text-xs text-fg-secondary">Workspace</label>
                      <input
                        type="text"
                        value={activeProject.workspaceId}
                        readOnly
                        className="w-full bg-surface-hover border border-edge rounded text-sm py-1.5 px-2 text-fg-primary"
                      />
                    </div>
                    <div className="space-y-1">
                      <label className="text-xs text-fg-secondary">Project Folder</label>
                      <input
                        type="text"
                        value={activeProject.basePath ?? activeProject.filePath ?? '(unsaved)'}
                        readOnly
                        className="w-full bg-surface-hover border border-edge rounded text-xs py-1.5 px-2 text-fg-primary font-mono"
                      />
                    </div>
                    <div className="grid grid-cols-2 gap-3">
                      <div className="space-y-1">
                        <label className="text-xs text-fg-secondary">Created</label>
                        <input
                          type="text"
                          value={new Date(activeProject.createdAt).toLocaleString()}
                          readOnly
                          className="w-full bg-surface-hover border border-edge rounded text-xs py-1.5 px-2 text-fg-primary"
                        />
                      </div>
                      <div className="space-y-1">
                        <label className="text-xs text-fg-secondary">Modified</label>
                        <input
                          type="text"
                          value={new Date(activeProject.modifiedAt).toLocaleString()}
                          readOnly
                          className="w-full bg-surface-hover border border-edge rounded text-xs py-1.5 px-2 text-fg-primary"
                        />
                      </div>
                    </div>
                    {activeProject.bounds && (
                      <div className="space-y-1">
                        <label className="text-xs text-fg-secondary">Bounds (N/S/E/W)</label>
                        <input
                          type="text"
                          value={`${activeProject.bounds.north.toFixed(4)} / ${activeProject.bounds.south.toFixed(4)} / ${activeProject.bounds.east.toFixed(4)} / ${activeProject.bounds.west.toFixed(4)}`}
                          readOnly
                          className="w-full bg-surface-hover border border-edge rounded text-xs py-1.5 px-2 text-fg-primary font-mono"
                        />
                      </div>
                    )}
                    <p className="text-[10px] text-fg-muted leading-relaxed pt-2">
                      Project metadata is saved in the <code className="text-accent">.ogproj</code> file.
                      Map state (selection, tiles, layers) is persisted automatically via autosave.
                    </p>
                  </>
                ) : (
                  <div className="flex flex-col items-center justify-center h-full text-center gap-2">
                    <FolderTree className="w-10 h-10 text-fg-muted" />
                    <p className="text-sm text-fg-secondary">No project open</p>
                    <p className="text-xs text-fg-muted">
                      Create or open a project to view its settings here.
                    </p>
                  </div>
                )}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
