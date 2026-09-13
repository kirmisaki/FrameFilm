// Frame 页 · 批量上传
//
// 多选相册照片 → 逐张转换（与 frame.js 同一套旋转/缩放/量化流程）
// → 逐张按 START/NAME/LEN/DATA/STOP 序列写入设备。

var batchQueue = [];
var batchSeq = 0;
var batchRunning = false;

var BATCH_TRANSFER_PREFIX = 'frame-batch-transfer-';

/* ---------- 工具 ---------- */

function batchEscape(str) {
    return String(str).replace(/[&<>"']/g, function (c) {
        return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
}

function batchRowEl(id) {
    return document.querySelector('.batch-row[data-id="' + id + '"]');
}

function batchSetState(item, state, label) {
    item.state = state;
    var row = batchRowEl(item.id);
    if (!row) return;
    row.dataset.state = state;
    var stateEl = row.querySelector('.batch-state');
    if (stateEl) stateEl.textContent = label;
}

function batchUpdateProgress(label, percent) {
    var statusEl = document.getElementById(BATCH_TRANSFER_PREFIX + 'status');
    var barEl = document.getElementById(BATCH_TRANSFER_PREFIX + 'progress-bar');
    var percentEl = document.getElementById(BATCH_TRANSFER_PREFIX + 'progress');
    if (statusEl) statusEl.textContent = label;
    if (barEl) barEl.style.width = percent + '%';
    if (percentEl) percentEl.textContent = percent + '%';
}

function batchRefreshCount() {
    var countEl = document.getElementById('batchCount');
    var startBtn = document.getElementById('batchStartBtn');
    var n = batchQueue.length;
    if (countEl) countEl.textContent = n + ' 张';
    if (startBtn) {
        startBtn.disabled = batchRunning || n === 0;
    }
    document.querySelectorAll('.batch-remove').forEach(function (btn) {
        btn.disabled = batchRunning;
    });
}

/* ---------- 列表渲染 ---------- */

function batchRenderList() {
    var listEl = document.getElementById('batchList');
    if (!listEl) return;

    if (batchQueue.length === 0) {
        listEl.innerHTML = '<div class="empty-state">还没有选择照片</div>';
        batchRefreshCount();
        return;
    }

    var html = '';
    batchQueue.forEach(function (item) {
        html += '<div class="batch-row" data-id="' + item.id + '" data-state="' + item.state + '">'
            + '<img class="batch-thumb" src="' + item.url + '" alt="' + batchEscape(item.name) + '">'
            + '<span class="batch-meta"><b>' + batchEscape(item.name) + '</b><small>' + item.size + '</small></span>'
            + '<span class="batch-state">' + item.label + '</span>'
            + '<button type="button" class="batch-remove" data-id="' + item.id + '" aria-label="移除 ' + batchEscape(item.name) + '">'
            + '<svg class="ic" viewBox="0 0 24 24"><use href="#i-trash"/></svg></button>'
            + '</div>';
    });
    listEl.innerHTML = html;

    listEl.querySelectorAll('.batch-remove').forEach(function (btn) {
        btn.addEventListener('click', function () {
            if (batchRunning) return;
            batchRemoveItem(parseInt(btn.dataset.id, 10));
        });
    });

    batchRefreshCount();
}

function batchAddFiles(files) {
    var added = 0;
    files.forEach(function (file) {
        if (!file.type || file.type.indexOf('image/') !== 0) return;
        batchSeq++;
        batchQueue.push({
            id: batchSeq,
            file: file,
            url: URL.createObjectURL(file),
            name: file.name,
            size: formatFileSize(file.size),
            state: 'idle',
            label: '等待'
        });
        added++;
    });
    if (added === 0 && files.length > 0) {
        showMessage('请选择图片文件', 'error');
        return;
    }
    batchRenderList();
}

function batchRemoveItem(id) {
    var index = -1;
    for (var i = 0; i < batchQueue.length; i++) {
        if (batchQueue[i].id === id) { index = i; break; }
    }
    if (index < 0) return;
    URL.revokeObjectURL(batchQueue[index].url);
    batchQueue.splice(index, 1);
    batchRenderList();
}

/* ---------- 转换：与 frame.js 保持同一套几何与量化流程 ---------- */

function batchConvertImage(img) {
    var cw = getCanvasWidth();
    var ch = getCanvasHeight();

    var canvas = document.createElement('canvas');
    canvas.width = cw;
    canvas.height = ch;
    var ctx = canvas.getContext('2d');

    // 竖屏设备（Max）：横图旋转 90°，竖图直接显示；横向设备：竖图旋转
    var rotation = isPortraitDevice()
        ? (img.width > img.height ? 1 : 0)
        : (img.height > img.width ? 1 : 0);

    var effWidth = rotation ? ch : cw;
    var effHeight = rotation ? cw : ch;

    ctx.save();
    if (rotation) {
        ctx.translate(0, ch);
        ctx.rotate(-Math.PI / 2);
    }

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, effWidth, effHeight);

    var scale = Math.min(effWidth / img.width, effHeight / img.height);
    var drawWidth = img.width * scale;
    var drawHeight = img.height * scale;
    ctx.drawImage(img, (effWidth - drawWidth) / 2, (effHeight - drawHeight) / 2, drawWidth, drawHeight);
    ctx.restore();

    // 先出预览效果（对比度 → 量化 → 打包 → 还原），再取像素打包成 film
    var imageData = ctx.getImageData(0, 0, cw, ch);
    adjustContrast(imageData, 1.2);
    var previewData = decodeProcessedData(processImageData(atkinsonEnhancedQuantize(imageData)), cw, ch);
    ctx.putImageData(previewData, 0, 0);

    var pixelData = processImageData(ctx.getImageData(0, 0, cw, ch));
    var header = generateFilmHeader();
    var totalSize = getFilmFileTotalSize();
    if (pixelData.length !== totalSize - 32) {
        throw new Error('像素数据大小不符（' + pixelData.length + '，应为 ' + (totalSize - 32) + '）');
    }

    var fileData = new Uint8Array(totalSize);
    fileData.set(header, 0);
    fileData.set(pixelData, 32);
    return fileData;
}

function batchBuildFilmData(file) {
    return new Promise(function (resolve, reject) {
        var reader = new FileReader();
        reader.onerror = function () { reject(new Error('读取文件失败')); };
        reader.onload = function (ev) {
            var img = new Image();
            img.onerror = function () { reject(new Error('图片解析失败')); };
            img.onload = function () {
                try {
                    resolve(batchConvertImage(img));
                } catch (err) {
                    reject(err);
                }
            };
            img.src = ev.target.result;
        };
        reader.readAsDataURL(file);
    });
}

/* ---------- 传输：与 frameUploadViaBle 同一序列，但把失败抛出来 ---------- */

async function batchUploadFilm(fileData, fileName, indexLabel) {
    var expectedSize = getFilmFileTotalSize();
    if (fileData.length !== expectedSize) {
        throw new Error('文件大小不符合要求(应为' + expectedSize + '字节)');
    }

    var container = document.getElementById(BATCH_TRANSFER_PREFIX + 'container');
    if (container) container.style.display = 'block';

    batchUpdateProgress(indexLabel + ' · 准备传输...', 0);

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
        batchUpdateProgress(indexLabel + ' · ' + fileName, Math.round((sentBytes / fileData.length) * 100));
    }

    // 静默结束：设备保存但不自动加载显示，避免传 N 张刷 N 次屏（与小程序批量一致）
    await sendBleFileStop(true);
    filmTransState = BLE_FILM_TRANS_STATE_IDLE;
}

