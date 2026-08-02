// 模板 - 备忘录
var filmUtils = require('../../../utils/film-utils');
var tplMemo = require('../../../utils/tpl-memo');
var sender = require('../../../utils/template-sender');
var e6pro = require('../../../utils/e6pro');
var app = getApp();

var STORAGE_KEY = 'ff_tpl_memo';

Page({
  data: {
    schemes: tplMemo.SCHEMES,
    schemeIndex: 0,
    items: [],
    inputText: '',
    showTransfer: false,
    transferStatus: '',
    transferProgress: 0
  },

  _canvas: null,
  _ctx: null,
  _items: null,

  onReady: function () {
    this._initCanvas();
  },

  onShow: function () {
    var that = this;
    var saved = null;
    try { saved = wx.getStorageSync(STORAGE_KEY); } catch (e) {}
    if (saved && saved.items && saved.items.length > 0) {
      that._items = saved.items;
      that.setData({ items: saved.items });
    } else {
      that._items = [];
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
    if (!that._canvas || !that._ctx) return;
    var canvas = that._canvas;
    var ctx = that._ctx;
    e6pro.processTemplate(canvas, ctx, function (rec, W, H) {
      var data = tplMemo.buildData(that._items || []);
      tplMemo.render(rec, W, H, data, that.data.schemes[that.data.schemeIndex]);
    });
  },

  _save: function () {
    try {
      wx.setStorageSync(STORAGE_KEY, { items: this._items || [] });
    } catch (e) {}
  },

  onInput: function (e) {
    this.setData({ inputText: e.detail.value || '' });
  },

  addItem: function () {
    var t = (this.data.inputText || '').trim();
    if (!t) {
      wx.showToast({ title: '先输入内容', icon: 'none' });
      return;
    }
    var items = this._items || [];
    if (items.length >= tplMemo.MAX_ITEMS) {
      wx.showToast({ title: '最多 ' + tplMemo.MAX_ITEMS + ' 条', icon: 'none' });
      return;
    }
    items.push({ text: t, done: false });
    this._items = items;
    this.setData({ items: items, inputText: '' });
    this._save();
    this._render();
  },

  toggleItem: function (e) {
    var idx = Number(e.currentTarget.dataset.idx);
    var items = this._items || [];
    if (idx < 0 || idx >= items.length) return;
    items[idx].done = !items[idx].done;
    this.setData({ items: items });
    this._save();
    this._render();
  },

  removeItem: function (e) {
    var idx = Number(e.currentTarget.dataset.idx);
    var items = this._items || [];
    if (idx < 0 || idx >= items.length) return;
    items.splice(idx, 1);
    this._items = items;
    this.setData({ items: items });
    this._save();
    this._render();
  },

  clearAll: function () {
    var that = this;
    wx.showModal({
      title: '清空备忘录',
      content: '确定清空所有条目吗？',
      success: function (res) {
        if (!res.confirm) return;
        that._items = [];
        that.setData({ items: [] });
        that._save();
        that._render();
      }
    });
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
    sender.sendToDevice(fileData, 'tpl-memo', function (status, pct) {
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
