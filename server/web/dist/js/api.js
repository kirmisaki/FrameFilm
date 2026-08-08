/* film-hub 前端 API 层：JWT 封装 + 统一错误处理 */
(function (global) {
  const BASE = '/api/v1/admin';
  const TOKEN_KEY = 'fh_token';

  function getToken() { return localStorage.getItem(TOKEN_KEY) || ''; }
  function setToken(t) { localStorage.setItem(TOKEN_KEY, t); }
  function clearToken() { localStorage.removeItem(TOKEN_KEY); }
  function isAuthed() { return !!getToken(); }

  async function request(path, opts) {
    opts = opts || {};
    const headers = Object.assign({}, opts.headers || {});
    const token = getToken();
    if (token) headers['Authorization'] = 'Bearer ' + token;
    const res = await fetch(BASE + path, Object.assign({}, opts, { headers }));
    if (res.status === 401) {
      clearToken();
      if (!location.pathname.endsWith('login.html')) {
        location.href = 'login.html';
      }
      throw new Error('未登录或登录已过期');
    }
    if (!res.ok) {
      let msg = '请求失败 (' + res.status + ')';
      try {
        const j = await res.json();
        msg = (j.detail && typeof j.detail === 'string' ? j.detail : j.msg) || msg;
      } catch (e) { /* 非 JSON 响应 */ }
      throw new Error(msg);
    }
    const ct = res.headers.get('content-type') || '';
    if (ct.indexOf('application/json') >= 0) return res.json();
    return res;
  }

  global.API = {
    get: (p) => request(p),
    post: (p, body) => request(p, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
    }),
    put: (p, body) => request(p, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
    }),
    del: (p) => request(p, { method: 'DELETE' }),
    upload: (p, formData) => request(p, { method: 'POST', body: formData }),
    /* 登录（无 token，独立于 /admin 前缀之外的 auth 路由） */
    login: async (username, password) => {
      const res = await fetch('/api/v1/admin/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, password }),
      });
      if (!res.ok) {
        let msg = '登录失败';
        try { const j = await res.json(); msg = j.detail || msg; } catch (e) {}
        throw new Error(msg);
      }
      return res.json();
    },
    setToken, clearToken, isAuthed,
  };
})(window);
