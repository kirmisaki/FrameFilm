// Film 格式常量
const CANVAS_WIDTH = 600;
const CANVAS_HEIGHT = 400;
const FILM_SCREEN_WIDTH = 600;
const FILM_SCREEN_HEIGHT = 400;
const FILM_HEADER_SIZE = 32;
const FILM_PIXEL_DATA_SIZE = (FILM_SCREEN_WIDTH * FILM_SCREEN_HEIGHT) / 2;
const FILM_FILE_TOTAL_SIZE = FILM_HEADER_SIZE + FILM_PIXEL_DATA_SIZE;

// 颜色编码索引
const COLOR_CODE_BLACK = 0x00;
const COLOR_CODE_WHITE = 0x01;
const COLOR_CODE_YELLOW = 0x02;
const COLOR_CODE_RED = 0x03;
const COLOR_CODE_BLUE = 0x04;
const COLOR_CODE_GREEN = 0x05;

// 六色调色板
const rgbPalette = [
  { name: "黑色", r: 0, g: 0, b: 0, value: 0x00, code: COLOR_CODE_BLACK },
  { name: "白色", r: 255, g: 255, b: 255, value: 0xff, code: COLOR_CODE_WHITE },
  { name: "黄色", r: 255, g: 255, b: 0, value: 0xfc, code: COLOR_CODE_YELLOW },
  { name: "红色", r: 255, g: 0, b: 0, value: 0xe0, code: COLOR_CODE_RED },
  { name: "蓝色", r: 0, g: 0, b: 255, value: 0x03, code: COLOR_CODE_BLUE },
  { name: "绿色", r: 41, g: 204, b: 20, value: 0x1c, code: COLOR_CODE_GREEN }
];

function rgbToHsl(r, g, b) {
  r /= 255; g /= 255; b /= 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  let h = 0, s = 0, l = (max + min) / 2;
  if (max !== min) {
    const d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    switch (max) {
      case r: h = ((g - b) / d + (g < b ? 6 : 0)) / 6; break;
      case g: h = ((b - r) / d + 2) / 6; break;
      case b: h = ((r - g) / d + 4) / 6; break;
    }
  }
  return { h: h * 360, s: s, l: l };
}

function rgbToLab(r, g, b) {
  r = r / 255; g = g / 255; b = b / 255;
  r = r > 0.04045 ? Math.pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
  g = g > 0.04045 ? Math.pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
  b = b > 0.04045 ? Math.pow((b + 0.055) / 1.055, 2.4) : b / 12.92;
  r *= 100; g *= 100; b *= 100;
  let x = r * 0.4124 + g * 0.3576 + b * 0.1805;
  let y = r * 0.2126 + g * 0.7152 + b * 0.0722;
  let z = r * 0.0193 + g * 0.1192 + b * 0.9505;
  x /= 95.047; y /= 100.0; z /= 108.883;
  x = x > 0.008856 ? Math.pow(x, 1 / 3) : (7.787 * x) + (16 / 116);
  y = y > 0.008856 ? Math.pow(y, 1 / 3) : (7.787 * y) + (16 / 116);
  z = z > 0.008856 ? Math.pow(z, 1 / 3) : (7.787 * z) + (16 / 116);
  return { l: (116 * y) - 16, a: 500 * (x - y), b: 200 * (y - z) };
}

function labDistance(lab1, lab2) {
  const dl = lab1.l - lab2.l, da = lab1.a - lab2.a, db = lab1.b - lab2.b;
  return Math.sqrt(dl * dl + da * da + db * db);
}

const paletteHsl = rgbPalette.map(function (c) {
  return { color: c, hsl: rgbToHsl(c.r, c.g, c.b) };
});

