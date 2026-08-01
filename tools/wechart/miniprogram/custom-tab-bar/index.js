Component({
  data: {
    selected: 0,
    list: [
      {
        pagePath: "/pages/home/index",
        text: "首页",
        iconPath: "/images/icons/home.svg",
        selectedIconPath: "/images/icons/home-active.svg"
      },
      {
        pagePath: "/pages/frame/index",
        text: "创作",
        iconPath: "/images/icons/create.svg",
        selectedIconPath: "/images/icons/create-active.svg"
      },
      {
        pagePath: "/pages/filmlist/index",
        text: "片单",
        iconPath: "/images/icons/list.svg",
        selectedIconPath: "/images/icons/list-active.svg"
      },
      {
        pagePath: "/pages/settings/index",
        text: "设置",
        iconPath: "/images/icons/settings.svg",
        selectedIconPath: "/images/icons/settings-active.svg"
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
