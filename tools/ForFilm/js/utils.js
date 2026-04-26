// 工具函数

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