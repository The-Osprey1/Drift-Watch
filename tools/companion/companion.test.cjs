// Execute the shipped page, including startup/listeners, with a small DOM and real Web
// Streams. No extracted/reimplemented production functions or browser dependencies.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { performance } = require('node:perf_hooks');

const html = fs.readFileSync(path.join(__dirname, '../../companion/index.html'), 'utf8');
const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const settle = () => new Promise(resolve => setImmediate(resolve));

function page() {
  const ids = new Map(), elements = [], frames = [];
  const canvas = new Proxy({}, { get: (target, key) => key in target ? target[key] :
    key === 'measureText' ? text => ({ width: String(text).length * 6 }) :
    /Gradient$/.test(key) ? () => ({ addColorStop() {} }) : () => {} });
  class Element {
    constructor(tag = 'div', attrs = {}) {
      this.tagName = tag; this.attrs = attrs; this.listeners = {}; this.children = [];
      this.style = { setProperty() {} }; this.dataset = {}; this.value = '';
      this.disabled = 'disabled' in attrs; this.checked = 'checked' in attrs;
      this.textContent = ''; this.width = 480; this.height = 480;
      const classes = new Set((attrs.class || '').split(/\s+/));
      this.classList = { add: x => classes.add(x), remove: x => classes.delete(x),
        toggle(x, on) { if (on === undefined) on = !classes.has(x); on ? classes.add(x) : classes.delete(x); }, contains: x => classes.has(x) };
      elements.push(this); if (attrs.id) this.id = attrs.id;
    }
    set id(value) { this._id = value; ids.set(value, this); }
    get id() { return this._id; }
    set innerHTML(value) { this._html = value; this.children = []; }
    get innerHTML() { return this._html || ''; }
    get firstChild() { return this.children[0]; }
    appendChild(child) { this.children.push(child); return child; }
    removeChild(child) { this.children.splice(this.children.indexOf(child), 1); }
    addEventListener(name, callback) { (this.listeners[name] ||= []).push(callback); }
    async dispatch(name, event = {}) { for (const fn of this.listeners[name] || []) await fn({ target: this, preventDefault() {}, ...event }); }
    click() { if (!this.disabled) return this.dispatch('click'); }
    getAttribute(key) { return this.attrs[key] ?? null; }
    setAttribute(key, value) { this.attrs[key] = String(value); }
    getContext() { return canvas; }
    setPointerCapture() {}
    getBoundingClientRect() { return { left: 0, top: 0, width: 240, height: 240 }; }
  }
  for (const match of html.matchAll(/<(\w+)([^<>]*)>/g)) {
    const attrs = {};
    for (const a of match[2].matchAll(/([\w-]+)(?:="([^"]*)")?/g)) attrs[a[1]] = a[2] ?? '';
    if (attrs.id || Object.keys(attrs).some(k => k.startsWith('data-'))) {
      const el = new Element(match[1], attrs);
      el.textContent = html.slice(match.index + match[0].length).match(/^[^<]*/)[0];
    }
  }
  const select = selector => elements.filter(el => {
    const m = selector.match(/^(\w+)?\[([^=\]]+)(?:="([^"]*)")?\]$/);
    return m && (!m[1] || m[1] === el.tagName) && m[2] in el.attrs && (m[3] === undefined || m[3] === el.attrs[m[2]]);
  });
  const root = new Element();
  const document = { getElementById: id => ids.get(id) || null, createElement: tag => new Element(tag),
    documentElement: root, querySelectorAll: select, querySelector: s => select(s)[0] || null };
  const serialEvents = {};
  const navigator = { bluetooth: {}, serial: { addEventListener: (name, fn) => { serialEvents[name] = fn; } } };
  const context = vm.createContext({ document, navigator, localStorage: { getItem() { return null; }, setItem() {} },
    performance, Date, Intl, TextEncoder, TextDecoder, TextDecoderStream, Uint8Array, DataView,
    setTimeout: (fn, ms) => setTimeout(fn, Math.min(ms, 80)), clearTimeout, console,
    requestAnimationFrame: fn => frames.push(fn) });
  vm.runInContext(source, context, { filename: 'companion/index.html' });
  return { context, ids, frames, navigator, serialEvents, run: expression => vm.runInContext(expression, context),
    text: id => ids.get(id).textContent, tick() { frames.shift()(); } };
}

function ble(p, options = {}) {
  const state = { bright: 2, motion: 1, format: 24, night: 0, face: 0, faces: 4095, battery: 86 };
  const writes = [], listeners = {}, delayed = [];
  let connected = false, fragment = '';
  const statusText = reply => `reply=${reply};fw=0.15.3;time=1800000000;bat=${state.battery};face=${state.face};faces=${state.faces};bright=${state.bright};motion=${state.motion};format=${state.format};night=${state.night}`;
  const emit = text => { const bytes = new TextEncoder().encode(text); for (const fn of listeners.status || []) fn({ target: { value: new DataView(bytes.buffer) } }); };
  const status = { addEventListener: (_name, fn) => (listeners.status ||= []).push(fn),
    startNotifications: async () => {}, readValue: async () => {
      if (options.initialReadFails) throw new Error('Initial read unavailable in wrapper');
      return new TextEncoder().encode(statusText('ok'));
    } };
  const command = { async writeValueWithResponse(bytes) {
    let line;
    if (bytes[0] === 1 || bytes[0] === 2) { fragment += new TextDecoder().decode(bytes.subarray(1)); if (bytes[0] === 1) return; line = fragment; fragment = ''; }
    else line = new TextDecoder().decode(bytes);
    writes.push(line);
    const respond = () => {
      let reply = options.refuse && line !== '?' ? options.refuse : 'ok';
      if (reply === 'ok' && !options.ignoreSettings) {
        const [key, value] = line.split('=');
        const name = { B: 'bright', M: 'motion', FO: 'format', NM: 'night', F: 'face', H: 'faces' }[key];
        if (name) state[name] = Number(value);
      }
      emit(line === '?' && options.queryStatus !== undefined ? options.queryStatus : statusText(reply));
    };
    if (options.delay && line !== '?') delayed.push(respond); else queueMicrotask(respond);
  } };
  const device = { name: 'Drift-Test', addEventListener: (name, fn) => (listeners[name] ||= []).push(fn),
    gatt: { get connected() { return connected; }, async connect() { connected = true; return this; },
      async getPrimaryService() { return { getCharacteristic: async id => id.includes('0002') ? command : status }; },
      disconnect() { connected = false; for (const fn of listeners.gattserverdisconnected || []) fn(); } } };
  p.navigator.bluetooth.requestDevice = () => Promise.resolve(device);
  return { state, writes, device, emit, delayed, get connected() { return connected; } };
}

function serial(p) {
  const state = { bright: 2, motion: 1, format: 24, night: 0, face: 0, faces: 4095, battery: 86 };
  let input, opened = false, closed = 0;
  const writes = [];
  const emit = text => input.enqueue(new TextEncoder().encode(text));
  const info = () => `DRIFT:VER=0.15.3,TIME=1800000000,BAT=${state.battery},FACES=0x${state.faces.toString(16)},FACE=${state.face},BRIGHT=${state.bright},MOTION=${state.motion},FORMAT=${state.format},NIGHT=${state.night}\r\n`;
  const port = { readable: new ReadableStream({ start(controller) { input = controller; } }),
    writable: new WritableStream({ write(bytes) {
      const line = new TextDecoder().decode(bytes).trim(); if (!line) return;
      writes.push(line);
      let reply = '';
      if (line === 'INFO?') reply = info();
      else {
        const [key, raw] = line.split(':'); const value = Number(raw);
        const name = { BRIGHT: 'bright', MOTION: 'motion', FORMAT: 'format', NIGHT: 'night', FACE: 'face', FACES: 'faces' }[key];
        if (name) state[name] = value;
        reply = key === 'TIMER' ? `Timer ${value ? 'started for ' + value + ' s' : 'reset'}\r\n` :
          key === 'BRIGHT' ? `Brightness set to ${value}\r\n` : key === 'FORMAT' ? `Format set to ${value}h\r\n` :
          key === 'WIFI' ? 'Configuring Wi-Fi for SSID: Home\r\n' : '';
      }
      if (reply) { emit(reply.slice(0, 7)); emit(reply.slice(7)); }
    } }), async open() { opened = true; }, async setSignals() {}, async close() { closed++; opened = false; } };
  p.navigator.serial.requestPort = () => Promise.resolve(port);
  return { port, state, writes, emit, get opened() { return opened; }, get closed() { return closed; } };
}

test('the real page boots without fabricated hardware diagnostics', () => {
  const p = page(); p.tick();
  assert.match(p.text('stageFpsLabel'), /Preview/);
  assert.match(p.text('chartBatVal'), /awaiting status/);
  assert.match(p.text('chartTempVal'), /not reported/);
  assert.ok(p.ids.get('toggleTorchBtn').disabled);
  assert.ok(p.ids.get('startTimerBtn').disabled);
});

test('BLE settings use known aliases, acknowledgements and fresh readback', async () => {
  const p = page(), h = ble(p);
  assert.equal(await p.context.connect(), true);
  await p.ids.get('fmt12Btn').click();
  await p.context.applySetting('night', 2);
  await p.context.setHardwareBrightness(3);
  assert.deepEqual(h.writes, ['?', 'FO=12', '?', 'NM=2', '?', 'B=3', '?']);
  assert.equal(p.run('watchState.format'), 12);
  assert.match(p.text('toolsMsg'), /confirmed/);
  assert.ok(p.ids.get('startTimerBtn').disabled);
  await p.context.disconnectTransport();
});

test('failed or missing BLE acknowledgements never optimistically change settings', async () => {
  for (const options of [{ refuse: 'out of range' }, { ignoreSettings: true }, { delay: true }]) {
    const p = page(); ble(p, options); await p.context.connect();
    await p.context.setHardwareBrightness(3);
    assert.equal(p.run('hwBrightness'), 2);
    assert.match(p.text('toolsMsg'), /refused|read back|acknowledge/);
    assert.equal(p.ids.get('toolsMsg').className, 'msg err');
    await p.context.disconnectTransport();
  }
});

test('physical telemetry is measured or unavailable, supports zero, and becomes stale', async () => {
  const p = page(), h = ble(p); h.state.battery = 0;
  await p.context.connect(); p.tick();
  assert.match(p.text('chartBatVal'), /0%.*voltage unavailable/);
  assert.match(p.text('chartTempVal'), /not reported/);
  p.run('watchState.lastSeen = Date.now() - 31000'); p.context.updateTelemetry();
  assert.match(p.text('chartBatVal'), /Stale/);
  h.state.battery = -1; await p.context.send('?'); p.context.updateTelemetry();
  assert.match(p.text('chartBatVal'), /Battery unavailable/);
  await p.context.disconnectTransport();
});

test('unsupported BLE tools cannot start a local timer or torch', async () => {
  const p = page(), h = ble(p); await p.context.connect();
  await assert.rejects(p.context.runTimer(60), /USB-C/);
  await p.ids.get('toggleTorchBtn').click();
  assert.equal(p.run('drawerTimerEndMs'), 0);
  assert.equal(p.run('drawerFlashlight'), false);
  assert.deepEqual(h.writes, ['?']);
  await p.context.disconnectTransport();
});

test('real Serial streams support split replies and acknowledged timers without a fake countdown', async () => {
  const p = page(), h = serial(p);
  assert.equal(await p.context.connectSerial(), true);
  assert.equal(p.run('watchState.version'), '0.15.3');
  await p.context.runTimer(60); p.tick();
  assert.equal(p.run('drawerTimerEndMs'), 0);
  assert.match(p.text('toolsMsg'), /does not report timer state/);
  assert.equal(p.text('studioTimerReadout'), 'On watch');
  assert.ok(h.writes.includes('TIMER:60'));
  await p.context.disconnectTransport();
  assert.equal(h.closed, 1);
});

test('one connection owns state; switching to Virtual closes Serial and ignores old BLE events', async () => {
  const p = page(), h = ble(p); await p.context.connect();
  await p.context.toggleSimulatedWatch();
  assert.equal(h.connected, false);
  h.emit('reply=ok;fw=old;bat=1;face=8');
  assert.equal(p.run('watchState.version'), '');
  assert.equal(p.run('isSimulated'), true);
  const cable = serial(p); await p.context.connectSerial();
  assert.equal(p.run('isSimulated'), false);
  await p.context.toggleSimulatedWatch();
  assert.equal(cable.closed, 1);
  assert.equal(p.run('isSerial'), false);
  await p.context.disconnectTransport();
});

test('Wi-Fi credentials are absent from logs and simulation status', async () => {
  const p = page(); await p.context.toggleSimulatedWatch();
  p.ids.get('ssid').value = 'Home'; p.ids.get('pass').value = 'private,secret';
  await p.context.sendWifi();
  await p.context.send('WIFI:Home,another-secret');
  const logs = p.ids.get('cliLog').children.map(el => el.textContent).join('\n');
  assert.match(logs, /redacted/);
  assert.doesNotMatch(logs + p.text('status'), /private,secret|another-secret/);
  assert.equal(p.ids.get('pass').value, '');
  await p.context.disconnectTransport();
});

test('profile failures never report an applied collection', async () => {
  const p = page(); ble(p, { ignoreSettings: true }); await p.context.connect();
  await assert.rejects(p.context.applyProfile({ name: 'Test', mask: 65, face: 6, motion: 'subtle', bright: 2, use12h: false }), /confirm/);
  assert.match(p.text('facesMsg'), /not fully applied/);
  await p.context.disconnectTransport();
});

test('downloads use real release/source paths instead of unshipped root files', () => {
  assert.doesNotMatch(html, /href="\/(?:firmware\/|v0\.15\.2\.patch|index\.html)/);
  assert.match(html, /releases\/tag\/v0\.15\.1/);
  assert.match(html, /blob\/main\/companion\/index\.html/);
});

test('a pending Virtual command is cancelled before it can change a new session', async () => {
  const p = page(); await p.context.toggleSimulatedWatch();
  const pending = p.context.send('TIMER:60');
  await settle();
  await p.context.disconnectTransport();
  await assert.rejects(pending, /Connection changed/);
  assert.equal(p.run('drawerTimerEndMs'), 0);
});

test('a cancelled BLE chooser cannot take ownership from a newer Virtual session', async () => {
  const p = page(), h = ble(p);
  let choose;
  p.navigator.bluetooth.requestDevice = () => new Promise(resolve => { choose = resolve; });
  const pending = p.context.connect(); await settle();
  await p.context.toggleSimulatedWatch(); choose(h.device);
  assert.equal(await pending, false);
  assert.equal(p.run('isSimulated'), true);
  assert.equal(h.connected, false);
  assert.match(p.text('firmwareVersionReadout'), /Virtual Watch/);
  await p.context.disconnectTransport();
});

test('rapid setting requests are serialized and every one reads back its own value', async () => {
  const p = page(), h = ble(p); await p.context.connect();
  await Promise.all([p.context.applySetting('bright', 0), p.context.applySetting('bright', 3)]);
  assert.deepEqual(h.writes, ['?', 'B=0', '?', 'B=3', '?']);
  assert.equal(p.run('watchState.bright'), 3);
  await p.context.disconnectTransport();
});

test('BLE notification query succeeds when the optional initial read fails', async () => {
  const p = page(), h = ble(p, { initialReadFails: true });
  assert.equal(await p.context.connect(), true);
  assert.deepEqual(h.writes, ['?']);
  assert.equal(p.run('watchState.version'), '0.15.3');
  assert.equal(p.run('transportReady'), true);
  await p.ids.get('fmt12Btn').click();
  assert.equal(h.state.format, 12);
  await p.context.disconnectTransport();
});

test('BLE refuses empty or malformed queried status even after a valid initial read', async () => {
  for (const queryStatus of ['', 'reply=ok', 'reply=ok;fw=starting;bat=oops;face=99']) {
    const p = page(), h = ble(p, { queryStatus });
    assert.equal(await p.context.connect(), false);
    assert.equal(p.run('transportReady'), false);
    assert.equal(h.connected, false);
    assert.match(p.text('connMsg'), /acknowledge|empty or invalid status/);
  }
});

test('BLE accepts valid available fields without requiring every firmware field', async () => {
  const p = page(); ble(p, { initialReadFails: true, queryStatus: 'reply=ok;bat=0' });
  assert.equal(await p.context.connect(), true);
  assert.equal(p.run('watchState.battery'), 0);
  assert.equal(p.run('watchState.version'), '');
  assert.equal(p.ids.get('fmt12Btn').disabled, true);
  assert.match(p.text('firmwareVersionReadout'), /Studio/);
  await p.context.disconnectTransport();
});

test('timer acknowledgements must match the requested operation and duration', async () => {
  const p = page();
  assert.equal(p.context.serialReply('TIMER:0').test('Timer started for 60 s'), false);
  assert.equal(p.context.serialReply('TIMER:60').test('Timer reset'), false);
  assert.equal(p.context.serialReply('TIMER:60').test('Timer started for 180 s'), false);
  assert.equal(p.context.serialReply('TIMER:60').test('Timer started for 60 s'), true);
  assert.equal(p.context.serialReply('TIMER:0').test('Timer reset'), true);
  assert.throws(() => p.context.serialReply('TIMER:garbage'), /integer/);
});

test('an unacknowledged timer quarantines the session and cancels the queued reset', async () => {
  const p = page(), h = serial(p);
  await p.context.connectSerial();
  p.run('serialWriter = { write: async () => {} }');
  const results = await Promise.allSettled([p.context.runTimer(60), p.context.runTimer(0)]);
  assert.equal(results[0].status, 'rejected');
  assert.match(results[0].reason.message, /result unknown.*Reconnect/);
  assert.equal(results[1].status, 'rejected');
  assert.equal(p.run('transportReady'), false);
  assert.doesNotMatch(p.text('toolsMsg'), /reset acknowledged/);
  await settle();
  assert.equal(h.closed, 1);
});

test('a stalled transport write still reaches its deadline and leaves controls usable', async () => {
  const p = page(); serial(p);
  await p.context.connectSerial();
  p.run('serialWriter = { write: () => new Promise(() => {}) }');
  await assert.rejects(p.context.runTimer(60), /did not acknowledge/);
  assert.equal(p.run('transportReady'), false);
  assert.equal(p.ids.get('startTimerBtn').disabled, true);
});

test('expanded timer presets and custom countdown quick-launch update readout and dispatch', async () => {
  const p = page(), h = serial(p);
  await p.context.connectSerial();

  // Test 10m (600s) preset
  const btn10m = p.elements ? p.elements.find(el => el.getAttribute('data-quick-timer') === '600') : p.context.document.querySelector('[data-quick-timer="600"]');
  assert.ok(btn10m, '10m preset button exists');
  await btn10m.click();
  assert.equal(p.text('studioTimerReadout'), '10:00');
  assert.ok(h.writes.includes('TIMER:600'));

  // Test 25m (1500s) preset
  const btn25m = p.context.document.querySelector('[data-quick-timer="1500"]');
  assert.ok(btn25m, '25m preset button exists');
  await btn25m.click();
  assert.equal(p.text('studioTimerReadout'), '25:00');
  assert.ok(h.writes.includes('TIMER:1500'));

  // Test custom countdown quick launch
  p.ids.get('customTimerSec').value = '450';
  await p.ids.get('launchCustomTimerBtn').click();
  assert.equal(p.text('studioTimerReadout'), '07:30');
  assert.ok(h.writes.includes('TIMER:450'));

  // Test reset
  await p.ids.get('resetTimerBtn').click();
  assert.equal(p.text('studioTimerReadout'), '03:00');
  assert.ok(h.writes.includes('TIMER:0'));

  await p.context.disconnectTransport();
});

test('BLE handshake synchronizes telemetry and hardware labels immediately upon connect', async () => {
  const p = page(), h = ble(p);
  h.state.battery = 72;
  h.state.bright = 3;
  h.state.format = 12;
  h.state.night = 1;

  assert.equal(await p.context.connect(), true);
  // Telemetry and hardware labels should be synchronized immediately after handshake
  assert.match(p.text('chartBatVal'), /72%.*voltage unavailable/);
  assert.match(p.text('hwFormatVal'), /12-Hour.*Night 22:00–07:00/);
  assert.equal(p.ids.get('fmt12Btn').classList.contains('active'), true);
  assert.equal(p.ids.get('fmt24Btn').classList.contains('active'), false);

  await p.context.disconnectTransport();
});

test('custom timer Enter keydown triggers launch and invalid duration reports error', async () => {
  const p = page(), h = serial(p);
  await p.context.connectSerial();

  // Test Enter key press on custom input
  p.ids.get('customTimerSec').value = '120';
  await p.ids.get('customTimerSec').dispatch('keydown', { key: 'Enter' });
  assert.equal(p.text('studioTimerReadout'), '02:00');
  assert.ok(h.writes.includes('TIMER:120'));

  // Test invalid durations (< 1 or > 86400)
  p.ids.get('customTimerSec').value = '0';
  await p.ids.get('launchCustomTimerBtn').click();
  assert.match(p.text('toolsMsg'), /duration between 1 and 86400/);

  p.ids.get('customTimerSec').value = '90000';
  await p.ids.get('launchCustomTimerBtn').click();
  assert.match(p.text('toolsMsg'), /duration between 1 and 86400/);

  await p.context.disconnectTransport();
});

test('USB-C serial handshake synchronizes telemetry and hardware labels immediately', async () => {
  const p = page(), h = serial(p);
  h.state.battery = 58;
  h.state.bright = 1;
  h.state.format = 24;
  h.state.night = 2;

  assert.equal(await p.context.connectSerial(), true);
  assert.match(p.text('chartBatVal'), /58%.*voltage unavailable/);
  assert.match(p.text('hwFormatVal'), /24-Hour.*Night 23:00–07:00/);
  assert.equal(p.ids.get('fmt24Btn').classList.contains('active'), true);
  assert.equal(p.ids.get('fmt12Btn').classList.contains('active'), false);

  await p.context.disconnectTransport();
});

