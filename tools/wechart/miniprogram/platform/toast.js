// 自绘提示：不依赖 wx.showToast，样式与 ForFilm 一致
const listeners = new Set();
let timer = null;

function notify(payload) {
  listeners.forEach(function (fn) {
    try {
      fn(payload);
    } catch (e) {}
  });
}

function show(message, type) {
  notify({ visible: true, message: message || '', type: type || 'info' });
  if (timer) clearTimeout(timer);
  timer = setTimeout(function () {
    notify({ visible: false, message: '', type: 'info' });
  }, 2200);
}

function hide() {
  if (timer) clearTimeout(timer);
  notify({ visible: false, message: '', type: 'info' });
}

function subscribe(fn) {
  listeners.add(fn);
  return function () {
    listeners.delete(fn);
  };
}

module.exports = { show, hide, subscribe };
