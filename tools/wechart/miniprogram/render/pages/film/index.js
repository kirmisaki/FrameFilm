const filmUtils = require('../../../utils/film-utils');
const filmPipeline = require('../../../utils/film-pipeline');
const algorithms = require('../../../utils/algorithms');
const sourcefit = require('../../../utils/sourcefit');
const store = require('../../../platform/store');
const ble = require('../../../platform/ble');
const media = require('../../../platform/media');
const toast = require('../../../platform/toast');
const colorfast = require('../../lib/colorfast');

// 高级算法（AE / SZ / 校色）在分包内注入，不占主包
filmUtils.attachDitherAdvanced(require('../../lib/dither-advanced'));

function deriveName(raw) {
  const text = String(raw || '').replace(/\.[^.]+$/, '');
  return ble.normalizeFileName(text || 'image');
}

Page({
  data: {
    statusBarHeight: 24,
    boxW: 0,
    boxH: 0,
    connected: false,
    deviceLabel: '',
    hasImage: false,
    fileName: '',
    algoValue: 'atkinsonEnhanced',
    algoOptions: [],
    showStrength: false,
    showColor: true,
    showSourceFit: true,
    ditherEnabled: true,
    strength: 1,
    contrast: 1,
    saturation: 1,
    rangeFit: 0,
    warmChroma: 0,
    rotation: 0,
    adaptiveText: '',
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
    const boxH = Math.round(boxW * ch / cw);

    this.setData({
      statusBarHeight: info.statusBarHeight || 24,
      boxW: boxW,
      boxH: boxH
    });

    const state = store.get();
    this._onState = function (s) {
      this._refreshAlgorithms(s);
      this.setData({
        connected: s.connected,
        deviceLabel: s.screenName ? (s.screenName + ' · ' + s.width + '×' + s.height) : ''
      });
      this._syncCanvas(s);
    }.bind(this);
    this._unsub = store.subscribe(this._onState);
    this._onState(state);

    this._src = state.pendingImage || '';
    this._rawName = state.pendingName || '';
    if (this._src) {
      store.patch({ pendingImage: '', pendingName: '' });
    }
  },

  onReady: function () {
    const query = wx.createSelectorQuery();
    query.select('#film-canvas').fields({ node: true, size: true }).exec(function (res) {
      if (!res || !res[0]) return;
      this.canvas = res[0].node;
      this.ctx = this.canvas.getContext('2d');
      const cw = filmUtils.getCanvasWidth();
      const ch = filmUtils.getCanvasHeight();
      this.canvas.width = cw;
      this.canvas.height = ch;
      this.tempCanvas = wx.createOffscreenCanvas({ type: '2d', width: cw, height: ch });
      this.tempCtx = this.tempCanvas.getContext('2d');
      if (this._src) {
        this.setData({ fileName: deriveName(this._rawName) });
        this._loadImage(this._src);
      }
    }.bind(this));
  },

  onUnload: function () {
    if (this._unsub) this._unsub();
    ble.cancelTransfer();
  },

  _syncCanvas: function (state) {
    if (!this.canvas) return;
    const cw = filmUtils.getCanvasWidth();
    const ch = filmUtils.getCanvasHeight();
    if (this.canvas.width === cw && this.canvas.height === ch) return;

    this.canvas.width = cw;
    this.canvas.height = ch;
    this.tempCanvas = wx.createOffscreenCanvas({ type: '2d', width: cw, height: ch });
    this.tempCtx = this.tempCanvas.getContext('2d');

    const info = typeof wx.getWindowInfo === 'function' ? wx.getWindowInfo() : wx.getSystemInfoSync();
    const boxW = Math.max(200, (info.windowWidth || 375) - 104);
    this.setData({ boxW: boxW, boxH: Math.round(boxW * ch / cw) });

    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    if (this.imageNode) this.updateImage();
  },

  _refreshAlgorithms: function (state) {
    const key = (state.model || '') + '|' + (state.panelId || 0);
    if (this._algoKey === key && this.data.algoOptions.length) return;
    this._algoKey = key;

    const list = algorithms.listFor({
      model: state.model,
      panelId: state.panelId,
      is4Color: state.panelId === 0x08
    });
    const options = list.map(function (item) {
      return { label: item.name, value: item.id };
    });
    let value = this.data.algoValue;
    let valid = list.some(function (item) {
      return item.id === value;
    });
    if (!valid) {
      value = 'atkinsonEnhanced';
      valid = list.some(function (item) {
        return item.id === value;
      });
      if (!valid) {
        value = 'adaptive';
        valid = list.some(function (item) {
          return item.id === value;
        });
      }
      if (!valid) value = list[0] ? list[0].id : 'floydSteinberg';
    }
    const meta = algorithms.find(value) || {};
    this.setData({
      algoOptions: options,
      algoValue: value,
      showStrength: !!meta.strength,
      showColor: meta.color !== false,
      showSourceFit: meta.sourceFit !== false
    });
  },

  _loadImage: function (path) {
    if (!this.canvas) return;
    const img = this.canvas.createImage();
    img.onload = function () {
      this.imageNode = img;
      this.scale = 1;
      this.offsetX = 0;
      this.offsetY = 0;
      this.setData({ hasImage: true, rotation: 0, adaptiveText: '' });
      this.updateImage();
    }.bind(this);
    img.onerror = function () {
      toast.show('图片加载失败', 'error');
    };
    img.src = path;
  },

  _drawToCanvas: function () {
    if (!this.canvas || !this.imageNode || !this.tempCanvas) return;
    const tctx = this.tempCtx;
    const CW = filmUtils.getCanvasWidth();
    const CH = filmUtils.getCanvasHeight();
    const img = this.imageNode;
    const rotation = this.data.rotation;

    tctx.clearRect(0, 0, CW, CH);
    tctx.fillStyle = '#ffffff';
    tctx.fillRect(0, 0, CW, CH);

    const radians = rotation * Math.PI / 180;
    const visW = (rotation === 90 || rotation === 270) ? img.height : img.width;
    const visH = (rotation === 90 || rotation === 270) ? img.width : img.height;
    const fit = Math.min(CW / visW, CH / visH) * this.scale;
    const finalW = img.width * fit;
    const finalH = img.height * fit;
    const cx = CW / 2 + this.offsetX;
    const cy = CH / 2 + this.offsetY;

    tctx.save();
    tctx.translate(cx, cy);
    tctx.rotate(radians);
    tctx.drawImage(img, -finalW / 2, -finalH / 2, finalW, finalH);
    tctx.restore();
  },

  _quickDraw: function () {
    this._drawToCanvas();
    const CW = filmUtils.getCanvasWidth();
    const CH = filmUtils.getCanvasHeight();
    this.ctx.clearRect(0, 0, CW, CH);
    this.ctx.drawImage(this.tempCanvas, 0, 0, CW, CH, 0, 0, CW, CH);
  },

  updateImage: function () {
    if (!this.imageNode) return;
    this._drawToCanvas();
    this._applyProcessing();
  },

  _sourceProfile: function (type) {
    if (type === 'colorFast55') return sourcefit.profiles.fast;
    if (type === 'colorQual') return sourcefit.profiles.legacyQual;
    if (type === 'colorQualMeasured' || type === 'colorQualCalibrated') {
      const p = colorfast.PROFILES[type];
      return sourcefit.measured(p.palette[3], p.palette[15]);
    }
    return sourcefit.profiles.native6;
  },

  _applyProcessing: function () {
    if (!this.tempCanvas || !this.imageNode) return;
    const type = this.data.algoValue;
    const strength = this.data.strength;
    const ditherEnabled = this.data.ditherEnabled;
    const isSZ = type === 'szEnhanced';
    const bpp8 = algorithms.isBpp8(type) && store.get().model === 'FRAMEFILMARK' && store.get().panelId === 0x02;

    let imageData = filmUtils.extractLandscapeData(this.tempCanvas);

    if (!isSZ) {
      if (this.data.contrast !== 1) filmUtils.adjustContrast(imageData, this.data.contrast);
      if (this.data.saturation !== 1) filmUtils.adjustSaturation(imageData, this.data.saturation);
      if (type !== 'atkinsonSzCalib' && (this.data.rangeFit || this.data.warmChroma)) {
        sourcefit.apply(imageData, this.data.rangeFit, this.data.warmChroma, this._sourceProfile(type));
      }
    }

    const cfg = filmUtils.getDeviceConfig();
    let preview;
    let fileData;

    if (bpp8) {
      const profile = colorfast.PROFILES[type];
      const quantized = colorfast.quantize(imageData, strength, ditherEnabled, profile);
      const pixelData = colorfast.pack(quantized, profile, cfg.pixelLayout);
      const header = colorfast.header(profile, cfg.screenWidth, cfg.screenHeight);
      fileData = new Uint8Array(header.length + pixelData.length);
      fileData.set(header, 0);
      fileData.set(pixelData, header.length);
      preview = colorfast.decode(pixelData, cfg.screenWidth, cfg.screenHeight, profile, cfg.pixelLayout);
    } else {
      const processed = ditherEnabled ? filmUtils.applyDitherByType(imageData, type, strength) : imageData;
      const pixelData = filmUtils.processImageData(processed);
      const header = filmUtils.generateFilmHeader();
      fileData = new Uint8Array(header.length + pixelData.length);
      fileData.set(header, 0);
      fileData.set(pixelData, header.length);
      preview = filmUtils.decodeProcessedData(pixelData, cfg.screenWidth, cfg.screenHeight);
    }

    this._fileData = fileData;
    filmPipeline.paintPreview(this.ctx, preview, filmUtils.getCanvasWidth(), filmUtils.getCanvasHeight());

    let adaptiveText = '';
    if (ditherEnabled && type === 'adaptive') {
      const choice = filmUtils.getLastAdaptiveChoice();
      const label = (algorithms.find(choice.type) || {}).name || choice.type;
      adaptiveText = '自适应选择：' + label + '，强度 ' + Number(choice.strength).toFixed(1);
    }
    this.setData({ adaptiveText: adaptiveText });
  },

  onTouchStart: function (e) {
    if (e.touches.length === 1) {
      this._touchStartX = e.touches[0].clientX;
      this._touchStartY = e.touches[0].clientY;
      this._startOffsetX = this.offsetX;
      this._startOffsetY = this.offsetY;
      this._isPinching = false;
    } else if (e.touches.length === 2) {
      this._isPinching = true;
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      this._startDist = Math.sqrt(dx * dx + dy * dy) || 1;
      this._startScale = this.scale;
    }
  },

  onTouchMove: function (e) {
    if (!this.imageNode) return;
    if (e.touches.length === 1 && !this._isPinching) {
      const dx = e.touches[0].clientX - this._touchStartX;
      const dy = e.touches[0].clientY - this._touchStartY;
      this.offsetX = this._startOffsetX + dx * 0.5;
      this.offsetY = this._startOffsetY + dy * 0.5;
      this._quickDraw();
    } else if (e.touches.length === 2 && this._isPinching) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      const dist = Math.sqrt(dx * dx + dy * dy) || 1;
      this.scale = Math.max(0.05, Math.min(10, this._startScale * (dist / this._startDist)));
      this._quickDraw();
    }
  },

  onTouchEnd: function (e) {
    if (e.touches.length < 2) this._isPinching = false;
    this.updateImage();
  },

  onAlgoChange: function (e) {
    const value = e.detail.value;
    const meta = algorithms.find(value) || {};
    this.setData({
      algoValue: value,
      showStrength: !!meta.strength,
      showColor: meta.color !== false,
      showSourceFit: meta.sourceFit !== false
    });
    this.updateImage();
  },

  onDitherToggle: function () {
    this.setData({ ditherEnabled: !this.data.ditherEnabled });
    this.updateImage();
  },

  onStrengthChanging: function (e) {
    this.setData({ strength: e.detail.value });
  },
  onStrengthChange: function (e) {
    this.setData({ strength: e.detail.value });
    this.updateImage();
  },

  onContrastChanging: function (e) {
    this.setData({ contrast: e.detail.value });
  },
  onContrastChange: function (e) {
    this.setData({ contrast: e.detail.value });
    this.updateImage();
  },

  onSaturationChanging: function (e) {
    this.setData({ saturation: e.detail.value });
  },
  onSaturationChange: function (e) {
    this.setData({ saturation: e.detail.value });
    this.updateImage();
  },

  onRangeFitChanging: function (e) {
    this.setData({ rangeFit: e.detail.value });
  },
  onRangeFitChange: function (e) {
    this.setData({ rangeFit: e.detail.value });
    this.updateImage();
  },

  onWarmChanging: function (e) {
    this.setData({ warmChroma: e.detail.value });
  },
  onWarmChange: function (e) {
    this.setData({ warmChroma: e.detail.value });
    this.updateImage();
  },

  onRotate: function () {
    this.setData({ rotation: (this.data.rotation + 90) % 360 });
    this.offsetX = 0;
    this.offsetY = 0;
    this.scale = 1;
    this.updateImage();
  },

  onResetZoom: function () {
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.updateImage();
  },

  onReset: function () {
    const ids = (this.data.algoOptions || []).map(function (item) {
      return item.value;
    });
    const id = ids.indexOf('atkinsonEnhanced') >= 0 ? 'atkinsonEnhanced' : (ids[0] || 'floydSteinberg');
    const meta = algorithms.find(id) || {};
    this.setData({
      rotation: 0,
      ditherEnabled: true,
      algoValue: id,
      strength: 1,
      contrast: 1,
      saturation: 1,
      rangeFit: 0,
      warmChroma: 0,
      adaptiveText: '',
      showStrength: !!meta.strength,
      showColor: meta.color !== false,
      showSourceFit: meta.sourceFit !== false
    });
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.updateImage();
  },

  onPickAnother: function () {
    media.chooseImages({ sourceType: ['album'], count: 1 }).then(function (files) {
      if (!files.length) return;
      this._fileData = null;
      this.setData({ fileName: deriveName(files[0].name), hasImage: false });
      this._loadImage(files[0].path);
    }.bind(this)).catch(function () {});
  },

  onFileNameInput: function (e) {
    this.setData({ fileName: e.detail.value });
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
    const fileName = ble.normalizeFileName(this.data.fileName);
    this.setData({ transferring: true, progress: 0 });
    ble.uploadFilm(this._fileData, fileName, function (progress) {
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
    if (!this._fileData) {
      toast.show('请先选择图片', 'warn');
      return;
    }
    const fileName = ble.normalizeFileName(this.data.fileName);
    media.shareFilm(this._fileData, fileName).then(function () {
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
