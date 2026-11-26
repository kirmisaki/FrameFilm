const toast = require('../../platform/toast');

Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  data: {
    visible: false,
    message: '',
    type: 'info'
  },

  lifetimes: {
    attached: function () {
      this._unsub = toast.subscribe(function (payload) {
        this.setData({
          visible: !!payload.visible,
          message: payload.message || '',
          type: payload.type || 'info'
        });
      }.bind(this));
    },
    detached: function () {
      if (this._unsub) this._unsub();
    }
  }
});
