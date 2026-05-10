// 蓝牙功能实现
let device = null;
let server = null;
let service = null;
let characteristic = null;

function debugLog(message, type = 'info') {
    console.log(message);
    const debugEl = document.getElementById('debug-log');
    if (debugEl) {
        const entry = document.createElement('div');
        entry.className = 'log-entry ' + type;
        entry.textContent = new Date().toLocaleTimeString() + ' ' + message;
        debugEl.appendChild(entry);
        debugEl.scrollTop = debugEl.scrollHeight;
    }
}

function toggleDebugLog() {
    const debugEl = document.getElementById('debug-log');
    if (debugEl) {
        debugEl.classList.toggle('show');
    }
}

const BLE_SERVICE_UUID = '00002000-0000-1000-8000-00805f9b34fb';
const BLE_CHARACTERISTIC_UUID = '00002001-0000-1000-8000-00805f9b34fb';

const BLE_CMD_HEAD = 0x55;

const BLE_FILM_TRANS_CH_FILE_START = 0x03;
const BLE_FILM_TRANS_CH_FILE_NAME = 0x00;
const BLE_FILM_TRANS_CH_FILE_LEN = 0x01;
const BLE_FILM_TRANS_CH_FILE_DATA = 0x02;
const BLE_FILM_TRANS_CH_FILE_STOP = 0x04;
const BLE_FILM_TRANS_CH_FILE_DELETE = 0x05;
const BLE_FILM_TRANS_CH_FILE_LIST = 0x06;
const BLE_FILM_TRANS_CH_FILE_DISPLAY = 0x07;
const BLE_FILM_TRANS_CH_FILE_DISPLAY_GET = 0x08;

const BLE_FILM_TRANS_CH_OTA_LEN = 0x10;
const BLE_FILM_TRANS_CH_OTA_DATA = 0x11;
const BLE_FILM_TRANS_CH_OTA_START = 0x12;
const BLE_FILM_TRANS_CH_OTA_STOP = 0x13;

const BLE_FILM_TRANS_CH_CTRL_MODE = 0x20;
const BLE_FILM_TRANS_CH_CTRL_MODE_GET = 0x21;
const BLE_FILM_TRANS_CH_CTRL_RESET = 0x22;
const BLE_FILM_TRANS_CH_CTRL_PWRREAD = 0x23;
const BLE_FILM_TRANS_CH_CTRL_REBOOT = 0x24;
const BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF = 0x25;
const BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET = 0x26;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE = 0x27;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET = 0x28;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME = 0x29;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET = 0x2A;

const BLE_CMD_LEN_MIN = 4;

const BLE_FILM_TRANS_STATE_IDLE = 0;
const BLE_FILM_TRANS_STATE_STARTED = 1;
const BLE_FILM_TRANS_STATE_RECV_NAME = 2;
const BLE_FILM_TRANS_STATE_RECV_LEN = 3;
const BLE_FILM_TRANS_STATE_RECV_DATA = 4;
const BLE_FILM_TRANS_STATE_STOPPED = 5;

const BLE_OTA_TRANS_STATE_IDLE = 0;
const BLE_OTA_TRANS_STATE_STARTED = 1;
const BLE_OTA_TRANS_STATE_RECV_LEN = 2;
const BLE_OTA_TRANS_STATE_RECV_DATA = 3;
const BLE_OTA_TRANS_STATE_STOPPED = 4;

const BLE_CHUNK_SIZE = 192;
const BLE_CTRL_DELAY = 50;
const BLE_DATA_DELAY = 5;

let filmTransState = BLE_FILM_TRANS_STATE_IDLE;
let filmTransFileName = '';
let filmTransFileSize = 0;
let filmTransSentBytes = 0;

let otaTransState = BLE_OTA_TRANS_STATE_IDLE;
let otaTransFileSize = 0;
let otaTransSentBytes = 0;

let bleCmdQueue = [];

