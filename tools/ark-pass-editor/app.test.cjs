/* SPDX-License-Identifier: GPL-3.0-or-later */
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const profileAPI = require('./profile.js');
const png = (width, height) => {
  const bytes = Buffer.alloc(24); bytes.set([137, 80, 78, 71, 13, 10, 26, 10]); bytes.writeUInt32BE(width, 16); bytes.writeUInt32BE(height, 20);
  return 'data:image/png;base64,' + bytes.toString('base64');
};
function harness() {
  const elements = new Map(), storage = new Map();
  function element() {
    const c = { width: 440, height: 608, value: '', disabled: false, textContent: '', classList: { toggle() {} }, parentElement: { classList: { toggle() {} } }, setAttribute() {}, addEventListener() {} };
    const g = { fillStyle: '', white: false,
      fillRect() { this.white = this.fillStyle === '#fff'; },
      drawImage() { assert.equal(this.white, true, 'transparent import must be white-backed'); },
      measureText(text) { return { width: text.length * 10 }; },
      getImageData(x, y, width, height) { return { width, height, data: new Uint8ClampedArray(width * height * 4).fill(255) }; },
      putImageData() {}, clearRect() {}, fillText() {}, strokeRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {}, save() {}, restore() {}, clip() {}, rect() {}
    };
    c.getContext = () => g; c.toDataURL = () => png(c.width, c.height);
    return c;
  }
  const c = vm.createContext({ console, setTimeout: cb => setImmediate(cb), clearTimeout: clearImmediate, Uint8Array, Uint8ClampedArray, Uint16Array, TextEncoder, TextDecoder, URLSearchParams, Blob,
    location: { search: '' }, navigator: {}, PassportProfile: profileAPI,
    localStorage: { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value) },
    document: { getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); }, createElement: element, querySelectorAll: () => [] },
    Image: class { async decode() { const bytes = Buffer.from(this.src.split(',')[1], 'base64'); this.width = this.naturalWidth = bytes.readUInt32BE(16); this.height = this.naturalHeight = bytes.readUInt32BE(20); } }
  });
  c.window = c; c.parent = c;
  const source = fs.readFileSync(path.join(__dirname, 'app.js'), 'utf8').replace('  init();', `  window.api = { imageFromDataUrl, parseProfile, restoreDraft, changeOutput, draw, jsonBytes, choosePassportBin, removeAvatar, syncForm,
    studentBaseUrl, STUDENT_AVATAR, STUDENT_CODE, STUDENT_NUMBER, STUDENT_RACE,
    set(value) { profile=value; avatarImage=null; avatarEdit=null; },
    get() { return {profile,renderedBin,drafts}; } };`);
  vm.runInContext(source, c);
  return { c, api: c.api, elements, storage };
}
const operator = () => ({ version: 3, kind: 'operator', output: 'i006', codename: '测试', codenameUnset: false, number: '01', numberUnset: false, affiliation: '', signature: '', avatar: null });
const student = () => ({ version: 3, kind: 'student', output: 'i006', template: 'rhodes-academy-student', palette: 'amiya', codename: '测试', codenameUnset: false, studentNumber: '202411020229', studentNumberUnset: false, race: '', avatar: null });

test('editor slot validation and migration normalize only legacy student color crops', async () => {
  const { api } = harness();
  const owner = student(); owner.avatar = png(239, 299);
  const img = await api.imageFromDataUrl(owner.avatar, owner);
  assert.equal(img.naturalWidth, 239); assert.equal(img.naturalHeight, 299);
  await assert.rejects(api.imageFromDataUrl(png(128, 160), owner), /239×299/);
  await assert.rejects(api.imageFromDataUrl(png(239, 299), operator()), /128×160/);
  const legacy = api.parseProfile(JSON.stringify({ ...student(), version: 2, avatar: png(128, 160), avatarMode: 'color' }));
  const restored = await api.imageFromDataUrl(legacy.avatar, legacy);
  assert.equal(restored.naturalWidth, 239); assert.equal(restored.naturalHeight, 299);
  assert.equal(legacy._legacyAvatar, false); assert.equal(legacy._legacySize, true);
});

