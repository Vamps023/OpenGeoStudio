const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('ogs', {
  downloadTerrain: (bounds, options) => ipcRenderer.invoke('terrain:download', bounds, options),
  saveGeoTIFF: (data, options) => ipcRenderer.invoke('terrain:save-geotiff', data, options),
  exportTerrain: (options) => ipcRenderer.invoke('terrain:export', options),
  onExportProgress: (callback) => {
    const handler = (_event, progress) => callback(progress)
    ipcRenderer.on('terrain:export-progress', handler)
    return () => ipcRenderer.removeListener('terrain:export-progress', handler)
  },
})
