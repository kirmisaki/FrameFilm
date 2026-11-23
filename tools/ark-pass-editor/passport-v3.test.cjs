/* SPDX-License-Identifier: GPL-3.0-or-later */
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const profile = require('./profile.js');
function load() {
  const c = vm.createContext({ console, setTimeout: cb => setImmediate(cb), TextEncoder, Uint8Array, Uint16Array, Uint8ClampedArray, Float32Array, Float64Array, DataView });
  c.window = c;
  c.ImageData = class { constructor(width, height) { this.width = width; this.height = height; this.data = new Uint8ClampedArray(width * height * 4); } };
  for (const file of ['../ForFilm/js/source-fit.js', '../ForFilm/js/convert.js', '../ForFilm/js/ark-512-profile.js', '../ForFilm/js/ark-512.js', 'pixels.js']) {
    vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), c);
  }
  return c;
}
const student = { version: 3, kind: 'student', output: 'i006', template: 'rhodes-academy-student', palette: 'amiya', codename: '测试', codenameUnset: false, studentNumber: '202411020229', studentNumberUnset: false, race: '菲林', avatar: null };
const operator = { version: 3, kind: 'operator', output: 'colorfast', codename: '测试', codenameUnset: false, number: '01', numberUnset: false, affiliation: '', signature: '', avatar: null };
const image = (width, height) => ({ width, height, data: new Uint8ClampedArray(width * height * 4).fill(255) });
function fill(p, x, y, width, height, color) {
  for (let yy = y; yy < y + height; yy++) for (let xx = x; xx < x + width; xx++) p.data.set([...color, 255], (yy * p.width + xx) * 4);
}

test('explicit v3 schema, local-only migration flags, old versions and exact UTF-8 limit', () => {
  for (const value of [student, operator]) for (const output of ['i006', 'colorfast']) {
    assert.deepEqual(profile.parse(JSON.stringify({ ...value, output })), { ...value, output });
  }
  assert.throws(() => profile.parse(JSON.stringify({ ...student, output: 'ffui' })), /输出/);
  assert.throws(() => profile.parse(JSON.stringify({ ...student, palette: 'unknown' })), /配色/);
  for (const avatarMode of ['mono', 'grayscale']) assert.throws(() => profile.parse(JSON.stringify({ ...student, avatarMode })), /黑白\/灰阶/);
  assert.throws(() => profile.parse(JSON.stringify({ ...student, kind: 'unknown' })), /kind/);
  const legacy = profile.parse(JSON.stringify({ ...student, version: 2, studentOutput: 'ffui', avatar: 'data:image/png;base64,AA==', avatarMode: 'mono' }));
  assert.equal(legacy.kind, 'student'); assert.equal(legacy.output, 'i006');
  assert.equal(legacy._legacyAvatar, true); assert.equal(legacy._legacyOutput, true); assert.equal(legacy._legacySize, true);
  const oldOperator = profile.parse(JSON.stringify({ ...operator, version: 1, avatarMode: 'color' }));
  assert.equal(oldOperator.kind, 'operator'); assert.equal(oldOperator._legacyAvatar, false);
  assert.ok(!new TextDecoder().decode(profile.wire(legacy)).includes('_legacy'));
  assert.throws(() => profile.wire({ ...student, avatar: '中'.repeat(120000) }), /350,000/);
  const empty = profile.wire({ ...student, avatar: '' }).length;
  assert.equal(profile.wire({ ...student, avatar: 'a'.repeat(350000 - empty) }).length, 350000);
  assert.throws(() => profile.wire({ ...student, avatar: 'a'.repeat(350001 - empty) }), /350,000/);
});

test('capabilities are mode-specific; old six-color/general ABC9 bits never downgrade', () => {
  const oldFlags = (1 << 0) | (1 << 5) | (1 << 10) | (1 << 13);
  for (const kind of ['student', 'operator']) for (const output of ['i006', 'colorfast']) {
    assert.throws(() => profile.capability(oldFlags, kind, output), /未降级/);
  }
  profile.capability(1 << 11, 'student', 'i006');
  profile.capability(1 << 14, 'student', 'colorfast');
  for (const output of ['i006', 'colorfast']) profile.capability(1 << 15, 'operator', output);
  assert.throws(() => profile.capability(1 << 11, 'student', 'colorfast'));
  assert.throws(() => profile.capability(1 << 14, 'student', 'i006'));
});

