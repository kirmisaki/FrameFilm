const TABS = [
  { index: 0, label: '蓝牙', icon: '/images/icons/bluetooth.svg', activeIcon: '/images/icons/bluetooth-active.png' },
  { index: 1, label: 'Frame', icon: '/images/icons/create.svg', activeIcon: '/images/icons/create-active.svg' },
  { index: 2, label: 'Film', icon: '/images/icons/film.png', activeIcon: '/images/icons/film-active.png' },
  { index: 3, label: '设置', icon: '/images/icons/settings.svg', activeIcon: '/images/icons/settings-active.svg' }
];

Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  properties: {
    active: { type: Number, value: 0 },
    detail: { type: Boolean, value: false }
  },
  data: {
    items: TABS
  },
  methods: {
    onTap: function (e) {
      const index = Number(e.currentTarget.dataset.index);
      const pages = getCurrentPages();
      const current = pages[pages.length - 1];

      let shellIndex = -1;
      for (let i = pages.length - 1; i >= 0; i--) {
        if (pages[i].route === 'pages/shell/index') {
          shellIndex = i;
          break;
        }
      }
      const shell = shellIndex >= 0 ? pages[shellIndex] : null;
      const backToShell = function () {
        shell.setData({ tab: index });
        wx.navigateBack({ delta: Math.max(1, pages.length - 1 - shellIndex) });
      };

      if (index === this.data.active) {
        // 详情页点当前 tab：回到宿主页对应 tab
        if (this.data.detail && shell) backToShell();
        return;
      }

      // 宿主页内
      if (current && current.route === 'pages/shell/index') {
        if (index === 2) {
          wx.navigateTo({ url: '/render/pages/film/index' });
        } else {
          current.setData({ tab: index });
        }
        return;
      }

      // 详情页点 Film：替换为 Film 页，返回时回到宿主页
      if (index === 2) {
        wx.redirectTo({ url: '/render/pages/film/index' });
        return;
      }

      if (shell) {
        backToShell();
        return;
      }

      wx.reLaunch({ url: '/pages/shell/index' });
    }
  }
});
