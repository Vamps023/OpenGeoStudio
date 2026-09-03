const { app, BrowserWindow, ipcMain, dialog } = require('electron')
const path = require('node:path')
const fs = require('node:fs')
const { writeGeoTIFF } = require('./terrain/geotiff-writer.cjs')
const { downloadTerrainDEM } = require('./terrain/demDownloader.cjs')
const { executeExport } = require('./terrain/exportEngine.cjs')

const isDev = Boolean(process.env.OGS_DEV)

function createWindow() {
  const window = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 1000,
    minHeight: 640,
    backgroundColor: '#0b1220',
    title: 'OpenGeoStudio',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      preload: path.join(__dirname, 'preload.cjs'),
    },
  })

  if (isDev) {
    window.loadURL('http://localhost:5173')
  } else {
    window.loadFile(path.join(__dirname, '..', 'dist', 'index.html'))
  }
}

// ─── IPC: Terrain download ──────────────────────────────────────
ipcMain.handle('terrain:download', async (event, bounds, options) => {
  try {
    const result = await downloadTerrainDEM(bounds, options || {})
    return { success: true, data: result }
  } catch (err) {
    return { success: false, error: err.message }
  }
})

// ─── IPC: Save GeoTIFF ──────────────────────────────────────────
ipcMain.handle('terrain:save-geotiff', async (event, data, options) => {
  try {
    const result = await dialog.showSaveDialog({
      title: 'Save GeoTIFF',
      defaultPath: options.filename || 'terrain.tif',
      filters: [{ name: 'GeoTIFF', extensions: ['tif', 'tiff'] }],
    })
    if (result.canceled || !result.filePath) return { success: false, error: 'Cancelled' }
    const buffer = writeGeoTIFF(data.elevations, options)
    fs.writeFileSync(result.filePath, buffer)
    return { success: true, path: result.filePath }
  } catch (err) {
    return { success: false, error: err.message }
  }
})

// ─── IPC: Export terrain (heightmap + albedo) ───────────────────
ipcMain.handle('terrain:export', async (event, options) => {
  try {
    // Show a directory picker if no outputPath is provided
    let outputPath = options.outputPath
    if (!outputPath) {
      const result = await dialog.showOpenDialog({
        title: 'Select export folder',
        properties: ['openDirectory', 'createDirectory'],
      })
      if (result.canceled || !result.filePaths || result.filePaths.length === 0) {
        return { success: false, error: 'Cancelled' }
      }
      outputPath = result.filePaths[0]
    }

    // Forward onProgress via event.reply on the 'terrain:export-progress' channel
    const opts = { ...options, outputPath, onProgress: (p) => event.sender.send('terrain:export-progress', p) }
    const result = await executeExport(opts)
    return { success: true, ...result }
  } catch (err) {
    return { success: false, error: err.message }
  }
})

app.whenReady().then(() => {
  createWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})
