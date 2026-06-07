// frame/index.js
const filmUtils = require('../../utils/film-utils');
const bleUtils = require('../../utils/ble-utils');
const app = getApp();

var CANVAS_WIDTH = filmUtils.CANVAS_WIDTH;
var CANVAS_HEIGHT = filmUtils.CANVAS_HEIGHT;
var FILM_FILE_TOTAL_SIZE = filmUtils.FILM_FILE_TOTAL_SIZE;
var BLE_CHUNK_SIZE = bleUtils.BLE_CHUNK_SIZE;
var BLE_CTRL_DELAY = bleUtils.BLE_CTRL_DELAY;
var BLE_DATA_DELAY = bleUtils.BLE_DATA_DELAY;

// 色彩方案
var frameColorSchemes = [
  { bg: '#ffffff', text: '#000000', accent: '#ff0000', author: '#000000' },
  { bg: '#ffffff', text: '#000000', accent: '#0000ff', author: '#000000' },
  { bg: '#ffffff', text: '#000000', accent: '#29cc14', author: '#000000' },
];

// 延迟工具函数
function delay(ms) {
  return new Promise(function (resolve) {
    setTimeout(resolve, ms);
  });
}

// 格式化日期
function formatDate() {
  var now = new Date();
  var y = now.getFullYear();
  var m = ('0' + (now.getMonth() + 1)).slice(-2);
  var d = ('0' + now.getDate()).slice(-2);
  return y + '.' + m + '.' + d;
}

// 拟合图片到画布区域（保持比例居中）
function fitImageToCanvas(imgW, imgH, canvasW, canvasH, fill) {
  var ratio = fill
    ? Math.max(canvasW / imgW, canvasH / imgH)
    : Math.min(canvasW / imgW, canvasH / imgH);
  var w = imgW * ratio;
  var h = imgH * ratio;
  var x = (canvasW - w) / 2;
  var y = (canvasH - h) / 2;
  return { x: x, y: y, w: w, h: h };
}

