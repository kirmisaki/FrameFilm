// template-sender.js - 模板上墙 BLE 发送管线（模板共用）
// 从一言页 _sendViaBle 抽取：canvas -> film 数据 -> BLE 分包发送 -> 记录最近上墙
var filmUtils = require('./film-utils');
var bleUtils = require('./ble-utils');
var recentUtils = require('./recent-utils');

var BLE_CHUNK_SIZE = bleUtils.BLE_CHUNK_SIZE;
var BLE_CTRL_DELAY = bleUtils.BLE_CTRL_DELAY;
var BLE_DATA_DELAY = bleUtils.BLE_DATA_DELAY;

function delay(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

// 把 canvas 当前内容转为完整 film 文件数据
// 与照片管线一致：任意彩色画布 -> 自适应抖动 -> 6 色编码，保证渐变/多色设计的上屏质感
function canvasToFilmData(canvas) {
  var imageData = filmUtils.extractLandscapeData(canvas);
  var dithered = filmUtils.applyDitherByType(imageData, 'adaptive', 1.0);
  var processedData = filmUtils.processImageData(dithered);
  var header = filmUtils.generateFilmHeader();
  var totalSize = filmUtils.getFilmFileTotalSize();
  var fileData = new Uint8Array(totalSize);
  fileData.set(header, 0);
  fileData.set(processedData, filmUtils.FILM_HEADER_SIZE);
  return fileData;
}

// 发送到设备。onStatus(status, progress) 用于页面更新弹层；返回 Promise
// progress: 0-99 传输中，100 完成，-1 失败
function sendToDevice(fileData, baseName, onStatus) {
  var app = getApp();
  return new Promise(function (resolve, reject) {
    if (!app.globalData.isConnected) {
      reject(new Error('请先连接蓝牙设备'));
      return;
    }

    var fileName = baseName + '.film';
    var totalSize = fileData.length;
    var lastDisplayedPct = -1;
    var step = 0;

    function updateProgress(extraPercent) {
      var controlWeight = 0.05;
      var dataWeight = 0.95;
      var controlProgress = (step / 4) * controlWeight * 100;
      var dataProgress = totalSize > 0 ? (extraPercent / totalSize) * dataWeight * 100 : 0;
      var pct = Math.min(99, Math.floor(controlProgress + dataProgress));
      if (pct !== lastDisplayedPct) {
        lastDisplayedPct = pct;
        onStatus('', pct);
      }
    }

    onStatus('发送开始指令...', 0);
    app.sendBlePacket(bleUtils.buildFileStartPacket())
      .then(function () { step = 1; updateProgress(0); return delay(BLE_CTRL_DELAY); })
      .then(function () {
        onStatus('发送文件名...', 0);
        return app.sendBlePacket(bleUtils.buildFileNamePacket(fileName));
      })
      .then(function () { step = 2; updateProgress(0); return delay(BLE_CTRL_DELAY); })
      .then(function () {
        onStatus('发送文件长度...', 0);
        return app.sendBlePacket(bleUtils.buildFileLenPacket(totalSize));
      })
      .then(function () { step = 3; updateProgress(0); return delay(BLE_CTRL_DELAY); })
      .then(function () {
        onStatus('传输数据...', 0);
        var offset = 0;
        function sendChunk() {
          if (offset >= totalSize) return Promise.resolve();
          var end = Math.min(offset + BLE_CHUNK_SIZE, totalSize);
          var chunk = fileData.slice(offset, end);
          return app.sendBlePacket(bleUtils.buildFileDataPacket(chunk)).then(function () {
            offset = end;
            updateProgress(offset);
            return delay(BLE_DATA_DELAY);
          }).then(sendChunk);
        }
        return sendChunk();
      })
      .then(function () {
        step = 4;
        onStatus('发送完成指令...', 0);
        return app.sendBlePacket(bleUtils.buildFileStopPacket());
      })
      .then(function () {
        onStatus('传输完成！', 100);
        recentUtils.addRecent(fileName, fileData);
        resolve();
      })
      .catch(function (err) {
        onStatus('传输失败：' + (err.errMsg || err.message || '未知错误'), -1);
        reject(err);
      });
  });
}

module.exports = {
  canvasToFilmData: canvasToFilmData,
  sendToDevice: sendToDevice
};