function findClosestColor(r, g, b) {
  const input = rgbToHsl(r, g, b);
  if (input.s < 0.12) {
    return input.l > 0.5 ? rgbPalette[1] : rgbPalette[0];
  }
  let minDist = Infinity;
  let closestColor = rgbPalette[0];
  for (let i = 2; i < paletteHsl.length; i++) {
    const p = paletteHsl[i];
    let hueDiff = Math.abs(input.h - p.hsl.h);
    if (hueDiff > 180) hueDiff = 360 - hueDiff;
    const satDiff = Math.abs(input.s - p.hsl.s);
    const lumDiff = Math.abs(input.l - p.hsl.l);
    const dist = hueDiff + satDiff * 120 + lumDiff * 80;
    if (dist < minDist) {
      minDist = dist;
      closestColor = p.color;
    }
  }
  const labInput = rgbToLab(r, g, b);
  const labBlack = rgbToLab(0, 0, 0);
  const labWhite = rgbToLab(255, 255, 255);
  const distBlack = labDistance(labInput, labBlack);
  const distWhite = labDistance(labInput, labWhite);
  const distNeutral = Math.min(distBlack, distWhite);
  const neutralColor = distBlack < distWhite ? rgbPalette[0] : rgbPalette[1];
  const labChosen = rgbToLab(closestColor.r, closestColor.g, closestColor.b);
  const distChosen = labDistance(labInput, labChosen);
  if (distNeutral < distChosen * 0.45) {
    return neutralColor;
  }
  return closestColor;
}

function adjustContrast(imageData, factor) {
  const data = imageData.data;
  for (let i = 0; i < data.length; i += 4) {
    data[i] = Math.min(255, Math.max(0, (data[i] - 128) * factor + 128));
    data[i + 1] = Math.min(255, Math.max(0, (data[i + 1] - 128) * factor + 128));
    data[i + 2] = Math.min(255, Math.max(0, (data[i + 2] - 128) * factor + 128));
  }
  return imageData;
}

function floydSteinbergDither(imageData, strength) {
  const width = imageData.width, height = imageData.height;
  const data = imageData.data;
  const tempData = new Uint8ClampedArray(data);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4;
      const closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
      const errR = (tempData[idx] - closest.r) * strength;
      const errG = (tempData[idx + 1] - closest.g) * strength;
      const errB = (tempData[idx + 2] - closest.b) * strength;
      if (x + 1 < width) {
        const ri = idx + 4;
        tempData[ri] = Math.min(255, Math.max(0, tempData[ri] + errR * 7 / 16));
        tempData[ri + 1] = Math.min(255, Math.max(0, tempData[ri + 1] + errG * 7 / 16));
        tempData[ri + 2] = Math.min(255, Math.max(0, tempData[ri + 2] + errB * 7 / 16));
      }
      if (y + 1 < height) {
        if (x > 0) {
          const di = idx + width * 4 - 4;
          tempData[di] = Math.min(255, Math.max(0, tempData[di] + errR * 3 / 16));
          tempData[di + 1] = Math.min(255, Math.max(0, tempData[di + 1] + errG * 3 / 16));
          tempData[di + 2] = Math.min(255, Math.max(0, tempData[di + 2] + errB * 3 / 16));
        }
        const di = idx + width * 4;
        tempData[di] = Math.min(255, Math.max(0, tempData[di] + errR * 5 / 16));
        tempData[di + 1] = Math.min(255, Math.max(0, tempData[di + 1] + errG * 5 / 16));
        tempData[di + 2] = Math.min(255, Math.max(0, tempData[di + 2] + errB * 5 / 16));
        if (x + 1 < width) {
          const di2 = idx + width * 4 + 4;
          tempData[di2] = Math.min(255, Math.max(0, tempData[di2] + errR * 1 / 16));
          tempData[di2 + 1] = Math.min(255, Math.max(0, tempData[di2 + 1] + errG * 1 / 16));
          tempData[di2 + 2] = Math.min(255, Math.max(0, tempData[di2 + 2] + errB * 1 / 16));
        }
      }
    }
  }
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4;
      const closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
      data[idx] = closest.r; data[idx + 1] = closest.g; data[idx + 2] = closest.b;
    }
  }
  return imageData;
}

