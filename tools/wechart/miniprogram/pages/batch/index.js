const filmUtils = require('../../utils/film-utils');
const filmPipeline = require('../../utils/film-pipeline');
const ble = require('../../platform/ble');
const store = require('../../platform/store');
const media = require('../../platform/media');
const toast = require('../../platform/toast');

const SAFE_ALGOS = ['adaptive', 'floydSteinberg', 'atkinson', 'stucki', 'jarvis'];

function baseOf(path, fallback) {
  let name = String(path || '').split('/').pop() || fallback;
  return name.replace(/\.[^.]+$/, '');
}

Page({
  data: {
    statusBarHeight: 24,
    connected: false,
    prefix: 'batch_',
    list: [],
    index: 0,
    currentName: '',
    progress: 0,
    overall: 0,
    running: false,
    done: 0,
    failed: 0,
    finished: false
  },

  onLoad: function () {
    let info = {};
    try {
      info = typeof wx.getWindowInfo === 'function' ? wx.getWindowInfo() : wx.getSystemInfoSync();
    } catch (e) {}
    this._paths = (store.get().batchPaths || []).slice();
    this.setData({
      statusBarHeight: info.statusBarHeight || 24,
      list: this._buildList()
    });
    const sync = function (state) {
      this.setData({ connected: state.connected });
    }.bind(this);
    sync(store.get());
    this._unsub = store.subscribe(sync);
  },

  onUnload: function () {
    if (this._unsub) this._unsub();
    this._cancelled = true;
    ble.cancelTransfer();
  },

  _buildList: function () {
    return this._paths.map(function (path, index) {
      return {
        index: index + 1,
        path: path,
        name: baseOf(path, 'image' + (index + 1)),
        status: '待上传'
      };
    });
  },

  _algo: function () {
    let saved = 'adaptive';
    try {
      saved = wx.getStorageSync('ff_create_dither_type') || saved;
    } catch (e) {}
    return SAFE_ALGOS.indexOf(saved) >= 0 ? saved : 'adaptive';
  },

  onSelect: function () {
    if (this.data.running) return;
    media.chooseImages({ sourceType: ['album'], count: 9 }).then(function (files) {
      const additions = files.map(function (f) {
        return f.path;
      });
      this._paths = this._paths.concat(additions);
      this.setData({ list: this._buildList(), finished: false, done: 0, failed: 0, overall: 0 });
    }.bind(this)).catch(function () {});
  },

  onPrefixInput: function (e) {
    this.setData({ prefix: e.detail.value });
  },

  _nameFor: function (index) {
    const item = this.data.list[index];
    const suffix = item ? item.name : ('image' + (index + 1));
    return ble.normalizeFileName((this.data.prefix || 'batch_') + suffix);
  },

  _setStatus: function (index, status) {
    const list = this.data.list.slice();
    if (list[index]) list[index].status = status;
    this.setData({ list: list });
  },

  onStart: function () {
    if (this.data.running) return;
    if (!this.data.connected) {
      toast.show('请先连接设备', 'warn');
      return;
    }
    if (!this._paths || !this._paths.length) {
      toast.show('没有待上传的图片', 'warn');
      return;
    }
    this._cancelled = false;
    this.setData({
      running: true,
      finished: false,
      done: 0,
      failed: 0,
      index: 0,
      progress: 0,
      overall: 0,
      list: this.data.list.map(function (item) {
        return { index: item.index, path: item.path, name: item.name, status: '待上传' };
      })
    });
    this._run(0);
  },

  onCancel: function () {
    this._cancelled = true;
    ble.cancelTransfer();
    this.setData({ running: false });
    toast.show('已取消');
  },

  _run: function (index) {
    if (this._cancelled) return;
    if (index >= this._paths.length) {
      this.setData({ running: false, finished: true, overall: 100, currentName: '' });
      toast.show('批量上传完成', 'ok');
      ble.refreshFileList().catch(function () {});
      return;
    }

    const path = this._paths[index];
    const cfg = filmUtils.getDeviceConfig();
    const canvas = wx.createOffscreenCanvas({
      type: '2d',
      width: cfg.canvasWidth,
      height: cfg.canvasHeight
    });
    const name = this._nameFor(index);

    this._setStatus(index, '上传中');
    this.setData({
      index: index + 1,
      currentName: name,
      progress: 0,
      overall: Math.round(index / this._paths.length * 100)
    });

    filmPipeline.loadImageToCanvas(path, canvas, 'cover').then(function () {
      const result = filmPipeline.buildFromCanvas(canvas, {
        ditherType: this._algo(),
        ditherEnabled: true,
        contrast: 1,
        saturation: 1
      });
      return ble.uploadFilm(result.fileData, name, function (p) {
        this.setData({
          progress: p,
          overall: Math.round((index + p / 100) / this._paths.length * 100)
        });
      }.bind(this));
    }.bind(this)).then(function () {
      this._setStatus(index, '完成');
      this.setData({ done: this.data.done + 1 });
      setTimeout(function () {
        this._run(index + 1);
      }.bind(this), 300);
    }.bind(this)).catch(function (err) {
      if (this._cancelled) return;
      this._setStatus(index, '失败');
      this.setData({
        failed: this.data.failed + 1,
        running: false
      });
      toast.show((err && err.message) || '第 ' + (index + 1) + ' 张失败', 'error');
    }.bind(this));
  },

  onBack: function () {
    if (!this.data.running) {
      wx.navigateBack();
      return;
    }
    wx.showModal({
      title: '正在批量上传',
      content: '离开将取消当前上传，确定继续？',
      confirmText: '离开',
      confirmColor: '#B3261E',
      success: function (res) {
        if (!res.confirm) return;
        this._cancelled = true;
        ble.cancelTransfer();
        wx.navigateBack();
      }.bind(this)
    });
  }
});
