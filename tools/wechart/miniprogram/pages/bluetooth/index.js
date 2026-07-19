const bleUtils = require('../../utils/ble-utils');
const filmUtils = require('../../utils/film-utils');
const app = getApp();

Page({
  data: {
    isScanning: false,
    isConnected: false,
    statusText: '未连接',
    deviceList: [],
    connectedDeviceName: '',
    batteryLevel: 0,
    batteryFillWidth: 0
  },

  _bleListener: null,

  onShow: function () {
    // 设置自定义 tabBar 选中状态
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 0 });
    }
    // 从globalData同步状态
    if (app.globalData.isConnected) {
      this.setData({
        isConnected: true,
        connectedDeviceName: app.globalData.deviceName,
        batteryLevel: app.globalData.batteryLevel,
        batteryFillWidth: app.globalData.batteryLevel,
        statusText: '已连接: ' + app.globalData.deviceName
      });
    }
    // 注册监听器用于UI更新
    var that = this;
    this._bleListener = function (resp) {
      that._onBleData(resp);
    };
    app.registerBleDataListener(this._bleListener);
  },

  onHide: function () {
    if (this._bleListener) {
      app.unregisterBleDataListener(this._bleListener);
      this._bleListener = null;
    }
  },

  onUnload: function () {
    if (this.data.isScanning) {
      this._stopScan();
    }
    if (this._bleListener) {
      app.unregisterBleDataListener(this._bleListener);
      this._bleListener = null;
    }
  },

  // 接收全局BLE数据，更新本页UI
  _onBleData: function (resp) {
    var cmdType = resp.cmdType;
    switch (cmdType) {
      case bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD:
        this.setData({ batteryLevel: app.globalData.batteryLevel, batteryFillWidth: app.globalData.batteryLevel });
        break;
      case bleUtils.BLE_FILM_TRANS_CH_FILE_LIST:
        // 文件列表更新，无需特殊UI处理
        break;
    }
  },

  // 扫描设备
  onScan: function () {
    if (this.data.isScanning || this.data.isConnected) return;
    var that = this;
    that.setData({ deviceList: [], isScanning: true, statusText: '正在初始化蓝牙...' });

    wx.openBluetoothAdapter({
      success: function () {
        that.setData({ statusText: '正在扫描FrameFilm设备...' });
        wx.startBluetoothDevicesDiscovery({
          services: [],
          allowDuplicatesKey: false,
          success: function () {
            wx.onBluetoothDeviceFound(function (res) {
              var devices = res.devices;
              var list = that.data.deviceList;
              for (var i = 0; i < devices.length; i++) {
                var d = devices[i];
                var name = d.name || d.localName || '';
                if (name.indexOf('FRAMEFILM') === -1) continue;
                var exists = false;
                for (var j = 0; j < list.length; j++) {
                  if (list[j].deviceId === d.deviceId) { exists = true; break; }
                }
                if (!exists) {
                  list.push({
                    name: name,
                    deviceId: d.deviceId,
                    RSSI: d.RSSI
                  });
                }
              }
              that.setData({ deviceList: list });
            });
          },
          fail: function (err) {
            console.error('startBluetoothDevicesDiscovery fail', err);
            that.setData({ isScanning: false, statusText: '扫描失败: ' + (err.errMsg || '未知错误') });
          }
        });
      },
      fail: function (err) {
        console.error('openBluetoothAdapter fail', err);
        that.setData({ isScanning: false, statusText: '蓝牙初始化失败，请检查蓝牙是否开启' });
      }
    });

    // 15秒后自动停止扫描
    setTimeout(function () {
      if (that.data.isScanning && !that.data.isConnected) {
        that._stopScan();
      }
    }, 15000);
  },

  _stopScan: function () {
    var that = this;
    wx.stopBluetoothDevicesDiscovery({
      success: function () {},
      fail: function () {}
    });
    that.setData({ isScanning: false });
    if (!that.data.isConnected) {
      that.setData({ statusText: '未连接' });
    }
  },

  // 连接设备
  onConnectDevice: function (e) {
    var that = this;
    var index = e.currentTarget.dataset.index;
    var device = that.data.deviceList[index];
    if (!device) return;

    that._stopScan();
    that.setData({ statusText: '正在连接: ' + (device.name || device.deviceId) + '...' });

    wx.createBLEConnection({
      deviceId: device.deviceId,
      timeout: 10000,
      success: function () {
        that.setData({ statusText: '已连接，正在协商MTU...' });
        // 尝试协商MTU（设备端为200）
        wx.setBLEMTU({
          deviceId: device.deviceId,
          mtu: 200,
          success: function (res) {
            console.log('MTU协商成功:', res.mtu);
          },
          fail: function (err) {
            console.log('MTU协商失败(使用默认值):', err.errMsg);
          },
          complete: function () {
            // 无论MTU协商是否成功，继续获取服务
            that._discoverServices(device);
          }
        });
      },
      fail: function (err) {
        console.error('createBLEConnection fail', err);
        that.setData({ statusText: '连接失败: ' + (err.errMsg || '未知错误') });
      }
    });

    // 监听连接断开
    wx.onBLEConnectionStateChange(function (res) {
      if (!res.connected && app.globalData.deviceId === device.deviceId) {
        that._handleDisconnect();
      }
    });
  },

  _normalizeUuid: function (uuid) {
    return (uuid || '').toLowerCase().replace(/-/g, '');
  },

  _discoverServices: function (device) {
    var that = this;
    setTimeout(function () {
      wx.getBLEDeviceServices({
        deviceId: device.deviceId,
        success: function (res) {
          console.log('发现的服务列表:', JSON.stringify(res.services.map(function(s) { return s.uuid; })));
          var targetServiceId = bleUtils.BLE_SERVICE_UUID;
          var normalizedTarget = that._normalizeUuid(targetServiceId);
          var serviceFound = false;
          var matchedServiceId = '';
          for (var i = 0; i < res.services.length; i++) {
            var normalizedService = that._normalizeUuid(res.services[i].uuid);
            if (normalizedService === normalizedTarget) {
              serviceFound = true;
              matchedServiceId = res.services[i].uuid;
              break;
            }
          }
          if (!serviceFound) {
            console.error('未匹配到目标服务，设备服务列表:', res.services.map(function(s) { return s.uuid; }));
            that.setData({ statusText: '未找到目标服务，请确认设备型号' });
            wx.closeBLEConnection({ deviceId: device.deviceId });
            return;
          }
          that.setData({ statusText: '正在获取特征值...' });
          that._discoverCharacteristics(device, matchedServiceId);
        },
        fail: function (err) {
          console.error('getBLEDeviceServices fail', err);
          that.setData({ statusText: '获取服务失败: ' + (err.errMsg || '') });
        }
      });
    }, 500);
  },

  _discoverCharacteristics: function (device, serviceId) {
    var that = this;
    wx.getBLEDeviceCharacteristics({
      deviceId: device.deviceId,
      serviceId: serviceId,
      success: function (res) {
        console.log('发现的特征值列表:', JSON.stringify(res.characteristics.map(function(c) { return c.uuid; })));
        var targetCharId = bleUtils.BLE_CHARACTERISTIC_UUID;
        var normalizedTarget = that._normalizeUuid(targetCharId);
        var charFound = false;
        var matchedCharId = '';
        for (var i = 0; i < res.characteristics.length; i++) {
          var normalizedChar = that._normalizeUuid(res.characteristics[i].uuid);
          if (normalizedChar === normalizedTarget) {
            charFound = true;
            matchedCharId = res.characteristics[i].uuid;
            break;
          }
        }
        if (!charFound) {
          console.error('未匹配到目标特征值:', res.characteristics.map(function(c) { return c.uuid; }));
          that.setData({ statusText: '未找到目标特征值' });
          wx.closeBLEConnection({ deviceId: device.deviceId });
          return;
        }
        // 检测设备类型
        var deviceType = 'FRAMEFILM';
        if (device.name && device.name.toUpperCase().indexOf('PRO') !== -1) {
          deviceType = 'FRAMEFILMPRO';
        }
        filmUtils.setDeviceType(deviceType);
        var devCfg = filmUtils.getDeviceConfig();

        // 保存连接信息到全局
        app.globalData._deviceId = device.deviceId;
        app.globalData._serviceId = serviceId;
        app.globalData._characteristicId = matchedCharId;
        app.globalData.deviceId = device.deviceId;
        app.globalData.deviceName = device.name;
        app.globalData.deviceType = deviceType;
        app.globalData.isConnected = true;

        that.setData({
          isConnected: true,
          connectedDeviceName: device.name,
          statusText: '已连接: ' + device.name + ' (' + devCfg.displayName + ')'
        });

        // 启用通知
        that._enableNotify(device.deviceId, serviceId, matchedCharId);
      },
      fail: function (err) {
        console.error('getBLEDeviceCharacteristics fail', err);
        that.setData({ statusText: '获取特征值失败' });
      }
    });
  },

  _enableNotify: function (deviceId, serviceId, characteristicId) {
    var that = this;
    wx.notifyBLECharacteristicValueChange({
      deviceId: deviceId,
      serviceId: serviceId,
      characteristicId: characteristicId,
      state: true,
      success: function () {
        console.log('BLE通知已启用');
        // 注册全局BLE数据监听
        app.startBleNotify();
        that.setData({ statusText: '已连接: ' + that.data.connectedDeviceName + '，正在获取设备信息...' });
        // 连接成功后发送初始化命令
        that._sendInitCommands();
      },
      fail: function (err) {
        console.error('notifyBLECharacteristicValueChange fail', err);
        that.setData({ statusText: '启用通知失败' });
      }
    });
  },

  // 发送初始化命令（与原版ForFilm一致的顺序和间隔）
  _sendInitCommands: function () {
    var that = this;
    var step = 500;
    var delay = step;

    // 1. 读取电量
    setTimeout(function () {
      console.log('发送初始化命令: PWRREAD');
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_PWRREAD, null).catch(function (e) {
        console.error('send PWRREAD fail', e);
      });
    }, delay);
    delay += step;

    // 2. 获取文件列表（使用防抖：停止收到新条目后再发送后续命令）
    setTimeout(function () {
      console.log('发送初始化命令: FILE_LIST');
      app.globalData.fileList = [];
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_LIST, null).catch(function (e) {
        console.error('send FILE_LIST fail', e);
      });
      // 设置防抖：监听文件列表计数，1秒无新条目后继续
      that._waitForFileListDone(function () {
        that._sendRemainingInitCommands();
      });
    }, delay);

  },

  // 等待文件列表接收完成（防抖：1秒无新条目视为完成）
  _waitForFileListDone: function (callback) {
    var that = this;
    var lastCount = -1;
    var checkTimer = null;
    var maxWait = 30000; // 最大等待30秒
    var startTime = Date.now();

    function check() {
      var currentCount = (app.globalData.fileList || []).length;
      if (currentCount > 0 && currentCount === lastCount) {
        // 计数未变，文件列表接收完成
        console.log('文件列表接收完成，共 ' + currentCount + ' 个文件');
        callback();
        return;
      }
      if (Date.now() - startTime > maxWait) {
        console.log('文件列表等待超时，已收到 ' + currentCount + ' 个文件');
        callback();
        return;
      }
      lastCount = currentCount;
      checkTimer = setTimeout(check, 1000);
    }
    check();
  },

  // 文件列表接收完成后发送剩余初始化命令
  _sendRemainingInitCommands: function () {
    var that = this;
    var step = 500;
    var delay = step;

    // 3. 获取当前显示文件
    setTimeout(function () {
      console.log('发送初始化命令: FILE_DISPLAY_GET');
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_FILE_DISPLAY_GET, null).catch(function (e) {
        console.error('send FILE_DISPLAY_GET fail', e);
      });
    }, delay);
    delay += step;

    // 4. 获取模式
    setTimeout(function () {
      console.log('发送初始化命令: MODE_GET');
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_MODE_GET, null).catch(function (e) {
        console.error('send MODE_GET fail', e);
      });
    }, delay);
    delay += step;

    // 5. 获取休眠开关
    setTimeout(function () {
      console.log('发送初始化命令: SLEEPONOFF_GET');
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET, null).catch(function (e) {
        console.error('send SLEEPONOFF_GET fail', e);
      });
    }, delay);
    delay += step;

    // 6. 获取休眠模式
    setTimeout(function () {
      console.log('发送初始化命令: SLEEPMODE_GET');
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET, null).catch(function (e) {
        console.error('send SLEEPMODE_GET fail', e);
      });
    }, delay);
    delay += step;

    // 7. 获取休眠时间
    setTimeout(function () {
      console.log('发送初始化命令: SLEEPMODE_TIME_GET');
      app.sendBleCmd(bleUtils.BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET, null).then(function () {
        setTimeout(function () {
          that.setData({ statusText: '已连接: ' + that.data.connectedDeviceName });
        }, 500);
      }).catch(function (e) {
        console.error('send SLEEPMODE_TIME_GET fail', e);
      });
    }, delay);
  },

  // 分享给朋友
  onShareAppMessage: function () {
    return {
      title: 'FrameFilm - 蓝牙',
      path: '/pages/bluetooth/index'
    };
  },

  // 断开连接
  onDisconnect: function () {
    var that = this;
    var deviceId = app.globalData.deviceId;
    if (!deviceId) return;

    wx.closeBLEConnection({
      deviceId: deviceId,
      success: function () {
        that._handleDisconnect();
      },
      fail: function (err) {
        console.error('closeBLEConnection fail', err);
      }
    });
  },

  _handleDisconnect: function () {
    app.stopBleNotify();
    app.globalData.deviceId = '';
    app.globalData.deviceName = '';
    app.globalData.deviceType = 'FRAMEFILM';
    app.globalData.isConnected = false;
    app.globalData.batteryLevel = 0;
    app.globalData.fileList = [];
    app.globalData.currentDisplayFileId = -1;
    app.globalData.photoMode = 0;
    app.globalData.sleepOnOff = 0;
    app.globalData.autoWakeMode = 0;
    app.globalData.wakeDuration = 60;
    app.globalData._deviceId = '';
    app.globalData._serviceId = '';
    app.globalData._characteristicId = '';

    // 重置设备类型
    filmUtils.setDeviceType('FRAMEFILM');

    // 关闭蓝牙适配器，确保下次扫描能正常工作
    wx.closeBluetoothAdapter({
      success: function () { console.log('蓝牙适配器已关闭'); },
      fail: function () {}
    });

    this.setData({
      isConnected: false,
      connectedDeviceName: '',
      batteryLevel: 0,
      batteryFillWidth: 0,
      deviceList: [],
      statusText: '未连接'
    });
  }
});