async function queueBleCmd(fn) {
    bleCmdQueue.push(fn);
    if (bleCmdQueue.length === 1) {
        processQueue();
    }
}

async function processQueue() {
    while (bleCmdQueue.length > 0) {
        const fn = bleCmdQueue[0];
        try {
            await fn();
        } catch (err) {
            console.error('BLE命令错误:', err);
        }
        bleCmdQueue.shift();
    }
}

function initBluetooth() {
    const scanButton = document.getElementById('scan-button');
    if (!scanButton) return;

    scanButton.addEventListener('click', async function() {
        const deviceList = document.getElementById('device-list');
        const status = document.getElementById('connection-status');

        try {
            if (!navigator.bluetooth) {
                status.textContent = '浏览器不支持蓝牙';
                return;
            }

            status.textContent = '正在扫描...';
            deviceList.innerHTML = '<div class="scanning-indicator"><span class="pulse"></span>扫描中...</div>';

            device = await navigator.bluetooth.requestDevice({
                filters: [{ namePrefix: 'FRAMEFILM' }],
                optionalServices: [BLE_SERVICE_UUID]
            });

            if (device.gatt.connected) {
                device.gatt.disconnect();
            }

            server = await device.gatt.connect();
            service = await server.getPrimaryService(BLE_SERVICE_UUID);
            characteristic = await service.getCharacteristic(BLE_CHARACTERISTIC_UUID);

            status.textContent = '已连接';
            status.className = 'status connected';

            deviceList.innerHTML = '<div class="device-item connected-device"><div class="device-info"><strong>' + (device.name || '已连接设备') + '</strong><p class="device-id">' + device.id + '</p></div><button class="disconnect-btn" onclick="disconnectDevice()">断开</button></div>';

            device.addEventListener('gattserverdisconnected', onDisconnected);
            console.log('设备已连接:', device.name);

            setupBluetoothListener();
            window.fileListBuffer = [];
            bleCmdQueue = [];

            setTimeout(() => {
                debugLog('开始发送初始化命令...');
                sendBlePwrRead();
            }, 1000);

            setTimeout(() => {
                sendBleFileList();
            }, 2000);

            setTimeout(() => {
                sendBleFileDisplayGet();
            }, 3000);

        } catch (error) {
            console.error('连接错误:', error);
            status.textContent = '连接失败: ' + error.message;
            status.className = 'status';
            deviceList.innerHTML = '<div class="no-devices">连接已取消或失败</div>';
        }
    });
}

function onDisconnected(event) {
    filmTransState = BLE_FILM_TRANS_STATE_IDLE;
    const status = document.getElementById('connection-status');
    if (status) {
        status.textContent = '设备已断开';
        status.className = 'status';
    }
    const deviceList = document.getElementById('device-list');
    if (deviceList) {
        deviceList.innerHTML = '<div class="no-devices">设备已断开连接</div>';
    }
}

async function disconnectDevice() {
    filmTransState = BLE_FILM_TRANS_STATE_IDLE;
    if (device && device.gatt && device.gatt.connected) {
        device.gatt.disconnect();
        const status = document.getElementById('connection-status');
        if (status) {
            status.textContent = '未连接';
            status.className = 'status';
        }
        const deviceList = document.getElementById('device-list');
        if (deviceList) {
            deviceList.innerHTML = '<div class="no-devices">已断开连接</div>';
        }
    }
}

async function sendDataViaBluetooth(data) {
    if (!device || !server || !characteristic) {
        throw new Error('请先连接设备');
    }
    if (!characteristic.properties.write) {
        throw new Error('特征值不支持写入操作');
    }
    const chunkSize = 512;
    for (let i = 0; i < data.length; i += chunkSize) {
        await characteristic.writeValue(data.slice(i, i + chunkSize));
    }
    return true;
}

function uploadToDevice() {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    if (!window.processedDataForDownload) {
        showMessage('请先转换图片', 'error');
        return;
    }

    const fileName = document.getElementById('fileName').value || 'output.film';
    const fileData = window.processedDataForDownload;

    uploadFilmFileViaBle(fileName, fileData);
}

