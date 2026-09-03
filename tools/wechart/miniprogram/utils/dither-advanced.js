// dither-advanced.js — 高级抖动算法（微信小程序版）
// 与 tools/ForFilm/js/convert.js 同源实现保持一致：
//   gammaFloydSteinberg / bayer / Atkinson 增强（AE）/ Atkinson·SZ 校色 / SZ 增强
//
// 约定：
//  - 输入输出均为 ImageData 兼容对象 { width, height, data }（data 为 RGBA）。
//  - 小程序无全局 ImageData 构造器，统一用 _makeImageDataLike 生成兼容对象。
//  - AE 依赖 atkinson_enhanced_lut.js（约 1.4MB base64，惰性解码，仅蓝/青区选色用）；
//    解码失败时自动回退到 CIELAB 加权最近色，与 Web 行为一致。
//  - gammaFS/bayer 复用 film-utils.findClosestColor（懒加载 require，避免模块循环）。
var _filmUtils = null;
function _fcc() {
  if (!_filmUtils) _filmUtils = require('./film-utils');
  return _filmUtils.findClosestColor;
}

// ===================== 兼容 ImageData 构造 =====================
function _makeImageDataLike(w, h) {
  return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) };
}

// ===================== sRGB <-> 线性（gamma 感知 FS 用） =====================
var SRGB_TO_LINEAR_LUT = new Float32Array(256);
var LINEAR_TO_SRGB_LUT = new Uint8Array(256);
(function () {
  for (var i = 0; i < 256; i++) {
    var v = i / 255;
    SRGB_TO_LINEAR_LUT[i] = v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
  }
  for (var j = 0; j < 256; j++) {
    var v2 = j / 255;
    var srgb = v2 <= 0.0031308 ? v2 * 12.92 : 1.055 * Math.pow(v2, 1 / 2.4) - 0.055;
    LINEAR_TO_SRGB_LUT[j] = Math.round(srgb * 255);
  }
})();

function srgbToLinear(c) {
  return SRGB_TO_LINEAR_LUT[c];
}

function linearToSrgb(c) {
  var clamped = Math.max(0, Math.min(1, c));
  return LINEAR_TO_SRGB_LUT[Math.round(clamped * 255)];
}

// ===================== Gamma 感知 Floyd-Steinberg（线性空间误差扩散） =====================
function gammaFloydSteinbergDither(imageData, strength) {
  var width = imageData.width;
  var height = imageData.height;
  var data = imageData.data;
  var findClosestColor = _fcc();
  // 工作缓冲：线性空间，每像素 3 通道浮点
  var linData = new Float32Array(width * height * 3);
  for (var i = 0; i < data.length; i += 4) {
    var p = (i / 4) * 3;
    linData[p] = srgbToLinear(data[i]);
    linData[p + 1] = srgbToLinear(data[i + 1]);
    linData[p + 2] = srgbToLinear(data[i + 2]);
  }

  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      var p2 = (y * width + x) * 3;
      var r = linData[p2];
      var g = linData[p2 + 1];
      var b = linData[p2 + 2];

      // 转回 sRGB 交给感知量化器选最近色
      var closest = findClosestColor(linearToSrgb(r), linearToSrgb(g), linearToSrgb(b));

      var idx = (y * width + x) * 4;
      data[idx] = closest.r;
      data[idx + 1] = closest.g;
      data[idx + 2] = closest.b;

      // 在线性空间计算误差（关键：误差值按物理亮度计）
      var errR = (r - srgbToLinear(closest.r)) * strength;
      var errG = (g - srgbToLinear(closest.g)) * strength;
      var errB = (b - srgbToLinear(closest.b)) * strength;

      if (x + 1 < width) {
        var np = p2 + 3;
        linData[np] += errR * 7 / 16;
        linData[np + 1] += errG * 7 / 16;
        linData[np + 2] += errB * 7 / 16;
      }
      if (y + 1 < height) {
        if (x > 0) {
          var np2 = p2 + width * 3 - 3;
          linData[np2] += errR * 3 / 16;
          linData[np2 + 1] += errG * 3 / 16;
          linData[np2 + 2] += errB * 3 / 16;
        }
        var np3 = p2 + width * 3;
        linData[np3] += errR * 5 / 16;
        linData[np3 + 1] += errG * 5 / 16;
        linData[np3 + 2] += errB * 5 / 16;
        if (x + 1 < width) {
          var np4 = p2 + width * 3 + 3;
          linData[np4] += errR * 1 / 16;
          linData[np4 + 1] += errG * 1 / 16;
          linData[np4 + 2] += errB * 1 / 16;
        }
      }
    }
  }
  return imageData;
}

