// 照片转换功能

// 全局变量
var currentImageData = null;
var isDitheringEnabled = false;
var canvasRotation = 0; // 0: 原始, 1: 旋转90度
var scale = 1.0;
var offsetX = 0;
var offsetY = 0;
var isDragging = false;
var startX = 0;
var startY = 0;
var startOffsetX = 0;
var startOffsetY = 0;
var originalImage = null;
var uploadedFileName = 'output';
window.processedDataForDownload = null;

// 拖动偏移换算：横屏设备画布被 CSS rotate(90deg) 显示，竖屏设备（Max）不旋转。
// 两种显示方式下 canvasRotation 对应的坐标映射不同，需分别换算，使拖动方向与视觉一致。
function applyDragOffset(deltaX, deltaY) {
    var cssRotated = !isPortraitDevice(); // 画布是否被 CSS 旋转 90° 显示
    if (canvasRotation === 1) {
        if (cssRotated) {
            offsetX = startOffsetX + deltaX;
            offsetY = startOffsetY + deltaY;
        } else {
            offsetX = startOffsetX - deltaY;
            offsetY = startOffsetY + deltaX;
        }
    } else {
        if (cssRotated) {
            offsetX = startOffsetX + deltaY;
            offsetY = startOffsetY - deltaX;
        } else {
            offsetX = startOffsetX + deltaX;
            offsetY = startOffsetY + deltaY;
        }
    }
}

// Film 文件头大小（固定 32 字节）
var FILM_HEADER_SIZE = 32;

// 颜色编码索引（对应 ColorTable 的位置）
const COLOR_CODE_BLACK = 0x00;
const COLOR_CODE_WHITE = 0x01;
const COLOR_CODE_YELLOW = 0x02;
const COLOR_CODE_RED = 0x03;
const COLOR_CODE_BLUE = 0x04;
const COLOR_CODE_GREEN = 0x05;

// 固定的六色调色板（带颜色编码索引）
const rgbPalette = [
    { name: "黑色", r: 0, g: 0, b: 0, value: 0x00, code: COLOR_CODE_BLACK },
    { name: "白色", r: 255, g: 255, b: 255, value: 0xff, code: COLOR_CODE_WHITE },
    { name: "黄色", r: 255, g: 255, b: 0, value: 0xfc, code: COLOR_CODE_YELLOW },
    { name: "红色", r: 255, g: 0, b: 0, value: 0xe0, code: COLOR_CODE_RED },
    { name: "蓝色", r: 0, g: 0, b: 255, value: 0x03, code: COLOR_CODE_BLUE },
    { name: "绿色", r: 41, g: 204, b: 20, value: 0x1c, code: COLOR_CODE_GREEN }
];

function initConvertTool() {
    // 事件监听器
    document.getElementById('imageFile').addEventListener('change', handleFileUpload);
    document.getElementById('ditherStrength').addEventListener('input', function() {
        document.getElementById('ditherStrengthValue').textContent = this.value;
        debounceUpdateImage();
    });
    document.getElementById('contrast').addEventListener('input', function() {
        document.getElementById('contrastValue').textContent = this.value;
        debounceUpdateImage();
    });
    document.getElementById('ditherType').addEventListener('change', function() {
        document.getElementById('ditherStrengthContainer').style.display =
            this.value === 'adaptive' ? 'none' : '';
        debounceUpdateImage();
    });
    document.getElementById('ditherStrengthContainer').style.display =
        document.getElementById('ditherType').value === 'adaptive' ? 'none' : '';
    
    // 鼠标滚轮缩放功能
    const canvas = document.getElementById('canvas');
    canvas.addEventListener('wheel', function(e) {
        if (!isDitheringEnabled) {
            e.preventDefault();
            const scaleFactor = e.deltaY > 0 ? 0.9 : 1.1;
            const newScale = Math.max(0.05, Math.min(10, scale * scaleFactor));
            
            // 计算鼠标在画布上的位置
            const rect = canvas.getBoundingClientRect();
            const mouseX = e.clientX - rect.left;
            const mouseY = e.clientY - rect.top;
            
            // 调整偏移量，使缩放以鼠标位置为中心
            var relativeX = mouseX / getCanvasWidth();
            var relativeY = mouseY / getCanvasHeight();
            
            const oldWidth = originalImage.width * scale;
            const oldHeight = originalImage.height * scale;
            const newWidth = originalImage.width * newScale;
            const newHeight = originalImage.height * newScale;
            
            offsetX = relativeX * (newWidth - oldWidth) + offsetX;
            offsetY = relativeY * (newHeight - oldHeight) + offsetY;
            
            scale = newScale;
            updateImage();
        }
    });
    
    // 鼠标拖动功能
    canvas.addEventListener('mousedown', function(e) {
        if (!isDitheringEnabled) {
            isDragging = true;
            startX = e.clientX;
            startY = e.clientY;
            startOffsetX = offsetX;
            startOffsetY = offsetY;
        }
    });

    canvas.addEventListener('mousemove', function(e) {
        if (isDragging && !isDitheringEnabled) {
            // 计算鼠标移动距离
            const deltaX = e.clientX - startX;
            const deltaY = e.clientY - startY;
            
            // 根据画布显示方式与旋转状态换算偏移，使拖动方向与视觉一致
            applyDragOffset(deltaX, deltaY);
            
            updateImage();
        }
    });
    
    canvas.addEventListener('mouseup', function() {
        isDragging = false;
    });
    
    canvas.addEventListener('mouseleave', function() {
        isDragging = false;
    });

    // 触摸事件支持（移动端）
    let touchStartX = 0;
    let touchStartY = 0;
    let touchStartDistance = 0;
    let isPinching = false;

    canvas.addEventListener('touchstart', function(e) {
        if (!isDitheringEnabled) {
            if (e.touches.length === 1) {
                isDragging = true;
                startX = e.touches[0].clientX;
                startY = e.touches[0].clientY;
                startOffsetX = offsetX;
                startOffsetY = offsetY;
            } else if (e.touches.length === 2) {
                isPinching = true;
                const touch1 = e.touches[0];
                const touch2 = e.touches[1];
                touchStartDistance = Math.sqrt(
                    Math.pow(touch2.clientX - touch1.clientX, 2) +
                    Math.pow(touch2.clientY - touch1.clientY, 2)
                );
            }
            e.preventDefault();
        }
    });

    canvas.addEventListener('touchmove', function(e) {
        if (isDitheringEnabled) return;
        
        if (e.touches.length === 1 && isDragging) {
            // 单指拖动
            const deltaX = e.touches[0].clientX - startX;
            const deltaY = e.touches[0].clientY - startY;
            
            applyDragOffset(deltaX, deltaY);
            
            updateImage();
        } else if (e.touches.length === 2 && isPinching) {
            // 双指缩放
            const touch1 = e.touches[0];
            const touch2 = e.touches[1];
            const currentDistance = Math.sqrt(
                Math.pow(touch2.clientX - touch1.clientX, 2) +
                Math.pow(touch2.clientY - touch1.clientY, 2)
            );
            
            const scaleFactor = currentDistance / touchStartDistance;
            const newScale = Math.max(0.05, Math.min(10, scale * scaleFactor));
            
            // 计算触摸中心点
            const centerX = (touch1.clientX + touch2.clientX) / 2;
            const centerY = (touch1.clientY + touch2.clientY) / 2;
            const rect = canvas.getBoundingClientRect();
            var relativeX = (centerX - rect.left) / getCanvasWidth();
            var relativeY = (centerY - rect.top) / getCanvasHeight();
            
            const oldWidth = originalImage.width * scale;
            const oldHeight = originalImage.height * scale;
            const newWidth = originalImage.width * newScale;
            const newHeight = originalImage.height * newScale;
            
            offsetX = relativeX * (newWidth - oldWidth) + offsetX;
            offsetY = relativeY * (newHeight - oldHeight) + offsetY;
            
            scale = newScale;
            touchStartDistance = currentDistance;
            updateImage();
        }
        e.preventDefault();
    });

    canvas.addEventListener('touchend', function(e) {
        isDragging = false;
        isPinching = false;
    });

    updateCanvasScale();
    window.addEventListener('resize', updateCanvasScale);
}