async function uploadFilmFileViaBle(fileName, fileData) {
    const expectedSize = 600 * 400;

    if (fileData.length !== expectedSize) {
        showMessage('文件大小不符合要求(应为240000字节)', 'error');
        return;
    }

    const transferContainer = document.getElementById('transfer-container');
    if (transferContainer) {
        transferContainer.style.display = 'block';
    }

    try {
        updateTransferStatus('准备传输...', 0);

        filmTransState = BLE_FILM_TRANS_STATE_STARTED;
        filmTransFileName = fileName;
        filmTransFileSize = fileData.length;
        filmTransSentBytes = 0;

        await sendBleFileStart();

        await sendBleFileName(fileName);

        await sendBleFileLen(fileData.length);

        const chunkSize = BLE_CHUNK_SIZE;
        let sentBytes = 0;
        for (let i = 0; i < fileData.length; i += chunkSize) {
            const chunk = fileData.slice(i, i + chunkSize);
            await sendBleFileData(chunk);
            sentBytes += chunk.length;
            filmTransSentBytes = sentBytes;

            const progress = Math.round((sentBytes / fileData.length) * 100);
            updateTransferStatus(`传输中: ${sentBytes}/${fileData.length} 字节`, progress);

            await delay(BLE_DATA_DELAY);
        }

        await sendBleFileStop();

        filmTransState = BLE_FILM_TRANS_STATE_STOPPED;
        updateTransferStatus('传输完成', 100);
        showMessage('文件传输成功!', 'success');

    } catch (error) {
        console.error('传输失败:', error);
        filmTransState = BLE_FILM_TRANS_STATE_IDLE;
        updateTransferStatus('传输失败', 0);
        showMessage('传输失败: ' + error.message, 'error');
    }
}

function calculateChecksum(data, len) {
    let sum = 0;
    for (let i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum & 0xFF;
}

async function sendBleFileStart() {
    const packet = new Uint8Array(4);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_START;
    packet[2] = 0;
    packet[3] = calculateChecksum(packet, 3);

    await characteristic.writeValue(packet);
    console.log('发送 FILE_START');
    await delay(BLE_CTRL_DELAY);
}

async function sendBleFileName(fileName) {
    const nameBytes = new TextEncoder().encode(fileName);
    const packet = new Uint8Array(4 + nameBytes.length);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_NAME;
    packet[2] = nameBytes.length;
    packet.set(nameBytes, 3);
    packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);

    await characteristic.writeValue(packet);
    console.log('发送 FILE_NAME:', fileName);
    await delay(BLE_CTRL_DELAY);
}

async function sendBleFileLen(fileSize) {
    const packet = new Uint8Array(8);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_LEN;
    packet[2] = 4;
    packet[3] = (fileSize >> 24) & 0xFF;
    packet[4] = (fileSize >> 16) & 0xFF;
    packet[5] = (fileSize >> 8) & 0xFF;
    packet[6] = fileSize & 0xFF;
    packet[7] = calculateChecksum(packet, 7);

    await characteristic.writeValue(packet);
    console.log('发送 FILE_LEN:', fileSize);
    await delay(BLE_CTRL_DELAY);
}

async function sendBleFileData(data) {
    const packet = new Uint8Array(4 + data.length);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_DATA;
    packet[2] = data.length;
    packet.set(data, 3);
    packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);

    await characteristic.writeValue(packet);
    await delay(BLE_DATA_DELAY);
}

async function sendBleFileStop() {
    const packet = new Uint8Array(4);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_STOP;
    packet[2] = 0;
    packet[3] = calculateChecksum(packet, 3);

    await characteristic.writeValue(packet);
    console.log('发送 FILE_STOP');
    await delay(BLE_CTRL_DELAY);
}

