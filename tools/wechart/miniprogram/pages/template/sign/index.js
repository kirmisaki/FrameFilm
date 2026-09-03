// 模板 - 每日一签
var filmUtils = require('../../../utils/film-utils');
var tplSign = require('../../../utils/tpl-sign');
var sender = require('../../../utils/template-sender');
var e6pro = require('../../../utils/e6pro');
var app = getApp();

Page({
  data: {
    boxH: 900,
    schemes: tplSign.SCHEMES,
    schemeIndex: 0,
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0
  },

  _canvas: null,
  _ctx: null,
  _sign: null,

  onReady: function () {
    this._initCanvas();
  },

  onShow: function () {
    var that = this;
    if (!that._sign) {
      that._sign = tplSign.getRandomSign();
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
    if (!that._canvas || !that._ctx || !that._sign) return;
    var canvas = that._canvas;
    var ctx = that._ctx;
    // imgStrategy='adaptive'：印章条走自适应抖动（保留主题色网点质感），文字层保持清晰
    e6pro.processTemplate(canvas, ctx, function (rec, W, H) {
      tplSign.render(rec, W, H, that._sign, that.data.schemes[that.data.schemeIndex]);
    }, { imgStrategy: 'adaptive' });
  },

  refreshSign: function () {
    this._sign = tplSign.getRandomSign();
    this._render();
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
    sender.sendToDevice(fileData, 'tpl-sign', function (status, pct) {
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
