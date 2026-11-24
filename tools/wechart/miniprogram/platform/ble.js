// BLE 连接、命令队列、文件传输与设备状态同步
const U = require('../utils/ble-utils');
const filmUtils = require('../utils/film-utils');
const store = require('./store');
const log = require('./log');

const TARGET_SERVICE = U.BLE_SERVICE_UUID;
const TARGET_CHAR = U.BLE_CHARACTERISTIC_UUID;

let inited = false;
let queue = [];
let pumping = false;
let responseHandlers = new Set();
let disconnectHandlers = new Set();
let scanHandler = null;
let scanTimer = null;
let scanDevices = {};
let abortFlag = false;
let fileRefreshActive = false;
let scanListenerReady = false;

const MODEL_FILM_TYPE = {
  FRAMEFILM: 'FRAMEFILM',
  FRAMEFILMPRO: 'FRAMEFILMPRO',
  FRAMEFILMMAX: 'FRAMEFILMMAX',
  FRAMEFILMDOCK: 'FRAMEFILMDOCK',
  FRAMEFILMC3: 'FRAMEFILMC3',
  // 通行证版与 Pro 3.70" 同屏同驱动，几何参数共用
  FRAMEFILMARK: 'FRAMEFILMPRO'
};

function detectModel(name) {
  const upper = String(name || '').toUpperCase();
  if (upper.indexOf('FRAMEFILMARK') === 0) return 'FRAMEFILMARK';
  if (upper.indexOf('FRAMEFILMC3') === 0) return 'FRAMEFILMC3';
  if (upper.indexOf('MAX') !== -1) return 'FRAMEFILMMAX';
  if (upper.indexOf('DOCK') !== -1) return 'FRAMEFILMDOCK';
  if (upper.indexOf('PRO') !== -1) return 'FRAMEFILMPRO';
  return 'FRAMEFILM';
}

function normalizeUuid(uuid) {
  return String(uuid || '').toLowerCase().replace(/-/g, '');
}

function toArrayBuffer(packet) {
  if (packet.byteOffset === 0 && packet.byteLength === packet.buffer.byteLength) {
    return packet.buffer;
  }
  return packet.buffer.slice(packet.byteOffset, packet.byteOffset + packet.byteLength);
}

function decodeUtf8(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length; i++) {
    const c = bytes[i];
    if (c === 0) break;
    if (c < 0x80) {
      out += String.fromCharCode(c);
    } else if (c >= 0xc0 && c < 0xe0 && i + 1 < bytes.length) {
      out += String.fromCharCode(((c & 0x1f) << 6) | (bytes[i + 1] & 0x3f));
      i += 1;
    } else if (c >= 0xe0 && c < 0xf0 && i + 2 < bytes.length) {
      out += String.fromCharCode(((c & 0x0f) << 12) | ((bytes[i + 1] & 0x3f) << 6) | (bytes[i + 2] & 0x3f));
      i += 2;
    } else {
      out += '?';
    }
  }
  return out;
}

/* ------------------------------ 初始化 ------------------------------ */

function init() {
  if (inited) return;
  inited = true;

  if (typeof wx.onBLEConnectionStateChange === 'function') {
    wx.onBLEConnectionStateChange(function (res) {
      const current = store.get();
      if (!res.connected && res.deviceId === current.deviceId) {
        handleDisconnect();
      }
    });
  }

  const onValue = function (res) {
    const current = store.get();
    if (res.deviceId && current.deviceId && res.deviceId !== current.deviceId) return;
    const resp = U.parseBleResponse(res.value);
    if (!resp) return;
    handleResponse(resp);
    responseHandlers.forEach(function (fn) {
      try {
        fn(resp);
      } catch (e) {
        console.error('[ble] response handler error', e);
      }
    });
  };

  if (typeof wx.onBLECharacteristicValueChange === 'function') {
    wx.onBLECharacteristicValueChange(onValue);
  } else if (typeof wx.onBLECharacteristicValueChanged === 'function') {
    wx.onBLECharacteristicValueChanged(onValue);
  }
}

