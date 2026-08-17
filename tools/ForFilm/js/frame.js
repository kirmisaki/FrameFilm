let frameOriginalImage = null;
let frameScale = 1.0;
let frameOffsetX = 0;
let frameOffsetY = 0;
let frameCanvasRotation = 0;
let frameCameraStream = null;
let frameActiveTab = 'frame-upload';

function initFrameTabSwitch() {
    var tabs = document.querySelectorAll('.frame-tab');
    tabs.forEach(function(tab) {
        tab.addEventListener('click', function() {
            var tabId = this.getAttribute('data-frame-tab');
            tabs.forEach(function(t) { t.classList.remove('active'); });
            this.classList.add('active');
            document.querySelectorAll('.frame-tab-content').forEach(function(c) {
                c.classList.remove('active');
            });
            document.getElementById(tabId).classList.add('active');
            frameActiveTab = tabId;
            if (tabId !== 'frame-camera' && frameCameraStream) {
                frameStopCamera();
            }
            setTimeout(function() {
                updateCanvasScale();
            }, 50);
        });
    });
}

function initFrameUpload() {
    var fileInput = document.getElementById('frameFileInput');
    var selectBtn = document.getElementById('frameSelectBtn');

    selectBtn.addEventListener('click', function() {
        fileInput.click();
    });

    fileInput.addEventListener('change', function(e) {
        var file = e.target.files[0];
        if (!file) return;
        e.target.value = '';
        var reader = new FileReader();
        reader.onload = function(ev) {
            var img = new Image();
            img.onload = function() {
                frameOriginalImage = img;
                frameSetupImage('frame-canvas', false);
                frameUpdateImage('frame-canvas');
                document.getElementById('frameUploadBtn').disabled = false;
            };
            img.src = ev.target.result;
        };
        reader.readAsDataURL(file);
    });

    document.getElementById('frameUploadBtn').addEventListener('click', function() {
        frameUploadToDevice('frame-canvas', 'frame-transfer-');
    });
}

function initFrameCamera() {
    var captureBtn = document.getElementById('frameCaptureBtn');
    var confirmBtn = document.getElementById('frameCaptureConfirm');

    captureBtn.addEventListener('click', function() {
        frameStartCamera();
    });

    confirmBtn.addEventListener('click', function() {
        frameCapturePhoto();
    });

    document.getElementById('frameCameraUploadBtn').addEventListener('click', function() {
        frameUploadToDevice('frame-camera-canvas', 'frame-camera-transfer-');
    });
}

function frameStartCamera() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        showMessage('当前浏览器不支持相机功能', 'error');
        return;
    }

    navigator.mediaDevices.getUserMedia({
        video: { facingMode: 'environment', width: { ideal: 1280 }, height: { ideal: 960 } }
    }).then(function(stream) {
        frameCameraStream = stream;
        var video = document.getElementById('frameVideo');
        video.srcObject = stream;
        document.getElementById('frame-video-container').style.display = 'block';
        document.getElementById('frameCaptureBtn').style.display = 'none';
    }).catch(function(err) {
        showMessage('无法访问相机: ' + err.message, 'error');
    });
}

function frameCapturePhoto() {
    var video = document.getElementById('frameVideo');
    var tempCanvas = document.createElement('canvas');
    tempCanvas.width = video.videoWidth;
    tempCanvas.height = video.videoHeight;
    var ctx = tempCanvas.getContext('2d');
    ctx.drawImage(video, 0, 0);

    var img = new Image();
    img.onload = function() {
        frameOriginalImage = img;
        frameSetupImage('frame-camera-canvas', true);
        frameUpdateImage('frame-camera-canvas');
        document.getElementById('frameCameraUploadBtn').disabled = false;
    };
    img.src = tempCanvas.toDataURL('image/png');

    frameStopCamera();
}

function frameStopCamera() {
    if (frameCameraStream) {
        frameCameraStream.getTracks().forEach(function(track) {
            track.stop();
        });
        frameCameraStream = null;
    }
    var video = document.getElementById('frameVideo');
    video.srcObject = null;
    document.getElementById('frame-video-container').style.display = 'none';
    document.getElementById('frameCaptureBtn').style.display = '';
}

