// 8bpp 索引色量化（ColorFast 55 色 / ColorQual 46 色 / 实测校色）
// 移植自 tools/ForFilm/js/convert.js，输出 v2 film 主体与文件头
const FILM_HEADER_SIZE = 32;

const SRGB_TO_LINEAR_LUT = new Float32Array(256);
for (let i = 0; i < 256; i++) {
  const v = i / 255;
  SRGB_TO_LINEAR_LUT[i] = v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}

function channelToLinear(c) {
  c = c / 255;
  return c > 0.04045 ? Math.pow((c + 0.055) / 1.055, 2.4) : c / 12.92;
}

function clampU8(value) {
  return value < 0 ? 0 : (value > 255 ? 255 : value);
}

function rgbToLab(r, g, b) {
  const rl = channelToLinear(r) * 100;
  const gl = channelToLinear(g) * 100;
  const bl = channelToLinear(b) * 100;
  const x = (rl * 0.4124564 + gl * 0.3575761 + bl * 0.1804375) / 95.047;
  const y = (rl * 0.2126729 + gl * 0.7151522 + bl * 0.0721750) / 100;
  const z = (rl * 0.0193339 + gl * 0.1191920 + bl * 0.9503041) / 108.883;
  const fx = x > 0.008856 ? Math.cbrt(x) : 7.787 * x + 16 / 116;
  const fy = y > 0.008856 ? Math.cbrt(y) : 7.787 * y + 16 / 116;
  const fz = z > 0.008856 ? Math.cbrt(z) : 7.787 * z + 16 / 116;
  return { l: 116 * fy - 16, a: 500 * (fx - fy), b: 200 * (fy - fz) };
}

const PALETTE_FAST = [
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [30, 25, 47],
  [62, 37, 56], [108, 62, 72], [117, 60, 49], [172, 97, 58],
  [41, 41, 79], [58, 62, 91], [69, 70, 100], [78, 78, 102],
  [121, 114, 122], [150, 131, 127], [171, 145, 120], [198, 170, 130],
  [45, 56, 112], [110, 114, 139], [163, 164, 169], [197, 197, 187],
  [0, 0, 0], [255, 255, 255], [255, 255, 255], [255, 255, 255],
  [34, 34, 84], [37, 37, 87], [38, 38, 92], [40, 39, 96],
  [66, 63, 110], [107, 86, 117], [133, 108, 112], [182, 146, 132],
  [21, 60, 135], [32, 84, 144], [38, 90, 147], [42, 95, 149],
  [70, 124, 158], [105, 147, 163], [129, 162, 153], [173, 184, 152],
  [63, 24, 29], [90, 24, 28], [94, 23, 27], [97, 25, 29],
  [128, 30, 31], [149, 37, 35], [157, 45, 43], [180, 71, 32],
  [38, 55, 83], [65, 101, 101], [81, 117, 105], [94, 127, 106],
  [123, 149, 112], [145, 162, 110], [169, 170, 100], [194, 187, 96],
  [62, 55, 73], [145, 132, 77], [187, 167, 70], [211, 185, 62],
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [219, 198, 57]
];

const PALETTE_QUAL = [
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [31, 25, 43],
  [46, 33, 55], [64, 46, 66], [84, 51, 55], [163, 91, 58],
  [46, 97, 188], [131, 170, 206], [191, 211, 221], [215, 227, 227],
  [255, 255, 255], [255, 255, 255], [255, 255, 255], [255, 255, 255],
  [27, 52, 146], [25, 69, 170], [27, 73, 178], [30, 81, 182],
  [66, 118, 191], [105, 146, 196], [133, 166, 190], [181, 196, 193],
  [24, 69, 145], [34, 91, 151], [39, 97, 152], [45, 106, 155],
  [72, 131, 163], [100, 151, 166], [128, 169, 152], [170, 191, 143],
  [103, 25, 30], [130, 19, 27], [133, 19, 27], [136, 21, 30],
  [157, 21, 28], [170, 24, 33], [175, 24, 30], [196, 46, 33],
  [55, 34, 61], [67, 37, 64], [72, 39, 66], [76, 42, 69],
  [115, 57, 79], [148, 73, 90], [166, 88, 96], [197, 136, 124],
  [28, 67, 69], [54, 103, 63], [72, 121, 66], [87, 133, 67],
  [108, 145, 55], [124, 154, 46], [154, 169, 34], [188, 187, 19],
  [58, 70, 78], [151, 155, 48], [205, 185, 15], [240, 211, 7],
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [243, 214, 7]
];