function downloadFile(data, fileName) {
    const blob = new Blob([data], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = fileName;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

// 生成 Film 文件头（32字节）
function generateFilmHeader() {
    var screenWidth = getCanvasWidth();
    var screenHeight = getCanvasHeight();
    var pixelDataSize = getFilmPixelDataSize();
    var header = new Array(FILM_HEADER_SIZE).fill(0);

    // FileSize (4 bytes) - 小端序
    var fileSize = pixelDataSize;
    header[0] = fileSize & 0xFF;
    header[1] = (fileSize >> 8) & 0xFF;
    header[2] = (fileSize >> 16) & 0xFF;
    header[3] = (fileSize >> 24) & 0xFF;

    // ScreenWidth (2 bytes) - 小端序
    header[4] = screenWidth & 0xFF;
    header[5] = (screenWidth >> 8) & 0xFF;

    // ScreenHeight (2 bytes) - 小端序
    header[6] = screenHeight & 0xFF;
    header[7] = (screenHeight >> 8) & 0xFF;

    // ColorCount (1 byte) - 6色
    header[8] = 6;

    // Reserved (7 bytes) - 保留字段，填充0
    // header[9] 到 header[15] 保持为0

    // ColorTable (16 bytes) - 颜色编码映射表
    // ColorTable[编码索引] = 实际颜色值（color_get 函数接收的输入）
    // 根据 hal_epd.c 中 color_get 的定义：
    // 0x00 -> Black, 0xFF -> White, 0xFC -> Yellow, 0xE0 -> Red, 0x03 -> Blue, 0x1C -> Green
    header[16] = 0x00;  // 编码0 -> 黑色 0x00
    header[17] = 0xFF;  // 编码1 -> 白色 0xFF
    header[18] = 0xFC;  // 编码2 -> 黄色 0xFC
    header[19] = 0xE0;  // 编码3 -> 红色 0xE0
    header[20] = 0x03;  // 编码4 -> 蓝色 0x03
    header[21] = 0x1C;  // 编码5 -> 绿色 0x1C
    // header[22] 到 header[31] 保持为0（未使用）

    return new Uint8Array(header);
}

function handleFileUpload(event) {
    const file = event.target.files[0];
    event.target.value = '';

    const fileNameWithoutExt = file.name.replace(/\.[^/.]+$/, '');
    uploadedFileName = fileNameWithoutExt || 'output';

    const reader = new FileReader();

    reader.onload = function (e) {
        const img = new Image();
        img.onload = function () {
            originalImage = img;

            var canvas = document.getElementById('canvas');
            var canvasWidth = getCanvasWidth();
            var canvasHeight = getCanvasHeight();

            var imgWidth = img.width;
            var imgHeight = img.height;

            // 竖屏设备（Max）：横图旋转 90°，竖图直接显示；横向设备：竖图旋转
            canvasRotation = isPortraitDevice()
                ? (imgWidth > imgHeight ? 1 : 0)
                : (imgHeight > imgWidth ? 1 : 0);

            let effectiveWidth = canvasWidth;
            let effectiveHeight = canvasHeight;
            if (canvasRotation === 1) {
                effectiveWidth = canvasHeight;
                effectiveHeight = canvasWidth;
            }

            const scaleX = effectiveWidth / imgWidth;
            const scaleY = effectiveHeight / imgHeight;
            scale = Math.min(scaleX, scaleY);

            offsetX = 0;
            offsetY = 0;

            document.getElementById('fileName').value = uploadedFileName + '.film';
            updateImage();
        };
        img.src = e.target.result;
    };
    reader.readAsDataURL(file);
}

function toggleDither() {
    isDitheringEnabled = !isDitheringEnabled;
    const toggleButton = document.getElementById('toggleDither');
    toggleButton.textContent = isDitheringEnabled ? '禁用抖动' : '启用抖动';
    toggleButton.classList.toggle('active', isDitheringEnabled);
    updateImage();
}

function resetImage() {
    currentImageData = null;
    originalImage = null;
    uploadedFileName = 'output';
    canvasRotation = 0;
    scale = 1.0;
    offsetX = 0;
    offsetY = 0;
    isDragging = false;
    startX = 0;
    startY = 0;
    const canvas = document.getElementById('canvas');
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, getCanvasWidth(), getCanvasHeight());
    document.getElementById('imageResult').innerHTML = '';
    document.getElementById('fileName').value = 'output.film';
}

function rotateCanvas() {
    canvasRotation = (canvasRotation + 1) % 2;

    var canvas = document.getElementById('canvas');
    var canvasWidth = getCanvasWidth();
    var canvasHeight = getCanvasHeight();
    var effectiveWidth, effectiveHeight;
    if (canvasRotation === 0) {
        effectiveWidth = canvasWidth;
        effectiveHeight = canvasHeight;
    } else {
        effectiveWidth = canvasHeight;
        effectiveHeight = canvasWidth;
    }

    const imgWidth = originalImage.width * scale;
    const imgHeight = originalImage.height * scale;

    const scaleX = effectiveWidth / originalImage.width;
    const scaleY = effectiveHeight / originalImage.height;
    const newScale = Math.min(scaleX, scaleY, 1);

    scale = newScale;

    const scaledWidth = originalImage.width * scale;
    const scaledHeight = originalImage.height * scale;
    offsetX = (effectiveWidth - scaledWidth) / 2;
    offsetY = (effectiveHeight - scaledHeight) / 2;

    updateImage();
}

let _rafId = null;
let _debounceTimer = null;
function debounceUpdateImage() {
    if (_debounceTimer) clearTimeout(_debounceTimer);
    _debounceTimer = setTimeout(function() {
        updateImage();
    }, 150);
}

function resetZoom() {
    if (!originalImage) return;
    var canvas = document.getElementById('canvas');
    var canvasWidth = getCanvasWidth();
    var canvasHeight = getCanvasHeight();
    var effectiveWidth = canvasRotation === 1 ? canvasHeight : canvasWidth;
    var effectiveHeight = canvasRotation === 1 ? canvasWidth : canvasHeight;
    const scaleX = effectiveWidth / originalImage.width;
    const scaleY = effectiveHeight / originalImage.height;
    scale = Math.min(scaleX, scaleY);
    offsetX = 0;
    offsetY = 0;
    updateImage();
}

function updateImage() {
    if (!originalImage) return;

    var canvas = document.getElementById('canvas');
    var canvasWidth = getCanvasWidth();
    var canvasHeight = getCanvasHeight();
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    var ctx = canvas.getContext('2d');

    // 清除画布
    ctx.clearRect(0, 0, canvasWidth, canvasHeight);

    // 保存当前状态
    ctx.save();

    let effectiveWidth = canvasWidth;
    let effectiveHeight = canvasHeight;

    // 处理旋转
    if (canvasRotation === 1) {
        // 旋转-90度（向左旋转）
        ctx.translate(0, canvasHeight);
        ctx.rotate(-Math.PI / 2);
        // 交换宽高
        effectiveWidth = canvasHeight;
        effectiveHeight = canvasWidth;
    }

    // 白色填充背景
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, effectiveWidth, effectiveHeight);

    // 计算缩放后的图像尺寸
    const imgWidth = originalImage.width * scale;
    const imgHeight = originalImage.height * scale;

    // 计算绘制位置（考虑偏移量）
    let drawX = (effectiveWidth - imgWidth) / 2 + offsetX;
    let drawY = (effectiveHeight - imgHeight) / 2 + offsetY;

    // 绘制图像
    ctx.drawImage(
        originalImage,
        0, 0,
        originalImage.width, originalImage.height,
        drawX, drawY,
        imgWidth, imgHeight
    );

    // 恢复状态
    ctx.restore();

    // 获取绘制后的图像数据
    currentImageData = ctx.getImageData(0, 0, canvasWidth, canvasHeight);

    // 应用对比度调整
    const contrastFactor = parseFloat(document.getElementById('contrast').value);
    const imageData = new ImageData(
        new Uint8ClampedArray(currentImageData.data),
        currentImageData.width,
        currentImageData.height
    );
    adjustContrast(imageData, contrastFactor);

    // 根据状态应用抖动或显示原始图像
    if (isDitheringEnabled) {
        const processedImageData = ditherImage(imageData);
        const processedData = processImageData(processedImageData);
        const finalImageData = decodeProcessedData(processedData, canvasWidth, canvasHeight);
        ctx.putImageData(finalImageData, 0, 0);

        const ditherType = document.getElementById('ditherType').value;
        if (ditherType === 'adaptive' && window._adaptiveConfig) {
            const cfg = window._adaptiveConfig;
            const algoNames = { floydSteinberg: 'Floyd-Steinberg', atkinson: 'Atkinson', stucki: 'Stucki', jarvis: 'Jarvis-Judice-Ninke' };
            document.getElementById('imageResult').innerHTML =
                '<div class="info">自适应选择：' + algoNames[cfg.type] + '，强度 ' + cfg.strength.toFixed(1) + '</div>';
        }
    } else {
        ctx.putImageData(imageData, 0, 0);
    }
    updateCanvasScale();
}