// ===================== Bayer 4×4 有序抖动 =====================
var BAYER_MATRIX = [
  [0, 8, 2, 10],
  [12, 4, 14, 6],
  [3, 11, 1, 9],
  [15, 7, 13, 5]
];

function bayerDither(imageData, strength) {
  var width = imageData.width;
  var height = imageData.height;
  var data = imageData.data;
  var findClosestColor = _fcc();

  for (var y = 0; y < height; y++) {
    var row = BAYER_MATRIX[y & 3];
    for (var x = 0; x < width; x++) {
      var bias = (row[x & 3] - 8) * strength;
      var idx = (y * width + x) * 4;
      var r = Math.min(255, Math.max(0, data[idx] + bias));
      var g = Math.min(255, Math.max(0, data[idx + 1] + bias));
      var b = Math.min(255, Math.max(0, data[idx + 2] + bias));
      var closest = findClosestColor(r, g, b);
      data[idx] = closest.r;
      data[idx + 1] = closest.g;
      data[idx + 2] = closest.b;
    }
  }
  return imageData;
}

// ===================== Atkinson 增强 / SZ 校色共享设施 =====================
// 六色显示调色板（索引顺序: 0黑 1白 2黄 3红 4绿 5蓝，理论 sRGB）
var AE_DISPLAY_PALETTE = [
  [0, 0, 0],
  [255, 255, 255],
  [255, 255, 0],
  [255, 0, 0],
  [0, 255, 0],
  [0, 0, 255]
];

// 六色残差校准色（按墨水屏实际显色值标定，误差扩散用，与最终显示色不同）
var AE_RESIDUAL_PALETTE = [
  [0, 0, 0],
  [255, 255, 255],
  [255, 235, 0],
  [154, 0, 0],
  [20, 85, 16],
  [0, 36, 154]
];

// SZ 增强校色（YRD0370 fit128 感知六色，与 sz_enhanced.js PALETTE_PERCEIVED 一致）
var SZ_FIT128_PALETTE = [
  [2, 2, 2],          // 黑
  [190, 200, 200],    // 白
  [197, 194, 7],      // 黄
  [89, 10, 6],        // 红
  [36, 75, 24],       // 绿
  [0, 18, 148]        // 蓝
];

// 墨色索引 -> 预览 RGB（film 码显示色：黑/白/黄/红/绿(41,204,20)/蓝），
// 供 processImageData 精确映射回 film 编码（与 Web FILM_CODE_RGB 一致）
var INK_INDEX_TO_PREVIEW_RGB = [
  [0, 0, 0],
  [255, 255, 255],
  [255, 255, 0],
  [255, 0, 0],
  [41, 204, 20],
  [0, 0, 255]
];

function aeChannelToLinear(c) {
  c = c / 255;
  return c > 0.04045 ? Math.pow((c + 0.055) / 1.055, 2.4) : c / 12.92;
}

function aeLabPivot(t) {
  return t > 0.008856 ? Math.pow(t, 1.0 / 3.0) : 7.787 * t + 16.0 / 116.0;
}

