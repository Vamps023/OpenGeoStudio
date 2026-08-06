// Comprehensive CDP interaction tool for Electron app testing
// Usage: node cdp-interact.mjs <command> [args...]
// Commands:
//   snapshot   - Get accessibility tree text
//   click <selector> - Click element by CSS selector
//   type <selector> <text> - Type text into element
//   eval <expr> - Evaluate JavaScript expression
//   screenshot <path> - Take screenshot
//   console - Get console messages
//   buttons - List all buttons with their text

const response = await fetch('http://localhost:9222/json');
const targets = await response.json();
const target = targets.find(t => t.type === 'page');
if (!target) { console.error('No page target found'); process.exit(1); }

const ws = new WebSocket(target.webSocketDebuggerUrl);
let msgId = 1;
const pending = new Map();
const consoleMessages = [];
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
  if (msg.id && pending.has(msg.id)) {
    pending.get(msg.id).resolve(msg);
    pending.delete(msg.id);
  }
  if (msg.method === 'Runtime.consoleAPICalled') {
    consoleMessages.push({
      type: msg.params.type,
      args: msg.params.args.map(a => a.value ?? a.description ?? '').join(' ')
    });
  }
  if (msg.method === 'Runtime.exceptionThrown') {
    exceptions.push({
      text: msg.params.exceptionDetails.text,
      stack: msg.params.exceptionDetails.exception?.description ?? ''
    });
  }
});

await new Promise((resolve) => ws.addEventListener('open', resolve, { once: true }));
await send('Runtime.enable');
await send('Page.enable');
await send('Log.enable');

const cmd = process.argv[2] || 'snapshot';

async function waitFor(ms) { await new Promise(r => setTimeout(r, ms)); }

if (cmd === 'snapshot') {
  const result = await send('Runtime.evaluate', {
    expression: `(() => {
      const walk = (el, depth=0) => {
        if (!el || el.nodeType !== 1) return '';
        let text = '';
        const tag = el.tagName.toLowerCase();
        const role = el.getAttribute('role') || '';
        const aria = el.getAttribute('aria-label') || '';
        const textContent = Array.from(el.childNodes).filter(n => n.nodeType===3).map(n=>n.textContent.trim()).join(' ').substring(0,80);
        const id = el.id ? '#'+el.id : '';
        const cls = el.className && typeof el.className==='string' ? '.'+el.className.split(' ').filter(Boolean).slice(0,2).join('.') : '';
        if (textContent || aria || tag==='button' || tag==='input' || tag==='select' || role) {
          const label = aria || textContent || '';
          text += '  '.repeat(depth) + '<'+tag+id+cls+(role?' role='+role:'')+(label?' ['+label+']':'')+'>'+ (el.disabled?' [disabled]':'') +'\\n';
        }
        for (const child of el.children) text += walk(child, depth+1);
        return text;
      };
      return walk(document.body);
    })()`,
    returnByValue: true
  });
  console.log(result.result?.result?.value ?? 'ERROR');
}

else if (cmd === 'buttons') {
  const result = await send('Runtime.evaluate', {
    expression: `Array.from(document.querySelectorAll('button')).map(b => ({
      text: b.textContent.trim().substring(0,60),
      title: b.title || '',
      disabled: b.disabled,
      classes: b.className.substring(0,80),
      rect: { x: Math.round(b.getBoundingClientRect().x), y: Math.round(b.getBoundingClientRect().y), w: Math.round(b.getBoundingClientRect().width), h: Math.round(b.getBoundingClientRect().height) }
    })).filter(b => b.rect.w > 0)`,
    returnByValue: true
  });
  const buttons = result.result?.result?.value || [];
  for (const b of buttons) {
    console.log(`[${b.rect.x},${b.rect.y} ${b.rect.w}x${b.rect.h}] "${b.text}"${b.disabled?' [disabled]':''} ${b.title?'title="'+b.title+'"':''}`);
  }
  console.log(`Total: ${buttons.length} buttons`);
}

else if (cmd === 'click') {
  const selector = process.argv[3];
  const result = await send('Runtime.evaluate', {
    expression: `(() => {
      const el = document.querySelector('${selector}');
      if (!el) return 'NOT FOUND: ${selector}';
      el.click();
      return 'CLICKED: ${selector}';
    })()`,
    returnByValue: true
  });
  console.log(result.result?.result?.value);
  await waitFor(500);
}

else if (cmd === 'clicktext') {
  const text = process.argv.slice(3).join(' ');
  const result = await send('Runtime.evaluate', {
    expression: `(() => {
      const buttons = Array.from(document.querySelectorAll('button, [role=tab], a'));
      const found = buttons.find(b => b.textContent.trim().includes('${text.replace(/'/g,"\\'")}'));
      if (!found) return 'NOT FOUND: ${text}';
      found.click();
      return 'CLICKED: ' + found.textContent.trim().substring(0,60);
    })()`,
    returnByValue: true
  });
  console.log(result.result?.result?.value);
  await waitFor(500);
}

else if (cmd === 'clickat') {
  const x = parseInt(process.argv[3]);
  const y = parseInt(process.argv[4]);
  await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
  await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
  await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
  console.log(`Clicked at ${x},${y}`);
  await waitFor(500);
}

else if (cmd === 'type') {
  const selector = process.argv[3];
  const text = process.argv.slice(4).join(' ');
  await send('Runtime.evaluate', {
    expression: `(() => {
      const el = document.querySelector('${selector}');
      if (!el) return 'NOT FOUND';
      el.focus();
      el.value = '${text.replace(/'/g,"\\'")}';
      el.dispatchEvent(new Event('input', {bubbles:true}));
      el.dispatchEvent(new Event('change', {bubbles:true}));
      return 'TYPED';
    })()`,
    returnByValue: true
  });
  console.log('Typed into ' + selector);
}

else if (cmd === 'eval') {
  const expr = process.argv.slice(3).join(' ');
  const result = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
  console.log(JSON.stringify(result.result?.result?.value, null, 2));
}

else if (cmd === 'screenshot') {
  const path = process.argv[3] || 'screenshot.png';
  const result = await send('Page.captureScreenshot', { format: 'png' });
  if (result.result?.data) {
    const fs = await import('fs');
    fs.writeFileSync(path, Buffer.from(result.result.data, 'base64'));
    console.log('Screenshot saved to ' + path);
  }
}

else if (cmd === 'console') {
  await waitFor(2000);
  console.log('=== CONSOLE ===');
  for (const m of consoleMessages) console.log(`[${m.type}] ${m.args}`);
  console.log('=== EXCEPTIONS ===');
  for (const e of exceptions) { console.log(`[EXCEPTION] ${e.text}`); if (e.stack) console.log(e.stack); }
  console.log(`Total: ${consoleMessages.length} messages, ${exceptions.length} exceptions`);
}

else {
  console.log('Unknown command: ' + cmd);
}

ws.close();
process.exit(0);
