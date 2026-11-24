const store = require('../../../../platform/store');
const ble = require('../../../../platform/ble');
const media = require('../../../../platform/media');
const toast = require('../../../../platform/toast');
const U = require('../../../../utils/ble-utils');
const debugLog = require('../../../../platform/log');

const DITHER_KEY = 'ff_create_dither_type';
const VERSION = '1.1.0';

const SAFE_ALGOS = [
  { label: '自适应', value: 'adaptive' },
  { label: 'Floyd-Steinberg', value: 'floydSteinberg' },
  { label: 'Atkinson', value: 'atkinson' },
  { label: 'Stucki', value: 'stucki' },
  { label: 'Jarvis-Judice-Ninke', value: 'jarvis' }
];

Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  data: {
    connected: false,
    sleepOnOff: 0,
    autoWake: 0,
    wakeMinutes: 60,
    wakeLabel: '1小时',

    fileList: [],
    selectedCount: 0,
    allSelected: false,
    photoMode: 0,
    wifiEnable: 0,

    otaName: '',
    otaSize: 0,
    otaProgress: 0,
    otaRunning: false,
    otaStatus: '',

    debugOpen: false,
    logs: [],

    algoValue: 'adaptive',
    algoOptions: SAFE_ALGOS,
    version: VERSION
  },

  lifetimes: {
    attached: function () {
      this._selected = {};
      this._unsub = store.subscribe(this._sync.bind(this));
      this._unsubLog = debugLog.subscribe(this._onLog.bind(this));
      this._sync(store.get());

      let saved = 'adaptive';
      try {
        saved = wx.getStorageSync(DITHER_KEY) || saved;
      } catch (e) {}
      const valid = SAFE_ALGOS.some(function (item) {
        return item.value === saved;
      });
      this.setData({ algoValue: valid ? saved : 'adaptive' });
    },
    detached: function () {
      if (this._unsub) this._unsub();
      if (this._unsubLog) this._unsubLog();
    }
  },

  methods: {
    _sync: function (state) {
      const wifi = state.wifiEnable || 0;
      const mode = (!wifi && state.photoMode === 2) ? 1 : (state.photoMode || 0);
      this.setData({
        connected: state.connected,
        sleepOnOff: state.sleepOnOff,
        autoWake: state.autoWake,
        wakeMinutes: state.wakeMinutes,
        wakeLabel: U.formatDuration(state.wakeMinutes || 60),
        photoMode: mode,
        wifiEnable: wifi
      });
      this._syncFiles(state);
    },

    _syncFiles: function (state) {
      const selected = this._selected || {};
      const list = (state.fileList || []).map(function (item) {
        return {
          fileId: item.fileId,
          name: item.name,
          selected: !!selected[item.fileId],
          current: item.fileId === state.currentFileId
        };
      });
      const selectedCount = list.filter(function (item) {
        return item.selected;
      }).length;
      this.setData({
        fileList: list,
        selectedCount: selectedCount,
        allSelected: list.length > 0 && selectedCount === list.length
      });
    },

    _onLog: function (logs) {
      if (!this.data.debugOpen) return;
      this.setData({ logs: logs.slice(-80) });
    },

    onRefreshFiles: function () {
      if (!this.data.connected) {
        toast.show('请先连接设备', 'warn');
        return;
      }
      ble.refreshFileList().then(function (list) {
        toast.show('已刷新，共 ' + (list || []).length + ' 个文件', 'ok');
      }).catch(function () {
        toast.show('刷新失败', 'error');
      });
    },

    toggleSelect: function (e) {
      const id = Number(e.currentTarget.dataset.id);
      if (Number.isNaN(id)) return;
      this._selected[id] = !this._selected[id];
      if (!this._selected[id]) delete this._selected[id];
      this._syncFiles(store.get());
    },

    toggleSelectAll: function () {
      if (this.data.allSelected) {
        this._selected = {};
      } else {
        const selected = {};
        this.data.fileList.forEach(function (item) {
          selected[item.fileId] = true;
        });
        this._selected = selected;
      }
      this._syncFiles(store.get());
    },

    deleteSelected: function () {
      if (!this._requireConnected()) return;
      const ids = Object.keys(this._selected).map(Number);
      if (!ids.length) return;
      wx.showModal({
        title: '删除文件',
        content: '确定删除选中的 ' + ids.length + ' 个文件？',
        confirmText: '删除',
        confirmColor: '#B3261E',
        success: function (res) {
          if (!res.confirm) return;
          let chain = Promise.resolve();
          ids.forEach(function (id) {
            chain = chain.then(function () {
              return ble.send(U.BLE_FILM_TRANS_CH_FILE_DELETE, id);
            });
          });
          chain.then(function () {
            this._selected = {};
            toast.show('已删除', 'ok');
            return ble.refreshFileList();
          }.bind(this)).catch(function () {
            toast.show('删除失败', 'error');
          });
        }.bind(this)
      });
    },

    displaySelected: function () {
      if (!this._requireConnected()) return;
      const ids = Object.keys(this._selected).map(Number);
      if (ids.length !== 1) {
        toast.show('请选择 1 个文件', 'warn');
        return;
      }
      ble.send(U.BLE_FILM_TRANS_CH_FILE_DISPLAY, ids[0]).then(function () {
        store.patch({ currentFileId: ids[0] });
        toast.show('已切换显示', 'ok');
      }).catch(function () {
        toast.show('切换失败', 'error');
      });
    },

    setPhotoMode: function (e) {
      if (!this._requireConnected()) return;
      const mode = e.currentTarget.dataset.mode;
      const map = { manual: 0, auto: 1, wifi: 2 };
      const value = map[mode];
      if (value === undefined) return;
      if (mode === 'wifi' && !this.data.wifiEnable) {
        toast.show('请先启用 WiFi', 'warn');
        return;
      }
      const previous = this.data.photoMode;
      store.patch({ photoMode: value });
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_MODE, value).catch(function () {
        store.patch({ photoMode: previous });
        toast.show('设置失败', 'error');
      });
    },

    _requireConnected: function () {
      if (!this.data.connected) {
        toast.show('请先连接设备', 'warn');
        return null;
      }
      return store.get();
    },

    onSleepToggle: function () {
      if (!this._requireConnected()) return;
      const next = this.data.sleepOnOff ? 0 : 1;
      store.patch({ sleepOnOff: next });
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF, next).catch(function () {
        store.patch({ sleepOnOff: next ? 0 : 1 });
        toast.show('设置失败', 'error');
      });
    },

    onAutoWakeToggle: function () {
      if (!this._requireConnected()) return;
      const next = this.data.autoWake ? 0 : 1;
      store.patch({ autoWake: next });
      ble.send(U.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE, next).catch(function () {
        store.patch({ autoWake: next ? 0 : 1 });
        toast.show('设置失败', 'error');
      });
    },

    onWakeChange: function (e) {
      if (!this._requireConnected()) return;
      const previous = this.data.wakeMinutes;
      const minutes = Math.max(10, Math.min(2880, Math.round(e.detail.value)));
      store.patch({ wakeMinutes: minutes });
      ble.sendPacket(U.buildSleepTimePacket(minutes)).catch(function () {
        store.patch({ wakeMinutes: previous });
        toast.show('设置失败', 'error');
      });
    },

    onSdReset: function () {
      if (!this._requireConnected()) return;
      wx.showModal({
        title: '格式化 SD 卡',
        content: '将删除设备存储卡里的全部文件，确定继续？',
        confirmText: '格式化',
        confirmColor: '#B3261E',
        success: function (res) {
          if (!res.confirm) return;
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_SDRESET).then(function () {
            toast.show('已发送格式化命令', 'ok');
          }).catch(function () {
            toast.show('操作失败', 'error');
          });
        }
      });
    },

    onReboot: function () {
      if (!this._requireConnected()) return;
      wx.showModal({
        title: '重启设备',
        content: '确定重启设备？',
        success: function (res) {
          if (!res.confirm) return;
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_REBOOT).catch(function () {});
        }
      });
    },

    onFactoryReset: function () {
      if (!this._requireConnected()) return;
      wx.showModal({
        title: '恢复出厂设置',
        content: '将清除设备配置，确定继续？',
        confirmText: '恢复',
        confirmColor: '#B3261E',
        success: function (res) {
          if (!res.confirm) return;
          ble.send(U.BLE_FILM_TRANS_CH_CTRL_RESET).then(function () {
            toast.show('已发送恢复命令', 'ok');
          }).catch(function () {});
        }
      });
    },

    onChooseFirmware: function () {
      media.chooseFirmware().then(function (file) {
        this._firmware = file;
        this.setData({
          otaName: file.name || 'firmware.bin',
          otaSize: file.size || 0,
          otaProgress: 0,
          otaStatus: '待升级'
        });
      }.bind(this)).catch(function (err) {
        if (err && err.errMsg && err.errMsg.indexOf('cancel') !== -1) return;
        toast.show('选择文件失败', 'error');
      });
    },

    onOtaStart: function () {
      if (this.data.otaRunning) return;
      if (!this._firmware) {
        toast.show('请先选择固件', 'warn');
        return;
      }
      if (!this.data.connected) {
        toast.show('请先连接设备', 'warn');
        return;
      }
      this.setData({ otaRunning: true, otaProgress: 0, otaStatus: '读取固件' });
      media.readFile(this._firmware.path).then(function (buffer) {
        const bytes = new Uint8Array(buffer);
        this.setData({ otaStatus: '正在升级' });
        return ble.uploadOta(bytes, function (progress) {
          this.setData({ otaProgress: progress, otaStatus: '传输中 ' + progress + '%' });
        }.bind(this));
      }.bind(this)).then(function () {
        this.setData({ otaRunning: false, otaProgress: 100, otaStatus: '升级完成，设备将重启' });
        toast.show('升级完成', 'ok');
      }.bind(this)).catch(function (err) {
        this.setData({
          otaRunning: false,
          otaStatus: (err && err.message) || '升级失败'
        });
        toast.show((err && err.message) || '升级失败', 'error');
      }.bind(this));
    },

    onOtaCancel: function () {
      ble.cancelTransfer();
      this.setData({ otaStatus: '正在取消' });
    },

    toggleDebug: function () {
      const open = !this.data.debugOpen;
      this.setData({ debugOpen: open, logs: open ? debugLog.get().slice(-80) : [] });
    },

    clearDebug: function () {
      debugLog.clear();
      this.setData({ logs: [] });
    },

    copyDebug: function () {
      const text = debugLog.get().map(function (item) {
        return item.text;
      }).join('\n');
      if (!text) {
        toast.show('暂无日志', 'warn');
        return;
      }
      wx.setClipboardData({
        data: text,
        success: function () {
          toast.show('日志已复制', 'ok');
        }
      });
    },

    onAlgoChange: function (e) {
      const value = e.detail.value;
      this.setData({ algoValue: value });
      try {
        wx.setStorageSync(DITHER_KEY, value);
      } catch (err) {}
    }
  }
});
