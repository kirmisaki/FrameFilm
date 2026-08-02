// e6pro.js — E6 新一代颜色转换算法（浏览器验证版 v3）
// 目标：字迹清晰 + 颜色丰富 + 颗粒感少
//
// v3 核心升级：从「C↔白/黑 单轴网点」升级为「双色相混合网点」，
//   让感知色域超出 E6 固有 6 色：
//     橙 = 红/黄 网点 · 紫 = 红/蓝 网点 · 青 = 蓝/绿 网点 · 黄绿 = 黄/绿 网点
//     浅粉 = 红/白 稀疏点 · 浅蓝 = 蓝/白 稀疏点 · 深蓝 = 蓝/黑 深网点
//
// 四路分层：
//   1. 边缘保护（Edge-Aware）：Sobel 梯度 / 暗度像素 → 零抖动最近色 + 局部对比度增强
//      → 文字/线条/图标锐利无网点扩散
//   2. 近中性（RGB 极差色度 < colorMinSat）或近黑/近白 → 黑白亮度色阶半色调 + 端点硬切
//      → 大面积底色干净
//   3. 高色度且 Lab 距离某彩色足够近 → 实心最近彩色色 → 纯色块零颗粒
//   4. 其余（中色度/色相过渡/浅彩/渐变）→ 两轮 Bayer 半色调：
//      第一轮在「色相混合参考色 X_mix ↔ 中性 N(白/黑)」间按亮度投影取网点，
//      第二轮（相位偏移阈值）在相邻两彩色 C_lo↔C_hi 间按色相位置取网点
//      → 中间色相靠双色网点密度混合呈现，颜色丰富且颗粒规整
(function () {
  'use strict';

  var PALETTE = [
    { name: '黑色', r: 0, g: 0, b: 0 },
    { name: '白色', r: 255, g: 255, b: 255 },
    { name: '黄色', r: 255, g: 255, b: 0 },
    { name: '红色', r: 255, g: 0, b: 0 },
    { name: '蓝色', r: 0, g: 0, b: 255 },
    { name: '绿色', r: 41, g: 204, b: 20 }
  ];
  var BLACK = PALETTE[0], WHITE = PALETTE[1];

  // 色相环上四个彩色（按色相排列），区间：红0→黄60→绿≈112→蓝240→红360
  var CHROMA = [
    { c: PALETTE[3], hue: 0 },     // 红
    { c: PALETTE[2], hue: 60 },    // 黄
    { c: PALETTE[5], hue: 112.3 }, // 绿
    { c: PALETTE[4], hue: 240 }    // 蓝
  ];
  var CHROMA_LUM = [0.299, 0.886, 0.526, 0.114]; // 与 CHROMA 同序的亮度

  function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
  var paletteLab = PALETTE.map(function (c) { return rgbToLab(c.r, c.g, c.b); });

  // 最近色映射（含中性色法则）—— 边缘/文字保护模式使用
  function findClosestColor(r, g, b) {
    var input = rgbToHsl(r, g, b);
    var maxc = Math.max(r, g, b), minc = Math.min(r, g, b);
    if ((maxc - minc) / 255 < 0.12) {
      return input.l > 0.5 ? WHITE : BLACK;
    }
    var minDist = Infinity, closestColor = BLACK;
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
    var distBlack = labDistance(labInput, paletteLab[0]);
    var distWhite = labDistance(labInput, paletteLab[1]);
    var distNeutral = Math.min(distBlack, distWhite);
    var neutralColor = distBlack < distWhite ? BLACK : WHITE;
    var distChosen = labDistance(labInput, paletteLab[PALETTE.indexOf(closestColor)]);
    if (distNeutral < distChosen * 0.45) return neutralColor;
    return closestColor;
  }

  // 最近「彩色」色（仅黄/红/蓝/绿）+ Lab 距离 —— 实心判断与半色调参考
  function findNearestChromatic(r, g, b) {
    var labInput = rgbToLab(r, g, b);
    var best = CHROMA[1].c, bestDist = Infinity; // 默认黄（纯色中性法则在近中性已被分流）
    for (var i = 0; i < CHROMA.length; i++) {
      var d = labDistance(labInput, paletteLab[PALETTE.indexOf(CHROMA[i].c)]);
      if (d < bestDist) { bestDist = d; best = CHROMA[i].c; }
    }
    return { color: best, dist: bestDist };
  }

  // 色相 → 所在区间 [lo, hi] 及位置 t（0=C_lo，1=C_hi）
  function hueInterval(h) {
    if (h < 60) return { lo: CHROMA[0], hi: CHROMA[1], t: h / 60 };
    if (h < 112.3) return { lo: CHROMA[1], hi: CHROMA[2], t: (h - 60) / 52.3 };
    if (h < 240) return { lo: CHROMA[2], hi: CHROMA[3], t: (h - 112.3) / 127.7 };
    return { lo: CHROMA[3], hi: CHROMA[0], t: (h - 240) / 120 };
  }

  // Bayer 阈值矩阵（值 0..(1-1/n^2)）
  function bayerOrder(n) {
    if (n === 1) return [[0]];
    var half = n / 2, sub = bayerOrder(half);
    var m = [];
    for (var y = 0; y < n; y++) {
      m[y] = [];
      for (var x = 0; x < n; x++) {
        m[y][x] = 4 * sub[y % half][x % half] + 2 * (y >= half ? 1 : 0) + (x >= half ? 1 : 0);
      }
    }
    return m;
  }

  // Sobel 梯度
  function sobelAt(d, w, h, x, y) {
    if (x < 1 || y < 1 || x >= w - 1 || y >= h - 1) return 0;
    function lum(px, py) {
      var i = (py * w + px) * 4;
      return d[i] * 0.299 + d[i + 1] * 0.587 + d[i + 2] * 0.114;
    }
    var tl = lum(x - 1, y - 1), tc = lum(x, y - 1), tr = lum(x + 1, y - 1);
    var ml = lum(x - 1, y), mr = lum(x + 1, y);
    var bl = lum(x - 1, y + 1), bc = lum(x, y + 1), br = lum(x + 1, y + 1);
    var gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
    var gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
    return Math.sqrt(gx * gx + gy * gy);
  }

  function enhance(v, k) {
    return clamp((v - 128) * k + 128, 0, 255);
  }

  var DEFAULTS = {
    edgeProtect: true,   // 边缘/文字保护开关
    edgeGradT: 35,       // Sobel 梯度阈值
    darkT: 0.35,         // 亮度阈值（低于此视为文字/深色主体）
    contrastK: 1.25,     // 边缘区域对比度增强
    bayerSize: 8,        // Bayer 矩阵尺寸（4 或 8）
    brightK: 1.0,        // 灰度亮度调制系数
    colorMinSat: 0.12,   // RGB 极差色度下限：低于此走黑/白灰度半色调（底色干净）
    paletteNearT: 18,    // Lab 距离阈值：离某彩色足够近 → 实心该色（保护纯色块零颗粒）
    band: 0.08,          // 端点硬切带
    gradStrategy: 'v3',  // 渐变层策略：v3=双色相混合 / adaptive=自适应抖动
    imgStrategy: 'v3',   // 图片层策略：同上
    textAA: false         // 文字层抗锯齿：默认关=清晰硬切（EPD 可读性最好）。开启时边缘像素按 4×4 Bayer 覆盖度半色调，会引入颗粒，供对比实验
  };

  // 主转换：输入 ImageData，输出 6 色 ImageData
  function convert(imageData, opts) {
    var o = {};
    for (var k in DEFAULTS) o[k] = DEFAULTS[k];
    for (var k2 in (opts || {})) o[k2] = opts[k2];
    o.bayerSize = o.bayerSize === 4 ? 4 : 8;

    var w = imageData.width, h = imageData.height;
    var d = imageData.data;
    var out = new Uint8ClampedArray(d.length);
    var bm = o.bayerSize - 1;
    var bayer = bayerOrder(o.bayerSize);
    var bayerNorm = o.bayerSize * o.bayerSize;

    // 颜色级缓存：平坦设计图唯一色少，缓存让 80 万像素也秒级完成
    var cache = {};

    for (var y = 0; y < h; y++) {
      for (var x = 0; x < w; x++) {
        var i = (y * w + x) * 4;
        var r = d[i], g = d[i + 1], b = d[i + 2];
        if (o.preserveAlpha && d[i + 3] < 128) { out[i + 3] = d[i + 3]; continue; }
        var key = (r << 16) | (g << 8) | b;
        var e = cache[key];
        var th1 = bayer[y & bm][x & bm] / bayerNorm;
        var th2 = bayer[(y + 4) & bm][(x + 5) & bm] / bayerNorm; // 相位偏移，与第一轮去相关
        var C;

        var isEdge = false;
        if (o.edgeProtect) {
          var grad = sobelAt(d, w, h, x, y);
          var lum0 = e ? e.lum : (r * 0.299 + g * 0.587 + b * 0.114) / 255;
          if (grad > o.edgeGradT || lum0 < o.darkT) isEdge = true;
        }

        if (isEdge) {
          // ===== 模式1：边缘/文字 — 零抖动最近色 + 对比度增强 =====
          var er = enhance(r, o.contrastK);
          var eg = enhance(g, o.contrastK);
          var eb = enhance(b, o.contrastK);
          C = findClosestColor(er, eg, eb);
        } else {
          if (!e) {
            var maxc = Math.max(r, g, b), minc = Math.min(r, g, b);
            e = cache[key] = {
              // 用 RGB 极差色度而非 HSL 饱和度：近白/近黑 HSL 饱和度会虚高（如 #fffaf6 s=1.0）
              chroma: (maxc - minc) / 255,
              lum: (r * 0.299 + g * 0.587 + b * 0.114) / 255,
              hsl: rgbToHsl(r, g, b),
              near: findNearestChromatic(r, g, b)
            };
          }
          if (e.chroma < o.colorMinSat || e.lum <= o.band || e.lum >= 1 - o.band) {
            // ===== 模式2：近中性/近黑/近白 — 黑白亮度色阶半色调 =====
            var bl = clamp((e.lum - 0.5) * o.brightK + 0.5, 0, 1);
            if (o.band > 0) {
              if (bl <= o.band) C = BLACK;
              else if (bl >= 1 - o.band) C = WHITE;
              else C = th1 < bl ? WHITE : BLACK;
            } else {
              C = th1 < bl ? WHITE : BLACK;
            }
          } else if (e.near.dist <= o.paletteNearT) {
            // ===== 模式3：高色度且离某彩色足够近 — 实心最近彩色色（纯色块零颗粒） =====
            C = e.near.color;
          } else {
            // ===== 模式4：双色相混合半色调 =====
            if (!e.iv) e.iv = hueInterval(e.hsl.h);
            var iv = e.iv;
            var li = CHROMA.indexOf(iv.lo), hiI = CHROMA.indexOf(iv.hi);
            var lMix = CHROMA_LUM[li] + (CHROMA_LUM[hiI] - CHROMA_LUM[li]) * iv.t;
            var N, tN;
            if (e.lum >= lMix) { N = WHITE; tN = (1 - lMix) > 0.01 ? (e.lum - lMix) / (1 - lMix) : 0; }
            else { N = BLACK; tN = lMix > 0.01 ? (lMix - e.lum) / lMix : 0; }
            tN = clamp(tN, 0, 1);
            if (th1 < (1 - tN)) {
              C = th2 < iv.t ? iv.hi.c : iv.lo.c; // 第二轮：色相位置 → C_lo / C_hi 网点
            } else {
              C = N;
            }
          }
        }
        out[i] = C.r; out[i + 1] = C.g; out[i + 2] = C.b;
        out[i + 3] = o.preserveAlpha ? d[i + 3] : 255;
      }
    }

    // ImageData 的 width/height 是只读 getter，构造时已设置，勿再赋值（否则 TypeError）
    return new ImageData(out, w, h);
  }

  // ===== v4 分层区域管线 =====
  // 思路：渲染期就知道每块内容的类型（文字/纯色/渐变/图片），
  //   各区域用最合适的方式转换后再按绘制顺序合成，避免全局处理互相污染。
  //   文字 → 最近色零抖动（+对比增强）· 纯色块 → 最近色零抖动
  //   渐变/图片 → v3 双色相混合半色调

  function convertZoneImage(imgData, mode, opts) {
    var o = {};
    for (var k in DEFAULTS) o[k] = DEFAULTS[k];
    for (var k2 in (opts || {})) o[k2] = opts[k2];

    var w = imgData.width, h = imgData.height;
    var d = imgData.data;
    var out = new Uint8ClampedArray(d.length);

    if (mode === 'solid' || mode === 'text') {
      // 最近色零抖动；solid 层 alpha 硬切（<64 透明，≥64 映射为不透明 6 色）→ 输出严格落在 6 色板
      // text 层若开 textAA：边缘像素（alpha 1..253）按覆盖度做 4×4 Bayer 半色调——
      //   命中则点亮为实心文字色，未命中则透明露出下层背景 → 边缘柔和、无杂色
      var isText = mode === 'text';
      var kCon = isText ? o.contrastK : 1.0;
      var bayer4 = (isText && o.textAA) ? bayerOrder(4) : null;
      for (var i = 0; i < d.length; i += 4) {
        var a = d[i + 3];
        if (a === 0) { out[i + 3] = 0; continue; }
        if (bayer4 && a < 254) {
          var px = i / 4;
          var yy = (px / w) | 0, xx = px % w;
          var th = bayer4[yy & 3][xx & 3] / 16;
          if (a / 255 < th) { out[i + 3] = 0; continue; }
        } else if (a < 64) {
          out[i + 3] = 0; continue;
        }
        var C;
        if (kCon !== 1.0) C = findClosestColor(enhance(d[i], kCon), enhance(d[i + 1], kCon), enhance(d[i + 2], kCon));
        else C = findClosestColor(d[i], d[i + 1], d[i + 2]);
        out[i] = C.r; out[i + 1] = C.g; out[i + 2] = C.b; out[i + 3] = 255;
      }
      return new ImageData(out, w, h);
    }

    // gradient / image：默认 v3 双色相混合半色调；可选 adaptive 自适应抖动（复用 EPDSim，逐像素误差扩散）
    var strat = (mode === 'gradient' ? o.gradStrategy : o.imgStrategy) || 'v3';
    if (strat === 'adaptive') {
      return adaptiveZone(imgData);
    }
    o.preserveAlpha = true;
    if (mode === 'gradient') o.edgeProtect = false;
    return convert(imgData, o);
  }

  // 自适应抖动层：仅不透明区参与误差扩散（透明像素保持 0），输出严格 6 色
  function adaptiveZone(imgData) {
    if (!window.EPDSim || !window.EPDSim.adaptiveDither) return convert(imgData, {});
    var w = imgData.width, h = imgData.height;
    var d = imgData.data;
    var work = new ImageData(w, h);
    var wd = work.data;
    for (var i = 0; i < d.length; i += 4) {
      if (d[i + 3] >= 128) {
        wd[i] = d[i]; wd[i + 1] = d[i + 1]; wd[i + 2] = d[i + 2]; wd[i + 3] = 255;
      }
    }
    window.EPDSim.adaptiveDither(work);
    var out = new Uint8ClampedArray(d.length);
    for (var j = 0; j < d.length; j += 4) {
      if (work.data[j + 3] > 0) {
        out[j] = work.data[j]; out[j + 1] = work.data[j + 1]; out[j + 2] = work.data[j + 2]; out[j + 3] = 255;
      }
    }
    return new ImageData(out, w, h);
  }

  // 录制式分层 ctx：把模板 render 的绘制按内容类型分到独立图层，
  // 最后 convertZoneImage 各自转换后按绘制顺序合成。
  function makeRecorder(W, H) {
    var zones = [];
    var cur = null;
    var pathOps = [];
    var state = {
      fillStyle: '#000000', strokeStyle: '#000000',
      font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic',
      lineWidth: 1, lineCap: 'butt', lineJoin: 'miter',
      tfs: []
    };
    var saveStack = [];

    function applyStateTo(ctx) {
      if (!isGrad(state.fillStyle)) ctx.fillStyle = state.fillStyle;
      if (!isGrad(state.strokeStyle)) ctx.strokeStyle = state.strokeStyle;
      ctx.font = state.font; ctx.textAlign = state.textAlign; ctx.textBaseline = state.textBaseline;
      ctx.lineWidth = state.lineWidth; ctx.lineCap = state.lineCap; ctx.lineJoin = state.lineJoin;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      for (var i = 0; i < state.tfs.length; i++) {
        var t = state.tfs[i];
        ctx[t[0]].apply(ctx, t[1]);
      }
    }

    function newZone(type) {
      var c = document.createElement('canvas');
      c.width = W; c.height = H;
      var z = { type: type, canvas: c, ctx: c.getContext('2d') };
      applyStateTo(z.ctx);
      zones.push(z);
      cur = z;
      return z.ctx;
    }

    function zoneFor(type) {
      if (!cur || cur.type !== type) return newZone(type);
      return cur.ctx;
    }

    function isGrad(v) { return v && v.__grad; }

    function applyFill(ctx, stroke) {
      var fs = stroke ? state.strokeStyle : state.fillStyle;
      if (isGrad(fs)) {
        var g = fs.__grad;
        var gr = g.type === 'radial'
          ? ctx.createRadialGradient(g.x0, g.y0, g.r0, g.x1, g.y1, g.r1)
          : ctx.createLinearGradient(g.x0, g.y0, g.x1, g.y1);
        for (var i = 0; i < g.stops.length; i++) gr.addColorStop(g.stops[i][0], g.stops[i][1]);
        ctx[stroke ? 'strokeStyle' : 'fillStyle'] = gr;
      } else {
        ctx[stroke ? 'strokeStyle' : 'fillStyle'] = fs;
      }
    }

    function flushPath(ctx) {
      for (var i = 0; i < pathOps.length; i++) {
        var o = pathOps[i];
        if (o[0] === 'roundRect') {
          if (ctx.roundRect) ctx.roundRect(o[1], o[2], o[3], o[4], o[5]);
          else ctx.rect(o[1], o[2], o[3], o[4]);
        } else {
          ctx[o[0]].apply(ctx, o.slice(1));
        }
      }
    }

    function paintPath(how) {
      var stroke = how === 'stroke';
      var fs = stroke ? state.strokeStyle : state.fillStyle;
      var type = isGrad(fs) ? 'gradient' : 'solid';
      var ctx = zoneFor(type);
      applyFill(ctx, stroke);
      flushPath(ctx);
      ctx[how]();
      pathOps = [];
    }

    var rec = {};

    ['fillStyle', 'strokeStyle', 'font', 'textAlign', 'textBaseline', 'lineWidth', 'lineCap', 'lineJoin'].forEach(function (p) {
      Object.defineProperty(rec, p, {
        get: function () { return state[p]; },
        set: function (v) {
          state[p] = v;
          if (cur && !isGrad(v)) cur.ctx[p] = v;
        }
      });
    });

    rec.createLinearGradient = function (x0, y0, x1, y1) {
      var g = { __grad: { type: 'linear', x0: x0, y0: y0, x1: x1, y1: y1, stops: [] } };
      g.addColorStop = function (offset, color) { g.__grad.stops.push([offset, color]); };
      return g;
    };
    rec.createRadialGradient = function (x0, y0, r0, x1, y1, r1) {
      var g = { __grad: { type: 'radial', x0: x0, y0: y0, r0: r0, x1: x1, y1: y1, r1: r1, stops: [] } };
      g.addColorStop = function (offset, color) { g.__grad.stops.push([offset, color]); };
      return g;
    };

    ['beginPath', 'moveTo', 'lineTo', 'quadraticCurveTo', 'bezierCurveTo', 'arc', 'arcTo', 'ellipse', 'closePath', 'rect'].forEach(function (m) {
      rec[m] = function () { pathOps.push([m].concat(Array.prototype.slice.call(arguments))); };
    });
    rec.roundRect = function (x, y, w, h, r) { pathOps.push(['roundRect', x, y, w, h, r]); };

    rec.fill = function () { paintPath('fill'); };
    rec.stroke = function () { paintPath('stroke'); };
    rec.fillRect = function (x, y, w, h) {
      var ctx = zoneFor(isGrad(state.fillStyle) ? 'gradient' : 'solid');
      applyFill(ctx, false);
      ctx.fillRect(x, y, w, h);
    };
    rec.strokeRect = function (x, y, w, h) {
      var ctx = zoneFor('solid');
      applyFill(ctx, true);
      ctx.strokeRect(x, y, w, h);
    };
    rec.fillText = function (text, x, y, maxW) {
      var ctx = zoneFor('text');
      applyFill(ctx, false);
      ctx.fillText(text, x, y, maxW);
    };
    rec.strokeText = function (text, x, y, maxW) {
      var ctx = zoneFor('text');
      applyFill(ctx, true);
      ctx.strokeText(text, x, y, maxW);
    };
    rec.drawImage = function () {
      var ctx = zoneFor('image');
      ctx.drawImage.apply(ctx, arguments);
    };
    rec.measureText = function (text) {
      var ctx = cur ? cur.ctx : zoneFor('solid');
      applyFill(ctx, false);
      return ctx.measureText(text);
    };
    rec.getImageData = function (x, y, w, h) {
      var ctx = cur ? cur.ctx : zoneFor('solid');
      return ctx.getImageData(x, y, w, h);
    };
    rec.putImageData = function () {
      var ctx = cur ? cur.ctx : zoneFor('solid');
      ctx.putImageData.apply(ctx, arguments);
    };

    ['setTransform', 'scale', 'translate', 'rotate'].forEach(function (m) {
      rec[m] = function () {
        state.tfs.push([m, Array.prototype.slice.call(arguments)]);
        if (cur) cur.ctx[m].apply(cur.ctx, arguments);
      };
    });
    rec.save = function () {
      saveStack.push({ tfs: state.tfs.slice(), fillStyle: state.fillStyle, strokeStyle: state.strokeStyle, font: state.font, textAlign: state.textAlign, textBaseline: state.textBaseline, lineWidth: state.lineWidth, lineCap: state.lineCap, lineJoin: state.lineJoin });
      if (cur) cur.ctx.save();
    };
    rec.restore = function () {
      var s = saveStack.pop();
      if (!s) return;
      state.tfs = s.tfs; state.fillStyle = s.fillStyle; state.strokeStyle = s.strokeStyle;
      state.font = s.font; state.textAlign = s.textAlign; state.textBaseline = s.textBaseline;
      state.lineWidth = s.lineWidth; state.lineCap = s.lineCap; state.lineJoin = s.lineJoin;
      if (cur) { cur.ctx.restore(); applyStateTo(cur.ctx); }
    };
    rec.clearRect = function (x, y, w, h) {
      if (cur) cur.ctx.clearRect(x, y, w, h);
    };

    // 分层转换 + 合成到目标 canvas
    rec.finish = function (dstCanvas, opts) {
      var dctx = dstCanvas.getContext('2d');
      var DW = dstCanvas.width, DH = dstCanvas.height;
      dctx.save();
      dctx.setTransform(1, 0, 0, 1, 0, 0);
      dctx.clearRect(0, 0, DW, DH);
      dctx.imageSmoothingEnabled = false;
      for (var i = 0; i < zones.length; i++) {
        var z = zones[i];
        var imgData = z.ctx.getImageData(0, 0, W, H);
        var mode = z.type === 'gradient' ? 'gradient' : (z.type === 'image' ? 'image' : (z.type === 'text' ? 'text' : 'solid'));
        var res = convertZoneImage(imgData, mode, opts);
        var tmp = document.createElement('canvas');
        tmp.width = W; tmp.height = H;
        tmp.getContext('2d').putImageData(res, 0, 0);
        dctx.drawImage(tmp, 0, 0, DW, DH);
      }
      dctx.restore();
    };

    return rec;
  }

  // 便捷：对 canvas 应用转换并回填（物理全尺寸）
  function applyTo(ctx, canvas, opts) {
    var imgData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    var res = convert(imgData, opts);
    var tmp = document.createElement('canvas');
    tmp.width = canvas.width; tmp.height = canvas.height;
    tmp.getContext('2d').putImageData(res, 0, 0);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(tmp, 0, 0);
    ctx.restore();
    return res;
  }

  window.E6Pro = { convert: convert, convertZoneImage: convertZoneImage, makeRecorder: makeRecorder, applyTo: applyTo, DEFAULTS: DEFAULTS, PALETTE: PALETTE };
})();
