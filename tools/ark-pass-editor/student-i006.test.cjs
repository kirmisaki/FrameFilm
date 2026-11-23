/* SPDX-License-Identifier: GPL-3.0-or-later */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');
const { test } = require('node:test');

const WIDTH = 720, HEIGHT = 480, COUNT = WIDTH * HEIGHT;
const repo = path.resolve(__dirname, '../..');
const source = fs.readFileSync(path.join(__dirname, 'student-i006.js'), 'utf8');
function load({ real = false, schedule = callback => setImmediate(callback) } = {}) {
  const context = vm.createContext({ setTimeout: schedule, Uint8Array, Uint16Array, Uint8ClampedArray, Float64Array });
  context.window = context;
  for (const file of ['ark-512-profile.js', 'ark-512.js']) {
    vm.runInContext(fs.readFileSync(path.join(repo, 'tools/ForFilm/js', file), 'utf8'), context);
  }
  const ark = context.window.Ark512, calls = [];
  if (!real) ark.render = async (image, options) => {
    calls.push({ image, options });
    const codes = new Uint16Array(COUNT);
    for (let y = 0; y < WIDTH; y++) for (let x = 0; x < HEIGHT; x++) codes[y * HEIGHT + x] = [75, 387, 407][(x + y) % 3];
    return { codes };
  };
  vm.runInContext(source, context);
  return { helper: context.window.PassportStudentI006, ark, calls };
}
const image = () => ({ width: WIDTH, height: HEIGHT, data: new Uint8ClampedArray(COUNT * 4).fill(255) });
const copy = input => ({ ...input, data: input.data.slice() });
function fill(input, x, y, width, height, color) {
  for (let yy = y; yy < y + height; yy++) for (let xx = x; xx < x + width; xx++) input.data.set(color, (yy * WIDTH + xx) * 4);
}
const baseline = (x, y) => [75, 387, 407][(HEIGHT - 1 - y + x) % 3];
const readerCode = (codes, x, y) => codes[COUNT - 1 - (y * WIDTH + x)];

test('reader/logical/native geometry, Ark reuse, ABC9 profile.bin and preview agree without mutation', async () => {
  const { helper, ark, calls } = load();
  const base = image(), composed = image();
  fill(composed, 0, 0, 1, 1, [1, 2, 3, 255]);
  fill(composed, 719, 0, 1, 1, [4, 5, 6, 255]);
  fill(composed, 0, 479, 1, 1, [7, 8, 9, 255]);
  fill(composed, 719, 479, 1, 1, [10, 11, 12, 255]);
  const original = composed.data.slice(), originalBase = base.data.slice(), cancelled = () => false;
  const result = await helper.render(composed, base, { cancelled });
  assert.ok(result.bin instanceof Uint8Array);
  assert.ok(result.rgba instanceof Uint8ClampedArray);
  assert.equal(result.bin.length, 691232);
  assert.deepEqual([...result.bin.slice(9, 16)], [127, 1, 0, 65, 66, 67, 57]);
  const { codes, width, height } = ark.parse(result.bin);
  assert.equal(width, WIDTH); assert.equal(height, HEIGHT);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].options.continuousTone, true);
  assert.equal(calls[0].options.cancelled, cancelled);
  assert.equal(calls[0].image.width, HEIGHT); assert.equal(calls[0].image.height, WIDTH);
  const logical = calls[0].image.data;
  for (const [x, y] of [[0, 0], [719, 0], [0, 479], [719, 479], [320, 230]]) {
    const k = (x * HEIGHT + HEIGHT - 1 - y) * 4;
    assert.deepEqual([...logical.slice(k, k + 4)], [...composed.data.slice((y * WIDTH + x) * 4, (y * WIDTH + x) * 4 + 4)]);
  }
  const physical = ark.preview(codes, WIDTH, HEIGHT);
  for (let i = 0; i < COUNT; i++) {
    const x = i % WIDTH, y = Math.floor(i / WIDTH);
    assert.equal(readerCode(codes, x, y), baseline(x, y));
    const src = (COUNT - 1 - i) * 4;
    for (let c = 0; c < 4; c++) assert.equal(result.rgba[i * 4 + c], physical[src + c]);
  }
  assert.deepEqual(composed.data, original);
  assert.deepEqual(base.data, originalBase);
});

