// 模板 - 纪念日倒数
var filmUtils = require('../../../utils/film-utils');
var tplCountdown = require('../../../utils/tpl-countdown');
var sender = require('../../../utils/template-sender');
var e6pro = require('../../../utils/e6pro');
var app = getApp();

var STORAGE_KEY = 'ff_tpl_countdown';

function pad(n) {
  return ('0' + n).slice(-2);
}

// 默认日期：今天 + 100 天
function defaultDateStr() {
  var d = new Date();
  d.setDate(d.getDate() + 100);
  return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate());
}

Page({
  data: {
    schemes: tplCountdown.SCHEMES,
    schemeIndex: 0,
    name: '',
    dateStr: '',
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0
  },

  _canvas: null,
  _ctx: null,

  onReady: function () {
    this._initCanvas();
  },

  onShow: function () {
    var that = this;
    var saved = null;
    try { saved = wx.getStorageSync(STORAGE_KEY); } catch (e) {}
    if (!saved || !saved.dateStr) {
      saved = { name: '', dateStr: defaultDateStr() };
      try { wx.setStorageSync(STORAGE_KEY, saved); } catch (e) {}
    }
    if (that.data.dateStr !== saved.dateStr || that.data.name !== saved.name) {
      that.setData({ name: saved.name, dateStr: saved.dateStr });
    }
    if (!that._canvas) {
      setTimeout(function () { that._initCanvas(); }, 100);
    } else {
      that._render();
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
      canvas.width = filmUtils.getCanvasWidth();
      canvas.height = filmUtils.getCanvasHeight();
      that._canvas = canvas;
      that._ctx = ctx;
      that._render();
    });
  },

  _render: function () {
    var that = this;
    if (!that._canvas || !that._ctx || !that.data.dateStr) return;
    var canvas = that._canvas;
    var ctx = that._ctx;
    // imgStrategy='adaptive'：用户输入的名称（可能含 emoji）走自适应抖动独立层
    e6pro.processTemplate(canvas, ctx, function (rec, W, H) {
      var data = tplCountdown.buildData(that.data.name, that.data.dateStr);
      tplCountdown.render(rec, W, H, data, that.data.schemes[that.data.schemeIndex]);
    }, { imgStrategy: 'adaptive' });
  },

  _save: function () {
    try {
      wx.setStorageSync(STORAGE_KEY, { name: this.data.name, dateStr: this.data.dateStr });
    } catch (e) {}
  },

  onInputName: function (e) {
    var name = e.detail.value || '';
    this.setData({ name: name });
    this._save();
    this._render();
  },

  onPickDate: function (e) {
    this.setData({ dateStr: e.detail.value });
    this._save();
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
    sender.sendToDevice(fileData, 'tpl-countdown', function (status, pct) {
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