function onResponse(fn) {
  responseHandlers.add(fn);
  return function () {
    responseHandlers.delete(fn);
  };
}

function onDisconnect(fn) {
  disconnectHandlers.add(fn);
  return function () {
    disconnectHandlers.delete(fn);
  };
}

/* ------------------------------ 扫描 ------------------------------ */

function startScan(onUpdate) {
  init();
  scanHandler = onUpdate || null;
  scanDevices = {};
  store.patch({ scanning: true, devices: [] });

  wx.openBluetoothAdapter({
    success: function () {
      if (!scanListenerReady) {
        scanListenerReady = true;
        wx.onBluetoothDeviceFound(function (res) {
        const found = res.devices || [];
        found.forEach(function (d) {
          const name = d.name || d.localName || '';
          if (name.toUpperCase().indexOf('FRAMEFILM') === -1) return;
          const item = {
            deviceId: d.deviceId,
            name: name,
            model: detectModel(name),
            RSSI: d.RSSI || -100
          };
          const old = scanDevices[item.deviceId];
          if (!old || old.RSSI !== item.RSSI) {
            scanDevices[item.deviceId] = item;
            const list = Object.keys(scanDevices).map(function (k) {
              return scanDevices[k];
            }).sort(function (a, b) {
              return b.RSSI - a.RSSI;
            });
            store.patch({ devices: list });
            if (scanHandler) scanHandler(list);
          }
        });
        });
      }

      wx.startBluetoothDevicesDiscovery({
        services: [],
        allowDuplicatesKey: true,
        interval: 0,
        success: function () {
          if (scanTimer) clearTimeout(scanTimer);
          scanTimer = setTimeout(stopScan, 15000);
        },
        fail: function () {
          store.patch({ scanning: false });
        }
      });
    },
    fail: function () {
      store.patch({ scanning: false });
    }
  });
}

function stopScan() {
  if (scanTimer) {
    clearTimeout(scanTimer);
    scanTimer = null;
  }
  wx.stopBluetoothDevicesDiscovery({
    complete: function () {
      store.patch({ scanning: false });
    }
  });
}

/* ------------------------------ 连接 ------------------------------ */

function connect(device, onStatus) {
  init();
  const status = function (text) {
    if (onStatus) onStatus(text);
  };
  stopScan();
  store.patch({ connecting: true });

  return new Promise(function (resolve, reject) {
    wx.createBLEConnection({
      deviceId: device.deviceId,
      timeout: 12000,
      success: function () {
        status('已连接，正在协商 MTU');
        const afterMtu = function () {
          status('正在发现服务');
          setTimeout(function () {
            discoverServices(device, status).then(function () {
              status('正在读取设备信息');
              store.patch({ connecting: false });
              resolve();
              setTimeout(function () {
                sendInitQueries();
              }, 200);
            }).catch(function (err) {
              store.patch({ connecting: false });
              reject(err);
            });
          }, 300);
        };
        if (typeof wx.setBLEMTU === 'function') {
          wx.setBLEMTU({
            deviceId: device.deviceId,
            mtu: 200,
            complete: afterMtu
          });
        } else {
          afterMtu();
        }
      },
      fail: function (err) {
        store.patch({ connecting: false });
        reject(err);
      }
    });
  });
}