function frameSetupImage(canvasId, fill) {
    var canvas = document.getElementById(canvasId);
    var img = frameOriginalImage;

    // 竖屏设备（Max）：横图旋转 90°，竖图直接显示；横向设备：竖图旋转
    frameCanvasRotation = isPortraitDevice()
        ? (img.width > img.height ? 1 : 0)
        : (img.height > img.width ? 1 : 0);

    var effectiveWidth = frameCanvasRotation === 1 ? getCanvasHeight() : getCanvasWidth();
    var effectiveHeight = frameCanvasRotation === 1 ? getCanvasWidth() : getCanvasHeight();
    var scaleX = effectiveWidth / img.width;
    var scaleY = effectiveHeight / img.height;
    frameScale = fill ? Math.max(scaleX, scaleY) : Math.min(scaleX, scaleY);
    frameOffsetX = 0;
    frameOffsetY = 0;
}

function frameUpdateImage(canvasId) {
    if (!frameOriginalImage) return;

    var canvas = document.getElementById(canvasId);
    var canvasWidth = getCanvasWidth();
    var canvasHeight = getCanvasHeight();
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    var ctx = canvas.getContext('2d');

    ctx.clearRect(0, 0, canvasWidth, canvasHeight);
    ctx.save();

    var effectiveWidth = canvasWidth;
    var effectiveHeight = canvasHeight;

    if (frameCanvasRotation === 1) {
        ctx.translate(0, canvasHeight);
        ctx.rotate(-Math.PI / 2);
        effectiveWidth = canvasHeight;
        effectiveHeight = canvasWidth;
    }

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, effectiveWidth, effectiveHeight);

    var imgWidth = frameOriginalImage.width * frameScale;
    var imgHeight = frameOriginalImage.height * frameScale;
    var drawX = (effectiveWidth - imgWidth) / 2 + frameOffsetX;
    var drawY = (effectiveHeight - imgHeight) / 2 + frameOffsetY;

    ctx.save();
    ctx.beginPath();
    ctx.rect(0, 0, effectiveWidth, effectiveHeight);
    ctx.clip();

    ctx.drawImage(
        frameOriginalImage,
        0, 0,
        frameOriginalImage.width, frameOriginalImage.height,
        drawX, drawY,
        imgWidth, imgHeight
    );

    ctx.restore();
    ctx.restore();

    var imageData = ctx.getImageData(0, 0, canvasWidth, canvasHeight);
    adjustContrast(imageData, 1.2);

    var processedImageData = adaptiveDither(imageData);
    var processedData = processImageData(processedImageData);
    var finalImageData = decodeProcessedData(processedData, canvasWidth, canvasHeight);
    ctx.putImageData(finalImageData, 0, 0);
    updateCanvasScale();
}

