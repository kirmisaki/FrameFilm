// Film 格式常量
const FILM_HEADER_SIZE = 32;

// 设备配置表
const DEVICE_CONFIGS = {
  FRAMEFILM: {
    screenWidth: 600, screenHeight: 400,
    canvasWidth: 400, canvasHeight: 600,
    displayName: 'FrameFilm',
    isPortraitPanel: true,   // 物理竖屏面板，需要旋转显示
    pixelLayout: 'rotated'   // 列优先翻转: (x * height) + (height - 1 - y)
  },
  FRAMEFILMPRO: {
    screenWidth: 792, screenHeight: 528,
    canvasWidth: 528, canvasHeight: 792,
    displayName: 'FrameFilm Pro',
    isPortraitPanel: true,     // 物理竖屏面板，需要旋转显示
    pixelLayout: 'row-major'   // 行优先: (y * width) + x，设备端采样方式不同无需旋转
  }
};

var currentDeviceType = 'FRAMEFILM';

function getDeviceConfig() {
  return DEVICE_CONFIGS[currentDeviceType] || DEVICE_CONFIGS['FRAMEFILM'];
}

function setDeviceType(type) {
  if (DEVICE_CONFIGS[type]) {
    currentDeviceType = type;
  }
}

function getDeviceType() {
  return currentDeviceType;
}

// 动态尺寸 getter（向后兼容旧页面引用）
function getCanvasWidth() { return getDeviceConfig().canvasWidth; }
function getCanvasHeight() { return getDeviceConfig().canvasHeight; }
function getScreenWidth() { return getDeviceConfig().screenWidth; }
function getScreenHeight() { return getDeviceConfig().screenHeight; }
function getFilmPixelDataSize() { return (getScreenWidth() * getScreenHeight()) / 2; }
function getFilmFileTotalSize() { return FILM_HEADER_SIZE + getFilmPixelDataSize(); }

// 保留旧常量作为默认值（标准版），供已有页面顶部 var 引用
const CANVAS_WIDTH = 400;
const CANVAS_HEIGHT = 600;
const FILM_SCREEN_WIDTH = 600;
const FILM_SCREEN_HEIGHT = 400;
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

function computeEdgeMap(data, width, height) {
  var edges = new Float32Array(width * height);
  for (var y = 1; y < height - 1; y++) {
    for (var x = 1; x < width - 1; x++) {
      var tl = data[((y-1)*width+x-1)*4]*0.299 + data[((y-1)*width+x-1)*4+1]*0.587 + data[((y-1)*width+x-1)*4+2]*0.114;
      var tc = data[((y-1)*width+x)*4]*0.299 + data[((y-1)*width+x)*4+1]*0.587 + data[((y-1)*width+x)*4+2]*0.114;
      var tr = data[((y-1)*width+x+1)*4]*0.299 + data[((y-1)*width+x+1)*4+1]*0.587 + data[((y-1)*width+x+1)*4+2]*0.114;
      var ml = data[(y*width+x-1)*4]*0.299 + data[(y*width+x-1)*4+1]*0.587 + data[(y*width+x-1)*4+2]*0.114;
      var mr = data[(y*width+x+1)*4]*0.299 + data[(y*width+x+1)*4+1]*0.587 + data[(y*width+x+1)*4+2]*0.114;
      var bl = data[((y+1)*width+x-1)*4]*0.299 + data[((y+1)*width+x-1)*4+1]*0.587 + data[((y+1)*width+x-1)*4+2]*0.114;
      var bc = data[((y+1)*width+x)*4]*0.299 + data[((y+1)*width+x)*4+1]*0.587 + data[((y+1)*width+x)*4+2]*0.114;
      var br = data[((y+1)*width+x+1)*4]*0.299 + data[((y+1)*width+x+1)*4+1]*0.587 + data[((y+1)*width+x+1)*4+2]*0.114;
      var gx = -tl - 2*ml - bl + tr + 2*mr + br;
      var gy = -tl - 2*tc - tr + bl + 2*bc + br;
      edges[y * width + x] = Math.sqrt(gx * gx + gy * gy);
    }
  }
  return edges;
}

