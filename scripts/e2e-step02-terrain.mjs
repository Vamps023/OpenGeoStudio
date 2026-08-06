// E2E Step 2: Terrain — Navigate to Pune, select ~1km², generate terrain
const response = await fetch('http://localhost:9222/json');
const targets = await response.json();
const target = targets.find(t => t.type === 'page');
if (!target) { console.error('No page target'); process.exit(1); }

const ws = new WebSocket(target.webSocketDebuggerUrl);
let msgId = 1;
const pending = new Map();
const consoleMsgs = [];
const exceptions = [];

function send(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = msgId++;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

ws.addEventListener('message', (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id).resolve(msg); pending.delete(msg.id); }
  if (msg.method === 'Runtime.consoleAPICalled') {
    consoleMsgs.push({ type: msg.params.type, args: msg.params.args.map(a => a.value ?? a.description ?? '').join(' ') });
  }
  if (msg.method === 'Runtime.exceptionThrown') {
    exceptions.push({ text: msg.params.exceptionDetails.text });
  }
});

await new Promise(r => ws.addEventListener('open', r, { once: true }));
await send('Runtime.enable');
await send('Page.enable');
await send('Page.setInterceptAllDialogs', { intercept: true });
ws.addEventListener('message', (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.method === 'Page.javascriptDialogOpening') {
    send('Page.handleJavaScriptDialog', { accept: true });
  }
});

async function evalJS(expr) {
  const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  if (r.result?.exceptionDetails) { console.log('  JS ERROR:', r.result.exceptionDetails.text); return null; }
  return r.result?.result?.value;
}
function wait(ms) { return new Promise(r => setTimeout(r, ms)); }
function log(msg) { console.log(msg); }

// Helper: find map instance
async function getMap() {
  return await evalJS(`(() => {
    const mapEl = document.querySelector('.maplibregl-map');
    if (!mapEl) return null;
    const ck = Object.keys(mapEl).find(k => k.startsWith('__reactFiber'));
    if (!ck) return null;
    const fiber = mapEl[ck];
    let f = fiber;
    let depth = 0;
    while (f && depth < 30) {
      const props = f.memoizedProps;
      if (props && props.mapRef && props.mapRef.current) return 'found';
      let s = f.memoizedState;
      while (s) {
        if (s.memoizedState && typeof s.memoizedState === 'object' && s.memoizedState.current && typeof s.memoizedState.current.getCenter === 'function') return 'found';
        s = s.next;
      }
      f = f.return;
      depth++;
    }
    return null;
  })()`);
}

// ═══════════════════════════════════════════════════════════════
log('\n══════════════════════════════════════════════════════════════');
log('STEP 2: TERRAIN — Pune, India (~1km²)');
log('══════════════════════════════════════════════════════════════');

// 2a. Navigate to Pune using the map instance
log('\n--- 2a: Navigate map to Pune, India ---');
const navResult = await evalJS(`(() => {
  const mapEl = document.querySelector('.maplibregl-map');
  if (!mapEl) return 'no map element';
  const ck = Object.keys(mapEl).find(k => k.startsWith('__reactFiber'));
  if (!ck) return 'no fiber';
  const fiber = mapEl[ck];
  let f = fiber;
  let mapInstance = null;
  let depth = 0;
  while (f && depth < 30 && !mapInstance) {
    const props = f.memoizedProps;
    if (props && props.mapRef && props.mapRef.current) mapInstance = props.mapRef.current;
    if (!mapInstance) {
      let s = f.memoizedState;
      while (s && !mapInstance) {
        if (s.memoizedState && typeof s.memoizedState === 'object' && s.memoizedState.current && typeof s.memoizedState.current.getCenter === 'function') mapInstance = s.memoizedState.current;
        s = s.next;
      }
    }
    f = f.return;
    depth++;
  }
  if (!mapInstance) return 'map not found';
  
  // Fly to Pune, India [lng, lat]
  mapInstance.flyTo({
    center: [73.8567, 18.5204],
    zoom: 15,
    duration: 3000
  });
  return 'flying to Pune [73.8567, 18.5204] at zoom 15';
})()`);
log(`  Result: ${navResult}`);
await wait(4000);

