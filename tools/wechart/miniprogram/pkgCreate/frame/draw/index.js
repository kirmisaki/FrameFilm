// 绘梦 - 画板涂鸦
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

var DEFAULT_COLORS = [
  { name: '黑', value: '#000000' },
  { name: '白', value: '#ffffff' },
  { name: '红', value: '#ff0000' },
  { name: '黄', value: '#ffff00' },
  { name: '绿', value: '#00aa00' },
  { name: '蓝', value: '#0000ff' },
];

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

function padHex(n) {
  var h = n.toString(16);
  return h.length < 2 ? '0' + h : h;
}

Page({
  data: {
    boxH: 900,
    tool: 'pen',
    brushSize: 3,
    currentColor: '#000000',
    colors: DEFAULT_COLORS,
    converted: false,
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0,
    showFileName: false,
    isEditingName: false,
    customFileName: '',
    showColorPicker: false,
    customColor: '#888888',
    colorR: 136,
    colorG: 136,
    colorB: 136,
    hueValue: 0,
  },

  _canvas: null,
  _ctx: null,
  _isDrawing: false,
  _lastX: 0,
  _lastY: 0,
  _canvasCssW: 0,
  _canvasCssH: 0,
  _originalData: null,

  onReady: function () {
    this._initCanvas();
  },

  _initCanvas: function () {
    var that = this;
    if (that._canvas) return;
    // 预览容器高度随设备画布宽高比自适应（容器宽固定 600rpx），避免非 2:3 画布（Dock/Max）预览纵向拉伸；
    // 先更新容器高度，待重排后再测量画布实际 CSS 尺寸，保证触摸坐标换算不失真
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    that.setData({ boxH: Math.round(600 * CH / CW) }, function () {
      var query = wx.createSelectorQuery();
      query.select('#draw-canvas').fields({ node: true, size: true }).exec(function (res) {
        if (!res || !res[0] || !res[0].node) return;
        var canvas = res[0].node;
        var ctx = canvas.getContext('2d');
        canvas.width = CW;
        canvas.height = CH;
        that._canvas = canvas;
        that._ctx = ctx;
        that._canvasCssW = res[0].width;
        that._canvasCssH = res[0].height;
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, CW, CH);
      });
    });
  },

  // 触摸绘制
  onTouchStart: function (e) {
    if (this.data.converted) return;
    var touch = e.touches[0];
    this._isDrawing = true;
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    var scaleX = CW / this._canvasCssW;
    var scaleY = CH / this._canvasCssH;
    var x = touch.x * scaleX;
    var y = touch.y * scaleY;
    this._lastX = x;
    this._lastY = y;
    var ctx = this._ctx;
    if (!ctx) return;
    ctx.beginPath();
    ctx.arc(x, y, this.data.brushSize / 2, 0, Math.PI * 2);
    ctx.fillStyle = this.data.tool === 'eraser' ? '#ffffff' : this.data.currentColor;
    ctx.fill();
  },

  onTouchMove: function (e) {
    if (!this._isDrawing || this.data.converted) return;
    var touch = e.touches[0];
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    var scaleX = CW / this._canvasCssW;
    var scaleY = CH / this._canvasCssH;
    var x = touch.x * scaleX;
    var y = touch.y * scaleY;
    var ctx = this._ctx;
    if (!ctx) return;
    ctx.beginPath();
    ctx.moveTo(this._lastX, this._lastY);
    ctx.lineTo(x, y);
    ctx.strokeStyle = this.data.tool === 'eraser' ? '#ffffff' : this.data.currentColor;
    ctx.lineWidth = this.data.brushSize;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.stroke();
    this._lastX = x;
    this._lastY = y;
  },

  onTouchEnd: function () {
    this._isDrawing = false;
  },

  // 工具切换
  selectPen: function () {
    this.setData({ tool: 'pen' });
  },

  selectEraser: function () {
    this.setData({ tool: 'eraser' });
  },

  clearCanvas: function () {
    var that = this;
    wx.showModal({
      title: '确认清空',
      content: '清空后无法恢复，确定要清空画布吗？',
      confirmText: '清空',
      confirmColor: '#c62828',
      success: function (res) {
        if (res.confirm) {
          var ctx = that._ctx;
          if (!ctx) return;
          ctx.fillStyle = '#ffffff';
          ctx.fillRect(0, 0, filmUtils.getCanvasWidth(), filmUtils.getCanvasHeight());
          that.setData({ converted: false, showFileName: false });
        }
      }
    });
  },

  onSizeChanging: function (e) {
    this.setData({ brushSize: e.detail.value });
  },

  onSizeChange: function (e) {
    this.setData({ brushSize: e.detail.value });
  },

  // 颜色选择
  selectColor: function (e) {
    var color = e.currentTarget.dataset.color;
    this.setData({ currentColor: color, tool: 'pen' });
  },

  // 调色盘
  openColorPicker: function () {
    this.setData({ showColorPicker: true });
  },

  closeColorPicker: function () {
    this.setData({ showColorPicker: false });
  },

  // HSL 转 RGB
  _hslToRgb: function (h, s, l) {
    h = h / 360;
    var r, g, b;
    if (s === 0) {
      r = g = b = l;
    } else {
      function hue2rgb(p, q, t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1/6) return p + (q - p) * 6 * t;
        if (t < 1/2) return q;
        if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
        return p;
      }
      var q = l < 0.5 ? l * (1 + s) : l + s - l * s;
      var p = 2 * l - q;
      r = hue2rgb(p, q, h + 1/3);
      g = hue2rgb(p, q, h);
      b = hue2rgb(p, q, h - 1/3);
    }
    return { r: Math.round(r * 255), g: Math.round(g * 255), b: Math.round(b * 255) };
  },

  onHueChange: function (e) {
    var hue = e.detail.value;
    var rgb = this._hslToRgb(hue, 1, 0.5);
    this.setData({
      hueValue: hue,
      colorR: rgb.r,
      colorG: rgb.g,
      colorB: rgb.b,
      customColor: '#' + padHex(rgb.r) + padHex(rgb.g) + padHex(rgb.b)
    });
  },

  onColorRChange: function (e) {
    var r = e.detail.value;
    this.setData({ colorR: r, customColor: '#' + padHex(r) + padHex(this.data.colorG) + padHex(this.data.colorB) });
  },

  onColorGChange: function (e) {
    var g = e.detail.value;
    this.setData({ colorG: g, customColor: '#' + padHex(this.data.colorR) + padHex(g) + padHex(this.data.colorB) });
  },

  onColorBChange: function (e) {
    var b = e.detail.value;
    this.setData({ colorB: b, customColor: '#' + padHex(this.data.colorR) + padHex(this.data.colorG) + padHex(b) });
  },

  applyCustomColor: function () {
    this.setData({
      currentColor: this.data.customColor,
      tool: 'pen',
      showColorPicker: false
    });
  },

  // 转换抖动 / 还原
  convertDither: function () {
    var that = this;
    var canvas = that._canvas;
    var ctx = that._ctx;
    if (!canvas || !ctx) return;

    if (that.data.converted) {
      // 还原到原始绘图
      if (that._originalData) {
        ctx.putImageData(that._originalData, 0, 0);
      }
      that.setData({ converted: false, showFileName: false });
    } else {
      // 保存原始数据
      that._originalData = ctx.getImageData(0, 0, filmUtils.getCanvasWidth(), filmUtils.getCanvasHeight());
      try {
        // 使用设置页「默认转换算法」；强度/对比度/饱和度统一 1.0
        var preset = ditherConfig.getCreateDitherParams();
        filmUtils.processAndDisplay(canvas, ctx, preset.type, preset.strength, preset.contrast, preset.saturation);
        that.setData({ converted: true, showFileName: true, customFileName: '', isEditingName: false });
      } catch (e) {
        console.error('convertDither error:', e);
        wx.showToast({ title: '转换失败', icon: 'none' });
      }
    }
  },

  // 文件名
  startEditName: function () {
    this.setData({ isEditingName: true });
  },

  onFileNameInput: function (e) {
    this.setData({ customFileName: e.detail.value });
  },

  onFileNameBlur: function () {
    this.setData({ isEditingName: false });
  },

  // 发送到设备
  sendToDevice: function () {
    var that = this;
    var canvas = that._canvas;
    var ctx = that._ctx;
    if (!canvas || !ctx) return;

    var imageData = filmUtils.extractLandscapeData(canvas);
    var processedData = filmUtils.processImageData(imageData);
    var header = filmUtils.generateFilmHeader();

    var totalSize = filmUtils.getFilmFileTotalSize();
    var fileData = new Uint8Array(totalSize);
    fileData.set(header, 0);
    fileData.set(processedData, FILM_HEADER_SIZE);

    that._sendViaBle(fileData);
  },

  _sendViaBle: function (fileData) {
    var that = this;

    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接蓝牙设备', icon: 'none' });
      return;
    }

    that.setData({ showTransfer: true, transferStatus: '准备传输...', transferProgress: 0 });

    var fileName = (that.data.customFileName || 'draw') + '.film';
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
      // 记录最近使用（首页最近上墙）
      recentUtils.addRecent(fileName, fileData);
      setTimeout(function () { that.setData({ showTransfer: false }); }, 1500);
    }).catch(function (err) {
      that.setData({ transferStatus: '传输失败：' + (err.errMsg || err.message || '未知错误') });
      console.error('BLE 传输失败:', err);
    });
  },
});