function analyzeImageAdvanced(imageData) {
  var data = imageData.data;
  var width = imageData.width, height = imageData.height;
  var pixelCount = width * height;
  var brightnessSum = 0, rSum = 0, gSum = 0, bSum = 0, saturationSum = 0;
  for (var i = 0; i < data.length; i += 4) {
    var r = data[i], g = data[i+1], b = data[i+2];
    rSum += r; gSum += g; bSum += b;
    brightnessSum += r * 0.299 + g * 0.587 + b * 0.114;
    var max = Math.max(r, g, b), min = Math.min(r, g, b);
    saturationSum += max > 0 ? (max - min) / max : 0;
  }
  var edges = computeEdgeMap(data, width, height);
  var edgeSum = 0, edgeCount = 0;
  for (var i = 0; i < edges.length; i++) {
    edgeSum += edges[i];
    if (edges[i] > 20) edgeCount++;
  }
  var innerPixels = (width - 2) * (height - 2);
  return {
    brightness: brightnessSum / pixelCount / 255,
    edgeDensity: innerPixels > 0 ? edgeCount / innerPixels : 0,
    avgGradient: innerPixels > 0 ? edgeSum / innerPixels / 255 : 0,
    saturation: saturationSum / pixelCount
  };
}

function downsampleImageData(imageData, tw, th) {
  var srcCanvas = wx.createOffscreenCanvas({ type: '2d', width: imageData.width, height: imageData.height });
  var srcCtx = srcCanvas.getContext('2d');
  var srcImgData = srcCtx.createImageData(imageData.width, imageData.height);
  srcImgData.data.set(imageData.data);
  srcCtx.putImageData(srcImgData, 0, 0);
  var dstCanvas = wx.createOffscreenCanvas({ type: '2d', width: tw, height: th });
  var dstCtx = dstCanvas.getContext('2d');
  dstCtx.drawImage(srcCanvas, 0, 0, tw, th);
  return dstCtx.getImageData(0, 0, tw, th);
}

function generateAdaptiveCandidates(analysis) {
  var candidates = [];
  var algos = ['floydSteinberg', 'atkinson', 'stucki', 'jarvis'];
  var strengths;
  if (analysis.edgeDensity > 0.2) {
    strengths = [0.6, 0.8, 1.0, 1.2, 1.4, 1.6];
  } else if (analysis.saturation > 0.3) {
    strengths = [0.7, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8];
  } else {
    strengths = [0.6, 0.8, 1.0, 1.2, 1.5, 1.8, 2.0];
  }
  for (var ai = 0; ai < algos.length; ai++) {
    for (var si = 0; si < strengths.length; si++) {
      candidates.push({ type: algos[ai], strength: strengths[si] });
    }
  }
  return candidates;
}

