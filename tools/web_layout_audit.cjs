// SPDX-License-Identifier: GPL-3.0-only
// Layout audit of the web pages (docs/sim, docs/device): at 8 widths and in every tab, visible text and controls whose
// boxes overlap, text cut by its box, a page wider than the screen. Serve docs/ first:
//   (cd docs && python3 -m http.server 8765) & node tools/web_layout_audit.cjs   -> {} when clean
let chromium; try { ({ chromium } = require('playwright')); } catch (e) { ({ chromium } = require('/opt/node-tools/node_modules/playwright')); }
// overlap / overflow audit: leaf elements whose boxes intersect (not ancestor/descendant), text cut by its box, page wider than the viewport
const AUDIT = () => {
  const vis = (e) => { const s = getComputedStyle(e); if (s.visibility === 'hidden' || s.display === 'none' || +s.opacity === 0) return false; const r = e.getBoundingClientRect(); return r.width > 1 && r.height > 1; };
  const leafs = [...document.querySelectorAll('body *')].filter((e) => {
    if (!vis(e) || e.closest('[hidden]') || e.id === 'toast' || e.closest('#power')) return false;
    if (['INPUT', 'SELECT', 'BUTTON', 'CANVAS', 'IMG', 'PROGRESS', 'CODE', 'KBD'].includes(e.tagName)) return true;
    return [...e.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim());
  });
  const name = (e) => e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + (e.className && typeof e.className === 'string' ? '.' + e.className.trim().split(/\s+/).join('.') : '') + ' "' + (e.innerText || e.value || '').trim().slice(0, 24) + '"';
  // what is visible of a box: cut by every ancestor that clips (scroll lists, ellipsis cells)
  const clip = (e, r) => {
    let L = r.left, T = r.top, R = r.right, B = r.bottom;
    for (let a = e.parentElement; a; a = a.parentElement) {
      const s = getComputedStyle(a);
      if (s.overflowX !== 'visible' || s.overflowY !== 'visible') { const c = a.getBoundingClientRect(); L = Math.max(L, c.left); T = Math.max(T, c.top); R = Math.min(R, c.right); B = Math.min(B, c.bottom); }
    }
    return R - L > 1 && B - T > 1 ? { left: L, top: T, right: R, bottom: B } : null;
  };
  const rects = (e) => { // per line of text (a wrapped inline is several boxes, not one big one)
    const own = getComputedStyle(e).overflowX !== 'visible' ? [e.getBoundingClientRect()] : null;
    let list;
    if (['INPUT', 'SELECT', 'BUTTON', 'CANVAS', 'IMG', 'PROGRESS'].includes(e.tagName)) list = [e.getBoundingClientRect()];
    else { const rg = document.createRange(); rg.selectNodeContents(e); list = [...rg.getClientRects()]; if (!list.length) list = [e.getBoundingClientRect()]; }
    const self = clip(e, e.getBoundingClientRect());
    return list.map((r) => { const c = clip(e, r); if (!c || !self) return null; if (own) { const o = own[0]; return { left: Math.max(c.left, o.left), top: Math.max(c.top, o.top), right: Math.min(c.right, o.right), bottom: Math.min(c.bottom, o.bottom) }; } return c; })
      .filter((r) => r && r.right - r.left > 1 && r.bottom - r.top > 1);
  };
  const boxes = [];
  for (const e of leafs) for (const r of rects(e)) boxes.push({ e, r });
  const out = { overlaps: [], cut: [], wide: document.documentElement.scrollWidth > innerWidth + 1 ? document.documentElement.scrollWidth : 0 };
  for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
    const a = boxes[i], b = boxes[j];
    if (a.e.contains(b.e) || b.e.contains(a.e)) continue;
    const x = Math.min(a.r.right, b.r.right) - Math.max(a.r.left, b.r.left), y = Math.min(a.r.bottom, b.r.bottom) - Math.max(a.r.top, b.r.top);
    if (x > 2 && y > 2 && a.e !== b.e) out.overlaps.push(`${name(a.e)}  x  ${name(b.e)}  (${x.toFixed(0)}x${y.toFixed(0)})`);
  }
  for (const e of leafs) {
    const s = getComputedStyle(e);
    if (['INPUT', 'SELECT', 'CANVAS', 'IMG', 'PROGRESS'].includes(e.tagName)) continue;
    if (e.scrollWidth > e.clientWidth + 1 && e.clientWidth > 0) out.cut.push(`${name(e)} ${e.scrollWidth}>${e.clientWidth}${s.textOverflow === 'ellipsis' ? ' (ellipsis)' : ''}`);
  }
  return out;
};
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1280, height: 900 } });
  p.on('dialog', (d) => d.accept());
  await p.route(/fonts\.(googleapis|gstatic)/, (r) => r.abort());
  const report = {};
  const W = [1440, 1280, 1024, 900, 768, 600, 390, 320];
  const run = async (key) => { for (const w of W) { await p.setViewportSize({ width: w, height: 900 }); await p.waitForTimeout(250); const r = await p.evaluate(AUDIT); if (r.overlaps.length || r.cut.length || r.wide) report[`${key} @${w}`] = r; } };
  await p.goto('http://localhost:8765/manual/', { waitUntil: 'networkidle' });
  await p.evaluate(async () => { document.documentElement.style.scrollBehavior = 'auto'; for (let y = 0; y < document.body.scrollHeight; y += 600) { scrollTo(0, y); await new Promise((r) => setTimeout(r, 30)); } scrollTo(0, 0); });
  await run('manual');
  await p.goto('http://localhost:8765/sim/');
  await run('sim (power overlay)');
  await p.click('#on'); await p.waitForTimeout(500);
  await run('sim');
  await p.goto('http://localhost:8765/device/');
  await run('device install (offline)');
  await p.click('#virtual'); await p.waitForSelector('#tracks .trk', { timeout: 15000 });
  await p.waitForFunction(() => document.querySelectorAll('#ggroups .group').length > 3, null, { timeout: 15000 });
  for (const tab of ['install', 'tracks', 'presets', 'samples', 'fm6', 'projects', 'backup']) {
    await p.setViewportSize({ width: 1280, height: 900 });
    await p.click(`#tabs button[data-go=${tab}]`); await p.waitForTimeout(1500);
    await run('device ' + tab);
  }
  // a synth track and a drum track detail
  await p.click('#tabs button[data-go=tracks]'); await p.click('#tracks .trk >> nth=0'); await p.waitForTimeout(1200);
  await run('device tracks (drum selected)');
  console.log(JSON.stringify(report, null, 1));
  await b.close();
})();