function updateTransferStatus(message, progress) {
    const statusEl = document.getElementById('transfer-status');
    const progressEl = document.getElementById('transfer-progress');
    const progressBarEl = document.getElementById('transfer-progress-bar');

    if (statusEl) {
        statusEl.textContent = message;
    }

    if (progressBarEl) {
        progressBarEl.style.width = progress + '%';
    }

    if (progressEl) {
        progressEl.textContent = progress + '%';
    }
}

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function checkBluetoothStatus() {
    const status = document.getElementById('connection-status');
    if (status && device && device.gatt && device.gatt.connected) {
        status.textContent = '已连接';
        status.className = 'status connected';
    }
}

setInterval(checkBluetoothStatus, 5000);

async function sendBleOtaStart() {
    const packet = new Uint8Array(4);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_OTA_START;
    packet[2] = 0;
    packet[3] = calculateChecksum(packet, 3);

    await characteristic.writeValue(packet);
    console.log('发送 OTA_START');
    await delay(BLE_CTRL_DELAY);
}

async function sendBleOtaLen(fileSize) {
    const packet = new Uint8Array(8);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_OTA_LEN;
    packet[2] = 4;
    packet[3] = (fileSize >> 24) & 0xFF;
    packet[4] = (fileSize >> 16) & 0xFF;
    packet[5] = (fileSize >> 8) & 0xFF;
    packet[6] = fileSize & 0xFF;
    packet[7] = calculateChecksum(packet, 7);

    await characteristic.writeValue(packet);
    console.log('发送 OTA_LEN:', fileSize);
    await delay(BLE_CTRL_DELAY);
}

async function sendBleOtaData(data) {
    const packet = new Uint8Array(4 + data.length);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_OTA_DATA;
    packet[2] = data.length;
    packet.set(data, 3);
    packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);

    await characteristic.writeValue(packet);
    await delay(BLE_DATA_DELAY);
}

async function sendBleOtaStop() {
    const packet = new Uint8Array(4);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_OTA_STOP;
    packet[2] = 0;
    packet[3] = calculateChecksum(packet, 3);

    await characteristic.writeValue(packet);
    console.log('发送 OTA_STOP');
    await delay(BLE_CTRL_DELAY);
}

function updateOtaTransferStatus(message, progress) {
    const statusEl = document.getElementById('ota-transfer-status');
    const progressEl = document.getElementById('ota-transfer-progress');
    const progressBarEl = document.getElementById('ota-transfer-progress-bar');

    if (statusEl) {
        statusEl.textContent = message;
    }

    if (progressBarEl) {
        progressBarEl.style.width = progress + '%';
    }

    if (progressEl) {
        progressEl.textContent = progress + '%';
    }
}

async function uploadOtaFileViaBle(fileData) {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    const transferContainer = document.getElementById('ota-transfer-container');
    if (transferContainer) {
        transferContainer.style.display = 'block';
    }

    try {
        updateOtaTransferStatus('准备传输...', 0);

        otaTransState = BLE_OTA_TRANS_STATE_STARTED;
        otaTransFileSize = fileData.length;
        otaTransSentBytes = 0;

        await sendBleOtaLen(fileData.length);

        await delay(50);

        const chunkSize = BLE_CHUNK_SIZE;
        let sentBytes = 0;
        for (let i = 0; i < fileData.length; i += chunkSize) {
            const chunk = fileData.slice(i, i + chunkSize);
            await sendBleOtaData(chunk);
            sentBytes += chunk.length;
            otaTransSentBytes = sentBytes;

            const progress = Math.round((sentBytes / fileData.length) * 100);
            updateOtaTransferStatus(`传输中: ${sentBytes}/${fileData.length} 字节`, progress);

            await delay(BLE_DATA_DELAY);
        }

        await sendBleOtaStop();

        otaTransState = BLE_OTA_TRANS_STATE_STOPPED;
        updateOtaTransferStatus('传输完成', 100);
        showMessage('OTA升级文件传输成功!', 'success');

    } catch (error) {
        console.error('OTA传输失败:', error);
        otaTransState = BLE_OTA_TRANS_STATE_IDLE;
        updateOtaTransferStatus('传输失败', 0);
        showMessage('OTA传输失败: ' + error.message, 'error');
    }
}