const PALETTE_QUAL_MEASURED = [
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [69, 73, 81],
  [73, 76, 84], [80, 81, 85], [84, 81, 80], [105, 94, 82],
  [72, 112, 147], [119, 147, 163], [148, 165, 171], [162, 173, 174],
  [255, 255, 255], [255, 255, 255], [255, 255, 255], [171, 179, 171],
  [65, 85, 114], [67, 94, 126], [67, 97, 131], [67, 97, 131],
  [66, 118, 191], [101, 126, 147], [133, 166, 190], [181, 196, 193],
  [59, 98, 123], [67, 109, 124], [67, 111, 130], [67, 111, 130],
  [74, 118, 135], [86, 126, 134], [104, 137, 131], [124, 148, 128],
  [106, 70, 74], [124, 70, 73], [128, 70, 73], [128, 70, 73],
  [138, 76, 77], [143, 80, 78], [147, 82, 78], [156, 93, 78],
  [80, 75, 86], [86, 78, 88], [86, 78, 88], [86, 78, 88],
  [105, 86, 94], [120, 92, 98], [131, 101, 102], [147, 118, 112],
  [67, 98, 87], [88, 116, 81], [98, 122, 80], [105, 127, 79],
  [112, 131, 79], [117, 135, 78], [130, 143, 76], [145, 151, 74],
  [88, 93, 87], [139, 138, 75], [162, 152, 71], [180, 165, 67],
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [180, 165, 67]
];

const PALETTE_QUAL_CALIBRATED = [
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [52, 58, 68],
  [58, 61, 71], [66, 68, 73], [71, 68, 66], [96, 83, 69],
  [56, 104, 141], [111, 141, 158], [142, 160, 166], [157, 168, 169],
  [252, 252, 252], [252, 252, 252], [252, 252, 252], [166, 174, 166],
  [46, 73, 106], [49, 83, 119], [49, 87, 124], [49, 87, 124],
  [48, 110, 187], [91, 119, 141], [126, 161, 186], [177, 192, 189],
  [37, 88, 116], [49, 100, 117], [49, 102, 123], [49, 102, 123],
  [59, 110, 128], [74, 119, 127], [95, 131, 124], [117, 142, 121],
  [97, 54, 59], [117, 54, 58], [121, 54, 58], [121, 54, 58],
  [132, 61, 63], [137, 66, 64], [141, 69, 64], [151, 82, 64],
  [66, 60, 74], [74, 64, 76], [74, 64, 76], [74, 64, 76],
  [96, 74, 83], [112, 81, 88], [124, 91, 92], [141, 110, 104],
  [49, 88, 75], [76, 108, 68], [88, 114, 66], [96, 120, 65],
  [104, 124, 65], [109, 128, 64], [123, 137, 61], [139, 145, 59],
  [76, 82, 75], [133, 132, 60], [157, 146, 55], [175, 160, 49],
  [0, 0, 0], [0, 0, 0], [0, 0, 0], [175, 160, 49]
];

const QUAL_CANDIDATES = [
  3, 4, 5, 6, 7, 8, 9, 10, 11,
  15, 16, 17, 18, 19, 21,
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 63
];

const FAST_CANDIDATES = [
  0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
  20, 21, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
  42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 63
];

const PROFILES = {
  colorFast55: {
    format: 0x03,
    palette: PALETTE_FAST,
    candidateIndexes: FAST_CANDIDATES,
    measured: false
  },
  colorQual: {
    format: 0x02,
    palette: PALETTE_QUAL,
    candidateIndexes: QUAL_CANDIDATES,
    measured: false
  },
  colorQualMeasured: {
    format: 0x02,
    palette: PALETTE_QUAL_MEASURED,
    candidateIndexes: QUAL_CANDIDATES,
    measured: true
  },
  colorQualCalibrated: {
    format: 0x02,
    palette: PALETTE_QUAL_CALIBRATED,
    candidateIndexes: QUAL_CANDIDATES,
    measured: true
  }
};

Object.keys(PROFILES).forEach(function (key) {
  const profile = PROFILES[key];
  profile.candidateLabs = profile.candidateIndexes.map(function (index) {
    const c = profile.palette[index];
    return rgbToLab(c[0], c[1], c[2]);
  });
});

