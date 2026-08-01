// 片单 - 设备文件管理 + 批量发送
var bleUtils = require('../../utils/ble-utils');
var recentUtils = require('../../utils/recent-utils');
var filmUtils = require('../../utils/film-utils');
var app = getApp();

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

Page({
  data: {
    fileList: [],
    currentDisplayFileId: -1,
    photoMode: 0,
    wifiEnable: false,
    isPro: false,
    batch: [],
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0,
    transferFailed: false,
    preview: false,
    previewImage: '',
    previewName: ''
  },

  _bleListener: null,
  _batchThumbCache: {},
  _disconnectTimer: null,

  onLoad: function () {
    // 片单缩略图/预览渲染按当前设备类型
    filmUtils.setDeviceType(app.globalData.deviceType || 'FRAMEFILM');
  },

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 2 });
    }
    var that = this;
    this._syncFromGlobal();
    this._bleListener = function () {
      that._syncFromGlobal();
    };
    app.registerBleDataListener(this._bleListener);
    if (app.globalData.isConnected) {
      this.refreshFileList();
    }
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
    filmUtils.setDeviceType(g.deviceType || 'FRAMEFILM');
    var that = this;
    this.setData({
      fileList: (g.fileList || []).slice(),
      currentDisplayFileId: g.currentDisplayFileId,
      photoMode: g.photoMode,
      wifiEnable: !!g.wifiEnable,
      isPro: g.deviceType === 'FRAMEFILMPRO',
      batch: recentUtils.getBatch()
    });
    this._renderBatchThumbs();
  },

  // 刷新设备文件列表
  refreshFileList: function () {
    var that = this;
    app.globalData.fileList = [];
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_LIST, null).then(function () {
      that._waitForFileListDone(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null);
      });
    }).catch(function () {});
  },

  _waitForFileListDone: function (callback) {
    var lastCount = -1;
    var maxWait = 30000;
    var startTime = Date.now();
    function check() {
      var count = (app.globalData.fileList || []).length;
      if (count > 0 && count === lastCount) {
        callback();
        return;
      }
      if (Date.now() - startTime > maxWait) {
        callback();
        return;
      }
      lastCount = count;
      setTimeout(check, 1000);
    }
    check();
  },

  // 点击文件 → 设为显示
  onFileSelect: function (e) {
    var that = this;
    var fileId = e.currentTarget.dataset.fileId;
    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接设备', icon: 'none' });
      return;
    }
    app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY, fileId).then(function () {
      wx.showToast({ title: '已设置显示', icon: 'success' });
      setTimeout(function () {
        app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null);
      }, 500);
    }).catch(function () {});
  },

  // 轮播开关（Pro WiFi轮播）
  toggleCarousel: function (e) {
    var that = this;
    var val = e.detail.value;
    if (val) {
      if (!that.data.wifiEnable) {
        wx.showToast({ title: '请先在设置中启用 WiFi', icon: 'none' });
        return;
      }
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE, 2).then(function () {
        that.setData({ photoMode: 2 });
        app.globalData.photoMode = 2;
      }).catch(function () {});
    } else {
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE, 0).then(function () {
        that.setData({ photoMode: 0 });
        app.globalData.photoMode = 0;
      }).catch(function () {});
    }
  },

  // 选择图片加入片单（一次最多 9 张，可多次选择追加）
  chooseImages: function () {
    var that = this;
    wx.chooseMedia({
      count: 9,
      mediaType: ['image'],
      sourceType: ['album', 'camera'],
      success: function (res) {
        var files = res.tempFiles;
        if (!files || !files.length) return;
        filmUtils.setDeviceType(app.globalData.deviceType || 'FRAMEFILM');
        var paths = [];
        for (var i = 0; i < files.length; i++) {
          paths.push(files[i].tempFilePath);
        }
        that._convertImages(paths, 0);
      },
      fail: function () {}
    });
  },

  // 逐张转换为 film 并加入片单
  _convertImages: function (paths, index) {
    var that = this;
    if (index >= paths.length) {
      wx.showToast({ title: '已加入片单', icon: 'success' });
      that._syncFromGlobal();
      return;
    }
    wx.showLoading({ title: '转换 ' + (index + 1) + '/' + paths.length, mask: true });
    filmUtils.imageToFilmData(paths[index], function (fileData) {
      wx.hideLoading();
      if (fileData) {
        var fileName = filmUtils.generateRandomFilename('batch');
        var b64 = '';
        try {
          b64 = wx.arrayBufferToBase64(fileData.buffer);
        } catch (e) {
          b64 = '';
        }
        if (b64) {
          recentUtils.addBatchItem({ name: fileName, time: that._nowText(), data: b64 });
        } else {
          wx.showToast({ title: '第 ' + (index + 1) + ' 张转换失败', icon: 'none' });
        }
      } else {
        wx.showToast({ title: '第 ' + (index + 1) + ' 张转换失败', icon: 'none' });
      }
      setTimeout(function () {
        that._convertImages(paths, index + 1);
      }, 50);
    });
  },

  // 列表缩略图异步渲染（按文件名缓存）
  _renderBatchThumbs: function () {
    var that = this;
    var list = this.data.batch;
    var needUpdate = false;
    for (var i = 0; i < list.length; i++) {
      var item = list[i];
      if (!item.data) continue;
      if (that._batchThumbCache[item.name]) {
        if (!item.thumb) {
          list[i].thumb = that._batchThumbCache[item.name];
          needUpdate = true;
        }
      } else {
        (function (name, data) {
          var fileData;
          try {
            fileData = new Uint8Array(wx.base64ToArrayBuffer(data));
          } catch (e) {
            return;
          }
          filmUtils.renderFilmThumbnail(fileData, function (path) {
            if (!path) return;
            that._batchThumbCache[name] = path;
            var cur = that.data.batch;
            for (var k = 0; k < cur.length; k++) {
              if (cur[k].name === name) {
                cur[k].thumb = path;
                break;
              }
            }
            that.setData({ batch: cur.slice() });
          }, 150);
        })(item.name, item.data);
      }
    }
    if (needUpdate) {
      this.setData({ batch: list.slice() });
    }
  },

  // 点击片单项 → 大图预览完整转换效果
  onBatchItemTap: function (e) {
    var that = this;
    var index = e.currentTarget.dataset.index;
    var item = this.data.batch[index];
    if (!item || !item.data) return;
    wx.showLoading({ title: '渲染预览...', mask: true });
    var fileData;
    try {
      fileData = new Uint8Array(wx.base64ToArrayBuffer(item.data));
    } catch (err) {
      wx.hideLoading();
      return;
    }
    filmUtils.renderFilmThumbnail(fileData, function (path) {
      wx.hideLoading();
      if (!path) {
        wx.showToast({ title: '预览失败', icon: 'none' });
        return;
      }
      that.setData({ preview: true, previewImage: path, previewName: item.name });
    }, 440);
  },

  // 移出片单
  removeBatchItem: function (e) {
    var that = this;
    var index = e.currentTarget.dataset.index;
    var item = that.data.batch[index];
    if (!item) return;
    wx.showModal({
      title: '移出片单',
      content: '确定移除《' + item.name + '》吗？',
      success: function (res) {
        if (res.confirm) {
          recentUtils.removeBatchItem(item.name);
          if (that._batchThumbCache[item.name]) {
            delete that._batchThumbCache[item.name];
          }
          that.setData({ batch: recentUtils.getBatch() });
        }
      }
    });
  },

  closePreview: function () {
    this.setData({ preview: false, previewImage: '', previewName: '' });
  },

  noop: function () {},

  _nowText: function () {
    var d = new Date();
    var pad = function (n) { return (n < 10 ? '0' : '') + n; };
    return (d.getMonth() + 1) + '/' + d.getDate() + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
  },

  // 批量发送片单中所有照片
  batchSend: function () {
    var that = this;
    var batch = that.data.batch;
    if (!batch.length) {
      wx.showToast({ title: '片单为空，先选择图片', icon: 'none' });
      return;
    }
    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接设备', icon: 'none' });
      return;
    }

    that.setData({ showTransfer: true, transferStatus: '准备发送...', transferProgress: 0, transferFailed: false });
    that._watchDisconnect();
    var total = batch.length;
    var sent = 0;

    function sendOne() {
      if (sent >= total) {
        that.setData({ transferStatus: '全部发送完成', transferProgress: 100 });
        setTimeout(function () {
          that._stopDisconnectWatch();
          that.setData({ showTransfer: false });
          that.refreshFileList();
        }, 1500);
        return;
      }
      var item = batch[sent];
      var fileData = new Uint8Array(wx.base64ToArrayBuffer(item.data));
      var startPct = Math.floor((sent / total) * 100);
      that.setData({ transferStatus: '发送 ' + (sent + 1) + '/' + total + ' ' + item.name });
      that._sendFile(fileData, item.name, function (pct) {
        var overall = Math.floor(startPct + (pct / 100) * (100 / total));
        that.setData({ transferProgress: overall });
      }).then(function () {
        sent++;
        sendOne();
      }).catch(function (err) {
        that._stopDisconnectWatch();
        that.setData({
          transferStatus: '发送失败：' + (err.message || err.errMsg || '未知错误') + '，可退出重试',
          transferFailed: true
        });
      });
    }

    sendOne();
  },

  // 发送期间轮询设备连接状态，断联时自动关闭弹窗
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
        that.setData({ showTransfer: false, transferFailed: false });
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

  // 失败后退出传输弹窗
  closeTransfer: function () {
    this._stopDisconnectWatch();
    this.setData({ showTransfer: false, transferFailed: false });
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

  // 分享给朋友
  onShareAppMessage: function () {
    return {
      title: 'FrameFilm - 片单',
      path: '/pages/filmlist/index'
    };
  }
});
