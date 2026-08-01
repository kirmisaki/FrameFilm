// frame/index.js - 创作页
Page({
  data: {},

  onShow: function () {
    if (typeof this.getTabBar === 'function' && this.getTabBar()) {
      this.getTabBar().setData({ selected: 1 });
    }
  },

  // 分享给朋友
  onShareAppMessage: function () {
    return {
      title: 'FrameFilm - 创作',
      path: '/pages/frame/index'
    };
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

  // 模板：占位功能
  goToTemplate: function () {
    wx.showToast({ title: '模板即将上线', icon: 'none' });
  },

  // 批量片单 → 片单页
  goToBatch: function () {
    wx.switchTab({ url: '/pages/filmlist/index' });
  },

  // 高级转换 → Film 工具页
  goToFilm: function () {
    wx.navigateTo({ url: '/pages/film/index' });
  }
});
