// 工具函数

// ===== 设备配置 =====
var DEVICE_CONFIGS = {
    FRAMEFILMC3: {
        screenWidth: 768, screenHeight: 552,
        displayName: 'FrameFilm C3', pixelLayout: 'row-major', colorCount: 4
    },
    FRAMEFILM: {
        screenWidth: 600,
        screenHeight: 400,
        displayName: 'FrameFilm',
        pixelLayout: 'rotated' // 列优先翻转: (x * height) + (height - 1 - y)
    },
    FRAMEFILMPRO: {
        screenWidth: 792,
        screenHeight: 528,
        displayName: 'FrameFilm Pro',
        pixelLayout: 'row-major' // 行优先: (y * width) + x
    },
    FRAMEFILMMAX: {
        screenWidth: 1200,
        screenHeight: 1600,
        displayName: 'FrameFilm Max',
        pixelLayout: 'row-major' // 行优先: (y * width) + x
    },
    // 通行证版（Ark）：单机型固件，屏固定 E6 3.70" 720×480（与 PRO 同屏同驱动，但功能集不同）
    FRAMEFILMARK: {
        screenWidth: 720,
        screenHeight: 480,
        displayName: 'FrameFilm Ark',
        pixelLayout: 'rotated-180' // 与同屏的 PRO 一致
    },
    FRAMEFILMDOCK: {
        screenWidth: 760,
        screenHeight: 568,
        displayName: 'FrameFilm Dock',
        pixelLayout: 'row-major', // 行优先: (y * width) + x
        hasKeyboard: true // 仅有底座支持设备按键键值（USB HID 键盘）
    }
};

var currentDeviceType = 'FRAMEFILM';

function getDeviceConfig() {
    return DEVICE_CONFIGS[currentDeviceType] || DEVICE_CONFIGS['FRAMEFILM'];
}

// 是否竖屏设备（画布高 > 宽），如 Max 版 1200x1600
function isPortraitDevice() {
    var cfg = getDeviceConfig();
    return cfg.screenHeight > cfg.screenWidth;
}

// 屏幕条件：8bpp 索引色（ColorFast 55 色 / ColorQual 46 色）的像素主体必须与驱动读取的
// 720*480 字节严格一致，其它分辨率下会越界读取，所以只有 3.7" 720×480 E6 spectra 面板
// （EPD_PANEL_ID 0x02）能渲染。
function is8bppPanelSupported() {
    var cfg = getDeviceConfig();
    return cfg.screenWidth === 720 && cfg.screenHeight === 480;
}

// 可用性 = 屏幕条件 + 机型条件。**目前只对通行证版（FRAMEFILMARK）开放**：
// 只有 frame_film_ark 的 EPD 驱动实现了 8bpp 播放；frame_film（三机型）会按 v1 4bpp
// 解读 8bpp 数据，屏上是乱码且不报错。
// 等 frame_film 也移植 film 2.0 后，把本函数改成直接 `return is8bppPanelSupported();`
// （纯按屏幕判定）即可放开。
function is8bppAvailable() {
    return currentDeviceType === 'FRAMEFILMARK' && is8bppPanelSupported();
}

function setDeviceType(type) {
    if (DEVICE_CONFIGS[type]) {
        currentDeviceType = type;
        // 面板未回包前按机型默认六色；收到 0x42 后由 applyScreenParams 覆盖
        DEVICE_CONFIGS[type].colorCount = type === 'FRAMEFILMC3' ? 4 : 6;
        onDeviceTypeChanged();
    }
}