function closestIndex(r, g, b, profile) {
  const labs = profile.candidateLabs;
  const indexes = profile.candidateIndexes;
  const rl = SRGB_TO_LINEAR_LUT[r] * 100;
  const gl = SRGB_TO_LINEAR_LUT[g] * 100;
  const bl = SRGB_TO_LINEAR_LUT[b] * 100;

  const x = (rl * 0.4124564 + gl * 0.3575761 + bl * 0.1804375) / 95.047;
  const y = (rl * 0.2126729 + gl * 0.7151522 + bl * 0.0721750) / 100;
  const z = (rl * 0.0193339 + gl * 0.1191920 + bl * 0.9503041) / 108.883;

  const fx = x > 0.008856 ? Math.cbrt(x) : 7.787 * x + 16 / 116;
  const fy = y > 0.008856 ? Math.cbrt(y) : 7.787 * y + 16 / 116;
  const fz = z > 0.008856 ? Math.cbrt(z) : 7.787 * z + 16 / 116;

  const labL = 116 * fy - 16;
  const labA = 500 * (fx - fy);
  const labB = 200 * (fy - fz);

  let best = 0;
  let bestDist = Infinity;
  for (let i = 0; i < labs.length; i++) {
    const pl = labs[i];
    const dl = labL - pl.l;
    const da = labA - pl.a;
    const db = labB - pl.b;
    const dist = profile.measured
      ? (1.2 * dl) * (1.2 * dl) + da * da + db * db
      : 2.0 * dl * dl + da * da + db * db;
    if (dist < bestDist) {
      bestDist = dist;
      best = i;
    }
  }
  return indexes[best];
}

function atkinsonBase(imageData, selectIndex, residualOf, previewOf, strength) {
  const width = imageData.width;
  const height = imageData.height;
  const data = imageData.data;
  const stride = (width + 3) * 3;
  const spread = strength === undefined ? 1 : strength;
  let currentErrors = new Int32Array(stride);
  let nextErrors = new Int32Array(stride);
  let secondErrors = new Int32Array(stride);
  const codes = new Uint8Array(width * height);

  function slot(x) {
    return (x + 1) * 3;
  }

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const eo = slot(x);
      const so = (y * width + x) * 4;
      const r = clampU8(data[so] + ((currentErrors[eo] + 4) >> 3));
      const g = clampU8(data[so + 1] + ((currentErrors[eo + 1] + 4) >> 3));
      const b = clampU8(data[so + 2] + ((currentErrors[eo + 2] + 4) >> 3));

      const lab = rgbToLab(r, g, b);
      const index = selectIndex(r, g, b, lab);
      codes[y * width + x] = index;

      const rp = residualOf(index);
      const errR = (r - rp[0]) * spread;
      const errG = (g - rp[1]) * spread;
      const errB = (b - rp[2]) * spread;

      let n = slot(x + 1);
      currentErrors[n] += errR; currentErrors[n + 1] += errG; currentErrors[n + 2] += errB;
      n = slot(x + 2);
      currentErrors[n] += errR; currentErrors[n + 1] += errG; currentErrors[n + 2] += errB;
      n = slot(x - 1);
      nextErrors[n] += errR; nextErrors[n + 1] += errG; nextErrors[n + 2] += errB;
      n = slot(x);
      nextErrors[n] += errR; nextErrors[n + 1] += errG; nextErrors[n + 2] += errB;
      n = slot(x + 1);
      nextErrors[n] += errR; nextErrors[n + 1] += errG; nextErrors[n + 2] += errB;
      n = slot(x);
      secondErrors[n] += errR; secondErrors[n + 1] += errG; secondErrors[n + 2] += errB;
    }
    const temp = currentErrors;
    currentErrors = nextErrors;
    nextErrors = secondErrors;
    secondErrors = temp;
    secondErrors.fill(0);
  }

  const out = { width: width, height: height, data: new Uint8ClampedArray(width * height * 4) };
  const outData = out.data;
  for (let i = 0; i < codes.length; i++) {
    const color = previewOf ? previewOf(codes[i]) : [0, 0, 0];
    outData[i * 4] = color[0];
    outData[i * 4 + 1] = color[1];
    outData[i * 4 + 2] = color[2];
    outData[i * 4 + 3] = 255;
  }
  return out;
}

function measuredDiffuse(work, original, offset, errR, errG, errB, factor) {
  for (let c = 0; c < 3; c++) {
    const delta = c === 0 ? errR : (c === 1 ? errG : errB);
    work[offset + c] = Math.max(original[offset + c] - 64,
      Math.min(original[offset + c] + 64, work[offset + c] + delta * factor));
  }
}

