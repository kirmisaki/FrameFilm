// USB(CDC 虚拟串口) 传输实现
//
// 通过 Web Serial 连接设备后，把接口适配成与 Web Bluetooth 一致的
// device / server / characteristic 对象，从而直接复用 bluetooth.js 中
// 已有的全部协议逻辑（命令、文件传输、OTA、WiFi 配置等）。
//
// 说明：CDC 是字节流，同一帧可能被拆成多段，或多帧粘在一起，
//       这里按“帧头 + 长度 + 校验和”完成组帧，保证每次回调都是一整帧。

const USB_BAUD_RATE = 115200; // CDC 为虚拟串口，波特率仅用于满足 Web Serial 参数
const USB_CMD_HEAD = 0x55;
const USB_VENDOR_ID_ESPRESSIF = 0x303A;

let usbPort = null;
let usbWriter = null;
let usbReader = null;
let usbReading = false;
let usbConnected = false;
let usbRxBuffer = new Uint8Array(0);
let usbValueListeners = [];
let usbDisconnectListeners = [];
let usbCharacteristic = null;
let usbServer = null;

// 把一整帧分发给监听者，模拟 characteristicvaluechanged 事件
function usbEmitFrame(frame) {
    const value = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
    for (let i = 0; i < usbValueListeners.length; i++) {
        try {
            usbValueListeners[i]({ target: { value: value } });
        } catch (e) {
            console.error('[ForFilm] USB 数据处理异常:', e);
        }
    }
}

// 字节流组帧：解析出一条完整且校验通过的帧就派发一次
function usbFeedData(chunk) {
    const merged = new Uint8Array(usbRxBuffer.length + chunk.length);
    merged.set(usbRxBuffer, 0);
    merged.set(chunk, usbRxBuffer.length);
    usbRxBuffer = merged;

    let offset = 0;
    while (usbRxBuffer.length - offset >= 4) {
        if (usbRxBuffer[offset] !== USB_CMD_HEAD) {
            offset++;
            continue;
        }

        const dataLen = usbRxBuffer[offset + 2];
        const frameLen = dataLen + 4;
        if (usbRxBuffer.length - offset < frameLen) {
            break; // 半包，等待后续数据
        }

        let sum = 0;
        for (let i = 0; i < 3 + dataLen; i++) {
            sum = (sum + usbRxBuffer[offset + i]) & 0xFF;
        }

        if (sum === usbRxBuffer[offset + 3 + dataLen]) {
            usbEmitFrame(usbRxBuffer.slice(offset, offset + frameLen));
            offset += frameLen;
        } else {
            console.warn('[ForFilm] USB 帧校验失败，重新同步');
            offset++;
        }
    }

    if (offset > 0) {
        usbRxBuffer = usbRxBuffer.slice(offset);
    }
}

async function usbReadLoop() {
    while (usbReading && usbPort && usbPort.readable) {
        let reader = null;
        try {
            reader = usbPort.readable.getReader();
            usbReader = reader;
            while (usbReading) {
                const { value, done } = await reader.read();
                if (done) break;
                if (value && value.length) {
                    usbFeedData(value);
                }
            }
        } catch (e) {
            if (usbReading) {
                console.error('[ForFilm] USB 读取异常:', e);
            }
        } finally {
            if (reader) {
                try { reader.releaseLock(); } catch (e) {}
            }
            if (usbReader === reader) {
                usbReader = null;
            }
        }
    }
}

// 适配出与 Web Bluetooth characteristic 相同的最小接口
function usbCreateCharacteristic() {
    return {
        properties: { write: true, notify: true },
        writeValue(data) {
            if (!usbWriter) {
                return Promise.reject(new Error('USB 未连接'));
            }
            const bytes = (data instanceof Uint8Array) ? data : new Uint8Array(data);
            return usbWriter.write(bytes);
        },
        startNotifications() {
            return Promise.resolve();
        },
        addEventListener(type, fn) {
            if (type === 'characteristicvaluechanged') {
                usbValueListeners.push(fn);
            }
        },
        removeEventListener(type, fn) {
            if (type === 'characteristicvaluechanged') {
                const i = usbValueListeners.indexOf(fn);
                if (i >= 0) usbValueListeners.splice(i, 1);
            }
        }
    };
}

