// ForFilm · 动画工坊（ARK 通行证版多帧 .film 创作台）
// ------------------------------------------------------------
// 目标设备：通行证版（FRAMEFILMARK，E6 3.70" 720×480 spectra 面板）。
// 产出的 .film 为 v2 多帧格式（见 docs/film/film.md §10.7）：
//   Format 0x01 MonoFast（1bpp 差分快刷）/ 0x02 ColorQual / 0x03 ColorFast，
//   FrameCount(0x0A) > 1，主体按帧顺序连续拼接。
// 设备端由 app_animation 播放；上传后服务层按帧数自动归档到 /sdcard/animation。
//
// 像素排布：与单帧管线同一套约定 —— 画布空间是"人眼看到的正方向"，
// 面板原生为行优先，画布像素 (x,y) 落到面板 (H-1-y, W-1-x)（rotated-180）。
// 8bpp 与 1bpp 都由驱动按"整行 W 像素"读取，故两种格式用同一套坐标映射。

/* ============================================================
   1. 常量
   ============================================================ */

var ANIM_W = 720;
var ANIM_H = 480;

// 像素绘制网格（10 倍放大正好铺满 720×480）
var ANIM_GRID_W = 72;
var ANIM_GRID_H = 48;

// 显示方向：与 Film / Frame 页一致 —— 数据画布是横屏 720×480，在屏幕上是"竖着"看的
// （顺时针转 90° 展示）。逻辑网格保持 72×48 横屏不动（打包/量化都用它），
// 只有绘制盘按同一角度转 90° 渲染成 48×72 的竖屏网格，两边看到的画面才一致：
//   逻辑格 (gx,gy) → 竖屏格 (px,py) = (ANIM_GRID_H-1-gy, gx)
var ANIM_GRID_PW = ANIM_GRID_H;    // 竖屏网格：宽 48 格
var ANIM_GRID_PH = ANIM_GRID_W;    // 竖屏网格：高 72 格

function animGridToPortrait(gx, gy) {
    return { px: ANIM_GRID_H - 1 - gy, py: gx };
}

function animPortraitToGrid(px, py) {
    return { gx: py, gy: ANIM_GRID_H - 1 - px };
}

// 把横屏画布按 90° 顺时针画进竖屏画布（洋葱皮 / 缩略图共用）
// 目标尺寸需满足 (宽,高) = 竖屏尺寸：本地 x 轴对应竖屏高度方向
function animDrawRotated(dstCtx, src, dstW, dstH) {
    dstCtx.save();
    dstCtx.translate(dstW, 0);
    dstCtx.rotate(Math.PI / 2);
    dstCtx.drawImage(src, 0, 0, src.width, src.height, 0, 0, dstH, dstW);
    dstCtx.restore();
}

// 帧缩略图尺寸（竖屏，与预览/绘制方向一致；96:144 = 480:720）
var ANIM_THUMB_W = 96;
var ANIM_THUMB_H = 144;

var ANIM_MONO_FRAME_BYTES = (ANIM_W * ANIM_H) / 8;   // 43200
var ANIM_COLOR_FRAME_BYTES = ANIM_W * ANIM_H;        // 345600

var ANIM_MAX_FRAMES = 48;                            // 帧数上限（内存与传输时长双约束）
// 整体上限：设备端优先把整份文件读进 PSRAM（>1.5MB 装不下时自动退化为"按帧从 SD 读"，
// 所以大文件能播，只是每帧多一次读卡）。这里卡的是 BLE 传输时长，不是能不能播。
var ANIM_MAX_BYTES = 2.5 * 1024 * 1024;
var ANIM_WARN_BYTES = 900 * 1024;                    // 超过即提示传输耗时

var ANIM_DRAW_CELL = 12;                             // 绘制画布内部格子边长（仅显示用）

// 位图帧的画面变换范围（叠加在"适应画幅"之上的用户缩放）
var ANIM_IMG_SCALE_MIN = 0.2;
var ANIM_IMG_SCALE_MAX = 8;

// 输出格式表（code 与 .film 头的 Format 字段一致）
var ANIM_FORMATS = {
    mono:      { code: 0x01, frameBytes: ANIM_MONO_FRAME_BYTES,  label: '黑白快刷 MonoFast',  short: 'MONO 1BPP' },
    colorFast: { code: 0x03, frameBytes: ANIM_COLOR_FRAME_BYTES, label: '彩色快刷 ColorFast', short: 'FAST 8BPP', profile: 'colorFast55' },
    colorQual: { code: 0x02, frameBytes: ANIM_COLOR_FRAME_BYTES, label: '彩色高质 ColorQual', short: 'QUAL 8BPP', profile: 'colorQual' }
};

// 绘制调色板（索引 0 固定为白底；其余为高饱和色，量化后落到设备色板）
var ANIM_PALETTE = [
    [255, 255, 255], // 0 白（底）
    [0, 0, 0],       // 1 黑
    [217, 58, 43],   // 2 红
    [255, 122, 0],   // 3 橙
    [255, 214, 0],   // 4 黄
    [168, 212, 0],   // 5 黄绿
    [0, 160, 32],    // 6 绿
    [0, 176, 160],   // 7 青
    [0, 160, 255],   // 8 天蓝
    [0, 48, 255],    // 9 蓝
    [0, 16, 96],     // 10 深蓝
    [128, 0, 192],   // 11 紫
    [255, 96, 160],  // 12 粉
    [138, 75, 32],   // 13 棕
    [128, 128, 128], // 14 灰
    [200, 200, 200]  // 15 浅灰
];

var ANIM_PALETTE_CSS = ANIM_PALETTE.map(function (c) {
    return 'rgb(' + c[0] + ',' + c[1] + ',' + c[2] + ')';
});

/* ============================================================
   2. 状态
   ============================================================ */

var animFrames = [];          // [{ canvas, grid|null, undo:[Uint8Array...], thumb }]
var animCurrent = -1;         // 当前选中帧下标
var animSeq = 0;              // 帧自增 id

var animMode = 'draw';        // draw | images | gif | camera
var animTool = 'pen';         // pen | eraser | fill | pick
var animColorIndex = 1;

var animFps = 8;
var animPlaying = false;
var animPlayTimer = null;

var animDither = false;               // 抖动（黑白=有序抖动；彩色=Atkinson 扩散）
var animSymmetry = false;             // 对称绘制（左右镜像）
var animOnion = false;                // 洋葱皮
var animDevicePreview = true;         // 设备色彩预览：默认开启（量化结果有缓存，播放时也生效）
var animTransformAll = false;         // 画面调整是否同步到所有位图帧（帧多时不用一帧帧调）

var animBusy = false;                 // 打包 / 传输中
var animCameraStream = null;
var animUploadAfterPlay = true;

// 绘制交互
var animDrawing = false;
var animLastCell = null;

/* ============================================================
   3. 基础工具
   ============================================================ */

// 画布像素 (x,y) → 文件主体下标（8bpp 一像素一字节 / v1 半字节）
function animPixelIndex(x, y) {
    return (ANIM_H - 1 - y) * ANIM_W + (ANIM_W - 1 - x);
}

// 画布像素 (x,y) → MonoFast 位图内的字节下标与位序
function animMonoBit(x, y) {
    var r = ANIM_H - 1 - y;
    var c = ANIM_W - 1 - x;
    return { byte: r * (ANIM_W >> 3) + (c >> 3), mask: 1 << (7 - (c & 7)) };
}

function animCreateCanvas() {
    var c = document.createElement('canvas');
    c.width = ANIM_W;
    c.height = ANIM_H;
    return c;
}

function animCurrentFrame() {
    return (animCurrent >= 0 && animCurrent < animFrames.length) ? animFrames[animCurrent] : null;
}

