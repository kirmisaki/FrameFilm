/* ============================================================================
 * 串口烧录（固件）
 *
 * 用 Web Serial + 内嵌的 esptool-js（js/esptool-js.js，挂在 window.ESPTOOL）
 * 把 .bin 直接写进设备 Flash —— 等价于命令行：
 *   esptool.py write_flash 0x0 bootloader.bin 0x8000 partition-table.bin \
 *                         0x29000 ota_data_initial.bin 0x30000 <app>.bin
 *
 * 相比 BLE OTA：快得多，而且能刷 bootloader / 分区表（OTA 只能写 app 分区）。
 *
 * 偏移按文件名自动匹配（与本仓库 partitions.csv 的前段布局一致）：
 *   bootloader*  -> 0x0
 *   partition*   -> 0x8000
 *   ota_data*    -> 0x29000
 *   其它（app）  -> 0x30000
 * 勾选“合并镜像”时只允许选 1 个文件，从 0x0 整片写入。
 *
 * 只在浏览器侧操作串口，不参与 BLE 协议；烧完设备会复位，需重新连接蓝牙。
 * ========================================================================== */

/* 分区偏移（与本仓库各固件的 partitions.csv 前段布局一致） */
const FW_ADDR_BOOTLOADER = 0x0;
const FW_ADDR_PARTITION  = 0x8000;
const FW_ADDR_OTA_DATA   = 0x29000;
const FW_ADDR_APP        = 0x30000;

/* 文件名 -> 偏移 的匹配规则（顺序敏感） */
const FW_ADDR_RULES = [
    { test: /bootloader/i, addr: FW_ADDR_BOOTLOADER },
    { test: /partition/i,  addr: FW_ADDR_PARTITION },
    { test: /ota_data/i,   addr: FW_ADDR_OTA_DATA }
];

let fwFiles = [];       // [{ file, name, size }]
let fwBusy = false;     // 正在烧写：禁止重入
let fwMergedDetected = null;   // 合并镜像自动判定结果：null=未判定，true/false=已判定
let fwOnlineCandidate = null;
let fwOnlineImport = false;

/* ---------------------------------------------------------------- 环境/工具 */

function serialFlashSupported() {
    return !!(navigator.serial && navigator.serial.requestPort);
}

function fwKernelReady() {
    return !!(window.ESPTOOL && window.ESPTOOL.ESPLoader && window.ESPTOOL.Transport);
}

function fwFormatBytes(n) {
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / 1024 / 1024).toFixed(2) + ' MB';
}

function fwLog(text) {
    const box = document.getElementById('fw-log');
    if (!box) return;
    const now = new Date();
    const ts = ('0' + now.getHours()).slice(-2) + ':' +
               ('0' + now.getMinutes()).slice(-2) + ':' +
               ('0' + now.getSeconds()).slice(-2);
    box.style.display = 'block';
    box.textContent += '[' + ts + '] ' + text + '\n';
    box.scrollTop = box.scrollHeight;
}

function fwSetStatus(text) {
    const el = document.getElementById('fw-transfer-status');
    if (el) el.textContent = text;
}

/** 传输区（状态 + 进度条）默认隐藏，开始动作后再显示 */
function fwShowTransfer(show) {
    const el = document.getElementById('fw-transfer-container');
    if (el) el.style.display = show ? 'block' : 'none';
}

function fwSetProgress(pct) {
    const bar = document.getElementById('fw-transfer-progress-bar');
    const txt = document.getElementById('fw-transfer-progress');
    if (bar) bar.style.width = Math.max(0, Math.min(100, pct)) + '%';
    if (txt) txt.textContent = pct + '%';
}

function fwSetControlsEnabled(enabled) {
    ['fw-source', 'fw-select-btn', 'fw-clear-btn', 'fw-online-family', 'fw-online-target', 'fw-online-screen',
     'fw-online-channel', 'fw-online-import'].forEach(function (id) {
        const el = document.getElementById(id);
        if (el) el.disabled = !enabled;
    });
    const canFlash = enabled && serialFlashSupported() && fwKernelReady();
    ['fw-detect-btn', 'fw-flash-btn', 'fw-erase-all', 'fw-baud'].forEach(function (id) {
        const el = document.getElementById(id);
        if (el) el.disabled = !canFlash;
    });
    const merged = document.getElementById('fw-merged-image');
    if (merged) merged.disabled = !enabled || fwFiles.some(item => item.onlineFlash);
}

