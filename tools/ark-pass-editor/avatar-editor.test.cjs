/* SPDX-License-Identifier: GPL-3.0-or-later */
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const { test } = require('node:test');
function load() {
  const context = vm.createContext({ window: {}, console, Uint8ClampedArray, Uint8Array, Float32Array, Float64Array, Math });
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../ForFilm/js/source-fit.js'), 'utf8'), context);
  context.FrameFilmSourceFit = context.window.FrameFilmSourceFit;
  vm.runInContext(fs.readFileSync(path.join(__dirname, '../ForFilm/js/convert.js'), 'utf8'), context);
  vm.runInContext(fs.readFileSync(path.join(__dirname, 'avatar-editor.js'), 'utf8'), context);
  return context;
}

test('old local options remain readable but output has no six-color/mono dispatch', () => {
  const c = load(), editor = c.window.PassportAvatarEditor;
  const valid = editor.validateEdit({ source: 'data:image/png;base64,AA==', options: { zoom: 2, turns: 1 } });
  assert.equal(valid.options.zoom, 2); assert.equal(valid.options.turns, 1);
  assert.throws(() => editor.validateEdit({ source: valid.source, options: { zoom: NaN } }));
  assert.throws(() => editor.validateEdit({ source: valid.source, options: { rangeFit: 2 } }));
  for (const name of ['adaptiveDither', 'applyDitherByType', 'findClosestColor']) c[name] = () => assert.fail('color source must never be quantized');
  let last;
  c.document = { createElement: () => ({ width: 0, height: 0, toDataURL: () => 'data:image/png;base64,raw', getContext: () => ({
    fillRect() {}, save() {}, translate() {}, rotate() {}, drawImage() {}, restore() {},
    putImageData(p) { last = p; },
    getImageData(x, y, width, height) { return { width, height, data: Uint8ClampedArray.from({ length: width * height * 4 }, (_, i) => [213, 165, 140, 255][i % 4]) }; }
  }) }) };
  for (const [width, height] of [[128, 160], [239, 299]]) {
    const result = editor.render({ width: 640, height: 800 }, editor.defaults, { width, height });
    assert.equal(result.pixels.width, width); assert.equal(result.pixels.height, height);
    assert.deepEqual([...last.data.subarray(0, 4)], [213, 165, 140, 255]);
    assert.equal(result.crop, result.processed, 'stored crop is color RGB, not a second quantized image');
  }
  editor.render({ width: 640, height: 800 }, { ...editor.defaults, brightness: .1, contrast: 1.5 }, { width: 239, height: 299 });
  assert.deepEqual([...last.data.subarray(0, 3)], [255, 222, 185]);
});

test('large transparent sources are white-backed before every reduction stage and final crop', () => {
  const c = load(), stages = [];
  c.document = { createElement: () => {
    const canvas = { width: 0, height: 0, toDataURL: () => 'data:image/png;base64,white' };
    const context = { fillStyle: '', filled: false,
      fillRect() { assert.equal(this.fillStyle, '#fff'); this.filled = true; },
      save() {}, translate() {}, rotate() {}, restore() {}, putImageData() {},
      drawImage(source) { assert.equal(this.filled, true, 'opaque canvas must not turn transparent pixels black'); stages.push([canvas.width, canvas.height, source.width, source.height]); },
      getImageData(x, y, width, height) { return { width, height, data: new Uint8ClampedArray(width * height * 4).fill(255) }; }
    };
    canvas.getContext = () => context; return canvas;
  } };
  const result = c.window.PassportAvatarEditor.render({ width: 4096, height: 4096, transparent: true }, c.window.PassportAvatarEditor.defaults, { width: 239, height: 299 });
  assert.ok(stages.length >= 3, 'large source uses staged high-quality reduction');
  assert.ok(result.pixels.data.every(x => x === 255));
  assert.deepEqual(stages.at(-1).slice(0, 2), [239, 299]);
});
