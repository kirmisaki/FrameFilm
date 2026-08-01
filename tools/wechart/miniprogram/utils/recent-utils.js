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
// 注意：film 文件 base64 约 156KB/张，若全部塞进 ff_batch 单 key 会触发微信
// 单个 key 1MB 上限，第 7 张起写入被 try/catch 静默吞掉，片单卡在 6 张。
// 因此每张的数据单独存一个 key，ff_batch 只存轻量元数据。
var BATCH_KEY = 'ff_batch';
var BATCH_DATA_PREFIX = 'ff_batch_data_';
var MAX_BATCH = 30;

function batchDataKey(name) {
  return BATCH_DATA_PREFIX + name;
}

// 片单列表，item 结构：{ name, time, data(film base64) }
function getBatch() {
  var list = getStorage(BATCH_KEY);
  var result = [];
  for (var i = 0; i < list.length; i++) {
    var meta = list[i];
    var data = meta.data || '';
    try {
      data = wx.getStorageSync(batchDataKey(meta.name)) || data;
    } catch (e) {}
    if (data) result.push({ name: meta.name, time: meta.time, data: data });
  }
  return result;
}

// 追加一个片单项（数据入独立 key，元数据入列表）
function addBatchItem(item) {
  var list = getStorage(BATCH_KEY);
  try {
    wx.setStorageSync(batchDataKey(item.name), item.data);
  } catch (e) {
    return list;
  }
  list.push({ name: item.name, time: item.time });
  if (list.length > MAX_BATCH) {
    var removed = list.splice(0, list.length - MAX_BATCH);
    for (var i = 0; i < removed.length; i++) {
      try { wx.removeStorageSync(batchDataKey(removed[i].name)); } catch (e2) {}
    }
  }
  setStorage(BATCH_KEY, list);
  return getBatch();
}

// 按文件名删除片单项
function removeBatchItem(name) {
  var list = getStorage(BATCH_KEY);
  for (var i = list.length - 1; i >= 0; i--) {
    if (list[i].name === name) {
      list.splice(i, 1);
      try { wx.removeStorageSync(batchDataKey(name)); } catch (e) {}
      break;
    }
  }
  setStorage(BATCH_KEY, list);
  return getBatch();
}

// 清空片单
function clearBatch() {
  var list = getStorage(BATCH_KEY);
  for (var i = 0; i < list.length; i++) {
    try { wx.removeStorageSync(batchDataKey(list[i].name)); } catch (e) {}
  }
  setStorage(BATCH_KEY, []);
  return [];
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
  removeBatchItem: removeBatchItem,
  clearBatch: clearBatch
};