/* ------------------------------------------------------------------ 文件管理 */

/**
 * 判断一个 .bin 是不是"合并镜像"（bootloader + 分区表 + ota_data + 应用的整片镜像）。
 *
 * 依据**内容**而非文件名，两条同时成立才算：
 *   1. 偏移 0 处是 ESP 镜像魔数 0xE9（bootloader 与普通应用镜像都有，只是必要条件）；
 *   2. 偏移 0x8000 处是分区表魔数 0xAA 0x50（ESP-IDF 固定把分区表放在 0x8000）。
 * 普通 app.bin 不会恰好在 0x8000 处带分区表，bootloader.bin 又短于 0x8000，
 * 所以这个判据既不会误判、也不会漏判。
 */
function fwLooksLikeMergedImage(bytes) {
    if (!bytes || bytes.length < 0x8002) return false;
    if (bytes[0] !== 0xe9) return false;
    return bytes[0x8000] === 0xaa && bytes[0x8001] === 0x50;
}

/**
 * 按文件名推断写入偏移。
 * 单文件 + 勾选“合并镜像”时交给调用方置 0x0。
 */
function fwResolveAddress(name) {
    for (let i = 0; i < FW_ADDR_RULES.length; i++) {
        if (FW_ADDR_RULES[i].test.test(name)) return FW_ADDR_RULES[i].addr;
    }
    return FW_ADDR_APP;
}

function fwMergedImageChecked() {
    const el = document.getElementById('fw-merged-image');
    return !!(el && el.checked);
}

/** 选中文件真正使用的偏移（考虑“合并镜像”） */
function fwAddressOf(item) {
    if (item.onlineFlash) return FW_ADDR_BOOTLOADER;
    if (fwMergedImageChecked() && fwFiles.length === 1) return 0x0;
    return item.address;
}

function fwSyncSource() {
    const online = document.getElementById('fw-source').value === 'online';
    document.getElementById('fw-online-options').hidden = !online;
    document.getElementById('fw-local-controls').hidden = online;
    document.getElementById('fw-file-list').hidden = online;
    document.getElementById('fw-local-merged').hidden = online;
}

function selectFirmwareFiles(online = false) {
    if (fwBusy) return;
    fwOnlineImport = online;
    const input = document.getElementById('fw-file-input');
    if (input) input.click();
}

/** 只读文件头（0x8002 字节）判断是否合并镜像，不整份读入 */
function fwPeekMergedImage(file) {
    if (!file || file.size < 0x8002) return Promise.resolve(false);
    return file.slice(0, 0x8002).arrayBuffer().then(function (buf) {
        return fwLooksLikeMergedImage(new Uint8Array(buf));
    });
}

async function handleFirmwareFiles(event) {
    /* 先复制 FileList 再清空 input：清空会把同一个 FileList 对象一起清掉 */
    const picked = Array.prototype.slice.call(event.target.files || []);
    event.target.value = '';

    if (!picked.length || fwBusy) return;

    const online = fwOnlineImport;
    fwOnlineImport = false;
    if (online) {
        if (!fwOnlineCandidate || picked.length !== 1) {
            fwOnlineStatus('请选择刚下载的一个完整镜像文件。');
            return;
        }
        fwBusy = true;
        fwSetControlsEnabled(false);
        try {
            const item = fwOnlineCandidate;
            if (picked[0].size !== item.size) throw new Error('文件大小与发布附件不一致');
            await fwStageOnline(new Uint8Array(await picked[0].arrayBuffer()), item);
        } catch (error) {
            fwOnlineStatus('导入失败：' + error.message);
        } finally {
            fwBusy = false;
            fwSetControlsEnabled(true);
        }
        return;
    }

    const bad = picked.filter(function (f) { return !/\.bin$/i.test(f.name); });
    if (bad.length) {
        showMessage('只支持 .bin 固件文件：' + bad.map(function (f) { return f.name; }).join('、'), 'error');
        return;
    }

    fwFiles = picked.map(function (f) {
        return { file: f, name: f.name, size: f.size, address: fwResolveAddress(f.name) };
    });
    fwSetControlsEnabled(true);

    fwRenderFileList();

    /* 合并镜像按内容自动判定（多文件必然不是合并镜像），省得用户去认文件名 */
    const cb = document.getElementById('fw-merged-image');
    fwMergedDetected = null;

    if (fwFiles.length === 1) {
        const file = fwFiles[0].file;
        fwPeekMergedImage(file).then(function (merged) {
            if (fwFiles.length !== 1 || fwFiles[0].file !== file) return;
            fwMergedDetected = merged;
            if (cb) cb.checked = merged;
            fwLog(merged
                ? '检测到合并镜像：0x8000 处有分区表魔数 → 从 0x0 整片写入'
                : '普通固件：0x8000 处没有分区表，按文件名规则配偏移');
            fwRenderFileList();
        }).catch(function () {
            if (fwFiles.length !== 1 || fwFiles[0].file !== file) return;
            fwMergedDetected = null;
            fwRenderFileList();
        });
    } else {
        fwMergedDetected = false;
        if (cb) cb.checked = false;
    }
}