function evaluateDitherResult(original, dithered) {
  var d1 = original.data, d2 = dithered.data;
  var width = original.width, height = original.height;
  var n = d1.length / 4;
  var totalLabError = 0, maxError = 0;
  for (var i = 0; i < d1.length; i += 4) {
    var lab1 = rgbToLab(d1[i], d1[i+1], d1[i+2]);
    var lab2 = rgbToLab(d2[i], d2[i+1], d2[i+2]);
    var dist = labDistance(lab1, lab2);
    totalLabError += dist;
    if (dist > maxError) maxError = dist;
  }
  var avgLabError = totalLabError / n;
  var origEdges = computeEdgeMap(d1, width, height);
  var dithEdges = computeEdgeMap(d2, width, height);
  var edgeCorrelation = 0, origEdgeEnergy = 0, dithEdgeEnergy = 0;
  for (var i = 0; i < origEdges.length; i++) {
    edgeCorrelation += origEdges[i] * dithEdges[i];
    origEdgeEnergy += origEdges[i] * origEdges[i];
    dithEdgeEnergy += dithEdges[i] * dithEdges[i];
  }
  var edgePreservation = origEdgeEnergy > 0 && dithEdgeEnergy > 0 ?
    edgeCorrelation / Math.sqrt(origEdgeEnergy * dithEdgeEnergy) : 0;
  var colorMap = {};
  for (var i = 0; i < d2.length; i += 4) {
    var key = (d2[i] << 16) | (d2[i+1] << 8) | d2[i+2];
    colorMap[key] = (colorMap[key] || 0) + 1;
  }
  var colorCounts = Object.values(colorMap).sort(function(a,b){ return b-a; });
  var colorEntropy = 0;
  for (var i = 0; i < colorCounts.length; i++) {
    var p = colorCounts[i] / n;
    if (p > 0) colorEntropy -= p * Math.log2(p);
  }
  var maxEntropy = Math.log2(Math.min(6, colorCounts.length));
  var colorBalance = maxEntropy > 0 ? colorEntropy / maxEntropy : 0;
  var score = avgLabError * 0.4 + (1 - edgePreservation) * 80 * 0.35 + (1 - colorBalance) * 30 * 0.25;
  return { score: score, avgLabError: avgLabError, edgePreservation: edgePreservation, colorBalance: colorBalance, maxError: maxError };
}

function adaptiveDither(imageData) {
  var width = imageData.width, height = imageData.height;
  var evalScale = 3;
  var evalW = Math.max(30, Math.floor(width / evalScale));
  var evalH = Math.max(30, Math.floor(height / evalScale));
  var evalData = downsampleImageData(imageData, evalW, evalH);
  var analysis = analyzeImageAdvanced(evalData);
  var candidates = generateAdaptiveCandidates(analysis);
  var bestScore = Infinity;
  var bestConfig = candidates[0];
  for (var ci = 0; ci < candidates.length; ci++) {
    var config = candidates[ci];
    var copy = { data: new Uint8ClampedArray(evalData.data), width: evalW, height: evalH };
    applyDitherByType(copy, config.type, config.strength);
    var result = evaluateDitherResult(evalData, copy);
    if (result.score < bestScore) {
      bestScore = result.score;
      bestConfig = config;
    }
  }
  return applyDitherByType(imageData, bestConfig.type, bestConfig.strength);
}

function applyDitherByType(imageData, type, strength) {
  switch (type) {
    case 'adaptive': return adaptiveDither(imageData);
    case 'floydSteinberg': return floydSteinbergDither(imageData, strength);
    case 'atkinson': return atkinsonDither(imageData, strength);
    case 'stucki': return stuckiDither(imageData, strength);
    case 'jarvis': return jarvisDither(imageData, strength);
    default: return imageData;
  }
}

// 从竖屏画布提取横屏数据（标准版90°旋转）或直接提取（Pro版无需旋转）
function extractLandscapeData(portraitCanvas) {
  var cfg = getDeviceConfig();
  var sw = cfg.screenWidth;
  var sh = cfg.screenHeight;
  if (cfg.isPortraitPanel) {
    // 标准版：竖屏面板，需要旋转90°得到横屏数据
    var landscapeCanvas = wx.createOffscreenCanvas({ type: '2d', width: sw, height: sh });
    var ctx = landscapeCanvas.getContext('2d');
    ctx.save();
    ctx.translate(0, sh);
    ctx.rotate(-Math.PI / 2);
    ctx.drawImage(portraitCanvas, 0, 0, cfg.canvasWidth, cfg.canvasHeight, 0, 0, cfg.canvasWidth, cfg.canvasHeight);
    ctx.restore();
    return ctx.getImageData(0, 0, sw, sh);
  } else {
    // Pro版：横屏面板，画布已是横屏方向，直接提取
    return portraitCanvas.getContext('2d').getImageData(0, 0, sw, sh);
  }
}