function selectOtaFile() {
    const fileInput = document.getElementById('ota-file-input');
    if (fileInput) {
        fileInput.click();
    }
}

function handleOtaFileSelect(event) {
    const file = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = function(e) {
        const arrayBuffer = e.target.result;
        const uint8Array = new Uint8Array(arrayBuffer);

        const sizeInfo = document.getElementById('ota-file-size');
        if (sizeInfo) {
            sizeInfo.textContent = `文件大小: ${uint8Array.length} 字节`;
        }

        window.selectedOtaData = uint8Array;

        event.target.value = '';
    };
    reader.readAsArrayBuffer(file);
}

function startOtaUpgrade() {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }

    if (!window.selectedOtaData) {
        showMessage('请先选择OTA升级文件', 'error');
        return;
    }

    uploadOtaFileViaBle(window.selectedOtaData);
}

async function sendBleCmd(cmdType, data = null) {
    if (!device || !server || !characteristic) {
        throw new Error('请先连接设备');
    }

    debugLog('sendBleCmd: cmd=' + cmdType.toString(16) + ', data=' + data);

    return queueBleCmd(async () => {
        let packet;
        if (data === null) {
            packet = new Uint8Array(4);
            packet[0] = BLE_CMD_HEAD;
            packet[1] = cmdType;
            packet[2] = 0;
            packet[3] = calculateChecksum(packet, 3);
        } else {
            packet = new Uint8Array(5);
            packet[0] = BLE_CMD_HEAD;
            packet[1] = cmdType;
            packet[2] = 1;
            packet[3] = data;
            packet[4] = calculateChecksum(packet, 4);
        }

        debugLog('发送数据包: ' + Array.from(packet).map(b => b.toString(16).padStart(2, '0')).join(' '));

        try {
            await characteristic.writeValue(packet);
            debugLog('BLE写入成功', 'success');
        } catch (err) {
            debugLog('BLE写入失败: ' + err.message, 'error');
            throw err;
        }

        console.log('发送命令:', cmdType.toString(16), data !== null ? '数据:' + data : '');
        await delay(BLE_CTRL_DELAY);
    });
}

async function sendBleReboot() {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_REBOOT);
    showMessage('重启命令已发送', 'success');
}

async function sendBleReset() {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    if (!confirm('确定要重置设备吗？设备将恢复出厂设置并重启。')) {
        return;
    }
    await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_RESET);
    showMessage('重置命令已发送，设备将重启', 'success');
}

async function sendBlePwrRead() {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        debugLog('电量读取失败: 未连接设备', 'error');
        return null;
    }
    try {
        debugLog('发送电量读取命令...');
        await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_PWRREAD);
        debugLog('电量读取命令已发送', 'success');
        return true;
    } catch (error) {
        showMessage('发送电量读取命令失败', 'error');
        debugLog('电量读取失败: ' + error.message, 'error');
        return null;
    }
}

async function sendBleSleepSet(onoff) {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    try {
        await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF, onoff ? 1 : 0);
        showMessage(onoff ? '休眠模式已开启' : '休眠模式已关闭', 'success');
    } catch (error) {
        showMessage('发送休眠设置命令失败', 'error');
    }
}

async function sendBleSleepModeSet(mode) {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    try {
        await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_SLEEPMODE, mode ? 1 : 0);
        showMessage(mode ? '自动唤醒已开启' : '自动唤醒已关闭', 'success');
    } catch (error) {
        showMessage('发送自动唤醒设置命令失败', 'error');
    }
}

async function sendBleSleepTimeSet(timeMinutes) {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    try {
        await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME, timeMinutes);
        showMessage('唤醒时间已设置', 'success');
    } catch (error) {
        showMessage('发送唤醒时间设置命令失败', 'error');
    }
}

