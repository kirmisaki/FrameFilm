// 蓝牙功能实现
let device = null;
let server = null;

function initBluetooth() {
    const scanButton = document.getElementById('scan-button');
    const deviceList = document.getElementById('device-list');
    const connectionStatus = document.getElementById('connection-status');

    if (!navigator.bluetooth) {
        connectionStatus.textContent = '浏览器不支持蓝牙';
        scanButton.disabled = true;
        return;
    }

    scanButton.addEventListener('click', async function() {
        try {
            connectionStatus.textContent = '正在扫描...';
            deviceList.innerHTML = '';

            // 扫描蓝牙设备
            device = await navigator.bluetooth.requestDevice({
                filters: [{
                    namePrefix: 'ForFilm'
                }],
                optionalServices: ['generic_access']
            });

            // 显示设备信息
            const deviceItem = document.createElement('div');
            deviceItem.className = 'device-item';
            deviceItem.innerHTML = `
                <div>
                    <strong>${device.name || '未知设备'}</strong>
                    <p>${device.id}</p>
                </div>
                <button onclick="connectToDevice('${device.id}')">连接</button>
            `;
            deviceList.appendChild(deviceItem);

            connectionStatus.textContent = '发现设备';
        } catch (error) {
            console.error('蓝牙扫描错误:', error);
            connectionStatus.textContent = '扫描失败';
        }
    });
}

async function connectToDevice(deviceId) {
    const connectionStatus = document.getElementById('connection-status');

    try {
        connectionStatus.textContent = '正在连接...';

        // 连接到设备
        server = await device.gatt.connect();
        connectionStatus.textContent = '已连接';
        connectionStatus.className = 'status connected';

        // 可以在这里添加服务发现和特征读写逻辑
        console.log('设备已连接:', device.name);

    } catch (error) {
        console.error('连接错误:', error);
        connectionStatus.textContent = '连接失败';
        connectionStatus.className = 'status';
    }
}

// 上传文件到设备（暂不实现）
function uploadToDevice() {
    if (!device || !server) {
        alert('请先连接设备');
        return;
    }

    alert('蓝牙上传功能暂未实现');
}

// 断开连接
function disconnectFromDevice() {
    if (device && device.gatt.connected) {
        device.gatt.disconnect();
        const connectionStatus = document.getElementById('connection-status');
        connectionStatus.textContent = '未连接';
        connectionStatus.className = 'status';
    }
}

// 检查蓝牙连接状态
function checkBluetoothStatus() {
    const connectionStatus = document.getElementById('connection-status');
    if (device && device.gatt.connected) {
        connectionStatus.textContent = '已连接';
        connectionStatus.className = 'status connected';
    } else {
        connectionStatus.textContent = '未连接';
        connectionStatus.className = 'status';
    }
}

// 定期检查连接状态
setInterval(checkBluetoothStatus, 5000);