function frameUploadToDevice(canvasId, prefix) {
    if (typeof device === 'undefined' || !device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    if (!frameOriginalImage) {
        showMessage('请先选择照片', 'error');
        return;
    }

    try {
        var canvas = document.getElementById(canvasId);
        var ctx = canvas.getContext('2d');
        var cw = getCanvasWidth();
        var ch = getCanvasHeight();
        var imageData = ctx.getImageData(0, 0, cw, ch);
        var processedData = processImageData(imageData);
        var header = generateFilmHeader();
        var totalSize = getFilmFileTotalSize();
        var fileData = new Uint8Array(totalSize);
        fileData.set(header, 0);
        fileData.set(processedData, 32);

        frameUploadViaBle('frame.film', fileData, prefix);
    } catch (error) {
        showMessage('转换失败: ' + error.message, 'error');
    }
}

function frameQuoteUploadToDevice() {
    if (typeof device === 'undefined' || !device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    try {
        var canvas = document.getElementById('frame-quote-canvas');
        var ctx = canvas.getContext('2d');
        var cw = getCanvasWidth();
        var ch = getCanvasHeight();
        var imageData = ctx.getImageData(0, 0, cw, ch);
        var processedData = processImageData(imageData);
        var header = generateFilmHeader();
        var totalSize = getFilmFileTotalSize();
        var fileData = new Uint8Array(totalSize);
        fileData.set(header, 0);
        fileData.set(processedData, 32);

        frameUploadViaBle('quote.film', fileData, 'frame-quote-transfer-');
    } catch (error) {
        showMessage('转换失败: ' + error.message, 'error');
    }
}

async function frameUploadViaBle(fileName, fileData, prefix) {
    var expectedSize = getFilmFileTotalSize();
    if (fileData.length !== expectedSize) {
        showMessage('文件大小不符合要求(应为' + expectedSize + '字节)', 'error');
        return;
    }

    var container = document.getElementById(prefix + 'container');
    if (container) {
        container.style.display = 'block';
    }

    frameUpdateTransferStatus(prefix, '准备传输...', 0);

    try {
        filmTransState = BLE_FILM_TRANS_STATE_STARTED;
        filmTransFileName = fileName;
        filmTransFileSize = fileData.length;
        filmTransSentBytes = 0;

        await sendBleFileStart();
        await sendBleFileName(fileName);
        await sendBleFileLen(fileData.length);

        var chunkSize = BLE_CHUNK_SIZE;
        var sentBytes = 0;
        for (var i = 0; i < fileData.length; i += chunkSize) {
            var chunk = fileData.slice(i, i + chunkSize);
            await sendBleFileData(chunk);
            sentBytes += chunk.length;
            filmTransSentBytes = sentBytes;
            var progress = Math.round((sentBytes / fileData.length) * 100);
            frameUpdateTransferStatus(prefix, '传输中...', progress);
        }

        await sendBleFileStop();
        frameUpdateTransferStatus(prefix, '传输完成', 100);
        showMessage('文件传输完成', 'success');
    } catch (error) {
        frameUpdateTransferStatus(prefix, '传输失败: ' + error.message, 0);
        showMessage('传输失败: ' + error.message, 'error');
    }
}

function frameUpdateTransferStatus(prefix, message, progress) {
    var statusEl = document.getElementById(prefix + 'status');
    var progressBarEl = document.getElementById(prefix + 'progress-bar');
    var progressEl = document.getElementById(prefix + 'progress');

    if (statusEl) statusEl.textContent = message;
    if (progressBarEl) progressBarEl.style.width = progress + '%';
    if (progressEl) progressEl.textContent = progress + '%';
}

function initFrameCanvasInteraction(canvasId, btnId) {
}

// ===== 每日一句 =====

var frameCurrentQuote = { text: '', author: '' };

// 六色方案
var frameColorSchemes = [
    { bg: '#ffffff', text: '#000000', accent: '#ff0000', author: '#000000' },
    { bg: '#ffffff', text: '#000000', accent: '#0000ff', author: '#000000' },
    { bg: '#ffffff', text: '#000000', accent: '#29cc14', author: '#000000' },
];

function initFrameQuote() {
    var quoteBtn = document.getElementById('frameQuoteBtn');
    var sendBtn = document.getElementById('frameQuoteUploadBtn');
    var customToggle = document.getElementById('frameCustomQuoteToggle');
    var customPanel = document.getElementById('frame-custom-quote-panel');
    var customBtn = document.getElementById('frameCustomQuoteBtn');
    var customText = document.getElementById('frameCustomText');
    var customAuthor = document.getElementById('frameCustomAuthor');

    quoteBtn.addEventListener('click', function() {
        frameFetchQuote();
    });

    sendBtn.addEventListener('click', function() {
        frameQuoteUploadToDevice();
    });

    // 自定义名言面板切换
    customToggle.addEventListener('click', function() {
        var isHidden = customPanel.style.display === 'none';
        customPanel.style.display = isHidden ? 'flex' : 'none';
    });

    // 生成自定义名言
    customBtn.addEventListener('click', function() {
        var text = customText.value.trim();
        if (!text) {
            customText.focus();
            return;
        }
        var author = customAuthor.value.trim();
        frameCurrentQuote.text = text;
        frameCurrentQuote.author = author;
        frameRenderQuote(text, author);
    });

    // 支持 Ctrl+Enter 键生成
    customText.addEventListener('keydown', function(e) {
        if (e.key === 'Enter' && e.ctrlKey) {
            customBtn.click();
        }
    });

    frameFetchQuote();
}

function frameFetchQuote() {
    frameRenderQuote('加载中…', '');
    fetch('https://international.v1.hitokoto.cn/?c=d&c=h&c=k&c=i&encode=json')
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (data && data.hitokoto) {
                frameCurrentQuote.text = data.hitokoto;
                frameCurrentQuote.author = data.from_who || data.from || '';
                frameRenderQuote(frameCurrentQuote.text, frameCurrentQuote.author);
            }
        })
        .catch(function() {
            frameRenderQuote('暂无法获取，请稍后再试', '');
        });
}

