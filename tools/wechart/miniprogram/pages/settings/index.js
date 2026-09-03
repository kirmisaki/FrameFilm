const bleUtils = require('../../utils/ble-utils');
const filmUtils = require('../../utils/film-utils');
const ditherConfig = require('../../utils/dither-config');
const app = getApp();

// 创作页默认转换算法：注册时按当前机型初始化（onShow 会按最新机型/已保存值刷新）
var ditherCreateInit = ditherConfig.ditherOptionsFor(filmUtils.getDeviceType());
var ditherCreateInitIndex = Math.max(0, ditherCreateInit.types.indexOf(ditherConfig.getCreateDitherType()));

Page({
  // 唤醒时长点位：60分钟前每10分钟一档，之后每60分钟一档
  _wakePoints: (function () {
    var pts = [];
    for (var i = 10; i <= 60; i += 10) pts.push(i);
    for (var i = 120; i <= 2880; i += 60) pts.push(i);
    return pts;
  })(),

  _durationToIndex: function (minutes) {
    var pts = this._wakePoints;
    var best = 0;
    var diff = Math.abs(pts[0] - minutes);
    for (var i = 1; i < pts.length; i++) {
      var d = Math.abs(pts[i] - minutes);
      if (d < diff) { diff = d; best = i; }
    }
    return best;
  },

  data: {
    isConnected: false,
    batteryLevel: 0,
    batteryFillWidth: 0, // 电池填充宽度 rpx (最大42rpx)
    fileList: [],
    currentDisplayFileId: -1,
    photoMode: 0,
    photoModeText: '关闭',
    sleepOnOff: false,
    autoWakeMode: false,
    wakeDuration: 60,
    wakeIndex: 5,  // 初始60分钟 = 第5个点
    wakeMaxIndex: 52,  // 最后一个点的索引
    wakeDurationText: '1小时',
    // WiFi 网络配置
    wifiEnable: false,
    wifiSsid: '',
    wifiPassword: '',
    filmApiUrl: '',
    heartbeatInterval: '60',
    wifiConnected: false,
    wifiStatusText: '未连接',
    networkExpanded: false,
    // 下载
    downloadState: 0,
    downloadProgress: 0,
    downloadStatusText: '',
    // 创作页默认转换算法
    ditherCreateTypes: ditherCreateInit.types,
    ditherCreateNames: ditherCreateInit.names,
    ditherCreateIndex: ditherCreateInitIndex,
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

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 3 });
    }
    this._syncFromGlobal();
    this._startSyncTimer();
    this._setupBleListener();
    // 刷新创作页默认转换算法（机型可能随 0x42 校准变化，已保存值也需同步到当前机型可选清单）
    this._refreshCreateDither();
    // 连接状态下查询WiFi配置
    if (app.globalData.isConnected) {
      this._queryWifiConfig();
    }
  },

  // 分享给朋友
  onShareAppMessage: function () {
    return {
      title: 'FrameFilm - 设置',
      path: '/pages/settings/index'
    };
  },

  onHide: function () {
    this._stopSyncTimer();
    this._removeBleListener();
    this._stopWifiStatusPoll();
    this._stopDownloadPoll();
    this._stopWifiEnablePoll();
  },

  onUnload: function () {
    this._stopSyncTimer();
    this._removeBleListener();
    this._stopWifiStatusPoll();
    this._stopDownloadPoll();
    this._stopWifiEnablePoll();
  },

  // 同步 globalData 状态到页面
  _syncFromGlobal: function () {
    var g = app.globalData;
    var updates = {};
    if (this.data.isConnected !== g.isConnected) {
      updates.isConnected = g.isConnected;
      // 断开连接时重置 OTA 升级状态
      if (!g.isConnected) {
        updates.otaFileName = '';
        updates.otaFileData = null;
        updates.showOtaTransfer = false;
        updates.otaTransferStatus = '';
        updates.otaTransferProgress = 0;
      }
    }
    if (this.data.batteryLevel !== g.batteryLevel) {
      updates.batteryLevel = g.batteryLevel;
      updates.batteryFillWidth = Math.round((g.batteryLevel / 100) * 46);
    }
    if (this.data.currentDisplayFileId !== g.currentDisplayFileId) updates.currentDisplayFileId = g.currentDisplayFileId;
    if (this.data.photoMode !== g.photoMode) {
      updates.photoMode = g.photoMode;
      updates.photoModeText = this._photoModeText(g.photoMode);
    }
    if (this.data.sleepOnOff !== g.sleepOnOff) updates.sleepOnOff = !!g.sleepOnOff;
    if (this.data.autoWakeMode !== g.autoWakeMode) updates.autoWakeMode = !!g.autoWakeMode;
    if (this.data.wakeDuration !== g.wakeDuration) {
      updates.wakeDuration = g.wakeDuration;
      updates.wakeIndex = this._durationToIndex(g.wakeDuration);
      updates.wakeDurationText = bleUtils.formatDuration(g.wakeDuration);
    }
    // WiFi 状态同步
    var wEnable = !!g.wifiEnable;
    if (this.data.wifiEnable !== wEnable) {
      updates.wifiEnable = wEnable;
      if (wEnable && !this.data.networkExpanded) updates.networkExpanded = true;
    }
    if (this.data.wifiSsid !== g.wifiSsid) updates.wifiSsid = g.wifiSsid;
    if (this.data.wifiPassword !== g.wifiPassword) updates.wifiPassword = g.wifiPassword;
    if (this.data.filmApiUrl !== g.filmApiUrl) updates.filmApiUrl = g.filmApiUrl;
    var hi = (g.heartbeatInterval || g.heartbeatInterval === 0) ? g.heartbeatInterval : 60;
    if (this.data.heartbeatInterval !== String(hi)) updates.heartbeatInterval = String(hi);
    var wConn = !!g.wifiConnected;
    if (this.data.wifiConnected !== wConn) {
      updates.wifiConnected = wConn;
      updates.wifiStatusText = wConn ? '已连接' : '未连接';
    }
    // 下载状态同步
    if (this.data.downloadState !== g.downloadState || this.data.downloadProgress !== g.downloadProgress) {
      updates.downloadState = g.downloadState;
      updates.downloadProgress = g.downloadProgress;
      updates.downloadStatusText = this._getDownloadStatusText(g.downloadState, g.downloadProgress);
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

  // 创作页默认转换算法：按当前机型可选清单 + 已保存默认值刷新下拉
  _refreshCreateDither: function () {
    var opts = ditherConfig.ditherOptionsFor(filmUtils.getDeviceType());
    var saved = ditherConfig.getCreateDitherType();
    var idx = opts.types.indexOf(saved);
    if (idx < 0) idx = Math.max(0, opts.types.indexOf(ditherConfig.DEFAULT_CREATE_DITHER_TYPE));
    var same = this.data.ditherCreateIndex === idx && this.data.ditherCreateTypes.length === opts.types.length;
    if (same) {
      for (var i = 0; i < opts.types.length; i++) {
        if (this.data.ditherCreateTypes[i] !== opts.types[i]) { same = false; break; }
      }
    }
    if (!same) {
      this.setData({
        ditherCreateTypes: opts.types,
        ditherCreateNames: opts.names,
        ditherCreateIndex: idx
      });
    }
  },

  // 选择创作页默认转换算法（相册/拍照/一言/绘梦生效，模板走独立 E6Pro 分层转换不受影响）
  onDitherCreateChange: function (e) {
    var idx = parseInt(e.detail.value, 10);
    var type = this.data.ditherCreateTypes[idx];
    if (!type) return;
    this.setData({ ditherCreateIndex: idx });
    ditherConfig.setCreateDitherType(type);
    var name = this.data.ditherCreateNames[idx];
    this.debugLog('默认转换算法: ' + name, 'success');
    wx.showToast({ title: '默认算法已设为 ' + name, icon: 'none' });
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
          this.setData({
            wakeDuration: minutes,
            wakeIndex: this._durationToIndex(minutes),
            wakeDurationText: text
          });
          app.globalData.wakeDuration = minutes;
          this.debugLog('唤醒时长: ' + text, 'success');
        }
        break;

      // WiFi 通知处理
      case bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET: // 0x31
        if (cmdLen >= 1) {
          var en = !!data[3];
          this.setData({ wifiEnable: en });
          app.globalData.wifiEnable = data[3];
          this.debugLog('WiFi: ' + (en ? '使能' : '禁用'), 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET: // 0x33
        if (cmdLen > 0) {
          var ssid = this._parseString(data, 3, cmdLen);
          this.setData({ wifiSsid: ssid });
          app.globalData.wifiSsid = ssid;
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET: // 0x35
        if (cmdLen > 0) {
          var pw = this._parseString(data, 3, cmdLen);
          this.setData({ wifiPassword: pw });
          app.globalData.wifiPassword = pw;
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET: // 0x37
        if (cmdLen > 0) {
          var furl = this._parseString(data, 3, cmdLen);
          this.setData({ filmApiUrl: furl });
          app.globalData.filmApiUrl = furl;
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET: // 0x41 心跳间隔
        if (cmdLen >= 1) {
          var hb = data[3];
          this.setData({ heartbeatInterval: String(hb) });
          app.globalData.heartbeatInterval = hb;
          this.debugLog('心跳间隔: ' + hb + ' 秒', 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET: // 0x3A
        if (cmdLen >= 1) {
          var conn = (data[3] === 1);
          this.setData({ wifiConnected: conn, wifiStatusText: conn ? '已连接' : '未连接' });
          app.globalData.wifiConnected = conn;
          if (conn) this._stopWifiStatusPoll();
          if (conn) this._stopWifiEnablePoll();
          this.debugLog('WiFi连接状态: ' + (conn ? '已连接' : '未连接'), 'success');
        }
        break;

      case bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE: // 0x3D
        if (cmdLen >= 2) {
          var ds = data[3];
          var dp = data[4];
          this.setData({
            downloadState: ds,
            downloadProgress: dp,
            downloadStatusText: this._getDownloadStatusText(ds, dp)
          });
          app.globalData.downloadState = ds;
          app.globalData.downloadProgress = dp;
          if (ds !== 1) this._stopDownloadPoll();
          this.debugLog('下载状态: ' + ds + ' 进度: ' + dp + '%', 'info');
        }
        break;
    }
  },

  // 点击电量图标刷新
  refreshBattery: function () {
    this.debugLog('刷新电量...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD, null).catch(function (e) {
      console.error('refreshBattery fail', e);
    });
  },

  // 辅助函数：从 BLE 数据中解析字符串
  _parseString: function (data, offset, len) {
    var str = '';
    for (var i = 0; i < len; i++) {
      if (data[offset + i] === 0) break;
      str += String.fromCharCode(data[offset + i]);
    }
    return str;
  },

  // 获取下载状态文本
  _getDownloadStatusText: function (state, progress) {
    switch (state) {
      case 0: return '就绪';
      case 1: return '下载中... ' + progress + '%';
      case 2: return '下载完成';
      case 3: return '下载失败';
      default: return '';
    }
  },

  // 照片模式
  _photoModeText: function (mode) {
    if (mode === 2) return 'WiFi轮播';
    if (mode === 1) return '本地轮播';
    return '关闭';
  },

  _setPhotoMode: function (modeValue, modeName) {
    var that = this;
    // WiFi 模式检查
    if (modeValue === 2 && !that.data.wifiEnable) {
      wx.showToast({ title: '请先启用WiFi', icon: 'none' });
      return;
    }
    that.debugLog('设置' + modeName + '模式...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE, modeValue).then(function () {
      that.debugLog(modeName + '模式已设置', 'success');
      that.setData({ photoMode: modeValue, photoModeText: modeName });
      app.globalData.photoMode = modeValue;
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  setWifiMode: function () {
    this._setPhotoMode(2, 'WiFi轮播');
  },

  setLocalMode: function () {
    this._setPhotoMode(1, '本地轮播');
  },

  setManualMode: function () {
    this._setPhotoMode(0, '关闭');
  },

  // 休眠设置
  onSleepSwitchChange: function (e) {
    var that = this;
    var val = e.detail.value ? 1 : 0;
    app.globalData.sleepOnOff = val;
    that.debugLog('休眠开关: ' + (val ? '开启' : '关闭'), 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF, val).then(function () {
      that.debugLog('休眠开关已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET, null);
      }, 500);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  onAutoWakeSwitchChange: function (e) {
    var that = this;
    var val = e.detail.value ? 1 : 0;
    app.globalData.autoWakeMode = val;
    that.debugLog('自动唤醒: ' + (val ? '开启' : '关闭'), 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE, val).then(function () {
      that.debugLog('自动唤醒已设置', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET, null);
      }, 500);
    }).catch(function (err) {
      that.debugLog('设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  // 唤醒时长滑块（固定点位数组）
  onWakeChanging: function (e) {
    var idx = e.detail.value;
    var pts = this._wakePoints;
    var minutes = pts[idx];
    this.setData({
      wakeIndex: idx,
      wakeDuration: minutes,
      wakeDurationText: bleUtils.formatDuration(minutes)
    });
  },

  onWakeChange: function (e) {
    var that = this;
    var idx = e.detail.value;
    var pts = that._wakePoints;
    var minutes = pts[idx];
    var text = bleUtils.formatDuration(minutes);
    that.setData({
      wakeIndex: idx,
      wakeDuration: minutes,
      wakeDurationText: text
    });
    // 立即更新 globalData，防止 _syncFromGlobal 用旧值覆盖
    app.globalData.wakeDuration = minutes;
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

  // 跳转到片单页管理设备照片
  goFilmlist: function () {
    wx.switchTab({ url: '/pages/filmlist/index' });
  },

  showHelp: function () {
    wx.showModal({
      title: '帮助与反馈',
      content: '使用中遇到问题，请查看 docs/knowledge 目录下的使用文档，或通过 GitHub Issues 反馈。',
      showCancel: false,
      confirmText: '知道了'
    });
  },

  showAbout: function () {
    wx.showModal({
      title: '关于 FrameFilm',
      content: 'FrameFilm v2.0\n开源彩色电子纸冰箱贴 · GPL-3.0\nBLE 传照片 · 本地草稿 · 批量上屏',
      showCancel: false,
      confirmText: '知道了'
    });
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

  // ==================== WiFi 网络配置 ====================

  toggleNetworkSection: function () {
    this.setData({ networkExpanded: !this.data.networkExpanded });
  },

  onWifiEnableChange: function (e) {
    var that = this;
    var enable = e.detail.value;
    that.setData({ wifiEnable: enable });
    app.globalData.wifiEnable = enable ? 1 : 0;
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE, enable ? 1 : 0).then(function () {
      that.debugLog('WiFi: ' + (enable ? '使能' : '禁用'), 'success');
      if (enable) {
        that.setData({ wifiStatusText: '初始化中...' });
        that._startWifiEnablePoll();
      } else {
        that._stopWifiEnablePoll();
        that._stopWifiStatusPoll();
        that.setData({ wifiStatusText: '已关闭', wifiConnected: false });
        app.globalData.wifiConnected = false;
        if (that.data.photoMode === 2) {
          that._setPhotoMode(1, '本地轮播');
        }
      }
    }).catch(function (err) {
      that.debugLog('WiFi设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  _wifiEnablePollTimer: null,
  _wifiEnablePollCount: 0,
  _WIFI_ENABLE_POLL_MAX: 20, // 20 * 500ms = 10s

  _startWifiEnablePoll: function () {
    var that = this;
    that._stopWifiEnablePoll();
    that._wifiEnablePollCount = 0;
    that._wifiEnablePollTimer = setInterval(function () {
      that._wifiEnablePollCount++;
      if (that._wifiEnablePollCount > that._WIFI_ENABLE_POLL_MAX) {
        that._stopWifiEnablePoll();
        if (!that.data.wifiConnected) {
          that.setData({ wifiStatusText: '未连接' });
        }
        return;
      }
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET, null).catch(function () {});
    }, 500);
  },

  _stopWifiEnablePoll: function () {
    if (this._wifiEnablePollTimer) {
      clearInterval(this._wifiEnablePollTimer);
      this._wifiEnablePollTimer = null;
    }
  },

  _queryWifiConfig: function () {
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET, null).catch(function () {});
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET, null).catch(function () {});
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET, null).catch(function () {});
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET, null).catch(function () {});
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET, null).catch(function () {});
  },

  applyWifiSsid: function (e) {
    var val = (e && e.detail && e.detail.value) ? e.detail.value.trim() : this.data.wifiSsid;
    if (!val) return;
    var that = this;
    that.setData({ wifiSsid: val });
    app.globalData.wifiSsid = val;
    app.sendBleStringCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_SSID, val, 64).then(function () {
      that.debugLog('SSID已设置: ' + val, 'success');
    }).catch(function (err) {
      that.debugLog('SSID设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  applyWifiPassword: function (e) {
    var val = (e && e.detail && e.detail.value) || this.data.wifiPassword;
    if (!val) return;
    var that = this;
    that.setData({ wifiPassword: val });
    app.globalData.wifiPassword = val;
    app.sendBleStringCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD, val, 64).then(function () {
      that.debugLog('密码已设置', 'success');
    }).catch(function (err) {
      that.debugLog('密码设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  applyFilmApiUrl: function (e) {
    var val = (e && e.detail && e.detail.value) ? e.detail.value.trim() : this.data.filmApiUrl;
    if (!val) return;
    var that = this;
    that.setData({ filmApiUrl: val });
    app.globalData.filmApiUrl = val;
    app.sendBleStringCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_API_URL, val, 128).then(function () {
      that.debugLog('API地址已设置', 'success');
    }).catch(function (err) {
      that.debugLog('API地址设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  applyHeartbeatInterval: function (e) {
    var that = this;
    var raw = (e && e.detail && e.detail.value) ? e.detail.value.trim() : '';
    if (!raw) {
      that.setData({ heartbeatInterval: String(app.globalData.heartbeatInterval || 60) });
      return;
    }
    var sec = parseInt(raw, 10);
    if (!(sec >= 5 && sec <= 180)) {
      wx.showToast({ title: '心跳间隔需在 5-180 秒之间', icon: 'none' });
      that.setData({ heartbeatInterval: String(app.globalData.heartbeatInterval || 60) });
      return;
    }
    that.setData({ heartbeatInterval: String(sec) });
    app.globalData.heartbeatInterval = sec;
    that.debugLog('设置心跳间隔: ' + sec + ' 秒', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL, sec).then(function () {
      that.debugLog('心跳间隔已发送，回读确认中…', 'success');
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL_GET, null).catch(function () {});
      }, 600);
    }).catch(function (err) {
      that.debugLog('心跳间隔设置失败: ' + JSON.stringify(err), 'error');
    });
  },

  onWifiConnect: function () {
    var that = this;
    that.debugLog('开始连接WiFi...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT, null).then(function () {
      that.setData({ wifiStatusText: '连接中...' });
      that._startWifiStatusPoll();
    }).catch(function (err) {
      that.debugLog('WiFi连接失败: ' + JSON.stringify(err), 'error');
    });
  },

  onWifiDisconnect: function () {
    var that = this;
    that._stopWifiStatusPoll();
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT, null).then(function () {
      that.setData({ wifiStatusText: '已断开', wifiConnected: false });
      app.globalData.wifiConnected = false;
    }).catch(function (err) {
      that.debugLog('WiFi断开失败: ' + JSON.stringify(err), 'error');
    });
  },

  onWifiClear: function () {
    var that = this;
    wx.showModal({
      title: '确认清除',
      content: '确定要清除所有网络配置吗？',
      success: function (res) {
        if (res.confirm) {
          that._stopWifiStatusPoll();
          that._stopDownloadPoll();
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR, null).then(function () {
            that.setData({
              wifiEnable: false, wifiSsid: '', wifiPassword: '',
              filmApiUrl: '', heartbeatInterval: '60', wifiConnected: false, wifiStatusText: '已清除'
            });
            app.globalData.wifiEnable = 0;
            app.globalData.wifiSsid = '';
            app.globalData.wifiPassword = '';
            app.globalData.filmApiUrl = '';
            app.globalData.heartbeatInterval = 60;
            app.globalData.wifiConnected = false;
            // WiFi模式切换到本地轮播
            if (that.data.photoMode === 2) {
              that._setPhotoMode(1, '本地轮播');
            }
            that.debugLog('网络配置已清除', 'success');
          }).catch(function (err) {
            that.debugLog('清除失败: ' + JSON.stringify(err), 'error');
          });
        }
      }
    });
  },

  // WiFi 状态轮询
  _wifiPollTimer: null,
  _wifiPollCount: 0,
  _WIFI_POLL_MAX: 60,

  _startWifiStatusPoll: function () {
    var that = this;
    that._stopWifiStatusPoll();
    that._wifiPollCount = 0;
    that._wifiPollTimer = setInterval(function () {
      that._wifiPollCount++;
      if (that._wifiPollCount > that._WIFI_POLL_MAX) {
        that._stopWifiStatusPoll();
        that.setData({ wifiStatusText: '连接超时', wifiConnected: false });
        app.globalData.wifiConnected = false;
        return;
      }
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET, null).catch(function (err) {
        console.error('WiFi状态查询失败:', err);
      });
    }, 500);
  },

  _stopWifiStatusPoll: function () {
    if (this._wifiPollTimer) {
      clearInterval(this._wifiPollTimer);
      this._wifiPollTimer = null;
    }
  },

  // ==================== 下载功能 ====================

  _downloadPollTimer: null,

  onFilmDownload: function () {
    var that = this;
    that.debugLog('开始下载测试...', 'info');
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD, null).then(function () {
      that.setData({ downloadStatusText: '下载中... 0%', downloadState: 1, downloadProgress: 0 });
      that._startDownloadPoll();
    }).catch(function (err) {
      that.debugLog('下载请求失败: ' + JSON.stringify(err), 'error');
    });
  },

  _startDownloadPoll: function () {
    var that = this;
    that._stopDownloadPoll();
    that._downloadPollTimer = setInterval(function () {
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE, null).catch(function (err) {
        console.error('下载状态查询失败:', err);
      });
    }, 1000);
  },

  _stopDownloadPoll: function () {
    if (this._downloadPollTimer) {
      clearInterval(this._downloadPollTimer);
      this._downloadPollTimer = null;
    }
  },

  // ==================== SD卡格式化 ====================

  sdFormat: function () {
    var that = this;
    wx.showModal({
      title: '确认格式化',
      content: '确定要格式化SD卡吗？此操作将清除SD卡上所有数据。',
      confirmColor: '#c62828',
      success: function (res) {
        if (res.confirm) {
          wx.showModal({
            title: '再次确认',
            content: '格式化后SD卡所有数据将永久丢失，确定继续吗？',
            confirmColor: '#c62828',
            success: function (res2) {
              if (res2.confirm) {
                that.debugLog('发送SD卡格式化命令...', 'info');
                app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SDRESET, null).then(function () {
                  that.debugLog('格式化命令已发送', 'success');
                  wx.showToast({ title: '设备正在格式化', icon: 'success' });
                }).catch(function (err) {
                  that.debugLog('格式化失败: ' + JSON.stringify(err), 'error');
                });
              }
            }
          });
        }
      }
    });
  },

  // ==================== OTA ====================
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

    // 1. 发送 OTA_LEN（固件端会自动进入 OTA 模式，无需再发 OTA_START）
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
      that.debugLog('OTA_LEN 已发送，等待设备初始化...', 'info');
      that.setData({ otaTransferStatus: '初始化中...' });
      return new Promise(function (resolve) {
        setTimeout(resolve, 5000);
      });
    }).then(function () {
      that.debugLog('开始传输数据...', 'info');
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
        return new Promise(function (resolve) {
          setTimeout(function () { resolve(sendNext()); }, 2);
        });
      });
    }

    return sendNext();
  }
});