/** “合并镜像”开关变更：与自动判定结果冲突时提醒（写错偏移会覆盖 bootloader） */
function fwOnMergedToggle() {
    const cb = document.getElementById('fw-merged-image');
    if (cb && cb.checked && fwFiles.length === 1 && fwMergedDetected === false) {
        showMessage('该文件看起来不是合并镜像（0x8000 处没有分区表），从 0x0 写会覆盖 bootloader，请确认', 'warning');
    }
    fwRenderFileList();
}

function clearFirmwareFiles() {
    if (fwBusy) return;
    fwFiles = [];
    fwMergedDetected = null;
    document.getElementById('fw-merged-image').checked = false;
    fwSetControlsEnabled(true);
    fwRenderFileList();
    const box = document.getElementById('fw-log');
    if (box) { box.textContent = ''; box.style.display = 'none'; }
    fwSetStatus('准备…');
    fwSetProgress(0);
}

function fwOnlineStatus(message) {
    document.getElementById('fw-online-status').textContent = message;
}

function resetOnlineFlash() {
    fwOnlineCandidate = null;
    fwOnlineImport = false;
    const link = document.getElementById('fw-online-link');
    link.hidden = true;
    link.removeAttribute('href');
    document.getElementById('fw-online-import').hidden = true;
    if (fwFiles.some(item => item.onlineFlash)) clearFirmwareFiles();
    fwOnlineStatus('');
}

/** 当前选择的设备家族：Ark 通行证版 / FrameFilm 经典 Pro */
function fwOnlineFamily() {
    const el = document.getElementById('fw-online-family');
    return el && el.value === 'classic' ? 'classic' : 'ark';
}

/** 按设备家族重建硬件规格下拉；经典固件再显示屏幕面板 */
function fwSyncOnlineOptions() {
    const family = fwOnlineFamily();
    const target = document.getElementById('fw-online-target');
    if (!target) return;
    const previous = target.value;
    const options = family === 'classic'
        ? ClassicFirmware.variants.map(item => ({ value: item.variant, label: item.label }))
        : ArkFirmware.targets.map(item => ({ value: item.targetId, label: item.label }));
    target.innerHTML = '';
    options.forEach(function (item) {
        const option = document.createElement('option');
        option.value = item.value;
        option.textContent = item.label;
        target.appendChild(option);
    });
    if (options.some(item => item.value === previous)) target.value = previous;

    const screen = document.getElementById('fw-online-screen');
    if (screen && !screen.options.length) {
        ClassicFirmware.screens.forEach(function (item) {
            const option = document.createElement('option');
            option.value = item.screen;
            option.textContent = item.label;
            screen.appendChild(option);
        });
        screen.value = 'e6-3.68-792x528';
    }
    const wrap = document.getElementById('fw-online-screen-wrap');
    if (wrap) wrap.hidden = family !== 'classic';
    const channelWrap = document.getElementById('fw-online-channel-wrap');
    if (channelWrap) channelWrap.hidden = family !== 'ark';
    resetOnlineFlash();
}

/** 连接设备后预选家族/屏幕：只能推断型号与面板，Flash/PSRAM 仍由用户确认 */
function fwSyncOnlineDevice() {
    if (fwBusy) return;
    const family = document.getElementById('fw-online-family');
    const type = window.currentDeviceType;
    if (!family || !type) return;
    if (type === 'FRAMEFILMARK') {
        family.value = 'ark';
    } else if (/^FRAMEFILM(?:STD|PRO|MAX)?$/.test(type)) {
        family.value = 'classic';
    } else {
        return;
    }
    fwSyncOnlineOptions();
    const screen = document.getElementById('fw-online-screen');
    const panel = typeof window.currentPanelId === 'number' ? ClassicFirmware.screenOf(window.currentPanelId) : null;
    if (family.value === 'classic' && screen && panel) screen.value = panel.screen;
}

