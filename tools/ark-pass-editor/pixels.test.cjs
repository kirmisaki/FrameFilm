const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { encode } = require('./pixels.js');
const legacyPalette = [[0,0,0],[255,255,255],[255,255,0],[255,0,0],[0,0,255],[41,204,20]];
// ui_display native_to_fast -> ForFilm CF_PALETTE_FAST / SDL RGB fixture.
const measuredPalette = [[0,0,0],[255,255,255],[219,198,57],[180,71,32],[21,60,135],[123,149,112]];
const codes = [0,1,2,3,5,6];
const blank = (width = 440, height = 608) => new Uint8ClampedArray(width * height * 4).fill(255);
const put = (pixels, width, x, y, color) => pixels.set(color, (y * width + x) * 4);
const fill = (pixels, width, rect, color) => {
  for (let y = rect.y; y < rect.y + rect.height; y++) for (let x = rect.x; x < rect.x + rect.width; x++) {
    put(pixels, width, x, y, typeof color === 'function' ? color(x, y) : color);
  }
};
const deviceCode = (bin, x, y) => {
  const width = bin[6] | (bin[7] << 8), i = y * width + x;
  return bin[4] === 2 ? (bin[16 + (i >> 1)] >> ((i & 1) ? 0 : 4)) & 15 :
    (bin[16 + (i >> 3)] & (0x80 >> (i & 7))) ? 0 : 1;
};
const yOf = rgb => .299 * rgb[0] + .587 * rgb[1] + .114 * rgb[2];
const meanY = (pixels, width, rect) => {
  let sum = 0;
  for (let y = rect.y; y < rect.y + rect.height; y++) for (let x = rect.x; x < rect.x + rect.width; x++) {
    const k = (y * width + x) * 4;
    sum += .299 * pixels[k] + .587 * pixels[k + 1] + .114 * pixels[k + 2];
  }
  return sum / (rect.width * rect.height);
};
const histogram = (bin, rect) => {
  const counts = new Map(codes.map(code => [code, 0]));
  for (let y = rect.y; y < rect.y + rect.height; y++) for (let x = rect.x; x < rect.x + rect.width; x++) {
    // Student landscape -> clockwise portrait.
    const code = deviceCode(bin, 479 - y, x);
    assert(counts.has(code), `Unexpected FFUI code ${code}`);
    counts.set(code, counts.get(code) + 1);
  }
  return counts;
};

// Existing operator pixels, avatar Bayer phase, codes and complete BIN stay exact.
const p = blank();
legacyPalette.forEach((c, i) => put(p, 440, 20 + i, 143, c));
put(p, 440, 0, 0, [255,0,0]);
const color = encode(p, 'color', true);
assert.equal(color.length, 133776);
assert.deepEqual([...color.slice(0,16)], [70,70,85,73,2,2,184,1,96,2,0,0,0,0,0,0]);
const offset = 16 + (143 * 440 + 20) / 2;
assert.deepEqual([...color.slice(offset, offset + 3)], [0x01,0x23,0x56]);
assert.equal(color[16] >> 4, 0);
const gray = blank();
fill(gray, 440, { x: 20, y: 143, width: 4, height: 4 }, [128,128,128]);
put(gray, 440, 0, 0, [128,128,128]);
assert.equal(encode(gray, 'grayscale', true).length, 33456);
let black = 0;
for (let y = 143; y < 147; y++) for (let x = 20; x < 24; x++) if (!gray[(y * 440 + x) * 4]) black++;
assert.equal(black, 8); assert.equal(gray[0], 0);
assert.equal(encode(blank(), 'mono', false).length, 33456);
const baselines = {
  mono: ['e17e14ce43e4ff15b381fa8976e7cc75082dcd4f6f08fea9c5e8e7641c19628d', 'e2e792e5bbd7e42e9dbe4a596d9c8871d3f43efe70349270350d1cee679c0e62'],
  grayscale: ['96721ef2d14ee46d2dda03970eed7ac664e8107769f66f02d60cbec72047226e', 'f895a6cb196c82be31992f4f36ff5f9eaa743988984a83777b7c04e57bb70c00'],
  color: ['302a507ce2bf4c6a654601439763802ba0671572c9e7398e59c940aff63d5dfe', '8bb0032e4158937c2429e1eae7246e17cce26254db323ec58574a455aa452e07'],
};
for (const mode of Object.keys(baselines)) {
  const pixels = Uint8ClampedArray.from({ length: 440 * 608 * 4 }, (_, i) => ((i * 73 + (i >>> 9) * 37) >>> 0) % 256);
  const bin = encode(pixels, mode, true);
  const hash = bytes => createHash('sha256').update(bytes).digest('hex');
  assert.deepEqual([hash(bin), hash(pixels)], baselines[mode], `${mode} operator changed`);
}