// Verify location
const locationCheck = await evalJS(`(() => {
  const mapEl = document.querySelector('.maplibregl-map');
  const ck = Object.keys(mapEl).find(k => k.startsWith('__reactFiber'));
  const fiber = mapEl[ck];
  let f = fiber;
  let mapInstance = null;
  let depth = 0;
  while (f && depth < 30 && !mapInstance) {
    const props = f.memoizedProps;
    if (props && props.mapRef && props.mapRef.current) mapInstance = props.mapRef.current;
    if (!mapInstance) { let s = f.memoizedState; while (s && !mapInstance) { if (s.memoizedState && typeof s.memoizedState === 'object' && s.memoizedState.current && typeof s.memoizedState.current.getCenter === 'function') mapInstance = s.memoizedState.current; s = s.next; } }
    f = f.return;
    depth++;
  }
  if (!mapInstance) return 'map not found';
  const center = mapInstance.getCenter();
  const zoom = mapInstance.getZoom();
  return JSON.stringify({lng: center.lng.toFixed(4), lat: center.lat.toFixed(4), zoom: zoom.toFixed(1)});
})()`);
log(`  Current location: ${locationCheck}`);

// 2b. Zoom in for ~1km² selection
log('\n--- 2b: Zoom in for 1km² selection ---');
await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const z = btns.find(b => b.title && b.title.includes('Zoom In')); if (z) { for (let i = 0; i < 3; i++) z.click(); return 'zoomed 3x'; } return 'no zoom'; })()`);
await wait(2000);

// 2c. Clear any existing selection
log('\n--- 2c: Clear existing selection ---');
await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const c = btns.find(b => b.textContent.trim() === 'Clear'); if (c) { c.click(); return 'cleared'; } return 'no clear'; })()`);
await wait(1000);

// 2d. Make a ~1km² selection using Shift+drag
log('\n--- 2d: Make ~1km² selection ---');
const selectionResult = await evalJS(`(() => {
  const canvas = document.querySelector('canvas.maplibregl-canvas');
  if (!canvas) return 'no canvas';
  const rect = canvas.getBoundingClientRect();
  const cx = rect.x + rect.width / 2, cy = rect.y + rect.height / 2;
  // At zoom 15-18, a 60px drag covers roughly 1km
  const dragSize = 60;
  canvas.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, clientX: cx-dragSize, clientY: cy-dragSize, offsetX: cx-dragSize-rect.x, offsetY: cy-dragSize-rect.y, shiftKey: true, button: 0, buttons: 1 }));
  for (let i = 1; i <= 10; i++) { const x = cx-dragSize+2*dragSize*i/10, y = cy-dragSize+2*dragSize*i/10; canvas.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, cancelable: true, clientX: x, clientY: y, offsetX: x-rect.x, offsetY: y-rect.y, shiftKey: true, button: 0, buttons: 1 })); }
  canvas.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, clientX: cx+dragSize, clientY: cy+dragSize, offsetX: cx+dragSize-rect.x, offsetY: cy+dragSize-rect.y, shiftKey: true, button: 0, buttons: 0 }));
  window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, clientX: cx+dragSize, clientY: cy+dragSize, shiftKey: true, button: 0, buttons: 0 }));
  return 'selection made';
})()`);
log(`  Result: ${selectionResult}`);
await wait(2000);