/* ---------- 收尾：让设备显示最后一张 + 刷新文件列表 ---------- */

function batchFindFileByName(name) {
    var list = window.fileListBuffer || [];
    for (var i = 0; i < list.length; i++) {
        if (list[i].filename === name) return list[i];
    }
    return null;
}

// 请求文件列表 → 按文件名找到 fileId → 发送 0x07 切图 → 0x08 查询显示状态
// 文件列表回包是异步逐条推来的，所以轮询 buffer 而不是等 sendBleFileList 的 Promise
async function batchDisplayLastOnDevice(name) {
    if (!name) return false;

    window.fileListBuffer = [];
    try {
        await sendBleFileList();
    } catch (err) {
        return false;
    }

    var found = null;
    for (var attempt = 0; attempt < 6 && !found; attempt++) {
        await delay(250);
        found = batchFindFileByName(name);
    }
    if (!found) return false;

    try {
        await sendBleFileDisplay(found.id);
        await sendBleFileDisplayGet();
        return true;
    } catch (err) {
        return false;
    }
}

/* ---------- 文件名 ---------- */

function batchFilmName(prefix, index, originalName) {
    var base = prefix
        ? prefix + (index < 10 ? '0' + index : '' + index)
        : originalName.replace(/\.[^.\/\\]+$/, '');
    return normalizeFilmFileName(base, 'batch.film');
}