function atkinsonDither(imageData, strength) {
  const width = imageData.width, height = imageData.height;
  const data = imageData.data;
  const tempData = new Uint8ClampedArray(data);
  const fraction = 1 / 8;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4;
      const closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
      data[idx] = closest.r; data[idx + 1] = closest.g; data[idx + 2] = closest.b;
      const errR = (tempData[idx] - closest.r) * strength;
      const errG = (tempData[idx + 1] - closest.g) * strength;
      const errB = (tempData[idx + 2] - closest.b) * strength;
      const offsets = [
        [1, 0], [2, 0], [-1, 1], [0, 1], [1, 1], [0, 2]
      ];
      for (const [dx, dy] of offsets) {
        const nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < width && ny < height) {
          const ni = (ny * width + nx) * 4;
          tempData[ni] = Math.min(255, Math.max(0, tempData[ni] + errR * fraction));
          tempData[ni + 1] = Math.min(255, Math.max(0, tempData[ni + 1] + errG * fraction));
          tempData[ni + 2] = Math.min(255, Math.max(0, tempData[ni + 2] + errB * fraction));
        }
      }
    }
  }
  return imageData;
}

function stuckiDither(imageData, strength) {
  const width = imageData.width, height = imageData.height;
  const data = imageData.data;
  const tempData = new Uint8ClampedArray(data);
  const divisor = 42;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4;
      const closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
      const errR = (tempData[idx] - closest.r) * strength;
      const errG = (tempData[idx + 1] - closest.g) * strength;
      const errB = (tempData[idx + 2] - closest.b) * strength;
      const offsets = [
        [1, 0, 8], [2, 0, 4],
        [-2, 1, 2], [-1, 1, 4], [0, 1, 8], [1, 1, 4], [2, 1, 2],
        [-2, 2, 1], [-1, 2, 2], [0, 2, 4], [1, 2, 2], [2, 2, 1]
      ];
      for (const [dx, dy, w] of offsets) {
        const nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < width && ny < height) {
          const ni = (ny * width + nx) * 4;
          tempData[ni] = Math.min(255, Math.max(0, tempData[ni] + errR * w / divisor));
          tempData[ni + 1] = Math.min(255, Math.max(0, tempData[ni + 1] + errG * w / divisor));
          tempData[ni + 2] = Math.min(255, Math.max(0, tempData[ni + 2] + errB * w / divisor));
        }
      }
    }
  }
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4;
      const closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
      data[idx] = closest.r; data[idx + 1] = closest.g; data[idx + 2] = closest.b;
    }
  }
  return imageData;
}

function jarvisDither(imageData, strength) {
  const width = imageData.width, height = imageData.height;
  const data = imageData.data;
  const tempData = new Uint8ClampedArray(data);
  const divisor = 48;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const idx = (y * width + x) * 4;
      const closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
      data[idx] = closest.r; data[idx + 1] = closest.g; data[idx + 2] = closest.b;
      const errR = (tempData[idx] - closest.r) * strength;
      const errG = (tempData[idx + 1] - closest.g) * strength;
      const errB = (tempData[idx + 2] - closest.b) * strength;
      const offsets = [
        [1, 0, 7], [2, 0, 5],
        [-2, 1, 3], [-1, 1, 5], [0, 1, 7], [1, 1, 5], [2, 1, 3],
        [-2, 2, 1], [-1, 2, 3], [0, 2, 5], [1, 2, 3], [2, 2, 1]
      ];
      for (const [dx, dy, w] of offsets) {
        const nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < width && ny < height) {
          const ni = (ny * width + nx) * 4;
          tempData[ni] = Math.min(255, Math.max(0, tempData[ni] + errR * w / divisor));
          tempData[ni + 1] = Math.min(255, Math.max(0, tempData[ni + 1] + errG * w / divisor));
          tempData[ni + 2] = Math.min(255, Math.max(0, tempData[ni + 2] + errB * w / divisor));
        }
      }
    }
  }
  return imageData;
}