async function sendBleModeGet() {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return null;
    }
    try {
        await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_MODE_GET);
        return true;
    } catch (error) {
        showMessage('发送模式查询命令失败', 'error');
        return null;
    }
}

async function sendBleModeSet(mode) {
    if (!device || !server || !characteristic) {
        showMessage('请先连接设备', 'error');
        return;
    }
    try {
        await sendBleCmd(BLE_FILM_TRANS_CH_CTRL_MODE, mode);
        showMessage(mode === 1 ? '已切换到自动模式' : '已切换到手动模式', 'success');
    } catch (error) {
        showMessage('发送模式设置命令失败', 'error');
    }
}

function setupBluetoothListener() {
    if (!characteristic) return;

    characteristic.addEventListener('characteristicvaluechanged', function(event) {
        const value = event.target.value;
        if (!value || value.byteLength < 4) return;

        const data = new Uint8Array(value.buffer);
        console.log('收到蓝牙数据:', Array.from(data).map(b => b.toString(16)).join(' '));

        const cmdType = data[1];
        const cmdLen = data[2];

        if (data[0] === BLE_CMD_HEAD && cmdType === BLE_FILM_TRANS_CH_CTRL_PWRREAD && cmdLen === 1) {
            const batteryLevel = data[3];
            updateBatteryDisplay(batteryLevel);
        }
        else if (data[0] === BLE_CMD_HEAD && cmdType === BLE_FILM_TRANS_CH_FILE_LIST && cmdLen >= 2) {
            const fileId = data[3];
            const nameLen = data[4];

            if (nameLen > 0 && nameLen < 64 && 5 + nameLen <= data.length) {
                const filenameBytes = data.slice(5, 5 + nameLen);
                const filename = String.fromCharCode.apply(null, filenameBytes).replace(/\0.*$/, '');

                console.log('FILE_LIST: id=' + fileId + ', name="' + filename + '"');

                if (window.fileListBuffer === undefined) {
                    window.fileListBuffer = [];
                }
                window.fileListBuffer.push({ id: fileId, filename: filename });

                updateFileListDisplay(window.fileListBuffer);
            }
        }
        else if (data[0] === BLE_CMD_HEAD && cmdType === BLE_FILM_TRANS_CH_FILE_DISPLAY_GET && cmdLen === 1) {
            const currentId = data[3];
            updateCurrentDisplayId(currentId);
        }
    });

    characteristic.startNotifications().then(() => {
        console.log('蓝牙通知已开启');
    }).catch(err => {
        console.error('开启蓝牙通知失败:', err);
    });
}

function updateBatteryDisplay(level) {
    const batteryLevelEl = document.getElementById('battery-level');
    const batteryFillEl = document.getElementById('battery-fill');

    if (batteryLevelEl) {
        batteryLevelEl.textContent = level + '%';
    }
    if (batteryFillEl) {
        batteryFillEl.style.width = level + '%';
    }
}

function toggleSleepSwitch() {
    const switchEl = document.getElementById('sleep-switch');
    if (switchEl) {
        sendBleSleepSet(switchEl.checked);
    }
}

function toggleAutoWakeSwitch() {
    const switchEl = document.getElementById('auto-wake-switch');
    if (switchEl) {
        sendBleSleepModeSet(switchEl.checked);
    }
}

function formatDuration(minutes) {
    if (minutes < 60) {
        return minutes + '分钟';
    } else if (minutes === 60) {
        return '1小时';
    } else {
        const hours = Math.floor(minutes / 60);
        const mins = minutes % 60;
        if (mins === 0) {
            return hours + '小时';
        }
        return hours + '小时' + mins + '分钟';
    }
}

function updateWakeDurationSlider() {
    const slider = document.getElementById('wake-duration');
    const valueDisplay = document.getElementById('wake-duration-value');
    if (slider && valueDisplay) {
        const minutes = parseInt(slider.value);
        valueDisplay.textContent = formatDuration(minutes);
    }
}

