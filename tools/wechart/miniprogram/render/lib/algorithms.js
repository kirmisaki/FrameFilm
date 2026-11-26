// 算法注册表：UI 列表、参数显隐、输出格式都由这里推导
const BASE = [
  { id: 'atkinsonEnhanced', name: 'Atkinson 增强', strength: false, color: true, sourceFit: true },
  { id: 'adaptive', name: '自适应', strength: false, color: true, sourceFit: true },
  { id: 'floydSteinberg', name: 'Floyd-Steinberg', strength: true, color: true, sourceFit: true },
  { id: 'atkinson', name: 'Atkinson', strength: true, color: true, sourceFit: true },
  { id: 'stucki', name: 'Stucki', strength: true, color: true, sourceFit: true },
  { id: 'jarvis', name: 'Jarvis-Judice-Ninke', strength: true, color: true, sourceFit: true },
  { id: 'gammaFloydSteinberg', name: 'Gamma 感知 FS', strength: true, color: true, sourceFit: true },
  { id: 'bayer', name: 'Bayer 4×4 有序抖动', strength: true, color: true, sourceFit: true }
];

const PRO = [
  { id: 'szEnhanced', name: 'SZ 增强', strength: false, color: false, sourceFit: false },
  { id: 'atkinsonSzCalib', name: 'Atkinson·SZ 校色', strength: false, color: true, sourceFit: false }
];

const BPP8 = [
  { id: 'colorFast55', name: '55 色 ColorFast', strength: true, color: true, sourceFit: true, bpp8: true },
  { id: 'colorQual', name: '46 色 ColorQual', strength: true, color: true, sourceFit: true, bpp8: true },
  { id: 'colorQualMeasured', name: '实测校色 · 原值', strength: true, color: true, sourceFit: true, bpp8: true },
  { id: 'colorQualCalibrated', name: '实测校色 · 校正 2.5%', strength: true, color: true, sourceFit: true, bpp8: true }
];

const FOUR_COLOR = [
  { id: 'floydSteinberg', name: 'Floyd-Steinberg', strength: true, color: true, sourceFit: true },
  { id: 'adaptive', name: '自适应', strength: false, color: true, sourceFit: true },
  { id: 'atkinson', name: 'Atkinson', strength: true, color: true, sourceFit: true },
  { id: 'stucki', name: 'Stucki', strength: true, color: true, sourceFit: true },
  { id: 'jarvis', name: 'Jarvis-Judice-Ninke', strength: true, color: true, sourceFit: true }
];

function listFor(ctx) {
  const model = (ctx && ctx.model) || 'FRAMEFILM';
  const panelId = ctx && ctx.panelId;
  const is4 = !!(ctx && ctx.is4Color);

  if (is4) return FOUR_COLOR.slice();

  const list = BASE.slice();
  const isPro = model === 'FRAMEFILMPRO' || model === 'FRAMEFILMARK';
  if (isPro) {
    PRO.forEach(function (item) {
      list.push(item);
    });
    // 8bpp 只有通行证版 3.70" 固件实现了播放
    if (model === 'FRAMEFILMARK' && panelId === 0x02) {
      BPP8.forEach(function (item) {
        list.push(item);
      });
    }
  }
  return list;
}

function find(id) {
  const all = BASE.concat(PRO, BPP8, FOUR_COLOR);
  for (let i = 0; i < all.length; i++) {
    if (all[i].id === id) return all[i];
  }
  return null;
}

function isBpp8(id) {
  return BPP8.some(function (item) {
    return item.id === id;
  });
}

module.exports = { listFor, find, isBpp8 };
