const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('ogs', {
  // ── OSM buildings (Overpass fetch runs in main process to avoid CORS) ──
  fetchOsmBuildings: (query) => ipcRenderer.invoke('osm:fetchBuildings', query),

  // ── Terrain ──
  // onProgress (a function) cannot cross the structured-clone IPC boundary —
  // strip it; downloads report progress via the final result only.
  downloadTerrain: (bounds, options) => {
    const { onProgress, ...rest } = options || {}
    void onProgress
    return ipcRenderer.invoke('terrain:download', bounds, rest)
  },
  saveGeoTIFF: (data, options) => ipcRenderer.invoke('terrain:save-geotiff', data, options),
  exportTerrain: (options) => ipcRenderer.invoke('terrain:export', options),
  onExportProgress: (callback) => {
    const handler = (_event, progress) => callback(progress)
    ipcRenderer.on('terrain:export-progress', handler)
    return () => ipcRenderer.removeListener('terrain:export-progress', handler)
  },

  // ── Project files ──
  saveProject: (project) => ipcRenderer.invoke('project:save', project),
  loadProject: (projectId, projectName) => ipcRenderer.invoke('project:load', projectId, projectName),
  listProjects: () => ipcRenderer.invoke('project:list'),
  deleteProject: (projectId, projectName) => ipcRenderer.invoke('project:delete', projectId, projectName),
  getExportsDir: (projectId, projectName) => ipcRenderer.invoke('project:getExportsDir', projectId, projectName),

  // ── Workspace ──
  getWorkspacePath: () => ipcRenderer.invoke('workspace:getPath'),
})
