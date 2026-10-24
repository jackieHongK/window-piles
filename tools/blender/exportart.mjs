// Exports the page's printed packaging art (colour / normal / ORM) for the Blender packaging scene.
import { writeFileSync, mkdirSync } from 'node:fs';
const port = process.argv[2], out = process.argv[3]; mkdirSync(out, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 40; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); break; } catch { await sleep(250); } }
const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let id = 0; const pending = new Map();
ws.addEventListener('message', (e) => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } });
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async (expr) => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
await send('Network.enable'); await send('Network.setCacheDisabled', { cacheDisabled: true }); await send('Page.enable');
await send('Page.navigate', { url: 'http://127.0.0.1:8765/index.html?keep' });
for (let i = 0; i < 300; i++) { if (await ev('!!(window.__unbox && window.__unbox.TIMES.first)')) break; await sleep(100); }
const list = await ev(`(()=>{ const out=[]; for (const [k,m] of Object.entries(window.__unbox.art)) for (const slot of ['map','normalMap','metalnessMap']) if (m[slot] && m[slot].image && m[slot].image.width > 1) out.push(k+'.'+slot); return out; })()`);
for (const key of list) {
  const [k, slot] = key.split('.');
  const url = await ev(`window.__unbox.art['${k}']['${slot}'].image.toDataURL('image/png')`);
  writeFileSync(`${out}/${k}_${slot === 'map' ? 'color' : slot === 'normalMap' ? 'normal' : 'orm'}.png`, Buffer.from(url.split(',')[1], 'base64')); console.log(key);
}
ws.close();
