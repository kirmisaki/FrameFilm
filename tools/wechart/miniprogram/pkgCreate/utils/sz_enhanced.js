// SZ 增强：YRD0370 V7.5 结构感知六色量化（仅 FrameFilm Pro 792×528 生效）
//
// 与交接包严格一致移植（完整流水线，非近似）：
//   H:\YRD0370_V7.5_AI_Handoff_2026-08-24\src\yrd0370_structure_quantize.cpp
//   H:\YRD0370_V7.5_AI_Handoff_2026-08-24\src\balanced_fit128_seed.mjs
//   H:\YRD0370_V7.5_AI_Handoff_2026-08-24\third_party\epaper-image-convert\src\processor.js
//   H:\YRD0370_V7.5_AI_Handoff_2026-08-24\scripts\process_image_v7p5.sh
//
// 流水线（与 process_image_v7p5.sh 逐条命令一致）：
//   1. V5 seed：balanced_fit128_seed.mjs
//      upstream balanced preset（compressDynamicRange + perceived 色板
//      Floyd-Steinberg 非蛇形抖动）生成严格六色 seed label。
//   2. V7：structure_quantize（seed = V5，refine-passes 1 / refine-same 0，
//      sparkle/chroma cleanup 0）
//   3. V7.5：structure_quantize（seed = V7 输出，refine-same -1 跳过主体 refine，
//      sparkle-cleanup 1，chroma-cleanup 1）
//
// 冻结参数（V7.5 实际命令行，与 C++ 默认 Config 不同，勿用默认值）：
//   --radius 0（关闭双边滤波）--detail-flat 1.0
//   --edge-low 0.100 --edge-high 0.300
//   --poster-l 22 --poster-c 10 --poster-h 36 --poster-blend 0.0
//   --dither-l 1.0 --dither-c 1.0
//   --neutral-c 0.035 --deep-black 0.070
//   --barrier-l 10.0 --barrier-c 10.0 --cross-hue 0.004
//   --refine-passes 1 --refine-edge 0.025 --refine-min-l 0.70 --refine-tolerance 0.000
//   --refine-same 0（V7）/-1（V7.5）
//   --left-text-outer 0.60 --left-text-chroma 0.075 --left-text-b 0.035
//   --left-text-l 0.080 --left-text-edge-scale 6.00
//   --sparkle-edge 0.040 --sparkle-bright-l 0.70 --sparkle-luma-delta 0.18
//   --sparkle-source-delta 0.050 --sparkle-data-tolerance 0.001
//   --sparkle-cleanup-passes 0（V7）/1（V7.5）--sparkle-cleanup-window 1
//   --chroma-cleanup-passes 0（V7）/1（V7.5）--chroma-cleanup-same 0
//   --cleanup 0
//
// 输入：原始 cover 裁切后的 ImageData（Pro 792×528，未叠加对比度/饱和度调整，
//       与 C++ 的 source_cover_792x528.rgb 一致；由 convert.js 传入）
// 输出：六色严格 RGB（kPalette output）ImageData，后续 processImageData 打包

