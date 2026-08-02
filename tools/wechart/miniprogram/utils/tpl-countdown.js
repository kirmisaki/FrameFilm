// tpl-countdown.js - 纪念日倒数模板渲染器
// 「撕页倒数卡」：浅色纸张底 + 白色撕页日历卡（装订孔/日期条/大数字/锯齿撕边）+ 纪念日名称
// 渲染使用任意设计色，上屏时由模板发送管线做自适应抖动转为 6 色网点质感
var core = require('./template-core');

// ============ 配色方案（浅色纸张风） ============
// 文字/描边只用深色系（映射为 EPD 黑色，保证清晰），颜色由 accent 饱和色承担
var SCHEMES = [
  { name: '蜜桃', accent: '#ff5f7f', text: '#3a2a2e', sub: '#3a2a2e', num: '#3a2a2e' },
  { name: '湖蓝', accent: '#3d7bff', text: '#23364e', sub: '#23364e', num: '#23364e' },
  { name: '抹茶', accent: '#1fb573', text: '#2a4636', sub: '#2a4636', num: '#2a4636' },
  { name: '赤金', accent: '#c44a1f', text: '#463a2c', sub: '#463a2c', num: '#463a2c' }
];

// 构造渲染数据：name=纪念日名称, dateStr='YYYY-MM-DD'
// 未来日期 -> countdown 模式；过去日期 -> passed 模式
function buildData(name, dateStr) {
  var parts = (dateStr || '').split('-');
  var target = new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
  var now = new Date();
  var today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  var diff = Math.round((target - today) / 86400000);
  var mode = diff >= 0 ? 'countdown' : 'passed';
  var nowStr = now.getFullYear() + '.' + core.pad(now.getMonth() + 1) + '.' + core.pad(now.getDate());
  return {
    name: name,
    dateStr: dateStr,
    days: Math.abs(diff),
    mode: mode,
    todayStr: nowStr
  };
}

// 撕页卡片路径：顶边圆角 + 两侧直线 + 底部锯齿撕边
function drawTornCard(ctx, x0, y0, w, h, r, teeth) {
  var tearH = h * 0.1;
  ctx.beginPath();
  ctx.moveTo(x0 + r, y0);
  ctx.lineTo(x0 + w - r, y0);
  ctx.quadraticCurveTo(x0 + w, y0, x0 + w, y0 + r);
  ctx.lineTo(x0 + w, y0 + h - tearH);
  for (var i = 0; i < teeth; i++) {
    var bx = x0 + w - (i + 0.5) * (w / teeth);
    ctx.lineTo(bx, y0 + h);
    ctx.lineTo(x0 + w - (i + 1) * (w / teeth), y0 + h - tearH);
  }
  ctx.lineTo(x0, y0 + h - tearH);
  ctx.lineTo(x0, y0 + r);
  ctx.quadraticCurveTo(x0, y0, x0 + r, y0);
  ctx.closePath();
}

