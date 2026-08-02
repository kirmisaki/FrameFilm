// tpl-calendar.js - 日历模板渲染器
// 月历网格 + 今日高亮 + 农历/节气/节日标注 + 底部进度条 + 配色切换
var core = require('./template-core');

// ============ 农历（1900-2100） ============
// 标准 lunarInfo 表：每年用一个 16 位十六进制数编码闰月/大小月
var lunarInfo = [
  0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
  0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
  0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
  0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
  0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
  0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
  0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
  0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b5a0, 0x195a6,
  0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
  0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5, 0x092e0,
  0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
  0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
  0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
  0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
  0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
  0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
  0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
  0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
  0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
  0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
  0x0d520
];

var MON_NAMES = ['正', '二', '三', '四', '五', '六', '七', '八', '九', '十', '冬', '腊'];
var DAY_NAMES = ['初一', '初二', '初三', '初四', '初五', '初六', '初七', '初八', '初九', '初十',
  '十一', '十二', '十三', '十四', '十五', '十六', '十七', '十八', '十九', '二十',
  '廿一', '廿二', '廿三', '廿四', '廿五', '廿六', '廿七', '廿八', '廿九', '三十'];

function leapMonth(y) { return lunarInfo[y - 1900] & 0xf; }
function leapDays(y) { return leapMonth(y) ? ((lunarInfo[y - 1900] & 0x10000) ? 30 : 29) : 0; }
function monthDays(y, m) { return (lunarInfo[y - 1900] & (0x10000 >> m)) ? 30 : 29; }
function lYearDays(y) {
  var sum = 348;
  for (var i = 0x8000; i > 0x8; i >>= 1) { sum += (lunarInfo[y - 1900] & i) ? 1 : 0; }
  return sum + leapDays(y);
}

// 阳历 -> 农历 { year, month, day, isLeap }
function solar2lunar(y, m, d) {
  var base = new Date(1900, 0, 31);
  var obj = new Date(y, m - 1, d);
  var offset = Math.floor((obj - base) / 86400000);
  var i, temp = 0;
  for (i = 1900; i < 2101 && temp + lYearDays(i) <= offset; i++) { temp += lYearDays(i); }
  var ly = i;
  var leap = leapMonth(ly);
  var isLeap = false;
  for (i = 1; i < 13 && temp + monthDays(ly, i) <= offset; i++) {
    temp += monthDays(ly, i);
    if (leap === i) {
      if (temp + leapDays(ly) <= offset) {
        temp += leapDays(ly);
      } else {
        isLeap = true;
        break;
      }
    }
  }
  return { year: ly, month: i, day: offset - temp + 1, isLeap: isLeap };
}

// 节气（公历固定日期近似）
var SOLAR_TERMS = {
  1: { 5: '小寒', 20: '大寒' },
  2: { 4: '立春', 19: '雨水' },
  3: { 6: '惊蛰', 21: '春分' },
  4: { 5: '清明', 20: '谷雨' },
  5: { 6: '立夏', 21: '小满' },
  6: { 6: '芒种', 21: '夏至' },
  7: { 7: '小暑', 23: '大暑' },
  8: { 8: '立秋', 23: '处暑' },
  9: { 8: '白露', 23: '秋分' },
  10: { 8: '寒露', 23: '霜降' },
  11: { 7: '立冬', 22: '小雪' },
  12: { 7: '大雪', 22: '冬至' }
};

function getTerm(m, d) {
  var t = SOLAR_TERMS[m];
  return t ? (t[d] || null) : null;
}

// 公历节日
var FESTIVALS = {
  '1-1': '元旦', '2-14': '情人节', '3-8': '妇女节', '4-1': '愚人节',
  '5-1': '劳动节', '5-4': '青年节', '6-1': '儿童节', '7-1': '建党节',
  '8-1': '建军节', '9-10': '教师节', '10-1': '国庆节', '12-24': '平安夜', '12-25': '圣诞节'
};

