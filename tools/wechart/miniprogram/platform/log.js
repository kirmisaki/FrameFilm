// 调试日志：环形缓冲，供设置页查看 BLE 收发
const MAX = 200;
let entries = [];
const listeners = new Set();

function push(text, type) {
  entries.push({ text: text, type: type || 'info', time: Date.now() });
  if (entries.length > MAX) entries.shift();
  notify();
}

function notify() {
  const snapshot = entries.slice();
  listeners.forEach(function (fn) {
    try {
      fn(snapshot);
    } catch (e) {}
  });
}

function get() {
  return entries.slice();
}

function clear() {
  entries = [];
  notify();
}

function subscribe(fn) {
  listeners.add(fn);
  return function () {
    listeners.delete(fn);
  };
}

module.exports = { push, get, clear, subscribe };