// 高质量渲染：超采样 + 高质量插值，仅用于传输给设备的画面，不影响预览画布
function renderDeviceImageData(supersample) {
    var ss = supersample || 2;
    var targetW = getCanvasWidth();
    var targetH = getCanvasHeight();

    // 1. 在超采样大画布上渲染原图（复用与 updateImage 相同的几何变换）
    var bigW = targetW * ss;
    var bigH = targetH * ss;
    var off = document.createElement('canvas');
    off.width = bigW;
    off.height = bigH;
    var ctx = off.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';

    ctx.save();
    var effW = bigW;
    var effH = bigH;
    if (canvasRotation === 1) {
        ctx.translate(0, bigH);
        ctx.rotate(-Math.PI / 2);
        effW = bigH;
        effH = bigW;
    }

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, effW, effH);

    var imgW = originalImage.width * scale * ss;
    var imgH = originalImage.height * scale * ss;
    var drawX = (effW - imgW) / 2 + offsetX * ss;
    var drawY = (effH - imgH) / 2 + offsetY * ss;
    ctx.drawImage(
        originalImage,
        0, 0,
        originalImage.width, originalImage.height,
        drawX, drawY,
        imgW, imgH
    );
    ctx.restore();

    // 2. 高质量缩小到设备目标分辨率
    var out = document.createElement('canvas');
    out.width = targetW;
    out.height = targetH;
    var octx = out.getContext('2d');
    octx.imageSmoothingEnabled = true;
    octx.imageSmoothingQuality = 'high';
    octx.drawImage(off, 0, 0, bigW, bigH, 0, 0, targetW, targetH);

    return octx.getImageData(0, 0, targetW, targetH);
}

// 生成设备传输用的图像数据：超采样渲染 + 对比度 + 抖动（与预览处理链路一致，但源为高质量渲染）
function buildDeviceImageData() {
    var imageData = renderDeviceImageData(2);
    var contrastFactor = parseFloat(document.getElementById('contrast').value);
    adjustContrast(imageData, contrastFactor);
    if (isDitheringEnabled) {
        imageData = ditherImage(imageData);
    }
    return imageData;
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

function rgbToLab(r, g, b) {
    r = r / 255;
    g = g / 255;
    b = b / 255;

    r = r > 0.04045 ? Math.pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = g > 0.04045 ? Math.pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = b > 0.04045 ? Math.pow((b + 0.055) / 1.055, 2.4) : b / 12.92;

    r *= 100;
    g *= 100;
    b *= 100;

    let x = r * 0.4124 + g * 0.3576 + b * 0.1805;
    let y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    let z = r * 0.0193 + g * 0.1192 + b * 0.9505;

    x /= 95.047;
    y /= 100.0;
    z /= 108.883;

    x = x > 0.008856 ? Math.pow(x, 1/3) : (7.787 * x) + (16 / 116);
    y = y > 0.008856 ? Math.pow(y, 1/3) : (7.787 * y) + (16 / 116);
    z = z > 0.008856 ? Math.pow(z, 1/3) : (7.787 * z) + (16 / 116);

    const l = (116 * y) - 16;
    const a = 500 * (x - y);
    const bLab = 200 * (y - z);

    return { l, a, b: bLab };
}

function labDistance(lab1, lab2) {
    const dl = lab1.l - lab2.l;
    const da = lab1.a - lab2.a;
    const db = lab1.b - lab2.b;
    return Math.sqrt(dl * dl + da * da + db * db);
}

// sRGB <-> 线性空间转换表（gamma 感知抖动用，参考 Caster degamma.v）
var SRGB_TO_LINEAR_LUT = new Float32Array(256);
var LINEAR_TO_SRGB_LUT = new Uint8Array(256);
(function() {
    for (var i = 0; i < 256; i++) {
        var v = i / 255;
        SRGB_TO_LINEAR_LUT[i] = v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
    }
    for (var i = 0; i < 256; i++) {
        var v = i / 255;
        var srgb = v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
        LINEAR_TO_SRGB_LUT[i] = Math.round(srgb * 255);
    }
})();

function srgbToLinear(c) {
    return SRGB_TO_LINEAR_LUT[c];
}

function linearToSrgb(c) {
    var clamped = Math.max(0, Math.min(1, c));
    return LINEAR_TO_SRGB_LUT[Math.round(clamped * 255)];
}

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

const paletteHsl = rgbPalette.map(function(c) {
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

// ===== E6 Natural 算法：Lab 感知空间 + 蛇形扫描 + 内容自适应三区扩散 =====

// 快速 sRGB -> Lab（复用预计算的 sRGB 线性 LUT + cbrt，避免逐像素 pow）
function srgbToLabLut(r, g, b) {
    var lr = SRGB_TO_LINEAR_LUT[r];
    var lg = SRGB_TO_LINEAR_LUT[g];
    var lb = SRGB_TO_LINEAR_LUT[b];
    var x = lr * 0.4124 + lg * 0.3576 + lb * 0.1805;
    var y = lr * 0.2126 + lg * 0.7152 + lb * 0.0722;
    var z = lr * 0.0193 + lg * 0.1192 + lb * 0.9505;
    x /= 0.95047; y /= 1.0; z /= 1.08883;
    var fx = x > 0.008856 ? Math.cbrt(x) : (7.787 * x + 16 / 116);
    var fy = y > 0.008856 ? Math.cbrt(y) : (7.787 * y + 16 / 116);
    var fz = z > 0.008856 ? Math.cbrt(z) : (7.787 * z + 16 / 116);
    return { l: 116 * fy - 16, a: 500 * (fx - fy), b: 200 * (fy - fz) };
}

// 六色调色板的 Lab 坐标（用于感知空间量化与误差扩散）
var paletteLab = rgbPalette.map(function(c) {
    return { color: c, lab: srgbToLabLut(c.r, c.g, c.b) };
});

// 在 Lab 空间找最近色（感知均匀，色彩过渡更自然）
function findClosestLab(L, A, B) {
    var minDist = Infinity;
    var best = paletteLab[0];
    for (var i = 0; i < paletteLab.length; i++) {
        var p = paletteLab[i].lab;
        var dl = L - p.l;
        var da = A - p.a;
        var db = B - p.b;
        var d = dl * dl + da * da + db * db;
        if (d < minDist) {
            minDist = d;
            best = paletteLab[i];
        }
    }
    return best;
}

function floydSteinbergDither(imageData, strength) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;
    const tempData = new Uint8ClampedArray(data);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const r = tempData[idx];
            const g = tempData[idx + 1];
            const b = tempData[idx + 2];

            const closest = findClosestColor(r, g, b);

            const errR = (r - closest.r) * strength;
            const errG = (g - closest.g) * strength;
            const errB = (b - closest.b) * strength;

            if (x + 1 < width) {
                const idxRight = idx + 4;
                tempData[idxRight] = Math.min(255, Math.max(0, tempData[idxRight] + errR * 7 / 16));
                tempData[idxRight + 1] = Math.min(255, Math.max(0, tempData[idxRight + 1] + errG * 7 / 16));
                tempData[idxRight + 2] = Math.min(255, Math.max(0, tempData[idxRight + 2] + errB * 7 / 16));
            }
            if (y + 1 < height) {
                if (x > 0) {
                    const idxDownLeft = idx + width * 4 - 4;
                    tempData[idxDownLeft] = Math.min(255, Math.max(0, tempData[idxDownLeft] + errR * 3 / 16));
                    tempData[idxDownLeft + 1] = Math.min(255, Math.max(0, tempData[idxDownLeft + 1] + errG * 3 / 16));
                    tempData[idxDownLeft + 2] = Math.min(255, Math.max(0, tempData[idxDownLeft + 2] + errB * 3 / 16));
                }
                const idxDown = idx + width * 4;
                tempData[idxDown] = Math.min(255, Math.max(0, tempData[idxDown] + errR * 5 / 16));
                tempData[idxDown + 1] = Math.min(255, Math.max(0, tempData[idxDown + 1] + errG * 5 / 16));
                tempData[idxDown + 2] = Math.min(255, Math.max(0, tempData[idxDown + 2] + errB * 5 / 16));
                if (x + 1 < width) {
                    const idxDownRight = idx + width * 4 + 4;
                    tempData[idxDownRight] = Math.min(255, Math.max(0, tempData[idxDownRight] + errR * 1 / 16));
                    tempData[idxDownRight + 1] = Math.min(255, Math.max(0, tempData[idxDownRight + 1] + errG * 1 / 16));
                    tempData[idxDownRight + 2] = Math.min(255, Math.max(0, tempData[idxDownRight + 2] + errB * 1 / 16));
                }
            }
        }
    }

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const r = tempData[idx];
            const g = tempData[idx + 1];
            const b = tempData[idx + 2];

            const closest = findClosestColor(r, g, b);
            data[idx] = closest.r;
            data[idx + 1] = closest.g;
            data[idx + 2] = closest.b;
        }
    }

    return imageData;
}

