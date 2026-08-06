// E2E Step 1: Project Creation — QA_EndToEnd_Project
// Note: Project creation via UI requires a native folder dialog that cannot
// be automated via CDP. Per instructions, we use IPC when UI has no possible way.
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
    exceptions.push({ text: msg.params.exceptionDetails.text, stack: msg.params.exceptionDetails.exception?.description ?? '' });
  }
});

await new Promise(r => ws.addEventListener('open', r, { once: true }));
await send('Runtime.enable');
await send('Page.enable');
await send('Page.setInterceptAllDialogs', { intercept: true });
ws.addEventListener('message', (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.method === 'Page.javascriptDialogOpening') {
    console.log('  [Dialog]', msg.params.message);
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

async function getCoreState() {
  return await evalJS(`(() => {
    const s = window.__CORE_STORE__?.getState();
    if (!s) return null;
    const proj = s.activeProject;
    return JSON.stringify({ name: proj?.name ?? null, basePath: proj?.basePath ?? null, filePath: proj?.filePath ?? null, dirty: proj?.dirty ?? null, workspaceId: proj?.workspaceId ?? null });
  })()`);
}

// ═══════════════════════════════════════════════════════════════
log('\n══════════════════════════════════════════════════════════════');
log('STEP 1: PROJECT CREATION — QA_EndToEnd_Project');
log('══════════════════════════════════════════════════════════════');

// 1a. Create project via IPC (native dialog can't be automated)
log('\n--- 1a: Create project via IPC ---');
const createResult = await evalJS(`(async () => {
  const r = await window.electronAPI.ipc.invoke('project:createWithFolder', 'QA_EndToEnd_Project', 'terrain', 'D:\\\\POC\\\\GeoTerrain\\\\test-projects');
  return JSON.stringify(r);
})()`);
log(`  Created: ${createResult}`);
await wait(1000);

// 1b. Load into store via UI
log('\n--- 1b: Activate project in store ---');
const activateResult = await evalJS(`(async () => {
  await window.__CORE_STORE__.getState().loadActiveProject();
  const proj = window.__CORE_STORE__.getState().activeProject;
  return JSON.stringify({ name: proj?.name, dirty: proj?.dirty, workspaceId: proj?.workspaceId });
})()`);
log(`  Activated: ${activateResult}`);
await wait(1000);

// 1c. Verify folder structure on disk
log('\n--- 1c: Verify folder structure ---');
const projectState = await getCoreState();
const proj = JSON.parse(projectState || '{}');
log(`  Name: ${proj.name}`);
log(`  BasePath: ${proj.basePath}`);
log(`  FilePath: ${proj.filePath}`);

// 1d. Verify .ogproj file
log('\n--- 1d: Verify .ogproj file ---');
const ogprojInfo = await evalJS(`(async () => {
  const a = await window.electronAPI.ipc.invoke('project:getActive');
  return JSON.stringify({
    name: a.name, id: a.id, workspaceId: a.workspaceId, dirty: a.dirty,
    hasModuleState: !!a.moduleState, filePath: a.filePath, createdAt: a.createdAt
  });
})()`);
log(`  .ogproj: ${ogprojInfo}`);

// 1e. Test autosave — zoom + select via UI
log('\n--- 1e: Test autosave (zoom + select via UI) ---');
// Zoom in using UI button
await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const z = btns.find(b => b.title && b.title.includes('Zoom In')); if (z) { for (let i = 0; i < 8; i++) z.click(); return 'zoomed 8x'; } return 'no zoom'; })()`);
await wait(2000);

// Make selection via UI (Shift+drag on canvas)
await evalJS(`(() => {
  const canvas = document.querySelector('canvas.maplibregl-canvas');
  if (!canvas) return 'no canvas';
  const rect = canvas.getBoundingClientRect();
  const cx = rect.x + rect.width / 2, cy = rect.y + rect.height / 2;
  canvas.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, cancelable: true, clientX: cx-25, clientY: cy-25, offsetX: cx-25-rect.x, offsetY: cy-25-rect.y, shiftKey: true, button: 0, buttons: 1 }));
  for (let i = 1; i <= 10; i++) { const x = cx-25+50*i/10, y = cy-25+50*i/10; canvas.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, cancelable: true, clientX: x, clientY: y, offsetX: x-rect.x, offsetY: y-rect.y, shiftKey: true, button: 0, buttons: 1 })); }
  canvas.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, clientX: cx+25, clientY: cy+25, offsetX: cx+25-rect.x, offsetY: cy+25-rect.y, shiftKey: true, button: 0, buttons: 0 }));
  window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, cancelable: true, clientX: cx+25, clientY: cy+25, shiftKey: true, button: 0, buttons: 0 }));
  return 'selection made';
})()`);
await wait(1000);

const dirtyAfterSelect = await getCoreState();
log(`  After selection: ${dirtyAfterSelect}`);

log('  Waiting for autosave (8s)...');
await wait(8000);

const afterAutosave = await evalJS(`(async () => {
  const a = await window.electronAPI.ipc.invoke('project:getActive');
  if (!a) return 'no active';
  const ts = a.moduleState?.terrain;
  return JSON.stringify({ dirty: a.dirty, hasTerrainState: !!ts, hasSelectedBounds: !!ts?.selectedBounds, modifiedAt: a.modifiedAt });
})()`);
log(`  After autosave: ${afterAutosave}`);

// 1f. Verify recent projects
log('\n--- 1f: Verify recent projects ---');
const recentCheck = await evalJS(`(async () => {
  const r = await window.electronAPI.ipc.invoke('project:getRecent');
  const found = r.find(p => p.name === 'QA_EndToEnd_Project');
  return JSON.stringify({ total: r.length, found: !!found, name: found?.name, path: found?.filePath });
})()`);
log(`  Recent: ${recentCheck}`);

// 1g. Save project via UI
log('\n--- 1g: Save project via UI ---');
const saveResult = await evalJS(`(() => {
  const btns = Array.from(document.querySelectorAll('button'));
  const save = btns.find(b => b.title && b.title.includes('Save Project'));
  if (save && !save.disabled) { save.click(); return 'clicked Save'; }
  return 'Save: ' + (save ? 'disabled' : 'not found');
})()`);
log(`  Result: ${saveResult}`);
await wait(2000);

const afterSave = await getCoreState();
log(`  After save: ${afterSave}`);

// 1h. Close project via UI (command palette)
log('\n--- 1h: Close project via UI ---');
await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const cmd = btns.find(b => b.title && b.title.includes('Command Palette')); if (cmd) { cmd.click(); return 'opened'; } return 'no palette'; })()`);
await wait(500);
await evalJS(`(() => { const inputs = document.querySelectorAll('input[type=text]'); const cmdInput = Array.from(inputs).find(i => i.placeholder.includes('command')); if (cmdInput) { cmdInput.focus(); cmdInput.value = 'close'; cmdInput.dispatchEvent(new Event('input', {bubbles:true})); return 'typed'; } return 'no input'; })()`);
await wait(500);
await evalJS(`(() => { const items = document.querySelectorAll('[role=option], [role=menuitem], button, [data-command]'); const close = Array.from(items).find(el => el.textContent.includes('Close Project')); if (close) { close.click(); return 'clicked'; } return 'not found'; })()`);
await wait(2000);