function frameRenderQuote(text, author) {
    var canvas = document.getElementById('frame-quote-canvas');
    var cw = getCanvasWidth();
    var ch = getCanvasHeight();
    canvas.width = cw;
    canvas.height = ch;
    var ctx = canvas.getContext('2d');

    var portrait = getCanvasHeight() > getCanvasWidth();
    ctx.save();
    var w, h;
    if (portrait) {
        // 竖屏设备（Max）：画布本身竖屏，直接绘制
        w = cw;
        h = ch;
    } else {
        // 横向设备：反向旋转抵消CSS的rotate(90deg)，有效绘制区域为 height x width
        ctx.translate(0, ch);
        ctx.rotate(-Math.PI / 2);
        w = ch;
        h = cw;
    }

    // 布局缩放系数：以 Pro 有效宽（528）为基准，仅放大竖屏大屏设备（Max），不改变基础版/Pro 现有布局
    var s = Math.max(1, w / 528);

    // 随机配色方案
    var scheme = frameColorSchemes[Math.floor(Math.random() * frameColorSchemes.length)];

    // 纯色背景
    ctx.fillStyle = scheme.bg;
    ctx.fillRect(0, 0, w, h);

    // 装饰引号
    ctx.font = 'italic ' + Math.round(80 * s) + 'px Georgia, serif';
    ctx.fillStyle = scheme.accent;
    ctx.fillText('\u201C', 20 * s, 90 * s);

    // 装饰线
    ctx.strokeStyle = scheme.accent;
    ctx.lineWidth = 3 * s;
    ctx.beginPath();
    ctx.moveTo(35 * s, 105 * s);
    ctx.lineTo(90 * s, 105 * s);
    ctx.stroke();

    // 文字
    ctx.font = 'bold ' + Math.round(30 * s) + 'px "Noto Serif SC", "Source Han Serif SC", "Songti SC", "SimSun", "Apple Color Emoji", "Segoe UI Emoji", "Noto Color Emoji", serif';
    ctx.fillStyle = scheme.text;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    
    var zhLines = frameWrapText(ctx, text, 30 * s, w - 80 * s);
    var zhLineHeight = 44 * s;
    var zhTotalHeight = zhLines.length * zhLineHeight;
    // 整体居中：考虑作者和装饰线的空间
    var bottomSpace = author ? 130 * s : 60 * s;  // 作者+装饰线+日期的空间
    var availableHeight = h - 100 * s - bottomSpace;  // 减去顶部和底部空间
    var zhStartY = 100 * s + (availableHeight - zhTotalHeight) / 2;

    for (var i = 0; i < zhLines.length; i++) {
        ctx.fillText(zhLines[i], w / 2, zhStartY + i * zhLineHeight);
    }

    // 作者
    if (author) {
        ctx.font = 'bold ' + Math.round(18 * s) + 'px "Noto Serif SC", "Songti SC", "SimSun", "Apple Color Emoji", "Segoe UI Emoji", "Noto Color Emoji", serif';
        ctx.fillStyle = scheme.author;
        ctx.fillText('\u2014\u2014 ' + author, w / 2, h - 130 * s);
    }

    // 底部装饰线
    ctx.strokeStyle = scheme.accent;
    ctx.lineWidth = 4 * s;
    ctx.beginPath();
    ctx.moveTo(w / 2 - 30 * s, h - 65 * s);
    ctx.lineTo(w / 2 + 30 * s, h - 65 * s);
    ctx.stroke();

    // 电量图标（右上角）
    var batteryX = w - 55 * s;
    var batteryY = 20 * s;
    var batteryWidth = 35 * s;
    var batteryHeight = 18 * s;
    var batteryLevel = (typeof deviceBatteryLevel !== 'undefined') ? deviceBatteryLevel / 100 : 0;
    var batteryRadius = 4 * s;

    // 绘制圆角矩形函数
    function drawRoundedRect(x, y, width, height, radius) {
        ctx.beginPath();
        ctx.moveTo(x + radius, y);
        ctx.lineTo(x + width - radius, y);
        ctx.quadraticCurveTo(x + width, y, x + width, y + radius);
        ctx.lineTo(x + width, y + height - radius);
        ctx.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
        ctx.lineTo(x + radius, y + height);
        ctx.quadraticCurveTo(x, y + height, x, y + height - radius);
        ctx.lineTo(x, y + radius);
        ctx.quadraticCurveTo(x, y, x + radius, y);
        ctx.closePath();
    }

    // 电池外框
    drawRoundedRect(batteryX, batteryY, batteryWidth, batteryHeight, batteryRadius);
    ctx.strokeStyle = scheme.accent;
    ctx.lineWidth = 1.5 * s;
    ctx.stroke();

    // 电池正极
    ctx.fillStyle = scheme.accent;
    ctx.fillRect(batteryX + batteryWidth + 1 * s, batteryY + 5 * s, 3 * s, batteryHeight - 10 * s);

    // 电池电量
    drawRoundedRect(batteryX + 2 * s, batteryY + 2 * s, (batteryWidth - 4 * s) * batteryLevel, batteryHeight - 4 * s, 2 * s);
    ctx.fillStyle = scheme.accent;
    ctx.fill();

    // 日期显示（底部居中）
    var now = new Date();
    var dateStr = now.getFullYear() + ' 年 ' + (now.getMonth() + 1).toString().padStart(2, '0') + ' 月 ' + now.getDate().toString().padStart(2, '0') + ' 日';
    ctx.font = '600 ' + Math.round(16 * s) + 'px "Noto Sans SC", "PingFang SC", "Microsoft YaHei", sans-serif';
    ctx.fillStyle = scheme.accent;
    ctx.textAlign = 'center';
    ctx.fillText(dateStr, w / 2, h - 30 * s);

    ctx.restore();

    // 转换为六色显示效果（根据开关决定是否使用抖动）
    var imageData = ctx.getImageData(0, 0, cw, ch);
    var useDither = document.getElementById('frameQuoteDither').checked;
    var processedData = useDither 
        ? processImageData(floydSteinbergDither(imageData, 0.8))
        : processImageData(imageData);
    var finalData = decodeProcessedData(processedData, cw, ch);
    ctx.putImageData(finalData, 0, 0);

    updateCanvasScale();
}