function atkinsonDither(imageData, strength) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;
    const tempData = new Uint8ClampedArray(data);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const r = tempData[idx];
            const g = tempData[idx + 1];
            const b = tempData[idx + 2];

            const closest = findClosestColor(r, g, b);

            data[idx] = closest.r;
            data[idx + 1] = closest.g;
            data[idx + 2] = closest.b;

            const errR = (r - closest.r) * strength;
            const errG = (g - closest.g) * strength;
            const errB = (b - closest.b) * strength;

            const fraction = 1 / 8;

            if (x + 1 < width) {
                const idxRight = idx + 4;
                tempData[idxRight] = Math.min(255, Math.max(0, tempData[idxRight] + errR * fraction));
                tempData[idxRight + 1] = Math.min(255, Math.max(0, tempData[idxRight + 1] + errG * fraction));
                tempData[idxRight + 2] = Math.min(255, Math.max(0, tempData[idxRight + 2] + errB * fraction));
            }
            if (x + 2 < width) {
                const idxRight2 = idx + 8;
                tempData[idxRight2] = Math.min(255, Math.max(0, tempData[idxRight2] + errR * fraction));
                tempData[idxRight2 + 1] = Math.min(255, Math.max(0, tempData[idxRight2 + 1] + errG * fraction));
                tempData[idxRight2 + 2] = Math.min(255, Math.max(0, tempData[idxRight2 + 2] + errB * fraction));
            }
            if (y + 1 < height) {
                if (x > 0) {
                    const idxDownLeft = idx + width * 4 - 4;
                    tempData[idxDownLeft] = Math.min(255, Math.max(0, tempData[idxDownLeft] + errR * fraction));
                    tempData[idxDownLeft + 1] = Math.min(255, Math.max(0, tempData[idxDownLeft + 1] + errG * fraction));
                    tempData[idxDownLeft + 2] = Math.min(255, Math.max(0, tempData[idxDownLeft + 2] + errB * fraction));
                }
                const idxDown = idx + width * 4;
                tempData[idxDown] = Math.min(255, Math.max(0, tempData[idxDown] + errR * fraction));
                tempData[idxDown + 1] = Math.min(255, Math.max(0, tempData[idxDown + 1] + errG * fraction));
                tempData[idxDown + 2] = Math.min(255, Math.max(0, tempData[idxDown + 2] + errB * fraction));
                if (x + 1 < width) {
                    const idxDownRight = idx + width * 4 + 4;
                    tempData[idxDownRight] = Math.min(255, Math.max(0, tempData[idxDownRight] + errR * fraction));
                    tempData[idxDownRight + 1] = Math.min(255, Math.max(0, tempData[idxDownRight + 1] + errG * fraction));
                    tempData[idxDownRight + 2] = Math.min(255, Math.max(0, tempData[idxDownRight + 2] + errB * fraction));
                }
            }
            if (y + 2 < height) {
                const idxDown2 = idx + width * 8;
                tempData[idxDown2] = Math.min(255, Math.max(0, tempData[idxDown2] + errR * fraction));
                tempData[idxDown2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2 + 1] + errG * fraction));
                tempData[idxDown2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2 + 2] + errB * fraction));
            }
        }
    }

    return imageData;
}

function stuckiDither(imageData, strength) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;
    const tempData = new Uint8ClampedArray(data);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const r = tempData[idx];
            const g = tempData[idx + 1];
            const b = tempData[idx + 2];

            const closest = findClosestColor(r, g, b);

            const errR = (r - closest.r) * strength;
            const errG = (g - closest.g) * strength;
            const errB = (b - closest.b) * strength;

            const divisor = 42;

            if (x + 1 < width) {
                const idxRight = idx + 4;
                tempData[idxRight] = Math.min(255, Math.max(0, tempData[idxRight] + errR * 8 / divisor));
                tempData[idxRight + 1] = Math.min(255, Math.max(0, tempData[idxRight + 1] + errG * 8 / divisor));
                tempData[idxRight + 2] = Math.min(255, Math.max(0, tempData[idxRight + 2] + errB * 8 / divisor));
            }
            if (x + 2 < width) {
                const idxRight2 = idx + 8;
                tempData[idxRight2] = Math.min(255, Math.max(0, tempData[idxRight2] + errR * 4 / divisor));
                tempData[idxRight2 + 1] = Math.min(255, Math.max(0, tempData[idxRight2 + 1] + errG * 4 / divisor));
                tempData[idxRight2 + 2] = Math.min(255, Math.max(0, tempData[idxRight2 + 2] + errB * 4 / divisor));
            }
            if (y + 1 < height) {
                if (x > 1) {
                    const idxDownLeft2 = idx + width * 4 - 8;
                    tempData[idxDownLeft2] = Math.min(255, Math.max(0, tempData[idxDownLeft2] + errR * 2 / divisor));
                    tempData[idxDownLeft2 + 1] = Math.min(255, Math.max(0, tempData[idxDownLeft2 + 1] + errG * 2 / divisor));
                    tempData[idxDownLeft2 + 2] = Math.min(255, Math.max(0, tempData[idxDownLeft2 + 2] + errB * 2 / divisor));
                }
                if (x > 0) {
                    const idxDownLeft = idx + width * 4 - 4;
                    tempData[idxDownLeft] = Math.min(255, Math.max(0, tempData[idxDownLeft] + errR * 4 / divisor));
                    tempData[idxDownLeft + 1] = Math.min(255, Math.max(0, tempData[idxDownLeft + 1] + errG * 4 / divisor));
                    tempData[idxDownLeft + 2] = Math.min(255, Math.max(0, tempData[idxDownLeft + 2] + errB * 4 / divisor));
                }
                const idxDown = idx + width * 4;
                tempData[idxDown] = Math.min(255, Math.max(0, tempData[idxDown] + errR * 8 / divisor));
                tempData[idxDown + 1] = Math.min(255, Math.max(0, tempData[idxDown + 1] + errG * 8 / divisor));
                tempData[idxDown + 2] = Math.min(255, Math.max(0, tempData[idxDown + 2] + errB * 8 / divisor));
                if (x + 1 < width) {
                    const idxDownRight1 = idx + width * 4 + 4;
                    tempData[idxDownRight1] = Math.min(255, Math.max(0, tempData[idxDownRight1] + errR * 4 / divisor));
                    tempData[idxDownRight1 + 1] = Math.min(255, Math.max(0, tempData[idxDownRight1 + 1] + errG * 4 / divisor));
                    tempData[idxDownRight1 + 2] = Math.min(255, Math.max(0, tempData[idxDownRight1 + 2] + errB * 4 / divisor));
                }
                if (x + 2 < width) {
                    const idxDownRight2 = idx + width * 4 + 8;
                    tempData[idxDownRight2] = Math.min(255, Math.max(0, tempData[idxDownRight2] + errR * 2 / divisor));
                    tempData[idxDownRight2 + 1] = Math.min(255, Math.max(0, tempData[idxDownRight2 + 1] + errG * 2 / divisor));
                    tempData[idxDownRight2 + 2] = Math.min(255, Math.max(0, tempData[idxDownRight2 + 2] + errB * 2 / divisor));
                }
            }
            if (y + 2 < height) {
                if (x > 1) {
                    const idxDown2Left2 = idx + width * 8 - 8;
                    tempData[idxDown2Left2] = Math.min(255, Math.max(0, tempData[idxDown2Left2] + errR * 1 / divisor));
                    tempData[idxDown2Left2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2Left2 + 1] + errG * 1 / divisor));
                    tempData[idxDown2Left2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2Left2 + 2] + errB * 1 / divisor));
                }
                if (x > 0) {
                    const idxDown2Left = idx + width * 8 - 4;
                    tempData[idxDown2Left] = Math.min(255, Math.max(0, tempData[idxDown2Left] + errR * 2 / divisor));
                    tempData[idxDown2Left + 1] = Math.min(255, Math.max(0, tempData[idxDown2Left + 1] + errG * 2 / divisor));
                    tempData[idxDown2Left + 2] = Math.min(255, Math.max(0, tempData[idxDown2Left + 2] + errB * 2 / divisor));
                }
                const idxDown2 = idx + width * 8;
                tempData[idxDown2] = Math.min(255, Math.max(0, tempData[idxDown2] + errR * 4 / divisor));
                tempData[idxDown2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2 + 1] + errG * 4 / divisor));
                tempData[idxDown2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2 + 2] + errB * 4 / divisor));
                if (x + 1 < width) {
                    const idxDown2Right = idx + width * 8 + 4;
                    tempData[idxDown2Right] = Math.min(255, Math.max(0, tempData[idxDown2Right] + errR * 2 / divisor));
                    tempData[idxDown2Right + 1] = Math.min(255, Math.max(0, tempData[idxDown2Right + 1] + errG * 2 / divisor));
                    tempData[idxDown2Right + 2] = Math.min(255, Math.max(0, tempData[idxDown2Right + 2] + errB * 2 / divisor));
                }
                if (x + 2 < width) {
                    const idxDown2Right2 = idx + width * 8 + 8;
                    tempData[idxDown2Right2] = Math.min(255, Math.max(0, tempData[idxDown2Right2] + errR * 1 / divisor));
                    tempData[idxDown2Right2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2Right2 + 1] + errG * 1 / divisor));
                    tempData[idxDown2Right2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2Right2 + 2] + errB * 1 / divisor));
                }
            }
        }
    }

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const r = tempData[idx];
            const g = tempData[idx + 1];
            const b = tempData[idx + 2];

            const closest = findClosestColor(r, g, b);
            data[idx] = closest.r;
            data[idx + 1] = closest.g;
            data[idx + 2] = closest.b;
        }
    }

    return imageData;
}

