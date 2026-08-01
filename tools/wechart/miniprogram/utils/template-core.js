// template-core.js - 模板通用工具与注册表元数据
// 所有模板渲染器统一依赖本模块：6 色安全色板、文本换行、圆角路径、模板中心元数据
var filmUtils = require('./film-utils');

// 6 色安全色板（与 EPD 六色编码对齐，保证上屏还原度）
var COLORS = {
  black: '#000000',
  white: '#ffffff',
  red: '#ff0000',
  blue: '#0000ff',
  green: '#29cc14',
  yellow: '#ffd400'
};

// 模板中心元数据（页面列表用）
var TEMPLATE_LIST = [
  {
    id: 'calendar',
    name: '日历',
    emoji: '📅',
    desc: '月历 · 农历 · 节气 · 今日高亮',
    gradient: 'linear-gradient(135deg, #FF6B6B, #FF9F6B)',
    accent: '#FF6B6B'
  },
  {
    id: 'sign',
    name: '每日一签',
    emoji: '🎋',
    desc: '今日签语 · 宜忌 · 幸运色',
    gradient: 'linear-gradient(135deg, #845EC2, #6B45A6)',
    accent: '#845EC2'
  },
  {
    id: 'weather',
    name: '天气',
    emoji: '⛅',
    desc: '实时温度 · 天气图标 · 未来 3 天',
    gradient: 'linear-gradient(135deg, #4D96FF, #2F6FD8)',
    accent: '#4D96FF'
  },
  {
    id: 'countdown',
    name: '纪念日倒数',
    emoji: '💝',
    desc: '大日子倒数 · 爱心天数',
    gradient: 'linear-gradient(135deg, #FF8FC7, #FF6B6B)',
    accent: '#FF6B6B'
  }
];

function pad(n) {
  return ('0' + n).slice(-2);
}

// 文本换行（转发 film-utils 实现）
function wrapText(ctx, text, fontSize, maxWidth) {
  return filmUtils.wrapText(ctx, text, fontSize, maxWidth);
}

// 圆角矩形路径（只建路径不填充，便于 fill/stroke 复用）
function roundRect(ctx, x, y, w, h, r) {
  r = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

module.exports = {
  COLORS: COLORS,
  TEMPLATE_LIST: TEMPLATE_LIST,
  pad: pad,
  wrapText: wrapText,
  roundRect: roundRect
};
