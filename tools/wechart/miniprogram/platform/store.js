// 全局状态：单例，页面与组件通过 subscribe 订阅
const state = {
  connected: false,
  connecting: false,
  scanning: false,
  devices: [],

  deviceId: '',
  serviceId: '',
  characteristicId: '',
  name: '',
  model: '',
  screenName: '',
  panelId: 0,
  width: 0,
  height: 0,
  battery: 0,

  fileList: [],
  currentFileId: -1,

  sleepOnOff: 0,
  autoWake: 0,
  wakeMinutes: 60,

  photoMode: 0,
  downloadState: 0,
  downloadProgress: 0,

  wifiEnable: 0,
  ssid: '',
  password: '',
  apiUrl: '',
  wifiConnected: 0,

  heartbeatUrl: '',
  heartbeatInterval: 60,

  // 页面间一次性的轻量数据，不落盘
  batchPaths: [],
  pendingImage: '',
  pendingName: ''
};

const listeners = new Set();

function get() {
  return state;
}

function patch(partial) {
  if (!partial) return state;
  Object.keys(partial).forEach(function (key) {
    state[key] = partial[key];
  });
  listeners.forEach(function (fn) {
    try {
      fn(state);
    } catch (e) {
      console.error('[store] listener error', e);
    }
  });
  return state;
}

function subscribe(fn) {
  listeners.add(fn);
  return function () {
    listeners.delete(fn);
  };
}

module.exports = { get, patch, subscribe };
