// tpl-sign.js - 每日一签模板渲染器
// 「书法大字签」：浅色纸张底 + 中央超大等级字（吉/平/藏）+ 等级印章 + 幽默签文 + 幸运信息
// 纯排版设计（放弃插画），四角角标 + 印章 + 大字三层视觉锚点
var core = require('./template-core');

// ============ 签语词库（幽默生活化，非名言） ============
// level: 0=大吉 1=吉 2=平 3=低调
var SIGN_BOOK = [
  {
    grade: '大吉', yi: '大笑', ji: '皱眉',
    quotes: [
      '今天的心情开了倍速，好运直接冲到你面前',
      '想见的人今天能见到，想吃的今天队伍也短',
      '钱包今日发福，适合犒劳自己一顿',
      '好运开关已打开，出门请微笑'
    ]
  },
  {
    grade: '吉', yi: '出发', ji: '犹豫',
    quotes: [
      '饭要好好吃，觉要好好睡，其余都是小事',
      '该出手时就出手，别等机会溜走再拍大腿',
      '今天适合做一件一直没敢做的事',
      '小步快跑，今天先完成一个小目标'
    ]
  },
  {
    grade: '平', yi: '放空', ji: '较真',
    quotes: [
      '平平淡淡才是真，今天适合躺着充电',
      '小计划慢慢推，别急着一步登天',
      '今天宜发呆，不宜纠结别人一句话',
      '稳住别浪，今天的重点是别累到自己'
    ]
  },
  {
    grade: '低调', yi: '低调', ji: '立flag',
    quotes: [
      '今天不宜立 flag，说啥啥不准',
      '大事别今天办，先喝口水冷静一下',
      '出门带把伞，虽然不一定下雨，但带着安心',
      '今天适合苟住，出风头的事改天再干'
    ]
  }
];

// 幸运色 -> 6 色安全色
var LUCKY_COLOR_HEX = {
  '红': '#ff0000',
  '蓝': '#0000ff',
  '绿': '#29cc14',
  '黄': '#ffd400'
};
var LUCKY_COLOR_NAMES = ['红', '蓝', '绿', '黄'];

// ============ 配色方案（浅色纸张风；上屏经抖动呈现） ============
// 文字颜色只用深色系（映射为 EPD 黑色，保证清晰），颜色由 accent 饱和色承担
var SCHEMES = [
  { name: '朱砂', bg1: '#fff7f3', bg2: '#ffe7dc', accent: '#e8553d', text: '#4a2f2a', sub: '#3a2f2a' },
  { name: '天青', bg1: '#f1f8ff', bg2: '#dcebf8', accent: '#2f6fd8', text: '#26384e', sub: '#2a3a52' },
  { name: '竹绿', bg1: '#f2faf4', bg2: '#ddf0e2', accent: '#1fa86c', text: '#2a4636', sub: '#2e4438' },
  { name: '赤金', bg1: '#fdf9f0', bg2: '#f6ecd8', accent: '#c44a1f', text: '#4a3d28', sub: '#4a3f2c' }
];

// 抽一签
function getRandomSign() {
  var level = Math.floor(Math.random() * SIGN_BOOK.length);
  var group = SIGN_BOOK[level];
  var quote = group.quotes[Math.floor(Math.random() * group.quotes.length)];
  var luckyName = LUCKY_COLOR_NAMES[Math.floor(Math.random() * LUCKY_COLOR_NAMES.length)];
  return {
    grade: group.grade,
    yi: group.yi,
    ji: group.ji,
    text: quote,
    signNum: Math.floor(Math.random() * 99) + 1,
    luckyColor: luckyName,
    luckyHex: LUCKY_COLOR_HEX[luckyName],
    luckyNum: Math.floor(Math.random() * 10)
  };
}

// 等级 -> 视觉大字（书法单字）
function gradeChar(grade) {
  if (grade === '大吉' || grade === '吉') return '吉';
  if (grade === '平') return '平';
  return '藏'; // 低调
}

// 四角角标装饰
function drawCorner(ctx, x, y, dirX, dirY, len) {
  ctx.beginPath();
  ctx.moveTo(x + dirX * len, y);
  ctx.lineTo(x, y);
  ctx.lineTo(x, y + dirY * len);
  ctx.stroke();
}