async function fwVerifyOnline(bytes, item) {
    if (item.family === 'classic') await ClassicFirmware.verifyFlash(bytes, item);
    else await ArkFirmware.verifyFlash(bytes, item);
}

async function fwStageOnline(bytes, item) {
    await fwVerifyOnline(bytes, item);
    const file = new File([bytes], item.name);
    fwFiles = [{ file, name: item.name, size: bytes.length, onlineFlash: item }];
    fwMergedDetected = true;
    document.getElementById('fw-merged-image').checked = true;
    document.getElementById('fw-source').value = 'local';
    fwSyncSource();
    fwRenderFileList();
    fwOnlineStatus(item.tag + ' · 完整镜像已校验，写入地址 0x0。');
}

async function fwFetchOnline(hardware, classic, channel) {
    const firmware = classic ? ClassicFirmware : ArkFirmware;
    fwSetStatus('正在查找 ' + hardware.label + ' 的完整镜像…');
    const release = classic ? await firmware.latest() : await firmware.latestFor(channel);
    const item = classic
        ? firmware.selectFlash(release, await firmware.readManifest(release), hardware)
        : firmware.selectFlash(release, hardware);
    fwOnlineCandidate = item;
    fwSetStatus('正在下载 ' + item.tag + ' · ' + item.name + '…');
    let bytes;
    try {
        bytes = await firmware.download(item);
    } catch (error) {
        if (error instanceof firmware.DownloadUnavailable) {
            const link = document.getElementById('fw-online-link');
            link.href = item.url;
            link.hidden = false;
            document.getElementById('fw-online-import').hidden = false;
            fwOnlineStatus(item.tag + ' · 联网下载失败。可以重试，或下载后导入该完整镜像。');
        }
        throw error;
    }
    fwSetStatus('正在校验完整镜像…');
    await fwVerifyOnline(bytes, item);
    fwOnlineStatus(item.tag + ' · ' + item.label + ' · 已校验');
    return { data: bytes, name: item.name, size: bytes.length, onlineFlash: item };
}

function fwRenderFileList() {
    const box = document.getElementById('fw-file-list');
    if (!box) return;

    if (!fwFiles.length) {
        box.innerHTML = '<div class="fw-file-empty">未选择固件（可多选：bootloader / 分区表 / ota_data / 应用）</div>';
        fwSetStatus('准备…');
        fwSetProgress(0);
        return;
    }

    let total = 0;
    let rows = '';
    fwFiles.forEach(function (item) {
        const addr = fwAddressOf(item);
        total += item.size;
        rows += '<div class="fw-file-row">' +
                    '<span class="fw-file-name" title="' + item.name + '">' + item.name + '</span>' +
                    '<span class="fw-file-size">' + fwFormatBytes(item.size) + '</span>' +
                    '<span class="fw-file-addr">0x' + addr.toString(16) + '</span>' +
                '</div>';
    });
    box.innerHTML = rows + '<div class="fw-file-total">共 ' + fwFiles.length + ' 个区 · ' +
        fwFormatBytes(total) + (fwMergedImageChecked() && fwFiles.length === 1 ? ' · 合并镜像 → 0x0' : '') +
        '</div>';
}

/* --------------------------------------------------------------------- MD5 */

/**
 * MD5（RFC 1321）——esptool-js 写完要用它和设备的回读值比对，故必须本地实现。
 * 已用 RFC 1321 全部测试向量 + 与 node crypto 对拍 0~200 字节验证。
 */
function fwMd5(bytes) {
    const S = [7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
               5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
               6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21];
    const K = [];
    for (let i = 0; i < 64; i++) {
        K.push(Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296));
    }

    const len = bytes.length;
    const padded = new Uint8Array((((len + 8) >> 6) + 1) << 6);
    padded.set(bytes);
    padded[len] = 0x80;

    const dv = new DataView(padded.buffer);
    dv.setUint32(padded.length - 8, (len << 3) >>> 0, true);
    dv.setUint32(padded.length - 4, Math.floor(len / 536870912) >>> 0, true);

    let a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;

    for (let off = 0; off < padded.length; off += 64) {
        let A = a0, B = b0, C = c0, D = d0;
        for (let i = 0; i < 64; i++) {
            let F, g;
            if (i < 16) { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) & 15; }
            else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) & 15; }
            else { F = C ^ (B | ~D); g = (7 * i) & 15; }

            const x = (A + F + K[i] + dv.getInt32(off + g * 4, true)) | 0;
            const s = S[i];
            const rot = (x << s) | (x >>> (32 - s));
            const tmp = D;
            D = C; C = B;
            B = (B + rot) | 0;
            A = tmp;
        }
        a0 = (a0 + A) | 0; b0 = (b0 + B) | 0; c0 = (c0 + C) | 0; d0 = (d0 + D) | 0;
    }

    let out = '';
    [a0, b0, c0, d0].forEach(function (v) {
        for (let i = 0; i < 4; i++) {
            out += ('0' + ((v >>> (i * 8)) & 0xff).toString(16)).slice(-2);
        }
    });
    return out;
}