test('true ColorFast student generic header, native corners, bounded indexes, same-code preview and source immutability', () => {
  const c = load(), p = image(720, 480);
  for (const [x, y, index] of [[0, 0, 32], [719, 0, 47], [0, 479, 63], [719, 479, 52]]) fill(p, x, y, 1, 1, c.CF_PROFILES.colorFast55.palette[index]);
  fill(p, 385, 130, 250, 200, [244, 244, 244]); // template watermark remains RGB input, not an ABC9 mask
  const before = p.data.slice(), result = c.PassportPixels.studentColorFast(p);
  assert.deepEqual(p.data, before);
  const v = new DataView(result.bin.buffer);
  assert.equal(result.bin.length, 345632); assert.equal(v.getUint32(0, true), 345600);
  assert.equal(v.getUint16(4, true), 720); assert.equal(v.getUint16(6, true), 480);
  assert.equal(result.bin[8], 0); assert.equal(result.bin[9], 3); assert.equal(v.getUint16(10, true), 1);
  assert.ok(result.bin.subarray(12, 32).every(x => x === 0));
  const palette = c.CF_PROFILES.colorFast55.palette;
  for (let i = 0; i < 345600; i++) {
    const code = result.bin[32 + 345599 - i]; assert.ok(code <= 63);
    assert.deepEqual([...result.rgba.subarray(i * 4, i * 4 + 4)], [...palette[code], 255]);
  }
  for (const [x, y, index] of [[0, 0, 32], [719, 0, 47], [0, 479, 63], [719, 479, 52]]) assert.equal(result.bin[32 + 345599 - y * 720 - x], index);
});

test('compact FFUI v3 exact I1 layout, hasAvatar flag, LE16 I006 or ColorFast planes, no source mutation', async () => {
  const c = load(), p = image(440, 608);
  fill(p, 0, 0, 1, 1, [0, 0, 0]); fill(p, 439, 607, 1, 1, [0, 0, 0]);
  fill(p, 20, 143, 128, 160, [213, 165, 140]);
  const before = p.data.slice();
  for (const output of ['i006', 'colorfast']) for (const hasAvatar of [false, true]) {
    const result = await c.PassportPixels.compact(p, output, hasAvatar), bytes = result.bin;
    assert.equal(bytes.length, 33456 + (hasAvatar ? 20480 * (output === 'i006' ? 2 : 1) : 0));
    assert.deepEqual([...bytes.subarray(0, 16)], [70, 70, 85, 73, 3, output === 'i006' ? 127 : 3, 184, 1, 96, 2, Number(hasAvatar), 0, 0, 0, 0, 0]);
    assert.equal(bytes[16] & 128, 128); assert.equal(bytes[33455] & 1, 1);
    assert.equal(bytes[16] & 64, 0);
    if (hasAvatar) {
      const codes = new Uint16Array(20480), view = new DataView(bytes.buffer);
      for (let i = 0; i < codes.length; i++) {
        codes[i] = output === 'i006' ? view.getUint16(33456 + i * 2, true) : bytes[33456 + i];
        assert.ok(codes[i] <= (output === 'i006' ? 511 : 63));
      }
      const expected = output === 'i006' ? c.Ark512.preview(codes, 128, 160) : null;
      for (let y = 0; y < 160; y++) for (let x = 0; x < 128; x++) {
        const i = y * 128 + x, dst = ((143 + y) * 440 + 20 + x) * 4;
        const rgb = expected ? [...expected.subarray(i * 4, i * 4 + 4)] : [...c.CF_PROFILES.colorFast55.palette[codes[i]], 255];
        assert.deepEqual([...result.rgba.subarray(dst, dst + 4)], rgb);
      }
      assert.ok(new Set(codes).size > 1, 'skin-tone detail is not reduced to one of six inks');
    }
    const chrome = output === 'i006' ? c.Ark512.preview(new Uint16Array([387, 75]), 2, 1) : new Uint8Array([0, 0, 0, 255, 255, 255, 255, 255]);
    assert.deepEqual([...result.rgba.subarray(0, 8)], [...chrome]);
  }
  assert.deepEqual(p.data, before);
  await assert.rejects(c.PassportPixels.compact(p, 'i006', true, { cancelled: () => true }), /取消/);
  await assert.rejects(c.PassportPixels.compact({ ...p, width: 720 }, 'i006', false), /无效/);
});