function getWakeDurationInMinutes() {
    const slider = document.getElementById('wake-duration');
    return slider ? parseInt(slider.value) : 60;
}

function setPhotoMode(mode) {
    const modeValue = mode === 'auto' ? 1 : 0;
    sendBleModeSet(modeValue);

    document.querySelectorAll('.mode-button').forEach(btn => {
        btn.classList.remove('active');
    });
    document.querySelector(`.mode-button[data-mode="${mode}"]`).classList.add('active');
}

function sendBleFileList() {
    return new Promise((resolve, reject) => {
        if (!device || !server || !characteristic) {
            showMessage('请先连接设备', 'error');
            reject(new Error('未连接设备'));
            return;
        }
        queueBleCmd(async () => {
            debugLog('开始发送文件列表请求...');
            let packet = new Uint8Array(4);
            packet[0] = BLE_CMD_HEAD;
            packet[1] = BLE_FILM_TRANS_CH_FILE_LIST;
            packet[2] = 0;
            packet[3] = calculateChecksum(packet, 3);

            try {
                await characteristic.writeValue(packet);
                debugLog('文件列表请求已发送', 'success');
            } catch (err) {
                debugLog('文件列表请求失败: ' + err.message, 'error');
                reject(err);
                return;
            }
            await delay(BLE_CTRL_DELAY);
            resolve();
        });
    });
}

function sendBleFileDelete(fileId) {
    return new Promise((resolve, reject) => {
        if (!device || !server || !characteristic) {
            showMessage('请先连接设备', 'error');
            reject(new Error('未连接设备'));
            return;
        }
        queueBleCmd(async () => {
            let packet = new Uint8Array(5);
            packet[0] = BLE_CMD_HEAD;
            packet[1] = BLE_FILM_TRANS_CH_FILE_DELETE;
            packet[2] = 1;
            packet[3] = fileId & 0xFF;
            packet[4] = calculateChecksum(packet, 4);

            try {
                await characteristic.writeValue(packet);
                debugLog('删除文件命令已发送', 'success');
            } catch (err) {
                debugLog('删除文件命令失败: ' + err.message, 'error');
                reject(err);
                return;
            }
            await delay(BLE_CTRL_DELAY);
            resolve();
        });
    });
}

function sendBleFileDisplay(fileId) {
    return new Promise((resolve, reject) => {
        if (!device || !server || !characteristic) {
            showMessage('请先连接设备', 'error');
            reject(new Error('未连接设备'));
            return;
        }
        queueBleCmd(async () => {
            let packet = new Uint8Array(5);
            packet[0] = BLE_CMD_HEAD;
            packet[1] = BLE_FILM_TRANS_CH_FILE_DISPLAY;
            packet[2] = 1;
            packet[3] = fileId & 0xFF;
            packet[4] = calculateChecksum(packet, 4);

            try {
                await characteristic.writeValue(packet);
                debugLog('显示文件命令已发送', 'success');
            } catch (err) {
                debugLog('显示文件命令失败: ' + err.message, 'error');
                reject(err);
                return;
            }
            await delay(BLE_CTRL_DELAY);
            resolve();
        });
    });
}

function sendBleFileDisplayGet() {
    return new Promise((resolve, reject) => {
        if (!device || !server || !characteristic) {
            showMessage('请先连接设备', 'error');
            reject(new Error('未连接设备'));
            return;
        }
        queueBleCmd(async () => {
            let packet = new Uint8Array(4);
            packet[0] = BLE_CMD_HEAD;
            packet[1] = BLE_FILM_TRANS_CH_FILE_DISPLAY_GET;
            packet[2] = 0;
            packet[3] = calculateChecksum(packet, 3);

            try {
                await characteristic.writeValue(packet);
                debugLog('查询显示状态已发送', 'success');
            } catch (err) {
                debugLog('查询显示状态失败: ' + err.message, 'error');
                reject(err);
                return;
            }
            await delay(BLE_CTRL_DELAY);
            resolve();
        });
    });
}