// 农历节日
function getLunarFestival(lm, ld) {
  if (lm === 1 && ld === 1) return '春节';
  if (lm === 1 && ld === 15) return '元宵节';
  if (lm === 5 && ld === 5) return '端午节';
  if (lm === 7 && ld === 7) return '七夕';
  if (lm === 8 && ld === 15) return '中秋节';
  if (lm === 9 && ld === 9) return '重阳节';
  if (lm === 12 && ld === 30) return '除夕';
  return null;
}

// ============ 配色方案（浅色纸张风；上屏经抖动呈现） ============
// 文字/描边只用深色系（映射为 EPD 黑色，保证清晰），颜色由 accent 饱和色承担
var SCHEMES = [
  { name: '米杏', text: '#3a3631', sub: '#4a443c', sub2: '#5a5448', accent: '#e8553d', card: '#ffffff' },
  { name: '雾蓝', text: '#2c3a4d', sub: '#33455c', sub2: '#47586e', accent: '#2f6fd8', card: '#ffffff' },
  { name: '薄荷', text: '#2c4438', sub: '#33503f', sub2: '#46604f', accent: '#1fa86c', card: '#ffffff' },
  { name: '奶油', text: '#453a38', sub: '#4a3c3a', sub2: '#5e4a46', accent: '#e8557a', card: '#ffffff' },
  { name: '杏黄', text: '#453b28', sub: '#4a402c', sub2: '#5c503a', accent: '#d94a2a', card: '#ffffff' }
];

// ============ 数据构建 ============
function buildData(date) {
  var y = date.getFullYear();
  var m = date.getMonth() + 1;
  var today = date.getDate();
  var daysInMonth = new Date(y, m, 0).getDate();
  var firstIdx = (new Date(y, m - 1, 1).getDay() + 6) % 7; // 周一=0

  var cells = [];
  var todayLunar = '';
  for (var i = 0; i < 42; i++) {
    var d = i - firstIdx + 1;
    if (d < 1 || d > daysInMonth) {
      cells.push(null);
      continue;
    }
    var lunar = solar2lunar(y, m, d);
    var fest = FESTIVALS[m + '-' + d];
    var term = getTerm(m, d);
    var lf = getLunarFestival(lunar.month, lunar.day);
    var note;
    if (fest) note = fest;
    else if (term) note = term;
    else if (lf) note = lf;
    else if (lunar.day === 1) note = MON_NAMES[lunar.month - 1] + '月';
    else note = DAY_NAMES[lunar.day - 1];

    var isToday = (d === today);
    if (isToday) {
      todayLunar = (lunar.isLeap ? '闰' : '') + MON_NAMES[lunar.month - 1] + '月' + DAY_NAMES[lunar.day - 1];
    }
    cells.push({
      day: d,
      note: note,
      isToday: isToday,
      isWeekend: (i % 7 === 5 || i % 7 === 6)
    });
  }

  var progress = Math.round(today / daysInMonth * 100);
  return {
    year: y,
    month: m,
    today: today,
    todayLunar: todayLunar,
    cells: cells,
    progress: progress,
    remainDays: daysInMonth - today
  };
}