function animEscape(str) {
    return String(str).replace(/[&<>"']/g, function (c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
}

// 能力判定：多帧 .film 只有通行证版（Ark）在 3.7 寸（720×480）屏上能播。
// 两个条件缺一不可 —— 三机型的 Pro 也是这块屏（面板 ID 0x02），但固件里没有动画 app，
// 所以"检测到同款屏"也不放行；反之 Ark 若报了别的分辨率同样不放行。
function animDeviceSupported() {
    var cfg = (typeof getDeviceConfig === 'function') ? getDeviceConfig() : null;
    var panelOk = !!cfg && cfg.screenWidth === 720 && cfg.screenHeight === 480;
    var modelOk = (typeof currentDeviceType !== 'undefined' && currentDeviceType === 'FRAMEFILMARK');
    return panelOk && modelOk;
}

// 设备端可达性：能力达标 + 已连接
function animDeviceReady() {
    return animDeviceSupported() &&
           typeof device !== 'undefined' && device && server && characteristic;
}

/* ============================================================
   4. 帧模型
   ============================================================ */

// 用像素网格建帧（grid: Uint8Array(GRID_W*GRID_H)，值为调色板下标）
function animNewPixelFrame(grid) {
    var frame = {
        id: ++animSeq,
        canvas: animCreateCanvas(),
        grid: grid || new Uint8Array(ANIM_GRID_W * ANIM_GRID_H),
        undo: [],
        thumb: document.createElement('canvas')
    };
    frame.thumb.width = ANIM_THUMB_W;
    frame.thumb.height = ANIM_THUMB_H;
    animRenderGrid(frame);
    return frame;
}

// 归一化图片源：drawImage 只接受 canvas / img / bitmap 这类 CanvasImageSource，
// 不接受 ImageData（GIF 拆帧给的就是 ImageData）—— 直接画会抛 TypeError 导致整帧丢失。
function animNormalizeSource(source) {
    if (!source) return null;
    if (typeof source.getContext === 'function') return source;                    // canvas
    if (typeof source.width !== 'number' || !source.data) return source;          // img / bitmap
    var c = document.createElement('canvas');
    c.width = source.width;
    c.height = source.height;
    c.getContext('2d').putImageData(source, 0, 0);
    return c;
}

// 用一张位图建帧（contain 适配，白底，不栅格化；需要时再由用户"栅格化"）
// transform 可选：复制帧时把画面变换一起带过来
function animNewImageFrame(source, transform) {
    source = animNormalizeSource(source);
    if (!source) return null;
    var frame = {
        id: ++animSeq,
        canvas: animCreateCanvas(),
        grid: null,
        // 原始位图 + 画面变换：拖动/缩放/旋转都基于 source 重绘，随时可反悔
        source: source,
        rotation: transform ? transform.rotation : 0,   // 0..3，每级 90°
        scale: transform ? transform.scale : 1,         // 叠加在"适应画幅"之上的用户缩放
        offsetX: transform ? transform.offsetX : 0,     // 画布坐标内的位移
        offsetY: transform ? transform.offsetY : 0,
        undo: [],
        thumb: document.createElement('canvas')
    };
    frame.thumb.width = ANIM_THUMB_W;
    frame.thumb.height = ANIM_THUMB_H;
    animRenderImageFrame(frame);
    return frame;
}

// 按 source + 变换把位图帧重绘到 720×480 画布：
// 以画布中心为基准，先 rotate(rotation×90°)，再按 fit×scale 缩放并叠加位移。
function animRenderImageFrame(frame) {
    if (!frame || !frame.source) {
        return;
    }
    var src = frame.source;
    var ctx = frame.canvas.getContext('2d');
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, ANIM_W, ANIM_H);

    var rot = ((frame.rotation || 0) % 4 + 4) % 4;
    // 转 90/270° 后源图的"视觉宽高"互换，适应画幅要按互换后的尺寸算
    var rw = (rot % 2 === 0) ? src.width : src.height;
    var rh = (rot % 2 === 0) ? src.height : src.width;
    var fit = Math.min(ANIM_W / rw, ANIM_H / rh);
    var s = fit * Math.max(ANIM_IMG_SCALE_MIN, Math.min(ANIM_IMG_SCALE_MAX, frame.scale || 1));
    var dw = src.width * s;
    var dh = src.height * s;

    ctx.save();
    ctx.translate(ANIM_W / 2 + (frame.offsetX || 0), ANIM_H / 2 + (frame.offsetY || 0));
    ctx.rotate(rot * Math.PI / 2);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(src, -dw / 2, -dh / 2, dw, dh);
    ctx.restore();

    frame.rev = (frame.rev || 0) + 1;   // 内容变了：设备色预览缓存靠它失效
}

// 画布 → 像素网格（栅格化）：取每个格子的平均色，再吸附到调色板
function animRasterizeFrame(frame) {
    if (!frame || frame.grid) {
        return;
    }
    var ctx = frame.canvas.getContext('2d');
    var data = ctx.getImageData(0, 0, ANIM_W, ANIM_H).data;
    var cw = ANIM_W / ANIM_GRID_W;
    var chh = ANIM_H / ANIM_GRID_H;
    var grid = new Uint8Array(ANIM_GRID_W * ANIM_GRID_H);

    for (var gy = 0; gy < ANIM_GRID_H; gy++) {
        for (var gx = 0; gx < ANIM_GRID_W; gx++) {
            var x0 = Math.floor(gx * cw);
            var y0 = Math.floor(gy * chh);
            var x1 = Math.max(x0 + 1, Math.floor((gx + 1) * cw));
            var y1 = Math.max(y0 + 1, Math.floor((gy + 1) * chh));
            var r = 0, g = 0, b = 0, n = 0;
            for (var y = y0; y < y1; y++) {
                for (var x = x0; x < x1; x++) {
                    var o = (y * ANIM_W + x) * 4;
                    r += data[o]; g += data[o + 1]; b += data[o + 2];
                    n++;
                }
            }
            r /= n; g /= n; b /= n;
            grid[gy * ANIM_GRID_W + gx] = animNearestPalette(r, g, b);
        }
    }

    frame.grid = grid;
    frame.undo = [];
    frame.source = null;    // 已栅格化成像素帧，不再需要原图与画面变换
    animRenderGrid(frame);
    animSyncTransformTools();
}

function animNearestPalette(r, g, b) {
    var best = 0;
    var bestDist = Infinity;
    for (var i = 0; i < ANIM_PALETTE.length; i++) {
        var c = ANIM_PALETTE[i];
        var dr = r - c[0], dg = g - c[1], db = b - c[2];
        var d = dr * dr + dg * dg + db * db;
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

// 网格 → 帧画布（每格 10×10）
function animRenderGrid(frame) {
    if (!frame || !frame.grid) {
        return;
    }
    var ctx = frame.canvas.getContext('2d');
    ctx.fillStyle = ANIM_PALETTE_CSS[0];
    ctx.fillRect(0, 0, ANIM_W, ANIM_H);
    var cellW = ANIM_W / ANIM_GRID_W;
    var cellH = ANIM_H / ANIM_GRID_H;
    for (var gy = 0; gy < ANIM_GRID_H; gy++) {
        for (var gx = 0; gx < ANIM_GRID_W; gx++) {
            var idx = frame.grid[gy * ANIM_GRID_W + gx];
            if (idx === 0) {
                continue;
            }
            ctx.fillStyle = ANIM_PALETTE_CSS[idx];
            ctx.fillRect(Math.round(gx * cellW), Math.round(gy * cellH),
                         Math.ceil(cellW), Math.ceil(cellH));
        }
    }
    frame.rev = (frame.rev || 0) + 1;   // 内容变了：设备色预览缓存靠它失效
}

function animRenderThumb(frame) {
    if (!frame) return;
    var ctx = frame.thumb.getContext('2d');
    ctx.clearRect(0, 0, frame.thumb.width, frame.thumb.height);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'low';
    // 帧画布是横屏，缩略图竖屏显示 → 转 90°（与预览/绘制同一方向）
    animDrawRotated(ctx, frame.canvas, frame.thumb.width, frame.thumb.height);
}

/* ============================================================
   5. 帧增删改查
   ============================================================ */

function animAddFrame(frame, index) {
    if (animFrames.length >= ANIM_MAX_FRAMES) {
        showMessage('最多 ' + ANIM_MAX_FRAMES + ' 帧', 'warning');
        return false;
    }
    if (typeof index === 'number' && index >= 0 && index <= animFrames.length) {
        animFrames.splice(index, 0, frame);
    } else {
        animFrames.push(frame);
    }
    animRenderThumb(frame);
    animSelectFrame(typeof index === 'number' ? index : animFrames.length - 1);
    animRefreshFrames();
    return true;
}

function animRemoveFrame(index) {
    if (index < 0 || index >= animFrames.length) return;
    animFrames.splice(index, 1);
    if (animFrames.length === 0) {
        animCurrent = -1;
    } else if (animCurrent >= animFrames.length) {
        animCurrent = animFrames.length - 1;
    }
    animStop();
    animRefreshFrames();
    animRenderDrawCanvas();
    animRenderStage();
}

function animMoveFrame(index, delta) {
    var to = index + delta;
    if (index < 0 || index >= animFrames.length || to < 0 || to >= animFrames.length) {
        return;
    }
    var f = animFrames.splice(index, 1)[0];
    animFrames.splice(to, 0, f);
    animSelectFrame(to);
    animRefreshFrames();
}

function animDuplicateFrame(index) {
    var src = animFrames[index];
    if (!src) return;
    if (animFrames.length >= ANIM_MAX_FRAMES) {
        showMessage('最多 ' + ANIM_MAX_FRAMES + ' 帧', 'warning');
        return;
    }
    var frame = src.grid ? animNewPixelFrame(src.grid.slice()) : animNewImageFrame(src.source, src);
    animAddFrame(frame, index + 1);
}

function animSelectFrame(index) {
    if (index < 0 || index >= animFrames.length) return;
    animCurrent = index;
    animRefreshFrameSelection();
    animRenderDrawCanvas();
    animRenderStage();
    animRenderScrub();
    animSyncTransformTools();
}

/* ============================================================
   6. 打包（多帧 .film）
   ============================================================ */

function animFrameImageData(frame) {
    var ctx = frame.canvas.getContext('2d');
    return ctx.getImageData(0, 0, ANIM_W, ANIM_H);
}

// MonoFast：1bpp 位图，1=黑 0=白；可选 4×4 Bayer 有序抖动做半调
function animQuantizeMono(imageData, useDither) {
    var out = new Uint8Array(ANIM_MONO_FRAME_BYTES);
    var data = imageData.data;
    for (var y = 0; y < ANIM_H; y++) {
        var row = BAYER_MATRIX[y & 3];
        for (var x = 0; x < ANIM_W; x++) {
            var o = (y * ANIM_W + x) * 4;
            var lum = 0.299 * data[o] + 0.587 * data[o + 1] + 0.114 * data[o + 2];
            if (useDither) {
                lum += (row[x & 3] - 8) * 8;
            }
            if (lum < 128) {
                var bit = animMonoBit(x, y);
                out[bit.byte] |= bit.mask;
            }
        }
    }
    return out;
}

// 8bpp 索引色：复用单帧管线的选色（cfClosestIndex / cfDither），
// 只是把落位下标换成动画自己的 animPixelIndex（不依赖当前机型配置）
function animQuantizeColor(imageData, profile, useDither) {
    var src = new ImageData(new Uint8ClampedArray(imageData.data), ANIM_W, ANIM_H);
    var quant = useDither ? cfDither(src, 1.0, profile) : cfQuantize(src, 1.0, false, profile);
    var data = quant.data;
    var out = new Uint8Array(ANIM_COLOR_FRAME_BYTES);

    // 量化后的颜色只可能来自色板（≤64 种），用 Map 缓存选色结果
    var cache = new Map();
    for (var y = 0; y < ANIM_H; y++) {
        for (var x = 0; x < ANIM_W; x++) {
            var o = (y * ANIM_W + x) * 4;
            var r = data[o], g = data[o + 1], b = data[o + 2];
            var key = (r << 16) | (g << 8) | b;
            var idx = cache.get(key);
            if (idx === undefined) {
                idx = cfClosestIndex(r, g, b, profile);
                cache.set(key, idx);
            }
            out[animPixelIndex(x, y)] = idx;
        }
    }
    return out;
}

/**
 * 打包多帧 .film
 * @param {string} formatKey mono | colorFast | colorQual
 * @param {boolean} useDither
 * @returns {{bytes: Uint8Array, frameSize: number, format: number}}
 */
function animBuildFilmFile(formatKey, useDither) {
    var fmt = ANIM_FORMATS[formatKey];
    if (!fmt) {
        throw new Error('未知输出格式');
    }
    if (animFrames.length === 0) {
        throw new Error('还没有任何帧');
    }

    var frameCount = animFrames.length;
    var frameSize = fmt.frameBytes;
    var bodySize = frameSize * frameCount;

    var header = new Uint8Array(32);
    header[0] = bodySize & 0xFF;
    header[1] = (bodySize >> 8) & 0xFF;
    header[2] = (bodySize >> 16) & 0xFF;
    header[3] = (bodySize >> 24) & 0xFF;
    header[4] = ANIM_W & 0xFF;
    header[5] = (ANIM_W >> 8) & 0xFF;
    header[6] = ANIM_H & 0xFF;
    header[7] = (ANIM_H >> 8) & 0xFF;
    header[8] = 0;                 // v2 不使用 ColorCount
    header[9] = fmt.code;          // Format
    header[10] = frameCount & 0xFF;
    header[11] = (frameCount >> 8) & 0xFF;
    // 0x0C~0x0F 保留、0x10~0x1F 颜色表（v2 全 0）

    var file = new Uint8Array(32 + bodySize);
    file.set(header, 0);

    var profile = fmt.profile ? CF_PROFILES[fmt.profile] : null;
    var off = 32;
    for (var i = 0; i < frameCount; i++) {
        var imageData = animFrameImageData(animFrames[i]);
        var bytes = (formatKey === 'mono')
            ? animQuantizeMono(imageData, useDither)
            : animQuantizeColor(imageData, profile, useDither);
        file.set(bytes, off);
        off += frameSize;
    }

    return { bytes: file, frameSize: frameSize, format: fmt.code };
}

/* ============================================================
   7. 预览 / 播放
   ============================================================ */

// 预览取景框：画布是横屏 720×480，与 Film 页一致顺时针转 90° 竖着展示。
// 不用全局 updateCanvasScale()：那个按"当前机型"的宽高判断是否旋转，
// 而这里恒定是 720×480 的横屏数据 → 恒定转 90°。
function animUpdateStageScale() {
    var inner = document.getElementById('animStageInner');
    var canvas = document.getElementById('animStage');
    if (!inner || !canvas) return;
    var cw = inner.clientWidth, ch = inner.clientHeight;
    if (!cw || !ch) return;
    var scale = Math.min(cw / ANIM_H, ch / ANIM_W);
    canvas.style.transform = 'translate(-50%,-50%) rotate(90deg) scale(' + scale + ')';
}

// 把一帧量化成"设备上会看到的"RGBA（与打包同一套色板/抖动），供取景框回显。
// 只对当前帧算一次，结果进 animPreviewCache —— 否则播放时每帧都重量化会直接卡死。
function animQuantizeFrameImageData(frame) {
    var fmt = ANIM_FORMATS[animSelectedFormat()];
    var imageData = animFrameImageData(frame);
    var out = new ImageData(ANIM_W, ANIM_H);
    var d = out.data;
    var x, y, o;

    if (fmt.code === 0x01) {
        var mono = animQuantizeMono(imageData, animDither);
        for (y = 0; y < ANIM_H; y++) {
            for (x = 0; x < ANIM_W; x++) {
                var bit = animMonoBit(x, y);
                var black = (mono[bit.byte] & bit.mask) !== 0;
                o = (y * ANIM_W + x) * 4;
                d[o] = d[o + 1] = d[o + 2] = black ? 0 : 255;
                d[o + 3] = 255;
            }
        }
        return out;
    }

    var profile = CF_PROFILES[fmt.profile];
    var idx8 = animQuantizeColor(imageData, profile, animDither);
    var palette = profile.palette;
    for (y = 0; y < ANIM_H; y++) {
        for (x = 0; x < ANIM_W; x++) {
            var c = palette[idx8[animPixelIndex(x, y)]] || [0, 0, 0];
            o = (y * ANIM_W + x) * 4;
            d[o] = c[0];
            d[o + 1] = c[1];
            d[o + 2] = c[2];
            d[o + 3] = 255;
        }
    }
    return out;
}

// 设备色预览缓存（LRU）。量化一帧要过 34 万像素，播放时逐帧算来不及，必须缓存。
// 键里带 frame.rev（每次内容重绘自增）与色板/抖动，所以改设置或改画面会自动失效。
var animPreviewCache = [];
var ANIM_PREVIEW_CACHE_MAX = 10;

function animPreviewKey(frame) {
    return animSelectedFormat() + '|' + (animDither ? 1 : 0) + '|' + frame.id + '|' + (frame.rev || 0);
}

function animDevicePreviewImage(frame) {
    var key = animPreviewKey(frame);
    for (var i = 0; i < animPreviewCache.length; i++) {
        if (animPreviewCache[i].key === key) {
            var hit = animPreviewCache.splice(i, 1)[0];
            animPreviewCache.unshift(hit);
            return hit.img;
        }
    }
    var img = animQuantizeFrameImageData(frame);
    animPreviewCache.unshift({ key: key, img: img });
    while (animPreviewCache.length > ANIM_PREVIEW_CACHE_MAX) {
        animPreviewCache.pop();
    }
    return img;
}

// 把一帧画到取景框。raw=true 强制走原图（拖动/滚轮过程中用，避免每次移动都重量化）
function animBlitStage(frame, raw) {
    var stage = document.getElementById('animStage');
    if (!stage || !frame) return;
    var ctx = stage.getContext('2d');
    ctx.imageSmoothingEnabled = false;
    if (animDevicePreview && !raw) {
        ctx.putImageData(animDevicePreviewImage(frame), 0, 0);
    } else {
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, ANIM_W, ANIM_H);
        ctx.drawImage(frame.canvas, 0, 0);
    }
}

function animRenderStage(raw) {
    var stage = document.getElementById('animStage');
    if (!stage) return;
    stage.width = ANIM_W;
    stage.height = ANIM_H;

    var frame = animCurrentFrame();
    if (!frame) {
        var ctx = stage.getContext('2d');
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, ANIM_W, ANIM_H);
        animRenderStageBadge();
        return;
    }

    animBlitStage(frame, raw);
    animRenderStageBadge();
    animUpdateStageScale();
}

function animRenderStageBadge() {
    var badge = document.getElementById('animStageBadge');
    if (!badge) return;
    badge.textContent = animFrames.length
        ? (animCurrent + 1) + ' / ' + animFrames.length
        : '0 / 0';
}

function animStart() {
    if (animPlaying || animFrames.length < 2) {
        if (animFrames.length < 2) showMessage('至少需要 2 帧才能播放', 'warning');
        return;
    }
    animPlaying = true;
    animUpdatePlayBtn();
    var interval = Math.max(40, Math.round(1000 / animFps));
    animPlayTimer = setInterval(function () {
        if (animFrames.length === 0) {
            animStop();
            return;
        }
        animCurrent = (animCurrent + 1) % animFrames.length;
        animRenderThumbFreeStage();
    }, interval);
}

function animStop() {
    animPlaying = false;
    if (animPlayTimer) {
        clearInterval(animPlayTimer);
        animPlayTimer = null;
    }
    animUpdatePlayBtn();
}

// 播放时只刷舞台（不回写选中态，避免每帧重建列表 DOM）
// 同样走设备色预览：量化结果有缓存，逐帧只是 putImageData，才跟得上帧率
function animRenderThumbFreeStage() {
    var frame = animCurrentFrame();
    if (!frame) return;
    animBlitStage(frame);
    animRenderStageBadge();
    animUpdateScrubActive();
}

function animUpdatePlayBtn() {
    var btn = document.getElementById('animPlayBtn');
    if (!btn) return;
    btn.classList.toggle('is-playing', animPlaying);
    var use = btn.querySelector('use');
    if (use) {
        use.setAttribute('href', animPlaying ? '#i-pause' : '#i-play');
    }
    btn.setAttribute('title', animPlaying ? '暂停' : '播放');
}

/* ============================================================
   7b. 位图帧的画面调整（拖动 / 缩放 / 旋转）
   ============================================================ */

// 画面变换只对"位图帧"有意义：像素帧是网格数据，请用像素工具编辑
function animFrameTransformable(frame) {
    return !!frame && !frame.grid && !!frame.source;
}

// "应用到所有帧"时的目标集合：除当前帧外的其它位图帧
function animOtherImageFrames(current) {
    var list = [];
    for (var i = 0; i < animFrames.length; i++) {
        var f = animFrames[i];
        if (f !== current && animFrameTransformable(f)) list.push(f);
    }
    return list;
}

// 单帧的画面重绘 + 缩略图 + 列表里对应的那张缩略图
function animRefreshImageFrame(f) {
    animRenderImageFrame(f);
    animRenderThumb(f);
    var idx = animFrames.indexOf(f);
    var item = idx >= 0 ? document.querySelector('.anim-frame[data-index="' + idx + '"]') : null;
    if (item) {
        var old = item.querySelector('canvas');
        if (old && old !== f.thumb) item.replaceChild(f.thumb, old);
    }
}

// 把当前帧的变换广播给其它位图帧（只改数值；重绘按需，拖动中先不重绘，避免卡）
function animBroadcastTransform(src, light) {
    var others = animOtherImageFrames(src);
    for (var i = 0; i < others.length; i++) {
        others[i].rotation = src.rotation;
        others[i].scale = src.scale;
        others[i].offsetX = src.offsetX;
        others[i].offsetY = src.offsetY;
        if (!light) animRefreshImageFrame(others[i]);
    }
    return others.length;
}

// 重绘当前帧并刷新各处视图。light=true 用于拖动过程中：
// 只重画当前帧（画面跟随手指），其它帧只更新数值，松手时再一次性补齐
function animApplyTransform(light, frame) {
    var f = frame || animCurrentFrame();
    if (!animFrameTransformable(f)) return;
    if (animTransformAll) animBroadcastTransform(f, light);
    animRefreshImageFrame(f);
    if (!animPlaying) animRenderStage(!!light);   // 拖动中先看原图，松手再回设备色
    animRenderGridOverlay();
    if (!light) animRenderDrawCanvas();
}

// 手势结束后补一次完整重绘（滚轮/捏合这类连续手势用）
var animTransformSettleTimer = 0;
function animScheduleTransformSettle() {
    if (animTransformSettleTimer) clearTimeout(animTransformSettleTimer);
    animTransformSettleTimer = setTimeout(function () {
        animTransformSettleTimer = 0;
        animApplyTransform(false);
    }, 220);
}

var animTransformRaf = 0;
function animScheduleTransform() {
    if (animTransformRaf) return;
    animTransformRaf = requestAnimationFrame(function () {
        animTransformRaf = 0;
        animApplyTransform(true);
    });
}

// 屏幕位移 → 画布位移：取景框把横屏画布顺时针转了 90°，位移要反着解
// （画布 +x 在屏幕上表现为向下、+y 表现为向左）
function animStageDeltaToCanvas(dx, dy) {
    var inner = document.getElementById('animStageInner');
    if (!inner) return { x: 0, y: 0 };
    var s = Math.min(inner.clientWidth / ANIM_H, inner.clientHeight / ANIM_W);
    if (!s) return { x: 0, y: 0 };
    return { x: dy / s, y: -dx / s };
}

function animRotateFrame() {
    var f = animCurrentFrame();
    if (!animFrameTransformable(f)) {
        showMessage('只有位图帧可以旋转（像素帧请用像素工具编辑）', 'warning');
        return;
    }
    f.rotation = ((f.rotation || 0) + 1) % 4;
    animApplyTransform(false);
}

function animResetTransform() {
    var f = animCurrentFrame();
    if (!animFrameTransformable(f)) {
        showMessage('只有位图帧可以调整画面', 'warning');
        return;
    }
    f.rotation = 0;
    f.scale = 1;
    f.offsetX = 0;
    f.offsetY = 0;
    animApplyTransform(false);
}

// 取景框手势：单指/鼠标拖动 = 移动，滚轮 / 双指捏合 = 缩放
function animInitStageInteraction() {
    var inner = document.getElementById('animStageInner');
    if (!inner) return;

    var pointers = new Map();
    var dragging = null;      // { id, lastX, lastY }
    var pinch = null;         // { ids:[a,b], dist }

    function dist(a, b) {
        var dx = a.x - b.x, dy = a.y - b.y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function endGesture() {
        if (dragging) {
            dragging = null;
            animApplyTransform(false);      // 收尾时补一次完整重绘
        }
        pinch = null;
    }

    inner.addEventListener('pointerdown', function (e) {
        if (animBusy || animPlaying) return;
        if (!animFrameTransformable(animCurrentFrame())) return;
        pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
        try {
            if (inner.setPointerCapture) inner.setPointerCapture(e.pointerId);
        } catch (err) {
            /* 合成事件 / 指针已释放时可能抛 NotFoundError，不影响后续拖动 */
        }
        if (pointers.size === 1) {
            dragging = { id: e.pointerId, lastX: e.clientX, lastY: e.clientY };
            pinch = null;
        } else if (pointers.size === 2) {
            var pts = Array.from(pointers.values());
            pinch = { ids: Array.from(pointers.keys()), dist: dist(pts[0], pts[1]) };
            dragging = null;
        }
        e.preventDefault();
    });

    inner.addEventListener('pointermove', function (e) {
        if (!pointers.has(e.pointerId)) return;
        pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
        var f = animCurrentFrame();
        if (!animFrameTransformable(f)) return;

        if (dragging && dragging.id === e.pointerId) {
            var d = animStageDeltaToCanvas(e.clientX - dragging.lastX, e.clientY - dragging.lastY);
            f.offsetX = (f.offsetX || 0) + d.x;
            f.offsetY = (f.offsetY || 0) + d.y;
            dragging.lastX = e.clientX;
            dragging.lastY = e.clientY;
            animScheduleTransform();
        } else if (pinch && pinch.ids.indexOf(e.pointerId) !== -1) {
            var pts = Array.from(pointers.values());
            var nd = dist(pts[0], pts[1]);
            if (pinch.dist > 0 && nd > 0) {
                f.scale = Math.max(ANIM_IMG_SCALE_MIN,
                    Math.min(ANIM_IMG_SCALE_MAX, (f.scale || 1) * (nd / pinch.dist)));
                pinch.dist = nd;
                animScheduleTransform();
                animScheduleTransformSettle();
            }
        }
        e.preventDefault();
    });

    function onPointerEnd(e) {
        pointers.delete(e.pointerId);
        if (dragging && dragging.id === e.pointerId) {
            dragging = null;
            animApplyTransform(false);
        }
        if (pinch && pinch.ids.indexOf(e.pointerId) !== -1) {
            pinch = null;
            animApplyTransform(false);
        }
        if (pointers.size === 0) endGesture();
    }
    inner.addEventListener('pointerup', onPointerEnd);
    inner.addEventListener('pointercancel', onPointerEnd);

    // 滚轮缩放（必须 passive:false 才能阻止页面跟着滚）
    // 连续滚动期间先按原图走（量化很贵），停手 220ms 后再回设备色
    inner.addEventListener('wheel', function (e) {
        if (animBusy || animPlaying) return;
        var f = animCurrentFrame();
        if (!animFrameTransformable(f)) return;
        e.preventDefault();
        var k = e.deltaY > 0 ? 0.9 : 1.1;
        f.scale = Math.max(ANIM_IMG_SCALE_MIN,
            Math.min(ANIM_IMG_SCALE_MAX, (f.scale || 1) * k));
        animApplyTransform(true);
        animScheduleTransformSettle();
    }, { passive: false });
}

// 调整工具的可用态（跟随当前帧类型）
function animSyncTransformTools() {
    var box = document.getElementById('animImgTools');
    var hint = document.getElementById('animImgHint');
    var label = document.getElementById('animApplyAllLabel');
    var usable = animFrameTransformable(animCurrentFrame());
    if (box) {
        box.classList.toggle('is-disabled', !usable);
        var btns = box.querySelectorAll('button');
        for (var i = 0; i < btns.length; i++) window.arkSetControlDisabled(btns[i], !usable);
    }
    if (label) label.classList.toggle('is-on', animTransformAll);
    if (hint) {
        var f = animCurrentFrame();
        if (!f) {
            hint.textContent = '选中一帧后可调整画面';
        } else if (!usable) {
            hint.textContent = '像素帧不参与画面调整：拖动/缩放/旋转只对导入的位图帧生效';
        } else if (animTransformAll) {
            var n = animOtherImageFrames(f).length;
            hint.textContent = n > 0
                ? '整体调整已开启：拖动/缩放/旋转会同步到另外 ' + n + ' 个位图帧'
                : '整体调整已开启（当前只有这一个位图帧）';
        } else {
            hint.textContent = '在取景框里拖动移动 · 滚轮 / 双指缩放 · 可旋转（仅位图帧）';
        }
    }
}

/* ============================================================
   8. 列表 / 舞台 / 进度条渲染
   ============================================================ */

function animRefreshFrames() {
    animRenderFrameList();
    animRenderScrub();
    animUpdateStats();
    animRenderStage();
    animRenderDrawCanvas();
    animSyncTransformTools();
    var chip = document.getElementById('animFrameChip');
    if (chip) chip.textContent = animFrames.length + ' 帧';
}

function animRenderFrameList() {
    var list = document.getElementById('animFrameList');
    if (!list) return;
    if (animFrames.length === 0) {
        list.innerHTML = '<div class="empty-state">还没有帧 —— 用上面的方式新建第一帧</div>';
        return;
    }
    list.innerHTML = '';
    animFrames.forEach(function (frame, i) {
        var item = document.createElement('button');
        item.type = 'button';
        item.className = 'anim-frame' + (i === animCurrent ? ' on' : '');
        item.dataset.index = i;
        item.appendChild(frame.thumb);
        var no = document.createElement('span');
        no.className = 'anim-frame__no';
        no.textContent = (i < 9 ? '0' : '') + (i + 1);
        item.appendChild(no);
        if (!frame.grid) {
            var tag = document.createElement('span');
            tag.className = 'anim-frame__tag';
            tag.textContent = 'IMG';
            item.appendChild(tag);
        }
        item.addEventListener('click', function () {
            animStop();
            animSelectFrame(i);
        });
        list.appendChild(item);
    });
    var sel = list.querySelector('.anim-frame.on');
    if (sel && sel.scrollIntoView) {
        sel.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    }
}

function animRefreshFrameSelection() {
    var list = document.getElementById('animFrameList');
    if (!list) return;
    var items = list.querySelectorAll('.anim-frame');
    for (var i = 0; i < items.length; i++) {
        items[i].classList.toggle('on', +items[i].dataset.index === animCurrent);
    }
    var hint = document.getElementById('animFrameHint');
    if (hint) {
        var f = animCurrentFrame();
        hint.textContent = f
            ? ('第 ' + (animCurrent + 1) + ' 帧 · ' + (f.grid ? '像素帧 · 48×72 竖屏网格' : '位图帧（可直接绘制，会先栅格化）'))
            : '未选中帧';
    }
}

function animRenderScrub() {
    var scrub = document.getElementById('animScrub');
    if (!scrub) return;
    scrub.innerHTML = '';
    if (animFrames.length === 0) return;
    for (var i = 0; i < animFrames.length; i++) {
        var dot = document.createElement('button');
        dot.type = 'button';
        dot.className = 'anim-scrub__dot';
        dot.dataset.index = i;
        dot.style.left = (animFrames.length === 1 ? 50 : (i / (animFrames.length - 1)) * 100) + '%';
        dot.addEventListener('click', (function (idx) {
            return function () {
                animStop();
                animSelectFrame(idx);
            };
        })(i));
        scrub.appendChild(dot);
    }
    animUpdateScrubActive();
}

function animUpdateScrubActive() {
    var scrub = document.getElementById('animScrub');
    if (!scrub) return;
    var dots = scrub.querySelectorAll('.anim-scrub__dot');
    for (var i = 0; i < dots.length; i++) {
        dots[i].classList.toggle('on', +dots[i].dataset.index === animCurrent);
    }
}

function animSelectedFormat() {
    var sel = document.getElementById('animFormat');
    return sel ? sel.value : 'mono';
}

function animUpdateStats() {
    var stat = document.getElementById('animStat');
    var hint = document.getElementById('animFormatHint');
    var fmt = ANIM_FORMATS[animSelectedFormat()];
    if (!fmt) return;

    if (hint) {
        var tips = {
            mono: '1bpp 差分快刷：流畅、体积最小，适合逐帧动画',
            colorFast: '8bpp 55 色 2 相刷新：每帧整屏刷新，偏"翻页"节奏',
            colorQual: '8bpp 46 色 3 相刷新：色准最好，刷新最慢'
        };
        hint.textContent = tips[animSelectedFormat()] || '';
    }
    if (!stat) return;

    var n = animFrames.length;
    if (n === 0) {
        stat.innerHTML = '<span class="anim-stat__lbl">SIZE</span><b>—</b>';
        return;
    }
    var bytes = 32 + fmt.frameBytes * n;
    var secs = Math.round(bytes / 192 * 9 / 1000);
    var cls = bytes > ANIM_MAX_BYTES ? ' over' : (bytes > ANIM_WARN_BYTES ? ' warn' : '');
    stat.innerHTML =
        '<span class="anim-stat__lbl">FRAME</span><b>' + n + ' × ' + fmt.short + '</b>' +
        '<span class="anim-stat__lbl">SIZE</span><b class="' + cls + '">' + formatFileSize(bytes) + '</b>' +
        '<span class="anim-stat__lbl">ETA</span><b>≈ ' + secs + ' s</b>' +
        (bytes > ANIM_MAX_BYTES
            ? '<span class="anim-stat__warn">超出传输上限（约 2.5 MB），传输会很慢，请减少帧数</span>'
            : '');
}

/* ============================================================
   9. 像素绘制
   ============================================================ */

// 绘制盘显示尺寸：把每格吸附成"整数 CSS 像素"。
// 否则格宽是 475/48 = 9.9px 这种小数，格线会落在半像素上发灰、间距还会 10/9 交替不匀。
// 放大模式固定按内部像素 1:1（每格 12px）。
function animUpdateDrawSize() {
    var wrap = document.getElementById('animDrawWrap');
    var canvas = document.getElementById('animDraw');
    if (!wrap || !canvas) return;

    var cell;
    if (wrap.classList.contains('is-zoom')) {
        cell = ANIM_DRAW_CELL;
    } else {
        var availW = wrap.clientWidth || 320;
        var availH = Math.max(240, Math.round(window.innerHeight * 0.62));
        cell = Math.floor(Math.min(availW / ANIM_GRID_PW, availH / ANIM_GRID_PH));
        cell = Math.max(4, Math.min(ANIM_DRAW_CELL, cell));
    }

    canvas.style.width = (cell * ANIM_GRID_PW) + 'px';
    canvas.style.height = (cell * ANIM_GRID_PH) + 'px';
    animRenderGridOverlay();
}

// 网格叠层：在独立 canvas 上按设备像素画 1px 细线。
// 只画内部格线（1..n-1），外框由 .anim-draw-box 的边框承担 —— 四边等宽，
// 不会出现"一边有线一边没有"；线位取整 +0.5，保证是 1 个设备像素而不是两条灰边。
function animRenderGridOverlay() {
    var overlay = document.getElementById('animDrawGrid');
    var draw = document.getElementById('animDraw');
    if (!overlay || !draw) return;

    var w = draw.clientWidth, h = draw.clientHeight;
    if (w <= 0 || h <= 0) return;

    var dpr = window.devicePixelRatio || 1;
    overlay.width = Math.round(w * dpr);
    overlay.height = Math.round(h * dpr);
    overlay.style.width = w + 'px';
    overlay.style.height = h + 'px';

    var ctx = overlay.getContext('2d');
    ctx.clearRect(0, 0, overlay.width, overlay.height);
    ctx.lineWidth = 1;
    ctx.strokeStyle = (getComputedStyle(overlay).getPropertyValue('--anim-grid-line') || '').trim()
                      || 'rgba(0,0,0,.16)';

    var i, p;
    ctx.beginPath();
    for (i = 1; i < ANIM_GRID_PW; i++) {
        p = Math.round(i / ANIM_GRID_PW * overlay.width) + 0.5;
        ctx.moveTo(p, 0);
        ctx.lineTo(p, overlay.height);
    }
    for (i = 1; i < ANIM_GRID_PH; i++) {
        p = Math.round(i / ANIM_GRID_PH * overlay.height) + 0.5;
        ctx.moveTo(0, p);
        ctx.lineTo(overlay.width, p);
    }
    ctx.stroke();
}

function animRenderDrawCanvas() {
    var canvas = document.getElementById('animDraw');
    if (!canvas) return;
    var ctx = canvas.getContext('2d');

    // 兜底自愈：绘制盘内部尺寸固定（48×72 格 × 12px）。机型切换时全局的
    // onDeviceTypeChanged() 会按分辨率重设所有 canvas[id]，虽然 index.html 已用
    // data-fixed-size 豁免，这里再对齐一次，避免任何后续代码把它改回横屏。
    var needW = ANIM_GRID_PW * ANIM_DRAW_CELL;
    var needH = ANIM_GRID_PH * ANIM_DRAW_CELL;
    if (canvas.width !== needW || canvas.height !== needH) {
        canvas.width = needW;
        canvas.height = needH;
    }

    var W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);

    var frame = animCurrentFrame();
    if (!frame) {
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, W, H);
        return;
    }

    // 绘制盘是竖屏（48×72 格），逻辑网格是横屏（72×48 格）
    var cellP = W / ANIM_GRID_PW;

    if (!frame.grid) {
        ctx.imageSmoothingEnabled = false;
        animDrawRotated(ctx, frame.canvas, W, H);
        return;
    }

    ctx.fillStyle = ANIM_PALETTE_CSS[0];
    ctx.fillRect(0, 0, W, H);

    // 洋葱皮：上一帧淡化垫底，当前帧的空格透出它（同一角度，才能与网格对齐）
    if (animOnion && animCurrent > 0) {
        ctx.save();
        ctx.globalAlpha = 0.25;
        ctx.imageSmoothingEnabled = false;
        animDrawRotated(ctx, animFrames[animCurrent - 1].canvas, W, H);
        ctx.restore();
    }

    for (var gy = 0; gy < ANIM_GRID_H; gy++) {
        for (var gx = 0; gx < ANIM_GRID_W; gx++) {
            var idx = frame.grid[gy * ANIM_GRID_W + gx];
            if (idx === 0) continue;
            var p = animGridToPortrait(gx, gy);
            ctx.fillStyle = ANIM_PALETTE_CSS[idx];
            ctx.fillRect(Math.round(p.px * cellP), Math.round(p.py * cellP),
                         Math.ceil(cellP), Math.ceil(cellP));
        }
    }
}

