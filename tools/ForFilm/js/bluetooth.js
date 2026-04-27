// 蓝牙功能实现
let device = null;
let server = null;
let service = null;
let characteristic = null;

const BLE_SERVICE_UUID = '00002000-0000-1000-8000-00805f9b34fb';
const BLE_CHARACTERISTIC_UUID = '00002001-0000-1000-8000-00805f9b34fb';

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
    if (device && device.gatt.connected) {
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
        alert('请先连接设备');
        return;
    }
    alert('请先处理图片，然后通过"发送"按钮上传');
}

function checkBluetoothStatus() {
    const status = document.getElementById('connection-status');
    if (status && device && device.gatt && device.gatt.connected) {
        status.textContent = '已连接';
        status.className = 'status connected';
    }
}

setInterval(checkBluetoothStatus, 5000);