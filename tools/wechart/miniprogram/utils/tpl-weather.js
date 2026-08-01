// tpl-weather.js - 天气模板渲染器
// 「深色氛围天气卡」：深色渐变底 + 大图标/大温度主视觉 + 未来 3 天半透明卡片
// 渲染使用任意设计色，上屏时由模板发送管线做自适应抖动转为 6 色网点质感
var core = require('./template-core');

// WMO 天气代码 -> 中文描述 / 图标类型
function codeToCond(code) {
  if (code === 0) return '晴';
  if (code === 1 || code === 2) return '多云';
  if (code === 3) return '阴';
  if (code >= 45 && code <= 48) return '雾';
  if (code >= 51 && code <= 67) return '雨';
  if (code >= 71 && code <= 77) return '雪';
  if (code >= 80 && code <= 82) return '阵雨';
  if (code >= 95) return '雷雨';
  return '多云';
}

// 0=晴 1=多云 2=阴 3=雨 4=阵雨/雷雨 5=雪
function codeToIcon(code) {
  if (code === 0) return 0;
  if (code === 1 || code === 2) return 1;
  if (code === 3) return 2;
  if (code >= 45 && code <= 48) return 2;
  if (code >= 51 && code <= 67) return 3;
  if (code >= 80 && code <= 82) return 4;
  if (code >= 95) return 4;
  if (code >= 71 && code <= 77) return 5;
  return 1;
}

// ============ 内置演示数据（无网/未授权时兜底） ============
function getDemoData() {
  var now = new Date();
  var weekNames = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];
  return {
    city: '深圳',
    cond: '多云',
    icon: 1,
    temp: 28,
    hi: 32,
    lo: 21,
    updated: '演示数据',
    days: [
      { name: weekNames[(now.getDay() + 1) % 7], icon: 0, hi: 32, lo: 22 },
      { name: weekNames[(now.getDay() + 2) % 7], icon: 3, hi: 29, lo: 21 },
      { name: weekNames[(now.getDay() + 3) % 7], icon: 1, hi: 30, lo: 20 }
    ]
  };
}

// 通过 Open-Meteo（免费、无需 key）拉取真实天气
function fetchWeather(callback) {
  wx.getLocation({
    type: 'gcj02',
    success: function (loc) {
      var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + loc.latitude +
        '&longitude=' + loc.longitude +
        '&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=3&timezone=auto';
      wx.request({
        url: url,
        success: function (res) {
          if (res.data && res.data.current) {
            var cur = res.data.current;
            var daily = res.data.daily;
            var weekNames = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];
            var days = [];
            for (var i = 0; i < daily.time.length; i++) {
              var d = new Date(daily.time[i] + 'T00:00:00');
              days.push({
                name: i === 0 ? '今日' : weekNames[d.getDay()],
                icon: codeToIcon(daily.weather_code[i]),
                hi: Math.round(daily.temperature_2m_max[i]),
                lo: Math.round(daily.temperature_2m_min[i])
              });
            }
            callback({
              city: '当前位置',
              cond: codeToCond(cur.weather_code),
              icon: codeToIcon(cur.weather_code),
              temp: Math.round(cur.temperature_2m),
              hi: days.length > 0 ? days[0].hi : 0,
              lo: days.length > 0 ? days[0].lo : 0,
              updated: '实时',
              days: days
            });
          } else {
            callback(getDemoData());
          }
        },
        fail: function () { callback(getDemoData()); }
      });
    },
    fail: function () { callback(getDemoData()); }
  });
}

// ============ 配色方案（浅色纸张风） ============
// 文字/描边只用深色系（映射为 EPD 黑色，保证清晰），颜色由 accent 饱和色承担
var SCHEMES = [
  { name: '晴蓝', bg1: '#f2f9ff', bg2: '#dceefb', accent: '#2f6fd8', text: '#1f3a5a', sub: '#2c4a6a', sub2: '#3a5a78', card: '#ffffff' },
  { name: '霞橙', bg1: '#fff6ef', bg2: '#ffe7d6', accent: '#e8762c', text: '#4a3020', sub: '#4a3324', sub2: '#5a3f2c', card: '#ffffff' },
  { name: '浅翠', bg1: '#f2faf4', bg2: '#ddf0e4', accent: '#1fa86c', text: '#23402f', sub: '#26443a', sub2: '#3a5848', card: '#ffffff' },
  { name: '靛蓝', bg1: '#f4f5fc', bg2: '#e2e4f2', accent: '#4450b8', text: '#2e3350', sub: '#2e3350', sub2: '#454a66', card: '#ffffff' }
];

