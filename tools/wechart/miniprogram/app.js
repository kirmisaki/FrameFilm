const bleUtils = require('./utils/ble-utils');

App({
  globalData: {
    deviceId: '',
    deviceName: '',
    deviceType: 'FRAMEFILM',
    isConnected: false,
    batteryLevel: 0,
    fileList: [],
    currentDisplayFileId: -1,
    photoMode: 0,
    sleepOnOff: 0,
    autoWakeMode: 0,
    wakeDuration: 60,
    _bleQueue: [],
    _bleProcessing: false,
    _deviceId: '',
    _serviceId: '',
    _characteristicId: '',
    _bleDataListeners: []
  },

  onLaunch: function () {
    // 全局BLE数据监听器在蓝牙连接成功后由bluetooth页面注册
    this._globalBleDataHandler = this._handleGlobalBleData.bind(this);
  },

  // 在蓝牙通知启用后调用此方法注册全局监听
  startBleNotify: function () {
    var that = this;
    if (typeof wx.onBLECharacteristicValueChanged === 'function') {
      wx.onBLECharacteristicValueChanged(that._globalBleDataHandler);
      console.log('BLE全局监听已注册(onBLECharacteristicValueChanged)');
    } else if (typeof wx.onBLECharacteristicValueChange === 'function') {
      // 兼容不同基础库版本
      wx.onBLECharacteristicValueChange(that._globalBleDataHandler);
      console.log('BLE全局监听已注册(onBLECharacteristicValueChange)');
    } else {
      console.warn('BLE特征值监听API不可用，尝试使用readBLECharacteristicValue轮询');
      that._startBlePolling();
    }
  },

  // 备用方案：通过轮询读取特征值
  _startBlePolling: function () {
    var that = this;
    if (that._blePollTimer) clearInterval(that._blePollTimer);
    that._blePollTimer = setInterval(function () {
      if (!that.globalData._deviceId) return;
      wx.readBLECharacteristicValue({
        deviceId: that.globalData._deviceId,
        serviceId: that.globalData._serviceId,
        characteristicId: that.globalData._characteristicId,
        success: function (res) {
          if (res.value) {
            that._globalBleDataHandler({ value: res.value });
          }
        },
        fail: function () {}
      });
    }, 200);
  },

  // 断开连接时移除监听
  stopBleNotify: function () {
    if (this._blePollTimer) {
      clearInterval(this._blePollTimer);
      this._blePollTimer = null;
    }
    try {
      if (typeof wx.offBLECharacteristicValueChanged === 'function') {
        wx.offBLECharacteristicValueChanged(this._globalBleDataHandler);
      } else if (typeof wx.offBLECharacteristicValueChange === 'function') {
        wx.offBLECharacteristicValueChange(this._globalBleDataHandler);
      }
    } catch (e) {}
  },

  // 注册BLE数据回调（各页面可在onShow注册，onHide移除）
  registerBleDataListener: function (listener) {
    var listeners = this.globalData._bleDataListeners;
    if (listeners.indexOf(listener) === -1) {
      listeners.push(listener);
    }
  },

  unregisterBleDataListener: function (listener) {
    var listeners = this.globalData._bleDataListeners;
    var idx = listeners.indexOf(listener);
    if (idx !== -1) {
      listeners.splice(idx, 1);
    }
  },

  // 全局BLE数据处理 - 更新globalData并通知所有监听器
  _handleGlobalBleData: function (res) {
    var resp = bleUtils.parseBleResponse(res.value);
    if (!resp) return;

    var cmdType = resp.cmdType;
    var cmdLen = resp.cmdLen;
    var data = resp.data;

    switch (cmdType) {
      case bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD: // 0x23
        if (cmdLen === 1) {
          this.globalData.batteryLevel = data[3];
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_FILE_LIST: // 0x06
        if (cmdLen >= 2) {
          var fileId = data[3];
          var nameLen = data[4];
          if (nameLen > 0 && 5 + nameLen <= data.length) {
            var fileName = '';
            for (var i = 0; i < nameLen; i++) {
              if (data[5 + i] === 0) break;
              fileName += String.fromCharCode(data[5 + i]);
            }
            var fileList = this.globalData.fileList || [];
            var found = false;
            for (var j = 0; j < fileList.length; j++) {
              if (fileList[j].fileId === fileId) {
                fileList[j].name = fileName;
                found = true;
                break;
              }
            }
            if (!found) {
              fileList.push({ fileId: fileId, name: fileName });
            }
            this.globalData.fileList = fileList;
          }
          // 文件列表高频到达，不触发页面监听器，由防抖定时器同步
          return;
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET: // 0x08
        if (cmdLen === 1) {
          this.globalData.currentDisplayFileId = data[3];
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE_GET: // 0x21
        if (cmdLen === 1) {
          this.globalData.photoMode = data[3];
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET: // 0x26
        if (cmdLen === 1) {
          this.globalData.sleepOnOff = data[3];
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET: // 0x28
        if (cmdLen === 1) {
          this.globalData.autoWakeMode = data[3];
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET: // 0x2A
        if (cmdLen === 2) {
          this.globalData.wakeDuration = (data[3] << 8) | data[4];
        }
        break;
    }

    // 通知所有页面级监听器
    var listeners = this.globalData._bleDataListeners;
    for (var k = 0; k < listeners.length; k++) {
      try {
        listeners[k](resp);
      } catch (e) {
        console.error('BLE监听器错误:', e);
      }
    }
  },

  // BLE 写入队列
  queueBleWrite: function (fn) {
    var that = this;
    return new Promise(function (resolve, reject) {
      that.globalData._bleQueue.push({ fn: fn, resolve: resolve, reject: reject });
      if (!that.globalData._bleProcessing) {
        that._processBleQueue();
      }
    });
  },

  _processBleQueue: function () {
    var that = this;
    if (that.globalData._bleQueue.length === 0) {
      that.globalData._bleProcessing = false;
      return;
    }
    that.globalData._bleProcessing = true;
    var item = that.globalData._bleQueue[0];
    item.fn().then(item.resolve).catch(item.reject).finally(function () {
      that.globalData._bleQueue.shift();
      that._processBleQueue();
    });
  },

  // 确保获取正确的 ArrayBuffer
  _getArrayBuffer: function (packet) {
    if (packet.byteOffset === 0 && packet.byteLength === packet.buffer.byteLength) {
      return packet.buffer;
    }
    return packet.buffer.slice(packet.byteOffset, packet.byteOffset + packet.byteLength);
  },

  sendBleCmd: function (cmdType, data) {
    var that = this;
    return that.queueBleWrite(function () {
      return new Promise(function (resolve, reject) {
        if (!that.globalData._deviceId) {
          reject(new Error('未连接设备'));
          return;
        }
        var packet = bleUtils.buildCmdPacket(cmdType, data);
        if (!packet) { reject(new Error('无效的数据包')); return; }
        var buf = that._getArrayBuffer(packet);
        console.log('发送BLE命令: cmd=0x' + cmdType.toString(16) +
          ' data=' + Array.from(packet).map(function(b) { return b.toString(16).padStart(2, '0'); }).join(' '));
        wx.writeBLECharacteristicValue({
          deviceId: that.globalData._deviceId,
          serviceId: that.globalData._serviceId,
          characteristicId: that.globalData._characteristicId,
          value: buf,
          success: function () {
            console.log('BLE写入成功: 0x' + cmdType.toString(16));
            setTimeout(resolve, bleUtils.BLE_CTRL_DELAY);
          },
          fail: function (err) {
            console.error('BLE写入失败: 0x' + cmdType.toString(16), err);
            reject(err);
          }
        });
      });
    });
  },

  sendBlePacket: function (packet) {
    var that = this;
    return that.queueBleWrite(function () {
      return new Promise(function (resolve, reject) {
        if (!that.globalData._deviceId) {
          reject(new Error('未连接设备'));
          return;
        }
        var buf = that._getArrayBuffer(packet);
        wx.writeBLECharacteristicValue({
          deviceId: that.globalData._deviceId,
          serviceId: that.globalData._serviceId,
          characteristicId: that.globalData._characteristicId,
          value: buf,
          success: function () { resolve(); },
          fail: function (err) { reject(err); }
        });
      });
    });
  }
});