function applyDitherByType(imageData, type, strength) {
  switch (type) {
    case 'floydSteinberg': return floydSteinbergDither(imageData, strength);
    case 'atkinson': return atkinsonDither(imageData, strength);
    case 'stucki': return stuckiDither(imageData, strength);
    case 'jarvis': return jarvisDither(imageData, strength);
    default: return imageData;
  }
}

function processImageData(imageData) {
  const width = imageData.width, height = imageData.height;
  const data = imageData.data;
  const processedData = new Uint8Array(FILM_PIXEL_DATA_SIZE);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const index = (y * width + x) * 4;
      const closest = findClosestColor(data[index], data[index + 1], data[index + 2]);
      const code = closest.code;
      const newIndex = (x * height) + (height - 1 - y);
      const byteIndex = Math.floor(newIndex / 2);
      if (newIndex % 2 === 0) {
        processedData[byteIndex] = (code << 4) | (processedData[byteIndex] & 0x0F);
      } else {
        processedData[byteIndex] = (processedData[byteIndex] & 0xF0) | code;
      }
    }
  }
  return processedData;
}

function decodeProcessedData(processedData, width, height) {
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const newIndex = (x * height) + (height - 1 - y);
      const byteIndex = Math.floor(newIndex / 2);
      const byte = processedData[byteIndex];
      const code = (newIndex % 2 === 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
      const color = rgbPalette.find(c => c.code === code) || rgbPalette[1];
      const index = (y * width + x) * 4;
      pixels[index] = color.r;
      pixels[index + 1] = color.g;
      pixels[index + 2] = color.b;
      pixels[index + 3] = 255;
    }
  }
  return { data: pixels, width, height };
}

function generateFilmHeader() {
  const header = new Uint8Array(FILM_HEADER_SIZE);
  const fileSize = FILM_PIXEL_DATA_SIZE;
  header[0] = fileSize & 0xFF;
  header[1] = (fileSize >> 8) & 0xFF;
  header[2] = (fileSize >> 16) & 0xFF;
  header[3] = (fileSize >> 24) & 0xFF;
  header[4] = FILM_SCREEN_WIDTH & 0xFF;
  header[5] = (FILM_SCREEN_WIDTH >> 8) & 0xFF;
  header[6] = FILM_SCREEN_HEIGHT & 0xFF;
  header[7] = (FILM_SCREEN_HEIGHT >> 8) & 0xFF;
  header[8] = 6;
  header[16] = 0x00;
  header[17] = 0xFF;
  header[18] = 0xFC;
  header[19] = 0xE0;
  header[20] = 0x03;
  header[21] = 0x1C;
  return header;
}

function wrapText(ctx, text, fontSize, maxWidth) {
  var lines = [];
  var paragraphs = text.split('\n');
  for (var p = 0; p < paragraphs.length; p++) {
    var currentLine = '';
    var chars = Array.from(paragraphs[p]);
    if (chars.length === 0) { lines.push(''); continue; }
    for (var i = 0; i < chars.length; i++) {
      var char = chars[i];
      var testLine = currentLine + char;
      var metrics = ctx.measureText(testLine);
      if (metrics.width > maxWidth && currentLine.length > 0) {
        lines.push(currentLine);
        currentLine = char;
      } else {
        currentLine = testLine;
      }
    }
    if (currentLine.length > 0) lines.push(currentLine);
  }
  return lines;
}

module.exports = {
  CANVAS_WIDTH, CANVAS_HEIGHT,
  FILM_SCREEN_WIDTH, FILM_SCREEN_HEIGHT,
  FILM_HEADER_SIZE, FILM_PIXEL_DATA_SIZE, FILM_FILE_TOTAL_SIZE,
  rgbPalette,
  findClosestColor,
  adjustContrast,
  floydSteinbergDither, atkinsonDither, stuckiDither, jarvisDither,
  applyDitherByType,
  processImageData, decodeProcessedData,
  generateFilmHeader,
  wrapText
};
