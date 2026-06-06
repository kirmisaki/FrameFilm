const filmUtils = require('../../utils/film-utils');
const bleUtils = require('../../utils/ble-utils');
const app = getApp();

Page({
  data: {
    ditherEnabled: false,
    ditherTypeIndex: 0,
    ditherTypes: ['adaptive', 'floydSteinberg', 'atkinson', 'stucki', 'jarvis'],
    ditherTypeNames: ['自适应（推荐）', 'Floyd-Steinberg', 'Atkinson', 'Stucki', 'Jarvis-Judice-Ninke'],
    ditherStrength: 1.0,
    ditherStrengthDisplay: '1.0',
    contrast: 1.2,
    contrastInt: 120,
    fileName: 'output.film',
    rotation: 0,
    hasImage: false,
    showStrength: false,
    transferStatus: '',
    transferProgress: 0,
    showTransfer: false,
    resultText: '',
    isConnected: false
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
    this.setData({ isConnected: app.globalData.isConnected });
  },

  onReady() {
    const query = wx.createSelectorQuery();
    query.select('#film-canvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0]) return;
      this.canvas = res[0].node;
      this.ctx = this.canvas.getContext('2d');
      this.canvas.width = 400;
      this.canvas.height = 600;

      // 临时画布 600×400（用于图像处理）
      this.tempCanvas = wx.createOffscreenCanvas({ type: '2d', width: 600, height: 400 });
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
        const img = this.canvas.createImage();
        img.onload = () => {
          this.imageNode = img;
          this.imageInfo = { width: img.width, height: img.height, path: tempFilePath };
          this.scale = 1;
          this.offsetX = 0;
          this.offsetY = 0;
          // 竖图自动旋转90°，与原版ForFilm一致
          var autoRotation = (img.height > img.width) ? 90 : 0;
          this.setData({
            hasImage: true,
            rotation: autoRotation,
            ditherEnabled: false,
            ditherTypeIndex: 0,
            ditherStrength: 1.0,
            ditherStrengthDisplay: '1.0',
            contrast: 1.2,
            contrastInt: 120,
            showStrength: false,
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

  updateImage() {
    if (!this.canvas || !this.imageNode || !this.tempCanvas) return;
    const tctx = this.tempCtx;
    const OW = 600, OH = 400;
    const img = this.imageNode;
    const rotation = this.data.rotation;

    // 1. 在临时画布上绘制（600×400 横屏，无旋转）
    tctx.clearRect(0, 0, OW, OH);
    tctx.fillStyle = '#ffffff';
    tctx.fillRect(0, 0, OW, OH);

    const radians = (rotation * Math.PI) / 180;
    const imgW = img.width;
    const imgH = img.height;

    // 缩放：旋转后图片的视觉尺寸变了
    var visW, visH;
    if (rotation === 90 || rotation === 270) {
      visW = imgH; visH = imgW;
    } else if (rotation === 180) {
      visW = imgW; visH = imgH;
    } else {
      visW = imgW; visH = imgH;
    }
    var fitScale = Math.min(OW / visW, OH / visH) * this.scale;
    var finalW = imgW * fitScale;
    var finalH = imgH * fitScale;
    var cx = OW / 2 + this.offsetX;
    var cy = OH / 2 + this.offsetY;

    tctx.save();
    tctx.translate(cx, cy);
    tctx.rotate(radians);
    tctx.drawImage(img, -finalW / 2, -finalH / 2, finalW, finalH);
    tctx.restore();

    // 2. 图像处理
    let imageData = tctx.getImageData(0, 0, OW, OH);

    if (this.data.contrast !== 1.0) {
      imageData = filmUtils.adjustContrast(imageData, this.data.contrast);
    }

    if (this.data.ditherEnabled) {
      const type = this.data.ditherTypes[this.data.ditherTypeIndex];
      const strength = this.data.ditherStrength;
      if (type === 'adaptive') {
        imageData = filmUtils.floydSteinbergDither(imageData, strength);
      } else {
        imageData = filmUtils.applyDitherByType(imageData, type, strength);
      }
      // Film 格式回显
      const processedData = filmUtils.processImageData(imageData);
      const decoded = filmUtils.decodeProcessedData(processedData, OW, OH);
      imageData.data.set(decoded.data);
    }

    // 3. 把处理结果放回临时画布
    tctx.putImageData(imageData, 0, 0);

    // 4. 旋转绘制到显示画布（400×600 竖屏）
    const dctx = this.ctx;
    dctx.clearRect(0, 0, 400, 600);
    dctx.save();
    dctx.translate(0, 600);
    dctx.rotate(-Math.PI / 2);
    dctx.drawImage(this.tempCanvas, 0, 0, OW, OH, 0, 0, OW, OH);
    dctx.restore();
  },

  toggleDither() {
    this.setData({ ditherEnabled: !this.data.ditherEnabled });
    this.updateImage();
  },

  onDitherTypeChange(e) {
    const index = parseInt(e.detail.value);
    const showStrength = index !== 0;
    this.setData({ ditherTypeIndex: index, showStrength: showStrength });
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

  autoConfigure() {
    if (!this.canvas || !this.imageNode) return;
    const ctx = this.ctx;
    const canvasW = 600;
    const canvasH = 400;

    // 绘制原图用于分析
    ctx.clearRect(0, 0, canvasW, canvasH);
    const img = this.imageNode;
    const rotation = this.data.rotation;
    const radians = (rotation * Math.PI) / 180;
    const isRotated = rotation % 180 !== 0;
    let drawW = isRotated ? img.height : img.width;
    let drawH = isRotated ? img.width : img.height;
    const ratioW = canvasW / drawW;
    const ratioH = canvasH / drawH;
    const fitScale = Math.min(ratioW, ratioH) * this.scale;
    const finalW = drawW * fitScale;
    const finalH = drawH * fitScale;
    const cx = canvasW / 2 + this.offsetX;
    const cy = canvasH / 2 + this.offsetY;
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(radians);
    ctx.drawImage(img, -finalW / 2, -finalH / 2, finalW, finalH);
    ctx.restore();

    const imageData = ctx.getImageData(0, 0, canvasW, canvasH);
    const data = imageData.data;

    // 计算亮度分布
    let totalLum = 0;
    let minLum = 255;
    let maxLum = 0;
    const lumValues = [];
    for (let i = 0; i < data.length; i += 4) {
      const lum = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
      lumValues.push(lum);
      totalLum += lum;
      if (lum < minLum) minLum = lum;
      if (lum > maxLum) maxLum = lum;
    }
    const avgLum = totalLum / lumValues.length;
    const lumRange = maxLum - minLum;

    // 计算标准差
    let variance = 0;
    for (let i = 0; i < lumValues.length; i++) {
      variance += (lumValues[i] - avgLum) * (lumValues[i] - avgLum);
    }
    variance /= lumValues.length;
    const stdDev = Math.sqrt(variance);

    // 根据图像特征自动配置
    let contrast = 1.2;
    let ditherStrength = 1.0;
    let ditherTypeIndex = 0;

    // 低对比度图像增加对比度
    if (lumRange < 100) {
      contrast = 1.8;
    } else if (lumRange < 150) {
      contrast = 1.5;
    } else if (stdDev < 40) {
      contrast = 1.4;
    }

    // 根据细节程度选择抖动算法
    if (stdDev > 60) {
      // 高细节图像使用 Atkinson（减少噪点）
      ditherTypeIndex = 2;
      ditherStrength = 0.8;
    } else if (stdDev > 40) {
      // 中等细节使用 Floyd-Steinberg
      ditherTypeIndex = 1;
      ditherStrength = 1.0;
    } else {
      // 低细节使用自适应
      ditherTypeIndex = 0;
      ditherStrength = 1.2;
    }

    this.setData({
      contrast: contrast,
      contrastInt: Math.round(contrast * 100),
      ditherEnabled: true,
      ditherTypeIndex: ditherTypeIndex,
      ditherStrength: ditherStrength,
      ditherStrengthDisplay: ditherStrength.toFixed(1),
      showStrength: ditherTypeIndex !== 0
    });

    wx.showToast({ title: '已自动配置参数', icon: 'success' });
    this.updateImage();
  },

  rotateCanvas() {
    const newRotation = (this.data.rotation + 90) % 360;
    this.setData({ rotation: newRotation });
    this.offsetX = 0;
    this.offsetY = 0;
    this.scale = 1;
    this.updateImage();
  },

  resetImage() {
    this.setData({
      hasImage: false,
      rotation: 0,
      ditherEnabled: false,
      ditherTypeIndex: 0,
      ditherStrength: 1.0,
      contrast: 1.2,
      showStrength: false,
      resultText: ''
    });
    this.imageNode = null;
    this.imageInfo = null;
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    if (this.canvas) {
      this.ctx.clearRect(0, 0, 400, 600);
    }
  },

  onTouchStart(e) {
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
    if (e.touches.length === 1 && !this._isPinching) {
      const dx = e.touches[0].clientX - this._touchStartX;
      const dy = e.touches[0].clientY - this._touchStartY;
      this.offsetX = this._startOffsetX + dx * 0.5;
      this.offsetY = this._startOffsetY + dy * 0.5;
      this.updateImage();
    } else if (e.touches.length === 2 && this._isPinching) {
      const dx = e.touches[0].clientX - e.touches[1].clientX;
      const dy = e.touches[0].clientY - e.touches[1].clientY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      const scaleChange = dist / this._startDist;
      this.scale = Math.max(0.1, Math.min(5, this._startScale * scaleChange));
      this.updateImage();
    }
  },

  onTouchEnd(e) {
    if (e.touches.length < 2) {
      this._isPinching = false;
    }
  },

  onFileNameInput(e) {
    this.setData({ fileName: e.detail.value });
  },

  downloadFilm() {
    if (!this.canvas || !this.imageNode) return;

    wx.showLoading({ title: '处理中...' });

    // 直接从临时画布获取 600×400 数据
    const imageData = this.tempCtx.getImageData(0, 0, 600, 400);
    const processedData = filmUtils.processImageData(imageData);
    const header = filmUtils.generateFilmHeader();

    // 合并 header + pixelData
    const fileData = new Uint8Array(filmUtils.FILM_FILE_TOTAL_SIZE);
    fileData.set(header, 0);
    fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);

    const fileName = this.data.fileName || 'output.film';
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

    // 获取当前画布图像数据
    const imageData = this.ctx.getImageData(0, 0, 600, 400);
    const processedData = filmUtils.processImageData(imageData);
    const header = filmUtils.generateFilmHeader();

    // 合并 header + pixelData
    const fileData = new Uint8Array(filmUtils.FILM_FILE_TOTAL_SIZE);
    fileData.set(header, 0);
    fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);

    wx.hideLoading();

    const fileName = this.data.fileName || 'output.film';
    this.sendFileViaBle(fileData, fileName);
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