// ============ 几何天气图标（浅色主题，跟随配色） ============
// 云 = 白色 + sub2 描边；太阳/雨/雪/闪电 = accent 色
function drawCloud(ctx, cx, cy, r, s) {
  ctx.beginPath();
  ctx.arc(cx - r * 0.38, cy, r * 0.34, 0, Math.PI * 2);
  ctx.arc(cx, cy - r * 0.22, r * 0.42, 0, Math.PI * 2);
  ctx.arc(cx + r * 0.4, cy, r * 0.32, 0, Math.PI * 2);
  ctx.arc(cx, cy + r * 0.08, r * 0.52, 0, Math.PI * 2);
  ctx.closePath();
  ctx.fillStyle = '#ffffff';
  ctx.fill();
  ctx.strokeStyle = s.sub2;
  ctx.lineWidth = Math.max(1.5, r * 0.08);
  ctx.stroke();
}

function drawSun(ctx, cx, cy, r, s) {
  ctx.lineWidth = Math.max(2, r * 0.11);
  ctx.strokeStyle = s.accent;
  ctx.fillStyle = s.accent;
  ctx.lineCap = 'round';
  for (var i = 0; i < 8; i++) {
    var a = Math.PI / 4 * i;
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(a) * r * 0.8, cy + Math.sin(a) * r * 0.8);
    ctx.lineTo(cx + Math.cos(a) * r * 1.12, cy + Math.sin(a) * r * 1.12);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.arc(cx, cy, r * 0.55, 0, Math.PI * 2);
  ctx.fill();
}

function drawIcon(ctx, cx, cy, r, type, s) {
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  if (type === 0) {
    drawSun(ctx, cx, cy, r, s);
  } else if (type === 1) {
    drawSun(ctx, cx - r * 0.4, cy - r * 0.28, r * 0.5, s);
    drawCloud(ctx, cx + r * 0.12, cy + r * 0.22, r * 0.72, s);
  } else if (type === 2) {
    drawCloud(ctx, cx, cy, r * 0.9, s);
  } else if (type === 3) {
    drawCloud(ctx, cx, cy - r * 0.12, r * 0.72, s);
    ctx.strokeStyle = s.accent;
    ctx.lineWidth = Math.max(2, r * 0.1);
    for (var j = -1; j <= 1; j++) {
      ctx.beginPath();
      ctx.moveTo(cx + j * r * 0.32, cy + r * 0.42);
      ctx.lineTo(cx + j * r * 0.38, cy + r * 0.78);
      ctx.stroke();
    }
  } else if (type === 4) {
    drawCloud(ctx, cx, cy - r * 0.12, r * 0.72, s);
    ctx.fillStyle = s.accent;
    ctx.beginPath();
    ctx.moveTo(cx + r * 0.12, cy + r * 0.3);
    ctx.lineTo(cx - r * 0.18, cy + r * 0.62);
    ctx.lineTo(cx + r * 0.02, cy + r * 0.62);
    ctx.lineTo(cx - r * 0.12, cy + r * 0.95);
    ctx.lineTo(cx + r * 0.22, cy + r * 0.55);
    ctx.lineTo(cx + r * 0.02, cy + r * 0.55);
    ctx.closePath();
    ctx.fill();
  } else {
    drawCloud(ctx, cx, cy - r * 0.12, r * 0.72, s);
    ctx.fillStyle = s.accent;
    for (var k = -1; k <= 1; k++) {
      ctx.beginPath();
      ctx.arc(cx + k * r * 0.32, cy + r * 0.58, r * 0.12, 0, Math.PI * 2);
      ctx.fill();
    }
  }
}

