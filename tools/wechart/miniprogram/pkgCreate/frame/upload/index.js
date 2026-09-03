// 拾光 - 从相册选择照片
var filmUtils = require('../../../utils/film-utils');
var ditherAdvanced = require('../../utils/dither-advanced'); // 分包高级算法引擎（含 AE LUT，不进主包）
filmUtils.attachDitherAdvanced(ditherAdvanced);
var bleUtils = require('../../../utils/ble-utils');
var recentUtils = require('../../../utils/recent-utils');
var ditherConfig = require('../../../utils/dither-config');
var app = getApp();

var FILM_HEADER_SIZE = filmUtils.FILM_HEADER_SIZE;
var BLE_CHUNK_SIZE = bleUtils.BLE_CHUNK_SIZE;
var BLE_CTRL_DELAY = bleUtils.BLE_CTRL_DELAY;
var BLE_DATA_DELAY = bleUtils.BLE_DATA_DELAY;

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

function fitImageToCanvas(imgW, imgH, canvasW, canvasH) {
  var ratio = Math.min(canvasW / imgW, canvasH / imgH);
  var w = imgW * ratio;
  var h = imgH * ratio;
  var x = (canvasW - w) / 2;
  var y = (canvasH - h) / 2;
  return { x: x, y: y, w: w, h: h };
}