async function sendBleCmdWithData(cmdType, value, dataLen = 4) {
    if (!device || !server || !characteristic) {
        throw new Error('请先连接设备');
    }

    return queueBleCmd(async () => {
        const packet = new Uint8Array(4 + dataLen);
        packet[0] = BLE_CMD_HEAD;
        packet[1] = cmdType;
        packet[2] = dataLen;

        if (dataLen === 1) {
            packet[3] = value & 0xFF;
        } else {
            packet[3] = (value >> 24) & 0xFF;
            packet[4] = (value >> 16) & 0xFF;
            packet[5] = (value >> 8) & 0xFF;
            packet[6] = value & 0xFF;
        }

        packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);

        debugLog('sendBleCmdWithData: ' + Array.from(packet).map(b => b.toString(16).padStart(2, '0')).join(' '));

        try {
            await characteristic.writeValue(packet);
            debugLog('BLE写入成功', 'success');
        } catch (err) {
            debugLog('BLE写入失败: ' + err.message, 'error');
            throw err;
        }

        console.log('发送命令:', cmdType.toString(16), '数据:' + value);
        await delay(BLE_CTRL_DELAY);
    });
}

function updateFileListDisplay(fileList) {
    const fileListEl = document.getElementById('film-file-list');
    if (!fileListEl) return;

    if (fileList.length === 0) {
        fileListEl.innerHTML = '<div class="empty-state">暂无文件</div>';
        return;
    }

    let html = '';
    fileList.forEach(file => {
        html += `<div class="file-item" data-id="${file.id}">
            <input type="checkbox" class="file-checkbox" data-id="${file.id}">
            <span class="file-id">${file.id}</span>
            <span class="file-name" title="${file.filename}">${file.filename}</span>
        </div>`;
    });
    fileListEl.innerHTML = html;

    document.querySelectorAll('.file-checkbox').forEach(cb => {
        cb.addEventListener('change', function() {
            const id = parseInt(this.dataset.id);
            if (this.checked) {
                window.selectedFileId = id;
                document.querySelectorAll('.file-checkbox').forEach(c => {
                    if (c !== this) c.checked = false;
                });
            }
        });
    });

    if (window.currentDisplayFileId !== undefined) {
        updateCurrentDisplayId(window.currentDisplayFileId);
    }
}

function updateCurrentDisplayId(id) {
    window.currentDisplayFileId = id;
    document.querySelectorAll('.file-item').forEach(item => {
        const itemId = parseInt(item.dataset.id);
        if (itemId === id) {
            item.classList.add('displaying');
        } else {
            item.classList.remove('displaying');
        }
    });
}

function refreshFileList() {
    window.fileListBuffer = [];
    const fileListEl = document.getElementById('film-file-list');
    if (fileListEl) {
        fileListEl.innerHTML = '<div class="empty-state">正在加载...</div>';
    }
    sendBleFileList().then(() => {
        setTimeout(() => {
            sendBleFileDisplayGet();
        }, 500);
    }).catch(err => console.error('刷新文件列表失败:', err));
}

function deleteSelectedFile() {
    if (window.selectedFileId === undefined || window.selectedFileId === null) {
        showMessage('请先选择要删除的文件', 'error');
        return;
    }
    if (!confirm('确定要删除选中的文件吗？')) {
        return;
    }
    sendBleFileDelete(window.selectedFileId).then(() => {
        showMessage('删除命令已发送', 'success');
        setTimeout(() => {
            refreshFileList();
        }, 500);
    });
}

function selectDisplayFile() {
    if (window.selectedFileId === undefined || window.selectedFileId === null) {
        showMessage('请先选择要显示的文件', 'error');
        return;
    }
    sendBleFileDisplay(window.selectedFileId).then(() => {
        showMessage('显示命令已发送', 'success');
        setTimeout(() => {
            sendBleFileDisplayGet();
        }, 500);
    });
}