function onDeviceTypeChanged() {
    // 更新所有 canvas 尺寸
    // 例外：标了 data-fixed-size 的画布内部尺寸是固定的，不能按机型分辨率覆盖。
    // 动画工坊的绘制盘（48×72 格竖屏网格）与取景框（恒为横屏 720×480 数据）都在此列 ——
    // 一旦被改成机型分辨率，绘制盘会变成横屏、网格与落点全错。
    var cfg = getDeviceConfig();
    var canvases = document.querySelectorAll('canvas[id]');
    for (var i = 0; i < canvases.length; i++) {
        if (canvases[i].hasAttribute('data-fixed-size')) continue;
        canvases[i].width = cfg.screenWidth;
        canvases[i].height = cfg.screenHeight;
    }
    // 更新设备类型信息显示
    var badge = document.getElementById('device-type-badge');
    var resolution = document.getElementById('device-resolution');
    var typeInfo = document.getElementById('device-type-info');
    if (badge) {
        badge.textContent = cfg.displayName;
    }
    if (resolution) {
        resolution.textContent = cfg.screenWidth + ' x ' + cfg.screenHeight;
    }
    if (typeInfo) {
        typeInfo.style.display = 'flex';
    }
    // SZ 增强算法仅 FrameFilm Pro 可用
    syncSzEnhancedAvailability();
    // 8bpp 索引色（ColorFast / ColorQual）仅 3.7" 720×480 屏可用
    if (typeof sync8bppAvailability === 'function') {
        sync8bppAvailability();
    }
    // 按键键值设置仅支持的机型可见
    if (typeof syncKeyboardAvailability === 'function') {
        syncKeyboardAvailability();
    }
    // 时间同步（0x4D）仅通行证版（ARK）支持
    if (typeof syncTimeSyncAvailability === 'function') {
        syncTimeSyncAvailability();
    }
    // 蓝牙遥控（0x4E）仅通行证版（ARK）支持
    if (typeof syncRemoteAvailability === 'function') {
        syncRemoteAvailability();
    }
    if (typeof syncArkDeviceNameAvailability === 'function') {
        syncArkDeviceNameAvailability();
    }
    // 动画工坊：设备播放能力随机型变化（仅 ARK 可播）
    if (typeof animSyncDeviceAvailability === 'function') {
        animSyncDeviceAvailability();
    }
    // ARK 通行证版皮肤：连上 FRAMEFILMARK 才切换，其余机型保持原皮肤
    if (typeof arkThemeSync === 'function') {
        arkThemeSync();
    }
}

// 屏幕面板 ID → 机型 + 像素排布（与固件 EPD_PANEL_ID 对应）
var PANEL_CONFIGS = {
    0x01: { deviceType: 'FRAMEFILMPRO', pixelLayout: 'row-major' },    // 3.68" 792×528
    0x02: { deviceType: 'FRAMEFILMPRO', pixelLayout: 'rotated-180' },  // 3.70" 720×480
    0x03: { deviceType: 'FRAMEFILM',    pixelLayout: 'rotated' },      // 3.60" 600×400
    0x05: { deviceType: 'FRAMEFILMMAX', pixelLayout: 'row-major' },    // 7.09" 1200×1600
    0x06: { deviceType: 'FRAMEFILMDOCK', pixelLayout: 'row-major' },   // 3.64" 760×568（底座）
    0x07: { deviceType: 'FRAMEFILMPRO', pixelLayout: 'row-major' },    // 3.98" 768×552（SE0398，六色）
    0x08: { deviceType: 'FRAMEFILMPRO', pixelLayout: 'row-major', colorCount: 4 } // 3.98" 768×552（JD79665AA，四色）
};

// 当前屏幕是否四色（黑/白/黄/红）：由 0x42 回包的面板 ID 决定，驱动会忽略蓝/绿
function is4ColorMode() {
    var cfg = getDeviceConfig();
    return !!cfg && cfg.colorCount === 4;
}

// 根据设备回传的屏幕参数（面板 ID + 分辨率）更新机型、画布尺寸与像素排布
function applyScreenParams(panelId, width, height) {
    var panel = PANEL_CONFIGS[panelId];
    if (!panel) {
        console.warn('[ForFilm] 未知屏幕面板 ID: 0x' + panelId.toString(16));
        return false;
    }
    // 面板 ID 不足以区分机型：0x02（3.70" 720×480）在 Pro 与通行证版（ARK）上是同一块屏。
    // 名字已判定为 ARK 时以名字为准，只更新尺寸/排布，不覆盖机型。
    if (currentDeviceType !== 'FRAMEFILMARK' && currentDeviceType !== 'FRAMEFILMC3') {
        setDeviceType(panel.deviceType);
    }
    var cfg = getDeviceConfig();
    if (cfg) {
        cfg.screenWidth = width;
        cfg.screenHeight = height;
        cfg.pixelLayout = panel.pixelLayout;
        cfg.colorCount = panel.colorCount || 6;
        onDeviceTypeChanged();
    }
    console.log('[ForFilm] 屏幕参数已更新: panelId=0x' + panelId.toString(16) + ', ' + width + 'x' + height + ' (' + cfg.displayName + ', ' + cfg.pixelLayout + ')');
    return true;
}