// ============ 渲染（深色月历海报） ============
var MONTH_EN = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'];
function render(ctx, W, H, data, scheme) {
  var s = scheme || SCHEMES[0];
  var margin = Math.round(W * 0.06);

  // ---- 纯白背景 ----
  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, W, H);
  ctx.textBaseline = 'middle';

  // ---- 顶部标题区：大月份 + 年份（左）/ 今日农历（右） ----
  ctx.fillStyle = s.text;
  ctx.font = 'bold ' + Math.round(W * 0.1) + 'px serif';
  ctx.textAlign = 'left';
  ctx.fillText(data.month + '月', margin, Math.round(H * 0.055));

  ctx.font = Math.round(W * 0.038) + 'px sans-serif';
  ctx.fillStyle = s.sub;
  ctx.fillText(MONTH_EN[data.month - 1] + ' · ' + data.year, margin + Math.round(W * 0.16), Math.round(H * 0.055));

  ctx.fillStyle = s.accent;
  ctx.font = 'bold ' + Math.round(W * 0.032) + 'px sans-serif';
  ctx.textAlign = 'right';
  ctx.fillText('今日 ' + data.todayLunar, W - margin, Math.round(H * 0.055));

  // 标题分隔线
  ctx.strokeStyle = s.accent;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(margin, Math.round(H * 0.13));
  ctx.lineTo(W - margin, Math.round(H * 0.13));
  ctx.stroke();

  // ---- 星期行 ----
  var weekDays = ['一', '二', '三', '四', '五', '六', '日'];
  var gridLeft = margin;
  var gridW = W - margin * 2;
  var cellW = gridW / 7;
  var headerY = Math.round(H * 0.165);
  ctx.font = Math.round(W * 0.034) + 'px sans-serif';
  ctx.fillStyle = s.sub;
  ctx.textAlign = 'center';
  for (var i = 0; i < 7; i++) {
    ctx.fillText(weekDays[i], Math.round(gridLeft + cellW * i + cellW / 2), headerY);
  }

  // ---- 日期卡片网格 ----
  var gridTop = Math.round(H * 0.2);
  var gridBottom = Math.round(H * 0.8);
  var cellH = (gridBottom - gridTop) / 6;
  var gap = Math.round(W * 0.008);
  for (var idx = 0; idx < data.cells.length; idx++) {
    var cell = data.cells[idx];
    if (!cell) continue;
    var row = Math.floor(idx / 7);
    var col = idx % 7;
    var cx = Math.round(gridLeft + col * cellW + cellW / 2);
    var cy = Math.round(gridTop + row * cellH + cellH / 2);
    var cardW = Math.round(cellW - gap * 2);
    var cardH = Math.round(cellH - gap * 2);

    // 卡片底 + 浅色描边
    core.roundRect(ctx, cx - cardW / 2, cy - cardH / 2, cardW, cardH, Math.round(W * 0.012));
    ctx.fillStyle = s.card;
    ctx.fill();
    ctx.strokeStyle = s.sub2;
    ctx.lineWidth = 1;
    ctx.stroke();

    // 今日：强调色圆底
    if (cell.isToday) {
      ctx.beginPath();
      ctx.arc(cx, Math.round(cy - cellH * 0.12), Math.round(cellW * 0.32), 0, Math.PI * 2);
      ctx.fillStyle = s.accent;
      ctx.fill();
    }

    // 日期数字
    ctx.font = 'bold ' + Math.round(cellW * 0.36) + 'px sans-serif';
    ctx.fillStyle = cell.isToday ? '#ffffff' : s.text;
    ctx.fillText(String(cell.day), cx, Math.round(cy - cellH * 0.1));

    // 小字标注（农历/节气/节日），今日用白色
    ctx.font = Math.round(cellW * 0.18) + 'px sans-serif';
    ctx.fillStyle = cell.isToday ? '#ffffff' : s.sub2;
    ctx.fillText(cell.note, cx, Math.round(cy + cellH * 0.3));
  }

  // ---- 底部本月进度条 ----
  var barY = Math.round(H * 0.85);
  var barH = Math.max(5, Math.round(H * 0.012));
  core.roundRect(ctx, gridLeft, barY, gridW, barH, barH / 2);
  ctx.strokeStyle = s.accent;
  ctx.lineWidth = 1.5;
  ctx.stroke();
  if (data.progress > 0) {
    core.roundRect(ctx, gridLeft, barY, Math.round(gridW * data.progress / 100), barH, barH / 2);
    ctx.fillStyle = s.accent;
    ctx.fill();
  }

  ctx.fillStyle = s.sub;
  ctx.font = Math.round(W * 0.03) + 'px sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('本月已过 ' + data.progress + '% · 距月底还有 ' + data.remainDays + ' 天', W / 2, Math.round(H * 0.9));
}

module.exports = {
  SCHEMES: SCHEMES,
  buildData: buildData,
  render: render
};
