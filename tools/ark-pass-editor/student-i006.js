/* SPDX-License-Identifier: GPL-3.0-or-later */
(function (root) {
  'use strict';
  const WIDTH = 720, HEIGHT = 480, COUNT = WIDTH * HEIGHT;
  const WHITE = 75, NEUTRAL_GRAY = 407;
  const DEFAULT_AVATAR = { x: 41, y: 111, width: 239, height: 299 };

  const RED_FADE_X = 414, RED_FADE_WIDTH = 76;
  // RGB samples identify the aligned Amiya stripe in the bundled PSD export.
  const RED_FADE_SAMPLES = [[414, 0xf6bfb7], [453, 0xdf7163], [489, 0xd19595]];
  // I6RFW101.film, reader-order crop (415, 0, 78, 10). A–S index the ABC9 codes.
  const W_CODES = [22, 32, 33, 34, 46, 47, 87, 97, 110, 111, 173, 174, 238, 295, 303, 366, 430, 487, 495];
  const W_ROWS = [
    'JOEONORORERROOERORERRRRSOROERRORPOEOSORORERSOOSSOSSSSESOOSOOOOOQQOKIKOKMKKQKLM',
    'GOHNNREROOOOEROEROREOORSERSSORSOESRREROSOOOOSROESOSSOOSOOEOOOSKIKOQQKQIKMIMMIA',
    'GNOOFOOERRERSROROEOBESORRORORESRSROSOSOERSSESROSOSOCSSOSOOSOSOOQOQIOIOMIQKIKIA',
    'JFHORORSOEOROEORERROORESOSEROSROEOSORESSOEOROSOCSSEOOCOOOOOEOOQIQIKOQOKIMIMQMM',
    'GRHRNNORROREOREORORERORORPSREOEOSREREROSPORSOESOEOSSSOSOEQOOOOKOOQQQKQMKQMKIIM',
    'GOOOFQROOROSROSREOEOREOESOOERERROESOOSROOSOSROSDSOOOESOOOOOOSOQQIOOIIOKMIKMIKM',
    'JOHNOOESRERRORRORERREORRREROROSESORESOESSOSEOSSOSSCSSOKOSOSOSOOOKOQQOOIIKIKKMM',
    'JNONNRRORSOOSRORORORORSOESSRSERORRESRPSOREOOSPOSOSOSOEOOOOOKOOQIQQOKQQKMQIMLIA',
    'GSONFORSORRREROEREOSDREROROESROROEOEOOSSOSSSSSOSSESSSSOSOSOSOSOQOOOKLOKLMIKKIM',
    'JRHDORREROROOERCORREOOSERESOROSESRRRSEDSESDSOESRSSSESOOSEOEOEOKKOQQQOKQLKLQLMM',
  ];

  function applyRedFade(native, base) {
    const matches = RED_FADE_SAMPLES.every(([x, expected]) => {
      const i = x * 4;
      return ((base[i] << 16) | (base[i + 1] << 8) | base[i + 2]) === expected;
    });
    if (!matches) return;
    // Match the narrower PSD layer with nearest-neighbor ABC9 sampling.
    for (let y = 0; y < W_ROWS.length; y++) {
      const row = W_ROWS[y];
      for (let x = 0; x < RED_FADE_WIDTH; x++) {
        const sourceX = Math.floor((x + 0.5) * row.length / RED_FADE_WIDTH);
        native[COUNT - 1 - (y * WIDTH + RED_FADE_X + x)] = W_CODES[row.charCodeAt(sourceX) - 65];
      }
    }
  }

  function validate(image) {
    if (!image || image.width !== WIDTH || image.height !== HEIGHT || !image.data || image.data.length !== COUNT * 4) {
      throw new Error('I006 学生证需要 720×480 RGBA 像素。');
    }
  }

  // Inputs and returned preview use reader-order landscape coordinates. The
  // native ABC9 film is itself profile.bin, not an FFUI wrapper. avatarRect is
  // optional reader-order geometry; always protecting it is safe without an avatar.
  async function render(composed, base, options = {}) {
    validate(composed); validate(base);
    const avatar = options.avatarRect || DEFAULT_AVATAR;
    if (![avatar.x, avatar.y, avatar.width, avatar.height].every(Number.isFinite) || avatar.width < 0 || avatar.height < 0) {
      throw new Error('I006 头像区域无效。');
    }
    const check = () => {
      if (options.cancelled && options.cancelled()) throw new Error('预览已取消');
    };
    const pause = async () => { check(); await new Promise(resolve => setTimeout(resolve, 0)); check(); };
    check();
    const ark = root.Ark512;
    if (!ark || !ark.render || !ark.pack || !ark.preview) throw new Error('I006 配色资料未加载');

    // Same geometry as render-i006-pass.cjs: reader -> clockwise portrait,
    // render in logical 480×720 serpentine order, then clockwise to native.
    const logical = new Uint8ClampedArray(COUNT * 4);
    const luma = new Uint8Array(COUNT);
    for (let y = 0; y < HEIGHT; y++) {
      for (let x = 0; x < WIDTH; x++) {
        const i = y * WIDTH + x, k = i * 4, dst = (x * HEIGHT + HEIGHT - 1 - y) * 4;
        logical.set(composed.data.subarray(k, k + 4), dst);
        // Pillow RGB.convert('L') integer rounding, used by the reviewed trial.
        luma[i] = (19595 * base.data[k] + 38470 * base.data[k + 1] + 7471 * base.data[k + 2] + 32768) >> 16;
      }
      if (y % 16 === 15) await pause();
    }
    const rendered = await ark.render({ data: logical, width: HEIGHT, height: WIDTH }, {
      continuousTone: true, cancelled: options.cancelled,
    });
    check();
    const native = new Uint16Array(COUNT);
    for (let y = 0; y < WIDTH; y++) {
      for (let x = 0; x < HEIGHT; x++) native[x * WIDTH + WIDTH - 1 - y] = rendered.codes[y * HEIGHT + x];
      if (y % 16 === 15) await pause();
    }

    const mask = new Uint8Array(COUNT), coverage = new Float64Array(COUNT);
    for (let y = 130; y < HEIGHT; y++) {
      for (let x = 385; x < WIDTH; x++) {
        const i = y * WIDTH + x, k = i * 4, value = luma[i];
        if (value < 232 || value >= 255 || (x >= avatar.x && x < avatar.x + avatar.width && y >= avatar.y && y < avatar.y + avatar.height)) continue;
        const r = base.data[k], g = base.data[k + 1], b = base.data[k + 2];
        if (Math.max(r, g, b) - Math.min(r, g, b) > 3 ||
            r !== composed.data[k] || g !== composed.data[k + 1] || b !== composed.data[k + 2] || base.data[k + 3] !== composed.data[k + 3]) continue;
        // MinFilter(5), with replicated image edges, protects fine label/logo
        // edges even where the composed pixel itself still equals the base.
        let margin = true;
        for (let dy = -2; dy <= 2 && margin; dy++) {
          const row = Math.max(0, Math.min(HEIGHT - 1, y + dy)) * WIDTH;
          for (let dx = -2; dx <= 2; dx++) {
            if (luma[row + Math.max(0, Math.min(WIDTH - 1, x + dx))] < 225) { margin = false; break; }
          }
        }
        if (margin) {
          mask[i] = 1;
          coverage[i] = Math.min((255 - value) / 11, 1) * .35;
        }
      }
      if (y % 16 === 15) await pause();
    }

    function diffuse(x, y, error, weight) {
      if (x >= 0 && x < WIDTH && y < HEIGHT && mask[y * WIDTH + x]) coverage[y * WIDTH + x] += error * weight / 16;
    }
    // A watermark: masked serpentine FS in reader order, with no diffusion or
    // code changes outside the mask. Only calibrated white/neutral gray states.
    for (let y = 0; y < HEIGHT; y++) {
      const direction = y % 2 === 0 ? 1 : -1;
      for (let x = direction === 1 ? 0 : WIDTH - 1; x >= 0 && x < WIDTH; x += direction) {
        const i = y * WIDTH + x;
        if (!mask[i]) continue;
        const value = Math.min(1, Math.max(0, coverage[i])), gray = value >= .5;
        native[COUNT - 1 - i] = gray ? NEUTRAL_GRAY : WHITE;
        const error = value - Number(gray);
        diffuse(x + direction, y, error, 7);
        diffuse(x - direction, y + 1, error, 3);
        diffuse(x, y + 1, error, 5);
        diffuse(x + direction, y + 1, error, 1);
      }
      if (y % 16 === 15) await pause();
    }
    check();
    applyRedFade(native, base.data);
    const bin = ark.pack(native, WIDTH, HEIGHT), physicalPreview = ark.preview(native, WIDTH, HEIGHT);
    const rgba = new Uint8ClampedArray(COUNT * 4);
    for (let y = 0; y < HEIGHT; y++) {
      for (let x = 0; x < WIDTH; x++) {
        const i = y * WIDTH + x, src = (COUNT - 1 - i) * 4;
        rgba.set(physicalPreview.subarray(src, src + 4), i * 4);
      }
      if (y % 16 === 15) await pause();
    }
    check();
    return { bin, rgba };
  }

  const api = { render };
  root.PassportStudentI006 = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
