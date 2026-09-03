const { app, BrowserWindow, ipcMain, dialog, protocol, net } = require('electron')
const path = require('node:path')
const fs = require('node:fs')
const { pathToFileURL } = require('node:url')
const { writeGeoTIFF } = require('./terrain/geotiff-writer.cjs')
const { downloadTerrainDEM } = require('./terrain/demDownloader.cjs')
const { executeExport } = require('./terrain/exportEngine.cjs')

const isDev = Boolean(process.env.OGS_DEV)
const isTest = Boolean(process.env.OGS_TEST)
const DIST_DIR = path.join(__dirname, '..', 'dist')

// Minimal MIME map for assets served via the custom `app://` protocol.
// Windows' registry often returns "text/plain" for .mjs / .js / .css / .json
// files, which the browser refuses to execute as a module script. Setting
// Content-Type explicitly ensures the renderer can load the worker bundle.
const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.htm':  'text/html; charset=utf-8',
  '.js':   'text/javascript; charset=utf-8',
  '.mjs':  'text/javascript; charset=utf-8',
  '.cjs':  'text/javascript; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg':  'image/svg+xml',
  '.png':  'image/png',
  '.jpg':  'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif':  'image/gif',
  '.webp': 'image/webp',
  '.ico':  'image/x-icon',
  '.woff': 'font/woff',
  '.woff2':'font/woff2',
  '.ttf':  'font/ttf',
  '.otf':  'font/otf',
  '.txt':  'text/plain; charset=utf-8',
  '.map':  'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.tif':  'image/tiff',
  '.tiff': 'image/tiff',
}
function mimeFor(filePath) {
  return MIME_TYPES[path.extname(filePath).toLowerCase()] || 'application/octet-stream'
}

// Software GL in test runs so capturePage reliably reflects what the map drew.
// Set OGS_HW=1 to keep hardware acceleration (reproduces the real user env).
if (isTest && !process.env.OGS_HW) app.disableHardwareAcceleration()
// Software-GL recovery relaunch (see maybeRelaunchWithSoftwareGL below).
if (process.env.OGS_SWGL) app.disableHardwareAcceleration()

// One-shot recovery: if the GPU cannot provide a WebGL context (common with
// outdated/broken GPU drivers), relaunch the app once with software rendering
// so the terrain map still works instead of showing a black screen.
let relaunchedForWebGL = false
function maybeRelaunchWithSoftwareGL(message) {
  if (relaunchedForWebGL || process.env.OGS_SWGL || isDev || isTest) return
  if (!/could not create (a )?webgl|webgl.{0,40}(is not supported|not available|failed to create)|failed to create (a )?webgl/i.test(message)) return
  relaunchedForWebGL = true
  console.warn('[ogs] WebGL context failed — relaunching once with software rendering…')
  process.env.OGS_SWGL = '1'
  app.relaunch({ args: process.argv.slice(1) })
  app.exit(0)
}

// Serve the production build over a custom protocol instead of file://.
// file:// breaks ES module scripts (opaque origin / CORS) and can report
// wrong MIME types; a privileged scheme behaves like a proper web origin.
protocol.registerSchemesAsPrivileged([
  {
    scheme: 'app',
    privileges: { standard: true, secure: true, supportFetchAPI: true, corsEnabled: true },
  },
])