/* --------------------------------------------------------------- 串口连接 */

const FW_ROM_BAUD = 115200;

function fwBaudrate() {
    const el = document.getElementById('fw-baud');
    const v = el ? parseInt(el.value, 10) : 0;
    return v > 0 ? v : 460800;
}

function fwErrorText(error) {
    return error?.message ? (error.name && error.name !== 'Error' ? error.name + ': ' : '') + error.message : String(error);
}

function fwChoosePort() {
    if (!serialFlashSupported()) throw new Error('当前浏览器不支持 Web Serial，请用 Chrome / Edge 桌面版');
    if (!fwKernelReady()) throw new Error('烧录内核未加载（js/esptool-js.js）');
    fwLog('请在弹窗中选择设备的串口（CH343 / CP210x 等 USB 串口）');
    return navigator.serial.requestPort();
}

function fwMakeLoader(port, setStage) {
    const { Transport, ESPLoader } = window.ESPTOOL;
    const conn = { port, flashId: null };
    conn.transport = new class extends Transport {
        async connect(rate, options) {
            if (rate === FW_ROM_BAUD) setStage('打开串口（115200）');
            await super.connect(rate, options);
            if (rate === FW_ROM_BAUD) setStage('进入下载模式 / ROM SYNC');
        }
    }(port, true);
    conn.loader = new class extends ESPLoader {
        identifyChip(...args) {
            setStage('识别芯片');
            return super.identifyChip(...args);
        }
        identifyChipByMagic(...args) {
            setStage('识别芯片');
            return super.identifyChipByMagic(...args);
        }
        runStub() {
            setStage('上传并启动 stub');
            return super.runStub();
        }
        async readFlashId() {
            setStage('读取 Flash ID');
            const id = await super.readFlashId();
            conn.flashId = id;
            return id;
        }
        eraseFlash() {
            setStage('整片擦除 Flash');
            return super.eraseFlash();
        }
        flashDeflBegin(...args) {
            setStage('准备写入 Flash（可能已开始擦除）');
            return super.flashDeflBegin(...args);
        }
        flashMd5sum(...args) {
            setStage('校验 Flash MD5');
            return super.flashMd5sum(...args);
        }
    }({
        transport: conn.transport,
        baudrate: FW_ROM_BAUD,
        romBaudrate: FW_ROM_BAUD,
        terminal: {
            clean() {},
            writeLine(data) { fwLog(String(data)); },
            write(data) { fwLog(String(data)); }
        },
        debugLogging: false
    });
    return conn;
}

function fwBaudProbeError(error) {
    const { ESPError, TimeoutError } = window.ESPTOOL;
    return error instanceof TimeoutError || (error instanceof ESPError &&
        /^(No serial data received|Serial data stream stopped|invalid response)/i.test(error.message));
}