const afterClose = await getCoreState();
log(`  After close: ${afterClose}`);

// 1i. Reopen from Recent Projects via UI
log('\n--- 1i: Reopen project via UI ---');
await evalJS(`(() => { const btns = Array.from(document.querySelectorAll('button')); const home = btns.find(b => b.title && b.title.includes('Start screen')); if (home) { home.click(); return 'Home'; } return 'no Home'; })()`);
await wait(1500);

const reopenResult = await evalJS(`(() => {
  const links = document.querySelectorAll('[role=button], button, a, [class*="cursor-pointer"]');
  const proj = Array.from(links).find(el => el.textContent.includes('QA_EndToEnd_Project'));
  if (proj) { proj.click(); return 'clicked'; }
  return 'not found';
})()`);
log(`  Reopen: ${reopenResult}`);
await wait(3000);

const afterReopen = await getCoreState();
log(`  Project restored: ${afterReopen}`);

// Summary
log('\n--- Step 1 Summary ---');
const errors = consoleMsgs.filter(m => m.type === 'error');
log(`Console errors: ${errors.length}`);
if (errors.length) errors.forEach(e => log(`  ERROR: ${e.args}`));
log(`Exceptions: ${exceptions.length}`);
if (exceptions.length) exceptions.forEach(e => log(`  EXC: ${e.text}`));

const finalProj = JSON.parse(afterReopen || '{}');
const step1Pass = finalProj.name === 'QA_EndToEnd_Project' && errors.length === 0 && exceptions.length === 0;
log(`\nSTEP 1 RESULT: ${step1Pass ? 'PASS' : 'FAIL'}`);

ws.close();
process.exit(step1Pass ? 0 : 1);