// 指针位置 → 逻辑网格格（竖屏显示 → 反推横屏逻辑坐标）
function animDrawCellFromEvent(e) {
    var canvas = document.getElementById('animDraw');
    if (!canvas) return null;
    var rect = canvas.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return null;
    var px = Math.floor((e.clientX - rect.left) / rect.width * ANIM_GRID_PW);
    var py = Math.floor((e.clientY - rect.top) / rect.height * ANIM_GRID_PH);
    if (px < 0 || px >= ANIM_GRID_PW || py < 0 || py >= ANIM_GRID_PH) return null;
    var g = animPortraitToGrid(px, py);
    return { x: g.gx, y: g.gy, px: px, py: py };
}

function animPushUndo(frame) {
    if (!frame || !frame.grid) return;
    frame.undo.push(frame.grid.slice());
    if (frame.undo.length > 24) {
        frame.undo.shift();
    }
}

function animSetCell(frame, x, y, value) {
    if (x < 0 || x >= ANIM_GRID_W || y < 0 || y >= ANIM_GRID_H) return;
    frame.grid[y * ANIM_GRID_W + x] = value;
    if (animSymmetry) {
        // 竖屏视角下镜像轴是"竖直中线"（画面左右对称）→ 在横屏逻辑网格里表现为翻转 y
        var my = ANIM_GRID_H - 1 - y;
        frame.grid[my * ANIM_GRID_W + x] = value;
    }
}

