const store = require('../../../../platform/store');
const media = require('../../../../platform/media');
const toast = require('../../../../platform/toast');

Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  data: {
    connected: false,
    busy: false
  },

  lifetimes: {
    attached: function () {
      this._unsub = store.subscribe(function (state) {
        this.setData({ connected: state.connected });
      }.bind(this));
      this.setData({ connected: store.get().connected });
    },
    detached: function () {
      if (this._unsub) this._unsub();
    }
  },

  methods: {
    _pick: function (sourceType, count) {
      if (this.data.busy) return;
      this.setData({ busy: true });
      media.chooseImages({ sourceType: sourceType, count: count }).then(function (files) {
        this.setData({ busy: false });
        if (!files.length) return;

        if (count > 1) {
          store.patch({
            batchPaths: files.map(function (f) {
              return f.path;
            })
          });
          wx.navigateTo({ url: '/pages/batch/index' });
          return;
        }

        store.patch({
          pendingImage: files[0].path,
          pendingName: files[0].name || ''
        });
        wx.navigateTo({
          url: '/pages/frame/index?tool=' + (sourceType[0] === 'camera' ? 'camera' : 'album')
        });
      }.bind(this)).catch(function (err) {
        this.setData({ busy: false });
        if (err && err.errMsg && err.errMsg.indexOf('cancel') !== -1) return;
        toast.show('选择图片失败', 'error');
      }.bind(this));
    },

    onAlbum: function () {
      this._pick(['album'], 1);
    },

    onCamera: function () {
      this._pick(['camera'], 1);
    },

    onBatch: function () {
      this._pick(['album'], 9);
    },

    onQuote: function () {
      wx.navigateTo({ url: '/pages/quote/index' });
    },

    onCalendar: function () {
      wx.navigateTo({ url: '/pages/calendar/index' });
    }
  }
});