/* ---------- 主流程 ---------- */

function batchShowResult(text) {
    var el = document.getElementById('frame-batch-result');
    if (!el) return;
    el.textContent = text;
    el.style.display = 'block';
}

async function batchStart() {
    if (batchRunning) return;

    if (typeof device === 'undefined' || !device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    var pending = batchQueue.filter(function (item) { return item.state !== 'done'; });
    if (pending.length === 0) {
        showMessage('没有待上传的照片', 'error');
        return;
    }

    var prefixInput = document.getElementById('batchPrefix');
    var prefix = prefixInput ? prefixInput.value.trim() : '';
    var resultEl = document.getElementById('frame-batch-result');
    if (resultEl) resultEl.style.display = 'none';

    batchRunning = true;
    batchRefreshCount();

    var done = 0;
    var failed = null;
    var lastUploadedName = null;

    for (var i = 0; i < pending.length; i++) {
        var item = pending[i];
        var order = batchQueue.indexOf(item) + 1;
        var label = '第 ' + (i + 1) + '/' + pending.length + ' 张';
        var fileName = batchFilmName(prefix, order, item.name);

        try {
            batchSetState(item, 'wait', '转换中…');
            var fileData = await batchBuildFilmData(item.file);

            batchSetState(item, 'busy', '发送中…');
            await batchUploadFilm(fileData, fileName, label);

            batchSetState(item, 'done', '已完成');
            lastUploadedName = fileName;
            done++;
        } catch (err) {
            batchSetState(item, 'fail', '失败');
            failed = item.name + '：' + err.message;
            showMessage('上传失败 ' + failed, 'error');
            break;
        }

        // 给设备状态机留出复位时间
        if (i < pending.length - 1) {
            await delay(400);
        }
    }

    if (failed) {
        batchRunning = false;
        batchRefreshCount();
        batchUpdateProgress('批量上传中断', 0);
        batchShowResult('已完成 ' + done + ' 张，' + failed + ' 上传失败，已停止后续上传');
        showMessage('批量上传中断', 'error');
        return;
    }

    // 收尾：刷新设备文件列表，并让设备切到最后一张（批量期间是静默保存的）
    batchUpdateProgress('正在刷新设备文件列表...', 100);
    var shown = await batchDisplayLastOnDevice(lastUploadedName);

    batchRunning = false;
    batchRefreshCount();
    batchUpdateProgress('批量上传完成', 100);
    batchShowResult(done + ' 张已全部写入设备' + (shown
        ? '，设备已切到最后一张'
        : '，但未能自动切换显示，可到设置页手动选择显示'));
    showMessage('批量上传完成', 'success');
}

/* ---------- 初始化 ---------- */

function initBatchPage() {
    var input = document.getElementById('batchFileInput');
    var selectBtn = document.getElementById('batchSelectBtn');
    var startBtn = document.getElementById('batchStartBtn');
    if (!input || !selectBtn || !startBtn) return;

    selectBtn.addEventListener('click', function () {
        if (batchRunning) return;
        input.click();
    });

    input.addEventListener('change', function (e) {
        var files = Array.prototype.slice.call(e.target.files || []);
        e.target.value = '';
        if (files.length === 0) return;
        batchAddFiles(files);
    });

    startBtn.addEventListener('click', batchStart);

    batchRenderList();
}

window.addEventListener('DOMContentLoaded', initBatchPage);
