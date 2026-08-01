// 最近使用 & 本地草稿记录工具（本地存储）
var RECENT_KEY = 'ff_recent_list';
var DRAFT_KEY = 'ff_drafts';
var MAX_RECENT = 6;
var MAX_DRAFT = 4;

function nowText() {
  var d = new Date();
  var pad = function (n) { return (n < 10 ? '0' : '') + n; };
  return (d.getMonth() + 1) + '/' + d.getDate() + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
}

function getStorage(key) {
  try {
    var v = wx.getStorageSync(key);
    return (v && v.length) ? v : [];
  } catch (e) {
    return [];
  }
}

function setStorage(key, list) {
  try {
    wx.setStorageSync(key, list);
  } catch (e) {}
}

// ---------- 最近使用 ----------
function getRecent() {
  return getStorage(RECENT_KEY);
}

// 追加一条最近使用（name 为文件名，fileData 为 Uint8Array 的 film 文件，用于首页缩略图）
function addRecent(name, fileData) {
  var list = getRecent();
  for (var i = 0; i < list.length; i++) {
    if (list[i].name === name) {
      list.splice(i, 1);
      break;
    }
  }
  var item = { name: name, time: nowText() };
  if (fileData) {
    try {
      var b64 = wx.arrayBufferToBase64(fileData.buffer);
      if (b64) item.data = b64;
    } catch (e) {}
  }
  list.unshift(item);
  if (list.length > MAX_RECENT) list = list.slice(0, MAX_RECENT);
  setStorage(RECENT_KEY, list);
  return list;
}

// 清空最近使用
function clearRecent() {
  setStorage(RECENT_KEY, []);
}

// ---------- 本地草稿（film 文件 base64）----------
function getDrafts() {
  return getStorage(DRAFT_KEY);
}

// 保存草稿：name 文件名，fileData 为 Uint8Array 的 film 文件
function saveDraft(name, fileData) {
  var list = getDrafts();
  for (var i = 0; i < list.length; i++) {
    if (list[i].name === name) {
      list.splice(i, 1);
      break;
    }
  }
  var b64 = '';
  try {
    b64 = wx.arrayBufferToBase64(fileData.buffer);
  } catch (e) {
    b64 = '';
  }
  if (!b64) return list;
  list.unshift({ name: name, time: nowText(), data: b64 });
  if (list.length > MAX_DRAFT) list = list.slice(0, MAX_DRAFT);
  setStorage(DRAFT_KEY, list);
  return list;
}

// 删除草稿
function removeDraft(name) {
  var list = getDrafts();
  for (var i = 0; i < list.length; i++) {
    if (list[i].name === name) {
      list.splice(i, 1);
      break;
    }
  }
  setStorage(DRAFT_KEY, list);
  return list;
}

// ---------- 批量片单（本地多选图片转换后的 film 文件）----------
var BATCH_KEY = 'ff_batch';
var MAX_BATCH = 30;

// 片单列表，item 结构：{ name, time, data(film base64) }
function getBatch() {
  return getStorage(BATCH_KEY);
}

// 追加一个片单项
function addBatchItem(item) {
  var list = getBatch();
  list.push(item);
  if (list.length > MAX_BATCH) list = list.slice(list.length - MAX_BATCH);
  setStorage(BATCH_KEY, list);
  return list;
}

// 按文件名删除片单项
function removeBatchItem(name) {
  var list = getBatch();
  for (var i = list.length - 1; i >= 0; i--) {
    if (list[i].name === name) list.splice(i, 1);
  }
  setStorage(BATCH_KEY, list);
  return list;
}

module.exports = {
  getRecent: getRecent,
  addRecent: addRecent,
  clearRecent: clearRecent,
  getDrafts: getDrafts,
  saveDraft: saveDraft,
  removeDraft: removeDraft,
  getBatch: getBatch,
  addBatchItem: addBatchItem,
  removeBatchItem: removeBatchItem
};
