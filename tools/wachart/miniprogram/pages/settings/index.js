const bleUtils = require('../../utils/ble-utils');
const app = getApp();

Page({
  data: {
    isConnected: false,
    batteryLevel: 0,
    batteryFillWidth: 0, // 电池填充宽度 rpx (最大42rpx)
    fileList: [],
    currentDisplayFileId: -1,
    selectedFileId: -1,
    photoMode: 0,
    sleepOnOff: false,
    autoWakeMode: false,
    wakeDuration: 60,
    wakeDurationText: '1小时',
    showDebug: false,
    debugLogs: [],
    otaFileName: '',
    otaFileSize: 0,
    otaFileData: null,
    showOtaTransfer: false,
    otaTransferStatus: '',
    otaTransferProgress: 0
  },

  _syncTimer: null,
  _fileListBuffer: [],

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 3 });
    }
    this._syncFromGlobal();
    this._startSyncTimer();
    this._setupBleListener();
  },

  onHide: function () {
    this._stopSyncTimer();
    this._removeBleListener();
  },

  onUnload: function () {
    this._stopSyncTimer();
    this._removeBleListener();
  },

  // 同步 globalData 状态到页面
  _syncFromGlobal: function () {
    var g = app.globalData;
    var updates = {};
    if (this.data.isConnected !== g.isConnected) updates.isConnected = g.isConnected;
    if (this.data.batteryLevel !== g.batteryLevel) {
      updates.batteryLevel = g.batteryLevel;
      updates.batteryFillWidth = Math.round((g.batteryLevel / 100) * 46);
    }
    if (this.data.currentDisplayFileId !== g.currentDisplayFileId) updates.currentDisplayFileId = g.currentDisplayFileId;
    if (this.data.photoMode !== g.photoMode) updates.photoMode = g.photoMode;
    if (this.data.sleepOnOff !== g.sleepOnOff) updates.sleepOnOff = !!g.sleepOnOff;
    if (this.data.autoWakeMode !== g.autoWakeMode) updates.autoWakeMode = !!g.autoWakeMode;
    if (this.data.wakeDuration !== g.wakeDuration) {
      updates.wakeDuration = g.wakeDuration;
      updates.wakeDurationText = bleUtils.formatDuration(g.wakeDuration);
    }
    // fileList 同步 - 直接使用拷贝避免引用共享
    var globalList = g.fileList || [];
    if (this.data.fileList.length !== globalList.length) {
      updates.fileList = globalList.slice();
    }
    if (Object.keys(updates).length > 0) {
      this.setData(updates);
    }
  },

  _startSyncTimer: function () {
    var that = this;
    that._syncTimer = setInterval(function () {
      that._syncFromGlobal();
    }, 1000);
  },

  _stopSyncTimer: function () {
    if (this._syncTimer) {
      clearInterval(this._syncTimer);
      this._syncTimer = null;
    }
  },

  // BLE 数据监听（使用全局监听器）
  _bleListener: null,

  _setupBleListener: function () {
    var that = this;
    this._bleListener = function (resp) {
      that._handleBleData(resp);
    };
    app.registerBleDataListener(this._bleListener);
  },

  _removeBleListener: function () {
    if (this._bleListener) {
      app.unregisterBleDataListener(this._bleListener);
      this._bleListener = null;
    }
  },

  _handleBleData: function (resp) {
    var cmdType = resp.cmdType;
    var data = resp.data;
    var cmdLen = resp.cmdLen;

    this.debugLog('收到 BLE 数据: cmd=0x' + cmdType.toString(16).toUpperCase() + ' len=' + cmdLen, 'info');

    switch (cmdType) {
      case bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD: // 0x23 电池电量
        if (cmdLen >= 1) {
          var level = data[3];
          var fillW = Math.round((level / 100) * 46);
          this.setData({ batteryLevel: level, batteryFillWidth: fillW });
          app.globalData.batteryLevel = level;
          this.debugLog('电池电量: ' + level + '%', 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_FILE_LIST: // 0x06 文件列表
        this._handleFileListEntry(data, cmdLen);
        break;

      case bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET: // 0x08 当前显示文件
        if (cmdLen >= 2) {
          var fileId = (data[3] << 8) | data[4];
          this.setData({ currentDisplayFileId: fileId });
          app.globalData.currentDisplayFileId = fileId;
          this.debugLog('当前显示文件 ID: ' + fileId, 'success');
        } else if (cmdLen >= 1) {
          var fileId = data[3];
          this.setData({ currentDisplayFileId: fileId });
          app.globalData.currentDisplayFileId = fileId;
          this.debugLog('当前显示文件 ID: ' + fileId, 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE_GET: // 0x21 照片模式
        if (cmdLen >= 1) {
          var mode = data[3];
          this.setData({ photoMode: mode });
          app.globalData.photoMode = mode;
          this.debugLog('照片模式: ' + (mode === 1 ? '自动' : '手动'), 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET: // 0x26 休眠开关
        if (cmdLen >= 1) {
          var on = !!data[3];
          this.setData({ sleepOnOff: on });
          app.globalData.sleepOnOff = data[3];
          this.debugLog('休眠开关: ' + (on ? '开启' : '关闭'), 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET: // 0x28 自动唤醒模式
        if (cmdLen >= 1) {
          var wake = !!data[3];
          this.setData({ autoWakeMode: wake });
          app.globalData.autoWakeMode = data[3];
          this.debugLog('自动唤醒: ' + (wake ? '开启' : '关闭'), 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET: // 0x2A 唤醒时长
        if (cmdLen >= 2) {
          var minutes = (data[3] << 8) | data[4];
          var text = bleUtils.formatDuration(minutes);
          this.setData({ wakeDuration: minutes, wakeDurationText: text });
          app.globalData.wakeDuration = minutes;
          this.debugLog('唤醒时长: ' + text, 'success');
        }
        break;
    }
  },

  _handleFileListEntry: function (data, cmdLen) {
    if (cmdLen < 3) return;
    var fileId = data[3];
    var nameLen = data[4];
    if (nameLen <= 0 || 5 + nameLen > data.length) return;
    var nameBytes = data.slice(5, 5 + nameLen);
    var name = '';
    for (var i = 0; i < nameBytes.length; i++) {
      if (nameBytes[i] === 0) break;
      name += String.fromCharCode(nameBytes[i]);
    }

    // 检查是否已存在
    var found = false;
    for (var i = 0; i < this._fileListBuffer.length; i++) {
      if (this._fileListBuffer[i].fileId === fileId) {
        this._fileListBuffer[i].name = name;
        found = true;
        break;
      }
    }
    if (!found) {
      this._fileListBuffer.push({ fileId: fileId, name: name });
    }

    // 更新到页面和 globalData
    this._fileListBuffer.sort(function (a, b) { return a.fileId - b.fileId; });
    this.setData({ fileList: this._fileListBuffer.slice() });
    app.globalData.fileList = this._fileListBuffer.slice();
    this.debugLog('文件: #' + fileId + ' ' + name, 'info');
  },

  // 文件管理
  refreshFileList: function () {
    var that = this;
    that.debugLog('刷新文件列表...', 'info');
    app.globalData.fileList = [];
    that.setData({ fileList: [] });
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_LIST, null).then(function () {
      that.debugLog('文件列表请求已发送', 'success');
      // 防抖：等待文件列表接收完成后再查询显示状态
      that._waitForFileListDone(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null);
      });
    }).catch(function (err) {
      that.debugLog('请求失败: ' + JSON.stringify(err), 'error');
    });
  },

  // 点击电量图标刷新
  refreshBattery: function () {
    this.debugLog('刷新电量...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD, null).catch(function (e) {
      console.error('refreshBattery fail', e);
    });
  },

  // 防抖等待文件列表接收完成
  _waitForFileListDone: function (callback) {
    var lastCount = -1;
    var maxWait = 30000;
    var startTime = Date.now();
    function check() {
      var currentCount = (app.globalData.fileList || []).length;
      if (currentCount > 0 && currentCount === lastCount) {
        callback();
        return;
      }
      if (Date.now() - startTime > maxWait) {
        callback();
        return;
      }
      lastCount = currentCount;
      setTimeout(check, 1000);
    }
    check();
  },

  onFileSelect: function (e) {
    var fileId = e.currentTarget.dataset.fileId;
    this.setData({ selectedFileId: fileId });
    this.debugLog('选中文件 #' + fileId, 'info');
  },

  deleteSelectedFile: function () {
    var that = this;
    var fileId = that.data.selectedFileId;
    if (fileId < 0) return;
    wx.showModal({
      title: '确认删除',
      content: '确定要删除文件 #' + fileId + ' 吗？',
      success: function (res) {
        if (res.confirm) {
          that.debugLog('删除文件 #' + fileId + '...', 'info');
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DELETE, fileId).then(function () {
            that.debugLog('删除命令已发送', 'success');
            that.setData({ selectedFileId: -1 });
            setTimeout(function () { that.refreshFileList(); }, 500);
          }).catch(function (err) {
            that.debugLog('删除失败: ' + JSON.stringify(err), 'error');
          });
        }
      }
    });
  },

  selectDisplayFile: function () {
    var that = this;
    var fileId = that.data.selectedFileId;
    if (fileId < 0) return;
    that.debugLog('设置显示文件 #' + fileId + '...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY, fileId).then(function () {
      that.debugLog('显示命令已发送', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null);
      }, 500);
    }).catch(function (err) {
      that.debugLog('显示失败: ' + JSON.stringify(err), 'error');
    });
  },

  // 照片模式
  setAutoMode: function () {
    var that = this;
    that.debugLog('设置自动模式...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE, 1).then(function () {
      that.debugLog('自动模式已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE_GET, null);
      }, 300);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  setManualMode: function () {
    var that = this;
    that.debugLog('设置手动模式...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE, 0).then(function () {
      that.debugLog('手动模式已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE_GET, null);
      }, 300);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  // 休眠设置
  onSleepSwitchChange: function (e) {
    var that = this;
    var val = e.detail.value ? 1 : 0;
    that.debugLog('休眠开关: ' + (val ? '开启' : '关闭'), 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF, val).then(function () {
      that.debugLog('休眠开关已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET, null);
      }, 300);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  onAutoWakeSwitchChange: function (e) {
    var that = this;
    var val = e.detail.value ? 1 : 0;
    that.debugLog('自动唤醒: ' + (val ? '开启' : '关闭'), 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE, val).then(function () {
      that.debugLog('自动唤醒已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET, null);
      }, 300);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  // 滑块拖动时实时更新显示
  onWakeDurationChanging: function (e) {
    var minutes = e.detail.value;
    var text = bleUtils.formatDuration(minutes);
    this.setData({ wakeDuration: minutes, wakeDurationText: text });
  },

  onWakeDurationChange: function (e) {
    var that = this;
    var minutes = e.detail.value;
    var text = bleUtils.formatDuration(minutes);
    that.setData({ wakeDuration: minutes, wakeDurationText: text });
    that.debugLog('唤醒时长: ' + text + ' (' + minutes + '分钟)', 'info');
    var packet = bleUtils.buildSleepTimePacket(minutes);
    app.sendBlePacket(packet).then(function () {
      that.debugLog('唤醒时长已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET, null);
      }, 300);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  // 设备控制
  reboot: function () {
    var that = this;
    wx.showModal({
      title: '确认重启',
      content: '确定要重启设备吗？',
      success: function (res) {
        if (res.confirm) {
          that.debugLog('发送重启命令...', 'info');
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_REBOOT, null).then(function () {
            that.debugLog('重启命令已发送', 'success');
            wx.showToast({ title: '设备正在重启', icon: 'success' });
          }).catch(function (err) {
            that.debugLog('重启失败: ' + JSON.stringify(err), 'error');
          });
        }
      }
    });
  },

  reset: function () {
    var that = this;
    wx.showModal({
      title: '恢复出厂设置',
      content: '此操作将清除所有数据，确定继续吗？',
      confirmColor: '#c62828',
      success: function (res) {
        if (res.confirm) {
          that.debugLog('发送恢复出厂命令...', 'info');
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_RESET, null).then(function () {
            that.debugLog('恢复出厂命令已发送', 'success');
            wx.showToast({ title: '设备正在恢复', icon: 'success' });
          }).catch(function (err) {
            that.debugLog('恢复失败: ' + JSON.stringify(err), 'error');
          });
        }
      }
    });
  },

  toggleDebug: function () {
    this.setData({ showDebug: !this.data.showDebug });
  },

  debugLog: function (text, type) {
    var logs = this.data.debugLogs.slice();
    var now = new Date();
    var time = now.getHours().toString().padStart(2, '0') + ':' +
               now.getMinutes().toString().padStart(2, '0') + ':' +
               now.getSeconds().toString().padStart(2, '0');
    logs.push({ text: '[' + time + '] ' + text, type: type || 'info' });
    if (logs.length > 200) logs = logs.slice(-200);
    this.setData({ debugLogs: logs });
  },

  // OTA
  chooseOtaFile: function () {
    var that = this;
    wx.chooseMessageFile({
      count: 1,
      type: 'file',
      extension: ['.bin'],
      success: function (res) {
        var file = res.tempFiles[0];
        that.setData({
          otaFileName: file.name,
          otaFileSize: file.size,
          otaFileData: null
        });
        that.debugLog('选择固件: ' + file.name + ' (' + file.size + ' 字节)', 'info');

        var fs = wx.getFileSystemManager();
        fs.readFile({
          filePath: file.path,
          success: function (readRes) {
            that.setData({ otaFileData: readRes.data });
            that.debugLog('固件已读取', 'success');
          },
          fail: function (err) {
            that.debugLog('读取文件失败: ' + JSON.stringify(err), 'error');
          }
        });
      }
    });
  },

  startOtaUpgrade: function () {
    var that = this;
    var fileData = that.data.otaFileData;
    if (!fileData) {
      wx.showToast({ title: '请先选择固件', icon: 'none' });
      return;
    }

    var otaData = new Uint8Array(fileData);
    var totalSize = otaData.length;
    var chunkSize = bleUtils.BLE_CHUNK_SIZE;
    var totalChunks = Math.ceil(totalSize / chunkSize);

    that.setData({
      showOtaTransfer: true,
      otaTransferStatus: '准备升级...',
      otaTransferProgress: 0
    });
    that.debugLog('开始 OTA 升级，总大小: ' + totalSize + ' 字节，分 ' + totalChunks + ' 包', 'info');

    // 1. 发送 OTA_LEN
    var lenPacket = new Uint8Array(8);
    lenPacket[0] = bleUtils.BLE_CMD_HEAD;
    lenPacket[1] = bleUtils.BLE_FILM_TRANS_CH_OTA_LEN;
    lenPacket[2] = 4;
    lenPacket[3] = (totalSize >> 24) & 0xFF;
    lenPacket[4] = (totalSize >> 16) & 0xFF;
    lenPacket[5] = (totalSize >> 8) & 0xFF;
    lenPacket[6] = totalSize & 0xFF;
    lenPacket[7] = bleUtils.calculateChecksum(lenPacket, 7);

    app.sendBlePacket(lenPacket).then(function () {
      that.debugLog('OTA_LEN 已发送', 'success');
      return app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_OTA_START, null);
    }).then(function () {
      that.debugLog('OTA_START 已发送，开始传输数据...', 'info');
      that.setData({ otaTransferStatus: '传输中...' });
      return that._sendOtaChunks(otaData, chunkSize, totalChunks);
    }).then(function () {
      that.debugLog('数据传输完成，发送 OTA_STOP...', 'info');
      return app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_OTA_STOP, null);
    }).then(function () {
      that.setData({
        otaTransferStatus: '升级完成',
        otaTransferProgress: 100
      });
      that.debugLog('OTA 升级完成！', 'success');
      wx.showToast({ title: '升级完成', icon: 'success' });
    }).catch(function (err) {
      that.setData({ otaTransferStatus: '升级失败' });
      that.debugLog('OTA 失败: ' + JSON.stringify(err), 'error');
      wx.showToast({ title: '升级失败', icon: 'none' });
    });
  },

  _sendOtaChunks: function (otaData, chunkSize, totalChunks) {
    var that = this;
    var offset = 0;
    var index = 0;

    function sendNext() {
      if (offset >= otaData.length) return Promise.resolve();

      var end = Math.min(offset + chunkSize, otaData.length);
      var chunk = otaData.slice(offset, end);
      var packet = new Uint8Array(4 + chunk.length);
      packet[0] = bleUtils.BLE_CMD_HEAD;
      packet[1] = bleUtils.BLE_FILM_TRANS_CH_OTA_DATA;
      packet[2] = chunk.length;
      packet.set(chunk, 3);
      packet[packet.length - 1] = bleUtils.calculateChecksum(packet, packet.length - 1);

      offset = end;
      index++;
      var progress = Math.round((index / totalChunks) * 100);

      return app.sendBlePacket(packet).then(function () {
        if (index % 10 === 0 || progress >= 100) {
          that.setData({
            otaTransferProgress: progress,
            otaTransferStatus: '传输中... ' + progress + '%'
          });
          that.debugLog('OTA 进度: ' + progress + '% (' + index + '/' + totalChunks + ')', 'info');
        }
        return sendNext();
      });
    }

    return sendNext();
  }
});
