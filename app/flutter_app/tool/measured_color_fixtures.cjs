// Run from repository root after changing the shared measured-color contract.
// Produces Web reference pixels for the Dart parity test; no browser/device needed.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
class ImageData {
  constructor(data, width, height) {
    this.data = data;
    this.width = width;
    this.height = height;
  }
}
const context = vm.createContext({ window: {}, ImageData });
vm.runInContext(fs.readFileSync('tools/ForFilm/js/convert.js', 'utf8'), context);
const fixtures = [];
const width = 9, height = 6;
const source = [];
for (let y = 0; y < height; y++) {
  for (let x = 0; x < width; x++) {
    // Include skin tones, grayscale and saturated edges to exercise error limits.
    const shade = Math.round(x * 255 / (width - 1));
    const rgb = y === 0 ? [shade, shade, shade]
      : y === 1 ? [235 - x * 11, 181 - x * 9, 150 - x * 8]
      : [(x * 97 + y * 53) % 256, (x * 43 + y * 61) % 256, (x * 11 + y * 113) % 256];
    source.push(...rgb, 255);
  }
}
for (const algorithm of ['colorQualMeasured', 'colorQualCalibrated']) {
  const profile = context.CF_PROFILES[algorithm];
  assert.equal(profile.format, 2);
  assert.equal(profile.palette.length, 64);
  assert.equal(profile.candidateIndexes.length, 52);
  for (const [dither, strength] of [[false, 1], [true, 0], [true, 1], [true, 5]]) {
    const pixels = new ImageData(new Uint8ClampedArray(source), width, height);
    const result = context.cfQuantize(pixels, strength, dither, profile);
    const codes = [];
    for (let i = 0; i < result.data.length; i += 4) {
      const rgb = Array.from(result.data.slice(i, i + 3));
      const index = context.cfClosestIndex(...rgb, profile);
      assert.deepEqual(Array.from(profile.palette[index]), rgb);
      assert.ok(profile.candidateIndexes.includes(index));
      codes.push(index);
    }
    fixtures.push({ algorithm, width, height, source, dither, strength,
      codes, preview: Array.from(result.data) });
  }
  const cases = fixtures.filter(f => f.algorithm === algorithm);
  assert.deepEqual(cases[0].codes, cases[1].codes, 'zero strength equals no dithering');
  assert.notDeepEqual(cases[0].codes, cases[2].codes, 'dithering should affect this sample');
}
fs.writeFileSync('app/flutter_app/test/fixtures/measured_color.json', JSON.stringify(fixtures) + '\n');
console.log(`Generated ${fixtures.length} measured-color Web/Dart parity fixtures`);