// 2e. Check selection bounds
log('\n--- 2e: Check selection bounds ---');
const selectionInfo = await evalJS(`(() => {
  const text = document.body.innerText;
  const grid = text.match(/(\\d+)×(\\d+) = (\\d+)/)?.[0];
  const sel = text.match(/(\\d+) selected/)?.[0];
  // Extract bounds from the coordinate display
  const n = text.match(/N:\\n(-?[\\d.]+°)/)?.[1];
  const s = text.match(/S:\\n(-?[\\d.]+°)/)?.[1];
  const e = text.match(/E:\\n(-?[\\d.]+°)/)?.[1];
  const w = text.match(/W:\\n(-?[\\d.]+°)/)?.[1];
  const exp = text.match(/Export (\\d+) Tiles/)?.[0];
  return JSON.stringify({grid, sel, N:n, S:s, E:e, W:w, exp});
})()`);
log(`  Selection: ${selectionInfo}`);

// Check if selection is in Pune area
const sel = JSON.parse(selectionInfo || '{}');
const isPune = sel.N && sel.S && parseFloat(sel.N) > 18 && parseFloat(sel.N) < 19;
log(`  Is Pune area: ${isPune}`);

// 2f. Walk through the workflow wizard via UI
log('\n--- 2f: Walk through workflow wizard ---');
for (let step = 2; step <= 5; step++) {
  const r = await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const n = btns.find(b => b.textContent.trim() === 'Next'); if (n && !n.disabled) { n.click(); return 'next'; } return 'disabled'; })()`);
  log(`  Step ${step-1}→${step}: ${r}`);
  await wait(500);
}

// 2g. Generate terrain via UI
log('\n--- 2g: Generate terrain ---');
const genClick = await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const g = btns.find(b => b.textContent.trim() === 'Generate Terrain'); if (g) { g.click(); return 'clicked'; } return 'not found'; })()`);
log(`  Result: ${genClick}`);
log('  Waiting for export (45s)...');
await wait(45000);

// 2h. Check export result
log('\n--- 2h: Check export result ---');
const exportResult = await evalJS(`(() => {
  const text = document.body.innerText;
  const complete = text.includes('Export complete') || text.includes('Export Complete');
  const error = text.match(/Export Failed[\\s\\S]{0,200}/)?.[0];
  return JSON.stringify({complete, error: error || null});
})()`);
log(`  Export: ${exportResult}`);

// 2i. Check generated files via manifest
log('\n--- 2i: Verify generated files ---');
const manifestInfo = await evalJS(`(async () => {
  const a = await window.electronAPI.ipc.invoke('project:getActive');
  if (!a) return 'no active project';
  const exportPath = a.basePath + '\\\\Exports';
  try {
    const manifest = await window.electronAPI.fs.readManifest(exportPath + '\\\\tile_0_0');
    if (manifest && !manifest.error) {
      return JSON.stringify({
        terrainName: manifest.terrainName,
        bounds: manifest.bounds,
        crs: manifest.crs,
        tileCount: manifest.tiles?.length,
        demSource: manifest.sources?.dem?.name,
        imagerySource: manifest.sources?.imagery?.name,
        exportPreset: manifest.exportPreset,
        heightmapResolution: manifest.tileGrid?.heightmapResolution,
        albedoResolution: manifest.tileGrid?.albedoResolution,
        elevation: manifest.tiles?.[0]?.elevation
      });
    }
    return 'manifest error: ' + manifest?.error;
  } catch(e) { return 'error: ' + e.message; }
})()`);
log(`  Manifest: ${manifestInfo}`);

// 2j. Check console errors
log('\n--- 2j: Console check ---');
const errors = consoleMsgs.filter(m => m.type === 'error');
log(`  Console errors: ${errors.length}`);
if (errors.length) errors.forEach(e => log(`    ERROR: ${e.args}`));
log(`  Exceptions: ${exceptions.length}`);
if (exceptions.length) exceptions.forEach(e => log(`    EXC: ${e.text}`));

const expData = JSON.parse(exportResult || '{}');
const step2Pass = expData.complete === true && errors.length === 0 && exceptions.length === 0;
log(`\nSTEP 2 RESULT: ${step2Pass ? 'PASS' : 'FAIL'}`);

ws.close();
process.exit(step2Pass ? 0 : 1);
