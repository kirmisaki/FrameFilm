/* SPDX-License-Identifier: GPL-3.0-or-later */
(function (root) {
  'use strict';
  const palette = [[0, 0, 0], [255, 255, 255], [255, 255, 0], [255, 0, 0], [0, 0, 255], [41, 204, 20]];
  const codes = [0, 1, 2, 3, 5, 6];
  // Student preview/metrics only: ui_display.c native_to_fast indexes
  // 0/21/63/47/32/52 in ForFilm's CF_PALETTE_FAST. This RGB fixture is
  // shared with the SDL preview, not a prediction of every physical panel.
  const studentPalette = [[0, 0, 0], [255, 255, 255], [219, 198, 57], [180, 71, 32], [21, 60, 135], [123, 149, 112]];
  const inkOrder = [3, 2, 5, 4, 3]; // red -> yellow -> green -> blue -> red
  const nominalAngles = [0, Math.PI / 2, 3 * Math.PI / 4, 3 * Math.PI / 2];
  const inkSegments = inkOrder.slice(0, -1).map((a, i) => {
    const b = inkOrder[i + 1], ca = studentPalette[a], cb = studentPalette[b];
    const ax = ca[0] - ca[1], ay = ca[1] - ca[2];
    const bx = cb[0] - cb[1], by = cb[1] - cb[2];
    const angle = (Math.atan2(ay, ax) + 2 * Math.PI) % (2 * Math.PI);
    return { a, b, ax, ay, bx, by, det: ax * by - ay * bx, angle,
      start: Math.min(angle, nominalAngles[i]), end: Math.max(angle, nominalAngles[i]) };
  });
  // Unit chroma for the display's yellow, red, blue and green inks.
  const artworkHues = [[1, 1, 0], [1, 0, 0], [0, 0, 1], [0, 1, 0]];
  const bayer = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
  const deviceWidth = 440, deviceHeight = 608, headerSize = 16;
  const operatorAvatar = { x: 20, y: 143, width: 128, height: 160 };

  function contains(rect, x, y) {
    return rect && x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
  }

  function thresholdAt(x, y) {
    return (bayer[(y & 3) * 4 + (x & 3)] + .5) * 255 / 16;
  }

  function artworkIndex(r, g, b, luminance, threshold, color) {
    const low = Math.min(r, g, b), high = Math.max(r, g, b), chroma = high - low;
    // Preserve solid dark text/ink as well as faint gray artwork.
    if (!color || chroma <= 24) return luminance < 80 || luminance < threshold ? 0 : 1;
    if (threshold < low) return 1;
    if (threshold >= high) return 0;
    // Match hue independently of brightness, so a pale blue does not turn green.
    const rr = (r - low) / chroma, gg = (g - low) / chroma, bb = (b - low) / chroma;
    let index = 2, distance = Infinity;
    artworkHues.forEach(([hr, hg, hb], p) => {
      const d = (rr - hr) ** 2 + (gg - hg) ** 2 + (bb - hb) ** 2;
      if (d < distance) { distance = d; index = p + 2; }
    });
    return index;
  }

  function quantize(rgba, mode, width, avatarRect, ditherArtwork) {
    const indices = new Uint8Array(deviceWidth * deviceHeight);
    for (let i = 0; i < indices.length; i++) {
      const k = i * 4, x = i % width, y = Math.floor(i / width);
      const inside = contains(avatarRect, x, y);
      const r = rgba[k], g = rgba[k + 1], b = rgba[k + 2];
      const luminance = .299 * r + .587 * g + .114 * b;
      let index = luminance < 160 ? 0 : 1;
      if (ditherArtwork && !inside) {
        index = artworkIndex(r, g, b, luminance, thresholdAt(x, y), mode === 'color');
      } else if (inside && mode === 'color') {
        let distance = Infinity;
        palette.forEach((rgb, p) => {
          const dr = r - rgb[0], dg = g - rgb[1], db = b - rgb[2];
          const d = .299 * dr * dr + .587 * dg * dg + .114 * db * db;
          if (d < distance) { distance = d; index = p; }
        });
      } else if (inside && mode === 'grayscale') {
        index = luminance < thresholdAt(x - avatarRect.x, y - avatarRect.y) ? 0 : 1;
      }
      indices[i] = index;
      rgba[k] = palette[index][0]; rgba[k + 1] = palette[index][1]; rgba[k + 2] = palette[index][2]; rgba[k + 3] = 255;
    }
    return indices;
  }

  // Continuous hue/chroma mapping into the measured six-ink gamut. A small
  // hue plateau at each ink keeps both nominal saturated primaries and the
  // measured fixture on that ink; interpolate the gaps instead of hard hue
  // classification. Opponent coordinates remove the neutral component.
  function studentBlend(r, g, b) {
    const weights = [0, 0, 0, 0, 0, 0];
    const magnitude = Math.hypot(r - g, g - b);
    let angle = (Math.atan2(g - b, r - g) + 2 * Math.PI) % (2 * Math.PI);
    for (let i = 0; i < inkSegments.length; i++) {
      const segment = inkSegments[i], next = inkSegments[(i + 1) % inkSegments.length];
      const nextStart = i === 3 ? 2 * Math.PI : next.start;
      if (angle > nextStart) continue;
      if (angle <= segment.end) angle = segment.angle;
      else {
        const nextAngle = next.angle + (i === 3 ? 2 * Math.PI : 0);
        angle = segment.angle + (angle - segment.end) / (nextStart - segment.end) * (nextAngle - segment.angle);
      }
      break;
    }
    const vx = Math.cos(angle) * magnitude, vy = Math.sin(angle) * magnitude;
    const luminance = .299 * r + .587 * g + .114 * b;
    const chroma = Math.max(r, g, b) - Math.min(r, g, b);
    const chromaScale = Math.min(1, (chroma - 12) / 24); // soft neutral protection, 12..36
    for (const segment of inkSegments) {
      const wa = (vx * segment.by - vy * segment.bx) / segment.det;
      const wb = (segment.ax * vy - segment.ay * vx) / segment.det;
      if (wa < -1e-9 || wb < -1e-9) continue;
      const total = Math.max(0, wa) + Math.max(0, wb);
      const fraction = Math.max(0, wa) / total;
      const ca = studentPalette[segment.a], cb = studentPalette[segment.b];
      const mix = ca.map((value, c) => fraction * value + (1 - fraction) * cb[c]);
      const mixY = .299 * mix[0] + .587 * mix[1] + .114 * mix[2];
      // Preserve source Y, reducing only chroma until black/white weights fit.
      // This prevents light seams when a gradient crosses a hue/gamut edge.
      const ink = Math.min(total * chromaScale, 1, luminance / mixY, (255 - luminance) / (255 - mixY));
      weights[1] = Math.max(0, (luminance - ink * mixY) / 255);
      weights[0] = Math.max(0, 1 - ink - weights[1]);
      weights[segment.a] = ink * fraction;
      weights[segment.b] = ink * (1 - fraction);
      return weights;
    }
    return weights; // All non-neutral RGB hues are enclosed by inkSegments.
  }

  function quantizeStudent(rgba, mode, width, height, avatarRect, ditherArtwork) {
    const indices = new Uint8Array(width * height);
    const classes = new Uint8Array(indices.length);
    for (let i = 0; i < indices.length; i++) {
      const k = i * 4, r = rgba[k], g = rgba[k + 1], b = rgba[k + 2];
      const neutral = mode !== 'color' || Math.max(r, g, b) - Math.min(r, g, b) <= 12;
      const luminance = .299 * r + .587 * g + .114 * b;
      // Do not leak diffusion across avatar edges or into solid text/paper.
      classes[i] = contains(avatarRect, i % width, Math.floor(i / width)) ? 3 :
        !ditherArtwork || (neutral && (luminance < 1 || luminance >= 254)) ? 0 : neutral ? 1 : 2;
    }
    let current = new Float64Array((width + 2) * 3), next = new Float64Array(current.length);
    function diffuse(x, y, row, type, er, eg, eb, factor) {
      if (x < 0 || x >= width || y >= height || classes[y * width + x] !== type) return;
      const k = (x + 1) * 3;
      row[k] += er * factor; row[k + 1] += eg * factor; row[k + 2] += eb * factor;
    }
    for (let y = 0; y < height; y++) {
      const direction = (y & 1) ? -1 : 1;
      for (let step = 0; step < width; step++) {
        const x = direction === 1 ? step : width - 1 - step;
        const i = y * width + x, k = i * 4, e = (x + 1) * 3, type = classes[i];
        const r = rgba[k], g = rgba[k + 1], b = rgba[k + 2];
        const luminance = .299 * r + .587 * g + .114 * b;
        let index = luminance < 160 ? 0 : 1;
        if (type === 3) {
          // Preserve avatar classification/Bayer behavior; only its student
          // RGB preview changes to the same fixture as the packed ink codes.
          if (mode === 'color') {
            let distance = Infinity;
            palette.forEach((rgb, p) => {
              const d = .299 * (r - rgb[0]) ** 2 + .587 * (g - rgb[1]) ** 2 + .114 * (b - rgb[2]) ** 2;
              if (d < distance) { distance = d; index = p; }
            });
          } else if (mode === 'grayscale') {
            index = luminance < thresholdAt(x - avatarRect.x, y - avatarRect.y) ? 0 : 1;
          }
        } else if (type !== 0) {
          const weights = type === 1 ? [1 - luminance / 255, luminance / 255, 0, 0, 0, 0] : studentBlend(r, g, b);
          const target = [0, 0, 0];
          for (let p = 0; p < weights.length; p++) {
            for (let c = 0; c < 3; c++) target[c] += weights[p] * studentPalette[p][c];
          }
          const rr = target[0] + current[e], gg = target[1] + current[e + 1], bb = target[2] + current[e + 2];
          let distance = Infinity, candidates = 0;
          studentPalette.forEach((rgb, p) => {
            if (weights[p] <= 1e-9) return;
            candidates++;
            const d = .299 * (rr - rgb[0]) ** 2 + .587 * (gg - rgb[1]) ** 2 + .114 * (bb - rgb[2]) ** 2;
            if (d < distance) { distance = d; index = p; }
          });
          // Reuse ForFilm's serpentine Floyd-Steinberg 7/3/5/1 kernel, in
          // floating-point measured RGB. Restrict candidates to the blend's
          // inks so neutral text cannot acquire color and cyan cannot turn gray.
          if (candidates > 1) {
            const rgb = studentPalette[index], er = rr - rgb[0], eg = gg - rgb[1], eb = bb - rgb[2];
            diffuse(x + direction, y, current, type, er, eg, eb, 7 / 16);
            diffuse(x - direction, y + 1, next, type, er, eg, eb, 3 / 16);
            diffuse(x, y + 1, next, type, er, eg, eb, 5 / 16);
            diffuse(x + direction, y + 1, next, type, er, eg, eb, 1 / 16);
          }
        }
        indices[i] = index;
        rgba[k] = studentPalette[index][0]; rgba[k + 1] = studentPalette[index][1]; rgba[k + 2] = studentPalette[index][2]; rgba[k + 3] = 255;
      }
      const previous = current; current = next; next = previous; next.fill(0);
    }
    return indices;
  }

  function pack(indices, color, width, height, rotateClockwise) {
    const format = color ? 2 : 1;
    const outputWidth = rotateClockwise ? height : width, outputHeight = rotateClockwise ? width : height;
    const bin = new Uint8Array(headerSize + indices.length / (color ? 2 : 8));
    bin.set([70, 70, 85, 73, format, format, outputWidth & 255, outputWidth >> 8, outputHeight & 255, outputHeight >> 8]);
    for (let i = 0; i < indices.length; i++) {
      const index = indices[i];
      // Fullscreen student 720×480 (x,y) -> 480×720 (479-y,x).
      const dst = rotateClockwise ? (i % width) * outputWidth + outputWidth - 1 - Math.floor(i / width) : i;
      if (color) bin[headerSize + (dst >> 1)] |= codes[index] << ((dst & 1) ? 0 : 4);
      else if (index === 0) bin[headerSize + (dst >> 3)] |= 0x80 >> (dst & 7);
    }
    return bin;
  }

  function encode(rgba, mode, hasAvatar, {
    width = deviceWidth, height = deviceHeight, avatarRect = operatorAvatar, ditherArtwork = false, rotateClockwise = false,
  } = {}) {
    const student = width === 720 && height === 480 && rotateClockwise;
    const operator = width === deviceWidth && height === deviceHeight && !rotateClockwise;
    if ((!student && !operator) || rgba.length !== width * height * 4 || !['mono', 'grayscale', 'color'].includes(mode)) {
      throw new Error('通行证像素或头像模式无效。');
    }
    const rect = hasAvatar ? avatarRect : null;
    const indices = student ? quantizeStudent(rgba, mode, width, height, rect, ditherArtwork) :
      quantize(rgba, mode, width, rect, ditherArtwork);
    return pack(indices, mode === 'color', width, height, rotateClockwise);
  }
  // New outputs never use the legacy six-ink encoder above. Both previews
  // and bytes below come from the existing Film/Ark quantizers.
  function colorFast(image) {
    const input = { width: image.width, height: image.height, data: image.data.slice() };
    const profile = root.CF_PROFILES?.colorFast55;
    if (!profile || !root.cfQuantize) throw new Error('ColorFast 配色资料未加载');
    const result = root.cfQuantize(input, 1, true, profile);
    const codes = new Uint8Array(image.width * image.height);
    for (let i = 0; i < codes.length; i++) {
      const k = i * 4;
      codes[i] = root.cfClosestIndex(result.data[k], result.data[k + 1], result.data[k + 2], profile);
      result.data.set([...profile.palette[codes[i]], 255], k);
    }
    return { codes, rgba: result.data };
  }

  async function avatar(image, output, options = {}) {
    if (!['i006', 'colorfast'].includes(output)) throw new Error('输出模式无效');
    if (options.cancelled?.()) throw new Error('预览已取消');
    const result = output === 'i006'
      ? await root.Ark512.render(image, { continuousTone: true, cancelled: options.cancelled }) : colorFast(image);
    if (options.cancelled?.()) throw new Error('预览已取消');
    return result;
  }

  function studentColorFast(image) {
    if (image.width !== 720 || image.height !== 480 || image.data.length !== 720 * 480 * 4) throw new Error('学生证需要 720×480 RGBA');
    // Reader landscape -> logical portrait (same sampling order as I006),
    // then logical -> native. Consequently native is 180° from reader.
    const logical = { width: 480, height: 720, data: new Uint8ClampedArray(image.data.length) };
    for (let y = 0; y < 480; y++) for (let x = 0; x < 720; x++) {
      logical.data.set(image.data.subarray((y * 720 + x) * 4, (y * 720 + x) * 4 + 4), (x * 480 + 479 - y) * 4);
    }
    const result = colorFast(logical), bin = new Uint8Array(32 + 345600), view = new DataView(bin.buffer);
    view.setUint32(0, 345600, true); view.setUint16(4, 720, true); view.setUint16(6, 480, true);
    bin[9] = 3; view.setUint16(10, 1, true);
    const rgba = new Uint8ClampedArray(image.data.length);
    for (let y = 0; y < 720; y++) for (let x = 0; x < 480; x++) {
      const src = y * 480 + x, native = x * 720 + 719 - y, reader = 345599 - native;
      bin[32 + native] = result.codes[src];
      rgba.set(result.rgba.subarray(src * 4, src * 4 + 4), reader * 4);
    }
    return { bin, rgba };
  }

  async function compact(image, output, hasAvatar, options = {}) {
    if (image.width !== 440 || image.height !== 608 || image.data.length !== 440 * 608 * 4 || !['i006', 'colorfast'].includes(output)) throw new Error('紧凑档案像素或输出模式无效');
    const bin = new Uint8Array(16 + 33440 + (hasAvatar ? 128 * 160 * (output === 'i006' ? 2 : 1) : 0));
    const view = new DataView(bin.buffer), rgba = new Uint8ClampedArray(image.data.length);
    bin.set([70, 70, 85, 73, 3, output === 'i006' ? 127 : 3]);
    view.setUint16(6, 440, true); view.setUint16(8, 608, true); bin[10] = Number(!!hasAvatar);
    const chrome = output === 'i006' ? root.Ark512.preview(new Uint16Array([387, 75]), 2, 1) : new Uint8ClampedArray([0, 0, 0, 255, 255, 255, 255, 255]);
    for (let i = 0; i < 440 * 608; i++) {
      const k = i * 4, black = .299 * image.data[k] + .587 * image.data[k + 1] + .114 * image.data[k + 2] < 160;
      if (black) bin[16 + (i >> 3)] |= 128 >> (i & 7);
      rgba.set(chrome.subarray(black ? 0 : 4, black ? 4 : 8), k);
    }
    if (hasAvatar) {
      const crop = { width: 128, height: 160, data: new Uint8ClampedArray(128 * 160 * 4) };
      for (let y = 0; y < 160; y++) {
        const src = ((143 + y) * 440 + 20) * 4;
        crop.data.set(image.data.subarray(src, src + 128 * 4), y * 128 * 4);
      }
      const result = await avatar(crop, output, options);
      for (let i = 0; i < result.codes.length; i++) {
        const code = result.codes[i];
        if (!Number.isInteger(code) || code < 0 || code > (output === 'i006' ? 511 : 63)) throw new Error('头像索引无效');
        if (output === 'i006') view.setUint16(33456 + i * 2, code, true);
        else bin[33456 + i] = code;
      }
      for (let y = 0; y < 160; y++) rgba.set(result.rgba.subarray(y * 128 * 4, (y + 1) * 128 * 4), ((143 + y) * 440 + 20) * 4);
    }
    if (options.cancelled?.()) throw new Error('预览已取消');
    return { bin, rgba };
  }
  const api = { encode, avatar, compact, studentColorFast };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PassportPixels = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
