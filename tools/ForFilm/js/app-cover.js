// ===== App 封面推送（封面菜单测试用） =====
//
// 把仓库 assets/app/<app>/cover.film 写入设备 /sdcard/app/<app>/cover.film。
// 复用标准 film 文件传输（FILE_START → FILE_NAME → FILE_LEN → FILE_DATA → FILE_STOP）：
// FILE_NAME 含 '/' 时固件按“相对 /sdcard 的显式路径”写入（固件 v1.6+），
// 不参与图片/动图列表刷新、不自动显示，因此不会打断设备当前画面。
//
// 封面为 MonoFast 720×480 单帧（32B 头 + 43200B 位图），仅 3.7"（面板 0x02）屏可渲染。

const APP_COVER_SOURCE_DIR = '../../assets/app/';
const APP_COVER_FILE_NAME = 'cover.film';
const APP_COVER_MONO_SIZE = 32 + (720 * 480) / 8; // 43232

const APP_COVER_LIST = [
    { name: 'image', label: '图片' },
    { name: 'template', label: '模板' },
    { name: 'clock', label: '时钟' },
    { name: 'animation', label: '动图' }
];

let appCoverUploading = false;

function initAppCover() {
    syncAppCoverAvailability();
}

// App 封面属于冰箱贴（三机型）能力，Dock 底座不支持
function syncAppCoverAvailability() {
    const section = document.getElementById('app-cover-section');
    if (section) {
        section.style.display = (currentDeviceType === 'FRAMEFILMDOCK') ? 'none' : '';
    }
    updateAppCoverDeviceHint();
}

function updateAppCoverDeviceHint() {
    const el = document.getElementById('app-cover-device-hint');
    if (!el) {
        return;
    }

    if (!characteristic) {
        el.textContent = '未连接设备：请先通过蓝牙或 USB 连接冰箱贴';
        return;
    }

    const cfg = getDeviceConfig();
    if (cfg.screenWidth === 720 && cfg.screenHeight === 480) {
        el.textContent = '当前屏幕 720×480（3.7"）：封面可在设备封面菜单中正常显示';
    } else {
        el.textContent = '当前屏幕 ' + cfg.screenWidth + '×' + cfg.screenHeight +
            '：封面仅 3.7"（720×480）屏可显示，上传仍会写入设备';
    }
}

function appCoverSetStatus(text, percent) {
    const container = document.getElementById('app-cover-transfer-container');
    const statusEl = document.getElementById('app-cover-transfer-status');
    const barEl = document.getElementById('app-cover-transfer-progress-bar');
    const progressEl = document.getElementById('app-cover-transfer-progress');

    if (container) {
        container.style.display = 'block';
    }
    if (statusEl) {
        statusEl.textContent = text;
    }
    if (barEl) {
        barEl.style.width = percent + '%';
    }
    if (progressEl) {
        progressEl.textContent = Math.round(percent) + '%';
    }
}

function appCoverSetResult(text) {
    const el = document.getElementById('app-cover-result');
    if (el) {
        el.textContent = text;
    }
}

function appCoverSetButtonsDisabled(disabled) {
    const section = document.getElementById('app-cover-section');
    if (!section) {
        return;
    }
    const buttons = section.querySelectorAll('button[data-app-cover], #app-cover-all-btn');
    for (let i = 0; i < buttons.length; i++) {
        buttons[i].disabled = disabled;
    }
}

// base64 → 字节数组（解码内联封面）
function appCoverBase64ToBytes(b64) {
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) {
        out[i] = bin.charCodeAt(i);
    }
    return out;
}

// 读取封面 film：优先用内联数据（js/app-cover-data.js，file:// 直接打开也能用），
// 无内联数据时回退到 fetch（要求以仓库根为服务根打开页面）
async function loadAppCoverFilm(name) {
    const b64 = (typeof APP_COVER_DATA !== 'undefined' && APP_COVER_DATA[name]) ? APP_COVER_DATA[name] : '';
    const data = b64 ? appCoverBase64ToBytes(b64) : await fetchAppCoverFilm(name);

    if (data.length < 32) {
        throw new Error('封面文件异常：仅 ' + data.length + ' 字节');
    }
    if (data.length !== APP_COVER_MONO_SIZE) {
        // 尺寸不符不阻断（设备侧按文件头 Format/FrameCount 自行校验），仅记录
        debugLog('封面 ' + name + ' 大小 ' + data.length + ' 字节（预期 ' + APP_COVER_MONO_SIZE + '）');
    }
    return data;
}

