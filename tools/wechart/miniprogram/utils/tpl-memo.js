// 模板 - 备忘录
var core = require('./template-core');

var SCHEMES = [
  { name: '朱砂', accent: '#e8553d', text: '#3a3631', sub: '#4a443c', sub2: '#5a5448' },
  { name: '晴蓝', accent: '#2f6fd8', text: '#2c3a4d', sub: '#33455c', sub2: '#47586e' },
  { name: '竹绿', accent: '#1fa86c', text: '#2c4438', sub: '#33503f', sub2: '#46604f' },
  { name: '蜜桃', accent: '#e8557a', text: '#453a38', sub: '#4a3c3a', sub2: '#5e4a46' },
  { name: '赤金', accent: '#c44a1f', text: '#453b28', sub: '#4a402c', sub2: '#5c503a' }
];
var MAX_ITEMS = 8;

function pad(n) {
  return ('0' + n).slice(-2);
}
function todayStr() {
  var d = new Date();
  return d.getFullYear() + '.' + pad(d.getMonth() + 1) + '.' + pad(d.getDate());
}

function buildData(items) {
  var doneCount = 0;
  items.forEach(function (it) { if (it.done) doneCount++; });
  return { items: items, todayStr: todayStr(), doneCount: doneCount };
}

function render(ctx, W, H, data, scheme) {
  var s = scheme; var margin = Math.round(W * 0.07);
  // 纯白背景
  ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, W, H);
  ctx.textBaseline = 'middle';
  // 顶部品牌行
  ctx.fillStyle = s.sub; ctx.font = Math.round(W * 0.033) + 'px sans-serif'; ctx.textAlign = 'left';
  ctx.fillText('FRAME FILM · 备忘录', margin, Math.round(H * 0.05));
  ctx.textAlign = 'right';
  ctx.fillText(data.todayStr, W - margin, Math.round(H * 0.05));
  // 大标题 + accent 下划线
  ctx.fillStyle = s.text; ctx.textAlign = 'center';
  ctx.font = 'bold ' + Math.round(W * 0.075) + 'px serif';
  ctx.fillText('今日备忘', W / 2, Math.round(H * 0.15));
  ctx.strokeStyle = s.accent; ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.moveTo(W / 2 - Math.round(W * 0.04), Math.round(H * 0.21));
  ctx.lineTo(W / 2 + Math.round(W * 0.04), Math.round(H * 0.21));
  ctx.stroke();
  // 条目卡片
  var items = data.items || [];
  var rowH = Math.round(W * 0.08);
  var padY = Math.round(W * 0.05);
  var cardX = margin; var cardW = W - margin * 2;
  var cardY = Math.round(H * 0.26);
  var cardH = padY * 2 + Math.max(items.length, 1) * rowH;
  core.roundRect(ctx, cardX, cardY, cardW, cardH, Math.round(W * 0.02));
  ctx.fillStyle = '#ffffff'; ctx.fill();
  ctx.strokeStyle = s.sub2; ctx.lineWidth = 1.5; ctx.stroke();
  if (items.length === 0) {
    ctx.fillStyle = s.sub2; ctx.font = Math.round(W * 0.038) + 'px sans-serif'; ctx.textAlign = 'center';
    ctx.fillText('暂无备忘，点击下方添加', W / 2, cardY + cardH / 2);
  } else {
    var box = Math.round(W * 0.045);
    var textX = cardX + Math.round(W * 0.05) + box + Math.round(W * 0.03);
    var maxW = cardX + cardW - Math.round(W * 0.05) - textX;
    items.forEach(function (it, i) {
      var cy = cardY + padY + i * rowH;
      var bx = cardX + Math.round(W * 0.05);
      // 勾选框
      core.roundRect(ctx, bx, cy - box / 2, box, box, Math.round(box * 0.25));
      if (it.done) {
        ctx.fillStyle = s.accent; ctx.fill();
        ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 2; ctx.lineCap = 'round';
        ctx.beginPath();
        ctx.moveTo(bx + box * 0.22, cy); ctx.lineTo(bx + box * 0.44, cy + box * 0.2);
        ctx.lineTo(bx + box * 0.8, cy - box * 0.24);
        ctx.stroke();
      } else {
        ctx.strokeStyle = s.sub2; ctx.lineWidth = 1.5; ctx.stroke();
      }
      // 文本（超长截断）
      ctx.fillStyle = it.done ? s.sub2 : s.text;
      ctx.font = Math.round(W * 0.042) + 'px sans-serif';
      ctx.textAlign = 'left';
      var txt = it.text;
      if (ctx.measureText(txt).width > maxW) {
        while (txt.length > 1 && ctx.measureText(txt + '…').width > maxW) txt = txt.slice(0, -1);
        txt += '…';
      }
      ctx.fillText(txt, textX, cy);
      // 完成态删除线
      if (it.done) {
        var tw = ctx.measureText(txt).width;
        ctx.strokeStyle = s.sub2; ctx.lineWidth = 1.5; ctx.lineCap = 'butt';
        ctx.beginPath();
        ctx.moveTo(textX, cy); ctx.lineTo(textX + tw, cy); ctx.stroke();
      }
      // 条目分割线
      if (i < items.length - 1) {
        ctx.strokeStyle = s.sub2; ctx.lineWidth = 1; ctx.globalAlpha = 0.35;
        ctx.beginPath();
        ctx.moveTo(cardX + Math.round(W * 0.05), cy + rowH / 2);
        ctx.lineTo(cardX + cardW - Math.round(W * 0.05), cy + rowH / 2);
        ctx.stroke();
        ctx.globalAlpha = 1;
      }
    });
  }
  // 底部统计 + 装饰线
  ctx.fillStyle = s.sub2; ctx.textAlign = 'center';
  ctx.font = Math.round(W * 0.028) + 'px sans-serif';
  ctx.fillText('共 ' + items.length + ' 条 · 完成 ' + data.doneCount + ' 条', W / 2, Math.round(H * 0.88));
  ctx.strokeStyle = s.accent; ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(W / 2 - Math.round(W * 0.06), Math.round(H * 0.94));
  ctx.lineTo(W / 2 + Math.round(W * 0.06), Math.round(H * 0.94));
  ctx.stroke();
}

module.exports = {
  SCHEMES: SCHEMES,
  MAX_ITEMS: MAX_ITEMS,
  buildData: buildData,
  render: render
};