function createWindow() {
  const isTest = Boolean(process.env.OGS_TEST)

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

  // Surface renderer problems in the terminal (also used by OGS_TEST runs)
  window.webContents.on('did-fail-load', (_event, code, description, url) => {
    console.error(`[ogs] failed to load ${url}: ${code} ${description}`)
  })
  window.webContents.on('console-message', (_event, ...args) => {
    const first = args[0]
    let level, message, sourceId, line
    if (first && typeof first === 'object') {
      level = first.level
      message = first.message
      sourceId = first.sourceId
      line = first.lineNumber
    } else {
      level = first
      message = args[1]
      line = args[2]
      sourceId = args[3]
    }
    const isError = level === 'error' || level === 3
    const isWarning = level === 'warning' || level === 2
    if (isError) {
      console.error(`[renderer] ${message} (${sourceId}:${line})`)
      maybeRelaunchWithSoftwareGL(message)
    }
    else if (isWarning) console.warn(`[renderer:warn] ${message}`)
  })

  if (isDev) {
    window.loadURL('http://localhost:5173')
  } else {
    window.loadURL('app://localhost/index.html')
  }

  if (isTest) {
    const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

    window.webContents.once('did-finish-load', () => {
      void (async () => {
        const wc = window.webContents
        try {
          const step = async (label, code) => {
            const result = await wc.executeJavaScript(code, true)
            console.log(`[e2e] ${label}: ${result}`)
          }

          await sleep(1200)
          await step('login', `(() => {
            const set = (el, v) => {
              const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set
              setter.call(el, v)
              el.dispatchEvent(new Event('input', { bubbles: true }))
            }
            const a = document.querySelector('#login-id')
            const b = document.querySelector('#login-password')
            if (!a || !b) return 'login inputs not found'
            set(a, 'Admin')
            set(b, 'Admin')
            document.querySelector('button[type=submit]').click()
            return 'submitted'
          })()`)

          await sleep(800)
          await step('new-project', `(() => {
            const btn = [...document.querySelectorAll('button')].find((b) => b.textContent.includes('New Project'))
            if (!btn) return 'button not found'
            btn.click()
            return 'dialog opened'
          })()`)

          await sleep(500)
          await step('create-project', `(() => {
            const input = document.querySelector('#project-name')
            if (!input) return 'input not found'
            const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set
            setter.call(input, 'MapTest')
            input.dispatchEvent(new Event('input', { bubbles: true }))
            const btn = [...document.querySelectorAll('button')].find((b) => b.textContent.includes('Create Project'))
            if (!btn) return 'create button not found'
            btn.click()
            return 'created'
          })()`)

          await sleep(800)
          await step('open-terrain', `(() => {
            const btn = [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Terrain')
            if (!btn) return 'terrain button not found'
            btn.click()
            return 'opening'
          })()`)

          await sleep(10000)
          await step('map-state', `(() => {
            const map = window.__ogsMap
            const c = document.querySelector('.maplibregl-canvas')
            const container = document.querySelector('.terrain-map-container')
            const tiles = performance.getEntriesByType('resource').filter((r) => r.name.includes('arcgisonline'))
            const pill = [...document.querySelectorAll('div')].some((d) => d.textContent.trim() === 'Loading map…' && d.offsetParent !== null)
            return JSON.stringify({
              mapReady: window.__ogsMapReady === true,
              webgl2: (() => { try { const g = document.createElement('canvas').getContext('webgl2'); if (!g) return 'UNAVAILABLE'; const dbg = g.getExtension('WEBGL_debug_renderer_info'); const r = dbg ? g.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : ''; return 'ok: ' + r } catch (e) { return 'ERROR: ' + e.message } })(),
              loadingPillVisible: pill,
              canvasAttrs: c ? c.width + 'x' + c.height : null,
              containerRect: container ? container.getBoundingClientRect().width + 'x' + container.getBoundingClientRect().height : null,
              mapState: map ? { loaded: map.loaded(), styleLoaded: map.isStyleLoaded(), tilesLoaded: map.areTilesLoaded() } : null,
              tileRequests: tiles.length,
            })
          })()`)

          await step('worker-spawn-test', `(() => {
            const workerFile = performance.getEntriesByType('resource').map((r) => r.name).find((n) => n.includes('maplibre-gl-worker'))
            const url = workerFile || new URL('./assets/' + Array.from(document.scripts).map((s) => s.src).join(''), window.location.href).href
            return new Promise((resolve) => {
              const results = {}
              try {
                const w = new Worker(url, { type: 'module' })
                w.onerror = (e) => { results.module = 'ERROR: ' + (e.message || 'unknown') }
                w.onmessage = () => { results.module = 'MESSAGE OK' }
                w.postMessage({ id: 1, type: 'ping' })
                setTimeout(() => {
                  if (!results.module) results.module = 'no response in 2.5s (no error)'
                  try { w.terminate() } catch {}
                  resolve(JSON.stringify({ workerUrlFound: !!workerFile, ...results }))
                }, 2500)
              } catch (err) {
                resolve(JSON.stringify({ spawnFailed: err.message }))
              }
            })
          })()`)

          await step('resize-test', `(() => {
            const map = window.__ogsMap
            if (!map) return 'no map'
            const before = map.getCanvas().width + 'x' + map.getCanvas().height
            map.resize()
            return new Promise((resolve) => setTimeout(() => {
              resolve(before + ' -> ' + map.getCanvas().width + 'x' + map.getCanvas().height + ' container=' + map.getContainer().offsetWidth + 'x' + map.getContainer().offsetHeight)
            }, 600))
          })()`)

          await step('shift-drag-test', `(() => {
            const map = window.__ogsMap
            const canvas = document.querySelector('.maplibregl-canvas')
            if (!map || !canvas) return 'no map or canvas'
            const rect = canvas.getBoundingClientRect()
            const startX = rect.left + 200
            const startY = rect.top + 200
            const moveX = rect.left + 350
            const moveY = rect.top + 350
            const opts = { bubbles: true, cancelable: true, view: window, button: 0, shiftKey: true }
            // Inspect event listeners
            const listenersOn = (el, ev) => { try { return Object.keys(el).filter(k => k.startsWith('__reactProps')).length } catch { return -1 } }
            const before = {
              mapLoaded: map.loaded(),
              styleLoaded: map.isStyleLoaded(),
              hasSelectionLayer: !!map.getLayer('selection'),
              hasSelectionSource: !!map.getSource('selection'),
              reactProps: listenersOn(canvas, 'mousedown'),
            }
            canvas.dispatchEvent(new MouseEvent('mousedown', { ...opts, clientX: startX, clientY: startY }))
            // a few intermediate moves
            canvas.dispatchEvent(new MouseEvent('mousemove', { ...opts, clientX: startX + 50, clientY: startY + 50 }))
            canvas.dispatchEvent(new MouseEvent('mousemove', { ...opts, clientX: moveX, clientY: moveY }))
            return new Promise((resolve) => setTimeout(() => {
              let layerInfo = 'no selection layer'
              let sourceInfo = 'no selection source'
              try {
                if (map.getLayer('selection')) {
                  const vis = map.getLayoutProperty('selection', 'visibility')
                  const paint = map.getPaintProperty('selection', 'fill-color') + '/' + map.getPaintProperty('selection', 'fill-opacity')
                  layerInfo = 'visibility=' + vis + ' paint=' + paint
                }
                if (map.getLayer('selection-outline')) {
                  layerInfo += ' | outline-visibility=' + map.getLayoutProperty('selection-outline', 'visibility') + ' outline-width=' + map.getPaintProperty('selection-outline', 'line-width')
                }
                const src = map.getSource('selection')
                if (src && src._data && src._data.geometry) {
                  const c = src._data.geometry.coordinates
                  sourceInfo = c && c[0] ? ('poly corners=' + c[0].length + ' first=' + JSON.stringify(c[0][0])) : 'empty geometry'
                } else if (src) {
                  sourceInfo = 'src exists but no _data'
                }
              } catch (e) { layerInfo = 'ERR: ' + e.message }
              resolve(JSON.stringify({ before, dragging: true, layer: layerInfo, source: sourceInfo }))
            }, 500))
          })()`)

          await step('shift-drag-screenshot', `(() => {
            const map = window.__ogsMap
            const canvas = document.querySelector('.maplibregl-canvas')
            const rect = canvas.getBoundingClientRect()
            const opts = { bubbles: true, cancelable: true, view: window, button: 0, shiftKey: true }
            // still dragging from previous step; do one more move then snapshot
            canvas.dispatchEvent(new MouseEvent('mousemove', { ...opts, clientX: rect.left + 400, clientY: rect.top + 400 }))
            return 'drag held'
          })()`)

          // release the drag (so the map can be interacted with again)
          await wc.executeJavaScript(`(() => {
            const canvas = document.querySelector('.maplibregl-canvas')
            const rect = canvas.getBoundingClientRect()
            const opts = { bubbles: true, cancelable: true, view: window, button: 0, shiftKey: true }
            canvas.dispatchEvent(new MouseEvent('mouseup', { ...opts, clientX: rect.left + 400, clientY: rect.top + 400 }))
            window.dispatchEvent(new MouseEvent('mouseup', { ...opts, clientX: rect.left + 400, clientY: rect.top + 400 }))
            return 'released'
          })()`, true)

          await step('after-release', `(() => {
            const map = window.__ogsMap
            let layerInfo = 'no selection layer'
            try {
              if (map.getLayer('selection')) {
                layerInfo = 'visibility=' + map.getLayoutProperty('selection', 'visibility') + ' outline-vis=' + map.getLayoutProperty('selection-outline', 'visibility')
              }
            } catch (e) { layerInfo = 'ERR: ' + e.message }
            const sidebarHasBounds = !!document.querySelector('.terrain-map-container')
            return JSON.stringify({ layer: layerInfo, sidebar: sidebarHasBounds })
          })()`)

          const shot = await wc.capturePage().catch(() => null)
          if (shot && !shot.isEmpty()) {
            const shotPath = path.join(DIST_DIR, '..', 'e2e-shot.png')
            fs.writeFileSync(shotPath, shot.toPNG())
            console.log(`[e2e] screenshot saved: ${shotPath}`)
          } else {
            console.log('[e2e] screenshot unavailable (empty capture)')
          }
        } catch (err) {
          console.error(`[e2e] driver failed: ${err.message}`)
        }
        app.quit()
      })()
    })
    setTimeout(() => app.quit(), 60000) // safety stop
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
  if (!isDev) {
    protocol.handle('app', (request) => {
      try {
        const { pathname } = new URL(request.url)
        const relative = decodeURIComponent(pathname).replace(/^\/+/, '')
        let filePath = path.normalize(path.join(DIST_DIR, relative))
        if (!filePath.startsWith(DIST_DIR)) {
          return new Response('Forbidden', { status: 403 })
        }
        if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
          // Only fall back to the entry document for extension-less paths
          // (SPA routes). Missing assets should 404 so failures are visible.
          if (path.extname(relative) !== '') {
            return new Response('Not found', { status: 404 })
          }
          filePath = path.join(DIST_DIR, 'index.html')
        }
        return net.fetch(pathToFileURL(filePath).toString(), {
          headers: { 'Content-Type': mimeFor(filePath) },
        })
      } catch (err) {
        return new Response(`Bad request: ${err.message}`, { status: 400 })
      }
    })
  }

  createWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})