test('A mask preserves fields, base text edges, avatar, near whites and neighboring colors', async () => {
  const { helper, ark } = load();
  const base = image();
  fill(base, 380, 125, 330, 340, [244, 244, 244, 255]);
  // Inclusive/exclusive L and chroma boundaries, away from the dark text.
  for (const [x, rgb] of [[400, [231, 231, 231]], [420, [232, 232, 232]], [440, [254, 254, 254]],
    [460, [255, 255, 255]], [480, [244, 247, 244]], [500, [244, 248, 244]], [520, [225, 225, 225]]]) {
    fill(base, x, 150, 8, 8, [...rgb, 255]);
  }
  fill(base, 580, 180, 10, 10, [21, 60, 135, 255]);
  fill(base, 600, 180, 10, 10, [213, 165, 140, 255]);
  fill(base, 620, 300, 1, 1, [0, 0, 0, 255]);
  const composed = copy(base);
  // Fields include a pale neutral overlay which luma/chroma alone cannot protect.
  fill(composed, 490, 350, 40, 8, [245, 245, 245, 255]);
  fill(composed, 550, 400, 40, 8, [0, 0, 0, 255]);
  fill(composed, 590, 350, 1, 1, [244, 244, 244, 254]);
  const avatarRect = { x: 640, y: 180, width: 40, height: 60 };
  const { bin } = await helper.render(composed, base, { avatarRect });
  const { codes } = ark.parse(bin);
  const at = (x, y) => readerCode(codes, x, y);
  for (const [x, y] of [[384, 200], [400, 129], [403, 153], [463, 153], [503, 153], [523, 153],
    [583, 183], [603, 183], [620, 300], [618, 298], [622, 302], [500, 353], [560, 403], [590, 350], [650, 200]]) {
    assert.equal(at(x, y), baseline(x, y), `protected reader pixel ${x},${y}`);
  }
  // A flat one-pixel region starts below threshold; the fixed A intensity does not become B.
  const isolated = image();
  fill(isolated, 700, 470, 1, 1, [244, 244, 244, 255]);
  const isolatedCodes = ark.parse((await helper.render(isolated, isolated)).bin).codes;
  assert.equal(readerCode(isolatedCodes, 700, 470), 75);

  let active = 0, gray = 0;
  // Independent predicate for every pixel: no code change may escape the mask.
  const luma = (x, y) => {
    const k = (y * WIDTH + x) * 4;
    return (19595 * base.data[k] + 38470 * base.data[k + 1] + 7471 * base.data[k + 2] + 32768) >> 16;
  };
  for (let y = 0; y < HEIGHT; y++) for (let x = 0; x < WIDTH; x++) {
    const k = (y * WIDTH + x) * 4, rgb = [...base.data.slice(k, k + 3)], value = luma(x, y);
    let mask = x >= 385 && y >= 130 && value >= 232 && value < 255 && Math.max(...rgb) - Math.min(...rgb) <= 3 &&
      base.data.slice(k, k + 4).every((v, c) => v === composed.data[k + c]) &&
      !(x >= avatarRect.x && x < avatarRect.x + avatarRect.width && y >= avatarRect.y && y < avatarRect.y + avatarRect.height);
    if (mask) for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) {
      if (luma(Math.max(0, Math.min(719, x + dx)), Math.max(0, Math.min(479, y + dy))) < 225) mask = false;
    }
    if (mask) {
      assert.ok(at(x, y) === 75 || at(x, y) === 407);
      if (value === 244) { active++; if (at(x, y) === 407) gray++; }
    } else assert.equal(at(x, y), baseline(x, y), `outside mask ${x},${y}`);
  }
  assert.ok(active > 90000);
  assert.ok(Math.abs(gray / active - .35) < .01, `A gray occupancy ${gray / active}`);
  for (const [x, y] of [[385, 130], [423, 153], [443, 153], [483, 153], [623, 300]]) assert.ok([75, 407].includes(at(x, y)));
});