Page({
  data: {
    activeTab: 'upload',
    // Tab 1 - 拾光
    sendDisabled: true,
    showTransferUpload: false,
    transferStatusUpload: '',
    transferProgressUpload: 0,
    // Tab 2 - 定影
    sendDisabledCamera: true,
    showTransferCamera: false,
    transferStatusCamera: '',
    transferProgressCamera: 0,
    // Tab 3 - 一言
    ditherChecked: true,
    customQuoteText: '',
    customQuoteAuthor: '',
    showCustomPanel: false,
    quoteText: '',
    quoteAuthor: '',
    showTransferQuote: false,
    transferStatusQuote: '',
    transferProgressQuote: 0,
  },

  // Canvas 引用（不放在 data 中避免序列化问题）
  _canvasUpload: null,
  _ctxUpload: null,
  _canvasCamera: null,
  _ctxCamera: null,
  _canvasQuote: null,
  _ctxQuote: null,

  onReady: function () {
    this._initCurrentCanvas();
  },

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 1 });
    }
    // 仅在画布未初始化时初始化
    var that = this;
    var tab = that.data.activeTab;
    var key = tab === 'upload' ? '_canvasUpload' : tab === 'camera' ? '_canvasCamera' : '_canvasQuote';
    if (!that[key]) {
      that._initCurrentCanvas();
    }
  },

  // 初始化当前 tab 的 canvas
  _initCurrentCanvas: function () {
    var that = this;
    var tab = that.data.activeTab;
    setTimeout(function () {
      if (tab === 'upload') {
        that._initCanvas('canvas-upload', '_canvasUpload', '_ctxUpload');
      } else if (tab === 'camera') {
        that._initCanvas('canvas-camera', '_canvasCamera', '_ctxCamera');
      } else if (tab === 'quote') {
        that._initCanvas('canvas-quote', '_canvasQuote', '_ctxQuote');
      }
    }, 100);
  },

  // 通用 canvas 初始化
  _initCanvas: function (selectorId, canvasKey, ctxKey) {
    var that = this;
    // 已初始化则跳过（避免重置画布内容）
    if (that[canvasKey]) return;
    var query = wx.createSelectorQuery();
    query.select('#' + selectorId).fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0] || !res[0].node) return;
      var canvas = res[0].node;
      var ctx = canvas.getContext('2d');
      canvas.width = CANVAS_WIDTH;
      canvas.height = CANVAS_HEIGHT;
      that[canvasKey] = canvas;
      that[ctxKey] = ctx;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);
    });
  },

  // Tab 切换
  switchTab: function (e) {
    var tab = e.currentTarget.dataset.tab;
    if (tab === this.data.activeTab) return;

    // 如果离开 quote tab，不做特殊处理
    // 如果离开 camera tab，可以通过停止预览（此处无需停止，因为用的是 chooseMedia）

    // wx:if 切换会销毁旧 canvas，清除引用以便重新初始化
    var oldTab = this.data.activeTab;
    if (oldTab === 'upload') { this._canvasUpload = null; this._ctxUpload = null; }
    else if (oldTab === 'camera') { this._canvasCamera = null; this._ctxCamera = null; }
    else if (oldTab === 'quote') { this._canvasQuote = null; this._ctxQuote = null; }

    this.setData({ activeTab: tab });

    var that = this;
    // 切换后初始化新 tab 的 canvas
    setTimeout(function () {
      if (tab === 'upload') {
        that._initCanvas('canvas-upload', '_canvasUpload', '_ctxUpload');
      } else if (tab === 'camera') {
        that._initCanvas('canvas-camera', '_canvasCamera', '_ctxCamera');
      } else if (tab === 'quote') {
        that._initCanvas('canvas-quote', '_canvasQuote', '_ctxQuote');
        // 首次切换到 quote 时自动获取语录
        if (!that.data.quoteText) {
          that.fetchQuote();
        }
      }
    }, 150);
  },

  // ========== Tab 1: 拾光 - 相册选择 ==========
  chooseImage: function () {
    var that = this;
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album'],
      success: function (res) {
        var tempFilePath = res.tempFiles[0].tempFilePath;
        that._loadImageToCanvas(tempFilePath, '_canvasUpload', '_ctxUpload', 'upload');
      },
      fail: function () {
        // 用户取消
      }
    });
  },

  // ========== Tab 2: 定影 - 拍照 ==========
  openCamera: function () {
    var that = this;
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['camera'],
      success: function (res) {
        var tempFilePath = res.tempFiles[0].tempFilePath;
        that._loadImageToCanvas(tempFilePath, '_canvasCamera', '_ctxCamera', 'camera');
      },
      fail: function () {
        // 用户取消
      }
    });
  },

  // 加载图片到 canvas 并处理
  _loadImageToCanvas: function (tempFilePath, canvasKey, ctxKey, tab) {
    var that = this;
    var canvas = that[canvasKey];
    var ctx = that[ctxKey];
    if (!canvas || !ctx) {
      wx.showToast({ title: '画布未就绪', icon: 'none' });
      return;
    }

    var img = canvas.createImage();
    img.onload = function () {
      // 清空画布并绘制原图
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT);
      // 横图自动旋转90°（拾光tab）
      var drawImgW = img.width;
      var drawImgH = img.height;
      if (tab === 'upload' && img.width > img.height) {
        drawImgW = img.height;
        drawImgH = img.width;
      }

      var fit = fitImageToCanvas(drawImgW, drawImgH, CANVAS_WIDTH, CANVAS_HEIGHT, tab === 'camera');

      if (tab === 'upload' && img.width > img.height) {
        // 横图：旋转90°后绘制
        ctx.save();
        ctx.translate(fit.x + fit.w / 2, fit.y + fit.h / 2);
        ctx.rotate(Math.PI / 2);
        ctx.drawImage(img, -fit.h / 2, -fit.w / 2, fit.h, fit.w);
        ctx.restore();
      } else {
        ctx.drawImage(img, fit.x, fit.y, fit.w, fit.h);
      }

      // 处理为 Film 格式并回显（对比度1.2 + 自适应抖动）
      try {
        filmUtils.processAndDisplay(canvas, ctx, 'floydSteinberg', 1.0, 1.2);
      } catch (e) {
        console.error('processAndDisplay error:', e);
      }

      // 启用发送按钮
      if (tab === 'upload') {
        that.setData({ sendDisabled: false });
      } else if (tab === 'camera') {
        that.setData({ sendDisabledCamera: false });
      }
    };
    img.onerror = function () {
      wx.showToast({ title: '图片加载失败', icon: 'none' });
    };
    img.src = tempFilePath;
  },

  // 发送上传图片到设备
  sendUploadToDevice: function () {
    this._sendCanvasToDevice('_canvasUpload', '_ctxUpload', 'upload');
  },

  // 发送拍照图片到设备
  sendCameraToDevice: function () {
    this._sendCanvasToDevice('_canvasCamera', '_ctxCamera', 'camera');
  },

  // 发送语录到设备
  sendQuoteToDevice: function () {
    this._sendCanvasToDevice('_canvasQuote', '_ctxQuote', 'quote');
  },

  // 通用：从 canvas 获取数据并发送
  _sendCanvasToDevice: function (canvasKey, ctxKey, tab) {
    var that = this;
    var canvas = that[canvasKey];
    var ctx = that[ctxKey];
    if (!canvas || !ctx) {
      wx.showToast({ title: '画布未就绪', icon: 'none' });
      return;
    }

    // 从 400×600 竖屏画布提取 600×400 横屏数据
    var imageData = filmUtils.extractLandscapeData(canvas);
    var processedData = filmUtils.processImageData(imageData);
    var header = filmUtils.generateFilmHeader();

    // 合并为完整的 film 文件数据
    var fileData = new Uint8Array(FILM_FILE_TOTAL_SIZE);
    fileData.set(header, 0);
    fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);

    var prefix = tab === 'upload' ? 'Upload' : tab === 'camera' ? 'Camera' : 'Quote';
    that.sendFileViaBle(fileData, prefix, tab);
  },

  // ========== Tab 3: 一言 - 语录 ==========

  // 获取随机语录
  fetchQuote: function () {
    var that = this;
    wx.showLoading({ title: '获取中...' });
    wx.request({
      url: 'https://international.v1.hitokoto.cn/?c=d&c=h&c=k&c=i&encode=json',
      success: function (res) {
        wx.hideLoading();
        if (res.data && res.data.hitokoto) {
          var text = res.data.hitokoto;
          var author = res.data.from_who || res.data.from || '';
          that.setData({
            quoteText: text,
            quoteAuthor: author
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

  // 渲染语录到画布
  renderQuote: function () {
    var that = this;
    var canvas = that._canvasQuote;
    var ctx = that._ctxQuote;
    if (!canvas || !ctx) return;

    var text = that.data.quoteText;
    var author = that.data.quoteAuthor;
    if (!text) return;

    // 选择色彩方案
    var schemeIndex = Math.floor(Math.random() * frameColorSchemes.length);
    var scheme = frameColorSchemes[schemeIndex];

    var drawW = CANVAS_WIDTH;  // 400
    var drawH = CANVAS_HEIGHT; // 600

    ctx.clearRect(0, 0, drawW, drawH);

    // 绘制背景
    ctx.fillStyle = scheme.bg;
    ctx.fillRect(0, 0, drawW, drawH);

    // 顶部装饰线
    ctx.fillStyle = scheme.accent;
    ctx.fillRect(30, 30, 6, 80);

    // 日期
    ctx.fillStyle = scheme.author;
    ctx.font = '18px sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText(formatDate(), 50, 48);

    // 装饰引号
    ctx.fillStyle = scheme.accent;
    ctx.font = 'bold 80px serif';
    ctx.textAlign = 'left';
    ctx.fillText('\u201C', 30, 160);

    // 计算文字区域
    var textX = 50;
    var textY = 170;
    var maxTextWidth = drawW - 80;
    var fontSize = 24;
    var lineHeight = 38;

    // 使用 filmUtils.wrapText 进行换行
    ctx.font = fontSize + 'px sans-serif';
    ctx.fillStyle = scheme.text;
    ctx.textAlign = 'left';

    var lines = filmUtils.wrapText(ctx, text, fontSize, maxTextWidth);
    // 限制最多 8 行
    if (lines.length > 8) {
      lines = lines.slice(0, 8);
      lines[7] = lines[7].slice(0, -1) + '\u2026';
    }

    for (var i = 0; i < lines.length; i++) {
      ctx.fillText(lines[i], textX, textY + i * lineHeight);
    }

    // 作者
    var authorY = textY + lines.length * lineHeight + 30;
    if (author) {
      ctx.fillStyle = scheme.author;
      ctx.font = '18px sans-serif';
      ctx.textAlign = 'right';
      ctx.fillText('\u2014\u2014 ' + author, drawW - 40, authorY);
    }

    // 底部装饰线
    ctx.fillStyle = scheme.accent;
    var bottomY = drawH - 40;
    ctx.fillRect(drawW - 60, bottomY, 30, 6);

    // 电池图标
    var batteryLevel = app.globalData.batteryLevel || 0;
    var battX = drawW - 50;
    var battY = drawH - 30;
    var battW = 24;
    var battH = 12;
    ctx.strokeStyle = scheme.text;
    ctx.lineWidth = 1.5;
    ctx.strokeRect(battX, battY, battW, battH);
    ctx.fillStyle = scheme.text;
    ctx.fillRect(battX + battW, battY + 3, 3, battH - 6);
    var fillW = Math.max(0, (batteryLevel / 100) * (battW - 2));
    ctx.fillRect(battX + 1, battY + 1, fillW, battH - 2);

    // 抖动处理和 Film 格式回显
    var ditherType = that.data.ditherChecked ? 'floydSteinberg' : null;
    filmUtils.processAndDisplay(canvas, ctx, ditherType, 1.0, null);
  },

  // 切换自定义面板
  toggleCustomPanel: function () {
    this.setData({ showCustomPanel: !this.data.showCustomPanel });
  },

  // 自定义文字输入
  onCustomQuoteInput: function (e) {
    this.setData({ customQuoteText: e.detail.value });
  },

  // 自定义作者输入
  onCustomAuthorInput: function (e) {
    this.setData({ customQuoteAuthor: e.detail.value });
  },

  // 生成自定义语录
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

  // 切换抖动
  toggleDither: function () {
    this.setData({ ditherChecked: !this.data.ditherChecked });
    // 如果已有语录，重新渲染
    if (this.data.quoteText) {
      this.renderQuote();
    }
  },

  // ========== BLE 文件传输 ==========
  sendFileViaBle: function (fileData, prefix, tab) {
    var that = this;

    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接蓝牙设备', icon: 'none' });
      return;
    }

    // 确定当前 tab 的状态 key
    var showKey, statusKey, progressKey;
    if (tab === 'upload') {
      showKey = 'showTransferUpload';
      statusKey = 'transferStatusUpload';
      progressKey = 'transferProgressUpload';
    } else if (tab === 'camera') {
      showKey = 'showTransferCamera';
      statusKey = 'transferStatusCamera';
      progressKey = 'transferProgressCamera';
    } else {
      showKey = 'showTransferQuote';
      statusKey = 'transferStatusQuote';
      progressKey = 'transferProgressQuote';
    }

    // 显示传输状态
    var updateObj = {};
    updateObj[showKey] = true;
    updateObj[statusKey] = '准备传输...';
    updateObj[progressKey] = 0;
    that.setData(updateObj);

    var fileName = prefix + '_' + Date.now() + '.film';
    var totalSize = fileData.length;

    // 发送流程
    that._bleSendFile(fileData, fileName, totalSize, showKey, statusKey, progressKey)
      .then(function () {
        var done = {};
        done[statusKey] = '传输完成！';
        done[progressKey] = 100;
        that.setData(done);
        wx.showToast({ title: '传输完成', icon: 'success' });
        setTimeout(function () {
          var hide = {};
          hide[showKey] = false;
          that.setData(hide);
        }, 2000);
      })
      .catch(function (err) {
        var fail = {};
        fail[statusKey] = '传输失败：' + (err.errMsg || err.message || '未知错误');
        that.setData(fail);
        console.error('BLE 传输失败:', err);
      });
  },

  // BLE 文件传输核心逻辑
  _bleSendFile: function (fileData, fileName, totalSize, showKey, statusKey, progressKey) {
    var that = this;

    return new Promise(function (resolve, reject) {
      var step = 0;
      var totalSteps = 4; // START, NAME, LEN, DATA+STOP
      var dataSent = 0;

      var lastDisplayedPct = -1;

      function updateProgress(extraPercent) {
        // 控制步骤占 5%，数据传输占 95%
        var controlWeight = 0.05;
        var dataWeight = 0.95;
        var controlProgress = (step / 4) * controlWeight * 100;
        var dataProgress = totalSize > 0 ? (extraPercent / totalSize) * dataWeight * 100 : 0;
        var pct = Math.min(99, Math.floor(controlProgress + dataProgress));
        // 仅在进度变化时更新 UI
        if (pct !== lastDisplayedPct) {
          lastDisplayedPct = pct;
          var obj = {};
          obj[progressKey] = pct;
          that.setData(obj);
        }
      }

      function updateStatus(msg) {
        var obj = {};
        obj[statusKey] = msg;
        that.setData(obj);
      }

      // Step 1: FILE_START
      updateStatus('发送开始指令...');
      var startPacket = bleUtils.buildFileStartPacket();
      app.sendBlePacket(startPacket).then(function () {
        step = 1;
        updateProgress(0);
        return delay(BLE_CTRL_DELAY);
      }).then(function () {
        // Step 2: FILE_NAME
        updateStatus('发送文件名...');
        var namePacket = bleUtils.buildFileNamePacket(fileName);
        return app.sendBlePacket(namePacket);
      }).then(function () {
        step = 2;
        updateProgress(0);
        return delay(BLE_CTRL_DELAY);
      }).then(function () {
        // Step 3: FILE_LEN
        updateStatus('发送文件长度...');
        var lenPacket = bleUtils.buildFileLenPacket(totalSize);
        return app.sendBlePacket(lenPacket);
      }).then(function () {
        step = 3;
        updateProgress(0);
        return delay(BLE_CTRL_DELAY);
      }).then(function () {
        // Step 4: 发送数据块
        updateStatus('传输数据...');
        var offset = 0;

        function sendChunk() {
          if (offset >= totalSize) {
            return Promise.resolve();
          }
          var end = Math.min(offset + BLE_CHUNK_SIZE, totalSize);
          var chunk = fileData.slice(offset, end);
          var dataPacket = bleUtils.buildFileDataPacket(chunk);
          return app.sendBlePacket(dataPacket).then(function () {
            offset = end;
            dataSent = offset;
            updateProgress(dataSent);
            return delay(BLE_DATA_DELAY);
          }).then(function () {
            return sendChunk();
          });
        }

        return sendChunk();
      }).then(function () {
        step = 4;
        // 发送 FILE_STOP
        updateStatus('发送完成指令...');
        var stopPacket = bleUtils.buildFileStopPacket();
        return app.sendBlePacket(stopPacket);
      }).then(function () {
        resolve();
      }).catch(function (err) {
        reject(err);
      });
    });
  },
});
