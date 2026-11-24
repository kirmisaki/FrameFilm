const filmUtils = require('../../utils/film-utils');
const filmPipeline = require('../../utils/film-pipeline');
const ble = require('../../platform/ble');
const media = require('../../platform/media');
const store = require('../../platform/store');
const toast = require('../../platform/toast');

const QUOTES = [
  { text: '生活明朗，万物可爱。', author: '汪曾祺' },
  { text: '心之所向，素履以往。', author: '木心' },
  { text: '且将新火试新茶，诗酒趁年华。', author: '苏轼' },
  { text: '愿你我都能成为自己的太阳。', author: '佚名' },
  { text: '慢慢来，比较快。', author: '佚名' },
  { text: '人间烟火气，最抚凡人心。', author: '佚名' },
  { text: '把日子过成诗。', author: '佚名' },
  { text: '今天也要好好生活。', author: '佚名' }
];

const SCHEMES = [
  { name: '黑', accent: '#000000' },
  { name: '红', accent: '#FF0000' },
  { name: '蓝', accent: '#0000FF' },
  { name: '绿', accent: '#29CC14' }
];

Page({
  data: {
    statusBarHeight: 24,
    boxW: 0,
    boxH: 0,
    connected: false,
    text: '生活明朗，万物可爱。',
    author: '汪曾祺',
    schemeIndex: 0,
    customOpen: false,
    fileName: 'quote.film',
    ditherEnabled: true,
    transferring: false,
    progress: 0
  },

  onLoad: function () {
    let info = {};
    try {
      info = typeof wx.getWindowInfo === 'function' ? wx.getWindowInfo() : wx.getSystemInfoSync();
    } catch (e) {}
    const cw = filmUtils.getCanvasWidth();
    const ch = filmUtils.getCanvasHeight();
    const boxW = Math.max(200, (info.windowWidth || 375) - 104);
    this.setData({
      statusBarHeight: info.statusBarHeight || 24,
      boxW: boxW,
      boxH: Math.round(boxW * ch / cw),
      schemeIndex: Math.floor(Math.random() * SCHEMES.length)
    });
    const sync = function (state) {
      this.setData({ connected: state.connected });
    }.bind(this);
    sync(store.get());
    this._unsub = store.subscribe(sync);
  },

  onReady: function () {
    const query = wx.createSelectorQuery();
    query.select('#quote-canvas').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0]) return;
      this.canvas = res[0].node;
      this.ctx = this.canvas.getContext('2d');
      this.canvas.width = filmUtils.getCanvasWidth();
      this.canvas.height = filmUtils.getCanvasHeight();
      this.render();
    }.bind(this));
  },

  onUnload: function () {
    if (this._unsub) this._unsub();
    if (this._timer) clearTimeout(this._timer);
    ble.cancelTransfer();
  },

  onTextInput: function (e) {
    this.setData({ text: e.detail.value });
    this._debounce();
  },

  onAuthorInput: function (e) {
    this.setData({ author: e.detail.value });
    this._debounce();
  },

  _debounce: function () {
    if (this._timer) clearTimeout(this._timer);
    this._timer = setTimeout(this.render.bind(this), 260);
  },

  onToggleCustom: function () {
    this.setData({ customOpen: !this.data.customOpen });
  },

  onApplyCustom: function () {
    this.setData({ schemeIndex: Math.floor(Math.random() * SCHEMES.length) });
    this.render();
  },

  onFetchQuote: function () {
    wx.request({
      url: 'https://international.v1.hitokoto.cn/?c=d&c=h&c=k&c=i&encode=json',
      success: function (res) {
        const data = res.data || {};
        if (data.hitokoto) {
          this.setData({
            text: data.hitokoto,
            author: data.from || '佚名',
            schemeIndex: Math.floor(Math.random() * SCHEMES.length)
          });
          this.render();
        } else {
          this._localQuote();
        }
      }.bind(this),
      fail: function () {
        this._localQuote();
      }.bind(this)
    });
  },

  _localQuote: function () {
    const item = QUOTES[Math.floor(Math.random() * QUOTES.length)];
    this.setData({
      text: item.text,
      author: item.author,
      schemeIndex: Math.floor(Math.random() * SCHEMES.length)
    });
    this.render();
  },

  render: function () {
    if (!this.canvas) return;
    const ctx = this.ctx;
    const CW = this.canvas.width;
    const CH = this.canvas.height;
    const s = CW / 400;
    const scheme = SCHEMES[this.data.schemeIndex];

    ctx.clearRect(0, 0, CW, CH);
    ctx.fillStyle = '#FFFFFF';
    ctx.fillRect(0, 0, CW, CH);

    ctx.fillStyle = scheme.accent;
    ctx.font = 'bold ' + Math.round(72 * s) + 'px serif';
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.fillText('\u201C', 26 * s, 40 * s);

    const fontSize = Math.round(24 * s);
    const lineHeight = Math.round(38 * s);
    const maxWidth = CW - 72 * s;
    ctx.font = 'bold ' + fontSize + 'px sans-serif';
    ctx.fillStyle = '#12100E';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';

    const lines = filmUtils.wrapText(ctx, this.data.text || '', fontSize, maxWidth);
    const display = lines.slice(0, 9);
    const totalH = display.length * lineHeight;
    let y = Math.max(120 * s, (CH - totalH) / 2 - 30 * s);
    display.forEach(function (line) {
      ctx.fillText(line, CW / 2, y);
      y += lineHeight;
    });

    y += 18 * s;
    ctx.font = 'bold ' + Math.round(16 * s) + 'px sans-serif';
    ctx.fillStyle = '#5B544A';
    ctx.fillText('—— ' + (this.data.author || '佚名'), CW / 2, y);

    const date = new Date();
    const dateText = date.getFullYear() + '.' + (date.getMonth() + 1) + '.' + date.getDate();
    ctx.font = Math.round(12 * s) + 'px sans-serif';
    ctx.fillStyle = '#9A9388';
    ctx.fillText(dateText, CW / 2, CH - 34 * s);

    this._renderFilm();
  },

  _renderFilm: function () {
    const imageData = filmUtils.extractLandscapeData(this.canvas);
    const result = filmPipeline.buildFromImageData(imageData, {
      ditherType: 'adaptive',
      ditherEnabled: this.data.ditherEnabled,
      contrast: 1,
      saturation: 1
    });
    this._fileData = result.fileData;
    filmPipeline.paintPreview(this.ctx, result.preview, this.canvas.width, this.canvas.height);
  },

  onDitherToggle: function () {
    this.setData({ ditherEnabled: !this.data.ditherEnabled });
    this.render();
  },

  onFileName: function (e) {
    this.setData({ fileName: e.detail.value });
  },

  onUpload: function () {
    if (!this._fileData) {
      toast.show('请先输入文字', 'warn');
      return;
    }
    if (!this.data.connected) {
      toast.show('请先连接设备', 'warn');
      return;
    }
    if (this.data.transferring) return;
    this.setData({ transferring: true, progress: 0 });
    ble.uploadFilm(this._fileData, ble.normalizeFileName(this.data.fileName), function (progress) {
      this.setData({ progress: progress });
    }.bind(this)).then(function () {
      this.setData({ transferring: false, progress: 100 });
      toast.show('上传完成', 'ok');
      ble.refreshFileList().catch(function () {});
    }.bind(this)).catch(function (err) {
      this.setData({ transferring: false });
      toast.show((err && err.message) || '上传失败', 'error');
    }.bind(this));
  },

  onExport: function () {
    if (!this._fileData) return;
    media.shareFilm(this._fileData, ble.normalizeFileName(this.data.fileName)).then(function () {
      toast.show('已导出', 'ok');
    }).catch(function (err) {
      toast.show((err && err.message) || '导出失败', 'error');
    });
  },

  onBack: function () {
    if (!this.data.transferring) {
      wx.navigateBack();
      return;
    }
    wx.showModal({
      title: '正在发送',
      content: '离开将取消本次发送，确定继续？',
      confirmText: '离开',
      confirmColor: '#B3261E',
      success: function (res) {
        if (!res.confirm) return;
        ble.cancelTransfer();
        wx.navigateBack();
      }
    });
  }
});