// ============ 渲染（深色氛围天气卡） ============
function render(ctx, W, H, data, scheme) {
  var s = scheme || SCHEMES[0];
  var margin = Math.round(W * 0.07);

  // ---- 深色背景渐变 ----
  var bg = ctx.createLinearGradient(0, 0, W, H);
  bg.addColorStop(0, s.bg1);
  bg.addColorStop(1, s.bg2);
  ctx.fillStyle = bg;
  ctx.fillRect(0, 0, W, H);
  ctx.textBaseline = 'middle';

  // ---- 顶部：城市（左）+ 更新时间胶囊（右） ----
  ctx.fillStyle = s.text;
  ctx.font = 'bold ' + Math.round(W * 0.052) + 'px serif';
  ctx.textAlign = 'left';
  ctx.fillText(data.city, margin, Math.round(H * 0.06));

  var tagW = Math.round(W * 0.13);
  var tagH = Math.round(W * 0.052);
  core.roundRect(ctx, W - margin - tagW, Math.round(H * 0.06) - tagH / 2, tagW, tagH, tagH / 2);
  ctx.fillStyle = s.accent;
  ctx.fill();
  ctx.fillStyle = '#ffffff';
  ctx.font = 'bold ' + Math.round(W * 0.026) + 'px sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText(data.updated, W - margin - tagW / 2, Math.round(H * 0.06));

  // ---- 主视觉：大图标 + 大温度 ----
  var iconR = Math.round(W * 0.16);
  drawIcon(ctx, Math.round(W * 0.3), Math.round(H * 0.3), iconR, data.icon, s);

  var tempStr = data.temp + '°';
  var tempFont = Math.round(W * 0.19);
  ctx.fillStyle = s.text;
  ctx.font = 'bold ' + tempFont + 'px serif';
  ctx.textAlign = 'left';
  ctx.fillText(tempStr, Math.round(W * 0.47), Math.round(H * 0.3));
  ctx.fillStyle = s.accent;
  ctx.font = 'bold ' + Math.round(W * 0.058) + 'px serif';
  ctx.fillText(data.cond, Math.round(W * 0.48), Math.round(H * 0.42));

  // 副行：温度范围
  ctx.fillStyle = s.sub;
  ctx.font = Math.round(W * 0.04) + 'px sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('最高 ' + data.hi + '°   最低 ' + data.lo + '°', W / 2, Math.round(H * 0.5));

  // ---- 分隔线 ----
  ctx.strokeStyle = s.accent;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(margin, Math.round(H * 0.545));
  ctx.lineTo(W - margin, Math.round(H * 0.545));
  ctx.stroke();

  // ---- 未来 3 天卡片（半透明 + accent 描边） ----
  var cardW = (W - margin * 2 - Math.round(W * 0.04) * 2) / 3;
  var cardH = Math.round(H * 0.3);
  var cardTop = Math.round(H * 0.58);
  for (var i = 0; i < data.days.length; i++) {
    var day = data.days[i];
    var x = margin + i * (cardW + Math.round(W * 0.04));
    core.roundRect(ctx, x, cardTop, cardW, cardH, Math.round(W * 0.02));
    ctx.fillStyle = s.card;
    ctx.fill();
    ctx.strokeStyle = s.accent;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // 星期
    ctx.fillStyle = s.sub;
    ctx.font = 'bold ' + Math.round(W * 0.038) + 'px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(day.name, x + cardW / 2, cardTop + cardH * 0.14);

    // 小图标
    drawIcon(ctx, Math.round(x + cardW / 2), Math.round(cardTop + cardH * 0.46), Math.round(cardW * 0.18), day.icon, s);

    // 温度：最高 accent 加粗，最低 sub2
    ctx.font = 'bold ' + Math.round(W * 0.042) + 'px sans-serif';
    ctx.fillStyle = s.accent;
    ctx.fillText(day.hi + '°', Math.round(x + cardW / 2 - cardW * 0.16), cardTop + cardH * 0.8);
    ctx.font = Math.round(W * 0.038) + 'px sans-serif';
    ctx.fillStyle = s.sub2;
    ctx.fillText(day.lo + '°', Math.round(x + cardW / 2 + cardW * 0.14), cardTop + cardH * 0.8);
  }

  // ---- 底部小标 ----
  ctx.font = Math.round(W * 0.03) + 'px sans-serif';
  ctx.fillStyle = s.accent;
  ctx.textAlign = 'center';
  ctx.fillText('FRAME FILM · WEATHER', W / 2, Math.round(H * 0.93));
}

module.exports = {
  SCHEMES: SCHEMES,
  getDemoData: getDemoData,
  fetchWeather: fetchWeather,
  render: render
};