// 适配出与 Web Bluetooth device 相同的最小接口
function usbCreateDevice() {
    return {
        name: 'FRAMEFILMDOCK USB',
        gatt: {
            get connected() { return usbConnected; },
            connect() { return Promise.resolve(usbServer); },
            disconnect() { usbClose(); }
        },
        addEventListener(type, fn) {
            if (type === 'gattserverdisconnected') {
                usbDisconnectListeners.push(fn);
            }
        },
        removeEventListener(type, fn) {
            if (type === 'gattserverdisconnected') {
                const i = usbDisconnectListeners.indexOf(fn);
                if (i >= 0) usbDisconnectListeners.splice(i, 1);
            }
        }
    };
}

async function usbClose() {
    if (!usbConnected && !usbPort) {
        return;
    }

    usbConnected = false;
    usbReading = false;

    try {
        if (usbReader) {
            await usbReader.cancel();
        }
    } catch (e) {}

    try {
        if (usbWriter) {
            usbWriter.releaseLock();
            usbWriter = null;
        }
    } catch (e) {}

    try {
        if (usbPort) {
            await usbPort.close();
        }
    } catch (e) {}

    usbPort = null;
    usbReader = null;

    // 通知断开监听者，复用蓝牙的 onDisconnected 收尾逻辑
    const listeners = usbDisconnectListeners.slice();
    usbDisconnectListeners = [];
    for (let i = 0; i < listeners.length; i++) {
        try {
            listeners[i]({ target: device });
        } catch (e) {}
    }
}

// USB 连接入口：装配接口对象后走与蓝牙一致的初始化流程
async function connectUsb() {
    const status = document.getElementById('connection-status');
    const deviceList = document.getElementById('device-list');

    if (!navigator.serial) {
        if (status) {
            status.textContent = '浏览器不支持 USB 串口';
            status.className = 'status';
        }
        if (typeof showMessage === 'function') {
            showMessage('当前浏览器不支持 Web Serial，请使用 Chrome/Edge', 'error');
        }
        return;
    }

    // 已连接时点击即断开
    if (usbConnected) {
        await usbClose();
        return;
    }

    try {
        if (status) {
            status.textContent = '请选择设备串口...';
            status.className = 'status';
        }
        if (deviceList) {
            deviceList.innerHTML = '<div class="no-devices">请在弹窗中选择 FRAMEFILM 对应的串口</div>';
        }

        usbPort = await navigator.serial.requestPort();

        // 无法按名称过滤串口，选中后校验 VID 是否为 Espressif
        const info = (usbPort.getInfo && usbPort.getInfo()) || {};
        if (info.usbVendorId !== undefined && info.usbVendorId !== USB_VENDOR_ID_ESPRESSIF) {
            console.warn('[ForFilm] 所选串口非 Espressif 设备, VID=0x' + info.usbVendorId.toString(16));
        }

        await usbPort.open({ baudRate: USB_BAUD_RATE });
        usbWriter = usbPort.writable.getWriter();

        usbConnected = true;
        usbReading = true;
        usbRxBuffer = new Uint8Array(0);
        usbValueListeners = [];
        usbDisconnectListeners = [];
        usbReadLoop();

        // 装配与蓝牙一致的接口对象，复用既有协议逻辑
        usbCharacteristic = usbCreateCharacteristic();
        usbServer = {
            getPrimaryService: () => Promise.resolve({
                getCharacteristic: () => Promise.resolve(usbCharacteristic)
            })
        };
        device = usbCreateDevice();
        server = usbServer;
        characteristic = usbCharacteristic;

        if (status) {
            status.textContent = 'USB 已连接，正在初始化...';
            status.className = 'status connected';
        }

        await onDeviceConnected(device.name || '');

    } catch (error) {
        console.error('[ForFilm] USB 连接失败:', error);
        await usbClose();

        const msg = (error && error.message) ? error.message : String(error);
        if (status) {
            status.textContent = 'USB 连接失败: ' + msg;
            status.className = 'status';
        }
        if (deviceList) {
            deviceList.innerHTML = '<div class="no-devices">USB 连接已取消或失败</div>';
        }
        if (typeof showMessage === 'function') {
            showMessage('USB 连接失败: ' + msg, 'error');
        }
    }
}

function initUsb() {
    const usbButton = document.getElementById('usb-button');
    if (usbButton) {
        usbButton.addEventListener('click', function() {
            connectUsb();
        });
    }

    // 已授权串口的插拔检测（浏览器需支持 connect/disconnect 事件）
    if (navigator.serial && navigator.serial.addEventListener) {
        navigator.serial.addEventListener('connect', function(e) {
            console.log('[ForFilm] 检测到串口设备接入');
        });
        navigator.serial.addEventListener('disconnect', function(e) {
            console.log('[ForFilm] 检测到串口设备移除');
            if (usbPort && e.target === usbPort) {
                usbClose();
            }
        });
    }
}