// 两点之间补格，避免快速拖动漏格
function animStrokeLine(frame, from, to, value) {
    var x0 = from.x, y0 = from.y, x1 = to.x, y1 = to.y;
    var dx = Math.abs(x1 - x0), dy = Math.abs(y1 - y0);
    var sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    var err = dx - dy;
    for (var guard = 0; guard < 4096; guard++) {
        animSetCell(frame, x0, y0, value);
        if (x0 === x1 && y0 === y1) break;
        var e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

function animFloodFill(frame, x, y, value) {
    var target = frame.grid[y * ANIM_GRID_W + x];
    if (target === value) return;
    var stack = [y * ANIM_GRID_W + x];
    var seen = new Uint8Array(ANIM_GRID_W * ANIM_GRID_H);
    while (stack.length) {
        var p = stack.pop();
        if (seen[p]) continue;
        seen[p] = 1;
        if (frame.grid[p] !== target) continue;
        frame.grid[p] = value;
        var px = p % ANIM_GRID_W, py = (p / ANIM_GRID_W) | 0;
        if (px > 0) stack.push(p - 1);
        if (px < ANIM_GRID_W - 1) stack.push(p + 1);
        if (py > 0) stack.push(p - ANIM_GRID_W);
        if (py < ANIM_GRID_H - 1) stack.push(p + ANIM_GRID_W);
    }
}

function animAfterEdit(frame) {
    animRenderGrid(frame);
    animRenderThumb(frame);
    animRenderDrawCanvas();
    var item = document.querySelector('.anim-frame[data-index="' + animCurrent + '"]');
    if (item) {
        var old = item.querySelector('canvas');
        if (old && old !== frame.thumb) {
            item.replaceChild(frame.thumb, old);
        }
    }
    animUpdateStats();
    if (!animPlaying) {
        animRenderStage();
    }
}

function animInitDrawInteraction() {
    var canvas = document.getElementById('animDraw');
    if (!canvas) return;

    function valueForTool(frame, cell) {
        if (animTool === 'eraser') return 0;
        if (animTool === 'pick') {
            animColorIndex = frame.grid[cell.y * ANIM_GRID_W + cell.x];
            animRenderPalette();
            return null;
        }
        return animColorIndex;
    }

    canvas.addEventListener('pointerdown', function (e) {
        if (animBusy) return;
        var frame = animCurrentFrame();
        if (!frame) return;
        canvas.setPointerCapture && canvas.setPointerCapture(e.pointerId);
        var cell = animDrawCellFromEvent(e);
        if (!cell) return;
        e.preventDefault();

        if (!frame.grid) {
            animRasterizeFrame(frame);
            showMessage('已把位图帧栅格化为 72×48 像素网格', 'info');
        }

        if (animTool === 'fill') {
            animPushUndo(frame);
            animFloodFill(frame, cell.x, cell.y, animColorIndex);
            animAfterEdit(frame);
            return;
        }

        var value = valueForTool(frame, cell);
        if (value === null) return;   // 吸管

        animPushUndo(frame);
        animDrawing = true;
        animLastCell = cell;
        animSetCell(frame, cell.x, cell.y, value);
        animAfterEdit(frame);
    });

    canvas.addEventListener('pointermove', function (e) {
        if (!animDrawing || animBusy) return;
        var frame = animCurrentFrame();
        if (!frame || !frame.grid) return;
        var cell = animDrawCellFromEvent(e);
        if (!cell) return;
        e.preventDefault();
        var value = (animTool === 'eraser') ? 0 : animColorIndex;
        if (animLastCell) {
            animStrokeLine(frame, animLastCell, cell, value);
        } else {
            animSetCell(frame, cell.x, cell.y, value);
        }
        animLastCell = cell;
        animRenderGrid(frame);
        animRenderThumb(frame);
        animRenderDrawCanvas();
    });

    function endStroke() {
        if (!animDrawing) return;
        animDrawing = false;
        animLastCell = null;
        var frame = animCurrentFrame();
        if (frame) animAfterEdit(frame);
    }
    canvas.addEventListener('pointerup', endStroke);
    canvas.addEventListener('pointercancel', endStroke);
    canvas.addEventListener('pointerleave', function () { animDrawing = false; animLastCell = null; });
}

function animInitPalette() {
    var box = document.getElementById('animPalette');
    if (!box) return;
    box.innerHTML = '';
    ANIM_PALETTE.forEach(function (c, i) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'anim-swatch' + (i === animColorIndex ? ' on' : '') + (i === 0 ? ' is-bg' : '');
        b.style.background = ANIM_PALETTE_CSS[i];
        b.dataset.index = i;
        b.title = i === 0 ? '白（底色）' : '颜色 ' + i;
        b.addEventListener('click', function () {
            animColorIndex = i;
            if (animTool === 'eraser' || animTool === 'pick') {
                animSetTool('pen');
            }
            animRenderPalette();
        });
        box.appendChild(b);
    });
}

function animRenderPalette() {
    var box = document.getElementById('animPalette');
    if (!box) return;
    var items = box.querySelectorAll('.anim-swatch');
    for (var i = 0; i < items.length; i++) {
        items[i].classList.toggle('on', +items[i].dataset.index === animColorIndex);
    }
}

function animSetTool(tool) {
    animTool = tool;
    var tools = document.querySelectorAll('[data-anim-tool]');
    for (var i = 0; i < tools.length; i++) {
        tools[i].classList.toggle('active', tools[i].dataset.animTool === tool);
    }
}

function animHandleTool(tool) {
    if (tool === 'undo') {
        var frame = animCurrentFrame();
        if (!frame || !frame.grid || frame.undo.length === 0) {
            showMessage('没有可撤销的操作', 'info');
            return;
        }
        frame.grid = frame.undo.pop();
        animAfterEdit(frame);
        return;
    }
    if (tool === 'clear') {
        var f = animCurrentFrame();
        if (!f) return;
        animPushUndo(f);
        if (!f.grid) {
            var ctx = f.canvas.getContext('2d');
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(0, 0, ANIM_W, ANIM_H);
            animRenderThumb(f);
            animAfterEdit(f);
            return;
        }
        f.grid.fill(0);
        animAfterEdit(f);
        return;
    }
    animSetTool(tool);
}

/* ============================================================
   10. 创作方式面板
   ============================================================ */

function animSetMode(mode) {
    animMode = mode;
    var btns = document.querySelectorAll('[data-anim-mode]');
    for (var i = 0; i < btns.length; i++) {
        btns[i].classList.toggle('active', btns[i].dataset.animMode === mode);
    }
    var panes = ['draw', 'images', 'gif', 'camera'];
    panes.forEach(function (p) {
        var el = document.getElementById('anim-pane-' + p);
        if (el) el.classList.toggle('active', p === mode);
    });
    if (mode !== 'camera') {
        animStopCamera();
    }
    if (mode === 'draw') {
        animRenderDrawCanvas();
        animUpdateDrawSize();
    }
}

/* ---- 逐帧图片 ---- */

function animHandleImageFiles(files) {
    var list = Array.prototype.slice.call(files).filter(function (f) {
        return f.type && f.type.indexOf('image/') === 0;
    });
    if (list.length === 0) {
        showMessage('请选择图片文件', 'error');
        return;
    }
    var remaining = ANIM_MAX_FRAMES - animFrames.length;
    if (remaining <= 0) {
        showMessage('最多 ' + ANIM_MAX_FRAMES + ' 帧', 'warning');
        return;
    }
    if (list.length > remaining) {
        list = list.slice(0, remaining);
        showMessage('已达帧数上限，只导入前 ' + remaining + ' 张', 'warning');
    }

    var loaded = 0;
    var ordered = new Array(list.length);
    list.forEach(function (file, i) {
        var reader = new FileReader();
        reader.onload = function (ev) {
            var img = new Image();
            img.onload = function () {
                ordered[i] = animNewImageFrame(img);
                loaded++;
                if (loaded === list.length) {
                    animAppendFrames(ordered);
                }
            };
            img.onerror = function () { loaded++; if (loaded === list.length) animAppendFrames(ordered.filter(Boolean)); };
            img.src = ev.target.result;
        };
        reader.onerror = function () { loaded++; if (loaded === list.length) animAppendFrames(ordered.filter(Boolean)); };
        reader.readAsDataURL(file);
    });
}

function animAppendFrames(frames) {
    frames = (frames || []).filter(function (f) { return !!f; });
    if (frames.length === 0) return;
    var first = animFrames.length === 0;
    frames.forEach(function (f) {
        animFrames.push(f);
        animRenderThumb(f);
    });
    if (first) {
        animCurrent = 0;
        animRefreshFrameSelection();
    }
    animRefreshFrames();
    showMessage('已加入 ' + frames.length + ' 帧', 'success');
}

/* ---- GIF 拆帧 ---- */

function animHandleGifFile(file) {
    if (!file) return;
    if (typeof decodeGifFrames !== 'function') {
        showMessage('GIF 解码器未加载', 'error');
        return;
    }
    var reader = new FileReader();
    reader.onload = function (ev) {
        var gifFrames;
        try {
            gifFrames = decodeGifFrames(ev.target.result);
        } catch (err) {
            showMessage('GIF 解析失败：' + err.message, 'error');
            return;
        }
        if (!gifFrames.length) {
            showMessage('GIF 没有可用帧', 'error');
            return;
        }

        // 帧数超限则等间隔抽样
        var picked = gifFrames;
        if (picked.length > ANIM_MAX_FRAMES) {
            var step = picked.length / ANIM_MAX_FRAMES;
            var sampled = [];
            for (var i = 0; i < ANIM_MAX_FRAMES; i++) {
                sampled.push(picked[Math.floor(i * step)]);
            }
            picked = sampled;
            showMessage('GIF 帧数较多，已抽取 ' + ANIM_MAX_FRAMES + ' 帧', 'warning');
        }

        var room = ANIM_MAX_FRAMES - animFrames.length;
        if (room <= 0) {
            showMessage('已达帧数上限', 'warning');
            return;
        }
        if (picked.length > room) {
            picked = picked.slice(0, room);
        }

        // 用 GIF 的平均帧延时同步预览帧率与设备帧间隔（同一份语义：每帧停留多久）
        var totalDelay = 0;
        picked.forEach(function (f) { totalDelay += (f.delayMs || 100); });
        var avgDelay = totalDelay / picked.length;
        animSetFps(Math.max(1, Math.min(24, Math.round(1000 / avgDelay))));

        var frames = picked.map(function (f) {
            return animNewImageFrame(f.imageData);
        });
        animAppendFrames(frames);
        showMessage('GIF 已拆成 ' + frames.length + ' 帧（平均 ' + Math.round(avgDelay) + ' ms/帧）', 'success');
    };
    reader.onerror = function () { showMessage('GIF 读取失败', 'error'); };
    reader.readAsArrayBuffer(file);
}

/* ---- 定格拍摄 ---- */

function animStartCamera() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        showMessage('当前浏览器不支持相机功能', 'error');
        return;
    }
    navigator.mediaDevices.getUserMedia({
        video: { facingMode: 'environment', width: { ideal: 1280 }, height: { ideal: 960 } }
    }).then(function (stream) {
        animCameraStream = stream;
        var video = document.getElementById('animVideo');
        video.srcObject = stream;
        video.play();
        document.getElementById('animVideoWrap').style.display = 'block';
        document.getElementById('animCameraBtn').style.display = 'none';
    }).catch(function (err) {
        showMessage('无法访问相机: ' + err.message, 'error');
    });
}

