/* SPDX-License-Identifier: GPL-3.0-or-later */
// Optional browser regression, not physical-device acceptance.
// Serve tools at 127.0.0.1:8768 and provide Playwright + Edge. This test
// supplies a BLE mock; the production standalone/embedded pages stay unmodified.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const palettes = ['amiya', 'sniper', 'medic', 'vanguard', 'guard', 'defender', 'caster', 'supporter', 'specialist'];
const stripeRuns = [[271, 274], [294, 299], [315, 324], [335, 352], [369, 407], [414, 490], [497, 572], [576, 720]];
async function mockBluetooth() {
  const mock = window.mockArk = { files: [], starts: 0, profileJson: null, currentFile: null, flags: (1 << 11) | (1 << 14) | (1 << 15), app: 5 };
  const encode = value => new TextEncoder().encode(value);
  const notify = (characteristic, channel, payload) => {
    const frame = new Uint8Array([0x55, channel, payload.length, ...payload, 0]);
    frame[frame.length - 1] = frame.slice(0, -1).reduce((sum, byte) => sum + byte, 0) & 255;
    characteristic.value = new DataView(frame.buffer); characteristic.dispatchEvent(new Event('characteristicvaluechanged'));
  };
  class Characteristic extends EventTarget {
    properties = { write: true };
    async startNotifications() { return this; }
    async writeValue(raw) {
      if (!peripheral.gatt.connected) throw new Error('Disconnected');
      const bytes = new Uint8Array(raw), channel = bytes[1], payload = bytes.slice(3, -1);
      let reply;
      if (channel === 0x42) reply = [2, 2, 208, 1, 224];
      else if (channel === 0x23) reply = [82];
      else if ([0x26, 0x28, 0x31, 0x3A].includes(channel)) reply = [0];
      else if (channel === 0x2A) reply = [0, 30];
      else if (channel === 0x54 || channel === 0x55) reply = [0, ...encode('FRAMEFILMARK-test'), 0];
      else if (channel === 0x4B) mock.app = bytes[3];
      else if (channel === 0x4C) reply = [mock.app];
      else if (channel === 0x51) reply = [3, 100, 0, 0, 0, 1, 0, 0, 0, 1, 0];
      else if (channel === 0x59) reply = [1, (mock.flags >>> 24) & 255, (mock.flags >>> 16) & 255, (mock.flags >>> 8) & 255, mock.flags & 255];
      else if (channel === 0x03) { mock.starts++; mock.currentFile = { name: '', bytes: [] }; }
      else if (channel === 0x00 && mock.currentFile) mock.currentFile.name = new TextDecoder().decode(payload).replace(/\0$/, '');
      else if (channel === 0x02 && mock.currentFile) mock.currentFile.bytes.push(...payload);
      else if (channel === 0x04 && mock.currentFile) {
        const file = mock.currentFile; mock.files.push(file);
        if (file.name === 'app/pass/profile.json') mock.profileJson = new TextDecoder().decode(Uint8Array.from(file.bytes));
        mock.currentFile = null;
      } else if (channel === 0x53) {
        const offset = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(3);
        const data = encode(mock.profileJson || '{"version":99}'), chunk = data.slice(offset, offset + (payload[4] || 128));
        const header = new Uint8Array(9), view = new DataView(header.buffer);
        view.setUint32(1, offset); view.setUint32(5, data.length); reply = [...header, ...chunk];
      }
      if (reply) await new Promise(resolve => setTimeout(() => { notify(this, channel, reply); resolve(); }, 1));
    }
  }
  const characteristic = new Characteristic(), peripheral = new EventTarget(); peripheral.name = 'FRAMEFILMARK-test';
  peripheral.gatt = {
    connected: false, async connect() { this.connected = true; return this; },
    disconnect() { this.connected = false; peripheral.dispatchEvent(new Event('gattserverdisconnected')); },
    async getPrimaryService() { return { getCharacteristic: async () => characteristic }; }
  };
  Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: { requestDevice: async () => peripheral, getDevices: async () => [peripheral] } });
}