// ============ 渲染 ============
function render(ctx, W, H, data, scheme) {
  var s = scheme || SCHEMES[0];
  var margin = Math.round(W * 0.07);

  // ---- 纯白背景 ----
  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, W, H);
  ctx.textBaseline = 'middle';

  // ---- 顶部：品牌（左）+ 今日（右） ----
  ctx.fillStyle = s.sub;
  ctx.font = Math.round(W * 0.033) + 'px sans-serif';
  ctx.textAlign = 'left';
  ctx.fillText('FRAME FILM · 幸福倒数', margin, Math.round(H * 0.05));
  ctx.textAlign = 'right';
  ctx.fillText(data.todayStr, W - margin, Math.round(H * 0.05));

  // ---- 白色撕页卡片 ----
  var cardW = Math.round(W * 0.74);
  var cardH = Math.round(H * 0.38);
  var x0 = Math.round((W - cardW) / 2);
  var y0 = Math.round(H * 0.16);
  var teeth = Math.max(6, Math.round(cardW / (W * 0.016)));

  // 卡片白底 + 撕边 + 浅色描边（区分浅色底）
  drawTornCard(ctx, x0, y0, cardW, cardH, Math.round(W * 0.02), teeth);
  ctx.fillStyle = '#ffffff';
  ctx.fill();
  ctx.strokeStyle = s.sub;
  ctx.lineWidth = 1.5;
  ctx.stroke();

  // 装订孔（浅灰小圆，顶部内侧）
  ctx.fillStyle = s.sub;
  ctx.beginPath();
  ctx.arc(x0 + cardW * 0.22, y0 + Math.round(W * 0.018), Math.round(W * 0.008), 0, Math.PI * 2);
  ctx.fill();
  ctx.beginPath();
  ctx.arc(x0 + cardW * 0.78, y0 + Math.round(W * 0.018), Math.round(W * 0.008), 0, Math.PI * 2);
  ctx.fill();

  // 日期条（卡片顶部 accent 色）
  var barH = Math.round(cardH * 0.2);
  core.roundRect(ctx, x0 + Math.round(W * 0.006), y0 + Math.round(W * 0.006), cardW - Math.round(W * 0.012), barH, Math.round(W * 0.014));
  ctx.fillStyle = s.accent;
  ctx.fill();
  var dateParts = data.dateStr.split('-');
  var prettyDate = dateParts[0] + '.' + dateParts[1] + '.' + dateParts[2];
  ctx.fillStyle = '#ffffff';
  ctx.textAlign = 'center';
  ctx.font = 'bold ' + Math.round(W * 0.034) + 'px sans-serif';
  ctx.fillText(data.mode === 'countdown' ? '距离 ' + prettyDate : '始于 ' + prettyDate, W / 2, y0 + Math.round(W * 0.006) + barH / 2);

  // 大数字 + DAYS（白卡片上深色）
  var dayStr = String(data.days);
  var dayFont = dayStr.length >= 4
    ? Math.round(W * 0.16)
    : (dayStr.length >= 3 ? Math.round(W * 0.23) : Math.round(W * 0.3));
  ctx.fillStyle = s.num;
  ctx.font = 'bold ' + dayFont + 'px serif';
  ctx.fillText(dayStr, W / 2, y0 + cardH * 0.58);
  ctx.fillStyle = s.accent;
  ctx.font = 'bold ' + Math.round(W * 0.042) + 'px sans-serif';
  ctx.fillText('DAYS', W / 2, y0 + cardH * 0.78);

  // ---- 纪念日名称（卡片下方；可能含 emoji，走 fillTextAdaptive 独立层做自适应抖动） ----
  var name = data.name;
  if (name) {
    if (name.length > 8) name = name.slice(0, 8);
    ctx.fillStyle = s.text;
    ctx.font = 'bold ' + Math.round(W * 0.082) + 'px serif';
    ctx.textAlign = 'center';
    if (ctx.fillTextAdaptive) {
      ctx.fillTextAdaptive(name, W / 2, Math.round(H * 0.66));
    } else {
      ctx.fillText(name, W / 2, Math.round(H * 0.66));
    }
  }

  // ---- 状态行：倒计时中 / 已纪念 ----
  ctx.fillStyle = s.accent;
  ctx.font = 'bold ' + Math.round(W * 0.034) + 'px sans-serif';
  ctx.fillText(data.mode === 'countdown' ? '倒计时中' : '已纪念', W / 2, Math.round(H * 0.74));

  // ---- 底部提示 ----
  ctx.fillStyle = s.sub;
  ctx.font = Math.round(W * 0.028) + 'px sans-serif';
  var hint = data.mode === 'countdown'
    ? (data.days > 0 ? '再坚持 ' + data.days + ' 天就到大日子' : '就是今天！大日子来啦')
    : '那些日子都值得被记住';
  ctx.fillText(hint, W / 2, Math.round(H * 0.87));

  // ---- 底部装饰线 ----
  ctx.strokeStyle = s.accent;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(W / 2 - Math.round(W * 0.06), Math.round(H * 0.93));
  ctx.lineTo(W / 2 + Math.round(W * 0.06), Math.round(H * 0.93));
  ctx.stroke();
}

module.exports = {
  SCHEMES: SCHEMES,
  buildData: buildData,
  render: render
};
