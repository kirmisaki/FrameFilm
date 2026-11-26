// 统一渲染管线：画布/图片 -> v1 4bpp film 文件
const filmUtils = require('./film-utils');
const sourcefit = require('./sourcefit');

function buildFromImageData(imageData, opts) {
  const options = opts || {};
  const type = options.ditherType || 'floydSteinberg';
  const strength = options.strength == null ? 1 : options.strength;
  const ditherEnabled = options.ditherEnabled !== false;
  const isSZ = type === 'szEnhanced';

  if (!isSZ) {
    if (options.contrast && options.contrast !== 1) {
      filmUtils.adjustContrast(imageData, options.contrast);
    }
    if (options.saturation && options.saturation !== 1) {
      filmUtils.adjustSaturation(imageData, options.saturation);
    }
    if (type !== 'atkinsonSzCalib' && (options.rangeFit || options.warmChroma)) {
      sourcefit.apply(imageData, options.rangeFit, options.warmChroma, sourcefit.profiles.native6);
    }
  }

  const processed = ditherEnabled
    ? filmUtils.applyDitherByType(imageData, type, strength)
    : imageData;

  const pixelData = filmUtils.processImageData(processed);
  const header = filmUtils.generateFilmHeader();
  const fileData = new Uint8Array(header.length + pixelData.length);
  fileData.set(header, 0);
  fileData.set(pixelData, header.length);

  const cfg = filmUtils.getDeviceConfig();
  const preview = filmUtils.decodeProcessedData(pixelData, cfg.screenWidth, cfg.screenHeight);
  return { fileData: fileData, preview: preview, pixelData: pixelData };
}

function buildFromCanvas(canvas, opts) {
  const imageData = filmUtils.extractLandscapeData(canvas);
  return buildFromImageData(imageData, opts);
}

// 把横屏预览数据按面板方向画回竖屏画布
function paintPreview(portraitCtx, preview, canvasWidth, canvasHeight) {
  const cfg = filmUtils.getDeviceConfig();
  const sw = preview.width;
  const sh = preview.height;
  const temp = wx.createOffscreenCanvas({ type: '2d', width: sw, height: sh });
  const tctx = temp.getContext('2d');
  const imgData = tctx.createImageData(sw, sh);
  imgData.data.set(preview.data);
  tctx.putImageData(imgData, 0, 0);

  portraitCtx.clearRect(0, 0, canvasWidth, canvasHeight);
  if (cfg.isPortraitPanel) {
    portraitCtx.save();
    portraitCtx.translate(canvasWidth, 0);
    portraitCtx.rotate(Math.PI / 2);
    portraitCtx.drawImage(temp, 0, 0, sw, sh, 0, 0, sw, sh);
    portraitCtx.restore();
  } else {
    portraitCtx.drawImage(temp, 0, 0, sw, sh, 0, 0, canvasWidth, canvasHeight);
  }
}

function loadImageToCanvas(path, canvas, fillMode) {
  return new Promise(function (resolve, reject) {
    const ctx = canvas.getContext('2d');
    const img = canvas.createImage();
    img.onload = function () {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      const scale = fillMode === 'contain'
        ? Math.min(canvas.width / img.width, canvas.height / img.height)
        : Math.max(canvas.width / img.width, canvas.height / img.height);
      const dw = img.width * scale;
      const dh = img.height * scale;
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(img, (canvas.width - dw) / 2, (canvas.height - dh) / 2, dw, dh);
      resolve(img);
    };
    img.onerror = reject;
    img.src = path;
  });
}

module.exports = { buildFromImageData, buildFromCanvas, paintPreview, loadImageToCanvas };