// 处理图像数据为 Film 格式并回显到画布
function processAndDisplay(portraitCanvas, portraitCtx, ditherType, ditherStrength, contrast) {
  var cfg = getDeviceConfig();
  var sw = cfg.screenWidth;
  var sh = cfg.screenHeight;
  var cw = cfg.canvasWidth;
  var ch = cfg.canvasHeight;

  var landscapeData = extractLandscapeData(portraitCanvas);
  // 与原版 ForFrame 一致：先应用外部对比度，再抖动
  if (contrast && contrast !== 1.0) {
    adjustContrast(landscapeData, contrast);
  }
  if (ditherType === 'adaptive') {
    landscapeData = adaptiveDither(landscapeData);
  } else if (ditherType) {
    landscapeData = applyDitherByType(landscapeData, ditherType, ditherStrength || 1.0);
  }
  var processedData = processImageData(landscapeData);
  var decoded = decodeProcessedData(processedData, sw, sh);

  if (cfg.isPortraitPanel) {
    // 标准版：将横屏结果旋转回竖屏用于显示
    var tempCanvas = wx.createOffscreenCanvas({ type: '2d', width: sw, height: sh });
    var tempCtx = tempCanvas.getContext('2d');
    var imgData = tempCtx.createImageData(sw, sh);
    imgData.data.set(decoded.data);
    tempCtx.putImageData(imgData, 0, 0);
    portraitCtx.clearRect(0, 0, cw, ch);
    portraitCtx.save();
    portraitCtx.translate(cw, 0);
    portraitCtx.rotate(Math.PI / 2);
    portraitCtx.drawImage(tempCanvas, 0, 0, sw, sh, 0, 0, sw, sh);
    portraitCtx.restore();
  } else {
    // Pro版：画布已是横屏，直接回显
    var imgData = portraitCtx.createImageData(sw, sh);
    imgData.data.set(decoded.data);
    portraitCtx.clearRect(0, 0, cw, ch);
    portraitCtx.putImageData(imgData, 0, 0);
  }
  return processedData;
}

function processImageData(imageData) {
  const width = imageData.width, height = imageData.height;
  const data = imageData.data;
  var cfg = getDeviceConfig();
  const processedData = new Uint8Array(getFilmPixelDataSize());
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const index = (y * width + x) * 4;
      const closest = findClosestColor(data[index], data[index + 1], data[index + 2]);
      const code = closest.code;
      var newIndex;
      if (cfg.pixelLayout === 'row-major') {
        // Pro版：行优先，设备端采样方式不同无需旋转
        newIndex = (y * width) + x;
      } else {
        // 标准版：列优先翻转，产生90°旋转
        newIndex = (x * height) + (height - 1 - y);
      }
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
  var cfg = getDeviceConfig();
  const pixels = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      var newIndex;
      if (cfg.pixelLayout === 'row-major') {
        newIndex = (y * width) + x;
      } else {
        newIndex = (x * height) + (height - 1 - y);
      }
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
  var pixelDataSize = getFilmPixelDataSize();
  var sw = getScreenWidth();
  var sh = getScreenHeight();
  header[0] = pixelDataSize & 0xFF;
  header[1] = (pixelDataSize >> 8) & 0xFF;
  header[2] = (pixelDataSize >> 16) & 0xFF;
  header[3] = (pixelDataSize >> 24) & 0xFF;
  header[4] = sw & 0xFF;
  header[5] = (sw >> 8) & 0xFF;
  header[6] = sh & 0xFF;
  header[7] = (sh >> 8) & 0xFF;
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
  DEVICE_CONFIGS,
  setDeviceType, getDeviceType, getDeviceConfig,
  getCanvasWidth, getCanvasHeight, getScreenWidth, getScreenHeight,
  getFilmPixelDataSize, getFilmFileTotalSize,
  rgbPalette,
  findClosestColor,
  adjustContrast,
  floydSteinbergDither, atkinsonDither, stuckiDither, jarvisDither,
  applyDitherByType,
  processImageData, decodeProcessedData,
  extractLandscapeData,
  processAndDisplay,
  generateFilmHeader,
  wrapText
};