Page({
  data: {
    boxH: 900,
    sendDisabled: true,
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0,
    showFileName: false,
    isEditingName: false,
    customFileName: '',
    adjustOn: false,
    showAdjustHint: false,
    imgLoaded: false,
    // 色彩调节（作用于默认转换算法的预览与发送）：对比度 0.5~2.0 / 饱和度 0~3.0，默认 1.0
    contrast: 1.0,
    contrastInt: 100,
    saturation: 1.0,
    saturationInt: 10
  },

  _canvas: null,
  _ctx: null,
  _img: null,
  _fit: null,
  _isPortraitRotate: false,
  _scale: 1,
  _offset: { x: 0, y: 0 },
  _gesture: null,
  _editTimer: null,

  onReady: function () {
    this._initCanvas();
  },

  _initCanvas: function () {
    var that = this;
    if (that._canvas) return;
    var query = wx.createSelectorQuery();
    query.select('#canvas-upload').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0] || !res[0].node) return;
      var canvas = res[0].node;
      var ctx = canvas.getContext('2d');
      var CW = filmUtils.getCanvasWidth();
      var CH = filmUtils.getCanvasHeight();
      // 预览容器高度随设备画布宽高比自适应（容器宽固定 600rpx），避免非 2:3 画布（Dock/Max）预览纵向拉伸
      that.setData({ boxH: Math.round(600 * CH / CW) });
      canvas.width = CW;
      canvas.height = CH;
      that._canvas = canvas;
      that._ctx = ctx;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, CW, CH);
    });
  },

  // 绘制照片：applyDither=true 时走 EPD 抖动，false 时显示原图（缩放过程用）
  _drawPhoto: function (applyDither) {
    var that = this;
    var canvas = that._canvas;
    var ctx = that._ctx;
    if (!canvas || !ctx || !that._img) return;
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    var img = that._img;
    var fit = that._fit;
    var scale = that._scale || 1;
    var off = that._offset || { x: 0, y: 0 };
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, CW, CH);
    if (that._isPortraitRotate) {
      ctx.save();
      ctx.translate(fit.x + fit.w / 2 + off.x, fit.y + fit.h / 2 + off.y);
      ctx.rotate(Math.PI / 2);
      ctx.scale(scale, scale);
      ctx.drawImage(img, -fit.h / 2, -fit.w / 2, fit.h, fit.w);
      ctx.restore();
    } else {
      ctx.save();
      ctx.translate(fit.x + fit.w / 2 + off.x, fit.y + fit.h / 2 + off.y);
      ctx.scale(scale, scale);
      ctx.drawImage(img, -fit.w / 2, -fit.h / 2, fit.w, fit.h);
      ctx.restore();
    }
    if (applyDither) {
      try {
        // 使用设置页「默认转换算法」；强度固定 1.0，对比度/饱和度用本页滑块值（默认 1.0 即不加调整）
        var preset = ditherConfig.getCreateDitherParams();
        var contrast = this.data.contrast || 1.0;
        var saturation = this.data.saturation || 1.0;
        filmUtils.processAndDisplay(canvas, ctx, preset.type, preset.strength, contrast, saturation);
      } catch (e) {
        console.error('processAndDisplay error:', e);
      }
    }
  },

  // 松手 1s 后加载抖动效果（完成后自动隐藏提示）
  _scheduleDither: function () {
    var that = this;
    that._cancelEditTimer();
    that._editTimer = setTimeout(function () {
      that._editTimer = null;
      if (that._img) {
        that._drawPhoto(true);
        that.setData({ showAdjustHint: false });
      }
    }, 1000);
  },

  _cancelEditTimer: function () {
    if (this._editTimer) {
      clearTimeout(this._editTimer);
      this._editTimer = null;
    }
  },

  // 滑块拖动中仅刷新数值，松手后才重新转换预览
  onContrastChanging: function (e) {
    var val = parseInt(e.detail.value, 10) / 100;
    this.setData({ contrast: val, contrastInt: parseInt(e.detail.value, 10) });
  },

  onContrastChange: function (e) {
    var val = parseInt(e.detail.value, 10) / 100;
    this.setData({ contrast: val, contrastInt: parseInt(e.detail.value, 10) });
    this._applyColorPreview();
  },

  onSaturationChanging: function (e) {
    var val = parseInt(e.detail.value, 10) / 10;
    this.setData({ saturation: val, saturationInt: parseInt(e.detail.value, 10) });
  },

  onSaturationChange: function (e) {
    var val = parseInt(e.detail.value, 10) / 10;
    this.setData({ saturation: val, saturationInt: parseInt(e.detail.value, 10) });
    this._applyColorPreview();
  },

  resetColors: function () {
    this.setData({ contrast: 1.0, contrastInt: 100, saturation: 1.0, saturationInt: 10 });
    this._applyColorPreview();
  },

  // 色彩调节后立即刷新抖动预览；若处于位置调节（缩放/拖动）模式则先退出，避免与手势冲突
  _applyColorPreview: function () {
    var that = this;
    that._cancelEditTimer();
    if (!that._img) return;
    that._gesture = null;
    if (that.data.adjustOn) {
      that.setData({ adjustOn: false, showAdjustHint: false });
    }
    that._drawPhoto(true);
  },

  toggleAdjust: function () {
    var on = !this.data.adjustOn;
    this.setData({ adjustOn: on, showAdjustHint: on });
    if (!on) {
      // 关闭调节：取消未完成的延迟抖动，立即回到抖动显示状态
      this._cancelEditTimer();
      this._gesture = null;
      if (this._img) this._drawPhoto(true);
    }
  },

  onTouchStart: function (e) {
    var that = this;
    if (!that.data.adjustOn || !that._img) return;
    var t = e.touches;
    if (!t || t.length === 0) return;
    that._cancelEditTimer();
    if (!that.data.showAdjustHint) {
      that.setData({ showAdjustHint: true });
    }
    if (t.length >= 2) {
      // 双指缩放
      that._gesture = {
        mode: 'pinch',
        startDist: that._touchDist(t),
        startScale: that._scale || 1,
        active: true
      };
    } else {
      // 单指拖动
      that._gesture = {
        mode: 'pan',
        startX: t[0].clientX,
        startY: t[0].clientY,
        startOffsetX: that._offset.x,
        startOffsetY: that._offset.y,
        active: true
      };
    }
    // 手势调整中关闭抖动，显示原图便于预览
    that._drawPhoto(false);
  },

  onTouchMove: function (e) {
    var that = this;
    var g = that._gesture;
    if (!g || !g.active) return;
    var t = e.touches;
    if (!t || t.length === 0) return;
    if (t.length >= 2) {
      // 双指缩放（从拖动切入时重新记录起点）
      if (g.mode !== 'pinch') {
        g.mode = 'pinch';
        g.startDist = that._touchDist(t);
        g.startScale = that._scale || 1;
      }
      var ratio = that._touchDist(t) / g.startDist;
      var newScale = Math.min(4, Math.max(0.5, g.startScale * ratio));
      that._scale = newScale;
      that._drawPhoto(false);
    } else if (g.mode === 'pan') {
      // 单指拖动
      var CW = filmUtils.getCanvasWidth();
      var CH = filmUtils.getCanvasHeight();
      var dx = t[0].clientX - g.startX;
      var dy = t[0].clientY - g.startY;
      that._offset = {
        x: Math.min(CW * 0.8, Math.max(-CW * 0.8, g.startOffsetX + dx)),
        y: Math.min(CH * 0.8, Math.max(-CH * 0.8, g.startOffsetY + dy))
      };
      that._drawPhoto(false);
    }
  },

  onTouchEnd: function () {
    var that = this;
    if (that._gesture) {
      that._gesture.active = false;
      that._gesture = null;
    }
    if (that.data.adjustOn && that._img) {
      that._scheduleDither();
    }
  },

  _touchDist: function (touches) {
    var dx = touches[0].clientX - touches[1].clientX;
    var dy = touches[0].clientY - touches[1].clientY;
    return Math.sqrt(dx * dx + dy * dy);
  },

  chooseImage: function () {
    var that = this;
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album'],
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
      that._img = img;
      that._scale = 1;
      that._offset = { x: 0, y: 0 };
      that._cancelEditTimer();

      var drawImgW = img.width;
      var drawImgH = img.height;
      var cfg = filmUtils.getDeviceConfig();
      // 标准版：横图自动旋转90°（竖屏面板需要）
      that._isPortraitRotate = !!(cfg.isPortraitPanel && img.width > img.height);
      if (that._isPortraitRotate) {
        drawImgW = img.height;
        drawImgH = img.width;
      }

      that._fit = fitImageToCanvas(drawImgW, drawImgH, CW, CH);
      that._drawPhoto(true);

      that.setData({ sendDisabled: false, showFileName: true, customFileName: filmUtils.generateRandomFilename('upload'), isEditingName: false, imgLoaded: true });
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

    // 若缩放调整后 1s 延迟抖动尚未触发，先立即应用抖动保证发送的是最终效果
    that._cancelEditTimer();
    that._gesture = null;
    if (that._img) that._drawPhoto(true);

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

    var fileName = that.data.customFileName || filmUtils.generateRandomFilename('upload');
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

    // Step 1: FILE_START
    that.setData({ transferStatus: '发送开始指令...' });
    app.sendBlePacket(bleUtils.buildFileStartPacket()).then(function () {
      step = 1; updateProgress(0);
      return delay(BLE_CTRL_DELAY);
    }).then(function () {
      // Step 2: FILE_NAME
      that.setData({ transferStatus: '发送文件名...' });
      return app.sendBlePacket(bleUtils.buildFileNamePacket(fileName));
    }).then(function () {
      step = 2; updateProgress(0);
      return delay(BLE_CTRL_DELAY);
    }).then(function () {
      // Step 3: FILE_LEN
      that.setData({ transferStatus: '发送文件长度...' });
      return app.sendBlePacket(bleUtils.buildFileLenPacket(totalSize));
    }).then(function () {
      step = 3; updateProgress(0);
      return delay(BLE_CTRL_DELAY);
    }).then(function () {
      // Step 4: DATA
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
      // 记录最近使用（首页最近上墙）
      recentUtils.addRecent(fileName, fileData);
      setTimeout(function () { that.setData({ showTransfer: false }); }, 1500);
    }).catch(function (err) {
      that.setData({ transferStatus: '传输失败：' + (err.errMsg || err.message || '未知错误') });
      console.error('BLE 传输失败:', err);
    });
  },
});
