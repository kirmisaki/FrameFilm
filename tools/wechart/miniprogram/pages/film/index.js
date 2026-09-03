const filmUtils = require('../../utils/film-utils');
const bleUtils = require('../../utils/ble-utils');
const app = getApp();

// ===== 抖动算法目录（与 ForFilm Web 对齐 10 种）=====
// 清单统一由 utils/dither-config.js 提供（film 页 / 设置页「创作页默认算法」/ 四个创作页共用，避免漂移）
var ditherCfg = require('../../utils/dither-config');
var DITHER_BASE = ditherCfg.DITHER_BASE;
var STRENGTH_TYPES = ditherCfg.STRENGTH_TYPES;

Page({
  data: {
    ditherEnabled: false,
    ditherTypeIndex: 0,
    ditherTypes: DITHER_BASE.map(function (it) { return it.type; }),
    ditherTypeNames: DITHER_BASE.map(function (it) { return it.name; }),
    ditherStrength: 1.0,
    ditherStrengthDisplay: '1.0',
    contrast: 1.0,
    contrastInt: 100,
    saturation: 1.0,
    saturationInt: 10,
    ditherChoice: '',
    fileName: filmUtils.generateRandomFilename('output'),
    rotation: 0,
    hasImage: false,
    showStrength: false,
    showColorAdjust: true,
    transferStatus: '',
    transferProgress: 0,
    showTransfer: false,
    resultText: '',
    isConnected: false,
    deviceName: '',
    canvasBoxHeight: 810
  },

  canvas: null,
  ctx: null,
  tempCanvas: null,
  tempCtx: null,
  imageInfo: null,
  imageNode: null,
  scale: 1,
  offsetX: 0,
  offsetY: 0,
  _touchStartX: 0,
  _touchStartY: 0,
  _startOffsetX: 0,
  _startOffsetY: 0,
  _startDist: 0,
  _startScale: 1,
  _isPinching: false,

  onShow() {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 2 });
    }
    this.setData({
      isConnected: app.globalData.isConnected,
      deviceName: app.globalData.deviceName || ''
    });
    // 设备类型可能变化（如重连 MAX/Dock），画布尺寸需要同步
    this._syncCanvasToDevice();
    this._refreshDitherLists();
  },

  // 按当前机型刷新抖动算法选择列表（SZ 增强/校色仅 Pro 可选，对齐 Web 端禁用逻辑）
  _refreshDitherLists: function () {
    var opts = ditherCfg.ditherOptionsFor(filmUtils.getDeviceType());
    var ditherTypes = opts.types;
    var ditherTypeNames = opts.names;
    var oldTypes = this.data.ditherTypes || [];
    var currentType = oldTypes[this.data.ditherTypeIndex];
    var newIndex = ditherTypes.indexOf(currentType);
    if (newIndex < 0) {
      // 当前选中项在新列表不可用（非 Pro 下选中的 SZ 增强/校色）→ 回退 Floyd-Steinberg
      newIndex = ditherTypes.indexOf('floydSteinberg');
      if (newIndex < 0) newIndex = 0;
    }
    this.setData({
      ditherTypes: ditherTypes,
      ditherTypeNames: ditherTypeNames,
      ditherTypeIndex: newIndex,
      showStrength: STRENGTH_TYPES.indexOf(ditherTypes[newIndex]) >= 0,
      showColorAdjust: ditherTypes[newIndex] !== 'szEnhanced'
    });
  },

  // 画布尺寸与显示比例跟随当前设备配置（机型切换/0x42 校准后）
  _syncCanvasToDevice: function () {
    var cw = filmUtils.getCanvasWidth();
    var ch = filmUtils.getCanvasHeight();
    // CSS 宽度固定 540rpx，高度按画布宽高比换算保持不失真
    var boxH = Math.round(540 * ch / cw);
    if (this.data.canvasBoxHeight !== boxH) {
      this.setData({ canvasBoxHeight: boxH });
    }
    if (!this.canvas) return;
    if (this.canvas.width === cw && this.canvas.height === ch) return;
    this.canvas.width = cw;
    this.canvas.height = ch;
    this.tempCanvas = wx.createOffscreenCanvas({ type: '2d', width: cw, height: ch });
    this.tempCtx = this.tempCanvas.getContext('2d');
    // 分辨率变化会打乱当前图片布局，重置编辑状态
    if (this.imageNode) {
      this.setData({
        hasImage: false, rotation: 0, ditherEnabled: false,
        ditherTypeIndex: 0, ditherStrength: 1.0,
        contrast: 1.0, contrastInt: 100,
        saturation: 1.0, saturationInt: 10,
        showStrength: false, showColorAdjust: true
      });
      this.imageNode = null;
      this.imageInfo = null;
      this.scale = 1;
      this.offsetX = 0;
      this.offsetY = 0;
      this.ctx.clearRect(0, 0, cw, ch);
      wx.showToast({ title: '设备分辨率已变化，请重新选图', icon: 'none' });
    }
  },

  onReady() {
    const query = wx.createSelectorQuery();
    query.select('#film-canvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0]) return;
      this.canvas = res[0].node;
      this.ctx = this.canvas.getContext('2d');
      var cw = filmUtils.getCanvasWidth();
      var ch = filmUtils.getCanvasHeight();
      this.canvas.width = cw;
      this.canvas.height = ch;

      // 临时画布（与显示画布一致）
      this.tempCanvas = wx.createOffscreenCanvas({ type: '2d', width: cw, height: ch });
      this.tempCtx = this.tempCanvas.getContext('2d');
    });
  },

  chooseImage() {
    if (!this.canvas || !this.tempCanvas) {
      wx.showToast({ title: '画布初始化中，请稍后重试', icon: 'none' });
      return;
    }
    wx.chooseMedia({
      count: 1,
      mediaType: ['image'],
      sourceType: ['album'],
      success: (res) => {
        const tempFilePath = res.tempFiles[0].tempFilePath;
        // 文件名沿用原图名（与 ForFilm 一致）；chooseMedia 拿不到原名时退回随机名
        const srcName = (res.tempFiles[0] && res.tempFiles[0].name) || '';
        const fileName = srcName ? srcName.replace(/\.[^.]+$/, '') + '.film' : this.data.fileName;
        const img = this.canvas.createImage();
        img.onload = () => {
          this.imageNode = img;
          this.imageInfo = { width: img.width, height: img.height, path: tempFilePath };
          this.scale = 1;
          this.offsetX = 0;
          this.offsetY = 0;
          this.setData({
            hasImage: true,
            rotation: 0,
            ditherEnabled: false,
            ditherTypeIndex: 0,
            ditherStrength: 1.0,
            ditherStrengthDisplay: '1.0',
            contrast: 1.0,
            contrastInt: 100,
            saturation: 1.0,
            saturationInt: 10,
            ditherChoice: '',
            showStrength: false,
            showColorAdjust: true,
            fileName: fileName,
            resultText: ''
          });
          this.updateImage();
        };
        img.onerror = () => {
          wx.showToast({ title: '图片加载失败', icon: 'none' });
        };
        img.src = tempFilePath;
      }
    });
  },

  _drawToCanvas() {
    if (!this.canvas || !this.imageNode || !this.tempCanvas) return;
    const tctx = this.tempCtx;
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    const img = this.imageNode;
    const rotation = this.data.rotation;

    // 1. 在临时画布上绘制
    tctx.clearRect(0, 0, CW, CH);
    tctx.fillStyle = '#ffffff';
    tctx.fillRect(0, 0, CW, CH);

    const radians = (rotation * Math.PI) / 180;
    const imgW = img.width;
    const imgH = img.height;

    // 计算旋转后的视觉尺寸
    var visW, visH;
    if (rotation === 90 || rotation === 270) {
      visW = imgH; visH = imgW;
    } else {
      visW = imgW; visH = imgH;
    }
    var fitScale = Math.min(CW / visW, CH / visH) * this.scale;
    var finalW = imgW * fitScale;
    var finalH = imgH * fitScale;
    var cx = CW / 2 + this.offsetX;
    var cy = CH / 2 + this.offsetY;

    tctx.save();
    tctx.translate(cx, cy);
    tctx.rotate(radians);
    tctx.drawImage(img, -finalW / 2, -finalH / 2, finalW, finalH);
    tctx.restore();
  },

  // 只画原图不处理，用于拖动缩放时保持流畅
  _quickDraw() {
    this._drawToCanvas();
    var dctx = this.ctx;
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    dctx.clearRect(0, 0, CW, CH);
    dctx.drawImage(this.tempCanvas, 0, 0, CW, CH, 0, 0, CW, CH);
  },

  // 完整处理：绘图 + 抖动算法
  updateImage() {
    this._drawToCanvas();
    this._applyProcessing();
    this._syncAdaptiveChoice();
  },

  _applyProcessing() {
    const tctx = this.tempCtx;
    var CW = filmUtils.getCanvasWidth();
    var CH = filmUtils.getCanvasHeight();
    if (this.data.ditherEnabled) {
      var ditherType = this.data.ditherTypes[this.data.ditherTypeIndex];
      filmUtils.processAndDisplay(this.tempCanvas, tctx, ditherType, this.data.ditherStrength, this.data.contrast, this.data.saturation);
    } else {
      // 未开启抖动：仅调整对比度/饱和度，显示原图。
      // szEnhanced 仍走原始图流水线（对齐 Web：该算法下整组色彩滑块隐藏且不生效）
      var ditherType = this.data.ditherTypes[this.data.ditherTypeIndex];
      if (ditherType !== 'szEnhanced') {
        var imageData = tctx.getImageData(0, 0, CW, CH);
        var touched = false;
        if (this.data.contrast !== 1.0) {
          filmUtils.adjustContrast(imageData, this.data.contrast);
          touched = true;
        }
        if (this.data.saturation !== 1.0) {
          filmUtils.adjustSaturation(imageData, this.data.saturation);
          touched = true;
        }
        if (touched) {
          tctx.putImageData(imageData, 0, 0);
        }
      }
    }

    // 3. 复制到显示画布
    const dctx = this.ctx;
    dctx.clearRect(0, 0, CW, CH);
    dctx.drawImage(this.tempCanvas, 0, 0, CW, CH, 0, 0, CW, CH);
  },

  // 自适应算法结束后展示其选择结果（对齐 ForFilm 结果行「自适应选择：X，强度 Y」）
  _syncAdaptiveChoice() {
    var type = this.data.ditherTypes[this.data.ditherTypeIndex];
    if (this.data.ditherEnabled && type === 'adaptive') {
      var choice = filmUtils.getLastAdaptiveChoice();
      var label = ditherCfg.ditherTypeLabel(choice.type);
      var text = '自适应选择：' + label + '，强度 ' + Number(choice.strength).toFixed(1);
      if (this.data.ditherChoice !== text) {
        this.setData({ ditherChoice: text });
      }
    } else if (this.data.ditherChoice) {
      this.setData({ ditherChoice: '' });
    }
  },

  toggleDither() {
    this.setData({ ditherEnabled: !this.data.ditherEnabled });
    this.updateImage();
  },

  onDitherTypeChange(e) {
    var index = parseInt(e.detail.value);
    var type = this.data.ditherTypes[index];
    // 防御：列表不含该算法时不做切换（正常不会触发，双保险对齐 Web 禁用逻辑）
    if (!type) return;
    this.setData({
      ditherTypeIndex: index,
      showStrength: STRENGTH_TYPES.indexOf(type) >= 0,
      showColorAdjust: type !== 'szEnhanced'
    });
    this.updateImage();
  },

  onDitherStrengthChange(e) {
    var val = parseFloat(parseFloat(e.detail.value).toFixed(1));
    this.setData({ ditherStrength: val, ditherStrengthDisplay: val.toFixed(1) });
    this.updateImage();
  },

  onDitherStrengthChanging(e) {
    var val = parseFloat(parseFloat(e.detail.value).toFixed(1));
    this.setData({ ditherStrength: val, ditherStrengthDisplay: val.toFixed(1) });
  },

  onContrastChange(e) {
    var val = parseInt(e.detail.value) / 100;
    this.setData({ contrast: val, contrastInt: parseInt(e.detail.value) });
    this.updateImage();
  },

  onContrastChanging(e) {
    var val = parseInt(e.detail.value) / 100;
    this.setData({ contrast: val, contrastInt: parseInt(e.detail.value) });
  },

  onSaturationChange(e) {
    var val = parseInt(e.detail.value) / 10;
    this.setData({ saturation: val, saturationInt: parseInt(e.detail.value) });
    this.updateImage();
  },

  onSaturationChanging(e) {
    var val = parseInt(e.detail.value) / 10;
    this.setData({ saturation: val, saturationInt: parseInt(e.detail.value) });
  },

  rotateCanvas() {
    const newRotation = (this.data.rotation + 90) % 360;
    this.setData({ rotation: newRotation });
    this.offsetX = 0;
    this.offsetY = 0;
    this.scale = 1;
    this.updateImage();
  },

  // 重置缩放/位置：回到等比铺满并居中（对齐 ForFilm fit_screen 重置缩放）
  resetZoom() {
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.updateImage();
  },

  resetImage() {
    this.setData({
      hasImage: false,
      rotation: 0,
      ditherEnabled: false,
      ditherTypeIndex: 0,
      ditherStrength: 1.0,
      contrast: 1.0,
      contrastInt: 100,
      saturation: 1.0,
      saturationInt: 10,
      ditherChoice: '',
      showStrength: false,
      showColorAdjust: true,
      resultText: ''
    });
    this.imageNode = null;
    this.imageInfo = null;
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    if (this.canvas) {
      this.ctx.clearRect(0, 0, filmUtils.getCanvasWidth(), filmUtils.getCanvasHeight());
    }
  },

  onTouchStart(e) {
    if (this.data.ditherEnabled) return;
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
      this._startDist = Math.sqrt(dx * dx + dy * dy);
      this._startScale = this.scale;
    }
  },

  onTouchMove(e) {
    if (this.data.ditherEnabled) return;
    if (e.touches.length === 1 && !this._isPinching) {
      const dx = e.touches[0].clientX - this._touchStartX;
      const dy = e.touches[0].clientY - this._touchStartY;
      this.offsetX = this._startOffsetX + dx * 0.5;
      this.offsetY = this._startOffsetY + dy * 0.5;
      this._quickDraw();
    } else if (e.touches.length === 2 && this._isPinching) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const scaleChange = dist / this._startDist;
      // 缩放范围与 ForFilm 对齐：0.05~10
      this.scale = Math.max(0.05, Math.min(10, this._startScale * scaleChange));
      this._quickDraw();
    }
  },

  onTouchEnd(e) {
    if (this.data.ditherEnabled) return;
    if (e.touches.length < 2) {
      this._isPinching = false;
    }
    this._applyProcessing();
  },

  onFileNameInput(e) {
    this.setData({ fileName: e.detail.value });
  },

  // 从画布提取横屏 Film 数据
  getFilmImageData() {
    return filmUtils.extractLandscapeData(this.tempCanvas);
  },

  downloadFilm() {
    if (!this.canvas || !this.imageNode) return;

    wx.showLoading({ title: '处理中...' });

    var imageData = this.getFilmImageData();
    const processedData = filmUtils.processImageData(imageData);
    const header = filmUtils.generateFilmHeader();

    // 合并 header + pixelData
    var totalSize = filmUtils.getFilmFileTotalSize();
    const fileData = new Uint8Array(totalSize);
    fileData.set(header, 0);
    fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);

    const fileName = this.data.fileName || filmUtils.generateRandomFilename('output');
    const filePath = `${wx.env.USER_DATA_PATH}/${fileName}`;

    const fs = wx.getFileSystemManager();
    fs.writeFile({
      filePath: filePath,
      data: fileData.buffer,
      encoding: 'binary',
      success: () => {
        wx.hideLoading();
        // 尝试分享文件
        wx.shareFileMessage({
          filePath: filePath,
          success: () => {
            this.setData({ resultText: '文件已分享' });
          },
          fail: (err) => {
            // 分享失败则显示保存路径
            this.setData({ resultText: '文件已保存: ' + filePath });
          }
        });
      },
      fail: (err) => {
        wx.hideLoading();
        this.setData({ resultText: '保存失败: ' + (err.errMsg || '未知错误') });
      }
    });
  },

  sendToDevice() {
    if (!this.canvas || !this.imageNode) {
      wx.showToast({ title: '请先选择图片', icon: 'none' });
      return;
    }
    if (!app.globalData.isConnected) {
      wx.showToast({ title: '请先连接蓝牙设备', icon: 'none' });
      return;
    }

    wx.showLoading({ title: '处理中...' });

    // 从画布提取数据
    var imageData = this.getFilmImageData();
    const processedData = filmUtils.processImageData(imageData);
    const header = filmUtils.generateFilmHeader();

    // 合并 header + pixelData
    var totalSize = filmUtils.getFilmFileTotalSize();
    const fileData = new Uint8Array(totalSize);
    fileData.set(header, 0);
    fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);

    wx.hideLoading();

    const fileName = this.data.fileName || filmUtils.generateRandomFilename('output');
    this.sendFileViaBle(fileData, fileName);
  },

  // 分享给朋友
  onShareAppMessage: function () {
    return {
      title: 'FrameFilm - Film',
      path: '/pages/film/index'
    };
  },

  sendFileViaBle(fileData, fileName) {
    const that = this;
    const chunkSize = bleUtils.BLE_CHUNK_SIZE;
    const totalSize = fileData.length;
    const totalChunks = Math.ceil(totalSize / chunkSize);

    that.setData({
      showTransfer: true,
      transferStatus: '准备发送...',
      transferProgress: 0,
      resultText: ''
    });

    // 发送流程：FILE_START -> FILE_NAME -> FILE_LEN -> FILE_DATA... -> FILE_STOP
    const sendStep = (step) => {
      switch (step) {
        case 'start':
          that.setData({ transferStatus: '初始化传输...' });
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_START, null).then(() => {
            sendStep('name');
          }).catch((err) => {
            console.error('FILE_START failed:', err);
            that.setData({ transferStatus: '传输失败', resultText: '初始化失败: ' + (err.errMsg || err.message || JSON.stringify(err)) });
          });
          break;

        case 'name':
          that.setData({ transferStatus: '发送文件名...' });
          const nameBytes = new Uint8Array(fileName.length);
          for (let i = 0; i < fileName.length; i++) {
            nameBytes[i] = fileName.charCodeAt(i);
          }
          const namePacket = new Uint8Array(4 + nameBytes.length);
          namePacket[0] = bleUtils.BLE_CMD_HEAD;
          namePacket[1] = bleUtils.BLE_FILM_TRANS_CH_FILE_NAME;
          namePacket[2] = nameBytes.length;
          namePacket.set(nameBytes, 3);
          namePacket[namePacket.length - 1] = bleUtils.calculateChecksum(namePacket, namePacket.length - 1);
          app.sendBlePacket(namePacket).then(() => {
            sendStep('length');
          }).catch((err) => {
            that.setData({ transferStatus: '传输失败: ' + (err.errMsg || '文件名发送错误') });
          });
          break;

        case 'length':
          that.setData({ transferStatus: '发送文件长度...' });
          const lenPacket = bleUtils.buildFileLenPacket(totalSize);
          app.sendBlePacket(lenPacket).then(() => {
            sendStep('data');
          }).catch((err) => {
            that.setData({ transferStatus: '传输失败: ' + (err.errMsg || '长度发送错误') });
          });
          break;

        case 'data':
          let chunkIndex = 0;
          const sendNextChunk = () => {
            if (chunkIndex >= totalChunks) {
              sendStep('stop');
              return;
            }
            const start = chunkIndex * chunkSize;
            const end = Math.min(start + chunkSize, totalSize);
            const chunk = fileData.slice(start, end);
            const packet = bleUtils.buildFileDataPacket(chunk);
            const progress = Math.round((chunkIndex + 1) / totalChunks * 100);
            that.setData({
              transferStatus: '传输数据中...',
              transferProgress: progress
            });
            app.sendBlePacket(packet).then(() => {
              chunkIndex++;
              setTimeout(sendNextChunk, bleUtils.BLE_DATA_DELAY);
            }).catch((err) => {
              that.setData({ transferStatus: '传输失败: ' + (err.errMsg || '数据发送错误') });
            });
          };
          sendNextChunk();
          break;

        case 'stop':
          that.setData({ transferStatus: '完成传输...', transferProgress: 100 });
          app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_STOP, null).then(() => {
            that.setData({ transferStatus: '传输完成！', resultText: '文件已发送到设备' });
            setTimeout(() => {
              that.setData({ showTransfer: false });
            }, 2000);
          }).catch((err) => {
            that.setData({ transferStatus: '传输完成（结束命令失败）', resultText: '文件可能已发送' });
          });
          break;
      }
    };

    sendStep('start');
  }
});
