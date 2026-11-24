const store = require('../../platform/store');

Page({
  data: {
    tab: 0,
    titles: ['设备连接', 'Frame', 'Film', '设置'],
    statusBarHeight: 24,
    connected: false,
    name: '',
    battery: 0
  },

  onLoad: function () {
    let info = {};
    try {
      info = typeof wx.getWindowInfo === 'function' ? wx.getWindowInfo() : wx.getSystemInfoSync();
    } catch (e) {}
    this.setData({ statusBarHeight: info.statusBarHeight || 24 });

    const sync = function (state) {
      this.setData({
        connected: state.connected,
        name: state.name || '',
        battery: state.battery || 0
      });
    }.bind(this);
    sync(store.get());
    this._unsub = store.subscribe(sync);
  },

  onUnload: function () {
    if (this._unsub) this._unsub();
  },

  onShareAppMessage: function () {
    return { title: 'FrameFilm', path: '/pages/shell/index' };
  }
});