const student = { width: 720, height: 480, avatarRect: { x: 59, y: 121, width: 184, height: 230 }, ditherArtwork: true, rotateClockwise: true };
for (const mode of ['mono', 'grayscale', 'color']) {
  const pixels = blank(720, 480);
  put(pixels, 720, 0, 0, [0,0,0]);
  put(pixels, 720, 719, 479, [0,0,0]);
  put(pixels, 720, 2, 479, [0,0,0]);
  put(pixels, 720, 719, 3, [0,0,0]);
  put(pixels, 720, 59, 121, mode === 'grayscale' ? [128,128,128] : [255,0,0]);
  put(pixels, 720, 60, 121, [128,128,128]);
  put(pixels, 720, 242, 350, [0,0,255]);
  const bin = encode(pixels, mode, true, student);
  const format = mode === 'color' ? 2 : 1;
  assert.equal(bin.length, mode === 'color' ? 172816 : 43216);
  assert.deepEqual([...bin.slice(0,16)], [70,70,85,73,format,format,224,1,208,2,0,0,0,0,0,0]);
  for (const [x,y] of [[479,0],[0,719],[0,2],[476,719]]) assert.equal(deviceCode(bin,x,y), 0);
  assert.equal(deviceCode(bin, 478, 0), 1);
  assert.equal(deviceCode(bin, 358, 59), mode === 'color' ? 3 : mode === 'grayscale' ? 1 : 0);
  if (mode === 'grayscale') assert.equal(deviceCode(bin, 358, 60), 0);
  assert.equal(deviceCode(bin, 129, 242), mode === 'color' ? 5 : 0);
  for (let y = 0; y < 480; y++) for (let x = 0; x < 720; x++) {
    const code = deviceCode(bin, 479 - y, x), index = codes.indexOf(code), k = (y * 720 + x) * 4;
    assert(index >= 0);
    const rgb = measuredPalette[index];
    if (rgb.some((value, c) => value !== pixels[k+c]) || pixels[k+3] !== 255) {
      assert.fail(`PNG buffer and ${mode} FFUI differ at ${x},${y}`);
    }
  }
}

// Avatar quantization remains byte-for-byte equivalent to operator behavior;
// only the student color preview uses measured RGB for each resulting code.
for (const mode of ['mono', 'grayscale', 'color']) {
  const portrait = blank(), landscape = blank(720,480);
  for (let y = 0; y < 160; y++) for (let x = 0; x < 128; x++) {
    const rgb = [(x*47+y*11)%256,(x*13+y*71)%256,(x*83+y*19)%256];
    put(portrait,440,x+20,y+143,rgb); put(landscape,720,x+59,y+121,rgb);
  }
  const before = encode(portrait,mode,true);
  const after = encode(landscape,mode,true,student);
  for (let y = 0; y < 160; y++) for (let x = 0; x < 128; x++) {
    assert.equal(deviceCode(after,479-y-121,x+59),deviceCode(before,x+20,y+143),`${mode} avatar at ${x},${y}`);
  }
}

// All measured ink fixtures are stable, including their luminance and FFUI codes.
{
  const pixels = blank(720,480);
  const rects = measuredPalette.map((rgb,i) => ({ x: 40+i*100, y: 30, width: 64, height: 64 }));
  rects.forEach((rect,i) => fill(pixels,720,rect,measuredPalette[i]));
  const bin = encode(pixels,'color',false,student);
  rects.forEach((rect,i) => assert.equal(histogram(bin,rect).get(codes[i]),64*64,`Measured ink ${codes[i]} drifted`));
}