function animCapturePhoto() {
    var video = document.getElementById('animVideo');
    if (!video || !video.videoWidth) {
        showMessage('相机尚未就绪', 'warning');
        return;
    }
    var tmp = document.createElement('canvas');
    tmp.width = video.videoWidth;
    tmp.height = video.videoHeight;
    tmp.getContext('2d').drawImage(video, 0, 0);
    animAppendFrames([animNewImageFrame(tmp)]);
}

function animStopCamera() {
    if (animCameraStream) {
        animCameraStream.getTracks().forEach(function (t) { t.stop(); });
        animCameraStream = null;
    }
    var video = document.getElementById('animVideo');
    if (video) {
        video.srcObject = null;
    }
    var wrap = document.getElementById('animVideoWrap');
    if (wrap) wrap.style.display = 'none';
    var btn = document.getElementById('animCameraBtn');
    if (btn) btn.style.display = '';
}

/* ============================================================
   11. 下载 / 上传 / 设备播放
   ============================================================ */

function animSanitizeFileName(raw) {
    var name = (raw || '').trim() || 'anim.film';
    name = name.replace(/[\\/]+/g, '_');          // 含 '/' 会被设备当成显式路径
    return normalizeFilmFileName(name, 'anim.film');
}

function animSetResult(html, cls) {
    var el = document.getElementById('anim-result');
    if (!el) return;
    el.className = 'result' + (cls ? ' ' + cls : '');
    el.innerHTML = html;
}