test('mode switches never mutate color PNG and a late I006 generation cannot overwrite ColorFast', async () => {
  const { c, api } = harness(), pending = [];
  c.PassportPixels = { compact: (pixels, output) => new Promise(resolve => pending.push({ output, resolve, pixels })) };
  const value = { ...operator(), avatar: png(128, 160) };
  api.set(value); api.draw();
  await new Promise(setImmediate);
  assert.equal(pending[0].output, 'i006');
  api.changeOutput('colorfast');
  assert.equal(api.get().renderedBin, null, 'old BIN disabled immediately');
  await new Promise(setImmediate);
  assert.equal(pending[1].output, 'colorfast');
  pending[1].resolve({ bin: new Uint8Array([3]), rgba: pending[1].pixels.data });
  await new Promise(setImmediate);
  assert.equal(api.get().renderedBin[0], 3);
  pending[0].resolve({ bin: new Uint8Array([127]), rgba: pending[0].pixels.data });
  await new Promise(setImmediate);
  assert.equal(api.get().renderedBin[0], 3, 'stale asynchronous render rejected');
  assert.equal(api.get().profile.avatar, value.avatar);
  const wire = JSON.parse(new TextDecoder().decode(api.jsonBytes()));
  assert.equal(wire.version, 3); assert.equal(wire.kind, 'operator'); assert.equal(wire.output, 'colorfast');
  assert.equal(wire.avatar, value.avatar); assert.equal(wire.avatarEditor, undefined);
});

test('cap rejection does not create fallback BIN and migrated mono draft stays blocked across reloads', async () => {
  const { api, c, elements, storage } = harness();
  assert.throws(() => api.choosePassportBin((1 << 0) | (1 << 5) | (1 << 10), 'student', 'i006', new Uint8Array([127])), /未降级/);
  const old = api.parseProfile(JSON.stringify({ ...operator(), version: 1, avatar: png(128, 160), avatarMode: 'mono' }));
  api.set(old);
  c.PassportPixels = { compact: async pixels => ({ bin: new Uint8Array([3]), rgba: pixels.data }) };
  api.syncForm(); await new Promise(setImmediate); await new Promise(setImmediate);
  assert.equal(elements.get('avatar-mode').value, '', 'legacy FFUI must explicitly choose either new output');
  assert.equal(elements.get('download-bin').disabled, true);
  assert.match(elements.get('migration-status').textContent, /不能恢复颜色/);
  storage.set('framefilm-ark-pass-profile-v1', JSON.stringify({ draftVersion: 3, profile: old, avatarEditor: null }));
  await api.restoreDraft('operator');
  assert.equal(api.get().drafts.operator.profile._legacyAvatar, true);
  assert.equal(api.get().drafts.operator.profile._legacySize, true);
  api.removeAvatar(); api.changeOutput('i006'); await new Promise(setImmediate); await new Promise(setImmediate);
  assert.equal(api.get().profile.avatar, null);
  assert.equal(api.get().profile._legacyAvatar, false); assert.equal(api.get().profile._legacySize, false);
  assert.equal(elements.get('download-bin').disabled, false);
});

test('the revised assets are loaded without moving the avatar or fields', () => {
  const { api } = harness();
  assert.equal(api.studentBaseUrl('amiya'), 'assets/student-passport/amiya.png?v=3');
  assert.deepEqual({ ...api.STUDENT_AVATAR }, { x: 41, y: 111, width: 239, height: 299 });
  assert.equal(api.STUDENT_CODE.x, 305);
  assert.equal(api.STUDENT_NUMBER.x, 308);
  assert.equal(api.STUDENT_RACE.x, 304);
});