/** esptool-js 0.7.0 的切速步骤。由页面控制回退边界，不再让 main() 隐式重试。 */
async function fwRaiseBaud(conn, rate, setStage) {
    const { loader, transport, port } = conn;
    if (loader.secureDownloadMode) {
        fwLog('安全下载模式不支持切速，使用 115200。');
        return true;
    }
    let phase = '发送切速命令';
    try {
        setStage(phase + '（' + rate + '）');
        const args = new Uint8Array(8);
        const view = new DataView(args.buffer);
        view.setUint32(0, rate, true);
        view.setUint32(4, loader.IS_STUB ? transport.baudrate : 0, true);
        await loader.checkCommand('change baud rate', loader.ESP_CHANGE_BAUDRATE, args);
        await new Promise(resolve => setTimeout(resolve, 50));
        phase = '切换主机串口速率';
        setStage(phase + '（' + rate + '）');
        await transport.changeBaudrate(rate, loader.serialOptions);
        phase = '切速后探活';
        setStage(phase + '（' + rate + '）');
        await transport.drainInput();
        for (let attempt = 0; ; attempt++) {
            try {
                await loader.readReg(loader.CHIP_DETECT_MAGIC_REG_ADDR, 500);
                break;
            } catch (error) {
                if (attempt || !fwBaudProbeError(error)) throw error;
                transport.flushInput();
            }
        }
        loader.baudrate = transport.baudrate;
        return true;
    } catch (error) {
        // Chromium 的高速 open 失败可能是 NetworkError；确认同一个已授权端口仍在，
        // 不把实际拔出、权限错误或关闭阶段失败当成可降速的速率错误。
        const reopenFailed = phase === '切换主机串口速率' && error.name === 'NetworkError' && !port.readable && !port.writable;
        if ((!reopenFailed && !fwBaudProbeError(error)) || port.connected === false ||
            !(await navigator.serial.getPorts()).includes(port)) throw error;
        fwLog(phase + '失败，无法使用 ' + rate + '：' + fwErrorText(error) + '；重新握手一次，改用 115200。');
        return false;
    }
}

/** 固件准备好后才打开串口。所有 ROM 握手均为 115200，只有烧录尝试提速。 */
async function fwConnectLoader(port, rate, setStage) {
    let conn = fwMakeLoader(port, setStage);
    try {
        conn.chip = await conn.loader.main();
        fwLog('检测到芯片：' + conn.chip);
        if (rate !== FW_ROM_BAUD && !(await fwRaiseBaud(conn, rate, setStage))) {
            const { chip, flashId } = conn;
            setStage('降速前关闭串口');
            await fwClosePort(conn);
            conn = fwMakeLoader(port, setStage);
            conn.chip = await conn.loader.main();
            setStage('降速后核对芯片与 Flash');
            if (conn.chip !== chip || conn.flashId !== flashId) {
                throw new Error('降速后识别的芯片或 Flash ID 与初次结果不一致，已停止烧录');
            }
        }
        fwLog('串口已连通；当前速率 ' + conn.transport.baudrate + '。');
        return conn;
    } catch (error) {
        await fwDisconnect(conn);
        throw error;
    }
}

async function fwClosePort(conn) {
    // 端口未打开、已拔出或切速重开失败时，不调用浏览器的 close()。
    if (conn && (conn.port.readable || conn.port.writable)) await conn.transport.disconnect();
}

async function fwDisconnect(conn) {
    try { await fwClosePort(conn); } catch (error) { fwLog('关闭串口：' + fwErrorText(error)); }
}

/**
 * 复位并让芯片从 Flash 启动（等价 esptool 的 hard_reset 经典时序）。
 *
 * 不用 esptool-js 的 loader.after('hard_reset')：它内部的 HardReset 只做
 * `setRTS(false)`（不先拉高 EN），而烧完那一刻 RTS 本来就是低
 * （连接阶段的 classic 时序最后一步是 setSignals(false,false)），
 * 于是这一步是**空动作**——在 CH343 这类“USB 转串口 + 自动复位电路”的板子上
 * 表现为：烧写成功但设备不复位、停在下载模式不启动。
 *
 * 这里自己做完整时序：IO0 拉高（正常启动）→ EN 拉低（复位）→ 释放 EN → 启动。
 */
function fwHardReset(transport) {
    const wait = function (ms) { return new Promise(function (r) { setTimeout(r, ms); }); };
    return Promise.resolve()
        .then(function () { return transport.setDTR(false); })   // IO0=高：正常启动，不进下载模式
        .then(function () { return transport.setRTS(true); })    // EN=低：按住复位
        .then(function () { return wait(120); })
        .then(function () { return transport.setRTS(false); })   // 释放复位 → 芯片从 Flash 启动
        .then(function () { return wait(120); });
}

/* ------------------------------------------------------- 只检测（不写 Flash） */