function jarvisDither(imageData, strength) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;
    const tempData = new Uint8ClampedArray(data);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const r = tempData[idx];
            const g = tempData[idx + 1];
            const b = tempData[idx + 2];

            const closest = findClosestColor(r, g, b);

            data[idx] = closest.r;
            data[idx + 1] = closest.g;
            data[idx + 2] = closest.b;

            const errR = (r - closest.r) * strength;
            const errG = (g - closest.g) * strength;
            const errB = (b - closest.b) * strength;

            const divisor = 48;

            if (x + 1 < width) {
                const idxRight = idx + 4;
                tempData[idxRight] = Math.min(255, Math.max(0, tempData[idxRight] + errR * 7 / divisor));
                tempData[idxRight + 1] = Math.min(255, Math.max(0, tempData[idxRight + 1] + errG * 7 / divisor));
                tempData[idxRight + 2] = Math.min(255, Math.max(0, tempData[idxRight + 2] + errB * 7 / divisor));
            }
            if (x + 2 < width) {
                const idxRight2 = idx + 8;
                tempData[idxRight2] = Math.min(255, Math.max(0, tempData[idxRight2] + errR * 5 / divisor));
                tempData[idxRight2 + 1] = Math.min(255, Math.max(0, tempData[idxRight2 + 1] + errG * 5 / divisor));
                tempData[idxRight2 + 2] = Math.min(255, Math.max(0, tempData[idxRight2 + 2] + errB * 5 / divisor));
            }
            if (y + 1 < height) {
                if (x > 1) {
                    const idxDownLeft2 = idx + width * 4 - 8;
                    tempData[idxDownLeft2] = Math.min(255, Math.max(0, tempData[idxDownLeft2] + errR * 3 / divisor));
                    tempData[idxDownLeft2 + 1] = Math.min(255, Math.max(0, tempData[idxDownLeft2 + 1] + errG * 3 / divisor));
                    tempData[idxDownLeft2 + 2] = Math.min(255, Math.max(0, tempData[idxDownLeft2 + 2] + errB * 3 / divisor));
                }
                if (x > 0) {
                    const idxDownLeft = idx + width * 4 - 4;
                    tempData[idxDownLeft] = Math.min(255, Math.max(0, tempData[idxDownLeft] + errR * 5 / divisor));
                    tempData[idxDownLeft + 1] = Math.min(255, Math.max(0, tempData[idxDownLeft + 1] + errG * 5 / divisor));
                    tempData[idxDownLeft + 2] = Math.min(255, Math.max(0, tempData[idxDownLeft + 2] + errB * 5 / divisor));
                }
                const idxDown = idx + width * 4;
                tempData[idxDown] = Math.min(255, Math.max(0, tempData[idxDown] + errR * 7 / divisor));
                tempData[idxDown + 1] = Math.min(255, Math.max(0, tempData[idxDown + 1] + errG * 7 / divisor));
                tempData[idxDown + 2] = Math.min(255, Math.max(0, tempData[idxDown + 2] + errB * 7 / divisor));
                if (x + 1 < width) {
                    const idxDownRight = idx + width * 4 + 4;
                    tempData[idxDownRight] = Math.min(255, Math.max(0, tempData[idxDownRight] + errR * 5 / divisor));
                    tempData[idxDownRight + 1] = Math.min(255, Math.max(0, tempData[idxDownRight + 1] + errG * 5 / divisor));
                    tempData[idxDownRight + 2] = Math.min(255, Math.max(0, tempData[idxDownRight + 2] + errB * 5 / divisor));
                }
                if (x + 2 < width) {
                    const idxDownRight2 = idx + width * 4 + 8;
                    tempData[idxDownRight2] = Math.min(255, Math.max(0, tempData[idxDownRight2] + errR * 3 / divisor));
                    tempData[idxDownRight2 + 1] = Math.min(255, Math.max(0, tempData[idxDownRight2 + 1] + errG * 3 / divisor));
                    tempData[idxDownRight2 + 2] = Math.min(255, Math.max(0, tempData[idxDownRight2 + 2] + errB * 3 / divisor));
                }
            }
            if (y + 2 < height) {
                if (x > 1) {
                    const idxDown2Left2 = idx + width * 8 - 8;
                    tempData[idxDown2Left2] = Math.min(255, Math.max(0, tempData[idxDown2Left2] + errR * 1 / divisor));
                    tempData[idxDown2Left2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2Left2 + 1] + errG * 1 / divisor));
                    tempData[idxDown2Left2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2Left2 + 2] + errB * 1 / divisor));
                }
                if (x > 0) {
                    const idxDown2Left = idx + width * 8 - 4;
                    tempData[idxDown2Left] = Math.min(255, Math.max(0, tempData[idxDown2Left] + errR * 3 / divisor));
                    tempData[idxDown2Left + 1] = Math.min(255, Math.max(0, tempData[idxDown2Left + 1] + errG * 3 / divisor));
                    tempData[idxDown2Left + 2] = Math.min(255, Math.max(0, tempData[idxDown2Left + 2] + errB * 3 / divisor));
                }
                const idxDown2 = idx + width * 8;
                tempData[idxDown2] = Math.min(255, Math.max(0, tempData[idxDown2] + errR * 5 / divisor));
                tempData[idxDown2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2 + 1] + errG * 5 / divisor));
                tempData[idxDown2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2 + 2] + errB * 5 / divisor));
                if (x + 1 < width) {
                    const idxDown2Right = idx + width * 8 + 4;
                    tempData[idxDown2Right] = Math.min(255, Math.max(0, tempData[idxDown2Right] + errR * 3 / divisor));
                    tempData[idxDown2Right + 1] = Math.min(255, Math.max(0, tempData[idxDown2Right + 1] + errG * 3 / divisor));
                    tempData[idxDown2Right + 2] = Math.min(255, Math.max(0, tempData[idxDown2Right + 2] + errB * 3 / divisor));
                }
                if (x + 2 < width) {
                    const idxDown2Right2 = idx + width * 8 + 8;
                    tempData[idxDown2Right2] = Math.min(255, Math.max(0, tempData[idxDown2Right2] + errR * 1 / divisor));
                    tempData[idxDown2Right2 + 1] = Math.min(255, Math.max(0, tempData[idxDown2Right2 + 1] + errG * 1 / divisor));
                    tempData[idxDown2Right2 + 2] = Math.min(255, Math.max(0, tempData[idxDown2Right2 + 2] + errB * 1 / divisor));
                }
            }
        }
    }

    return imageData;
}

