const filmUtils = require('../../utils/film-utils');
const filmPipeline = require('../../utils/film-pipeline');
const ble = require('../../platform/ble');
const store = require('../../platform/store');
const media = require('../../platform/media');
const toast = require('../../platform/toast');

function deriveName(raw) {
  const text = String(raw || '').replace(/\.[^.]+$/, '');
  return ble.normalizeFileName(text || 'frame');
}

Page({
  data: {
    statusBarHeight: 24,
    boxW: 0,
    boxH: 0,
    connected: false,
    hasImage: false,
    toolTitle: '拾光',
    fileName: 'frame.film',
    ditherEnabled: true,
    transferring: false,
    progress: 0
  },

  onLoad: function (options) {
    const tool = (options && options.tool) === 'camera' ? 'camera' : 'album';
    let info = {};
    try {
      info = typeof wx.getWindowInfo === 'function' ? wx.getWindowInfo() : wx.getSystemInfoSync();
    } catch (e) {}
    const cw = filmUtils.getCanvasWidth();
    const ch = filmUtils.getCanvasHeight();
    const boxW = Math.max(200, (info.windowWidth || 375) - 104);
    const state = store.get();
    this._src = state.pendingImage || '';
    this._rawName = state.pendingName || '';
    if (this._src) store.patch({ pendingImage: '', pendingName: '' });

    this.setData({
      statusBarHeight: info.statusBarHeight || 24,
      boxW: boxW,
      boxH: Math.round(boxW * ch / cw),
      toolTitle: tool === 'camera' ? '定影' : '拾光',
      fileName: deriveName(this._rawName || (tool === 'camera' ? 'camera' : 'frame'))
    });

    const sync = function (s) {
      this.setData({ connected: s.connected });
    }.bind(this);
    sync(state);
    this._unsub = store.subscribe(sync);
  },

  onReady: function () {
    const query = wx.createSelectorQuery();
    query.select('#frame-canvas').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0]) return;
      this.canvas = res[0].node;
      this.ctx = this.canvas.getContext('2d');
      this.canvas.width = filmUtils.getCanvasWidth();
      this.canvas.height = filmUtils.getCanvasHeight();
      this._work = wx.createOffscreenCanvas({
        type: '2d',
        width: filmUtils.getCanvasWidth(),
        height: filmUtils.getCanvasHeight()
      });
      if (this._src) this._load(this._src);
    }.bind(this));
  },

  onUnload: function () {
    if (this._unsub) this._unsub();
    ble.cancelTransfer();
  },

  _load: function (path) {
    filmPipeline.loadImageToCanvas(path, this._work, 'cover').then(function () {
      this.setData({ hasImage: true });
      this._build();
    }.bind(this)).catch(function () {
      toast.show('图片加载失败', 'error');
    });
  },

  _build: function () {
    if (!this._work || !this.ctx) return;
    const result = filmPipeline.buildFromCanvas(this._work, {
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
    this._build();
  },

  onFileName: function (e) {
    this.setData({ fileName: e.detail.value });
  },

  onPick: function () {
    media.chooseImages({ sourceType: ['album'], count: 1 }).then(function (files) {
      if (!files.length) return;
      this._src = files[0].path;
      this._rawName = files[0].name || '';
      this.setData({ fileName: deriveName(this._rawName) });
      this._load(this._src);
    }.bind(this)).catch(function () {});
  },

  onAdvanced: function () {
    if (!this._src) {
      toast.show('请先选择图片', 'warn');
      return;
    }
    store.patch({ pendingImage: this._src, pendingName: this._rawName });
    wx.navigateTo({ url: '/render/pages/film/index' });
  },

  onUpload: function () {
    if (!this._fileData) {
      toast.show('请先选择图片', 'warn');
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