// Saturated source primaries keep their ink identity (plus neutral for Y), not
// unrelated hue ink. Cyan must contain BOTH green and blue, not just blue/gray.
{
  const sources = [[255,0,0],[255,255,0],[0,255,0],[0,0,255],[0,255,255],[100,210,220],[180,240,240]];
  const inkCodes = [[3],[2],[6],[5],[5,6],[5,6],[5,6]];
  const pixels = blank(720,480);
  const rects = sources.map((rgb,i) => ({ x: 10+i*100, y: 30, width: 80, height: 96 }));
  rects.forEach((rect,i) => fill(pixels,720,rect,sources[i]));
  const bin = encode(pixels,'color',false,student);
  rects.forEach((rect,i) => {
    const counts = histogram(bin,rect);
    for (const code of [2,3,5,6]) {
      if (inkCodes[i].includes(code)) assert(counts.get(code) > 40, `Missing ${code} in ${sources[i]}`);
      else assert.equal(counts.get(code),0,`Unrelated hue ${code} in ${sources[i]}`);
    }
    assert(Math.abs(meanY(pixels,720,rect)-yOf(sources[i])) < 2,`Primary/cyan Y drift at ${sources[i]}`);
  });
}

// Black text, paper and slightly tinted neutral artwork cannot acquire colored
// specks or error from an adjacent colored region, including at the avatar edge.
for (const mode of ['mono','grayscale','color']) {
  const pixels = blank(720,480);
  const neutral = { x: 100,y: 80,width: 160,height: 64 }, text = { x: 104,y: 90,width: 2,height: 40 };
  fill(pixels,720,{ x: 80,y: 80,width: 20,height: 64 },[0,255,255]);
  fill(pixels,720,neutral,[160,164,168]); fill(pixels,720,text,[0,0,0]);
  const bin = encode(pixels,mode,false,student), counts = histogram(bin,neutral);
  for (const code of [2,3,5,6]) assert.equal(counts.get(code),0);
  assert.equal(histogram(bin,text).get(0),80);
  assert.equal(deviceCode(bin,479-79,100),1,'Paper above artwork remains white');
}

// Neutral ramp: local density/brightness changes continuously over the full
// range, including dark antialiasing, without repeating a fixed 4x4 Bayer tile.
{
  const pixels = blank(720,480), ramp = { x: 40,y: 20,width: 640,height: 128 };
  fill(pixels,720,ramp,x => { const v = Math.round((x-40)*255/639); return [v,v,v]; });
  const original = pixels.slice(), bin = encode(pixels,'color',false,student);
  let previous = -1;
  for (let x = 40; x < 680; x += 20) {
    const window = { x,y: 20,width: 20,height: 128 }, actual = meanY(pixels,720,window);
    assert(Math.abs(actual-meanY(original,720,window)) < 2,'Gray ramp lost average brightness');
    assert(actual > previous,'Gray ramp is not monotonic'); previous = actual;
  }
  const flat = blank(720,480), patch = { x: 100,y: 100,width: 128,height: 128 };
  fill(flat,720,patch,[153,153,153]); encode(flat,'color',false,student);
  let differing = 0;
  for (let y = 104; y < 220; y++) for (let x = 104; x < 220; x++) {
    if (flat[(y*720+x)*4] !== flat[(y*720+x+4)*4]) differing++;
  }
  assert(differing > 1000,'Diffusion regressed to a repeating four-pixel grid');
  assert.equal([...histogram(bin,ramp)].filter(([code,n]) => code > 1 && n).length,0);
}