function discoverServices(device, status) {
  return new Promise(function (resolve, reject) {
    wx.getBLEDeviceServices({
      deviceId: device.deviceId,
      success: function (res) {
        const list = res.services || [];
        let matched = '';
        for (let i = 0; i < list.length; i++) {
          if (normalizeUuid(list[i].uuid) === normalizeUuid(TARGET_SERVICE)) {
            matched = list[i].uuid;
            break;
          }
        }
        if (!matched) {
          reject(new Error('未找到目标服务'));
          return;
        }
        status('正在发现特征值');
        wx.getBLEDeviceCharacteristics({
          deviceId: device.deviceId,
          serviceId: matched,
          success: function (charRes) {
            const chars = charRes.characteristics || [];
            let charId = '';
            for (let j = 0; j < chars.length; j++) {
              if (normalizeUuid(chars[j].uuid) === normalizeUuid(TARGET_CHAR)) {
                charId = chars[j].uuid;
                break;
              }
            }
            if (!charId) {
              reject(new Error('未找到目标特征值'));
              return;
            }
            enableNotify(device, matched, charId).then(resolve).catch(reject);
          },
          fail: reject
        });
      },
      fail: reject
    });
  });
}

function enableNotify(device, serviceId, characteristicId) {
  return new Promise(function (resolve, reject) {
    wx.notifyBLECharacteristicValueChange({
      deviceId: device.deviceId,
      serviceId: serviceId,
      characteristicId: characteristicId,
      state: true,
      success: function () {
        const model = detectModel(device.name);
        const filmType = MODEL_FILM_TYPE[model] || 'FRAMEFILM';
        filmUtils.setDeviceType(filmType);
        const cfg = filmUtils.getDeviceConfig();
        store.patch({
          connected: true,
          connecting: false,
          deviceId: device.deviceId,
          serviceId: serviceId,
          characteristicId: characteristicId,
          name: device.name,
          model: model,
          screenName: cfg.displayName,
          width: cfg.screenWidth,
          height: cfg.screenHeight,
          panelId: 0,
          fileList: [],
          currentFileId: -1
        });
        if (typeof wx.setKeepScreenOn === 'function') {
          wx.setKeepScreenOn({ keepScreenOn: true });
        }
        try {
          wx.setStorageSync('ff_last_device', {
            deviceId: device.deviceId,
            name: device.name || ''
          });
        } catch (e) {}
        resolve();
      },
      fail: reject
    });
  });
}

function handleDisconnect() {
  if (typeof wx.setKeepScreenOn === 'function') {
    wx.setKeepScreenOn({ keepScreenOn: false });
  }
  store.patch({
    connected: false,
    connecting: false,
    deviceId: '',
    serviceId: '',
    characteristicId: '',
    name: '',
    model: '',
    screenName: '',
    panelId: 0,
    width: 0,
    height: 0,
    battery: 0,
    fileList: [],
    currentFileId: -1
  });
  disconnectHandlers.forEach(function (fn) {
    try {
      fn();
    } catch (e) {}
  });
}

function disconnect() {
  const current = store.get();
  if (!current.deviceId) {
    handleDisconnect();
    return Promise.resolve();
  }
  return new Promise(function (resolve) {
    wx.closeBLEConnection({
      deviceId: current.deviceId,
      complete: function () {
        handleDisconnect();
        resolve();
      }
    });
  });
}

function isConnected() {
  return !!store.get().connected;
}

function reconnectLast() {
  let cached = null;
  try {
    cached = wx.getStorageSync('ff_last_device');
  } catch (e) {}
  if (!cached || !cached.deviceId) {
    return Promise.reject(new Error('没有可重连的设备'));
  }
  return connect({ deviceId: cached.deviceId, name: cached.name || 'FrameFilm' });
}

// 网络下载测试：0x3C 触发，1 秒轮询 0x3D，直到完成/失败
function downloadTest(onStatus) {
  store.patch({ downloadState: 0, downloadProgress: 0 });
  return send(U.BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD).then(function () {
    return new Promise(function (resolve, reject) {
      let elapsed = 0;
      const timer = setInterval(function () {
        elapsed += 1000;
        send(U.BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE).catch(function () {});
        const s = store.get();
        if (onStatus) onStatus(s.downloadState, s.downloadProgress);
        if (s.downloadState === 2 || s.downloadState === 3) {
          clearInterval(timer);
          resolve(s);
        } else if (elapsed > 120000) {
          clearInterval(timer);
          reject(new Error('下载超时'));
        }
      }, 1000);
    });
  });
}