test('invalid input and pre-cancellation fail before Ark rendering', async () => {
  const { helper, calls } = load();
  await assert.rejects(helper.render({ ...image(), width: 480 }, image()), /720×480/);
  await assert.rejects(helper.render(image(), { ...image(), data: new Uint8Array(4) }), /720×480/);
  await assert.rejects(helper.render(image(), image(), { avatarRect: { x: NaN, y: 0, width: 10, height: 10 } }), /头像区域/);
  await assert.rejects(helper.render(image(), image(), { cancelled: () => true }), /预览已取消/);
  assert.equal(calls.length, 0);
});

test('cancellation is checked after yielding in each helper phase and passed into Ark', async () => {
  // Helper yields: preparation 30, native rotation 45, mask 22, FS 30, preview 30.
  for (const stopAt of [1, 31, 76, 98, 128]) {
    let ticks = 0, cancelled = false;
    const { helper, calls } = load({ schedule: callback => {
      if (++ticks === stopAt) cancelled = true;
      setImmediate(callback);
    } });
    await assert.rejects(helper.render(image(), image(), { cancelled: () => cancelled }), /预览已取消/);
    assert.equal(ticks, stopAt);
    assert.equal(calls.length, stopAt <= 30 ? 0 : 1);
  }
  const { helper, ark } = load();
  ark.render = async (_, options) => {
    assert.equal(typeof options.cancelled, 'function');
    throw new Error('Ark cancelled render');
  };
  await assert.rejects(helper.render(image(), image(), { cancelled: () => false }), /Ark cancelled render/);
});

test('known I006 continuous + A snapshot is byte-identical to PW23112A.film when available', { timeout: 120000 }, async t => {
  const folder = path.join(repo, '.output/ark/student-pass-simulator');
  const composedPath = path.join(folder, 'trim-browser-review/amiya-color/profile-source.png');
  const basePath = path.join(__dirname, 'student-palettes/fullscreen/amiya.png');
  const expectedPath = path.join(folder, 'PW23112A.film');
  if (![composedPath, basePath, expectedPath].every(file => fs.existsSync(file))) {
    t.skip('Local reviewed snapshot is not present; synthetic protection and geometry tests still run.');
    return;
  }
  // Decode only trusted local fixture images. Never execute the trial scripts,
  // which generate other files; Python isolated mode avoids local module imports.
  let raw;
  try {
    raw = execFileSync('python', ['-I', '-c',
      'from PIL import Image; import sys\nfor filename in sys.argv[1:]:\n im=Image.open(filename); assert im.size==(720,480); sys.stdout.buffer.write(im.convert("RGBA").tobytes())',
      composedPath, basePath], { maxBuffer: COUNT * 8 + 1024 });
  } catch (error) {
    if (error.code === 'ENOENT' || /No module named ['"]PIL/.test(String(error.stderr))) {
      t.skip('Optional snapshot decoding needs Python with Pillow.');
      return;
    }
    throw error;
  }
  const composed = { width: WIDTH, height: HEIGHT, data: Uint8ClampedArray.from(raw.subarray(0, COUNT * 4)) };
  const base = { width: WIDTH, height: HEIGHT, data: Uint8ClampedArray.from(raw.subarray(COUNT * 4)) };
  const { helper, ark } = load({ real: true, schedule: setTimeout });
  const result = await helper.render(composed, base);
  assert.deepEqual(Buffer.from(result.bin), fs.readFileSync(expectedPath));
  assert.equal(ark.parse(result.bin).codes.length, COUNT);
});
