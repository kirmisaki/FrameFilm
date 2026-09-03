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
    wx.navigateTo({ url: '/pkgCreate/frame/upload/index' });
  },

  goToCamera: function () {
    wx.navigateTo({ url: '/pkgCreate/frame/camera/index' });
  },

  goToQuote: function () {
    wx.navigateTo({ url: '/pkgCreate/frame/quote/index' });
  },

  goToDraw: function () {
    wx.navigateTo({ url: '/pkgCreate/frame/draw/index' });
  },

  // 模板：模板中心
  goToTemplate: function () {
    wx.navigateTo({ url: '/pages/template/index' });
  },

  // 批量片单 → 片单页
  goToBatch: function () {
    wx.switchTab({ url: '/pages/filmlist/index' });
  },

  // 高级转换 → Film 工具页
  goToFilm: function () {
    wx.navigateTo({ url: '/pkgCreate/film/index' });
  }
});