// SZ 增强（结构感知六色量化）依赖 8bpp 的 3.7" 720×480 屏：
// 三机型固件的 Pro 与通行证版（ARK）同屏同驱动，都可用；其余机型禁用选项，
// 若已选中则回退到 Floyd-Steinberg
function syncSzEnhancedAvailability() {
    var select = document.getElementById('ditherType');
    if (!select) {
        return;
    }
    var option = select.querySelector('option[value="szEnhanced"]');
    var option2 = select.querySelector('option[value="atkinsonSzCalib"]');
    var supportsSz = (currentDeviceType === 'FRAMEFILMPRO' || currentDeviceType === 'FRAMEFILMARK')
                     && !is4ColorMode();
    if (option) {
        option.disabled = !supportsSz;
    }
    if (option2) {
        option2.disabled = !supportsSz;
    }
    if ((select.value === 'szEnhanced' || select.value === 'atkinsonSzCalib') && !supportsSz) {
        select.value = 'floydSteinberg';
        // 恢复滑块显示（对比度/饱和度/抖动强度）
        if (typeof syncAdjustSliders === 'function') {
            syncAdjustSliders();
        }
    }
}

function getCanvasWidth() {
    var w = getDeviceConfig().screenWidth;
    console.log('[DEBUG] getCanvasWidth() = ' + w + ' | deviceType=' + currentDeviceType);
    return w;
}

function getCanvasHeight() {
    var h = getDeviceConfig().screenHeight;
    console.log('[DEBUG] getCanvasHeight() = ' + h + ' | deviceType=' + currentDeviceType);
    return h;
}

function getFilmPixelDataSize() {
    var cfg = getDeviceConfig();
    var size = (cfg.screenWidth * cfg.screenHeight) / 2;
    console.log('[DEBUG] getFilmPixelDataSize() = ' + size + ' | deviceType=' + currentDeviceType);
    return size;
}

function getFilmFileTotalSize() {
    var total = 32 + getFilmPixelDataSize();
    console.log('[DEBUG] getFilmFileTotalSize() = ' + total + ' | deviceType=' + currentDeviceType);
    return total;
}

// 按文件头字段（宽/高/Format/FrameCount）推算 .film 应有的总大小，供传输前校验
// Format: 0x00=v1 4bpp(2px/字节), 0x01=MonoFast 1bpp(8px/字节), 0x02/0x03=8bpp(1px/字节)
// FrameCount（0x0A，小端；0/1 视为单帧）：>1 时主体按帧连续拼接，用于多帧动画
// 见 docs/film/film.md
function getFilmFileExpectedSize(fileData) {
    if (!fileData || fileData.length < 32) {
        return -1;
    }
    var width = fileData[4] | (fileData[5] << 8);
    var height = fileData[6] | (fileData[7] << 8);
    var format = fileData[9];
    var frameCount = fileData[10] | (fileData[11] << 8);
    if (frameCount < 1) {
        frameCount = 1;
    }
    var pixels = width * height;
    var bodySize = (format === 0x01) ? (pixels / 8) : (format >= 0x02 ? pixels : (pixels / 2));
    return 32 + bodySize * frameCount;
}

// 规范化发送到设备的 film 文件名
// 1) 去首尾空格，为空则使用默认名
// 2) 统一补齐 .film 后缀（设备只把 .film 文件计入文件列表）
// 3) 限长 64 字节，避免超出设备端文件名/列表缓冲
function normalizeFilmFileName(raw, fallback) {
    var name = (raw || '').trim();
    if (!name) {
        name = fallback || 'output.film';
    }
    if (!/\.film$/i.test(name)) {
        name = name.replace(/\.[^.\/\\]+$/, '') + '.film';
    }
    if (name.length > 64) {
        name = name.substring(0, 59) + '.film';
    }
    return name;
}