// sRGB -> D65 CIELAB
function aeRgbToLab(r, g, b) {
  var rl = aeChannelToLinear(r) * 100.0;
  var gl = aeChannelToLinear(g) * 100.0;
  var bl = aeChannelToLinear(b) * 100.0;
  var x = (rl * 0.4124564 + gl * 0.3575761 + bl * 0.1804375) / 95.047;
  var y = (rl * 0.2126729 + gl * 0.7151522 + bl * 0.0721750) / 100.0;
  var z = (rl * 0.0193339 + gl * 0.1191920 + bl * 0.9503041) / 108.883;
  var fx = aeLabPivot(x);
  var fy = aeLabPivot(y);
  var fz = aeLabPivot(z);
  return { l: 116.0 * fy - 16.0, a: 500.0 * (fx - fy), b: 200.0 * (fy - fz) };
}

var AE_PALETTE_LABS = AE_DISPLAY_PALETTE.map(function (color) {
  return aeRgbToLab(color[0], color[1], color[2]);
});

// 64^3 查表单元索引
function aeLutCell(r, g, b) {
  return ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2);
}

// Atkinson 增强 LUT（惰性解码，首用才加载 ~1.4MB base64）
var _lutModule = null;
var _aeCorrectionLUT = null;
var _aeSelectionLUT = null;
var _aeLutChecked = false;

function _ensureAeLut() {
  if (_aeLutChecked) return;
  _aeLutChecked = true;
  try {
    if (!_lutModule) _lutModule = require('./atkinson_enhanced_lut');
    var res = _lutModule.getAtkinsonLut();
    if (res && res.ready) {
      _aeCorrectionLUT = res.correction;
      _aeSelectionLUT = res.selection;
    }
  } catch (e) {
    _aeCorrectionLUT = null;
    _aeSelectionLUT = null;
  }
}

function aeLutAvailable() {
  return _aeCorrectionLUT !== null && _aeCorrectionLUT !== undefined &&
    _aeSelectionLUT !== null && _aeSelectionLUT !== undefined;
}

// 六色选色: 蓝/青区域走两级 LUT（墨水屏蓝色补偿，仅 AE 校色），其余走 CIELAB 加权最近色
function aeSelectInkColor(r, g, b, lab, paletteLabs, useLut) {
  var bestIndex;
  if (useLut !== false && aeLutAvailable() && (lab.b < -10.0 || lab.a < -35.0)) {
    // 修正 LUT 混合后查选色 LUT
    var cell = aeLutCell(r, g, b) * 3;
    var cr = _aeCorrectionLUT[cell];
    var cg = _aeCorrectionLUT[cell + 1];
    var cb = _aeCorrectionLUT[cell + 2];
    var r2 = r - ((r - cr) >> 2);
    var g2 = g - ((g - cg) >> 2);
    var b2 = b - ((b - cb) >> 2);
    bestIndex = _aeSelectionLUT[aeLutCell(r2, g2, b2)];
    if (bestIndex >= 6) bestIndex = 0;
  } else {
    bestIndex = 0;
    var bestDistance = 0x7FFFFFFF;
    for (var i = 0; i < paletteLabs.length; i++) {
      var pl = paletteLabs[i];
      var dl = lab.l - pl.l;
      var da = lab.a - pl.a;
      var db = lab.b - pl.b;
      // 加权距离先截断成整数再做严格整数比较，平局保留较早索引
      var distance = Math.trunc(2.0 * dl * dl + 0.8 * da * da + db * db);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestIndex = i;
      }
    }
  }
  return bestIndex;
}

function aeClampU8(value) {
  return value < 0 ? 0 : (value > 255 ? 255 : value);
}