function frameWrapText(ctx, text, fontSize, maxWidth) {
    var lines = [];
    // 先按手动换行符分割
    var paragraphs = text.split('\n');
    for (var p = 0; p < paragraphs.length; p++) {
        var currentLine = '';
        // 使用Array.from正确处理emoji等多码点字符
        var chars = Array.from(paragraphs[p]);
        if (chars.length === 0) {
            lines.push('');  // 保留空行
            continue;
        }
        for (var i = 0; i < chars.length; i++) {
            var char = chars[i];
            var testLine = currentLine + char;
            var metrics = ctx.measureText(testLine);
            if (metrics.width > maxWidth && currentLine.length > 0) {
                lines.push(currentLine);
                currentLine = char;
            } else {
                currentLine = testLine;
            }
        }
        if (currentLine.length > 0) {
            lines.push(currentLine);
        }
    }
    return lines;
}

// ===== 初始化 =====

function initFramePage() {
    initFrameTabSwitch();
    initFrameUpload();
    initFrameCamera();
    initFrameQuote();
    initFrameCanvasInteraction('frame-canvas', 'frameUploadBtn');
    initFrameCanvasInteraction('frame-camera-canvas', 'frameCameraUploadBtn');

    document.querySelectorAll('.nav-item').forEach(function(item) {
        if (item.getAttribute('data-page') !== 'frame-page') {
            item.addEventListener('click', function() {
                if (frameCameraStream) {
                    frameStopCamera();
                }
            });
        }
    });
}

window.addEventListener('DOMContentLoaded', initFramePage);
