// 抖动/转换算法目录（与 ForFilm Web 对齐 10 种）
// 单一来源：film 页「高级转换」、设置页「创作页默认转换算法」、
// 创作页（相册/拍照/一言/绘梦）共用同一份清单，避免多处复制导致漂移。
var filmUtils = require('./film-utils');

// base 为全机型可用；SZ 增强 / Atkinson·SZ 校色仅 Pro（fit128 校色链路，Web 端同名选项按机型禁用）
var DITHER_BASE = [
  { type: 'atkinsonEnhanced', name: 'Atkinson 增强' },
  { type: 'adaptive', name: '自适应' },
  { type: 'floydSteinberg', name: 'Floyd-Steinberg' },
  { type: 'atkinson', name: 'Atkinson' },
  { type: 'stucki', name: 'Stucki' },
  { type: 'jarvis', name: 'Jarvis-Judice-Ninke' },
  { type: 'gammaFloydSteinberg', name: 'Gamma 感知 FS（线性）' },
  { type: 'bayer', name: 'Bayer 4×4 有序抖动' }
];
var DITHER_PRO_ONLY = [
  { type: 'szEnhanced', name: 'SZ 增强（Pro）' },
  { type: 'atkinsonSzCalib', name: 'Atkinson·SZ 校色（Pro）' }
];
// 需要抖动强度滑块的算法（自适应/AE/SZ/校色 内部强度固定，与 Web 隐藏规则一致）
var STRENGTH_TYPES = ['floydSteinberg', 'atkinson', 'stucki', 'jarvis', 'gammaFloydSteinberg', 'bayer'];

// 创作页默认转换算法：设置页可选，默认 Atkinson 增强（本地持久化）
var DEFAULT_CREATE_DITHER_TYPE = 'atkinsonEnhanced';
var CREATE_DITHER_STORAGE_KEY = 'ff_create_dither_type';

// 按机型返回可选算法列表（Pro 追加两项 Pro-only）
function ditherListFor(deviceType) {
  var list = DITHER_BASE.slice();
  if (deviceType === 'FRAMEFILMPRO') {
    list = list.concat(DITHER_PRO_ONLY);
  }
  return list;
}

// 按机型返回可选算法 type/name 数组（picker range/value 用）
function ditherOptionsFor(deviceType) {
  var list = ditherListFor(deviceType || filmUtils.getDeviceType());
  var types = [];
  var names = [];
  for (var i = 0; i < list.length; i++) {
    types.push(list[i].type);
    names.push(list[i].name);
  }
  return { types: types, names: names };
}

function ditherTypeLabel(type) {
  var i;
  for (i = 0; i < DITHER_BASE.length; i++) {
    if (DITHER_BASE[i].type === type) return DITHER_BASE[i].name;
  }
  for (i = 0; i < DITHER_PRO_ONLY.length; i++) {
    if (DITHER_PRO_ONLY[i].type === type) return DITHER_PRO_ONLY[i].name;
  }
  return type;
}

// 读取设置页保存的创作页默认算法；当前机型不可用（如非 Pro 上存了 SZ）时回退默认
function getCreateDitherType(deviceType) {
  var saved = DEFAULT_CREATE_DITHER_TYPE;
  try {
    saved = wx.getStorageSync(CREATE_DITHER_STORAGE_KEY) || DEFAULT_CREATE_DITHER_TYPE;
  } catch (e) {}
  var list = ditherListFor(deviceType || filmUtils.getDeviceType());
  for (var i = 0; i < list.length; i++) {
    if (list[i].type === saved) return saved;
  }
  return DEFAULT_CREATE_DITHER_TYPE;
}

function setCreateDitherType(type) {
  try {
    wx.setStorageSync(CREATE_DITHER_STORAGE_KEY, type);
  } catch (e) {}
}

// 创作页统一转换参数：算法=设置默认，强度/对比度/饱和度一律 1.0（无对应参数的算法自动忽略）
function getCreateDitherParams(deviceType) {
  return {
    type: getCreateDitherType(deviceType),
    strength: 1.0,
    contrast: 1.0,
    saturation: 1.0
  };
}

module.exports = {
  DITHER_BASE: DITHER_BASE,
  DITHER_PRO_ONLY: DITHER_PRO_ONLY,
  STRENGTH_TYPES: STRENGTH_TYPES,
  DEFAULT_CREATE_DITHER_TYPE: DEFAULT_CREATE_DITHER_TYPE,
  CREATE_DITHER_STORAGE_KEY: CREATE_DITHER_STORAGE_KEY,
  ditherListFor: ditherListFor,
  ditherOptionsFor: ditherOptionsFor,
  ditherTypeLabel: ditherTypeLabel,
  getCreateDitherType: getCreateDitherType,
  setCreateDitherType: setCreateDitherType,
  getCreateDitherParams: getCreateDitherParams
};