/* ------------------------------ 收发 ------------------------------ */

function enqueue(task) {
  return new Promise(function (resolve, reject) {
    queue.push({ task: task, resolve: resolve, reject: reject });
    pump();
  });
}

function pump() {
  if (pumping) return;
  const item = queue.shift();
  if (!item) {
    pumping = false;
    return;
  }
  pumping = true;
  item.task().then(item.resolve).catch(item.reject).then(function () {
    pumping = false;
    setTimeout(pump, 0);
  });
}

function sendPacket(packet, delay) {
  return enqueue(function () {
    return new Promise(function (resolve, reject) {
      const s = store.get();
      if (!s.connected || !s.deviceId) {
        reject(new Error('未连接设备'));
        return;
      }
      const channel = packet[1];
      if (channel !== 0x02 && channel !== 0x11) {
        log.push('TX 0x' + channel.toString(16).toUpperCase() + ' LEN ' + packet[2], 'tx');
      }
      wx.writeBLECharacteristicValue({
        deviceId: s.deviceId,
        serviceId: s.serviceId,
        characteristicId: s.characteristicId,
        value: toArrayBuffer(packet),
        success: function () {
          setTimeout(resolve, delay === undefined ? U.BLE_CTRL_DELAY : delay);
        },
        fail: reject
      });
    });
  });
}

function send(cmd, data, delay) {
  const packet = U.buildCmdPacket(cmd, data);
  if (!packet) return Promise.reject(new Error('无效的数据包'));
  return sendPacket(packet, delay);
}

function sendString(cmd, text, maxLen) {
  return sendPacket(U.buildStringPacket(cmd, text, maxLen || 128), U.BLE_CTRL_DELAY);
}

function sendInitQueries() {
  const cmds = [
    U.BLE_FILM_TRANS_CH_CTRL_SCREEN_RESOLUTION_GET,
    U.BLE_FILM_TRANS_CH_CTRL_PWRREAD,
    U.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET,
    U.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET,
    U.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET,
    U.BLE_FILM_TRANS_CH_CTRL_MODE_GET,
    U.BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET,
    U.BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET,
    U.BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET,
    U.BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET,
    U.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET,
    U.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET,
    U.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET
  ];
  cmds.reduce(function (p, cmd) {
    return p.then(function () {
      return send(cmd).catch(function () {});
    });
  }, Promise.resolve()).then(function () {
    refreshFileList().catch(function () {});
  });
}

/* ------------------------------ 响应 ------------------------------ */

