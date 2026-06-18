// 一言 - 语录
var filmUtils = require('../../../utils/film-utils');
var bleUtils = require('../../../utils/ble-utils');
var app = getApp();

var CANVAS_WIDTH = filmUtils.CANVAS_WIDTH;
var CANVAS_HEIGHT = filmUtils.CANVAS_HEIGHT;
var FILM_FILE_TOTAL_SIZE = filmUtils.FILM_FILE_TOTAL_SIZE;
var BLE_CHUNK_SIZE = bleUtils.BLE_CHUNK_SIZE;
var BLE_CTRL_DELAY = bleUtils.BLE_CTRL_DELAY;
var BLE_DATA_DELAY = bleUtils.BLE_DATA_DELAY;

var frameColorSchemes = [
  { bg: '#ffffff', text: '#000000', accent: '#ff0000', author: '#000000' },
  { bg: '#ffffff', text: '#000000', accent: '#0000ff', author: '#000000' },
  { bg: '#ffffff', text: '#000000', accent: '#29cc14', author: '#000000' },
];

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

Page({
  data: {
    ditherChecked: true,
    customQuoteText: '',
    customQuoteAuthor: '',
    showCustomPanel: false,
    quoteText: '',
    quoteAuthor: '',
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0,
  },

  _canvas: null,
  _ctx: null,

  onReady: function () {
    this._initCanvas();
  },

  onShow: function () {
    var that = this;
    if (!that._canvas) {
      setTimeout(function () { that._initCanvas(); }, 100);
    }
    // 首次进入自动获取语录
    if (!that.data.quoteText) {
      setTimeout(function () { that.fetchQuote(); }, 200);
    }
  },

  _initCanvas: function () {
    var that = this;
    if (that._canvas) return;
    var query = wx.createSelectorQuery();
    query.select('#canvas-quote').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0] || !res[0].node) return;
      var canvas = res[0].node;
      var ctx = canvas.getContext('2d');
      canvas.width = CANVAS_WIDTH;
      canvas.height = CANVAS_HEIGHT;
      that._canvas = canvas;
      that._ctx = ctx;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);
    });
  },

  fetchQuote: function () {
    var that = this;
    wx.showLoading({ title: '获取中...' });
    wx.request({
      url: 'https://international.v1.hitokoto.cn/?c=d&c=h&c=k&c=i&encode=json',
      success: function (res) {
        wx.hideLoading();
        if (res.data && res.data.hitokoto) {
          that.setData({
            quoteText: res.data.hitokoto,
            quoteAuthor: res.data.from_who || res.data.from || ''
          });
          that.renderQuote();
        } else {
          wx.showToast({ title: '获取失败', icon: 'none' });
        }
      },
      fail: function () {
        wx.hideLoading();
        wx.showToast({ title: '网络错误', icon: 'none' });
      }
    });
  },

  renderQuote: function () {
    var that = this;
    var canvas = that._canvas;
    var ctx = that._ctx;
    if (!canvas || !ctx) return;

    var text = that.data.quoteText;
    var author = that.data.quoteAuthor;
    if (!text) return;

    var scheme = frameColorSchemes[Math.floor(Math.random() * frameColorSchemes.length)];
    var w = CANVAS_WIDTH;
    var h = CANVAS_HEIGHT;

    ctx.clearRect(0, 0, w, h);

    // 纯色背景
    ctx.fillStyle = scheme.bg;
    ctx.fillRect(0, 0, w, h);

    // 装饰引号
    ctx.font = '80px serif';
    ctx.fillStyle = scheme.accent;
    ctx.textAlign = 'left';
    ctx.fillText('\u201C', 5, 90);

    // 装饰线
    ctx.strokeStyle = scheme.accent;
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(45, 100);
    ctx.lineTo(100, 100);
    ctx.stroke();

    // 文字居中
    ctx.font = 'bold 30px serif';
    ctx.fillStyle = scheme.text;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    var lines = filmUtils.wrapText(ctx, text, 30, w - 80);
    var zhLineHeight = 44;
    var zhTotalHeight = lines.length * zhLineHeight;
    var bottomSpace = author ? 130 : 60;
    var availableHeight = h - 100 - bottomSpace;
    var zhStartY = 100 + (availableHeight - zhTotalHeight) / 2;

    for (var i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], w / 2, zhStartY + i * zhLineHeight);
    }

    // 作者
    if (author) {
      ctx.font = 'bold 18px serif';
      ctx.fillStyle = scheme.author;
      ctx.fillText('\u2014\u2014 ' + author, w / 2, h - 130);
    }

    // 底部装饰线
    ctx.strokeStyle = scheme.accent;
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(w / 2 - 30, h - 65);
    ctx.lineTo(w / 2 + 30, h - 65);
    ctx.stroke();

    // 电量图标
    var batteryLevel = (app.globalData.batteryLevel || 0) / 100;
    var battX = w - 55, battY = 20;
    var battW = 35, battH = 18;
    var battR = 4;

    function drawRoundedRect(x, y, width, height, radius) {
      ctx.beginPath();
      ctx.moveTo(x + radius, y);
      ctx.lineTo(x + width - radius, y);
      ctx.quadraticCurveTo(x + width, y, x + width, y + radius);
      ctx.lineTo(x + width, y + height - radius);
      ctx.quadraticCurveTo(x + width, y + height, x + width - radius, y + height);
      ctx.lineTo(x + radius, y + height);
      ctx.quadraticCurveTo(x, y + height, x, y + height - radius);
      ctx.lineTo(x, y + radius);
      ctx.quadraticCurveTo(x, y, x + radius, y);
      ctx.closePath();
    }

    drawRoundedRect(battX, battY, battW, battH, battR);
    ctx.strokeStyle = scheme.accent;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    ctx.fillStyle = scheme.accent;
    ctx.fillRect(battX + battW + 1, battY + 5, 3, battH - 10);

    drawRoundedRect(battX + 2, battY + 2, (battW - 4) * batteryLevel, battH - 4, 2);
    ctx.fillStyle = scheme.accent;
    ctx.fill();

    // 日期
    var now = new Date();
    var dateStr = now.getFullYear() + ' \u5E74 ' + ('0' + (now.getMonth() + 1)).slice(-2) + ' \u6708 ' + ('0' + now.getDate()).slice(-2) + ' \u65E5';
    ctx.font = '600 16px sans-serif';
    ctx.fillStyle = scheme.accent;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    ctx.fillText(dateStr, w / 2, h - 30);

    // Film 格式处理并回显
    var ditherType = that.data.ditherChecked ? 'floydSteinberg' : null;
    filmUtils.processAndDisplay(canvas, ctx, ditherType, 0.8, null);
  },

  toggleCustomPanel: function () {
    this.setData({ showCustomPanel: !this.data.showCustomPanel });
  },

  onCustomQuoteInput: function (e) {
    this.setData({ customQuoteText: e.detail.value });
  },

  onCustomAuthorInput: function (e) {
    this.setData({ customQuoteAuthor: e.detail.value });
  },

  generateCustomQuote: function () {
    var text = this.data.customQuoteText.trim();
    if (!text) {
      wx.showToast({ title: '请输入文字', icon: 'none' });
      return;
    }
    this.setData({
      quoteText: text,
      quoteAuthor: this.data.customQuoteAuthor.trim()
    });
    this.renderQuote();
  },

  toggleDither: function () {
    this.setData({ ditherChecked: !this.data.ditherChecked });
    if (this.data.quoteText) {
      this.renderQuote();
    }
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

    var fileData = new Uint8Array(FILM_FILE_TOTAL_SIZE);
    fileData.set(header, 0);
    fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);

    that._sendViaBle(fileData, 'quote');
  },

  _sendViaBle: function (fileData, tab) {
    var that = this;

    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接蓝牙设备', icon: 'none' });
      return;
    }

    that.setData({ showTransfer: true, transferStatus: '准备传输...', transferProgress: 0 });

    var fileName = tab + '.film';
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
      wx.showToast({ title: '传输完成', icon: 'success' });
      setTimeout(function () { that.setData({ showTransfer: false }); }, 2000);
    }).catch(function (err) {
      that.setData({ transferStatus: '传输失败：' + (err.errMsg || err.message || '未知错误') });
      console.error('BLE 传输失败:', err);
    });
  },
});
