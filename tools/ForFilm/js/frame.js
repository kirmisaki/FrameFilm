let frameOriginalImage = null;
let frameScale = 1.0;
let frameOffsetX = 0;
let frameOffsetY = 0;
let frameCanvasRotation = 0;
let frameCameraStream = null;
let frameActiveTab = 'frame-upload';

const FRAME_CANVAS_WIDTH = 600;
const FRAME_CANVAS_HEIGHT = 400;

function initFramePage() {
    initFrameTabSwitch();
    initFrameUpload();
    initFrameCamera();
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
                frameSetupImage('frame-canvas');
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
        frameSetupImage('frame-camera-canvas');
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

function frameSetupImage(canvasId) {
    var canvas = document.getElementById(canvasId);
    var img = frameOriginalImage;

    if (img.height > img.width) {
        frameCanvasRotation = 1;
    } else {
        frameCanvasRotation = 0;
    }

    var effectiveWidth = frameCanvasRotation === 1 ? CANVAS_HEIGHT : CANVAS_WIDTH;
    var effectiveHeight = frameCanvasRotation === 1 ? CANVAS_WIDTH : CANVAS_HEIGHT;
    var scaleX = effectiveWidth / img.width;
    var scaleY = effectiveHeight / img.height;
    frameScale = Math.min(scaleX, scaleY);
    frameOffsetX = 0;
    frameOffsetY = 0;
}

function frameUpdateImage(canvasId) {
    if (!frameOriginalImage) return;

    var canvas = document.getElementById(canvasId);
    canvas.width = CANVAS_WIDTH;
    canvas.height = CANVAS_HEIGHT;
    var ctx = canvas.getContext('2d');
    var canvasWidth = CANVAS_WIDTH;
    var canvasHeight = CANVAS_HEIGHT;

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
        var imageData = ctx.getImageData(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);
        var processedData = processImageData(imageData);
        var header = generateFilmHeader();
        var fileData = new Uint8Array(FILM_FILE_TOTAL_SIZE);
        fileData.set(header, 0);
        fileData.set(processedData, FILM_HEADER_SIZE);

        frameUploadViaBle('frame.film', fileData, prefix);
    } catch (error) {
        showMessage('转换失败: ' + error.message, 'error');
    }
}

async function frameUploadViaBle(fileName, fileData, prefix) {
    var expectedSize = FILM_FILE_TOTAL_SIZE;
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

window.addEventListener('DOMContentLoaded', initFramePage);