function handleResponse(resp) {
  const data = resp.data;
  const len = resp.cmdLen;
  if (resp.cmdType !== 0x02 && resp.cmdType !== 0x11 && resp.cmdType !== 0x06) {
    log.push('RX 0x' + resp.cmdType.toString(16).toUpperCase() + ' LEN ' + len, 'rx');
  }
  switch (resp.cmdType) {
    case U.BLE_FILM_TRANS_CH_CTRL_PWRREAD:
      if (len === 1) store.patch({ battery: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_SCREEN_RESOLUTION_GET:
      applyScreen(len === 5 ? data[3] : 0,
        len === 5 ? ((data[4] << 8) | data[5]) : ((data[3] << 8) | data[4]),
        len === 5 ? ((data[6] << 8) | data[7]) : ((data[5] << 8) | data[6]));
      break;

    case U.BLE_FILM_TRANS_CH_FILE_LIST:
      parseFileList(data, len);
      break;

    case U.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET:
      if (len === 1) store.patch({ currentFileId: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET:
      if (len === 1) store.patch({ sleepOnOff: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET:
      if (len === 1) store.patch({ autoWake: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET:
      if (len === 2) store.patch({ wakeMinutes: (data[3] << 8) | data[4] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_MODE_GET:
      if (len === 1) store.patch({ photoMode: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE:
      if (len === 2) store.patch({ downloadState: data[3], downloadProgress: data[4] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET:
      if (len === 1) store.patch({ wifiEnable: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET:
      if (len > 0) store.patch({ ssid: decodeUtf8(data.subarray(3, 3 + len)) });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET:
      if (len > 0) store.patch({ password: decodeUtf8(data.subarray(3, 3 + len)) });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET:
      if (len > 0) store.patch({ apiUrl: decodeUtf8(data.subarray(3, 3 + len)) });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET:
      if (len === 1) store.patch({ wifiConnected: data[3] });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_URL_GET:
      if (len > 0) store.patch({ heartbeatUrl: decodeUtf8(data.subarray(3, 3 + len)) });
      break;

    case U.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET:
      if (len === 1) store.patch({ heartbeatInterval: data[3] });
      break;

    default:
      break;
  }
}

function applyScreen(panelId, width, height) {
  if (!width || !height) return;
  const current = store.get();
  const isArk = current.model === 'FRAMEFILMARK';

  if (panelId && filmUtils.PANEL_CONFIGS[panelId]) {
    filmUtils.applyScreenParams(panelId, width, height);
    // 通行证版身份由广播名决定，屏幕参数只校正几何
    if (isArk) filmUtils.setDeviceType('FRAMEFILMPRO');
  } else {
    const cfg = filmUtils.getDeviceConfig();
    cfg.screenWidth = width;
    cfg.screenHeight = height;
    cfg.canvasWidth = cfg.isPortraitPanel ? height : width;
    cfg.canvasHeight = cfg.isPortraitPanel ? width : height;
  }

  const cfg = filmUtils.getDeviceConfig();
  if (isArk && cfg.colorCount === 4) cfg.colorCount = 6;
  store.patch({
    panelId: panelId || current.panelId,
    width: cfg.screenWidth,
    height: cfg.screenHeight,
    screenName: cfg.displayName
  });
}

function parseFileList(data, len) {
  if (len < 2) return;
  const fileId = data[3];
  const nameLen = data[4];
  if (nameLen <= 0 || 5 + nameLen > data.length) return;
  const name = decodeUtf8(data.subarray(5, 5 + nameLen));
  const list = (store.get().fileList || []).slice();
  let found = false;
  for (let i = 0; i < list.length; i++) {
    if (list[i].fileId === fileId) {
      list[i].name = name;
      found = true;
      break;
    }
  }
  if (!found) list.push({ fileId: fileId, name: name });
  list.sort(function (a, b) {
    return a.fileId - b.fileId;
  });
  store.patch({ fileList: list });
}

/* ------------------------------ 文件列表 ------------------------------ */

function refreshFileList() {
  if (fileRefreshActive) return Promise.resolve(store.get().fileList);
  fileRefreshActive = true;
  store.patch({ fileList: [] });

  return send(U.BLE_FILM_TRANS_CH_FILE_LIST).then(function () {
    return new Promise(function (resolve) {
      const started = Date.now();
      let last = -1;
      let stable = 0;
      const timer = setInterval(function () {
        const count = (store.get().fileList || []).length;
        if (count === last) stable += 1;
        else stable = 0;
        last = count;
        const elapsed = Date.now() - started;
        if ((stable >= 2 && elapsed > 1200) || elapsed > 15000) {
          clearInterval(timer);
          send(U.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET).catch(function () {});
          resolve(store.get().fileList);
        }
      }, 600);
    });
  }).then(function (list) {
    fileRefreshActive = false;
    return list;
  }).catch(function (err) {
    fileRefreshActive = false;
    throw err;
  });
}

/* ------------------------------ 文件传输 ------------------------------ */

function normalizeFileName(name) {
  let base = String(name || 'image.film').replace(/[^\x20-\x7e]/g, '');
  base = base.replace(/[\/\\:*?"<>|]/g, '_').trim();
  if (!base || base === '.film') base = 'image.film';
  if (!/\.film$/i.test(base)) base += '.film';
  if (base.length > 50) {
    base = base.slice(0, 45) + '.film';
  }
  return base || 'image.film';
}

function cancelTransfer() {
  abortFlag = true;
}

function uploadFilm(fileData, fileName, onProgress) {
  abortFlag = false;
  const name = normalizeFileName(fileName);
  return send(U.BLE_FILM_TRANS_CH_FILE_START)
    .then(function () {
      return sendPacket(U.buildFileNamePacket(name), U.BLE_CTRL_DELAY);
    })
    .then(function () {
      return sendPacket(U.buildFileLenPacket(fileData.length), U.BLE_CTRL_DELAY);
    })
    .then(function () {
      const total = Math.ceil(fileData.length / U.BLE_CHUNK_SIZE);
      let index = 0;
      let lastPct = -1;
      const next = function () {
        if (abortFlag) throw new Error('已取消');
        if (index >= total) return Promise.resolve();
        const start = index * U.BLE_CHUNK_SIZE;
        const end = Math.min(start + U.BLE_CHUNK_SIZE, fileData.length);
        const chunk = fileData.subarray ? fileData.subarray(start, end) : fileData.slice(start, end);
        const packet = U.buildFileDataPacket(chunk);
        index += 1;
        return sendPacket(packet, U.BLE_DATA_DELAY).then(function () {
          const pct = Math.round(index / total * 100);
          if (onProgress && pct !== lastPct) {
            lastPct = pct;
            onProgress(pct);
          }
          return next();
        });
      };
      return next();
    })
    .then(function () {
      return send(U.BLE_FILM_TRANS_CH_FILE_STOP);
    });
}

function uploadOta(fileData, onProgress) {
  abortFlag = false;
  const lenPacket = (function () {
    const p = new Uint8Array(8);
    p[0] = U.BLE_CMD_HEAD;
    p[1] = U.BLE_FILM_TRANS_CH_OTA_LEN;
    p[2] = 4;
    p[3] = (fileData.length >> 24) & 0xFF;
    p[4] = (fileData.length >> 16) & 0xFF;
    p[5] = (fileData.length >> 8) & 0xFF;
    p[6] = fileData.length & 0xFF;
    p[7] = U.calculateChecksum(p, 7);
    return p;
  })();

  // OTA_LEN 会同步擦除分区，必须等设备就绪
  return sendPacket(lenPacket, 5000).then(function () {
    const total = Math.ceil(fileData.length / U.BLE_CHUNK_SIZE);
    let index = 0;
    let lastPct = -1;
    const next = function () {
      if (abortFlag) throw new Error('已取消');
      if (index >= total) return Promise.resolve();
      const start = index * U.BLE_CHUNK_SIZE;
      const end = Math.min(start + U.BLE_CHUNK_SIZE, fileData.length);
      const chunk = fileData.subarray ? fileData.subarray(start, end) : fileData.slice(start, end);
      const packet = new Uint8Array(4 + chunk.length);
      packet[0] = U.BLE_CMD_HEAD;
      packet[1] = U.BLE_FILM_TRANS_CH_OTA_DATA;
      packet[2] = chunk.length;
      packet.set(chunk, 3);
      packet[packet.length - 1] = U.calculateChecksum(packet, packet.length - 1);
      index += 1;
      return sendPacket(packet, U.BLE_DATA_DELAY).then(function () {
        const pct = Math.round(index / total * 100);
        if (onProgress && pct !== lastPct) {
          lastPct = pct;
          onProgress(pct);
        }
        return next();
      });
    };
    return next();
  }).then(function () {
    return send(U.BLE_FILM_TRANS_CH_OTA_STOP);
  });
}

module.exports = {
  init,
  onResponse,
  onDisconnect,
  startScan,
  stopScan,
  connect,
  disconnect,
  isConnected,
  reconnectLast,
  downloadTest,
  send,
  sendString,
  sendPacket,
  refreshFileList,
  uploadFilm,
  uploadOta,
  cancelTransfer,
  normalizeFileName,
  detectModel
};
