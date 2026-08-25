// 工具函数

// ===== 设备配置 =====
var DEVICE_CONFIGS = {
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
    FRAMEFILMSE: {
        screenWidth: 720,
        screenHeight: 480,
        displayName: 'FrameFilm SE',
        pixelLayout: 'transposed' // 面板 480x720 竖装呈横屏，film 按面板行序（转置）打包: (x * height) + y
    },
    FRAMEFILMMAX: {
        screenWidth: 1200,
        screenHeight: 1600,
        displayName: 'FrameFilm Max',
        pixelLayout: 'row-major' // 行优先: (y * width) + x
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

function setDeviceType(type) {
    if (DEVICE_CONFIGS[type]) {
        currentDeviceType = type;
        onDeviceTypeChanged();
    }
}

function onDeviceTypeChanged() {
    // 更新所有 canvas 尺寸
    var cfg = getDeviceConfig();
    var canvases = document.querySelectorAll('canvas[id]');
    for (var i = 0; i < canvases.length; i++) {
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

// 根据设备类型返回正确的像素索引
function getPixelIndex(x, y, width, height) {
    var layout = getDeviceConfig().pixelLayout;
    if (layout === 'rotated') {
        // 老设备 FrameFilm: 列优先翻转
        return (x * height) + (height - 1 - y);
    }
    if (layout === 'transposed') {
        // SE 版: 面板 480x720 竖装呈横屏，film 按面板行序打包（列优先转置）
        return (x * height) + y;
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
    messageDiv.style.borderRadius = '8px';
    messageDiv.style.zIndex = '10000';
    messageDiv.style.fontWeight = '600';
    messageDiv.style.boxShadow = '0 4px 12px rgba(0, 0, 0, 0.15)';

    // 设置不同类型的颜色
    switch (type) {
        case 'success':
            messageDiv.style.backgroundColor = '#4CAF50';
            messageDiv.style.color = 'white';
            break;
        case 'error':
            messageDiv.style.backgroundColor = '#f44336';
            messageDiv.style.color = 'white';
            break;
        case 'warning':
            messageDiv.style.backgroundColor = '#ff9800';
            messageDiv.style.color = 'white';
            break;
        default:
            messageDiv.style.backgroundColor = '#2196F3';
            messageDiv.style.color = 'white';
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