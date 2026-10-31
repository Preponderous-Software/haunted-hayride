// Smoke test for the browser build in headless Chromium (Playwright).
//
//   node web/smoke/smoke.cjs <url> <out-dir> [--phone]
//
// Checks: the page starts with no page errors and shows the ride menu (three
// rides) over the attract ride; the canvas draws the night farm (not one flat
// colour); choosing Pumpkin Patch (a click, or a tap on the phone profile)
// starts the ride with the HUD up; a spook peeks out, and pointing at it with
// the real input (the mouse, or a finger on the phone) lights it and catches
// it; then, with the page's autopilot and a faster clock, the ride reaches the
// barn and the score card says "You found N spooks!"; "Pick a ride" goes back
// to the menu; the Sound button turns the sound off. On the phone profile,
// every button is at least 44 CSS pixels and inside the screen, and nothing
// scrolls sideways. Writes PNGs to <out-dir>.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { chromium, devices } = require('playwright');

const [url, outDir] = process.argv.slice(2);
const phone = process.argv.includes('--phone');
if (!url || !outDir) {
  console.error('usage: smoke.cjs <url> <out-dir> [--phone]');
  process.exit(2);
}
fs.mkdirSync(outDir, { recursive: true });

// Minimal PNG decoder (8-bit RGB/RGBA, non-interlaced: what Chromium writes).
function decodePng(buf) {
  let pos = 8, width = 0, height = 0, channels = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      channels = { 2: 3, 6: 4 }[data[9]];
      if (data[8] !== 8 || !channels || data[12] !== 0) throw new Error('unsupported PNG');
    } else if (type === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= channels ? out[y * stride + x - channels] : 0;
      const b = y > 0 ? out[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? out[(y - 1) * stride + x - channels] : 0;
      let p;
      if (f === 0) p = 0;
      else if (f === 1) p = a;
      else if (f === 2) p = b;
      else if (f === 3) p = (a + b) >> 1;
      else {
        const pa = Math.abs(b - c), pb = Math.abs(a - c), pc = Math.abs(a + b - 2 * c);
        p = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      out[y * stride + x] = (v + p) & 255;
    }
  }
  return { width, height, channels, data: out };
}
// Distinct colours (quantised to 4 bits per channel) in a PNG.
function colourCount(png) {
  const img = decodePng(png);
  const seen = new Set();
  for (let i = 0; i < img.data.length; i += img.channels)
    seen.add(((img.data[i] >> 4) << 8) | ((img.data[i + 1] >> 4) << 4) | (img.data[i + 2] >> 4));
  return seen.size;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const base = phone ? devices['iPhone 13'] : { viewport: { width: 1280, height: 800 } };
  // "headless" in the UA keeps arcade from counting a smoke run as a play.
  const context = await browser.newContext({ ...base, userAgent: (base.userAgent || 'Mozilla/5.0') + ' hayride-smoke headless' });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  const fail = async (why) => {
    console.error('FAIL: ' + why);
    await page.screenshot({ path: path.join(outDir, 'failure.png'), timeout: 120000 }).catch(() => {});
    await browser.close();
    process.exit(1);
  };
  const shot = (name) => page.screenshot({ path: path.join(outDir, name), timeout: 180000 });
  await page.goto(url, { waitUntil: 'commit' });
  try {
    await page.waitForFunction(() => window.hayride && window.hayrideStatus, null, { timeout: 120000 });
  } catch (e) {
    await fail('the game never became ready: ' + errors.join(' | '));
  }
  const status = async () => JSON.parse(await page.evaluate(() => window.hayride.status()));
  const until = async (pred, ms = 30000) => {
    const end = Date.now() + ms;
    let st = await status();
    while (!pred(st) && Date.now() < end) {
      await page.waitForTimeout(150);
      st = await status();
    }
    return st;
  };
  const checkErrors = async (when) => { if (errors.length) await fail(`page errors ${when}: ${errors.join(' | ')}`); };

  // On a phone, every visible button is big enough and on screen, and nothing scrolls sideways.
  const checkLayout = async (when) => {
    if (!phone) return;
    const r = await page.evaluate(() => {
      const out = [];
      document.querySelectorAll('button').forEach((b) => {
        const x = b.getBoundingClientRect();
        if (x.width > 0 && x.height > 0 && b.offsetParent) out.push({ id: b.id || b.textContent.trim(), x: x.left, y: x.top, w: x.width, h: x.height });
      });
      return { out, vw: innerWidth, vh: innerHeight, sw: document.documentElement.scrollWidth };
    });
    if (r.sw > r.vw + 1) await fail(`${when}: the page scrolls sideways (${r.sw} > ${r.vw})`);
    for (const b of r.out) {
      if (b.w < 44 || b.h < 44) await fail(`${when}: ${b.id} is only ${b.w}x${b.h}`);
      if (b.x < 0 || b.x + b.w > r.vw + 0.5) await fail(`${when}: ${b.id} is off screen`);
    }
    console.log(`ok: ${r.out.length} buttons, all on screen and finger-sized (${when})`);
  };

  // The menu, over the attract ride.
  let s = await status();
  console.log(`ready: mode ${s.mode}, attract ride ${s.ride}`);
  if (s.mode !== 'title') await fail('the game should open on the menu, not ' + s.mode);
  if (!(await page.locator('#menu').isVisible())) await fail('the ride menu is not shown');
  if ((await page.locator('#rides .ride').count()) !== 3) await fail('the menu should offer three rides');
  await page.waitForTimeout(2500);
  await checkLayout('menu');
  const menuShot = await shot('menu.png');
  const colours = colourCount(menuShot);
  console.log(`menu screenshot: ${colours} distinct colours`);
  if (colours < 24) await fail('the page looks blank (' + colours + ' colours)');
  await checkErrors('on the menu');

  // Pumpkin Patch, chosen the way a player would.
  if (phone) await page.locator('#ride0').tap();
  else await page.locator('#ride0').click();
  s = await until((st) => st.mode === 'riding', 10000);
  if (s.mode !== 'riding' || s.ride !== 0) await fail('choosing Pumpkin Patch did not start it: ' + JSON.stringify(s));
  if (!(await page.locator('#hud').isVisible())) await fail('the HUD is not shown while riding');
  if (await page.locator('#menu').isVisible()) await fail('the menu is still up while riding');
  await checkLayout('riding');
  console.log(`riding: ${s.total} spooks on ${s.expected.toFixed(0)} s of road`);

  // Point at the first spook that peeks out, with the mouse or a finger, until it is caught.
  s = await until((st) => st.out > 0 && st.target[0] >= 0, 90000);
  if (!(s.out > 0)) await fail('no spook ever peeked out: ' + JSON.stringify(s));
  console.log(`a spook is out at ${s.target.map((v) => v.toFixed(2)).join(', ')} after ${s.seconds.toFixed(1)} s`);
  const box = await page.locator('#canvas').boundingBox();
  const end = Date.now() + 60000;
  let litSeen = false;
  while (s.caught === 0 && Date.now() < end) {
    if (s.target[0] >= 0) {
      const x = box.x + s.target[0] * box.width, y = box.y + s.target[1] * box.height;
      if (phone) await page.touchscreen.tap(x, y);
      else await page.mouse.move(x, y);
    }
    await page.waitForTimeout(120);
    s = await status();
    litSeen = litSeen || s.lit > 0;
  }
  if (s.caught < 1) await fail('pointing at a peeking spook never caught it: ' + JSON.stringify(s));
  if (s.auto) await fail('the catch was made by the autopilot, not the pointer');
  console.log(`caught one by ${phone ? 'touch' : 'mouse'} at ${s.seconds.toFixed(1)} s (lit seen: ${litSeen})`);
  await shot('catch.png');
  await page.waitForFunction(() => document.getElementById('foundN').textContent === '1', null, { timeout: 5000 })
    .catch(() => fail('the HUD did not count the spook'));

  // The rest of the ride, on autopilot and a faster clock, to the score card.
  await page.evaluate(() => { window.hayride.auto(1); window.hayride.speed(8); });
  s = await until((st) => st.mode === 'done', 300000);
  if (s.mode !== 'done') await fail('the ride never reached the barn: ' + JSON.stringify(s));
  await page.waitForFunction(() => !document.getElementById('score').classList.contains('hidden'), null, { timeout: 10000 })
    .catch(() => fail('the score card was not shown'));
  const title = await page.locator('#scoreTitle').innerText();
  console.log(`score card: "${title}" (${s.caught} of ${s.total})`);
  if (!new RegExp(`^You found ${s.caught} spooks?!$`).test(title)) await fail('the score card says ' + title);
  if (s.caught < Math.ceil(s.total * 0.6)) await fail(`the autopilot found only ${s.caught} of ${s.total}`);
  await page.waitForTimeout(1600);
  await checkLayout('score card');
  await shot('score.png');
  await checkErrors('after the ride');

  // Back to the menu; the best score is remembered on the ride's card.
  if (phone) await page.locator('#pickBtn').tap();
  else await page.locator('#pickBtn').click();
  s = await until((st) => st.mode === 'title', 10000);
  if (s.mode !== 'title' || !(await page.locator('#menu').isVisible())) await fail('"Pick a ride" did not go back to the menu');
  const meta = await page.locator('#ride0 .meta').innerText();
  if (!/Best: \d+/.test(meta)) await fail('the best score is not on the ride card: ' + meta);

  // Sound off, from its button (shown while riding).
  if (phone) await page.locator('#ride1').tap();
  else await page.locator('#ride1').click();
  await page.waitForFunction(() => !document.getElementById('hud').classList.contains('hidden'), null, { timeout: 10000 }).catch(() => {});
  if (phone) await page.locator('#soundBtn').tap();
  else await page.locator('#soundBtn').click();
  const pressed = await page.locator('#soundBtn').getAttribute('aria-pressed');
  const stored = await page.evaluate(() => localStorage.getItem('hayride-sound'));
  if (pressed !== 'false' || stored !== 'off') await fail(`the Sound button did not turn the sound off (${pressed}, ${stored})`);
  await checkErrors('at the end');
  console.log('PASS');
  await browser.close();
})().catch((e) => { console.error('FAIL: ' + (e && e.stack || e)); process.exit(1); });