function animDownload() {
    if (animFrames.length === 0) {
        showMessage('还没有任何帧', 'error');
        return;
    }
    var formatKey = animSelectedFormat();
    var bytes;
    try {
        bytes = animBuildFilmFile(formatKey, animDither).bytes;
    } catch (err) {
        showMessage('打包失败：' + err.message, 'error');
        return;
    }
    downloadFile(bytes, animSanitizeFileName(document.getElementById('animFileName').value));
    animSetResult('<b>已下载</b> ' + ANIM_FORMATS[formatKey].label +
        ' · ' + animFrames.length + ' 帧 · ' + formatFileSize(bytes.length), 'success');
    showMessage('已生成 ' + formatFileSize(bytes.length) + ' 的动画', 'success');
}

function animUpdateTransfer(label, percent) {
    var statusEl = document.getElementById('anim-transfer-status');
    var barEl = document.getElementById('anim-transfer-progress-bar');
    var pctEl = document.getElementById('anim-transfer-progress');
    if (statusEl) statusEl.textContent = label;
    if (barEl) barEl.style.width = percent + '%';
    if (pctEl) pctEl.textContent = percent + '%';
}

async function animUpload() {
    if (animBusy) return;
    if (animFrames.length === 0) {
        showMessage('还没有任何帧', 'error');
        return;
    }
    if (typeof device === 'undefined' || !device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    if (!animDeviceSupported()) {
        showMessage('动画（多帧 .film）仅通行证版（Ark）的 3.7 寸屏可播放，当前设备不支持', 'error');
        return;
    }

    var formatKey = animSelectedFormat();
    var fmt = ANIM_FORMATS[formatKey];
    // 先按帧数与帧长估算，避免在超限时先分配整包造成移动端卡死/OOM。
    var estimatedBytes = 32 + fmt.frameBytes * animFrames.length;
    if (estimatedBytes > ANIM_MAX_BYTES) {
        showMessage('文件 ' + formatFileSize(estimatedBytes) + ' 过大，设备无法整帧载入（上限约 2.5 MB）', 'error');
        return;
    }
    var built;
    try {
        built = animBuildFilmFile(formatKey, animDither);
    } catch (err) {
        showMessage('打包失败：' + err.message, 'error');
        return;
    }
    var fileData = built.bytes;

    if (fileData.length > ANIM_MAX_BYTES) {
        showMessage('文件 ' + formatFileSize(fileData.length) + ' 过大，设备无法整帧载入（上限约 2.5 MB）', 'error');
        return;
    }

    var fileName = animSanitizeFileName(document.getElementById('animFileName').value);
    var container = document.getElementById('anim-transfer-container');
    if (container) container.style.display = 'block';
    animUpdateTransfer('准备传输…', 0);
    animSetResult('');

    animBusy = true;
    animStop();

    try {
        assertFilmTransferIdle();
        filmTransState = BLE_FILM_TRANS_STATE_STARTED;
        filmTransFileName = fileName;
        filmTransFileSize = fileData.length;
        filmTransSentBytes = 0;

        await sendBleFileStart();
        await sendBleFileName(fileName);
        await sendBleFileLen(fileData.length);

        var chunkSize = BLE_CHUNK_SIZE;
        var sent = 0;
        for (var i = 0; i < fileData.length; i += chunkSize) {
            var chunk = fileData.slice(i, i + chunkSize);
            await sendBleFileData(chunk);
            sent += chunk.length;
            filmTransSentBytes = sent;
            animUpdateTransfer('传输中 ' + formatFileSize(sent) + ' / ' + formatFileSize(fileData.length),
                Math.round(sent / fileData.length * 100));
        }

        await sendBleFileStop(true);     // 静默保存，避免设备逐帧刷屏
        filmTransState = BLE_FILM_TRANS_STATE_IDLE;
        animUpdateTransfer('已完成', 100);

        var played = false;
        if (animDeviceReady() && animUploadAfterPlay) {
            animUpdateTransfer('正在切换到设备动画…', 100);
            played = await animPlayOnDevice(true);
        }

        animSetResult('<b>已写入设备</b> ' + animEscape(fileName) +
            ' · ' + animFrames.length + ' 帧 · ' + formatFileSize(fileData.length) +
            '<br><span class="anim-result__sub">' +
            (played
                ? '设备已切到动画并在播放（多帧文件自动归档到 /sdcard/animation）'
                : '已归档到 /sdcard/animation，可在设备「动画」里查看') +
            '</span>', 'success');
        showMessage('动画已发送到设备', 'success');
    } catch (err) {
        filmTransState = BLE_FILM_TRANS_STATE_IDLE;
        legacyFilmTransferActive = false;
        animUpdateTransfer('传输失败：' + err.message, 0);
        animSetResult('<b>传输失败</b> ' + animEscape(err.message), 'error');
        showMessage('传输失败：' + err.message, 'error');
    } finally {
        animBusy = false;
    }
}

// 切到设备动画 app + 下发播放参数
async function animPlayOnDevice(silentParams) {
    if (typeof device === 'undefined' || !device || !server || !characteristic) {
        if (!silentParams) showMessage('请先连接设备', 'error');
        return false;
    }
    if (!animDeviceSupported()) {
        if (!silentParams) showMessage('动画播放仅支持通行证版（Ark）的 3.7 寸屏', 'warning');
        return false;
    }
    try {
        await sendBleAnimParams(animSelectedPlayMode(), animFrameMs(), animLoopSeconds());
        await sendBleAppSwitch(BLE_APP_ID_ANIMATION);
        return true;
    } catch (err) {
        if (!silentParams) showMessage('下发失败：' + err.message, 'error');
        return false;
    }
}

function animSelectedPlayMode() {
    var btn = document.querySelector('[data-anim-playmode].active');
    return btn ? +btn.dataset.animPlaymode : 0;
}

function animFrameMs() {
    var el = document.getElementById('animFrameMs');
    return el ? +el.value : 200;
}

function animLoopSeconds() {
    var el = document.getElementById('animLoopSec');
    return el ? +el.value : 0;
}

/* ============================================================
   12. 帧率 / 设备参数控件
   ============================================================ */

function animSetFps(v) {
    animFps = v;
    var input = document.getElementById('animFps');
    if (input) input.value = v;
    var label = document.getElementById('animFpsVal');
    if (label) label.textContent = v;
    var msEl = document.getElementById('animFrameMs');
    if (msEl) {
        var ms = Math.max(100, Math.min(2000, Math.round(1000 / v / 100) * 100));
        msEl.value = ms;
        animSyncFrameMsLabel();
    }
    if (animPlaying) {
        animStop();
        animStart();
    }
}

function animSyncFrameMsLabel() {
    var el = document.getElementById('animFrameMs');
    var label = document.getElementById('animFrameMsVal');
    if (el && label) label.textContent = el.value + ' ms';
}

function animSyncLoopLabel() {
    var el = document.getElementById('animLoopSec');
    var label = document.getElementById('animLoopSecVal');
    if (el && label) label.textContent = (+el.value === 0 ? '不等待' : el.value + ' s');
}

/* ============================================================
   13. 设备可用性联动
   ============================================================ */

// 导航项可见性：动画页默认不出现，只有连上能播动画的通行证版（Ark 3.7"）才亮出来。
// 断开时若正停在动画页，退回 Film 页 —— 否则会停在一个已经隐藏的页面上。
function animSyncNavVisibility() {
    var item = document.querySelector('.nav-item[data-page="anim-page"]');
    if (!item) return;
    var show = animDeviceReady();
    document.body.classList.toggle('show-anim-tab', show);
    if (!show && item.classList.contains('active')) {
        var fallback = document.querySelector('.nav-item[data-page="convert-page"]');
        if (fallback) fallback.click();
    }
}

function animSyncDeviceAvailability() {
    var panel = document.getElementById('animDevicePanel');
    var hint = document.getElementById('animDeviceHint');
    var connected = (typeof device !== 'undefined' && device && server && characteristic);
    var ready = animDeviceReady();

    animSyncNavVisibility();
    if (panel) {
        panel.classList.toggle('is-disabled', !ready);
    }
    if (hint) {
        hint.textContent = ready
            ? '下发播放模式 / 速度后会切到设备「动画」页立即播放'
            : '动画播放仅通行证版（Ark）的 3.7 寸（720×480）屏支持：连接后可一键在设备上播放（仍可下载 .film）';
    }
    // 连着设备但不能播动画（比如同屏的 Pro）→ 在输出区给一条明确提示，别让人传完才发现屏上没反应
    var warn = document.getElementById('animWarn');
    if (warn) {
        if (connected && !animDeviceSupported()) {
            warn.style.display = '';
            warn.textContent = '当前设备不支持动画：多帧 .film 需要通行证版（Ark）的 3.7 寸（720×480）屏才能播放，本机无法显示（仍可下载文件）。';
        } else {
            warn.style.display = 'none';
        }
    }
    animUpdateStats();
}

/* ============================================================
   14. 页面进入 / 离开（动画是独立的底部导航页）
   ============================================================ */

// 进入动画页：刷新一次帧列表与设备可用性（页面隐藏期间尺寸/设备可能变过）
function animOnPageEnter() {
    animRefreshFrames();
    animSyncDeviceAvailability();
    animRenderDrawCanvas();
    animUpdateDrawSize();
    animUpdateStageScale();
}

// 离开动画页：停播放与相机（省电、避免后台继续刷预览）
function animOnPageLeave() {
    animStop();
    animStopCamera();
}

/* ============================================================
   15. 初始化
   ============================================================ */

function initAnimationStudio() {
    // 底部导航：进动画页刷新一次，离开时停播放 / 相机
    document.querySelectorAll('.nav-item').forEach(function (item) {
        item.addEventListener('click', function () {
            if (item.getAttribute('data-page') === 'anim-page') {
                setTimeout(animOnPageEnter, 0);
            } else {
                animOnPageLeave();
            }
        });
    });

    // 创作方式
    var modeBtns = document.querySelectorAll('[data-anim-mode]');
    for (var m = 0; m < modeBtns.length; m++) {
        modeBtns[m].addEventListener('click', function () {
            animSetMode(this.dataset.animMode);
        });
    }

    // 绘制
    // 绘制盘：内部按竖屏网格出图（48×72 格），显示尺寸再按容器吸附到整格像素
    var draw = document.getElementById('animDraw');
    if (draw) {
        draw.width = ANIM_GRID_PW * ANIM_DRAW_CELL;
        draw.height = ANIM_GRID_PH * ANIM_DRAW_CELL;
    }
    animUpdateDrawSize();
    animInitDrawInteraction();
    animInitPalette();

    var toolBtns = document.querySelectorAll('[data-anim-tool]');
    for (var t = 0; t < toolBtns.length; t++) {
        toolBtns[t].addEventListener('click', function () {
            animHandleTool(this.dataset.animTool);
        });
    }
    var zoomBtn = document.getElementById('animDrawZoom');
    if (zoomBtn) {
        zoomBtn.addEventListener('click', function () {
            var wrap = document.getElementById('animDrawWrap');
            wrap.classList.toggle('is-zoom');
            zoomBtn.classList.toggle('active', wrap.classList.contains('is-zoom'));
            animUpdateDrawSize();
        });
    }

    // 逐帧图片
    var imgInput = document.getElementById('animImageInput');
    var imgBtn = document.getElementById('animImageBtn');
    if (imgBtn && imgInput) {
        imgBtn.addEventListener('click', function () { imgInput.click(); });
        imgInput.addEventListener('change', function (e) {
            // 必须先把 FileList 复制成数组再清空输入框：input.value = '' 会把同一个
            // FileList 一并清空，之后再用它长度就是 0（什么都不会发生）。
            var files = Array.prototype.slice.call(e.target.files || []);
            e.target.value = '';
            if (files.length) animHandleImageFiles(files);
        });
    }

    // GIF
    var gifInput = document.getElementById('animGifInput');
    var gifBtn = document.getElementById('animGifBtn');
    if (gifBtn && gifInput) {
        gifBtn.addEventListener('click', function () { gifInput.click(); });
        gifInput.addEventListener('change', function (e) {
            var file = e.target.files && e.target.files[0];
            e.target.value = '';
            if (file) animHandleGifFile(file);
        });
    }

    // 定格拍摄
    var camBtn = document.getElementById('animCameraBtn');
    var capBtn = document.getElementById('animCaptureBtn');
    if (camBtn) camBtn.addEventListener('click', animStartCamera);
    if (capBtn) capBtn.addEventListener('click', animCapturePhoto);

    // 帧操作
    var addBtn = document.getElementById('animFrameAdd');
    var dupBtn = document.getElementById('animFrameDup');
    var delBtn = document.getElementById('animFrameDel');
    var leftBtn = document.getElementById('animFrameLeft');
    var rightBtn = document.getElementById('animFrameRight');
    if (addBtn) addBtn.addEventListener('click', function () {
        animStop();
        animAddFrame(animNewPixelFrame(null));
    });
    if (dupBtn) dupBtn.addEventListener('click', function () {
        animStop();
        if (animCurrent < 0) { showMessage('先选中一帧', 'warning'); return; }
        animDuplicateFrame(animCurrent);
    });
    if (delBtn) delBtn.addEventListener('click', function () {
        animStop();
        if (animCurrent < 0) return;
        animRemoveFrame(animCurrent);
    });
    if (leftBtn) leftBtn.addEventListener('click', function () {
        animStop();
        if (animCurrent > 0) animMoveFrame(animCurrent, -1);
    });
    if (rightBtn) rightBtn.addEventListener('click', function () {
        animStop();
        if (animCurrent >= 0 && animCurrent < animFrames.length - 1) animMoveFrame(animCurrent, 1);
    });

    // 画面调整（仅位图帧）：取景框手势 + 旋转/重置
    animInitStageInteraction();
    var rotateBtn = document.getElementById('animRotateBtn');
    if (rotateBtn) rotateBtn.addEventListener('click', animRotateFrame);
    var fitBtn = document.getElementById('animFitBtn');
    if (fitBtn) fitBtn.addEventListener('click', animResetTransform);

    // 整体调整：开启后拖动/缩放/旋转同步到所有位图帧（帧多时省事）
    var applyAllSw = document.getElementById('animApplyAll');
    if (applyAllSw) applyAllSw.addEventListener('change', function () {
        animTransformAll = this.checked;
        animSyncTransformTools();
        if (animTransformAll) {
            var n = animOtherImageFrames(animCurrentFrame()).length;
            if (n > 0) showMessage('整体调整已开启：之后的拖动/缩放/旋转会同步到另外 ' + n + ' 个位图帧', 'success');
        }
    });

    // 播放
    var playBtn = document.getElementById('animPlayBtn');
    if (playBtn) playBtn.addEventListener('click', function () {
        if (animPlaying) animStop(); else animStart();
    });
    var fps = document.getElementById('animFps');
    if (fps) fps.addEventListener('input', function () {
        animSetFps(+this.value);
    });

    // 输出
    var fmtSel = document.getElementById('animFormat');
    if (fmtSel) fmtSel.addEventListener('change', function () {
        animUpdateStats();
        if (!animPlaying) animRenderStage();
    });
    var ditherSw = document.getElementById('animDither');
    if (ditherSw) ditherSw.addEventListener('change', function () {
        animDither = this.checked;
        if (!animPlaying) animRenderStage();
    });
    var symSw = document.getElementById('animSymmetry');
    if (symSw) symSw.addEventListener('change', function () { animSymmetry = this.checked; });
    var onionSw = document.getElementById('animOnion');
    if (onionSw) onionSw.addEventListener('change', function () {
        animOnion = this.checked;
        animRenderDrawCanvas();
    });
    var devPrevSw = document.getElementById('animDevicePreview');
    if (devPrevSw) devPrevSw.addEventListener('change', function () {
        animDevicePreview = this.checked;
        if (animPlaying) animRenderThumbFreeStage(); else animRenderStage();
    });
    var autoPlaySw = document.getElementById('animUploadPlay');
    if (autoPlaySw) autoPlaySw.addEventListener('change', function () {
        animUploadAfterPlay = this.checked;
    });

    var dlBtn = document.getElementById('animDownloadBtn');
    if (dlBtn) dlBtn.addEventListener('click', animDownload);
    var upBtn = document.getElementById('animUploadBtn');
    if (upBtn) upBtn.addEventListener('click', animUpload);

    // 设备播放设置
    var pmBtns = document.querySelectorAll('[data-anim-playmode]');
    for (var p = 0; p < pmBtns.length; p++) {
        pmBtns[p].addEventListener('click', function () {
            for (var k = 0; k < pmBtns.length; k++) pmBtns[k].classList.remove('active');
            this.classList.add('active');
        });
    }
    var msEl = document.getElementById('animFrameMs');
    if (msEl) msEl.addEventListener('input', animSyncFrameMsLabel);
    var loopEl = document.getElementById('animLoopSec');
    if (loopEl) loopEl.addEventListener('input', animSyncLoopLabel);
    var applyBtn = document.getElementById('animApplyBtn');
    if (applyBtn) applyBtn.addEventListener('click', function () {
        animPlayOnDevice(false).then(function (ok) {
            if (ok) showMessage('已切到设备动画并下发播放参数', 'success');
        });
    });

    animSetMode('draw');
    animSetFps(animFps);
    animSyncFrameMsLabel();
    animSyncLoopLabel();
    animRefreshFrames();
    animSyncDeviceAvailability();

    // 窗口尺寸变化时重新贴合取景框与绘制盘（两者都是"按容器算出来的像素尺寸"）
    window.addEventListener('resize', function () {
        animUpdateStageScale();
        animUpdateDrawSize();
    });
}

window.addEventListener('DOMContentLoaded', initAnimationStudio);

// 供 utils.js 的 onDeviceTypeChanged() 调用
window.animSyncDeviceAvailability = animSyncDeviceAvailability;
