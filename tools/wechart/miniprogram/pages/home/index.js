// 首页 - 设备状态 + 一键发送
var app = getApp();
var recentUtils = require('../../utils/recent-utils');
var filmUtils = require('../../utils/film-utils');
var bleUtils = require('../../utils/ble-utils');

// 缩略图缓存（key: name|time），避免每次 onShow 重复渲染
var thumbCache = {};

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

Page({
  data: {
    isConnected: false,
    deviceName: '',
    deviceTypeText: '',
    batteryLevel: 0,
    currentFile: '',
    devThumb: '',
    monthName: '',
    recentList: [],
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0
  },

  // 最近上墙原始记录（含 film 数据），仅供点击时使用，不进入 setData
  _recentRaw: [],
  _disconnectTimer: null,

  _bleListener: null,

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 0 });
    }
    var that = this;
    this._syncFromGlobal();
    // 已连接时主动刷新当前显示与文件列表，保证设备框「显示中」同步
    // （设备自动加载新文件后不会主动推送，需客户端查询）
    if (app.globalData.isConnected) {
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_LIST, null).catch(function () {});
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null).catch(function () {});
    }
    // 注册 BLE 监听，实时刷新电量/当前显示文件
    this._bleListener = function () {
      that._syncFromGlobal();
    };
    app.registerBleDataListener(this._bleListener);
  },

  onHide: function () {
    this._removeBleListener();
    this._stopDisconnectWatch();
  },

  onUnload: function () {
    this._removeBleListener();
    this._stopDisconnectWatch();
  },

  _removeBleListener: function () {
    if (this._bleListener) {
      app.unregisterBleDataListener(this._bleListener);
      this._bleListener = null;
    }
  },

  _syncFromGlobal: function () {
    var g = app.globalData;
    var currentFile = '';
    var list = g.fileList || [];
    for (var i = 0; i < list.length; i++) {
      if (list[i].fileId === g.currentDisplayFileId) {
        currentFile = list[i].name;
        break;
      }
    }
    var devThumb = currentFile ? (thumbCache['dev:' + currentFile] || '') : '';
    if (g.isConnected && currentFile && !devThumb) {
      this._renderDevThumb(currentFile);
    }
    var recentRaw = recentUtils.getRecent().slice(0, 6);
    this._recentRaw = recentRaw;
    var recentList = [];
    for (var j = 0; j < recentRaw.length; j++) {
      var item = recentRaw[j];
      recentList.push({
        name: item.name,
        time: item.time,
        thumb: thumbCache[item.name + '|' + item.time] || ''
      });
    }
    this.setData({
      isConnected: g.isConnected,
      deviceName: g.deviceName || 'FrameFilm',
      deviceTypeText: g.deviceType === 'FRAMEFILMPRO' ? 'FrameFilm Pro' : 'FrameFilm',
      batteryLevel: g.batteryLevel,
      currentFile: currentFile,
      devThumb: devThumb,
      monthName: (new Date().getMonth() + 1) + '月日历',
      recentList: recentList
    });
    this._renderRecentThumbs(recentRaw);
  },

  // 渲染设备框当前显示文件的预览（从草稿/最近记录中匹配同名 film 数据）
  _renderDevThumb: function (name) {
    var that = this;
    var found = null;
    var drafts = recentUtils.getDrafts();
    for (var i = 0; i < drafts.length; i++) {
      if (drafts[i].name === name && drafts[i].data) {
        found = drafts[i];
        break;
      }
    }
    if (!found) {
      var recents = recentUtils.getRecent();
      for (var j = 0; j < recents.length; j++) {
        if (recents[j].name === name && recents[j].data) {
          found = recents[j];
          break;
        }
      }
    }
    if (!found) return;
    var fileData;
    try {
      fileData = new Uint8Array(wx.base64ToArrayBuffer(found.data));
    } catch (e) {
      return;
    }
    filmUtils.renderFilmThumbnail(fileData, function (path) {
      if (!path) return;
      thumbCache['dev:' + name] = path;
      that.setData({ devThumb: path });
    });
  },

  // 异步为最近上墙渲染 film 缩略图（带缓存）
  _renderRecentThumbs: function (rawList) {
    var that = this;
    for (var i = 0; i < rawList.length; i++) {
      (function (item) {
        if (!item || !item.data) return;
        var key = item.name + '|' + item.time;
        if (thumbCache[key]) return;
        var fileData;
        try {
          fileData = new Uint8Array(wx.base64ToArrayBuffer(item.data));
        } catch (e) {
          return;
        }
        filmUtils.renderFilmThumbnail(fileData, function (path) {
          if (!path) return;
          thumbCache[key] = path;
          var cur = that.data.recentList;
          for (var k = 0; k < cur.length; k++) {
            if (cur[k].name === item.name && cur[k].time === item.time) {
              cur[k].thumb = path;
              that.setData({ recentList: cur.slice() });
              break;
            }
          }
        });
      })(rawList[i]);
    }
  },

  // 点击设备卡 → 蓝牙连接页
  goBluetooth: function () {
    wx.navigateTo({ url: '/pages/bluetooth/index' });
  },

  // 发送照片 CTA → 相册上传
  sendPhoto: function () {
    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接设备', icon: 'none' });
      return;
    }
    wx.navigateTo({ url: '/pages/frame/upload/index' });
  },

  // 拍一张 CTA → 相机页
  goCamera: function () {
    wx.navigateTo({ url: '/pages/frame/camera/index' });
  },

  // 模板卡 → 模板中心
  goToTemplate: function () {
    wx.navigateTo({ url: '/pages/template/index' });
  },

  // 最近上墙点击：设备有同名文件直接切换显示，否则上传这一张
  tapRecent: function (e) {
    var that = this;
    var index = e.currentTarget.dataset.index;
    var item = (this._recentRaw && this._recentRaw[index]) || null;
    if (!item) return;
    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接设备', icon: 'none' });
      return;
    }
    if (!item.data) {
      wx.showToast({ title: '本地数据已失效，请重新制作', icon: 'none' });
      return;
    }
    var fileList = app.globalData.fileList || [];
    if (fileList.length === 0) {
      // 设备文件列表还没拉取过，先刷新再检索
      this._loadFileListThen(function (list) {
        that._switchOrUpload(item, list);
      });
    } else {
      this._switchOrUpload(item, fileList);
    }
  },

  // 拉取设备文件列表并等待收集完成
  _loadFileListThen: function (callback) {
    var g = app.globalData;
    g.fileList = [];
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_LIST, null).then(function () {
      var startTime = Date.now();
      var lastCount = -1;
      function check() {
        var count = (g.fileList || []).length;
        if (count > 0 && count === lastCount) {
          callback((g.fileList || []).slice());
          return;
        }
        if (Date.now() - startTime > 10000) {
          callback((g.fileList || []).slice());
          return;
        }
        lastCount = count;
        setTimeout(check, 800);
      }
      check();
    }).catch(function () {
      callback((g.fileList || []).slice());
    });
  },

  // 检索设备文件列表，有则切换显示，无则上传
  _switchOrUpload: function (item, fileList) {
    var that = this;
    var found = null;
    for (var i = 0; i < fileList.length; i++) {
      if (fileList[i].name === item.name) {
        found = fileList[i];
        break;
      }
    }
    if (found) {
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY, found.fileId).then(function () {
        wx.showToast({ title: '已切换显示', icon: 'success' });
        setTimeout(function () {
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null);
        }, 500);
      }).catch(function () {
        wx.showToast({ title: '切换失败', icon: 'none' });
      });
      return;
    }
    // 设备上没有 → 重新上传
    var fileData;
    try {
      fileData = new Uint8Array(wx.base64ToArrayBuffer(item.data));
    } catch (err) {
      wx.showToast({ title: '数据解析失败', icon: 'none' });
      return;
    }
    that.setData({ showTransfer: true, transferProgress: 0, transferStatus: '正在上传 ' + item.name + ' ...' });
    that._watchDisconnect();
    that._sendFile(fileData, item.name, function (pct) {
      that.setData({ transferProgress: pct });
    }).then(function () {
      that.setData({ transferProgress: 100, transferStatus: '上传完成' });
      setTimeout(function () {
        that._stopDisconnectWatch();
        that.setData({ showTransfer: false });
        wx.showToast({ title: '已上墙', icon: 'success' });
        // 作废列表缓存，下次点击重新拉取设备真实文件列表
        app.globalData.fileList = [];
      }, 1200);
    }).catch(function (err) {
      that._stopDisconnectWatch();
      that.setData({ showTransfer: false });
      wx.showToast({ title: '上传失败：' + (err.message || '未知错误'), icon: 'none' });
    });
  },

  // 上传期间轮询设备连接状态，断联时自动关闭弹窗
  _watchDisconnect: function () {
    var that = this;
    if (this._disconnectTimer) clearInterval(this._disconnectTimer);
    this._disconnectTimer = setInterval(function () {
      if (!that.data.showTransfer) {
        that._stopDisconnectWatch();
        return;
      }
      if (!app.globalData.isConnected) {
        that._stopDisconnectWatch();
        that.setData({ showTransfer: false });
        wx.showToast({ title: '设备已断开', icon: 'none' });
      }
    }, 500);
  },

  _stopDisconnectWatch: function () {
    if (this._disconnectTimer) {
      clearInterval(this._disconnectTimer);
      this._disconnectTimer = null;
    }
  },

  // BLE 四步传输（START→NAME→LEN→DATA→STOP）
  _sendFile: function (fileData, fileName, onProgress) {
    var that = this;
    var totalSize = fileData.length;
    var step = 0;
    var lastPct = -1;

    function update(extra) {
      var controlWeight = 0.05;
      var dataWeight = 0.95;
      var pct = Math.min(99, Math.floor((step / 4) * controlWeight * 100 + (extra / totalSize) * dataWeight * 100));
      if (pct !== lastPct) {
        lastPct = pct;
        if (onProgress) onProgress(pct);
      }
    }

    return app.sendBlePacket(bleUtils.buildFileStartPacket()).then(function () {
      step = 1; update(0);
      return delay(bleUtils.BLE_CTRL_DELAY);
    }).then(function () {
      return app.sendBlePacket(bleUtils.buildFileNamePacket(fileName));
    }).then(function () {
      step = 2; update(0);
      return delay(bleUtils.BLE_CTRL_DELAY);
    }).then(function () {
      return app.sendBlePacket(bleUtils.buildFileLenPacket(totalSize));
    }).then(function () {
      step = 3; update(0);
      return delay(bleUtils.BLE_CTRL_DELAY);
    }).then(function () {
      var offset = 0;
      function sendChunk() {
        if (offset >= totalSize) return Promise.resolve();
        var end = Math.min(offset + bleUtils.BLE_CHUNK_SIZE, totalSize);
        var chunk = fileData.slice(offset, end);
        return app.sendBlePacket(bleUtils.buildFileDataPacket(chunk)).then(function () {
          offset = end;
          update(offset);
          return delay(bleUtils.BLE_DATA_DELAY);
        }).then(sendChunk);
      }
      return sendChunk();
    }).then(function () {
      step = 4; update(0);
      return app.sendBlePacket(bleUtils.buildFileStopPacket());
    }).then(function () {
      if (onProgress) onProgress(100);
    });
  },

  // 清除最近上墙记录（带确认）
  clearRecent: function () {
    var that = this;
    wx.showModal({
      title: '清除记录',
      content: '确定清除全部「最近上墙」记录吗？',
      confirmText: '清除',
      confirmColor: '#FF6B6B',
      cancelText: '取消',
      success: function (res) {
        if (!res.confirm) return;
        recentUtils.clearRecent();
        that._recentRaw = [];
        // 清理缩略图缓存（保留设备预览缓存）
        var keys = Object.keys(thumbCache);
        for (var i = 0; i < keys.length; i++) {
          if (keys[i].indexOf('dev:') !== 0) {
            delete thumbCache[keys[i]];
          }
        }
        that.setData({ recentList: [] });
        wx.showToast({ title: '已清除', icon: 'none' });
      }
    });
  },

  // 分享给朋友
  onShareAppMessage: function () {
    return {
      title: 'FrameFilm - 电子纸冰箱贴',
      path: '/pages/home/index'
    };
  }
});