(function () {
    'use strict';

    // =====================================================================
    // 常量（与 C++ kPalette 一致）：0黑 1白 2黄 3红 4绿 5蓝
    // =====================================================================
    var PALETTE_OUTPUT = [
        [0, 0, 0],                             // black  理论色
        [255, 255, 255],                       // white
        [255, 255, 0],                         // yellow
        [255, 0, 0],                           // red
        [0, 255, 0],                           // green
        [0, 0, 255]                            // blue
    ];

    // fit128 感知色板（Epson V19 II 0° Gamma2.2 扫描实测）
    var PALETTE_PERCEIVED = [
        [2.0, 2.0, 2.0],                       // black
        [190.0, 200.0, 200.0],                 // white
        [197.477795, 194.494739, 6.653345],    // yellow
        [88.942443, 10.069738, 5.819193],      // red
        [35.987383, 74.688839, 23.922108],     // green
        [0.0, 18.061406, 148.165358]           // blue
    ];

    // upstream 色板（V5 seed 用）：index 4 = reserved（跳过）
    var UPSTREAM_PERCEIVED = [
        [2.0, 2.0, 2.0],
        [190.0, 200.0, 200.0],
        [197.477795, 194.494739, 6.653345],
        [88.942443, 10.069738, 5.819193],
        [0.0, 0.0, 0.0],
        [0.0, 18.061406, 148.165358],
        [35.987383, 74.688839, 23.922108]
    ];
    // upstream index -> kPalette label（index 4 reserved 不会命中）
    var UPSTREAM_TO_LABEL = [0, 1, 2, 3, 0, 5, 4];

    // 8×8 Bayer 矩阵（C++ kBayer8）
    var BAYER8 = [
        0, 48, 12, 60, 3, 51, 15, 63,
        32, 16, 44, 28, 35, 19, 47, 31,
        8, 56, 4, 52, 11, 59, 7, 55,
        40, 24, 36, 20, 43, 27, 39, 23,
        2, 50, 14, 62, 1, 49, 13, 61,
        34, 18, 46, 30, 33, 17, 45, 29,
        10, 58, 6, 54, 9, 57, 5, 53,
        42, 26, 38, 22, 41, 25, 37, 21
    ];

    var FS_TAPS = [
        [1, 0, 7 / 16],
        [-1, 1, 3 / 16],
        [0, 1, 5 / 16],
        [1, 1, 1 / 16]
    ];

    var PI = Math.PI;

    // =====================================================================
    // 基础数学（与 C++ 逐行一致）
    // =====================================================================
    function clamp(v, low, high) {
        return v < low ? low : (v > high ? high : v);
    }

    function smoothstep(low, high, value) {
        if (high <= low) {
            return value >= high ? 1.0 : 0.0;
        }
        var t = clamp((value - low) / (high - low), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    function srgbToLinearChannel(encoded) {
        var value = clamp(encoded / 255.0, 0.0, 1.0);
        return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
    }

    function linearToSrgbChannel(linear) {
        linear = clamp(linear, 0.0, 1.0);
        var encoded = linear <= 0.0031308 ? 12.92 * linear : 1.055 * Math.pow(linear, 1.0 / 2.4) - 0.055;
        return encoded * 255.0;
    }

    // sRGB(RGB 0-255) -> Oklab [l, a, b]
    function rgbToOklab(r, g, b) {
        var rl = srgbToLinearChannel(r);
        var gl = srgbToLinearChannel(g);
        var bl = srgbToLinearChannel(b);
        var l = 0.4122214708 * rl + 0.5363325363 * gl + 0.0514459929 * bl;
        var m = 0.2119034982 * rl + 0.6806995451 * gl + 0.1073969566 * bl;
        var s = 0.0883024619 * rl + 0.2817188376 * gl + 0.6299787005 * bl;
        var lr = Math.cbrt(l);
        var mr = Math.cbrt(m);
        var sr = Math.cbrt(s);
        return [
            0.2104542553 * lr + 0.7936177850 * mr - 0.0040720468 * sr,
            1.9779984951 * lr - 2.4285922050 * mr + 0.4505937099 * sr,
            0.0259040371 * lr + 0.7827717662 * mr - 0.8086757660 * sr
        ];
    }

    // Oklab -> sRGB(RGB 0-255)
    function oklabToRgb(lab) {
        var ll = lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2];
        var mm = lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2];
        var ss = lab[0] - 0.0894841775 * lab[1] - 1.2914855480 * lab[2];
        var l = ll * ll * ll;
        var m = mm * mm * mm;
        var s = ss * ss * ss;
        return [
            linearToSrgbChannel(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
            linearToSrgbChannel(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
            linearToSrgbChannel(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
        ];
    }

    var LAB_EPSILON = 216.0 / 24389.0;
    var LAB_KAPPA = 24389.0 / 27.0;

    function labForward(value) {
        return value > LAB_EPSILON ? Math.cbrt(value) : (LAB_KAPPA * value + 16.0) / 116.0;
    }

    function labInverse(value) {
        var cube = value * value * value;
        return cube > LAB_EPSILON ? cube : (116.0 * value - 16.0) / LAB_KAPPA;
    }

    // sRGB(RGB 0-255) -> CIELab（D65，L 0-100）
    function rgbToCielab(r, g, b) {
        var rl = srgbToLinearChannel(r);
        var gl = srgbToLinearChannel(g);
        var bl = srgbToLinearChannel(b);
        var x = (0.4124564 * rl + 0.3575761 * gl + 0.1804375 * bl) / 0.95047;
        var y = 0.2126729 * rl + 0.7151522 * gl + 0.0721750 * bl;
        var z = (0.0193339 * rl + 0.1191920 * gl + 0.9503041 * bl) / 1.08883;
        var fx = labForward(x);
        var fy = labForward(y);
        var fz = labForward(z);
        return [116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)];
    }

    // CIELab -> sRGB(RGB 0-255)
    function cielabToRgb(lab) {
        var fy = (lab[0] + 16.0) / 116.0;
        var fx = fy + lab[1] / 500.0;
        var fz = fy - lab[2] / 200.0;
        var x = 0.95047 * labInverse(fx);
        var y = labInverse(fy);
        var z = 1.08883 * labInverse(fz);
        var r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
        var g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
        var b = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;
        return [
            linearToSrgbChannel(r),
            linearToSrgbChannel(g),
            linearToSrgbChannel(b)
        ];
    }

    function oklabAdd(f, s) {
        return [f[0] + s[0], f[1] + s[1], f[2] + s[2]];
    }

    function oklabMultiply(v, scalar) {
        return [v[0] * scalar, v[1] * scalar, v[2] * scalar];
    }

    function oklabMix(f, s, secondWeight) {
        return oklabAdd(oklabMultiply(f, 1.0 - secondWeight), oklabMultiply(s, secondWeight));
    }

    // Oklab 加权平方距离：1.18*dl² + da² + db²
    function squaredDistance(f, s) {
        var dl = f[0] - s[0];
        var da = f[1] - s[1];
        var db = f[2] - s[2];
        return 1.18 * dl * dl + da * da + db * db;
    }

    // 归一化 RGB 平方距离（0-1）
    function squaredRgbDistance(f, s) {
        var dr = f[0] - s[0];
        var dg = f[1] - s[1];
        var db = f[2] - s[2];
        return (dr * dr + dg * dg + db * db) / (3.0 * 255.0 * 255.0);
    }

    function chroma(lab) {
        return Math.hypot(lab[1], lab[2]);
    }

    // =====================================================================
    // upstream（V5 seed）色彩工具：带 round、0.008856/7.787 阈值
    // =====================================================================
    function upstreamRgbToLab(r, g, b) {
        r = r / 255;
        g = g / 255;
        b = b / 255;
        r = r > 0.04045 ? Math.pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
        g = g > 0.04045 ? Math.pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
        b = b > 0.04045 ? Math.pow((b + 0.055) / 1.055, 2.4) : b / 12.92;
        var x = (r * 0.4124564 + g * 0.3575761 + b * 0.1804375) * 100;
        var y = (r * 0.2126729 + g * 0.7151522 + b * 0.0721750) * 100;
        var z = (r * 0.0193339 + g * 0.1191920 + b * 0.9503041) * 100;
        x = x / 95.047;
        y = y / 100.0;
        z = z / 108.883;
        x = x > 0.008856 ? Math.pow(x, 1 / 3) : 7.787 * x + 16 / 116;
        y = y > 0.008856 ? Math.pow(y, 1 / 3) : 7.787 * y + 16 / 116;
        z = z > 0.008856 ? Math.pow(z, 1 / 3) : 7.787 * z + 16 / 116;
        return [116 * y - 16, 500 * (x - y), 200 * (y - z)];
    }

    function upstreamLabToRgb(L, a, b) {
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

    // upstream findClosestColorRGB：跳过 index 4（reserved）
    function upstreamClosest(r, g, b) {
        var minDist = Infinity;
        var closest = 1; // 默认白
        for (var i = 0; i < 7; i++) {
            if (i === 4) continue;
            var p = UPSTREAM_PERCEIVED[i];
            var dr = r - p[0];
            var dg = g - p[1];
            var db = b - p[2];
            var dist = dr * dr + dg * dg + db * db;
            if (dist < minDist) {
                minDist = dist;
                closest = i;
            }
        }
        return closest;
    }

    // =====================================================================
    // balanced tone map（C++ 338-352）：CIELab L 压缩到面板黑/白范围 -> Oklab
    // =====================================================================
    function balancedToneMap(imageData) {
        var width = imageData.width;
        var height = imageData.height;
        var src = imageData.data;
        var n = width * height;
        var blackL = rgbToCielab(PALETTE_PERCEIVED[0][0], PALETTE_PERCEIVED[0][1], PALETTE_PERCEIVED[0][2])[0];
        var whiteL = rgbToCielab(PALETTE_PERCEIVED[1][0], PALETTE_PERCEIVED[1][1], PALETTE_PERCEIVED[1][2])[0];
        var toneL = new Float64Array(n);
        var toneA = new Float64Array(n);
        var toneB = new Float64Array(n);
        for (var i = 0; i < n; i++) {
            var idx = i * 4;
            var lab = rgbToCielab(src[idx], src[idx + 1], src[idx + 2]);
            lab[0] = blackL + (lab[0] / 100.0) * (whiteL - blackL);
            var rgb = cielabToRgb(lab);
            var oklab = rgbToOklab(rgb[0], rgb[1], rgb[2]);
            toneL[i] = oklab[0];
            toneA[i] = oklab[1];
            toneB[i] = oklab[2];
        }
        return { toneL: toneL, toneA: toneA, toneB: toneB };
    }

    // =====================================================================
    // box_blur（C++ 362-415，通道盒式模糊，滑动窗口 O(n)）
    // =====================================================================
    function boxBlur(input, width, height, radius) {
        var n = width * height;
        if (radius <= 0) {
            return input;
        }
        var horizontal = new Float64Array(n);
        var output = new Float64Array(n);
        for (var y = 0; y < height; y++) {
            var sum = 0;
            var count = 0;
            for (var x = -radius; x <= radius; x++) {
                if (x >= 0 && x < width) {
                    sum += input[y * width + x];
                    count++;
                }
            }
            for (var x2 = 0; x2 < width; x2++) {
                horizontal[y * width + x2] = sum / count;
                var removeX = x2 - radius;
                var addX = x2 + radius + 1;
                if (removeX >= 0) {
                    sum -= input[y * width + removeX];
                    count--;
                }
                if (addX < width) {
                    sum += input[y * width + addX];
                    count++;
                }
            }
        }
        for (var x3 = 0; x3 < width; x3++) {
            var sum2 = 0;
            var count2 = 0;
            for (var y2 = -radius; y2 <= radius; y2++) {
                if (y2 >= 0 && y2 < height) {
                    sum2 += horizontal[y2 * width + x3];
                    count2++;
                }
            }
            for (var y3 = 0; y3 < height; y3++) {
                output[y3 * width + x3] = sum2 / count2;
                var removeY = y3 - radius;
                var addY = y3 + radius + 1;
                if (removeY >= 0) {
                    sum2 -= horizontal[removeY * width + x3];
                    count2--;
                }
                if (addY < height) {
                    sum2 += horizontal[addY * width + x3];
                    count2++;
                }
            }
        }
        return output;
    }

    // =====================================================================
    // sobel_magnitude（C++ 417-433，输入 Oklab L 0-1，输出 /8）
    // =====================================================================
    function sobelMagnitude(input, width, height) {
        var n = width * height;
        var output = new Float64Array(n);
        for (var y = 1; y + 1 < height; y++) {
            for (var x = 1; x + 1 < width; x++) {
                var at = function (dx, dy) {
                    return input[(y + dy) * width + (x + dx)];
                };
                var gx = -at(-1, -1) + at(1, -1) - 2.0 * at(-1, 0) + 2.0 * at(1, 0) - at(-1, 1) + at(1, 1);
                var gy = -at(-1, -1) - 2.0 * at(0, -1) - at(1, -1) + at(-1, 1) + 2.0 * at(0, 1) + at(1, 1);
                output[y * width + x] = Math.hypot(gx, gy) / 8.0;
            }
        }
        return output;
    }

    // =====================================================================
    // bilateral_filter（C++ 435-492；radius=0 时原样返回）
    // =====================================================================
    function bilateralFilter(toneL, toneA, toneB, width, height, config) {
        if (config.bilateralRadius <= 0) {
            return { toneL: toneL, toneA: toneA, toneB: toneB };
        }
        var radius = config.bilateralRadius;
        var diameter = radius * 2 + 1;
        var spatial = new Float64Array(diameter * diameter);
        for (var dy = -radius; dy <= radius; dy++) {
            for (var dx = -radius; dx <= radius; dx++) {
                var d2 = dx * dx + dy * dy;
                spatial[(dy + radius) * diameter + dx + radius] =
                    Math.exp(-d2 / (2.0 * config.bilateralSigmaSpatial * config.bilateralSigmaSpatial));
            }
        }
        var invL = 1.0 / (2.0 * config.bilateralSigmaL * config.bilateralSigmaL);
        var invC = 1.0 / (2.0 * config.bilateralSigmaChroma * config.bilateralSigmaChroma);
        var n = width * height;
        var outL = new Float64Array(n);
        var outA = new Float64Array(n);
        var outB = new Float64Array(n);
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                var pixel = y * width + x;
                var centerL = toneL[pixel];
                var centerA = toneA[pixel];
                var centerB = toneB[pixel];
                var totalL = 0, totalA = 0, totalB = 0, totalWeight = 0;
                for (var fy = -radius; fy <= radius; fy++) {
                    var ny = y + fy;
                    if (ny < 0 || ny >= height) continue;
                    for (var fx = -radius; fx <= radius; fx++) {
                        var nx = x + fx;
                        if (nx < 0 || nx >= width) continue;
                        var idx = ny * width + nx;
                        var dl = toneL[idx] - centerL;
                        var da = toneA[idx] - centerA;
                        var db = toneB[idx] - centerB;
                        var range = Math.exp(-(dl * dl * invL + (da * da + db * db) * invC));
                        var weight = spatial[(fy + radius) * diameter + fx + radius] * range;
                        totalL += toneL[idx] * weight;
                        totalA += toneA[idx] * weight;
                        totalB += toneB[idx] * weight;
                        totalWeight += weight;
                    }
                }
                outL[pixel] = totalL / totalWeight;
                outA[pixel] = totalA / totalWeight;
                outB[pixel] = totalB / totalWeight;
            }
        }
        return { toneL: outL, toneA: outA, toneB: outB };
    }

    // =====================================================================
    // posterize（C++ 642-668）
    // =====================================================================
    function posterize(lab, config, panelBlack, panelWhite) {
        var qL = lab[0];
        var qA = lab[1];
        var qB = lab[2];
        if (config.posterLLevels > 1) {
            var normalized = clamp((lab[0] - panelBlack) / (panelWhite - panelBlack), 0.0, 1.0);
            var level = Math.round(normalized * (config.posterLLevels - 1)) / (config.posterLLevels - 1);
            qL = panelBlack + level * (panelWhite - panelBlack);
        }
        var sourceChroma = chroma(lab);
        if (sourceChroma > 1e-8 && config.posterCLevels > 1) {
            var kChromaCeiling = 0.32;
            var normC = clamp(sourceChroma / kChromaCeiling, 0.0, 1.0);
            var quantizedChroma = Math.round(normC * (config.posterCLevels - 1)) / (config.posterCLevels - 1) * kChromaCeiling;
            var hue = Math.atan2(lab[2], lab[1]);
            if (config.posterHBins > 1) {
                var step = 2.0 * PI / config.posterHBins;
                hue = Math.round(hue / step) * step;
            }
            qA = quantizedChroma * Math.cos(hue);
            qB = quantizedChroma * Math.sin(hue);
        }
        return oklabMix(lab, [qL, qA, qB], config.posterBlend);
    }

    // =====================================================================
    // classify_hue / allowed_mask（C++ 670-702）
    // =====================================================================
    function classifyHue(lab, neutralThreshold) {
        if (chroma(lab) < neutralThreshold) {
            return 0;
        }
        var hue = Math.atan2(lab[2], lab[1]) * 180.0 / PI;
        if (hue >= -30.0 && hue < 115.0) return 1;   // warm
        if (hue >= 115.0) return 2;                  // green
        if (hue < -55.0) return 3;                   // cool
        return 4;                                    // magenta
    }

    function allowedMask(hueClass) {
        var blackWhite = (1 << 0) | (1 << 1);
        switch (hueClass) {
            case 0: return blackWhite;
            case 1: return blackWhite | (1 << 2) | (1 << 3);
            case 2: return blackWhite | (1 << 2) | (1 << 4);
            case 3: return blackWhite | (1 << 4) | (1 << 5);
            case 4: return blackWhite | (1 << 3) | (1 << 5);
        }
        return blackWhite;
    }

    // =====================================================================
    // nearest_palette_rgb（C++ 704-724，target 为 RGB，perceived 距离 + 越色相代价）
    // =====================================================================
    function nearestPaletteRgb(target, availableMask, preferredMask, crossHuePenalty) {
        var best = 0;
        var bestScore = Infinity;
        for (var i = 0; i < 6; i++) {
            if ((availableMask & (1 << i)) === 0) {
                continue;
            }
            var p = PALETTE_PERCEIVED[i];
            var score = squaredRgbDistance(target, p);
            if ((preferredMask & (1 << i)) === 0) {
                score += crossHuePenalty;
            }
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        return best;
    }

    // =====================================================================
    // dilate_mask（C++ 726-750，0/1 Uint8Array 膨胀）
    // =====================================================================
    function dilateMask(input, width, height, radius) {
        var n = width * height;
        var output = new Uint8Array(n);
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                var found = false;
                for (var dy = -radius; dy <= radius && !found; dy++) {
                    var ny = y + dy;
                    if (ny < 0 || ny >= height) continue;
                    for (var dx = -radius; dx <= radius; dx++) {
                        var nx = x + dx;
                        if (nx >= 0 && nx < width && input[ny * width + nx]) {
                            found = true;
                            break;
                        }
                    }
                }
                output[y * width + x] = found ? 1 : 0;
            }
        }
        return output;
    }

    // =====================================================================
    // detect_film_strips（C++ 757-783；普通照片返回 left=0 right=width）
    // =====================================================================
    function detectFilmStrips(toneL, width, height, panelBlack) {
        var darkFraction = new Float64Array(width);
        for (var x = 0; x < width; x++) {
            var dark = 0;
            for (var y = 0; y < height; y++) {
                if (toneL[y * width + x] < panelBlack + 0.20) {
                    dark++;
                }
            }
            darkFraction[x] = dark / height;
        }
        var leftExclusive = 0;
        var rightBegin = width;
        while (leftExclusive < Math.floor(width / 4) && darkFraction[leftExclusive] > 0.60) {
            leftExclusive++;
        }
        while (rightBegin > Math.floor(width * 3 / 4) && darkFraction[rightBegin - 1] > 0.60) {
            rightBegin--;
        }
        if (leftExclusive < 8) leftExclusive = 0;
        if (rightBegin > width - 8) rightBegin = width;
        return { leftExclusive: leftExclusive, rightBegin: rightBegin };
    }

    // =====================================================================
    // V5 seed（balanced_fit128_seed.mjs：compressDynamicRange + perceived FS 非蛇形）
    // 返回 0-5 六色 label
    // =====================================================================
    function v5SeedDither(imageData) {
        var width = imageData.width;
        var height = imageData.height;
        var src = imageData.data;
        var n = width * height;

        // 1. compressDynamicRange（upstream balanced preset）
        var blackL = upstreamRgbToLab(2, 2, 2)[0];
        var whiteL = upstreamRgbToLab(190, 200, 200)[0];
        var comp = new Uint8ClampedArray(n * 3);
        for (var i = 0; i < n; i++) {
            var idx = i * 4;
            var lab = upstreamRgbToLab(src[idx], src[idx + 1], src[idx + 2]);
            var compressedL = blackL + (lab[0] / 100.0) * (whiteL - blackL);
            var rgb = upstreamLabToRgb(compressedL, lab[1], lab[2]);
            comp[i * 3] = rgb[0];
            comp[i * 3 + 1] = rgb[1];
            comp[i * 3 + 2] = rgb[2];
        }

        // 2. Floyd-Steinberg 误差扩散（非蛇形，perceived 色板，RGB 距离选色）
        var errors = new Float64Array(n * 3);
        var labels = new Uint8Array(n);
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                var p = y * width + x;
                var e = p * 3;
                var wR = clamp(comp[e] + errors[e], 0, 255);
                var wG = clamp(comp[e + 1] + errors[e + 1], 0, 255);
                var wB = clamp(comp[e + 2] + errors[e + 2], 0, 255);
                var colorIdx = upstreamClosest(wR, wG, wB);
                labels[p] = UPSTREAM_TO_LABEL[colorIdx];
                var ditherColor = UPSTREAM_PERCEIVED[colorIdx];
                var errR = wR - ditherColor[0];
                var errG = wG - ditherColor[1];
                var errB = wB - ditherColor[2];
                for (var t = 0; t < FS_TAPS.length; t++) {
                    var tap = FS_TAPS[t];
                    var nx = x + tap[0];
                    var ny = y + tap[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    var ni = (ny * width + nx) * 3;
                    errors[ni] += errR * tap[2];
                    errors[ni + 1] += errG * tap[2];
                    errors[ni + 2] += errB * tap[2];
                }
            }
        }
        return labels;
    }

    // =====================================================================
    // quantize（C++ 791-2055 完整移植，ForFilm 普通图无胶片边框）
    // =====================================================================
    function quantize(tone, grad, filmStrips, config, perceived, seedLabels) {
        var width = tone.width;
        var height = tone.height;
        var n = width * height;
        var toneL = tone.toneL, toneA = tone.toneA, toneB = tone.toneB;

        var panelBlack = perceived[0][0];
        var panelWhite = perceived[1][0];

        // ---- base（C++ 811-828）----
        // radius=0 -> filtered = tone；detailKeepFlat=1.0 -> keep=1；posterBlend=0 -> base = tone
        var baseL = new Float64Array(n);
        var baseA = new Float64Array(n);
        var baseB = new Float64Array(n);
        var baseR = new Float64Array(n);
        var baseG = new Float64Array(n);
        var baseBlu = new Float64Array(n);
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                var pixel = y * width + x;
                var structure = smoothstep(config.edgeLow, config.edgeHigh, grad[pixel]);
                var keep = config.detailKeepFlat + (1.0 - config.detailKeepFlat) * structure;
                var reduced = oklabMix(
                    [toneL[pixel], toneA[pixel], toneB[pixel]],
                    [toneL[pixel], toneA[pixel], toneB[pixel]],
                    keep
                );
                var baseLab = posterize(reduced, config, panelBlack, panelWhite);
                baseL[pixel] = baseLab[0];
                baseA[pixel] = baseLab[1];
                baseB[pixel] = baseLab[2];
                var rgb = oklabToRgb(baseLab);
                baseR[pixel] = rgb[0];
                baseG[pixel] = rgb[1];
                baseBlu[pixel] = rgb[2];
            }
        }

        // ---- classes / masks / deep_black / text_seed（C++ 830-877）----
        var classes = new Uint8Array(n);
        var preferred = new Uint8Array(n);
        var available = new Uint8Array(n);
        var deepBlack = new Uint8Array(n);
        var textSeed = new Uint8Array(n);
        for (var y2 = 0; y2 < height; y2++) {
            for (var x2 = 0; x2 < width; x2++) {
                var p2 = y2 * width + x2;
                var baseLab2 = [baseL[p2], baseA[p2], baseB[p2]];
                classes[p2] = classifyHue(baseLab2, config.neutralChroma);
                preferred[p2] = allowedMask(classes[p2]);
                available[p2] = 0x3F;
                deepBlack[p2] = toneL[p2] < panelBlack + config.deepBlackMargin ? 1 : 0;
                if (deepBlack[p2]) {
                    preferred[p2] = 1 << 0;
                    available[p2] = 1 << 0;
                }
                var localMin = toneL[p2];
                for (var dy = -1; dy <= 1; dy++) {
                    var ny2 = y2 + dy;
                    if (ny2 < 0 || ny2 >= height) continue;
                    for (var dx = -1; dx <= 1; dx++) {
                        var nx2 = x2 + dx;
                        if (nx2 >= 0 && nx2 < width) {
                            localMin = Math.min(localMin, toneL[ny2 * width + nx2]);
                        }
                    }
                }
                textSeed[p2] =
                    chroma([toneL[p2], toneA[p2], toneB[p2]]) > 0.060 &&
                    grad[p2] > config.edgeLow &&
                    toneL[p2] - localMin > 0.035 ? 1 : 0;
            }
        }

        var textNeighborhood = dilateMask(textSeed, width, height, 1);
        var textProtect = new Uint8Array(n);
        for (var p3 = 0; p3 < n; p3++) {
            textProtect[p3] =
                textNeighborhood[p3] &&
                chroma([toneL[p3], toneA[p3], toneB[p3]]) > config.neutralChroma * 1.25 ? 1 : 0;
            if (textProtect[p3]) {
                available[p3] = preferred[p3];
            }
        }

        // ---- 结构感知误差扩散（serpentine FS，C++ 879-998）----
        var errorsR = new Float64Array(n);
        var errorsG = new Float64Array(n);
        var errorsB = new Float64Array(n);
        var labels = new Uint8Array(n);
        for (var y3 = 0; y3 < height; y3++) {
            var reverse = (y3 & 1) !== 0;
            for (var step = 0; step < width; step++) {
                var x3 = reverse ? width - 1 - step : step;
                var px = y3 * width + x3;
                var structure2 = smoothstep(config.edgeLow, config.edgeHigh, grad[px]);
                var protectedPixel = textProtect[px] || structure2 > 0.82 || deepBlack[px];
                var errorApply = protectedPixel ? 0.0 : (1.0 - structure2);
                var working = [
                    clamp(baseR[px] + errorsR[px] * errorApply, 0.0, 255.0),
                    clamp(baseG[px] + errorsG[px] * errorApply, 0.0, 255.0),
                    clamp(baseBlu[px] + errorsB[px] * errorApply, 0.0, 255.0)
                ];
                var selected = nearestPaletteRgb(working, available[px], preferred[px], config.crossHuePenalty);
                labels[px] = selected;
                if (protectedPixel) {
                    continue;
                }
                var per = PALETTE_PERCEIVED[selected];
                var errR = working[0] - per[0];
                var errG = working[1] - per[1];
                var errB = working[2] - per[2];
                var localL = config.ditherL * (1.0 - structure2);
                var localC = config.ditherC * (1.0 - structure2);
                var luminanceError = 0.2126 * errR + 0.7152 * errG + 0.0722 * errB;
                errR = clamp(luminanceError * localL + (errR - luminanceError) * localC, -255.0, 255.0);
                errG = clamp(luminanceError * localL + (errG - luminanceError) * localC, -255.0, 255.0);
                errB = clamp(luminanceError * localL + (errB - luminanceError) * localC, -255.0, 255.0);
                for (var t = 0; t < FS_TAPS.length; t++) {
                    var tap = FS_TAPS[t];
                    var ddx = reverse ? -tap[0] : tap[0];
                    var nx3 = x3 + ddx;
                    var ny3 = y3 + tap[1];
                    if (nx3 < 0 || nx3 >= width || ny3 < 0 || ny3 >= height) continue;
                    var target = ny3 * width + nx3;
                    if (textProtect[target] || deepBlack[target]) continue;
                    var dl = Math.abs(baseL[px] - baseL[target]);
                    var dc = Math.hypot(baseA[px] - baseA[target], baseB[px] - baseB[target]);
                    var barrier = Math.exp(-dl / config.errorBarrierL - dc / config.errorBarrierC);
                    if (classes[px] !== classes[target]) {
                        barrier *= 0.08;
                    }
                    var targetStructure = smoothstep(config.edgeLow, config.edgeHigh, grad[target]);
                    barrier *= (1.0 - 0.92 * targetStructure);
                    var weight = tap[2] * barrier;
                    errorsR[target] += errR * weight;
                    errorsG[target] += errG * weight;
                    errorsB[target] += errB * weight;
                }
            }
        }

        // ---- seed refine（C++ 1000-1117）----
        if (seedLabels !== null) {
            labels.set(seedLabels);
            var leftTextLimit = Math.ceil(filmStrips.leftExclusive * config.leftTextOuterFraction);
            var leftText = new Uint8Array(n);
            var rightText = new Uint8Array(n);
            var perceivedLuma = function (color) {
                return 0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2];
            };
            var blackLuma = perceivedLuma(PALETTE_PERCEIVED[0]);
            var yellowLuma = perceivedLuma(PALETTE_PERCEIVED[2]);
            for (var y4 = 0; y4 < height; y4++) {
                for (var x4 = 0; x4 < width; x4++) {
                    var p4 = y4 * width + x4;
                    if (x4 < leftTextLimit) {
                        leftText[p4] =
                            chroma([toneL[p4], toneA[p4], toneB[p4]]) > config.leftTextChroma &&
                            toneB[p4] > config.leftTextB &&
                            toneL[p4] > panelBlack + config.leftTextLMargin ? 1 : 0;
                    }
                    if (x4 >= filmStrips.rightBegin) {
                        rightText[p4] =
                            chroma([toneL[p4], toneA[p4], toneB[p4]]) > 0.065 &&
                            toneA[p4] > 0.030 &&
                            toneL[p4] > panelBlack + 0.045 ? 1 : 0;
                    }
                }
            }
            var allText = new Uint8Array(n);
            for (var p5 = 0; p5 < n; p5++) {
                allText[p5] = (leftText[p5] || rightText[p5]) ? 1 : 0;
            }
            var textGuard = dilateMask(allText, width, height, 2);

            for (var pass = 0; pass < config.refinePasses; pass++) {
                var refined = new Uint8Array(labels);
                for (var y5 = 0; y5 < height; y5++) {
                    for (var x5 = 0; x5 < width; x5++) {
                        var p6 = y5 * width + x5;
                        var border = x5 < filmStrips.leftExclusive || x5 >= filmStrips.rightBegin;
                        if (leftText[p6]) {
                            var seedLabel = seedLabels[p6];
                            if (seedLabel === 2) {
                                refined[p6] = 2;
                            } else {
                                var seedLuma = perceivedLuma(PALETTE_PERCEIVED[seedLabel]);
                                var baseCoverage = clamp(
                                    (seedLuma - blackLuma) / Math.max(1e-9, yellowLuma - blackLuma),
                                    0.0, 1.0
                                );
                                var coverage = clamp(baseCoverage * config.leftTextEdgeScale, 0.0, 1.0);
                                var threshold = (BAYER8[(y5 & 7) * 8 + (x5 & 7)] + 0.5) / 64.0;
                                refined[p6] = coverage > threshold ? 2 : 0;
                            }
                            continue;
                        }
                        if (rightText[p6]) {
                            refined[p6] = 3;
                            continue;
                        }
                        if (border && toneL[p6] < panelBlack + 0.155) {
                            refined[p6] = 0;
                            continue;
                        }
                        if (x5 === 0 || y5 === 0 || x5 + 1 === width || y5 + 1 === height ||
                            grad[p6] >= config.refineEdge ||
                            toneL[p6] < config.refineMinL || textGuard[p6] ||
                            config.refineSameNeighbors < 0) {
                            continue;
                        }
                        var current = labels[p6];
                        if (current < 2 || (preferred[p6] & (1 << current)) !== 0) {
                            continue;
                        }
                        var same = 0;
                        for (var dy2 = -1; dy2 <= 1; dy2++) {
                            for (var dx2 = -1; dx2 <= 1; dx2++) {
                                if (dx2 === 0 && dy2 === 0) continue;
                                same += labels[(y5 + dy2) * width + (x5 + dx2)] === current ? 1 : 0;
                            }
                        }
                        if (same > config.refineSameNeighbors) {
                            continue;
                        }
                        var replacement = nearestPaletteRgb(
                            [baseR[p6], baseG[p6], baseBlu[p6]], preferred[p6], preferred[p6], 0.0
                        );
                        var currentError = squaredRgbDistance(
                            [baseR[p6], baseG[p6], baseBlu[p6]], PALETTE_PERCEIVED[current]
                        );
                        var replacementError = squaredRgbDistance(
                            [baseR[p6], baseG[p6], baseBlu[p6]], PALETTE_PERCEIVED[replacement]
                        );
                        if (replacementError <= currentError + config.refineTolerance) {
                            refined[p6] = replacement;
                        }
                    }
                }
                labels = refined;
            }
        }

        // ---- sparkle sweeps（C++ 1128-1309；sparkleSweeps=0 跳过）----
        if (config.sparkleSweeps > 0) {
            var isBrightLabel = function (label) {
                return perceived[label][0] >= config.sparkleBrightL;
            };
            var brightNeighbors = function (pixel, excluded) {
                var bx = pixel % width;
                var by = Math.floor(pixel / width);
                var count = 0;
                for (var dy3 = -1; dy3 <= 1; dy3++) {
                    for (var dx3 = -1; dx3 <= 1; dx3++) {
                        if (dx3 === 0 && dy3 === 0) continue;
                        var nx4 = bx + dx3;
                        var ny4 = by + dy3;
                        if (nx4 < 0 || nx4 >= width || ny4 < 0 || ny4 >= height) continue;
                        if ((ny4 * width + nx4) !== excluded && isBrightLabel(labels[ny4 * width + nx4])) {
                            count++;
                        }
                    }
                }
                return count;
            };
            var salientSingletonSweep = function (pixel) {
                var sx = pixel % width;
                var sy = Math.floor(pixel / width);
                if (sx === 0 || sy === 0 || sx + 1 === width || sy + 1 === height ||
                    sx < filmStrips.leftExclusive || sx >= filmStrips.rightBegin ||
                    textProtect[pixel] || deepBlack[pixel] ||
                    grad[pixel] >= config.sparkleEdge ||
                    !isBrightLabel(labels[pixel]) ||
                    brightNeighbors(pixel, n) !== 0) {
                    return false;
                }
                var outputNeighborL = 0.0;
                var sourceNeighborL = 0.0;
                var count = 0;
                for (var dy4 = -1; dy4 <= 1; dy4++) {
                    for (var dx4 = -1; dx4 <= 1; dx4++) {
                        if (dx4 === 0 && dy4 === 0) continue;
                        var nb = (sy + dy4) * width + (sx + dx4);
                        outputNeighborL += perceived[labels[nb]][0];
                        sourceNeighborL += toneL[nb];
                        count++;
                    }
                }
                outputNeighborL /= count;
                sourceNeighborL /= count;
                var outputSpike = perceived[labels[pixel]][0] - outputNeighborL >= config.sparkleLumaDelta;
                var realSourceHighlight = toneL[pixel] - sourceNeighborL > config.sparkleSourceDelta;
                return outputSpike && !realSourceHighlight;
            };
            var dataCost = function (pixel, label) {
                var cost = squaredDistance([baseL[pixel], baseA[pixel], baseB[pixel]], perceived[label]);
                if ((preferred[pixel] & (1 << label)) === 0) {
                    cost += config.crossHuePenalty;
                }
                return cost;
            };
            for (var sweep = 0; sweep < config.sparkleSweeps; sweep++) {
                var used = new Uint8Array(n);
                var reverseRows = (sweep & 1) !== 0;
                for (var rowStep = 1; rowStep + 1 < height; rowStep++) {
                    var sy2 = reverseRows ? height - 1 - rowStep : rowStep;
                    var reverseColumns = ((sy2 + sweep) & 1) !== 0;
                    for (var colStep = filmStrips.leftExclusive; colStep < filmStrips.rightBegin; colStep++) {
                        var sx2 = reverseColumns
                            ? filmStrips.rightBegin - 1 - (colStep - filmStrips.leftExclusive)
                            : colStep;
                        var sp = sy2 * width + sx2;
                        if (used[sp] || !salientSingletonSweep(sp)) continue;
                        var bestAnchor = -1;
                        var bestDestination = -1;
                        var bestScore = Infinity;
                        for (var ay = sy2 - config.sparkleRadius; ay <= sy2 + config.sparkleRadius; ay++) {
                            if (ay <= 0 || ay + 1 >= height) continue;
                            for (var ax = sx2 - config.sparkleRadius; ax <= sx2 + config.sparkleRadius; ax++) {
                                if (ax < filmStrips.leftExclusive || ax >= filmStrips.rightBegin ||
                                    (ax === sx2 && ay === sy2)) continue;
                                var anchor = ay * width + ax;
                                if (used[anchor] || !salientSingletonSweep(anchor)) continue;
                                for (var dy5 = -1; dy5 <= 1; dy5++) {
                                    for (var dx5 = -1; dx5 <= 1; dx5++) {
                                        if (dx5 === 0 && dy5 === 0) continue;
                                        var qx = ax + dx5;
                                        var qy = ay + dy5;
                                        if (qx < filmStrips.leftExclusive || qx >= filmStrips.rightBegin ||
                                            qy <= 0 || qy + 1 >= height) continue;
                                        var dest = qy * width + qx;
                                        if (dest === sp || used[dest] ||
                                            isBrightLabel(labels[dest]) ||
                                            textProtect[dest] || deepBlack[dest] ||
                                            grad[dest] >= config.sparkleEdge ||
                                            classes[sp] !== classes[dest] ||
                                            (preferred[sp] & (1 << labels[dest])) === 0 ||
                                            (preferred[dest] & (1 << labels[sp])) === 0) {
                                            continue;
                                        }
                                        var sourceDistance = Math.sqrt(squaredDistance(
                                            [baseL[sp], baseA[sp], baseB[sp]],
                                            [baseL[dest], baseA[dest], baseB[dest]]
                                        ));
                                        if (sourceDistance > config.sparkleSourceDistance ||
                                            baseL[dest] + config.sparkleSourceDelta < baseL[sp]) {
                                            continue;
                                        }
                                        var dataDelta =
                                            dataCost(sp, labels[dest]) + dataCost(dest, labels[sp]) -
                                            dataCost(sp, labels[sp]) - dataCost(dest, labels[dest]);
                                        if (dataDelta > config.sparkleDataTolerance) continue;
                                        var score = dataDelta + 0.10 * sourceDistance -
                                            0.04 * (baseL[dest] - baseL[sp]);
                                        if (score < bestScore) {
                                            bestScore = score;
                                            bestAnchor = anchor;
                                            bestDestination = dest;
                                        }
                                    }
                                }
                            }
                        }
                        if (bestDestination !== -1) {
                            var tmp = labels[sp];
                            labels[sp] = labels[bestDestination];
                            labels[bestDestination] = tmp;
                            used[sp] = 1;
                            used[bestDestination] = 1;
                            used[bestAnchor] = 1;
                        }
                    }
                }
            }
        }

        // ---- sparkle cleanup（C++ 1317-1438）----
        if (config.sparkleCleanupPasses > 0) {
            var isBright2 = function (label) {
                return perceived[label][0] >= config.sparkleBrightL;
            };
            var salientSingletonCleanup = function (pixel) {
                var sx = pixel % width;
                var sy = Math.floor(pixel / width);
                if (sx === 0 || sy === 0 || sx + 1 === width || sy + 1 === height ||
                    sx < filmStrips.leftExclusive || sx >= filmStrips.rightBegin ||
                    textProtect[pixel] || deepBlack[pixel] ||
                    grad[pixel] >= config.sparkleEdge ||
                    !isBright2(labels[pixel])) {
                    return false;
                }
                var nearbyBright = 0;
                var outputNeighborL = 0.0;
                var sourceNeighborL = 0.0;
                for (var dy6 = -1; dy6 <= 1; dy6++) {
                    for (var dx6 = -1; dx6 <= 1; dx6++) {
                        if (dx6 === 0 && dy6 === 0) continue;
                        var nb2 = (sy + dy6) * width + (sx + dx6);
                        if (isBright2(labels[nb2])) nearbyBright++;
                        outputNeighborL += perceived[labels[nb2]][0];
                        sourceNeighborL += toneL[nb2];
                    }
                }
                outputNeighborL /= 8.0;
                sourceNeighborL /= 8.0;
                return nearbyBright === 0 &&
                    perceived[labels[pixel]][0] - outputNeighborL >= config.sparkleLumaDelta &&
                    toneL[pixel] - sourceNeighborL <= config.sparkleSourceDelta;
            };
            for (var pass2 = 0; pass2 < config.sparkleCleanupPasses; pass2++) {
                var cleaned = new Uint8Array(labels);
                for (var y6 = 1; y6 + 1 < height; y6++) {
                    for (var x6 = Math.max(1, filmStrips.leftExclusive); x6 + 1 < filmStrips.rightBegin; x6++) {
                        var sp2 = y6 * width + x6;
                        if (!salientSingletonCleanup(sp2)) continue;
                        var radius = config.sparkleCleanupWindow;
                        var srcTotal = [0.0, 0.0, 0.0];
                        var outTotal = [0.0, 0.0, 0.0];
                        var localCounts = [0, 0, 0, 0, 0, 0];
                        var count2 = 0;
                        for (var dy7 = -radius; dy7 <= radius; dy7++) {
                            var ny5 = y6 + dy7;
                            if (ny5 < 0 || ny5 >= height) continue;
                            for (var dx7 = -radius; dx7 <= radius; dx7++) {
                                var nx5 = x6 + dx7;
                                if (nx5 < filmStrips.leftExclusive || nx5 >= filmStrips.rightBegin) continue;
                                var nb3 = ny5 * width + nx5;
                                srcTotal[0] += baseL[nb3];
                                srcTotal[1] += baseA[nb3];
                                srcTotal[2] += baseB[nb3];
                                var pl = perceived[labels[nb3]];
                                outTotal[0] += pl[0];
                                outTotal[1] += pl[1];
                                outTotal[2] += pl[2];
                                localCounts[labels[nb3]]++;
                                count2++;
                            }
                        }
                        var sourceAverage = [
                            srcTotal[0] / count2, srcTotal[1] / count2, srcTotal[2] / count2
                        ];
                        var outputAverage = [
                            outTotal[0] / count2, outTotal[1] / count2, outTotal[2] / count2
                        ];
                        var beforeError = squaredDistance(sourceAverage, outputAverage);
                        var current2 = labels[sp2];
                        var best2 = current2;
                        var bestError = beforeError;
                        for (var candidate = 0; candidate < 6; candidate++) {
                            if (candidate === current2 || isBright2(candidate) ||
                                localCounts[candidate] === 0 ||
                                (preferred[sp2] & (1 << candidate)) === 0) {
                                continue;
                            }
                            var shifted = [
                                (outTotal[0] - perceived[current2][0] + perceived[candidate][0]) / count2,
                                (outTotal[1] - perceived[current2][1] + perceived[candidate][1]) / count2,
                                (outTotal[2] - perceived[current2][2] + perceived[candidate][2]) / count2
                            ];
                            var afterError = squaredDistance(sourceAverage, shifted);
                            if (afterError <= beforeError + config.sparkleDataTolerance &&
                                afterError < bestError + config.sparkleDataTolerance) {
                                var directBefore = squaredDistance(
                                    [baseL[sp2], baseA[sp2], baseB[sp2]], perceived[current2]
                                );
                                var directAfter = squaredDistance(
                                    [baseL[sp2], baseA[sp2], baseB[sp2]], perceived[candidate]
                                );
                                var score2 = afterError + 0.002 * directAfter;
                                var bestScore2 = bestError +
                                    0.002 * squaredDistance([baseL[sp2], baseA[sp2], baseB[sp2]], perceived[best2]);
                                if (score2 < bestScore2 && directAfter <= directBefore + 0.12) {
                                    best2 = candidate;
                                    bestError = afterError;
                                }
                            }
                        }
                        cleaned[sp2] = best2;
                    }
                }
                labels = cleaned;
            }
        }

        // ---- chroma relocate（C++ 1445-1543；0 跳过）----
        if (config.chromaRelocateSweeps > 0) {
            var dataCost2 = function (pixel, label) {
                var cost = squaredDistance([baseL[pixel], baseA[pixel], baseB[pixel]], perceived[label]);
                if ((preferred[pixel] & (1 << label)) === 0) {
                    cost += config.crossHuePenalty;
                }
                return cost;
            };
            var sameNeighbors = function (pixel, label) {
                var bx = pixel % width;
                var by = Math.floor(pixel / width);
                var same2 = 0;
                for (var dy8 = -1; dy8 <= 1; dy8++) {
                    for (var dx8 = -1; dx8 <= 1; dx8++) {
                        if (dx8 === 0 && dy8 === 0) continue;
                        var nx6 = bx + dx8;
                        var ny6 = by + dy8;
                        if (nx6 < 0 || nx6 >= width || ny6 < 0 || ny6 >= height) continue;
                        if (labels[ny6 * width + nx6] === label) same2++;
                    }
                }
                return same2;
            };
            for (var sweep2 = 0; sweep2 < config.chromaRelocateSweeps; sweep2++) {
                var used2 = new Uint8Array(n);
                for (var y7 = 1; y7 + 1 < height; y7++) {
                    for (var x7 = Math.max(1, filmStrips.leftExclusive); x7 + 1 < filmStrips.rightBegin; x7++) {
                        var sp3 = y7 * width + x7;
                        var cur = labels[sp3];
                        if (used2[sp3] || cur < 4 || textProtect[sp3] || deepBlack[sp3] ||
                            grad[sp3] >= config.sparkleEdge ||
                            (preferred[sp3] & (1 << cur)) !== 0 ||
                            sameNeighbors(sp3, cur) > 1) {
                            continue;
                        }
                        var best3 = -1;
                        var bestScore3 = 0.0;
                        for (var dy9 = -config.chromaRelocateRadius; dy9 <= config.chromaRelocateRadius; dy9++) {
                            var qy2 = y7 + dy9;
                            if (qy2 <= 0 || qy2 + 1 >= height) continue;
                            for (var dx9 = -config.chromaRelocateRadius; dx9 <= config.chromaRelocateRadius; dx9++) {
                                var qx2 = x7 + dx9;
                                if ((dx9 === 0 && dy9 === 0) ||
                                    qx2 < filmStrips.leftExclusive || qx2 >= filmStrips.rightBegin) {
                                    continue;
                                }
                                var dest2 = qy2 * width + qx2;
                                var destLabel = labels[dest2];
                                if (used2[dest2] || destLabel === cur ||
                                    textProtect[dest2] || deepBlack[dest2] ||
                                    grad[dest2] >= config.chromaRelocateEdge ||
                                    (preferred[sp3] & (1 << destLabel)) === 0) {
                                    continue;
                                }
                                var before2 = dataCost2(sp3, cur) + dataCost2(dest2, destLabel);
                                var after2 = dataCost2(sp3, destLabel) + dataCost2(dest2, cur);
                                if (after2 > before2 + config.chromaRelocateTolerance) continue;
                                var attached = sameNeighbors(dest2, cur);
                                var score3 = after2 - before2 - 0.0015 * attached -
                                    0.002 * grad[dest2];
                                if (best3 === -1 || score3 < bestScore3) {
                                    best3 = dest2;
                                    bestScore3 = score3;
                                }
                            }
                        }
                        if (best3 !== -1) {
                            var tmp2 = labels[sp3];
                            labels[sp3] = labels[best3];
                            labels[best3] = tmp2;
                            used2[sp3] = 1;
                            used2[best3] = 1;
                        }
                    }
                }
            }
        }

        // ---- chroma cleanup（C++ 1548-1655）----
        if (config.chromaCleanupPasses > 0) {
            var chromaSingleton = function (pixel) {
                var sx = pixel % width;
                var sy = Math.floor(pixel / width);
                var current3 = labels[pixel];
                var outsideSourceHue = (preferred[pixel] & (1 << current3)) === 0;
                if (sx === 0 || sy === 0 || sx + 1 === width || sy + 1 === height ||
                    sx < filmStrips.leftExclusive || sx >= filmStrips.rightBegin ||
                    textProtect[pixel] || deepBlack[pixel] ||
                    grad[pixel] >= config.sparkleEdge || current3 < 2 || !outsideSourceHue) {
                    return false;
                }
                var same3 = 0;
                for (var dy10 = -1; dy10 <= 1; dy10++) {
                    for (var dx10 = -1; dx10 <= 1; dx10++) {
                        if (dx10 === 0 && dy10 === 0) continue;
                        if (labels[(sy + dy10) * width + (sx + dx10)] === current3) same3++;
                    }
                }
                return same3 <= config.chromaCleanupSame;
            };
            for (var pass3 = 0; pass3 < config.chromaCleanupPasses; pass3++) {
                var cleaned2 = new Uint8Array(labels);
                for (var y8 = 1; y8 + 1 < height; y8++) {
                    for (var x8 = Math.max(1, filmStrips.leftExclusive); x8 + 1 < filmStrips.rightBegin; x8++) {
                        var sp4 = y8 * width + x8;
                        if (!chromaSingleton(sp4)) continue;
                        var radius2 = config.sparkleCleanupWindow;
                        var srcTotal2 = [0.0, 0.0, 0.0];
                        var outTotal2 = [0.0, 0.0, 0.0];
                        var localCounts2 = [0, 0, 0, 0, 0, 0];
                        var count3 = 0;
                        for (var dy11 = -radius2; dy11 <= radius2; dy11++) {
                            var ny7 = y8 + dy11;
                            if (ny7 < 0 || ny7 >= height) continue;
                            for (var dx11 = -radius2; dx11 <= radius2; dx11++) {
                                var nx7 = x8 + dx11;
                                if (nx7 < filmStrips.leftExclusive || nx7 >= filmStrips.rightBegin) continue;
                                var nb4 = ny7 * width + nx7;
                                srcTotal2[0] += baseL[nb4];
                                srcTotal2[1] += baseA[nb4];
                                srcTotal2[2] += baseB[nb4];
                                var pl2 = perceived[labels[nb4]];
                                outTotal2[0] += pl2[0];
                                outTotal2[1] += pl2[1];
                                outTotal2[2] += pl2[2];
                                localCounts2[labels[nb4]]++;
                                count3++;
                            }
                        }
                        var sourceAverage2 = [
                            srcTotal2[0] / count3, srcTotal2[1] / count3, srcTotal2[2] / count3
                        ];
                        var outputAverage2 = [
                            outTotal2[0] / count3, outTotal2[1] / count3, outTotal2[2] / count3
                        ];
                        var beforeError2 = squaredDistance(sourceAverage2, outputAverage2);
                        var current4 = labels[sp4];
                        var best4 = current4;
                        var bestScore4 = beforeError2 +
                            0.002 * squaredDistance([baseL[sp4], baseA[sp4], baseB[sp4]], perceived[current4]);
                        for (var candidate2 = 0; candidate2 < 6; candidate2++) {
                            if (candidate2 === current4 || localCounts2[candidate2] === 0 ||
                                (preferred[sp4] & (1 << candidate2)) === 0 ||
                                perceived[candidate2][0] - perceived[current4][0] > 0.25) {
                                continue;
                            }
                            var shifted2 = [
                                (outTotal2[0] - perceived[current4][0] + perceived[candidate2][0]) / count3,
                                (outTotal2[1] - perceived[current4][1] + perceived[candidate2][1]) / count3,
                                (outTotal2[2] - perceived[current4][2] + perceived[candidate2][2]) / count3
                            ];
                            var afterError2 = squaredDistance(sourceAverage2, shifted2);
                            var directBefore2 = squaredDistance(
                                [baseL[sp4], baseA[sp4], baseB[sp4]], perceived[current4]
                            );
                            var directAfter2 = squaredDistance(
                                [baseL[sp4], baseA[sp4], baseB[sp4]], perceived[candidate2]
                            );
                            var score4 = afterError2 + 0.002 * directAfter2;
                            if (afterError2 <= beforeError2 + config.sparkleDataTolerance &&
                                directAfter2 <= directBefore2 + 0.08 && score4 < bestScore4) {
                                best4 = candidate2;
                                bestScore4 = score4;
                            }
                        }
                        cleaned2[sp4] = best4;
                    }
                }
                labels = cleaned2;
            }
        }

        return labels;
    }

    // =====================================================================
    // 主入口：两阶段流水线（V5 seed -> V7 -> V7.5）
    // =====================================================================
    function v7Config() {
        return {
            bilateralRadius: 0,
            bilateralSigmaSpatial: 1.35,
            bilateralSigmaL: 0.050,
            bilateralSigmaChroma: 0.060,
            detailKeepFlat: 1.0,
            edgeLow: 0.100,
            edgeHigh: 0.300,
            posterLLevels: 22,
            posterCLevels: 10,
            posterHBins: 36,
            posterBlend: 0.0,
            mergeColors: 0,
            ditherL: 1.0,
            ditherC: 1.0,
            neutralChroma: 0.035,
            deepBlackMargin: 0.070,
            errorBarrierL: 10.0,
            errorBarrierC: 10.0,
            crossHuePenalty: 0.004,
            regularizationLambda: 0.0,
            regularizationEdge: 0.040,
            regularizationSweeps: 0,
            refinePasses: 1,
            refineEdge: 0.025,
            refineMinL: 0.70,
            refineTolerance: 0.000,
            refineSameNeighbors: 0,
            leftTextOuterFraction: 0.60,
            leftTextChroma: 0.075,
            leftTextB: 0.035,
            leftTextLMargin: 0.080,
            leftTextEdgeScale: 6.00,
            sparkleSweeps: 0,
            sparkleRadius: 4,
            sparkleEdge: 0.040,
            sparkleBrightL: 0.70,
            sparkleLumaDelta: 0.18,
            sparkleSourceDelta: 0.050,
            sparkleSourceDistance: 0.075,
            sparkleDataTolerance: 0.001,
            sparkleCleanupPasses: 0,
            sparkleCleanupWindow: 1,
            chromaCleanupPasses: 0,
            chromaCleanupSame: 0,
            chromaRelocateSweeps: 0,
            chromaRelocateRadius: 4,
            chromaRelocateEdge: 0.060,
            chromaRelocateTolerance: 0.0,
            reorderTile: 0,
            reorderEdge: 0.030,
            reorderPattern: 0.060,
            dbsSweeps: 0,
            dbsRadius: 2,
            dbsSigma: 1.0,
            dbsEdge: 0.060,
            dbsDataWeight: 0.015,
            dbsHueWeight: 0.002,
            cleanupPasses: 0
        };
    }

    function v75Config() {
        var c = v7Config();
        c.refineSameNeighbors = -1;
        c.sparkleCleanupPasses = 1;
        c.chromaCleanupPasses = 1;
        return c;
    }

    // perceived 色板转 Oklab（C++ main 2823-2826）
    function perceivedToOklab() {
        var arr = [];
        for (var i = 0; i < 6; i++) {
            var p = PALETTE_PERCEIVED[i];
            arr.push(rgbToOklab(p[0], p[1], p[2]));
        }
        return arr;
    }

    function szEnhancedDither(imageData) {
        var width = imageData.width;
        var height = imageData.height;
        var n = width * height;

        // 1. V5 seed（upstream balanced：compressDynamicRange + perceived FS）
        var seedLabels = v5SeedDither(imageData);

        // 2. 共享结构计算（C++ quantize 前置：tone/grad/film strips）
        var tone = balancedToneMap(imageData);
        tone.width = width;
        tone.height = height;
        var lightness = tone.toneL;
        var grad = sobelMagnitude(boxBlur(lightness, width, height, 1), width, height);
        var perceived = perceivedToOklab();
        var panelBlack = perceived[0][0];
        var filmStrips = detectFilmStrips(tone.toneL, width, height, panelBlack);

        // 3. V7：seed = V5，refine-same 0，cleanup 全 0
        var v7Labels = quantize(tone, grad, filmStrips, v7Config(), perceived, seedLabels);

        // 4. V7.5：seed = V7 输出，refine-same -1，sparkle/chroma cleanup 1
        var v75Labels = quantize(tone, grad, filmStrips, v75Config(), perceived, v7Labels);

        // 5. 输出严格六色 RGB（与 C++ strict_rgb 一致，绿用理论色 0,255,0，
        //    processImageData 会按 FILM 编码重映射）
        var out = _createImageDataLike(width, height);
        var od = out.data;
        for (var o = 0; o < n; o++) {
            var rgbc = PALETTE_OUTPUT[v75Labels[o]];
            od[o * 4] = rgbc[0];
            od[o * 4 + 1] = rgbc[1];
            od[o * 4 + 2] = rgbc[2];
            od[o * 4 + 3] = 255;
        }
        return out;
    }

    // ---- 暴露接口（小程序版：无 ImageData 全局构造器，用兼容对象替代；CommonJS 导出）----
    function _createImageDataLike(w, h) {
        var data = new Uint8ClampedArray(w * h * 4);
        return { width: w, height: h, data: data };
    }

    var szDebug = {
        balancedToneMap: balancedToneMap,
        boxBlur: boxBlur,
        sobelMagnitude: sobelMagnitude,
        detectFilmStrips: detectFilmStrips,
        quantize: quantize,
        v7Config: v7Config,
        v75Config: v75Config,
        perceivedToOklab: perceivedToOklab,
        PALETTE_OUTPUT: PALETTE_OUTPUT
    };

    module.exports = {
        szEnhancedDither: szEnhancedDither,
        // 调试钩子（供与交接包 seed/输出逐字节对比验证）
        szV5Seed: v5SeedDither,
        szDebug: szDebug
    };
})();