// 回退路径：读取仓库内的封面 film（相对 index.html 定位到仓库根 assets 目录）
async function fetchAppCoverFilm(name) {
    const url = APP_COVER_SOURCE_DIR + name + '/' + APP_COVER_FILE_NAME;
    const resp = await fetch(url);

    if (!resp.ok) {
        throw new Error('读取封面失败(' + resp.status + ')：' + url +
            '（请重新生成 js/app-cover-data.js，或以仓库根为服务根打开页面）');
    }
    return new Uint8Array(await resp.arrayBuffer());
}

// 按显式相对路径推送一个 film 文件（relPath 形如 app/image/cover.film）
async function pushFilmToRelPath(relPath, fileData, onProgress) {
    await sendBleFileStart();
    await sendBleFileName(relPath);
    await sendBleFileLen(fileData.length);

    const chunkSize = BLE_CHUNK_SIZE;
    let sent = 0;
    for (let i = 0; i < fileData.length; i += chunkSize) {
        const chunk = fileData.slice(i, i + chunkSize);
        await sendBleFileData(chunk);
        sent += chunk.length;
        if (onProgress) {
            onProgress(sent, fileData.length);
        }
    }

    // 静默 flag：显式路径保存本就不参与自动显示，这里再明确一次
    await sendBleFileStop(true);
}

async function uploadSingleAppCover(item, index, total) {
    const relPath = 'app/' + item.name + '/' + APP_COVER_FILE_NAME;
    const fileData = await loadAppCoverFilm(item.name);

    await pushFilmToRelPath(relPath, fileData, function (sent, size) {
        const overall = ((index + sent / size) / total) * 100;
        appCoverSetStatus('上传 ' + item.label + '（' + (index + 1) + '/' + total + '）：' +
            sent + '/' + size + ' 字节', overall);
    });

    debugLog('封面已写入 /sdcard/' + relPath + '（' + fileData.length + ' 字节）');
}

// 上传单个 app 的封面
async function uploadAppCover(name) {
    const item = APP_COVER_LIST.find(function (it) {
        return it.name === name;
    });
    if (!item) {
        return;
    }

    if (appCoverUploading) {
        showMessage('正在上传，请稍候', 'error');
        return;
    }
    if (!characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    appCoverUploading = true;
    appCoverSetButtonsDisabled(true);
    appCoverSetResult('');
    updateAppCoverDeviceHint();

    try {
        appCoverSetStatus('上传 ' + item.label + '（1/1）...', 0);
        await uploadSingleAppCover(item, 0, 1);
        appCoverSetStatus(item.label + ' 封面上传完成', 100);
        appCoverSetResult('已写入：/sdcard/app/' + item.name + '/' + APP_COVER_FILE_NAME);
        showMessage(item.label + ' 封面上传成功', 'success');
    } catch (error) {
        console.error('[ForFilm] 封面上传失败:', error);
        appCoverSetStatus('上传失败', 0);
        appCoverSetResult('失败：' + error.message);
        showMessage('封面上传失败: ' + error.message, 'error');
    } finally {
        appCoverUploading = false;
        appCoverSetButtonsDisabled(false);
    }
}

// 依次上传 4 个 app 封面
async function uploadAllAppCovers() {
    if (appCoverUploading) {
        showMessage('正在上传，请稍候', 'error');
        return;
    }
    if (!characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    appCoverUploading = true;
    appCoverSetButtonsDisabled(true);
    appCoverSetResult('');
    updateAppCoverDeviceHint();

    const total = APP_COVER_LIST.length;
    const failed = [];

    try {
        for (let i = 0; i < total; i++) {
            const item = APP_COVER_LIST[i];
            try {
                await uploadSingleAppCover(item, i, total);
            } catch (error) {
                console.error('[ForFilm] 封面 ' + item.name + ' 上传失败:', error);
                failed.push(item.label + '（' + error.message + '）');
            }
        }

        if (failed.length === 0) {
            appCoverSetStatus('全部封面已上传（' + total + ' 个）', 100);
            appCoverSetResult('已写入 /sdcard/app/{' + APP_COVER_LIST.map(function (it) {
                return it.name;
            }).join(', ') + '}/' + APP_COVER_FILE_NAME);
            showMessage(total + ' 个 app 封面已全部上传', 'success');
        } else {
            appCoverSetStatus('上传结束，失败 ' + failed.length + ' 个', 100);
            appCoverSetResult('失败：' + failed.join('；'));
            showMessage('部分封面上传失败：' + failed.length + ' 个', 'error');
        }
    } finally {
        appCoverUploading = false;
        appCoverSetButtonsDisabled(false);
    }
}