async function detectSerialDevice() {
    if (fwBusy) return;

    fwBusy = true;
    fwSetControlsEnabled(false);
    const box = document.getElementById('fw-log');
    if (box) { box.textContent = ''; }
    fwLog('=== 串口检测（不写 Flash）===');
    fwShowTransfer(true);
    fwSetProgress(0);

    let conn = null;
    let stage = '选择串口';
    const setStage = text => { stage = text; fwSetStatus(text + '…'); };
    try {
        conn = fwMakeLoader(await fwChoosePort(), setStage);
        await conn.loader.detectChip();
        const chip = conn.loader.chip?.CHIP_NAME;
        if (!chip) throw new Error('未识别出芯片');
        const detected = '串口正常（' + conn.transport.baudrate + '）：' + chip;
        fwLog(detected);
        setStage('检测后复位');
        try {
            await fwHardReset(conn.transport);
            fwLog('检测后复位信号已发送。');
        } catch (err) {
            const msg = detected + '；检测后复位失败：' + fwErrorText(err);
            fwLog(msg);
            fwSetStatus(msg);
            showMessage(msg, 'warning');
            return;
        }
        fwSetStatus(detected);
        showMessage(detected, 'success');
    } catch (err) {
        const msg = '串口检测失败（' + stage + '）：' + fwErrorText(err);
        fwLog(msg);
        fwSetStatus(msg);
        showMessage(msg, 'error');
    } finally {
        await fwDisconnect(conn);
        fwBusy = false;
        fwSetControlsEnabled(true);
    }
}

/* ------------------------------------------------------------------ 烧录 */

function fwConfirmWrite(files, eraseAll) {
    const onlineFlash = files[0].onlineFlash;
    if (onlineFlash && (!onlineFlash.label || !Number.isSafeInteger(onlineFlash.flashBytes) ||
        onlineFlash.flashBytes <= 0)) {
        throw new Error('完整镜像缺少硬件规格，请重新获取固件');
    }
    const plan = files.map(item => '  ' + item.name + ' -> 0x' + fwAddressOf(item).toString(16)).join('\n');
    const totalSize = files.reduce((sum, item) => sum + item.size, 0);
    const riskyMerged = !onlineFlash && fwMergedImageChecked() && files.length === 1 && fwMergedDetected === false;
    return confirm('即将向设备写入固件：\n\n' + plan +
        (onlineFlash ? '\n\n请确认实物为 ' + onlineFlash.label + '，包括 PSRAM 规格。' : '') +
        (onlineFlash || fwMergedImageChecked() ? '\n完整镜像会覆盖镜像范围内的分区，设备设置可能重置。' : '') +
        '\n\n共 ' + fwFormatBytes(totalSize) + (eraseAll ? '（先整片擦除 Flash）' : '') +
        (riskyMerged ? '\n\n注意：该文件在 0x8000 处没有分区表，却按“合并镜像”从 0x0 写入；若不是合并镜像会覆盖 bootloader。' : '') +
        '\n\n期间请勿断开 USB、勿断电。bootloader / 分区表写坏后需要重新烧录才能恢复。\n\n继续？');
}