// Gamma 感知 Floyd-Steinberg 误差扩散（在线性空间扩散误差，参考 Caster error_diffusion_kernel.v）
// 量化仍用感知化的 findClosestColor，但误差按线性空间计算与累积
function gammaFloydSteinbergDither(imageData, strength) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;
    // 工作缓冲：线性空间，每像素 3 通道浮点
    const linData = new Float32Array(width * height * 3);
    for (let i = 0; i < data.length; i += 4) {
        const p = (i / 4) * 3;
        linData[p] = srgbToLinear(data[i]);
        linData[p + 1] = srgbToLinear(data[i + 1]);
        linData[p + 2] = srgbToLinear(data[i + 2]);
    }

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const p = (y * width + x) * 3;
            const r = linData[p];
            const g = linData[p + 1];
            const b = linData[p + 2];

            // 转回 sRGB 交给感知量化器选最近色
            const closest = findClosestColor(linearToSrgb(r), linearToSrgb(g), linearToSrgb(b));

            const idx = (y * width + x) * 4;
            data[idx] = closest.r;
            data[idx + 1] = closest.g;
            data[idx + 2] = closest.b;

            // 在线性空间计算误差（关键：误差值按物理亮度计）
            const errR = (r - srgbToLinear(closest.r)) * strength;
            const errG = (g - srgbToLinear(closest.g)) * strength;
            const errB = (b - srgbToLinear(closest.b)) * strength;

            if (x + 1 < width) {
                const np = p + 3;
                linData[np] += errR * 7 / 16;
                linData[np + 1] += errG * 7 / 16;
                linData[np + 2] += errB * 7 / 16;
            }
            if (y + 1 < height) {
                if (x > 0) {
                    const np = p + width * 3 - 3;
                    linData[np] += errR * 3 / 16;
                    linData[np + 1] += errG * 3 / 16;
                    linData[np + 2] += errB * 3 / 16;
                }
                const np = p + width * 3;
                linData[np] += errR * 5 / 16;
                linData[np + 1] += errG * 5 / 16;
                linData[np + 2] += errB * 5 / 16;
                if (x + 1 < width) {
                    const np = p + width * 3 + 3;
                    linData[np] += errR * 1 / 16;
                    linData[np + 1] += errG * 1 / 16;
                    linData[np + 2] += errB * 1 / 16;
                }
            }
        }
    }

    return imageData;
}

// 4×4 Bayer 有序抖动（参考 Caster bayer_dithering.v MONO：标准矩阵中心化到 -8..7）
var BAYER_MATRIX = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5]
];

function bayerDither(imageData, strength) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;

    for (let y = 0; y < height; y++) {
        const row = BAYER_MATRIX[y & 3];
        for (let x = 0; x < width; x++) {
            const bias = (row[x & 3] - 8) * strength;
            const idx = (y * width + x) * 4;
            const r = Math.min(255, Math.max(0, data[idx] + bias));
            const g = Math.min(255, Math.max(0, data[idx + 1] + bias));
            const b = Math.min(255, Math.max(0, data[idx + 2] + bias));
            const closest = findClosestColor(r, g, b);
            data[idx] = closest.r;
            data[idx + 1] = closest.g;
            data[idx + 2] = closest.b;
        }
    }

    return imageData;
}

// E6 Natural：Lab 感知空间误差扩散 + 蛇形扫描 + 三区内容自适应。
// 平坦区直接量化（零颗粒）、渐变区误差扩散（过渡自然）、边缘区保真（锐利）。
function e6NaturalDither(imageData, strength) {
    var width = imageData.width;
    var height = imageData.height;
    var data = imageData.data;
    var N = width * height;
    var s = Math.max(0, Math.min(1, typeof strength === 'number' ? strength : 1));

    // 1. sRGB -> Lab 工作缓冲 + 亮度
    var lab = new Float32Array(N * 3);
    var lum = new Float32Array(N);
    for (var i = 0; i < N; i++) {
        var o = i * 4;
        var r = data[o], g = data[o + 1], b = data[o + 2];
        var l = srgbToLabLut(r, g, b);
        lab[i * 3] = l.l;
        lab[i * 3 + 1] = l.a;
        lab[i * 3 + 2] = l.b;
        lum[i] = 0.299 * r + 0.587 * g + 0.114 * b;
    }

    // 2. Sobel 梯度幅度（内容自适应依据）
    var grad = new Float32Array(N);
    var maxGrad = 0;
    for (var y = 1; y < height - 1; y++) {
        var row = y * width;
        for (var x = 1; x < width - 1; x++) {
            var p = row + x;
            var tl = lum[p - 1 - width], tc = lum[p - width], tr = lum[p + 1 - width];
            var ml = lum[p - 1], mr = lum[p + 1];
            var bl = lum[p - 1 + width], bc = lum[p + width], br = lum[p + 1 + width];
            var gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
            var gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
            var m = Math.sqrt(gx * gx + gy * gy);
            grad[p] = m;
            if (m > maxGrad) maxGrad = m;
        }
    }

    // 3. 自适应阈值（绝对 Sobel 幅度，稳健区分平坦/渐变/边缘）
    // 平坦区梯度接近 0，需保证其落入平坦档；边缘区梯度大，独立成档
    var flatThr = Math.max(6, maxGrad * 0.01);
    var edgeThr = Math.max(60, maxGrad * 0.15);
    if (edgeThr <= flatThr) edgeThr = flatThr + 6;

    // 4. 蛇形扫描 + 三区自适应误差扩散（Floyd-Steinberg 权重）
    for (var y2 = 0; y2 < height; y2++) {
        var ltr = (y2 % 2 === 0);
        for (var step = 0; step < width; step++) {
            var x2 = ltr ? step : (width - 1 - step);
            var p = y2 * width + x2;
            var act = grad[p];

            var L = lab[p * 3], A = lab[p * 3 + 1], B = lab[p * 3 + 2];
            var closest = findClosestLab(L, A, B);

            var o = p * 4;
            data[o] = closest.color.r;
            data[o + 1] = closest.color.g;
            data[o + 2] = closest.color.b;

            // 仅渐变区扩散误差：平坦/边缘区直接量化（颗粒少、边缘锐利）
            if (act < flatThr || act >= edgeThr) continue;

            var eL = (L - closest.lab.l);
            var eA = (A - closest.lab.a);
            var eB = (B - closest.lab.b);
            var w7 = 7 / 16 * s, w5 = 5 / 16 * s, w3 = 3 / 16 * s, w1 = 1 / 16 * s;

            if (ltr) {
                if (x2 + 1 < width) {
                    var q = (p + 1) * 3;
                    lab[q] += eL * w7; lab[q + 1] += eA * w7; lab[q + 2] += eB * w7;
                }
                if (y2 + 1 < height) {
                    if (x2 > 0) {
                        var q2 = (p + width - 1) * 3;
                        lab[q2] += eL * w3; lab[q2 + 1] += eA * w3; lab[q2 + 2] += eB * w3;
                    }
                    var q3 = (p + width) * 3;
                    lab[q3] += eL * w5; lab[q3 + 1] += eA * w5; lab[q3 + 2] += eB * w5;
                    if (x2 + 1 < width) {
                        var q4 = (p + width + 1) * 3;
                        lab[q4] += eL * w1; lab[q4 + 1] += eA * w1; lab[q4 + 2] += eB * w1;
                    }
                }
            } else {
                if (x2 - 1 >= 0) {
                    var q = (p - 1) * 3;
                    lab[q] += eL * w7; lab[q + 1] += eA * w7; lab[q + 2] += eB * w7;
                }
                if (y2 + 1 < height) {
                    if (x2 + 1 < width) {
                        var q2 = (p + width + 1) * 3;
                        lab[q2] += eL * w3; lab[q2 + 1] += eA * w3; lab[q2 + 2] += eB * w3;
                    }
                    var q3 = (p + width) * 3;
                    lab[q3] += eL * w5; lab[q3 + 1] += eA * w5; lab[q3 + 2] += eB * w5;
                    if (x2 - 1 >= 0) {
                        var q4 = (p + width - 1) * 3;
                        lab[q4] += eL * w1; lab[q4 + 1] += eA * w1; lab[q4 + 2] += eB * w1;
                    }
                }
            }
        }
    }

    return imageData;
}

