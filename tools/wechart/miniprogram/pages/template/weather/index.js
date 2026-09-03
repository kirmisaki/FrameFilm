// 模板 - 天气
var filmUtils = require('../../../utils/film-utils');
var tplWeather = require('../../../utils/tpl-weather');
var sender = require('../../../utils/template-sender');
var e6pro = require('../../../utils/e6pro');
var app = getApp();

Page({
  data: {
    boxH: 900,
    schemes: tplWeather.SCHEMES,
    schemeIndex: 0,
    cityLabel: '',
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0
  },

  _canvas: null,
  _ctx: null,
  _data: null,

  onReady: function () {
    this._initCanvas();
  },

  onShow: function () {
    var that = this;
    if (!that._data) {
      that._data = tplWeather.getDemoData();
      that.setData({ cityLabel: that._data.city });
    }
    if (!that._canvas) {
      setTimeout(function () { that._initCanvas(); }, 100);
    }
  },

  _initCanvas: function () {
    var that = this;
    if (that._canvas) return;
    var query = wx.createSelectorQuery();
    query.select('#canvas-tpl').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0] || !res[0].node) return;
      var canvas = res[0].node;
      var ctx = canvas.getContext('2d');
      var cw = filmUtils.getCanvasWidth();
      var ch = filmUtils.getCanvasHeight();
      // 预览容器高度随设备画布宽高比自适应（容器宽固定 600rpx），避免非 2:3 画布（Dock/Max）预览纵向拉伸
      var boxH = Math.round(600 * ch / cw);
      canvas.width = cw;
      canvas.height = ch;
      that.setData({ boxH: boxH });
      that._canvas = canvas;
      that._ctx = ctx;
      that._render();
    });
  },

  _render: function () {
    var that = this;
    if (!that._canvas || !that._ctx || !that._data) return;
    var canvas = that._canvas;
    var ctx = that._ctx;
    e6pro.processTemplate(canvas, ctx, function (rec, W, H) {
      tplWeather.render(rec, W, H, that._data, that.data.schemes[that.data.schemeIndex]);
    });
  },

  useDemo: function () {
    var that = this;
    that._data = tplWeather.getDemoData();
    that.setData({ cityLabel: that._data.city });
    that._render();
  },

  selectScheme: function (e) {
    this.setData({ schemeIndex: Number(e.currentTarget.dataset.idx) });
    this._render();
  },

  sendToDevice: function () {
    var that = this;
    if (!that._canvas) {
      wx.showToast({ title: '画布未就绪', icon: 'none' });
      return;
    }
    that.setData({ showTransfer: true, transferStatus: '准备传输...', transferProgress: 0 });
    var fileData = e6pro.canvasToFilmData(that._canvas);
    sender.sendToDevice(fileData, 'tpl-weather', function (status, pct) {
      if (pct === 100) {
        that.setData({ transferProgress: 100, transferStatus: status });
        setTimeout(function () { that.setData({ showTransfer: false }); }, 1500);
      } else if (pct === -1) {
        that.setData({ transferStatus: status });
        setTimeout(function () { that.setData({ showTransfer: false }); }, 2000);
      } else {
        that.setData({ transferProgress: pct });
        if (status) { that.setData({ transferStatus: status }); }
      }
    }).catch(function () {
      setTimeout(function () { that.setData({ showTransfer: false }); }, 2000);
    });
  }
});
