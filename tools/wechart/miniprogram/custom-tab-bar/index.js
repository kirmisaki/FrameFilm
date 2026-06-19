Component({
  data: {
    selected: 0,
    list: [
      {
        pagePath: "/pages/bluetooth/index",
        text: "蓝牙",
        iconPath: "/images/icons/bluetooth.png",
        selectedIconPath: "/images/icons/bluetooth-active.png"
      },
      {
        pagePath: "/pages/frame/index",
        text: "Frame",
        iconPath: "/images/icons/frame.png",
        selectedIconPath: "/images/icons/frame-active.png"
      },
      {
        pagePath: "/pages/film/index",
        text: "Film",
        iconPath: "/images/icons/film.png",
        selectedIconPath: "/images/icons/film-active.png"
      },
      {
        pagePath: "/pages/settings/index",
        text: "设置",
        iconPath: "/images/icons/setting.png",
        selectedIconPath: "/images/icons/setting-active.png"
      }
    ]
  },

  methods: {
    switchTab: function (e) {
      var index = e.currentTarget.dataset.index;
      var url = e.currentTarget.dataset.url;
      wx.switchTab({ url: url });
    }
  }
});