function computeEdgeMap(data, width, height) {
    const edges = new Float32Array(width * height);
    for (let y = 1; y < height - 1; y++) {
        for (let x = 1; x < width - 1; x++) {
            const idx = (y * width + x) * 4;
            const tl = data[((y-1)*width+x-1)*4]*0.299 + data[((y-1)*width+x-1)*4+1]*0.587 + data[((y-1)*width+x-1)*4+2]*0.114;
            const tc = data[((y-1)*width+x)*4]*0.299 + data[((y-1)*width+x)*4+1]*0.587 + data[((y-1)*width+x)*4+2]*0.114;
            const tr = data[((y-1)*width+x+1)*4]*0.299 + data[((y-1)*width+x+1)*4+1]*0.587 + data[((y-1)*width+x+1)*4+2]*0.114;
            const ml = data[(y*width+x-1)*4]*0.299 + data[(y*width+x-1)*4+1]*0.587 + data[(y*width+x-1)*4+2]*0.114;
            const mr = data[(y*width+x+1)*4]*0.299 + data[(y*width+x+1)*4+1]*0.587 + data[(y*width+x+1)*4+2]*0.114;
            const bl = data[((y+1)*width+x-1)*4]*0.299 + data[((y+1)*width+x-1)*4+1]*0.587 + data[((y+1)*width+x-1)*4+2]*0.114;
            const bc = data[((y+1)*width+x)*4]*0.299 + data[((y+1)*width+x)*4+1]*0.587 + data[((y+1)*width+x)*4+2]*0.114;
            const br = data[((y+1)*width+x+1)*4]*0.299 + data[((y+1)*width+x+1)*4+1]*0.587 + data[((y+1)*width+x+1)*4+2]*0.114;
            const gx = -tl - 2*ml - bl + tr + 2*mr + br;
            const gy = -tl - 2*tc - tr + bl + 2*bc + br;
            edges[y * width + x] = Math.sqrt(gx * gx + gy * gy);
        }
    }
    return edges;
}

function analyzeImageAdvanced(imageData) {
    const data = imageData.data;
    const width = imageData.width;
    const height = imageData.height;
    const pixelCount = width * height;
    let brightnessSum = 0;
    let rSum = 0, gSum = 0, bSum = 0;
    let saturationSum = 0;

    for (let i = 0; i < data.length; i += 4) {
        const r = data[i], g = data[i + 1], b = data[i + 2];
        rSum += r;
        gSum += g;
        bSum += b;
        brightnessSum += r * 0.299 + g * 0.587 + b * 0.114;
        const max = Math.max(r, g, b);
        const min = Math.min(r, g, b);
        saturationSum += max > 0 ? (max - min) / max : 0;
    }

    const edges = computeEdgeMap(data, width, height);
    let edgeSum = 0;
    let edgeCount = 0;
    for (let i = 0; i < edges.length; i++) {
        edgeSum += edges[i];
        if (edges[i] > 20) edgeCount++;
    }

    const innerPixels = (width - 2) * (height - 2);
    return {
        brightness: brightnessSum / pixelCount / 255,
        edgeDensity: innerPixels > 0 ? edgeCount / innerPixels : 0,
        avgGradient: innerPixels > 0 ? edgeSum / innerPixels / 255 : 0,
        saturation: saturationSum / pixelCount
    };
}

function downsampleImageData(imageData, tw, th) {
    const src = document.createElement('canvas');
    src.width = imageData.width;
    src.height = imageData.height;
    src.getContext('2d').putImageData(imageData, 0, 0);
    const dst = document.createElement('canvas');
    dst.width = tw;
    dst.height = th;
    const ctx = dst.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'medium';
    ctx.drawImage(src, 0, 0, tw, th);
    return ctx.getImageData(0, 0, tw, th);
}

function generateAdaptiveCandidates(analysis) {
    const candidates = [];
    const algos = ['floydSteinberg', 'atkinson', 'stucki', 'jarvis'];
    let strengths;

    if (analysis.edgeDensity > 0.2) {
        strengths = [0.6, 0.8, 1.0, 1.2, 1.4, 1.6];
    } else if (analysis.saturation > 0.3) {
        strengths = [0.7, 0.9, 1.0, 1.2, 1.4, 1.6, 1.8];
    } else {
        strengths = [0.6, 0.8, 1.0, 1.2, 1.5, 1.8, 2.0];
    }

    for (const algo of algos) {
        for (const s of strengths) {
            candidates.push({ type: algo, strength: s });
        }
    }
    return candidates;
}

function evaluateDitherResult(original, dithered) {
    const d1 = original.data;
    const d2 = dithered.data;
    const width = original.width;
    const height = original.height;
    const n = d1.length / 4;

    let totalLabError = 0;
    let maxError = 0;
    for (let i = 0; i < d1.length; i += 4) {
        const lab1 = rgbToLab(d1[i], d1[i + 1], d1[i + 2]);
        const lab2 = rgbToLab(d2[i], d2[i + 1], d2[i + 2]);
        const dist = labDistance(lab1, lab2);
        totalLabError += dist;
        if (dist > maxError) maxError = dist;
    }
    const avgLabError = totalLabError / n;

    const origEdges = computeEdgeMap(d1, width, height);
    const dithEdges = computeEdgeMap(d2, width, height);
    let edgeCorrelation = 0;
    let origEdgeEnergy = 0;
    let dithEdgeEnergy = 0;
    for (let i = 0; i < origEdges.length; i++) {
        edgeCorrelation += origEdges[i] * dithEdges[i];
        origEdgeEnergy += origEdges[i] * origEdges[i];
        dithEdgeEnergy += dithEdges[i] * dithEdges[i];
    }
    const edgePreservation = origEdgeEnergy > 0 && dithEdgeEnergy > 0 ?
        edgeCorrelation / Math.sqrt(origEdgeEnergy * dithEdgeEnergy) : 0;

    const colorMap = new Map();
    for (let i = 0; i < d2.length; i += 4) {
        const key = (d2[i] << 16) | (d2[i+1] << 8) | d2[i+2];
        colorMap.set(key, (colorMap.get(key) || 0) + 1);
    }
    const colorCounts = Array.from(colorMap.values()).sort((a, b) => b - a);
    let colorEntropy = 0;
    for (const count of colorCounts) {
        const p = count / n;
        if (p > 0) colorEntropy -= p * Math.log2(p);
    }
    const maxEntropy = Math.log2(Math.min(6, colorCounts.length));
    const colorBalance = maxEntropy > 0 ? colorEntropy / maxEntropy : 0;

    const score = avgLabError * 0.4 + (1 - edgePreservation) * 80 * 0.35 + (1 - colorBalance) * 30 * 0.25;

    return { score, avgLabError, edgePreservation, colorBalance, maxError };
}

function applyDitherByType(imageData, type, strength) {
    switch (type) {
        case 'floydSteinberg': return floydSteinbergDither(imageData, strength);
        case 'atkinson': return atkinsonDither(imageData, strength);
        case 'stucki': return stuckiDither(imageData, strength);
        case 'jarvis': return jarvisDither(imageData, strength);
        case 'gammaFloydSteinberg': return gammaFloydSteinbergDither(imageData, strength);
        case 'bayer': return bayerDither(imageData, strength);
        default: return imageData;
    }
}

function adaptiveDither(imageData) {
    const width = imageData.width;
    const height = imageData.height;
    const evalScale = 3;
    const evalW = Math.max(30, Math.floor(width / evalScale));
    const evalH = Math.max(30, Math.floor(height / evalScale));
    const evalData = downsampleImageData(imageData, evalW, evalH);

    const analysis = analyzeImageAdvanced(evalData);
    const candidates = generateAdaptiveCandidates(analysis);

    let bestScore = Infinity;
    let bestConfig = candidates[0];

    for (const config of candidates) {
        const copy = new ImageData(
            new Uint8ClampedArray(evalData.data),
            evalW,
            evalH
        );
        applyDitherByType(copy, config.type, config.strength);
        const result = evaluateDitherResult(evalData, copy);
        if (result.score < bestScore) {
            bestScore = result.score;
            bestConfig = config;
        }
    }

    window._adaptiveConfig = bestConfig;
    return applyDitherByType(imageData, bestConfig.type, bestConfig.strength);
}