// Lab -> sRGB（upstream Lab 反变换，与 sz_enhanced.js upstreamLabToRgb 一致）
function szLabToRgb(L, a, b) {
  var y = (L + 16) / 116;
  var x = a / 500 + y;
  var z = y - b / 200;
  x = x > 0.206897 ? Math.pow(x, 3) : (x - 16 / 116) / 7.787;
  y = y > 0.206897 ? Math.pow(y, 3) : (y - 16 / 116) / 7.787;
  z = z > 0.206897 ? Math.pow(z, 3) : (z - 16 / 116) / 7.787;
  x = x * 95.047 / 100;
  y = y * 100.0 / 100;
  z = z * 108.883 / 100;
  var r = x * 3.2404542 + y * -1.5371385 + z * -0.4985314;
  var g = x * -0.969266 + y * 1.8760108 + z * 0.041556;
  var b2 = x * 0.0556434 + y * -0.2040259 + z * 1.0572252;
  r = r > 0.0031308 ? 1.055 * Math.pow(r, 1 / 2.4) - 0.055 : 12.92 * r;
  g = g > 0.0031308 ? 1.055 * Math.pow(g, 1 / 2.4) - 0.055 : 12.92 * g;
  b2 = b2 > 0.0031308 ? 1.055 * Math.pow(b2, 1 / 2.4) - 0.055 : 12.92 * b2;
  return [
    Math.max(0, Math.min(255, Math.round(r * 255))),
    Math.max(0, Math.min(255, Math.round(g * 255))),
    Math.max(0, Math.min(255, Math.round(b2 * 255)))
  ];
}

// SZ 校色预处理：CIELab L 压缩到面板黑/白范围（黑=fit128 2,2,2，白=fit128 190,200,200）
function szCompressDynamicRange(imageData) {
  var width = imageData.width;
  var height = imageData.height;
  var src = imageData.data;
  var n = width * height;
  var blackL = aeRgbToLab(2, 2, 2).l;
  var whiteL = aeRgbToLab(190, 200, 200).l;
  var out = _makeImageDataLike(width, height);
  var od = out.data;
  for (var i = 0; i < n; i++) {
    var o = i * 4;
    var lab = aeRgbToLab(src[o], src[o + 1], src[o + 2]);
    var compressedL = blackL + (lab.l / 100.0) * (whiteL - blackL);
    var rgb = szLabToRgb(compressedL, lab.a, lab.b);
    od[o] = rgb[0];
    od[o + 1] = rgb[1];
    od[o + 2] = rgb[2];
    od[o + 3] = 255;
  }
  return out;
}

// SZ 选色：RGB 平方距离最近邻 fit128 感知六色（黄/绿惩罚平衡亮暗不均）
var SZ_YELLOW_PENALTY = 1.8;
var SZ_GREEN_PENALTY = 1.8;
function szClosestFit128(r, g, b) {
  var best = 0;
  var bestDist = Infinity;
  for (var i = 0; i < SZ_FIT128_PALETTE.length; i++) {
    var c = SZ_FIT128_PALETTE[i];
    var dr = r - c[0];
    var dg = g - c[1];
    var db = b - c[2];
    var dist = dr * dr + dg * dg + db * db;
    if (i === 2) {
      dist *= SZ_YELLOW_PENALTY;
    } else if (i === 4) {
      dist *= SZ_GREEN_PENALTY;
    }
    if (dist < bestDist) {
      bestDist = dist;
      best = i;
    }
  }
  return best;
}

