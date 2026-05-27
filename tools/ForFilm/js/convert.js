// 照片转换功能

// 全局变量
let currentImageData = null;
let isDitheringEnabled = false;
let canvasRotation = 0; // 0: 原始, 1: 旋转90度
let scale = 1.0;
let offsetX = 0;
let offsetY = 0;
let isDragging = false;
let startX = 0;
let startY = 0;
let startOffsetX = 0;
let startOffsetY = 0;
let originalImage = null;
let uploadedFileName = 'output';
window.processedDataForDownload = null;

// Film 文件格式常量
const FILM_SCREEN_WIDTH = 600;
const FILM_SCREEN_HEIGHT = 400;
const FILM_HEADER_SIZE = 32;
const FILM_PIXEL_DATA_SIZE = (FILM_SCREEN_WIDTH * FILM_SCREEN_HEIGHT) / 2;  // 120000 字节
const FILM_FILE_TOTAL_SIZE = FILM_HEADER_SIZE + FILM_PIXEL_DATA_SIZE;  // 120032 字节

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
        updateImage();
    });
    document.getElementById('contrast').addEventListener('input', function() {
        document.getElementById('contrastValue').textContent = this.value;
        updateImage();
    });
    document.getElementById('ditherType').addEventListener('change', updateImage);
    
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
            const relativeX = mouseX / canvas.width;
            const relativeY = mouseY / canvas.height;
            
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
            
            // 根据旋转状态调整偏移量
            if (canvasRotation === 1) {
                // 旋转状态下，X和Y方向互换并调整符号，使鼠标移动与图片视觉移动一致
                offsetX = startOffsetX - deltaY;
                offsetY = startOffsetY + deltaX;
            } else {
                // 非旋转状态下，正常处理
                offsetX = startOffsetX + deltaX;
                offsetY = startOffsetY + deltaY;
            }
            
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
            
            if (canvasRotation === 1) {
                offsetX = startOffsetX - deltaY;
                offsetY = startOffsetY + deltaX;
            } else {
                offsetX = startOffsetX + deltaX;
                offsetY = startOffsetY + deltaY;
            }
            
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
            const relativeX = (centerX - rect.left) / canvas.width;
            const relativeY = (centerY - rect.top) / canvas.height;
            
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

    canvas.addEventListener('touchend', function() {
        isDragging = false;
        isPinching = false;
    });
}

// 头文件转换功能已移除

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
    const header = new Array(FILM_HEADER_SIZE).fill(0);

    // FileSize (4 bytes) - 小端序
    const fileSize = FILM_PIXEL_DATA_SIZE;
    header[0] = fileSize & 0xFF;
    header[1] = (fileSize >> 8) & 0xFF;
    header[2] = (fileSize >> 16) & 0xFF;
    header[3] = (fileSize >> 24) & 0xFF;

    // ScreenWidth (2 bytes) - 小端序
    header[4] = FILM_SCREEN_WIDTH & 0xFF;
    header[5] = (FILM_SCREEN_WIDTH >> 8) & 0xFF;

    // ScreenHeight (2 bytes) - 小端序
    header[6] = FILM_SCREEN_HEIGHT & 0xFF;
    header[7] = (FILM_SCREEN_HEIGHT >> 8) & 0xFF;

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
            canvasRotation = 0;

            const canvas = document.getElementById('canvas');
            const canvasWidth = canvas.width;
            const canvasHeight = canvas.height;

            const imgWidth = img.width;
            const imgHeight = img.height;

            const scaleX = canvasWidth / imgWidth;
            const scaleY = canvasHeight / imgHeight;
            scale = Math.min(scaleX, scaleY);

            const scaledWidth = imgWidth * scale;
            const scaledHeight = imgHeight * scale;
            offsetX = (canvasWidth - scaledWidth) / 2;
            offsetY = (canvasHeight - scaledHeight) / 2;

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
    
    if (isDitheringEnabled && currentImageData) {
        // Automatically configure dithering parameters when enabled
        const imageAnalysis = analyzeImage(currentImageData);
        const optimalParams = getOptimalDitherParameters(imageAnalysis);
        applyDitherParameters(optimalParams);
    }
    
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
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    document.getElementById('imageResult').innerHTML = '';
    document.getElementById('fileName').value = 'output.film';
}

function rotateCanvas() {
    canvasRotation = (canvasRotation + 1) % 2;

    const canvas = document.getElementById('canvas');
    const canvasWidth = canvas.width;
    const canvasHeight = canvas.height;

    let effectiveWidth, effectiveHeight;
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

function updateImage() {
    if (!originalImage) return;

    const canvas = document.getElementById('canvas');
    const ctx = canvas.getContext('2d');
    const canvasWidth = canvas.width;
    const canvasHeight = canvas.height;

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
    } else {
        ctx.putImageData(imageData, 0, 0); // 显示原始图像（调整对比度后）
    }
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
    return Math.sqrt(0.2 * dl * dl + 3 * da * da + 3 * db * db);
}

function findClosestColor(r, g, b) {
    // 蓝色特殊情况
    if (r < 50 && g < 150 && b > 100) {
        return rgbPalette[4]; // 蓝色
    }

    const inputLab = rgbToLab(r, g, b);
    let minDistance = Infinity;
    let closestColor = rgbPalette[0];

    for (const color of rgbPalette) {
        const colorLab = rgbToLab(color.r, color.g, color.b);
        const distance = labDistance(inputLab, colorLab);
        if (distance < minDistance) {
            minDistance = distance;
            closestColor = color;
        }
    }

    return closestColor;
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

function ditherImage(imageData) {
    const ditherType = document.getElementById('ditherType').value;
    const ditherStrength = parseFloat(document.getElementById('ditherStrength').value);

    switch (ditherType) {
        case 'floydSteinberg':
            return floydSteinbergDither(imageData, ditherStrength);
        case 'atkinson':
            return atkinsonDither(imageData, ditherStrength);
        case 'stucki':
            return stuckiDither(imageData, ditherStrength);
        case 'jarvis':
            return jarvisDither(imageData, ditherStrength);
        default:
            return imageData;
    }
}

function decodeProcessedData(processedData, width, height) {
    const imageData = new ImageData(width, height);
    const data = imageData.data;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const newIndex = (x * height) + (height - 1 - y);
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

    const processedData = new Uint8Array(FILM_PIXEL_DATA_SIZE);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const index = (y * width + x) * 4;
            const r = data[index];
            const g = data[index + 1];
            const b = data[index + 2];

            const closest = findClosestColor(r, g, b);
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
        const canvasWidth = canvas.width;
        const canvasHeight = canvas.height;

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
    if (!window.processedDataForDownload) {
        document.getElementById('imageResult').innerHTML = '<div class="error">请先点击转换按钮</div>';
        return;
    }

    // 生成文件头
    const header = generateFilmHeader();

    // 合并文件头和像素数据
    const filmFile = new Uint8Array(FILM_FILE_TOTAL_SIZE);
    filmFile.set(header, 0);
    filmFile.set(window.processedDataForDownload, FILM_HEADER_SIZE);

    const fileName = document.getElementById('fileName').value || 'output.film';
    downloadFile(filmFile, fileName);
}