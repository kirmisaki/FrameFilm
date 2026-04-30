// 蓝牙功能实现
let device = null;
let server = null;
let service = null;
let characteristic = null;

const BLE_SERVICE_UUID = '00002000-0000-1000-8000-00805f9b34fb';
const BLE_CHARACTERISTIC_UUID = '00002001-0000-1000-8000-00805f9b34fb';

const BLE_CMD_HEAD = 0x55;

const BLE_FILM_TRANS_CH_FILE_START = 0x03;
const BLE_FILM_TRANS_CH_FILE_NAME = 0x00;
const BLE_FILM_TRANS_CH_FILE_LEN = 0x01;
const BLE_FILM_TRANS_CH_FILE_DATA = 0x02;
const BLE_FILM_TRANS_CH_FILE_STOP = 0x04;

const BLE_FILM_TRANS_STATE_IDLE = 0;
const BLE_FILM_TRANS_STATE_STARTED = 1;
const BLE_FILM_TRANS_STATE_RECV_NAME = 2;
const BLE_FILM_TRANS_STATE_RECV_LEN = 3;
const BLE_FILM_TRANS_STATE_RECV_DATA = 4;
const BLE_FILM_TRANS_STATE_STOPPED = 5;

const BLE_CHUNK_SIZE = 192;  // MTU 200 - 协议开销(4字节头+1字节校验) = 195, 使用192更安全

let filmTransState = BLE_FILM_TRANS_STATE_IDLE;
let filmTransFileName = '';
let filmTransFileSize = 0;
let filmTransSentBytes = 0;

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

            await delay(15);
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
    await delay(20);
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
    await delay(20);
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
    await delay(20);
}

async function sendBleFileData(data) {
    const packet = new Uint8Array(4 + data.length);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_DATA;
    packet[2] = data.length;
    packet.set(data, 3);
    packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);

    await characteristic.writeValue(packet);
    await delay(10);
}

async function sendBleFileStop() {
    const packet = new Uint8Array(4);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = BLE_FILM_TRANS_CH_FILE_STOP;
    packet[2] = 0;
    packet[3] = calculateChecksum(packet, 3);

    await characteristic.writeValue(packet);
    console.log('发送 FILE_STOP');
    await delay(20);
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