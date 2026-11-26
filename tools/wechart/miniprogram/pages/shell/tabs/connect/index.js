const store = require('../../../../platform/store');
const ble = require('../../../../platform/ble');
const U = require('../../../../utils/ble-utils');
const toast = require('../../../../platform/toast');

const MODEL_LABEL = {
  FRAMEFILM: '基础版',
  FRAMEFILMPRO: 'Pro',
  FRAMEFILMMAX: 'Max',
  FRAMEFILMDOCK: 'Dock',
  FRAMEFILMARK: '通行证版',
  FRAMEFILMC3: 'C3'
};

Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  data: {
    scanning: false,
    connecting: false,
    connected: false,
    devices: [],
    reconnectAvailable: false,
    statusText: '未连接',
    modelLabel: '',
    screenName: '',
    width: 0,
    height: 0,
    battery: 0,

    networkOpen: false,
    wifiEnable: 0,
    wifiConnected: 0,
    wifiStatusText: '未连接',
    ssid: '',
    password: '',
    apiUrl: '',
    heartbeatUrl: '',
    heartbeatInterval: 60,
    downloadStatusText: '就绪'
  },

  lifetimes: {
    attached: function () {
      this._editing = {};
      this._unsub = store.subscribe(this._sync.bind(this));
      this._sync(store.get());
    },
    detached: function () {
      if (this._unsub) this._unsub();
    }
  },

  methods: {
    _sync: function (state) {
      const patch = {
        scanning: state.scanning,
        connecting: state.connecting,
        connected: state.connected,
        devices: state.devices || [],
        battery: state.battery,
        wifiEnable: state.wifiEnable,
        wifiConnected: state.wifiConnected,
        wifiStatusText: state.wifiConnected ? '已连接路由器' : '未连接',
        heartbeatInterval: state.heartbeatInterval,
        modelLabel: MODEL_LABEL[state.model] || state.model || '',
        screenName: state.screenName,
        width: state.width,
        height: state.height,
        downloadStatusText: this._downloadText(state.downloadState, state.downloadProgress)
      };

      if (state.connected) {
        patch.statusText = state.name + ' 已连接';
      } else if (state.connecting) {
        patch.statusText = '正在连接...';
      } else {
        patch.statusText = '未连接';
      }

      let cached = null;
      try {
        cached = wx.getStorageSync('ff_last_device');
      } catch (e) {}
      patch.reconnectAvailable = !state.connected && !!(cached && cached.deviceId);

      if (!this._editing.ssid) patch.ssid = state.ssid || '';
      if (!this._editing.password) patch.password = state.password || '';
      if (!this._editing.apiUrl) patch.apiUrl = state.apiUrl || '';
      if (!this._editing.heartbeatUrl) patch.heartbeatUrl = state.heartbeatUrl || '';
      if (!this._editing.heartbeatInterval) patch.heartbeatInterval = state.heartbeatInterval;

      this.setData(patch);
    },

    _downloadText: function (state, progress) {
      if (state === 1) return '下载中... ' + (progress || 0) + '%';
      if (state === 2) return '下载完成';
      if (state === 3) return '下载失败';
      return '就绪';
    },

    onScanTap: function () {
      if (this.data.scanning) {
        ble.stopScan();
        return;
      }
      if (this.data.connected || this.data.connecting) return;
      ble.startScan(function () {});
    },

    onReconnect: function () {
      if (this.data.connected || this.data.connecting) return;
      ble.reconnectLast().then(function () {
        toast.show('已重连', 'ok');
      }).catch(function (err) {
        toast.show((err && err.message) || '重连失败', 'error');
      });
    },

    onConnect: function (e) {
      if (this.data.connected || this.data.connecting) return;
      const index = Number(e.currentTarget.dataset.index);
      const device = this.data.devices[index];
      if (!device) return;
      ble.connect(device, function (text) {
        this.setData({ statusText: text });
      }.bind(this)).then(function () {
        toast.show('已连接 ' + device.name, 'ok');
      }).catch(function (err) {
        toast.show((err && err.errMsg) || '连接失败', 'error');
      });
    },

    onDisconnect: function () {
      ble.disconnect().then(function () {
        toast.show('已断开');
      });
    },

    toggleNetwork: function () {
      this.setData({ networkOpen: !this.data.networkOpen });
    },

    onWifiToggle: function () {
      const turningOff = !!this.data.wifiEnable;
      const value = turningOff ? 0 : 1;
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE, value).catch(function () {
        toast.show('设置失败', 'error');
      });
      if (turningOff && store.get().photoMode === 2) {
        ble.send(U.BLE_FILM_TRANS_CH_CTRL_MODE, 1).then(function () {
          store.patch({ photoMode: 1 });
        }).catch(function () {});
      }
    },

    onInput: function (e) {
      const field = e.currentTarget.dataset.field;
      if (!field) return;
      this._editing[field] = true;
      const patch = {};
      patch[field] = e.detail.value;
      this.setData(patch);
    },

    onBlur: function (e) {
      const field = e.currentTarget.dataset.field;
      const cmd = Number(e.currentTarget.dataset.cmd);
      const maxLen = Number(e.currentTarget.dataset.max || 128);
      if (!field || !cmd) return;
      this._editing[field] = false;
      ble.sendString(cmd, this.data[field] || '', maxLen).catch(function () {
        toast.show('保存失败', 'error');
      });
    },

    onApplyApi: function () {
      this._editing.apiUrl = false;
      ble.sendString(U.BLE_FILM_TRANS_CH_CTRL_FILM_API_URL, this.data.apiUrl || '', 128).then(function () {
        toast.show('API 地址已设置', 'ok');
      }).catch(function () {
        toast.show('设置失败', 'error');
      });
    },

    onApplyHeartbeat: function () {
      this._editing.heartbeatInterval = false;
      let value = Number(this.data.heartbeatInterval) || 5;
      value = Math.max(5, Math.min(180, value));
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_FILM_HEARTBEAT_INTERVAL, value).then(function () {
        toast.show('心跳间隔已设置', 'ok');
      }).catch(function () {
        toast.show('设置失败', 'error');
      });
    },

    onWifiConnect: function () {
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT).then(function () {
        toast.show('已请求连接');
        setTimeout(function () {
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET).catch(function () {});
        }, 1500);
        setTimeout(function () {
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET).catch(function () {});
        }, 4000);
      }).catch(function () {
        toast.show('请求失败', 'error');
      });
    },

    onWifiDisconnect: function () {
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT).then(function () {
        setTimeout(function () {
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET).catch(function () {});
        }, 800);
      }).catch(function () {});
    },

    onWifiClear: function () {
      wx.showModal({
        title: '清除网络配置',
        content: '将清除设备保存的 WiFi 与心跳配置，确定继续？',
        confirmText: '清除',
        confirmColor: '#B3261E',
        success: function (res) {
          if (!res.confirm) return;
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR).then(function () {
            this._editing = {};
            store.patch({
              ssid: '',
              password: '',
              apiUrl: '',
              heartbeatUrl: '',
              wifiEnable: 0,
              wifiConnected: 0,
              photoMode: store.get().photoMode === 2 ? 1 : store.get().photoMode
            });
            this.setData({ ssid: '', password: '', apiUrl: '', heartbeatUrl: '' });
            if (store.get().photoMode === 1) {
              ble.send(U.BLE_FILM_TRANS_CH_CTRL_MODE, 1).catch(function () {});
            }
            toast.show('已清除网络配置', 'ok');
          }.bind(this)).catch(function () {});
        }
      });
    },

    onDownloadTest: function () {
      this.setData({ downloadStatusText: '下载中... 0%' });
      ble.downloadTest(function (state, progress) {
        this.setData({ downloadStatusText: this._downloadText(state, progress) });
      }.bind(this)).then(function (state) {
        toast.show(state.downloadState === 2 ? '下载完成' : '下载失败', state.downloadState === 2 ? 'ok' : 'error');
      }).catch(function (err) {
        this.setData({ downloadStatusText: '下载失败' });
        toast.show((err && err.message) || '下载失败', 'error');
      });
    }
  }
});