// Constant-Y gradient crosses the old chroma=24 cutoff, then hue/gamut edges.
// Each local window must retain Y; neighboring windows cannot form a light seam.
{
  const pixels = blank(720,480), gradient = { x: 40,y: 20,width: 640,height: 192 };
  fill(pixels,720,gradient,(x,y) => {
    const t = (x-40)/639, hue = (y-20)/191;
    const dr = -90*t, db = 90*t*(1-2*hue), dg = -(.299*dr+.114*db)/.587;
    return [153+dr,153+dg,153+db];
  });
  const original = pixels.slice(); encode(pixels,'color',false,student);
  for (let y = 20; y < 212; y += 32) for (let x = 40; x < 680; x += 16) {
    const window = { x,y,width: 16,height: 32 };
    const drift = Math.abs(meanY(pixels,720,window)-meanY(original,720,window));
    assert(drift < 5,`Gradient luminance seam at ${x},${y}: ${drift}`);
  }
}

// Actual Amiya seam crossings, averaged/rounded from the installed fullscreen
// source (left x=0..39,y=148..164; top y=0..19,x=519..540). Keep this regression
// self-contained: no PNG decoder, Pillow or browser dependency in pixel tests.
{
  const left = [[157,148,178],[158,147,176],[158,147,176],[159,146,175],[160,146,174],[161,146,173],[162,146,173],[163,145,171],[164,145,171],[165,145,170],[166,144,168],[166,144,168],[167,143,167],[169,143,166],[169,142,165],[170,142,164],[171,142,163]];
  const top = [[181,150,167],[181,151,168],[180,151,169],[179,151,170],[178,152,172],[177,152,172],[176,153,173],[175,154,175],[174,154,176],[173,154,177],[172,155,178],[171,156,179],[170,156,180],[168,156,181],[167,156,182],[167,157,184],[165,157,185],[164,158,186],[163,158,186],[162,158,188],[161,158,189],[160,159,189]];
  const pixels = blank(720,480);
  fill(pixels,720,{ x:0,y:148,width:40,height:left.length },(x,y) => left[y-148]);
  fill(pixels,720,{ x:519,y:0,width:top.length,height:20 },x => top[x-519]);
  const original = pixels.slice(); encode(pixels,'color',false,student);
  const windows = [
    ...Array.from({length:10},(_,i) => ({ x:0,y:148+i,width:40,height:8 })),
    ...Array.from({length:15},(_,i) => ({ x:519+i,y:0,width:8,height:20 })),
  ];
  for (const window of windows) {
    const drift = Math.abs(meanY(pixels,720,window)-meanY(original,720,window));
    assert(drift < 5,`Amiya seam crossing lost Y at ${window.x},${window.y}: ${drift}`);
  }
}

// Walk all hue sectors at constant Y, including the red/blue wrap. Averaging
// small windows must show a continuous transition, not hard ink switches.
{
  const pixels = blank(720,480), ramp = { x:40,y:20,width:640,height:128 };
  fill(pixels,720,ramp,x => {
    const angle = (x-40)/639*2*Math.PI, dr = 70*Math.cos(angle), db = 70*Math.sin(angle);
    return [170+dr,170-(.299*dr+.114*db)/.587,170+db];
  });
  encode(pixels,'color',false,student);
  let previous;
  for (let x = 48; x < 664; x += 4) {
    const average = [0,0,0];
    for (let y = 20; y < 148; y++) for (let xx = x; xx < x+16; xx++) {
      for (let c = 0; c < 3; c++) average[c] += pixels[(y*720+xx)*4+c]/2048;
    }
    if (previous) for (let c = 0; c < 3; c++) {
      assert(Math.abs(average[c]-previous[c]) < 12,`Hard hue switch at ${x}, channel ${c}`);
    }
    previous = average;
  }
}

// Only the exact operator and fullscreen student layouts are accepted. The old
// unused 608x440 rotated card layout no longer masquerades as fullscreen data.
assert.throws(() => encode(blank(608,440),'color',false,{...student,width:608,height:440}));
assert.throws(() => encode(blank(720,480),'color',false,{...student,rotateClockwise:false}));
assert.throws(() => encode(blank(720,480).subarray(4),'color',false,student));
assert.throws(() => encode(blank(),'invalid',false));
console.log('Passport pixels passed: exact operator/avatar baselines; fullscreen rotation/PNG-BIN equality; measured inks, cyan, neutral/text, gradation and seam regressions');