function measuredDither(imageData, strength, profile) {
  const data = imageData.data;
  const original = new Uint8Array(data);
  const work = new Float64Array(data.length);
  for (let i = 0; i < data.length; i += 4) {
    work[i] = original[i];
    work[i + 1] = original[i + 1];
    work[i + 2] = original[i + 2];
  }
  const width = imageData.width;
  const height = imageData.height;
  const stride = width * 4;

  for (let y = 0; y < height; y++) {
    const reverse = (y & 1) !== 0;
    for (let step = 0; step < width; step++) {
      const x = reverse ? width - 1 - step : step;
      const offset = y * stride + x * 4;
      const r = Math.round(Math.max(0, Math.min(255, work[offset])));
      const g = Math.round(Math.max(0, Math.min(255, work[offset + 1])));
      const b = Math.round(Math.max(0, Math.min(255, work[offset + 2])));
      const index = closestIndex(r, g, b, profile);
      const color = profile.palette[index];
      const errR = (r - color[0]) * strength;
      const errG = (g - color[1]) * strength;
      const errB = (b - color[2]) * strength;
      data[offset] = color[0];
      data[offset + 1] = color[1];
      data[offset + 2] = color[2];

      const forward = reverse ? -1 : 1;
      if (x + forward >= 0 && x + forward < width) {
        measuredDiffuse(work, original, offset + forward * 4, errR, errG, errB, 7 / 16);
      }
      if (y + 1 < height) {
        const down = offset + stride;
        if (x - forward >= 0 && x - forward < width) {
          measuredDiffuse(work, original, down - forward * 4, errR, errG, errB, 3 / 16);
        }
        measuredDiffuse(work, original, down, errR, errG, errB, 5 / 16);
        if (x + forward >= 0 && x + forward < width) {
          measuredDiffuse(work, original, down + forward * 4, errR, errG, errB, 1 / 16);
        }
      }
    }
  }
  return imageData;
}

function quantize(imageData, strength, useDither, profile) {
  if (useDither) {
    if (profile.measured) return measuredDither(imageData, strength, profile);
    return atkinsonBase(
      imageData,
      function (r, g, b) {
        return closestIndex(r, g, b, profile);
      },
      function (index) {
        return profile.palette[index];
      },
      function (index) {
        return profile.palette[index];
      },
      strength
    );
  }

  const data = imageData.data;
  const palette = profile.palette;
  for (let i = 0; i < data.length; i += 4) {
    const color = palette[closestIndex(data[i], data[i + 1], data[i + 2], profile)];
    data[i] = color[0];
    data[i + 1] = color[1];
    data[i + 2] = color[2];
  }
  return imageData;
}

function layoutIndex(layout, x, y, width, height) {
  if (layout === 'rotated') return (x * height) + (height - 1 - y);
  if (layout === 'rotated-180') return (height - 1 - y) * width + (width - 1 - x);
  return y * width + x;
}

function exactIndex(profile, r, g, b) {
  const palette = profile.palette;
  for (let i = 0; i < palette.length; i++) {
    const c = palette[i];
    if (c[0] === r && c[1] === g && c[2] === b) return i;
  }
  return closestIndex(r, g, b, profile);
}

function pack(imageData, profile, layout) {
  const width = imageData.width;
  const height = imageData.height;
  const data = imageData.data;
  const out = new Uint8Array(width * height);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4;
      out[layoutIndex(layout, x, y, width, height)] = exactIndex(profile, data[o], data[o + 1], data[o + 2]);
    }
  }
  return out;
}

function header(profile, width, height) {
  const bodySize = width * height;
  const out = new Uint8Array(FILM_HEADER_SIZE);
  out[0] = bodySize & 0xFF;
  out[1] = (bodySize >> 8) & 0xFF;
  out[2] = (bodySize >> 16) & 0xFF;
  out[3] = (bodySize >> 24) & 0xFF;
  out[4] = width & 0xFF;
  out[5] = (width >> 8) & 0xFF;
  out[6] = height & 0xFF;
  out[7] = (height >> 8) & 0xFF;
  out[9] = profile.format;
  return out;
}

function decode(pixelData, width, height, profile, layout) {
  const out = { width: width, height: height, data: new Uint8ClampedArray(width * height * 4) };
  const data = out.data;
  const palette = profile.palette;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const color = palette[pixelData[layoutIndex(layout, x, y, width, height)]] || palette[0];
      const o = (y * width + x) * 4;
      data[o] = color[0];
      data[o + 1] = color[1];
      data[o + 2] = color[2];
      data[o + 3] = 255;
    }
  }
  return out;
}

module.exports = {
  PROFILES,
  quantize,
  pack,
  header,
  decode,
  layoutIndex
};
