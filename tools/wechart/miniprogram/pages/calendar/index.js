const filmUtils = require('../../utils/film-utils');
const filmPipeline = require('../../utils/film-pipeline');
const tplCalendar = require('../../utils/tpl-calendar');
const e6pro = require('../../utils/e6pro');
const ble = require('../../platform/ble');
const media = require('../../platform/media');
const store = require('../../platform/store');
const toast = require('../../platform/toast');

Page({
  data: {
    statusBarHeight: 24,
    boxW: 0,
    boxH: 0,
    connected: false,
    monthLabel: '',
    schemes: tplCalendar.SCHEMES,
    schemeIndex: 0,
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
    this._date = new Date();
    this.setData({
      statusBarHeight: info.statusBarHeight || 24,
      boxW: boxW,
      boxH: Math.round(boxW * ch / cw),
      monthLabel: this._monthLabel(this._date)
    });
    const sync = function (state) {
      this.setData({ connected: state.connected });
    }.bind(this);
    sync(store.get());
    this._unsub = store.subscribe(sync);
  },

  onReady: function () {
    const query = wx.createSelectorQuery();
    query.select('#calendar-canvas').fields({ node: true, size: true }).exec(function (res) {
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
    ble.cancelTransfer();
  },

  _monthLabel: function (date) {
    return date.getFullYear() + ' 年 ' + (date.getMonth() + 1) + ' 月';
  },

  onPrev: function () {
    this._date = new Date(this._date.getFullYear(), this._date.getMonth() - 1, 1);
    this.setData({ monthLabel: this._monthLabel(this._date) });
    this.render();
  },

  onNext: function () {
    this._date = new Date(this._date.getFullYear(), this._date.getMonth() + 1, 1);
    this.setData({ monthLabel: this._monthLabel(this._date) });
    this.render();
  },

  onToday: function () {
    this._date = new Date();
    this.setData({ monthLabel: this._monthLabel(this._date) });
    this.render();
  },

  onScheme: function (e) {
    const index = Number(e.currentTarget.dataset.index);
    if (index === this.data.schemeIndex) return;
    this.setData({ schemeIndex: index });
    this.render();
  },

  render: function () {
    if (!this.canvas) return;
    const scheme = tplCalendar.SCHEMES[this.data.schemeIndex];
    e6pro.processTemplate(this.canvas, this.ctx, function (rec, W, H) {
      const data = tplCalendar.buildData(this._date);
      tplCalendar.render(rec, W, H, data, scheme);
    }.bind(this));
    this._buildFilm();
  },

  _buildFilm: function () {
    const imageData = filmUtils.extractLandscapeData(this.canvas);
    const result = filmPipeline.buildFromImageData(imageData, {
      ditherType: 'floydSteinberg',
      ditherEnabled: false,
      contrast: 1,
      saturation: 1
    });
    this._fileData = result.fileData;
    filmPipeline.paintPreview(this.ctx, result.preview, this.canvas.width, this.canvas.height);
  },

  _fileName: function () {
    const y = this._date.getFullYear();
    const m = ('0' + (this._date.getMonth() + 1)).slice(-2);
    return ble.normalizeFileName('calendar_' + y + m);
  },

  onUpload: function () {
    if (!this._fileData) {
      toast.show('还没有生成日历', 'warn');
      return;
    }
    if (!this.data.connected) {
      toast.show('请先连接设备', 'warn');
      return;
    }
    if (this.data.transferring) return;
    this.setData({ transferring: true, progress: 0 });
    ble.uploadFilm(this._fileData, this._fileName(), function (progress) {
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
    media.shareFilm(this._fileData, this._fileName()).then(function () {
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