// Atkinson 增强量化: 三行滚动缓冲六邻域误差扩散。
// selectIndexFn(r,g,b,lab) 返回 0-5 色板索引，residualOf(index) 返回残差 RGB。
// 输出预览像素用 film 显示色填充；上层 processImageData 精确映射回 film 编码。
function atkinsonQuantizeBase(imageData, selectIndexFn, residualOf) {
  var width = imageData.width;
  var height = imageData.height;
  var data = imageData.data;
  var stride = (width + 3) * 3;
  var currentErrors = new Int32Array(stride);
  var nextErrors = new Int32Array(stride);
  var secondErrors = new Int32Array(stride);

  // 当前像素的墨色索引（逻辑行优先）
  var indexes = new Uint8Array(width * height);

  function slot(x) {
    return (x + 1) * 3;
  }

  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      var errorOffset = slot(x);
      var sourceOffset = (y * width + x) * 4;
      // 缓冲里累加的是未除以 8 的原始残差，消费时一次性 (sum+4)>>3
      var r = aeClampU8(data[sourceOffset] + ((currentErrors[errorOffset] + 4) >> 3));
      var g = aeClampU8(data[sourceOffset + 1] + ((currentErrors[errorOffset + 1] + 4) >> 3));
      var b = aeClampU8(data[sourceOffset + 2] + ((currentErrors[errorOffset + 2] + 4) >> 3));

      var lab = aeRgbToLab(r, g, b);
      var index = selectIndexFn(r, g, b, lab);
      indexes[y * width + x] = index;

      // 残差按墨水屏校准色计算（非显示色）
      var rp = residualOf(index);
      var errR = r - rp[0];
      var errG = g - rp[1];
      var errB = b - rp[2];

      // 六邻域: (x+1,y) (x+2,y) (x-1,y+1) (x,y+1) (x+1,y+1) (x,y+2)
      var n = slot(x + 1);
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
    // 滚动三行缓冲
    var temp = currentErrors;
    currentErrors = nextErrors;
    nextErrors = secondErrors;
    secondErrors = temp;
    secondErrors.fill(0);
  }

  // 预览: 用 film 显示色填充（与打包/设备显示一致）
  var out = _makeImageDataLike(width, height);
  var outData = out.data;
  for (var i = 0; i < indexes.length; i++) {
    var color = INK_INDEX_TO_PREVIEW_RGB[indexes[i]];
    outData[i * 4] = color[0];
    outData[i * 4 + 1] = color[1];
    outData[i * 4 + 2] = color[2];
    outData[i * 4 + 3] = 255;
  }
  return out;
}

// 原版：AE 校色（含蓝/青 LUT 补偿，选色用 AE 加权 CIELAB 距离）
function atkinsonEnhancedQuantize(imageData) {
  _ensureAeLut();
  return atkinsonQuantizeBase(imageData,
    function (r, g, b, lab) {
      return aeSelectInkColor(r, g, b, lab, AE_PALETTE_LABS, true);
    },
    function (i) {
      return AE_RESIDUAL_PALETTE[i];
    });
}

// 新算法：Atkinson 扩散 + SZ 校色（compressDynamicRange 预处理 + fit128 感知色
// RGB 距离选色与残差，与 SZ 增强 V5 seed 的校色链路一致，无蓝青 LUT）
function atkinsonSzCalibQuantize(imageData) {
  return atkinsonQuantizeBase(szCompressDynamicRange(imageData),
    function (r, g, b) {
      return szClosestFit128(r, g, b);
    },
    function (i) {
      return SZ_FIT128_PALETTE[i];
    });
}

// ===================== SZ 增强（结构感知六色量化，仅 Pro） =====================
// 惰性加载 sz_enhanced.js（仅当真正使用到才解析该 70KB 模块，避免拖慢冷启动）
var _szModule = null;
function szEnhancedDither(imageData) {
  if (!_szModule) _szModule = require('./sz_enhanced');
  return _szModule.szEnhancedDither(imageData);
}

module.exports = {
  gammaFloydSteinbergDither: gammaFloydSteinbergDither,
  bayerDither: bayerDither,
  atkinsonEnhancedQuantize: atkinsonEnhancedQuantize,
  atkinsonSzCalibQuantize: atkinsonSzCalibQuantize,
  szEnhancedDither: szEnhancedDither,
  aeLutAvailable: aeLutAvailable,
  BAYER_MATRIX: BAYER_MATRIX,
  szDebug: {
    aeRgbToLab: aeRgbToLab,
    szLabToRgb: szLabToRgb,
    szClosestFit128: szClosestFit128,
    szCompressDynamicRange: szCompressDynamicRange,
    atkinsonQuantizeBase: atkinsonQuantizeBase,
    SZ_FIT128_PALETTE: SZ_FIT128_PALETTE
  }
};