function ditherImage(imageData) {
    const ditherType = document.getElementById('ditherType').value;
    const ditherStrength = parseFloat(document.getElementById('ditherStrength').value);

    switch (ditherType) {
        case 'adaptive':
            return adaptiveDither(imageData);
        case 'floydSteinberg':
            return floydSteinbergDither(imageData, ditherStrength);
        case 'atkinson':
            return atkinsonDither(imageData, ditherStrength);
        case 'stucki':
            return stuckiDither(imageData, ditherStrength);
        case 'jarvis':
            return jarvisDither(imageData, ditherStrength);
        case 'gammaFloydSteinberg':
            return gammaFloydSteinbergDither(imageData, ditherStrength);
        case 'bayer':
            return bayerDither(imageData, ditherStrength);
        case 'e6Natural':
            return e6NaturalDither(imageData, ditherStrength);
        default:
            return imageData;
    }
}

function decodeProcessedData(processedData, width, height) {
    const imageData = new ImageData(width, height);
    const data = imageData.data;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const newIndex = getPixelIndex(x, y, width, height);
            const byteIndex = Math.floor(newIndex / 2);
            const byte = processedData[byteIndex];

            const code = (newIndex % 2 === 0) ? (byte >> 4) & 0x0F : byte & 0x0F;
            const color = rgbPalette.find(c => c.code === code) || rgbPalette[1];

            const index = (y * width + x) * 4;
            data[index] = color.r;
            data[index + 1] = color.g;
            data[index + 2] = color.b;
            data[index + 3] = 255;
        }
    }

    return imageData;
}

function processImageData(imageData) {
    const width = imageData.width;
    const height = imageData.height;
    const data = imageData.data;

    var processedData = new Uint8Array(getFilmPixelDataSize());
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const index = (y * width + x) * 4;
            const r = data[index];
            const g = data[index + 1];
            const b = data[index + 2];

            const closest = findClosestColor(r, g, b);
            const code = closest.code;

            const newIndex = getPixelIndex(x, y, width, height);
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

function analyzeImage(imageData) {
    const data = imageData.data;
    let totalBrightness = 0;
    let totalContrast = 0;
    let rSum = 0, gSum = 0, bSum = 0;
    let rVariance = 0, gVariance = 0, bVariance = 0;
    let pixelCount = data.length / 4;

    // Calculate average brightness and color values
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i + 1];
        const b = data[i + 2];
        
        const brightness = (r + g + b) / 3;
        totalBrightness += brightness;
        rSum += r;
        gSum += g;
        bSum += b;
    }

    const avgBrightness = totalBrightness / pixelCount;
    const avgR = rSum / pixelCount;
    const avgG = gSum / pixelCount;
    const avgB = bSum / pixelCount;

    // Calculate contrast and color variance
    for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i + 1];
        const b = data[i + 2];
        
        const brightness = (r + g + b) / 3;
        totalContrast += Math.abs(brightness - avgBrightness);
        rVariance += Math.pow(r - avgR, 2);
        gVariance += Math.pow(g - avgG, 2);
        bVariance += Math.pow(b - avgB, 2);
    }

    const contrast = totalContrast / pixelCount;
    const colorVariance = (rVariance + gVariance + bVariance) / (3 * pixelCount);
    const colorSaturation = Math.sqrt(colorVariance) / 255;

    return {
        brightness: avgBrightness / 255, // Normalized 0-1
        contrast: contrast / 127, // Normalized 0-2, but typically 0-1
        colorSaturation: Math.min(1, colorSaturation) // Normalized 0-1
    };
}

function getOptimalDitherParameters(imageAnalysis) {
    const { brightness, contrast, colorSaturation } = imageAnalysis;

    let ditherType = 'floydSteinberg';

    if (colorSaturation > 0.6) {
        ditherType = 'stucki';
    } else if (contrast < 0.3) {
        ditherType = 'atkinson';
    } else if (brightness < 0.3 || brightness > 0.7) {
        ditherType = 'jarvis';
    }

    let ditherStrength = 1.0;

    if (contrast < 0.4) {
        ditherStrength = 1.5 + (0.4 - contrast) * 2;
    } else if (contrast > 0.7) {
        ditherStrength = 0.8 - (contrast - 0.7) * 2;
    }

    if (colorSaturation > 0.5) {
        ditherStrength *= 1.1;
    }

    ditherStrength = Math.max(0.5, Math.min(3.0, ditherStrength));

    let contrastAdjustment = 1.2;

    if (brightness < 0.4) {
        contrastAdjustment = 1.4 + (0.4 - brightness) * 0.8;
    } else if (brightness > 0.7) {
        contrastAdjustment = 1.0 - (brightness - 0.7) * 0.5;
    }

    contrastAdjustment = Math.max(0.8, Math.min(2.0, contrastAdjustment));

    return {
        ditherType,
        ditherStrength: parseFloat(ditherStrength.toFixed(1)),
        contrast: parseFloat(contrastAdjustment.toFixed(1))
    };
}

function applyDitherParameters(params) {
    document.getElementById('ditherType').value = params.ditherType;
    document.getElementById('ditherStrength').value = params.ditherStrength;
    document.getElementById('ditherStrengthValue').textContent = params.ditherStrength;
    document.getElementById('contrast').value = params.contrast;
    document.getElementById('contrastValue').textContent = params.contrast;
    document.getElementById('ditherStrengthContainer').style.display =
        params.ditherType === 'adaptive' ? 'none' : '';
}

function autoConfigureDither() {
    if (!currentImageData) {
        alert('请先上传图片');
        return;
    }

    const imageAnalysis = analyzeImage(currentImageData);
    const optimalParams = getOptimalDitherParameters(imageAnalysis);
    applyDitherParameters(optimalParams);

    if (!isDitheringEnabled) {
        toggleDither();
    } else {
        updateImage();
    }

    document.getElementById('imageResult').innerHTML = '<div class="info">已自动配置抖动参数</div>';
}

function convertImage() {
    if (!originalImage) {
        document.getElementById('imageResult').innerHTML = '<div class="error">请先上传图片</div>';
        return;
    }

    try {
        const canvas = document.getElementById('canvas');
        const canvasWidth = getCanvasWidth();
        const canvasHeight = getCanvasHeight();

        const ctx = canvas.getContext('2d');
        const imageData = ctx.getImageData(0, 0, canvasWidth, canvasHeight);

        window.processedDataForDownload = processImageData(imageData);

        document.getElementById('imageResult').innerHTML = `
            <div class="success">转换完成！</div>
            <p>文件大小: ${window.processedDataForDownload.length} 字节 (像素数据)</p>
            <p class="info">文件总大小（含头）: ${window.processedDataForDownload.length + FILM_HEADER_SIZE} 字节</p>
            <p class="info">点击下载按钮保存文件</p>
        `;
    } catch (error) {
        document.getElementById('imageResult').innerHTML = `<div class="error">转换失败: ${error.message}</div>`;
    }
}

function downloadFilmFile() {
    if (!originalImage) {
        document.getElementById('imageResult').innerHTML = '<div class="error">请先上传图片</div>';
        return;
    }

    try {
        window.processedDataForDownload = processImageData(buildDeviceImageData());
    } catch (error) {
        document.getElementById('imageResult').innerHTML = '<div class="error">转换失败: ' + error.message + '</div>';
        return;
    }

    const header = generateFilmHeader();

    // 合并文件头和像素数据
    var totalSize = getFilmFileTotalSize();
    var filmFile = new Uint8Array(totalSize);
    filmFile.set(header, 0);
    filmFile.set(window.processedDataForDownload, FILM_HEADER_SIZE);

    const fileName = document.getElementById('fileName').value || 'output.film';
    downloadFile(filmFile, fileName);

    document.getElementById('imageResult').innerHTML = '<div class="success">下载完成！</div>';
}

function updateCanvasScale() {
    var containers = document.querySelectorAll('.polaroid-inner');
    for (var i = 0; i < containers.length; i++) {
        var container = containers[i];
        var canvas = container.querySelector('canvas');
        if (!canvas) continue;
        var containerWidth = container.clientWidth;
        var containerHeight = container.clientHeight;
        if (containerWidth === 0 || containerHeight === 0) continue;
        // 竖屏设备（Max）画布本身竖屏，无需旋转；横向设备画布旋转 90° 后竖屏显示
        var portrait = getCanvasHeight() > getCanvasWidth();
        var rotatedWidth = portrait ? getCanvasWidth() : getCanvasHeight();
        var rotatedHeight = portrait ? getCanvasHeight() : getCanvasWidth();
        var scaleVal = Math.min(containerWidth / rotatedWidth, containerHeight / rotatedHeight);
        canvas.style.transform = portrait
            ? 'translate(-50%, -50%) scale(' + scaleVal + ')'
            : 'translate(-50%, -50%) rotate(90deg) scale(' + scaleVal + ')';
    }
}