// 根据设备类型返回正确的像素索引
function getPixelIndex(x, y, width, height) {
    var layout = getDeviceConfig().pixelLayout;
    if (layout === 'rotated') {
        // 老设备 FrameFilm: 列优先翻转
        return (x * height) + (height - 1 - y);
    }
    if (layout === 'rotated-180') {
        // 3.70" 720×480 面板: 显示方向与画布差 180°
        return (height - 1 - y) * width + (width - 1 - x);
    }
    // Pro 及默认: 行优先
    return (y * width) + x;
}

// 显示消息提示
function showMessage(message, type = 'info') {
    const messageDiv = document.createElement('div');
    messageDiv.className = `message ${type}`;
    messageDiv.textContent = message;
    messageDiv.style.position = 'fixed';
    messageDiv.style.top = '20px';
    messageDiv.style.left = '50%';
    messageDiv.style.transform = 'translateX(-50%)';
    messageDiv.style.padding = '12px 24px';
    messageDiv.style.borderRadius = 'var(--notice-radius, 8px)';
    messageDiv.style.zIndex = '10000';
    messageDiv.style.fontWeight = '600';
    messageDiv.style.boxShadow = 'var(--notice-shadow, 0 4px 12px rgba(0, 0, 0, 0.15))';
    messageDiv.style.color = 'var(--notice-color, white)';

    // 设置不同类型的颜色
    switch (type) {
        case 'success':
            messageDiv.style.backgroundColor = 'var(--notice-success, #4CAF50)';
            break;
        case 'error':
            messageDiv.style.backgroundColor = 'var(--notice-error, #f44336)';
            break;
        case 'warning':
            messageDiv.style.backgroundColor = 'var(--notice-warning, #ff9800)';
            break;
        default:
            messageDiv.style.backgroundColor = 'var(--notice-info, #2196F3)';
    }

    document.body.appendChild(messageDiv);

    // 3秒后自动移除
    setTimeout(() => {
        messageDiv.style.opacity = '0';
        messageDiv.style.transition = 'opacity 0.3s ease';
        setTimeout(() => {
            document.body.removeChild(messageDiv);
        }, 300);
    }, 3000);
}

function showHint(element) {
    const tooltip = element.querySelector('.hint-tooltip');
    if (tooltip) {
        tooltip.style.display = tooltip.style.display === 'block' ? 'none' : 'block';
        setTimeout(() => {
            tooltip.style.display = 'none';
        }, 3000);
    }
}

// 检查浏览器是否支持蓝牙
function checkBluetoothSupport() {
    return navigator.bluetooth !== undefined;
}

// 检查是否在移动设备上
function isMobileDevice() {
    return /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
}

// 防抖函数
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

// 节流函数
function throttle(func, limit) {
    let inThrottle;
    return function executedFunction(...args) {
        if (!inThrottle) {
            func.apply(this, args);
            inThrottle = true;
            setTimeout(() => inThrottle = false, limit);
        }
    };
}

// 格式化文件大小
function formatFileSize(bytes) {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// 生成随机ID
function generateId() {
    return Math.random().toString(36).substring(2, 15) + Math.random().toString(36).substring(2, 15);
}

// 深拷贝对象
function deepClone(obj) {
    if (obj === null || typeof obj !== 'object') return obj;
    if (obj instanceof Date) return new Date(obj.getTime());
    if (obj instanceof Array) return obj.map(item => deepClone(item));
    if (typeof obj === 'object') {
        const clonedObj = {};
        for (const key in obj) {
            if (obj.hasOwnProperty(key)) {
                clonedObj[key] = deepClone(obj[key]);
            }
        }
        return clonedObj;
    }
}

// 检查是否为有效的JSON
function isValidJSON(str) {
    try {
        JSON.parse(str);
        return true;
    } catch (e) {
        return false;
    }
}

// 延迟函数
function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// 计算两点之间的距离
function distance(x1, y1, x2, y2) {
    return Math.sqrt(Math.pow(x2 - x1, 2) + Math.pow(y2 - y1, 2));
}

// 限制数字在指定范围内
function clamp(value, min, max) {
    return Math.min(Math.max(value, min), max);
}

// 线性插值
function lerp(start, end, t) {
    return start + (end - start) * t;
}

// 映射值到新范围
function map(value, inMin, inMax, outMin, outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
