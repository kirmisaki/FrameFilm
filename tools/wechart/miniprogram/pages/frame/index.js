// frame/index.js - 功能入口页面
Page({
  data: {},

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 1 });
    }
  },

  goToUpload: function () {
    wx.navigateTo({ url: '/pages/frame/upload/index' });
  },

  goToCamera: function () {
    wx.navigateTo({ url: '/pages/frame/camera/index' });
  },

  goToQuote: function () {
    wx.navigateTo({ url: '/pages/frame/quote/index' });
  },

  goToDraw: function () {
    wx.navigateTo({ url: '/pages/frame/draw/index' });
  },
});
