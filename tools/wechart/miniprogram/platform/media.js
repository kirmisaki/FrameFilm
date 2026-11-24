// 小程序侧媒体与文件辅助
function chooseImages(opts) {
  const options = opts || {};
  return new Promise(function (resolve, reject) {
    wx.chooseMedia({
      count: options.count || 1,
      mediaType: ['image'],
      sourceType: options.sourceType || ['album'],
      sizeType: ['original', 'compressed'],
      success: function (res) {
        resolve((res.tempFiles || []).map(function (f) {
          return {
            path: f.tempFilePath,
            size: f.size,
            name: f.name || ''
          };
        }));
      },
      fail: reject
    });
  });
}

function chooseFirmware() {
  return new Promise(function (resolve, reject) {
    wx.chooseMessageFile({
      count: 1,
      type: 'file',
      extension: ['bin'],
      success: function (res) {
        const file = (res.tempFiles || [])[0];
        if (!file) {
          reject(new Error('未选择文件'));
          return;
        }
        resolve(file);
      },
      fail: reject
    });
  });
}

function readFile(path) {
  return new Promise(function (resolve, reject) {
    wx.getFileSystemManager().readFile({
      filePath: path,
      success: function (res) {
        resolve(res.data);
      },
      fail: reject
    });
  });
}

function writeFilmFile(fileData, fileName) {
  return new Promise(function (resolve, reject) {
    const path = wx.env.USER_DATA_PATH + '/' + fileName;
    const fs = wx.getFileSystemManager();
    const buffer = fileData.buffer.slice(fileData.byteOffset, fileData.byteOffset + fileData.byteLength);
    fs.writeFile({
      filePath: path,
      data: buffer,
      success: function () {
        resolve(path);
      },
      fail: reject
    });
  });
}

function shareFilm(fileData, fileName) {
  return writeFilmFile(fileData, fileName).then(function (path) {
    return new Promise(function (resolve, reject) {
      if (typeof wx.shareFileMessage === 'function') {
        wx.shareFileMessage({
          filePath: path,
          fileName: fileName,
          success: resolve,
          fail: reject
        });
      } else {
        reject(new Error('当前微信版本不支持分享文件'));
      }
    });
  });
}

module.exports = { chooseImages, chooseFirmware, readFile, writeFilmFile, shareFilm };
