// epd-sim.js — 浏览器 EPD 效果模拟器（预览页专用）
// 与小程序模板预览管线 processAndDisplay(canvas, ctx, 'adaptive', 1.0, null) 对齐：
//   竖屏画布 → 旋转提取横屏 → 自适应抖动（选最优算法/强度）→ 6 色最近色量化 → 旋转回竖屏
// 用法：EPDSim.applyTo(ctx, canvas, CW, CH)  — 在 render() 画完设计稿后调用，覆盖为 EPD 效果
// 设计色 → EPD 上屏色 差异即所见即所得，避免「网页好看 / 设备糊」的偏差
(function () {
  'use strict';

  // ============ 6 色安全色板（与 film-utils.js rgbPalette 完全一致） ============
  var PALETTE = [
    { name: '黑色', r: 0, g: 0, b: 0 },
    { name: '白色', r: 255, g: 255, b: 255 },
    { name: '黄色', r: 255, g: 255, b: 0 },
    { name: '红色', r: 255, g: 0, b: 0 },
    { name: '蓝色', r: 0, g: 0, b: 255 },
    { name: '绿色', r: 41, g: 204, b: 20 }
  ];

  function rgbToHsl(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    var max = Math.max(r, g, b), min = Math.min(r, g, b);
    var h = 0, s = 0, l = (max + min) / 2;
    if (max !== min) {
      var d = max - min;
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
    var x = r * 0.4124 + g * 0.3576 + b * 0.1805;
    var y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    var z = r * 0.0193 + g * 0.1192 + b * 0.9505;
    x /= 95.047; y /= 100.0; z /= 108.883;
    x = x > 0.008856 ? Math.pow(x, 1 / 3) : (7.787 * x) + (16 / 116);
    y = y > 0.008856 ? Math.pow(y, 1 / 3) : (7.787 * y) + (16 / 116);
    z = z > 0.008856 ? Math.pow(z, 1 / 3) : (7.787 * z) + (16 / 116);
    return { l: (116 * y) - 16, a: 500 * (x - y), b: 200 * (y - z) };
  }

  function labDistance(lab1, lab2) {
    var dl = lab1.l - lab2.l, da = lab1.a - lab2.a, db = lab1.b - lab2.b;
    return Math.sqrt(dl * dl + da * da + db * db);
  }

  var paletteHsl = PALETTE.map(function (c) {
    return { color: c, hsl: rgbToHsl(c.r, c.g, c.b) };
  });

  function findClosestColor(r, g, b) {
    var input = rgbToHsl(r, g, b);
    if (input.s < 0.12) {
      return input.l > 0.5 ? PALETTE[1] : PALETTE[0];
    }
    var minDist = Infinity, closestColor = PALETTE[0];
    for (var i = 2; i < paletteHsl.length; i++) {
      var p = paletteHsl[i];
      var hueDiff = Math.abs(input.h - p.hsl.h);
      if (hueDiff > 180) hueDiff = 360 - hueDiff;
      var satDiff = Math.abs(input.s - p.hsl.s);
      var lumDiff = Math.abs(input.l - p.hsl.l);
      var dist = hueDiff + satDiff * 120 + lumDiff * 80;
      if (dist < minDist) { minDist = dist; closestColor = p.color; }
    }
    var labInput = rgbToLab(r, g, b);
    var distBlack = labDistance(labInput, rgbToLab(0, 0, 0));
    var distWhite = labDistance(labInput, rgbToLab(255, 255, 255));
    var distNeutral = Math.min(distBlack, distWhite);
    var neutralColor = distBlack < distWhite ? PALETTE[0] : PALETTE[1];
    var distChosen = labDistance(labInput, rgbToLab(closestColor.r, closestColor.g, closestColor.b));
    if (distNeutral < distChosen * 0.45) return neutralColor;
    return closestColor;
  }

  // ============ 抖动算法（与 film-utils.js 一致） ============
  function floydSteinbergDither(imageData, strength) {
    var width = imageData.width, height = imageData.height;
    var data = imageData.data;
    var tempData = new Uint8ClampedArray(data);
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        var idx = (y * width + x) * 4;
        var closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
        var errR = (tempData[idx] - closest.r) * strength;
        var errG = (tempData[idx + 1] - closest.g) * strength;
        var errB = (tempData[idx + 2] - closest.b) * strength;
        if (x + 1 < width) {
          var ri = idx + 4;
          tempData[ri] = Math.min(255, Math.max(0, tempData[ri] + errR * 7 / 16));
          tempData[ri + 1] = Math.min(255, Math.max(0, tempData[ri + 1] + errG * 7 / 16));
          tempData[ri + 2] = Math.min(255, Math.max(0, tempData[ri + 2] + errB * 7 / 16));
        }
        if (y + 1 < height) {
          if (x > 0) {
            var di = idx + width * 4 - 4;
            tempData[di] = Math.min(255, Math.max(0, tempData[di] + errR * 3 / 16));
            tempData[di + 1] = Math.min(255, Math.max(0, tempData[di + 1] + errG * 3 / 16));
            tempData[di + 2] = Math.min(255, Math.max(0, tempData[di + 2] + errB * 3 / 16));
          }
          var di2 = idx + width * 4;
          tempData[di2] = Math.min(255, Math.max(0, tempData[di2] + errR * 5 / 16));
          tempData[di2 + 1] = Math.min(255, Math.max(0, tempData[di2 + 1] + errG * 5 / 16));
          tempData[di2 + 2] = Math.min(255, Math.max(0, tempData[di2 + 2] + errB * 5 / 16));
          if (x + 1 < width) {
            var di3 = idx + width * 4 + 4;
            tempData[di3] = Math.min(255, Math.max(0, tempData[di3] + errR * 1 / 16));
            tempData[di3 + 1] = Math.min(255, Math.max(0, tempData[di3 + 1] + errG * 1 / 16));
            tempData[di3 + 2] = Math.min(255, Math.max(0, tempData[di3 + 2] + errB * 1 / 16));
          }
        }
      }
    }
    for (var y2 = 0; y2 < height; y2++) {
      for (var x2 = 0; x2 < width; x2++) {
        var i2 = (y2 * width + x2) * 4;
        var c2 = findClosestColor(tempData[i2], tempData[i2 + 1], tempData[i2 + 2]);
        data[i2] = c2.r; data[i2 + 1] = c2.g; data[i2 + 2] = c2.b;
      }
    }
    return imageData;
  }

  function atkinsonDither(imageData, strength) {
    var width = imageData.width, height = imageData.height;
    var data = imageData.data;
    var tempData = new Uint8ClampedArray(data);
    var fraction = 1 / 8;
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        var idx = (y * width + x) * 4;
        var closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
        data[idx] = closest.r; data[idx + 1] = closest.g; data[idx + 2] = closest.b;
        var errR = (tempData[idx] - closest.r) * strength;
        var errG = (tempData[idx + 1] - closest.g) * strength;
        var errB = (tempData[idx + 2] - closest.b) * strength;
        var offsets = [[1, 0], [2, 0], [-1, 1], [0, 1], [1, 1], [0, 2]];
        for (var oi = 0; oi < offsets.length; oi++) {
          var dx = offsets[oi][0], dy = offsets[oi][1];
          var nx = x + dx, ny = y + dy;
          if (nx >= 0 && nx < width && ny < height) {
            var ni = (ny * width + nx) * 4;
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
    var width = imageData.width, height = imageData.height;
    var data = imageData.data;
    var tempData = new Uint8ClampedArray(data);
    var divisor = 42;
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        var idx = (y * width + x) * 4;
        var closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
        var errR = (tempData[idx] - closest.r) * strength;
        var errG = (tempData[idx + 1] - closest.g) * strength;
        var errB = (tempData[idx + 2] - closest.b) * strength;
        var offsets = [
          [1, 0, 8], [2, 0, 4],
          [-2, 1, 2], [-1, 1, 4], [0, 1, 8], [1, 1, 4], [2, 1, 2],
          [-2, 2, 1], [-1, 2, 2], [0, 2, 4], [1, 2, 2], [2, 2, 1]
        ];
        for (var oi = 0; oi < offsets.length; oi++) {
          var dx = offsets[oi][0], dy = offsets[oi][1], w = offsets[oi][2];
          var nx = x + dx, ny = y + dy;
          if (nx >= 0 && nx < width && ny < height) {
            var ni = (ny * width + nx) * 4;
            tempData[ni] = Math.min(255, Math.max(0, tempData[ni] + errR * w / divisor));
            tempData[ni + 1] = Math.min(255, Math.max(0, tempData[ni + 1] + errG * w / divisor));
            tempData[ni + 2] = Math.min(255, Math.max(0, tempData[ni + 2] + errB * w / divisor));
          }
        }
      }
    }
    for (var y2 = 0; y2 < height; y2++) {
      for (var x2 = 0; x2 < width; x2++) {
        var i2 = (y2 * width + x2) * 4;
        var c2 = findClosestColor(tempData[i2], tempData[i2 + 1], tempData[i2 + 2]);
        data[i2] = c2.r; data[i2 + 1] = c2.g; data[i2 + 2] = c2.b;
      }
    }
    return imageData;
  }

  function jarvisDither(imageData, strength) {
    var width = imageData.width, height = imageData.height;
    var data = imageData.data;
    var tempData = new Uint8ClampedArray(data);
    var divisor = 48;
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        var idx = (y * width + x) * 4;
        var closest = findClosestColor(tempData[idx], tempData[idx + 1], tempData[idx + 2]);
        data[idx] = closest.r; data[idx + 1] = closest.g; data[idx + 2] = closest.b;
        var errR = (tempData[idx] - closest.r) * strength;
        var errG = (tempData[idx + 1] - closest.g) * strength;
        var errB = (tempData[idx + 2] - closest.b) * strength;
        var offsets = [
          [1, 0, 7], [2, 0, 5],
          [-2, 1, 3], [-1, 1, 5], [0, 1, 7], [1, 1, 5], [2, 1, 3],
          [-2, 2, 1], [-1, 2, 3], [0, 2, 5], [1, 2, 3], [2, 2, 1]
        ];
        for (var oi = 0; oi < offsets.length; oi++) {
          var dx = offsets[oi][0], dy = offsets[oi][1], w = offsets[oi][2];
          var nx = x + dx, ny = y + dy;
          if (nx >= 0 && nx < width && ny < height) {
            var ni = (ny * width + nx) * 4;
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

  // ============ 自适应选优（与 film-utils.js adaptiveDither 一致） ============
  function computeEdgeMap(data, width, height) {
    var edges = new Float32Array(width * height);
    for (var y = 1; y < height - 1; y++) {
      for (var x = 1; x < width - 1; x++) {
        function gray(px, py) {
          return data[(py * width + px) * 4] * 0.299 + data[(py * width + px) * 4 + 1] * 0.587 + data[(py * width + px) * 4 + 2] * 0.114;
        }
        var tl = gray(x - 1, y - 1), tc = gray(x, y - 1), tr = gray(x + 1, y - 1);
        var ml = gray(x - 1, y), mr = gray(x + 1, y);
        var bl = gray(x - 1, y + 1), bc = gray(x, y + 1), br = gray(x + 1, y + 1);
        var gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
        var gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
        edges[y * width + x] = Math.sqrt(gx * gx + gy * gy);
      }
    }
    return edges;
  }

  function analyzeImageAdvanced(imageData) {
    var data = imageData.data;
    var width = imageData.width, height = imageData.height;
    var pixelCount = width * height;
    var brightnessSum = 0, saturationSum = 0;
    for (var i = 0; i < data.length; i += 4) {
      var r = data[i], g = data[i + 1], b = data[i + 2];
      brightnessSum += r * 0.299 + g * 0.587 + b * 0.114;
      var max = Math.max(r, g, b), min = Math.min(r, g, b);
      saturationSum += max > 0 ? (max - min) / max : 0;
    }
    var edges = computeEdgeMap(data, width, height);
    var edgeSum = 0, edgeCount = 0;
    for (var e = 0; e < edges.length; e++) {
      edgeSum += edges[e];
      if (edges[e] > 20) edgeCount++;
    }
    var innerPixels = (width - 2) * (height - 2);
    return {
      brightness: brightnessSum / pixelCount / 255,
      edgeDensity: innerPixels > 0 ? edgeCount / innerPixels : 0,
      saturation: saturationSum / pixelCount
    };
  }

  function downsampleImageData(imageData, tw, th) {
    var srcCanvas = document.createElement('canvas');
    srcCanvas.width = imageData.width; srcCanvas.height = imageData.height;
    var sctx = srcCanvas.getContext('2d');
    sctx.putImageData(imageData, 0, 0);
    var dstCanvas = document.createElement('canvas');
    dstCanvas.width = tw; dstCanvas.height = th;
    var dctx = dstCanvas.getContext('2d');
    dctx.imageSmoothingEnabled = true;
    dctx.drawImage(srcCanvas, 0, 0, tw, th);
    return dctx.getImageData(0, 0, tw, th);
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
    var totalLabError = 0;
    for (var i = 0; i < d1.length; i += 4) {
      var lab1 = rgbToLab(d1[i], d1[i + 1], d1[i + 2]);
      var lab2 = rgbToLab(d2[i], d2[i + 1], d2[i + 2]);
      totalLabError += labDistance(lab1, lab2);
    }
    var avgLabError = totalLabError / n;
    var origEdges = computeEdgeMap(d1, width, height);
    var dithEdges = computeEdgeMap(d2, width, height);
    var edgeCorrelation = 0, origEdgeEnergy = 0, dithEdgeEnergy = 0;
    for (var j = 0; j < origEdges.length; j++) {
      edgeCorrelation += origEdges[j] * dithEdges[j];
      origEdgeEnergy += origEdges[j] * origEdges[j];
      dithEdgeEnergy += dithEdges[j] * dithEdges[j];
    }
    var edgePreservation = origEdgeEnergy > 0 && dithEdgeEnergy > 0
      ? edgeCorrelation / Math.sqrt(origEdgeEnergy * dithEdgeEnergy) : 0;
    var colorMap = {};
    for (var k = 0; k < d2.length; k += 4) {
      var key = (d2[k] << 16) | (d2[k + 1] << 8) | d2[k + 2];
      colorMap[key] = (colorMap[key] || 0) + 1;
    }
    var colorCounts = [];
    for (var key2 in colorMap) colorCounts.push(colorMap[key2]);
    colorCounts.sort(function (a, b) { return b - a; });
    var colorEntropy = 0;
    for (var ci = 0; ci < colorCounts.length; ci++) {
      var p = colorCounts[ci] / n;
      if (p > 0) colorEntropy -= p * Math.log2(p);
    }
    var maxEntropy = Math.log2(Math.min(6, colorCounts.length));
    var colorBalance = maxEntropy > 0 ? colorEntropy / maxEntropy : 0;
    var score = avgLabError * 0.4 + (1 - edgePreservation) * 80 * 0.35 + (1 - colorBalance) * 30 * 0.25;
    return { score: score };
  }

  function adaptiveDither(imageData) {
    var width = imageData.width, height = imageData.height;
    var evalScale = 3;
    var evalW = Math.max(30, Math.floor(width / evalScale));
    var evalH = Math.max(30, Math.floor(height / evalScale));
    var evalData = downsampleImageData(imageData, evalW, evalH);
    var analysis = analyzeImageAdvanced(evalData);
    var candidates = generateAdaptiveCandidates(analysis);
    var bestScore = Infinity, bestConfig = candidates[0];
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

  // ============ 对外接口 ============
  // 从物理画布抽逻辑竖屏 → 旋转横屏 → 自适应抖动 → 旋转回竖屏，返回 pw×ph 的 ImageData
  function simulateFromCanvas(sourceCanvas, pw, ph, sw, sh) {
    var log = document.createElement('canvas');
    log.width = pw; log.height = ph;
    var lctx = log.getContext('2d');
    lctx.imageSmoothingEnabled = true;
    lctx.drawImage(sourceCanvas, 0, 0, sourceCanvas.width, sourceCanvas.height, 0, 0, pw, ph);

    var land = document.createElement('canvas');
    land.width = sw; land.height = sh;
    var l2 = land.getContext('2d');
    l2.save();
    l2.translate(0, sh);
    l2.rotate(-Math.PI / 2);
    l2.drawImage(log, 0, 0, pw, ph, 0, 0, pw, ph);
    l2.restore();

    var dithered = adaptiveDither(l2.getImageData(0, 0, sw, sh));

    var dCanvas = document.createElement('canvas');
    dCanvas.width = sw; dCanvas.height = sh;
    dCanvas.getContext('2d').putImageData(dithered, 0, 0);

    var port = document.createElement('canvas');
    port.width = pw; port.height = ph;
    var pctx = port.getContext('2d');
    pctx.save();
    pctx.translate(pw, 0);
    pctx.rotate(Math.PI / 2);
    pctx.drawImage(dCanvas, 0, 0, sw, sh, 0, 0, sw, sh);
    pctx.restore();
    return pctx.getImageData(0, 0, pw, ph);
  }

  // 在已画好设计稿的 ctx 上叠加 EPD 效果（ctx 已 scale；用 drawImage 回填保持缩放）
  function applyTo(ctx, canvas, pw, ph, sw, sh) {
    var img = simulateFromCanvas(canvas, pw, ph, sw || ph, sh || pw);
    var tmp = document.createElement('canvas');
    tmp.width = pw; tmp.height = ph;
    tmp.getContext('2d').putImageData(img, 0, 0);
    var wasSmooth = ctx.imageSmoothingEnabled;
    ctx.imageSmoothingEnabled = false;
    ctx.clearRect(0, 0, pw, ph);
    ctx.drawImage(tmp, 0, 0, pw, ph, 0, 0, pw, ph);
    ctx.imageSmoothingEnabled = wasSmooth;
  }

  window.EPDSim = { simulateFromCanvas: simulateFromCanvas, applyTo: applyTo };
})();