async function startSerialFlash() {
    if (fwBusy) return;

    if (!serialFlashSupported()) {
        showMessage('当前浏览器不支持 Web Serial，请用 Chrome / Edge 桌面版', 'error');
        return;
    }
    if (!fwKernelReady()) {
        showMessage('烧录内核未加载（js/esptool-js.js）', 'error');
        return;
    }
    const online = document.getElementById('fw-source').value === 'online';
    if (!online && !fwFiles.length) {
        showMessage('请先选择固件文件', 'warning');
        return;
    }
    if (!online && fwMergedImageChecked() && fwFiles.length !== 1) {
        showMessage('“合并镜像”只能选 1 个文件', 'error');
        return;
    }

    const eraseAll = document.getElementById('fw-erase-all').checked;
    const classic = fwOnlineFamily() === 'classic';
    const channel = document.getElementById('fw-online-channel').value;
    const targetId = document.getElementById('fw-online-target').value;
    let hardware;
    if (online) {
        hardware = classic
            ? ClassicFirmware.target(targetId, document.getElementById('fw-online-screen').value)
            : ArkFirmware.target(targetId);
        if (!hardware) {
            showMessage('请按设备实物选择硬件规格' + (classic ? '与屏幕面板' : ''), 'warning');
            return;
        }
        resetOnlineFlash();
    } else {
        try {
            if (!fwConfirmWrite(fwFiles, eraseAll)) return;
        } catch (error) {
            showMessage(error.message, 'error');
            return;
        }
    }

    fwBusy = true;
    fwSetControlsEnabled(false);
    const box = document.getElementById('fw-log');
    if (box) { box.textContent = ''; }
    fwLog('=== 开始烧录 ===');
    fwShowTransfer(true);
    fwSetProgress(0);

    let conn = null;
    let stage = '选择串口';
    let writeStarted = false;
    let writeVerified = false;
    const setStage = text => { stage = text; fwSetStatus(text + '…'); };
    const rate = fwBaudrate();
    try {
        const port = await fwChoosePort();
        if (online) setStage('准备在线固件');
        const files = online ? [await fwFetchOnline(hardware, classic, channel)] : fwFiles;
        if (online && !fwConfirmWrite(files, eraseAll)) {
            fwSetStatus('已取消烧录');
            return;
        }
        fwLog('请求烧录速率：' + rate + '；ROM 握手固定 115200。');
        conn = await fwConnectLoader(port, rate, setStage);

        const onlineFlash = files[0].onlineFlash;
        setStage('核对芯片与固件规格');
        if (onlineFlash) {
            const flashSize = await conn.loader.detectFlashSize();
            setStage('核对芯片与固件规格');
            const expectedSize = (onlineFlash.flashBytes / (1024 * 1024)) + 'MB';
            if (!/ESP32-S3\b/i.test(conn.chip || '') || flashSize !== expectedSize) {
                try { await fwHardReset(conn.transport); }
                catch (err) { fwLog('硬件不匹配，检测后复位失败：' + fwErrorText(err)); }
                throw new Error('串口检测到 ' + conn.chip + ' / ' + (flashSize || 'Flash 未知') +
                    '，与 ' + onlineFlash.label + ' 所需的 ESP32-S3 / ' + expectedSize + ' 不匹配');
            }
        } else if (!/S3/i.test(conn.chip || '')) {
            if (!confirm('检测到的芯片是「' + conn.chip + '」，但本仓库固件都是 ESP32-S3。\n\n确定继续烧写？')) {
                throw new Error('已按用户要求中止（芯片型号不匹配）');
            }
        }

        const fileArray = [];
        for (const item of files) {
            const data = item.data || new Uint8Array(await item.file.arrayBuffer());
            fwLog('载入 ' + item.name + '（' + fwFormatBytes(data.length) + '）-> 0x' +
                  fwAddressOf(item).toString(16));
            fileArray.push({ data: data, address: fwAddressOf(item) });
        }

        setStage('准备擦写 Flash');
        writeStarted = true;
        await conn.loader.writeFlash({
            fileArray: fileArray,
            flashMode: 'keep',
            flashFreq: 'keep',
            flashSize: 'keep',
            eraseAll: eraseAll,
            compress: true,
            reportProgress: function (fileIndex, written, total) {
                const pct = total ? Math.round(written * 100 / total) : 0;
                const name = files[fileIndex]?.name || ('区 ' + (fileIndex + 1));
                fwSetProgress(pct);
                stage = '写入 Flash（' + name + '）';
                fwSetStatus('写入 ' + name + ' … ' + pct + '%');
            },
            calculateMD5Hash: fwMd5
        });

        writeVerified = true;
        fwSetProgress(100);
        fwLog('写入完成（每个区均已按 MD5 回读校验）');

        setStage('写入完成，正在复位设备');
        fwLog('发送复位时序（EN 拉低 → 释放）…');
        await fwHardReset(conn.transport);
        fwLog('复位已发出，设备应从新固件启动；若没有反应，按一下 RESET 键或重新上电即可');
        fwSetStatus('完成');
        showMessage('固件烧录完成，复位信号已发送', 'success');
    } catch (err) {
        const msg = writeVerified ? '固件已写入并校验，但复位失败：' + fwErrorText(err) + '；可手动复位。' :
            '烧录失败（' + stage + '）：' + fwErrorText(err) + (writeStarted ? '；未自动重试。' : '');
        fwLog(msg);
        fwSetStatus(msg);
        showMessage(msg, writeVerified ? 'warning' : 'error');
    } finally {
        await fwDisconnect(conn);
        fwBusy = false;
        fwSetControlsEnabled(true);
    }
}

/* ------------------------------------------------------------------ 初始化 */

(function fwInit() {
    const ready = fwKernelReady();
    const supported = serialFlashSupported();

    const hint = document.getElementById('fw-env-hint');
    if (hint) {
        if (!supported) {
            hint.textContent = '当前浏览器不支持 Web Serial（请用 Chrome / Edge 桌面版），此功能不可用。';
        } else if (!ready) {
            hint.textContent = '烧录内核未加载：缺少 js/esptool-js.js，此功能不可用。';
        } else {
            hint.textContent = '内核已就绪：串口检测固定 115200；烧录可在握手成功后提速。';
        }
    }
    fwSetControlsEnabled(true);
    fwRenderFileList();
    fwSyncOnlineOptions();
    fwSyncSource();
})();
