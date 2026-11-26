// FrameFilm 小程序入口：只做全局初始化，状态与连接由 platform 层模块持有
const ble = require('./platform/ble');

App({
  globalData: {},

  onLaunch: function () {
    ble.init();
  },

  onShow: function () {},

  onHide: function () {}
});