// ============ 渲染 ============
function render(ctx, W, H, data, scheme) {
  var s = scheme || SCHEMES[0];
  var margin = Math.round(W * 0.08);

  // ---- 深色背景渐变 ----
  var bg = ctx.createLinearGradient(0, 0, W, H);
  bg.addColorStop(0, s.bg1);
  bg.addColorStop(1, s.bg2);
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, W, H);
  ctx.textBaseline = 'middle';

  // ---- 四角角标 ----
  ctx.strokeStyle = s.sub;
  ctx.lineWidth = 2;
  var cl = Math.round(W * 0.05);
  drawCorner(ctx, margin, Math.round(H * 0.045), 1, 1, cl);
  drawCorner(ctx, W - margin, Math.round(H * 0.045), -1, 1, cl);
  drawCorner(ctx, margin, Math.round(H * 0.955), 1, -1, cl);
  drawCorner(ctx, W - margin, Math.round(H * 0.955), -1, -1, cl);

  // ---- 顶部：品牌（左）+ 日期（右） ----
  ctx.fillStyle = s.sub;
  ctx.font = Math.round(W * 0.028) + 'px sans-serif';
  ctx.textAlign = 'left';
  ctx.fillText('FRAME FILM · 今日一签', margin, Math.round(H * 0.055));
  var now = new Date();
  var dateStr = now.getFullYear() + '.' + core.pad(now.getMonth() + 1) + '.' + core.pad(now.getDate());
  ctx.textAlign = 'right';
  ctx.fillText(dateStr, W - margin, Math.round(H * 0.055));

  // ---- 等级印章（accent 圆角方块，内白字等级全称） ----
  var sealW = Math.round(W * 0.2);
  var sealH = Math.round(W * 0.085);
  var sealX = W / 2 - sealW / 2;
  var sealY = Math.round(H * 0.17);
  core.roundRect(ctx, sealX, sealY, sealW, sealH, sealH / 2);
  ctx.fillStyle = s.accent;
  ctx.fill();
  ctx.fillStyle = '#ffffff';
  ctx.textAlign = 'center';
  ctx.font = 'bold ' + Math.round(W * 0.042) + 'px serif';
  ctx.fillText('第 ' + data.signNum + ' 签 · ' + data.grade, W / 2, sealY + sealH / 2);

  // ---- 中央书法大字 ----
  var bigChar = gradeChar(data.grade);
  var bigFont = Math.round(W * 0.42);
  ctx.fillStyle = s.text;
  ctx.font = 'bold ' + bigFont + 'px serif';
  ctx.fillText(bigChar, W / 2, Math.round(H * 0.4));

  // ---- 签文（居中 1-2 行） ----
  var fontSize = Math.round(W * 0.052);
  ctx.font = 'bold ' + fontSize + 'px serif';
  var lines = core.wrapText(ctx, data.text, fontSize, W - margin * 2);
  var lineHeight = Math.round(fontSize * 1.6);
  var textStartY = Math.round(H * 0.6);
  ctx.fillStyle = s.text;
  ctx.textAlign = 'center';
  for (var i = 0; i < lines.length; i++) {
    ctx.fillText(lines[i], W / 2, textStartY + i * lineHeight);
  }

  // ---- 底部幸运信息 ----
  var luckyY = Math.round(H * 0.8);
  ctx.fillStyle = s.accent;
  ctx.font = 'bold ' + Math.round(W * 0.034) + 'px sans-serif';
  ctx.fillText('宜 ' + data.yi, W / 2 - Math.round(W * 0.32), luckyY);
  ctx.fillStyle = s.text;
  ctx.font = Math.round(W * 0.034) + 'px sans-serif';
  ctx.fillText('忌 ' + data.ji, W / 2 - Math.round(W * 0.11), luckyY);
  ctx.beginPath();
  ctx.arc(W / 2 + Math.round(W * 0.09), luckyY, Math.round(W * 0.02), 0, Math.PI * 2);
  ctx.fillStyle = data.luckyHex;
  ctx.fill();
  ctx.strokeStyle = s.text;
  ctx.lineWidth = 1.5;
  ctx.stroke();
  ctx.fillStyle = s.text;
  ctx.fillText('幸运数字 ' + data.luckyNum, W / 2 + Math.round(W * 0.2), luckyY);

  // ---- 底部装饰线 ----
  ctx.strokeStyle = s.accent;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(W / 2 - Math.round(W * 0.06), Math.round(H * 0.92));
  ctx.lineTo(W / 2 + Math.round(W * 0.06), Math.round(H * 0.92));
  ctx.stroke();
}

module.exports = {
  SCHEMES: SCHEMES,
  getRandomSign: getRandomSign,
  render: render
};
