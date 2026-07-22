// 定影 - 拍照上传
var filmUtils = require('../../../utils/film-utils');
var bleUtils = require('../../../utils/ble-utils');
var app = getApp();

var FILM_HEADER_SIZE = filmUtils.FILM_HEADER_SIZE;
var BLE_CHUNK_SIZE = bleUtils.BLE_CHUNK_SIZE;
var BLE_CTRL_DELAY = bleUtils.BLE_CTRL_DELAY;
var BLE_DATA_DELAY = bleUtils.BLE_DATA_DELAY;

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

function fitImageToCanvas(imgW, imgH, canvasW, canvasH) {
  var ratio = Math.max(canvasW / imgW, canvasH / imgH);
  var w = imgW * ratio;
  var h = imgH * ratio;
  var x = (canvasW - w) / 2;
  var y = (canvasH - h) / 2;
  return { x: x, y: y, w: w, h: h };
}

Page({
  data: {
    sendDisabled: true,
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0,
    showFileName: false,
    isEditingName: false,
    customFileName: '',
  },

  _canvas: null,
  _ctx: null,

  onReady: function () {
    this._initCanvas();
  },

  _initCanvas: function () {
    var that = this;
    if (that._canvas) return;
    var query = wx.createSelectorQuery();
    query.select('#canvas-camera').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0] || !res[0].node) return;
      var canvas = res[0].node;
      var ctx = canvas.getContext('2d');
      var CW = filmUtils.getCanvasWidth();
      var CH = filmUtils.getCanvasHeight();
      canvas.width = CW;
      canvas.height = CH;
      that._canvas = canvas;
      that._ctx = ctx;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, CW, CH);
    });
  },

  openCamera: function () {
    var that = this;
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['camera'],
      success: function (res) {
        var tempFilePath = res.tempFiles[0].tempFilePath;
        that._loadImage(tempFilePath);
      }
    });
  },

  _loadImage: function (tempFilePath) {
    var that = this;
    var canvas = that._canvas;
    var ctx = that._ctx;
    if (!canvas || !ctx) {
      wx.showToast({ title: '画布未就绪', icon: 'none' });
      return;
    }

    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();

    var img = canvas.createImage();
    img.onload = function () {
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, CW, CH);

      var fit = fitImageToCanvas(img.width, img.height, CW, CH);
      ctx.drawImage(img, fit.x, fit.y, fit.w, fit.h);

      try {
        filmUtils.processAndDisplay(canvas, ctx, 'adaptive', 1.0, 1.2);
      } catch (e) {
        console.error('processAndDisplay error:', e);
      }

      that.setData({ sendDisabled: false, showFileName: true, customFileName: filmUtils.generateRandomFilename('camera'), isEditingName: false });
    };
    img.onerror = function () {
      wx.showToast({ title: '图片加载失败', icon: 'none' });
    };
    img.src = tempFilePath;
  },

  sendToDevice: function () {
    var that = this;
    var canvas = that._canvas;
    var ctx = that._ctx;
    if (!canvas || !ctx) {
      wx.showToast({ title: '画布未就绪', icon: 'none' });
      return;
    }

    var imageData = filmUtils.extractLandscapeData(canvas);
    var processedData = filmUtils.processImageData(imageData);
    var header = filmUtils.generateFilmHeader();

    var totalSize = filmUtils.getFilmFileTotalSize();
    var fileData = new Uint8Array(totalSize);
    fileData.set(header, 0);
    fileData.set(processedData, FILM_HEADER_SIZE);

    that._sendViaBle(fileData);
  },

  startEditName: function () {
    this.setData({ isEditingName: true });
  },

  onFileNameInput: function (e) {
    this.setData({ customFileName: e.detail.value });
  },

  onFileNameBlur: function () {
    this.setData({ isEditingName: false });
  },

  _sendViaBle: function (fileData) {
    var that = this;

    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接蓝牙设备', icon: 'none' });
      return;
    }

    that.setData({ showTransfer: true, transferStatus: '准备传输...', transferProgress: 0 });

    var fileName = that.data.customFileName || filmUtils.generateRandomFilename('camera');
    var totalSize = fileData.length;
    var lastDisplayedPct = -1;
    var step = 0;

    function updateProgress(extraPercent) {
      var controlWeight = 0.05;
      var dataWeight = 0.95;
      var controlProgress = (step / 4) * controlWeight * 100;
      var dataProgress = totalSize > 0 ? (extraPercent / totalSize) * dataWeight * 100 : 0;
      var pct = Math.min(99, Math.floor(controlProgress + dataProgress));
      if (pct !== lastDisplayedPct) {
        lastDisplayedPct = pct;
        that.setData({ transferProgress: pct });
      }
    }

    that.setData({ transferStatus: '发送开始指令...' });
    app.sendBlePacket(bleUtils.buildFileStartPacket()).then(function () {
      step = 1; updateProgress(0);
      return delay(BLE_CTRL_DELAY);
    }).then(function () {
      that.setData({ transferStatus: '发送文件名...' });
      return app.sendBlePacket(bleUtils.buildFileNamePacket(fileName));
    }).then(function () {
      step = 2; updateProgress(0);
      return delay(BLE_CTRL_DELAY);
    }).then(function () {
      that.setData({ transferStatus: '发送文件长度...' });
      return app.sendBlePacket(bleUtils.buildFileLenPacket(totalSize));
    }).then(function () {
      step = 3; updateProgress(0);
      return delay(BLE_CTRL_DELAY);
    }).then(function () {
      that.setData({ transferStatus: '传输数据...' });
      var offset = 0;
      function sendChunk() {
        if (offset >= totalSize) return Promise.resolve();
        var end = Math.min(offset + BLE_CHUNK_SIZE, totalSize);
        var chunk = fileData.slice(offset, end);
        return app.sendBlePacket(bleUtils.buildFileDataPacket(chunk)).then(function () {
          offset = end;
          updateProgress(offset);
          return delay(BLE_DATA_DELAY);
        }).then(sendChunk);
      }
      return sendChunk();
    }).then(function () {
      step = 4;
      that.setData({ transferStatus: '发送完成指令...' });
      return app.sendBlePacket(bleUtils.buildFileStopPacket());
    }).then(function () {
      that.setData({ transferStatus: '传输完成！', transferProgress: 100 });
      setTimeout(function () { that.setData({ showTransfer: false }); }, 1500);
    }).catch(function (err) {
      that.setData({ transferStatus: '传输失败：' + (err.errMsg || err.message || '未知错误') });
      console.error('BLE 传输失败:', err);
    });
  },
});
