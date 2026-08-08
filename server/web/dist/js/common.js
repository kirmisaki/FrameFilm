/* film-hub 前端公共层：登录守卫 / Toast / 顶栏状态 / 工具函数 */
(function (global) {
  const PAGES = [
    ['dashboard.html', '仪表盘'],
    ['devices.html', '设备管理'],
    ['templates.html', '模板库'],
    ['streams.html', '轮播流'],
    ['albums.html', '相册'],
    ['ai.html', 'AI 创作'],
    ['settings.html', '设置'],
  ];

  /* 登录守卫：后台页面统一入口调用 */
  function guard() {
    if (!API.isAuthed()) {
      location.href = 'login.html';
      return false;
    }
    return true;
  }

  /* 顶栏在线状态：拉一次设备列表汇总在线数与平均电量 */
  async function refreshStatus() {
    const el = document.querySelector('.topnav .status');
    if (!el) return;
    try {
      const devs = await API.get('/devices');
      const online = devs.filter(d => d.online).length;
      const avg = devs.length
        ? Math.round(devs.reduce((s, d) => s + (d.battery_percent || 0), 0) / devs.length)
        : 100;
      el.innerHTML = '<span class="dot dot-pulse"></span>' + online + ' 台在线 · ' + avg + '%';
    } catch (e) { /* 忽略状态刷新失败 */ }
  }

  /* 全局 Toast（纸墨风格） */
  function toast(msg, type) {
    let box = document.getElementById('fh-toast');
    if (!box) {
      box = document.createElement('div');
      box.id = 'fh-toast';
      box.style.cssText =
        'position:fixed;top:18px;left:50%;transform:translateX(-50%) translateY(-16px);' +
        'z-index:999;opacity:0;transition:all .25s;display:flex;flex-direction:column;gap:8px;align-items:center;';
      document.body.appendChild(box);
    }
    const item = document.createElement('div');
    item.style.cssText =
      'background:var(--ink);color:var(--paper);font-size:13px;padding:9px 18px;' +
      'border-radius:8px;box-shadow:0 8px 20px rgba(0,0,0,.25);white-space:nowrap;letter-spacing:.5px;';
    if (type === 'err') item.style.background = 'var(--red)';
    if (type === 'ok') item.style.background = '#14640a';
    item.textContent = msg;
    box.appendChild(item);
    requestAnimationFrame(() => { box.style.opacity = '1'; box.style.transform = 'translateX(-50%) translateY(0)'; });
    setTimeout(() => {
      item.style.opacity = '0';
      item.style.transition = 'opacity .3s';
      setTimeout(() => item.remove(), 320);
    }, 2600);
  }

  /* 加载受鉴权保护的图片（如模板服务端预览），返回 objectURL */
  async function authImg(url) {
    const token = localStorage.getItem('fh_token') || '';
    const res = await fetch(url, { headers: { 'Authorization': 'Bearer ' + token } });
    if (!res.ok) throw new Error('图片加载失败 (' + res.status + ')');
    return URL.createObjectURL(await res.blob());
  }

  /* 格式化时间 */
  function fmtTime(v) {
    if (!v) return '—';
    const d = new Date(v);
    const p = n => (n < 10 ? '0' + n : n);
    return p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }
  function fmtClock(d) {
    const p = n => (n < 10 ? '0' + n : n);
    return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
  }

  /* 确认框（纸墨风格，返回 Promise<boolean>） */
  function confirmBox(msg) {
    return new Promise(resolve => {
      let mask = document.getElementById('fh-confirm');
      if (!mask) {
        mask = document.createElement('div');
        mask.id = 'fh-confirm';
        mask.style.cssText =
          'position:fixed;inset:0;background:rgba(26,26,26,.45);display:grid;place-items:center;z-index:998;';
        mask.innerHTML =
          '<div style="background:var(--paper);border:1px solid var(--ink);border-radius:12px;' +
          'width:340px;max-width:calc(100vw-40px);box-shadow:0 16px 30px rgba(0,0,0,.2)">' +
          '<div id="fh-confirm-msg" style="padding:22px 22px 6px;font-size:13.5px;line-height:1.7"></div>' +
          '<div style="padding:16px 22px;display:flex;justify-content:flex-end;gap:10px">' +
          '<button class="btn" id="fh-confirm-no">取消</button>' +
          '<button class="btn btn-danger" id="fh-confirm-yes">确认</button>' +
          '</div></div>';
        document.body.appendChild(mask);
      }
      mask.style.display = 'grid';
      document.getElementById('fh-confirm-msg').textContent = msg;
      const done = ok => { mask.style.display = 'none'; resolve(ok); };
      document.getElementById('fh-confirm-yes').onclick = () => done(true);
      document.getElementById('fh-confirm-no').onclick = () => done(false);
    });
  }

  /* e-ink 转场 + 导航跳转 */
  function setupNavRefresh() {
    const app = document.getElementById('app');
    if (!app) return;
    document.querySelectorAll('.nav a').forEach(a => {
      a.addEventListener('click', e => {
        const href = a.getAttribute('href');
        if (!href || href === location.pathname.split('/').pop()) return;
        e.preventDefault();
        app.classList.add('refresh');
        setTimeout(() => { location.href = href; }, 240);
      });
    });
  }

  global.FH = { guard, refreshStatus, toast, fmtTime, fmtClock, confirmBox, setupNavRefresh, authImg, PAGES };
})(window);