(async () => {
  const browser = await chromium.launch({ headless: true, channel: 'msedge' });
  try {
    for (const embedded of [false, true]) {
      const page = await browser.newPage({ viewport: { width: 1280, height: 960 }, reducedMotion: 'reduce' });
      page.setDefaultTimeout(120000);
      const errors = []; page.on('pageerror', error => errors.push(error.message));
      await page.route(/fonts\.(googleapis|gstatic)\.com/, route => route.abort());
      await page.addInitScript(mockBluetooth);
      await page.goto(`http://127.0.0.1:8768/${embedded ? 'ForFilm' : 'ark-pass-editor'}/`, { waitUntil: 'networkidle' });
      let frame = page;
      if (embedded) {
        await page.locator('[data-page="bluetooth-page"]').click(); await page.locator('#scan-button').click();
        await page.waitForFunction(() => ArkDevice.connected());
        await page.locator('#ark-login[open] .ark-login-skip').click(); await page.locator('[data-page="pass-page"]').click();
        frame = await (await page.locator('#ark-pass-editor').elementHandle()).contentFrame();
      } else await page.locator('#connect').click();
      const at = id => frame.locator('#' + id);
      const ready = () => frame.waitForFunction(() => !document.getElementById('download-bin').disabled);
      if (!embedded) {
        for (const palette of palettes) {
          const response = await page.request.get(`http://127.0.0.1:8768/ark-pass-editor/assets/student-passport/${palette}.png`);
          assert.equal(response.status(), 200, palette);
          const appAsset = path.join(__dirname, '../../../app/flutter_app/assets/student_passport', `${palette}.png`);
          assert.deepEqual(await response.body(), fs.readFileSync(appAsset), `${palette} differs from Flutter`);
        }
      }
      const sampleAvatar = await frame.evaluate(() => {
        const make = (width, height) => {
          const canvas = document.createElement('canvas'); canvas.width = width; canvas.height = height;
          const g = canvas.getContext('2d'); const gradient = g.createLinearGradient(0, 0, width, height);
          gradient.addColorStop(0, '#d5a58c'); gradient.addColorStop(1, '#153c87'); g.fillStyle = gradient; g.fillRect(0, 0, width, height);
          return canvas.toDataURL('image/png');
        };
        return { student: make(239, 299), operator: make(128, 160) };
      });
      for (const kind of ['student', 'operator']) {
        const profile = kind === 'student' ? { version: 3, kind, output: 'i006', template: 'rhodes-academy-student', palette: 'amiya',
          codename: '学生档案测试', codenameUnset: false, studentNumber: '202411020229', studentNumberUnset: false, race: '菲林', avatar: sampleAvatar.student } :
          { version: 3, kind, output: 'i006', codename: '干员测试', codenameUnset: false, number: '01', numberUnset: false, affiliation: '', signature: '', avatar: sampleAvatar.operator };
        await at('import-json').setInputFiles({ name: 'profile.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(profile)) });
        await at('file-status').filter({ hasText: '已导入' }).waitFor(); await ready();
        const storedAvatar = await frame.evaluate(kind => JSON.parse(localStorage.getItem(kind === 'student'
          ? 'framefilm-ark-pass-rhodes-academy-student-v2' : 'framefilm-ark-pass-profile-v1')).profile.avatar, kind);
        if (kind === 'student') {
          assert.deepEqual(await at('student-palette').locator('option').evaluateAll(nodes => nodes.map(node => node.value)), palettes);
          const runs = await at('preview').evaluate(canvas => {
            const pixels = canvas.getContext('2d').getImageData(185, 3, 535, 1).data;
            const colored = x => x < 720 && pixels.slice((x - 185) * 4, (x - 185) * 4 + 3).some(v => v < 240);
            const spans = [];
            for (let x = 185; x < 720;) {
              if (!colored(x)) { x++; continue; }
              const start = x;
              while (colored(x)) x++;
              spans.push([start, x]);
            }
            return spans;
          });
          assert.deepEqual(runs, stripeRuns, 'eight segments must render with the measured gaps');
        }
        const output = at(kind === 'student' ? 'student-output' : 'avatar-mode');
        assert.deepEqual(await output.locator('option').evaluateAll(nodes => nodes.map(node => node.value)), ['i006', 'colorfast']);
        for (const mode of ['i006', 'colorfast']) {
          await output.selectOption(mode); await ready();
          await page.evaluate(() => { mockArk.files = []; mockArk.flags = (1 << 0) | (1 << 5) | (1 << 10); });
          const starts = await page.evaluate(() => mockArk.starts);
          await at('send').click(); await at('device-status').filter({ hasText: '未降级' }).waitFor();
          assert.equal(await page.evaluate(() => mockArk.starts), starts, 'capability rejection must not touch files');
          await page.evaluate(() => { mockArk.flags = (1 << 11) | (1 << 14) | (1 << 15); });
          await at('send').click(); await at('device-status').filter({ hasText: '回读一致' }).waitFor();
          const received = await page.evaluate(() => mockArk.files);
          assert.deepEqual(received.map(file => file.name), ['app/pass/profile.bin', 'app/pass/profile.json']);
          const bytes = received[0].bytes, json = JSON.parse(new TextDecoder().decode(Uint8Array.from(received[1].bytes)));
          assert.equal(json.version, 3); assert.equal(json.kind, kind); assert.equal(json.output, mode); assert.equal(json.avatar, storedAvatar);
          assert.equal(json.avatarMode, undefined); assert.equal(json.studentOutput, undefined); assert.equal(json.avatarEditor, undefined);
          if (kind === 'student') {
            assert.equal(bytes.length, mode === 'i006' ? 691232 : 345632); assert.equal(bytes[9], mode === 'i006' ? 127 : 3);
            assert.deepEqual(bytes.slice(4, 8), [208, 2, 224, 1]); assert.equal(bytes[10], 1); assert.equal(bytes[11], 0);
            if (mode === 'i006') {
              const redFade = createHash('sha256');
              for (let y = 0; y < 10; y++) for (let x = 414; x < 490; x++) {
                const offset = 32 + 2 * (720 * 480 - 1 - y * 720 - x);
                redFade.update(Buffer.from(bytes.slice(offset, offset + 2)));
              }
              // I6RFW101 in reader order, with the red segment resized from 78 to 76 px.
              assert.equal(redFade.digest('hex'), 'f51bc6163123703001e4d3cf9ec24746f076adc69d20165e9368f3efc4d4dcd1');
            }
          } else {
            assert.equal(bytes.length, mode === 'i006' ? 74416 : 53936);
            assert.deepEqual(bytes.slice(0, 16), [70, 70, 85, 73, 3, mode === 'i006' ? 127 : 3, 184, 1, 96, 2, 1, 0, 0, 0, 0, 0]);
          }
        }
      }
      // Large transparent PNG exercises the real Canvas encoder/decoder and
      // the staged pre-resize path. It must remain paper-white, not opaque black.
      const transparent = await frame.evaluate(() => {
        const canvas = document.createElement('canvas'); canvas.width = 4096; canvas.height = 4096;
        const g = canvas.getContext('2d'); g.fillStyle = '#d5a58c'; g.fillRect(1900, 1900, 296, 296);
        return canvas.toDataURL('image/png').split(',')[1];
      });
      await at('template-select').selectOption('rhodes-academy-student'); await ready();
      await at('avatar').setInputFiles({ name: 'large-transparent.png', mimeType: 'image/png', buffer: Buffer.from(transparent, 'base64') });
      await frame.locator('[data-apply]:enabled').waitFor();
      assert.deepEqual(await frame.locator('[data-crop]').evaluate(canvas => [canvas.width, canvas.height, ...canvas.getContext('2d').getImageData(0, 0, 1, 1).data]), [239, 299, 255, 255, 255, 255]);
      await frame.locator('[data-cancel]').click();
      await page.setViewportSize({ width: 360, height: 740 });
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
      assert.deepEqual(errors, []); await page.close();
    }
    console.log('PASS standalone + embedded v3 outputs, caps/no downgrade, unchanged RGB source, exact BIN headers, and transparent crop');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
