Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  properties: {
    label: { type: String, value: '' },
    value: { type: Number, value: 0 },
    min: { type: Number, value: 0 },
    max: { type: Number, value: 100 },
    step: { type: Number, value: 1 },
    decimals: { type: Number, value: 0 },
    unit: { type: String, value: '' },
    disabled: { type: Boolean, value: false },
    showValue: { type: Boolean, value: true }
  },

  data: {
    percent: 0,
    display: '0'
  },

  observers: {
    'value, min, max, decimals': function (value, min, max, decimals) {
      this._sync(value, min, max, decimals);
    }
  },

  lifetimes: {
    attached: function () {
      this._sync(this.data.value, this.data.min, this.data.max, this.data.decimals);
    }
  },

  methods: {
    _sync: function (value, min, max, decimals) {
      const safeMax = max <= min ? min + 1 : max;
      const ratio = Math.max(0, Math.min(1, (value - min) / (safeMax - min)));
      this.setData({
        percent: Number((ratio * 100).toFixed(2)),
        display: Number(value || 0).toFixed(decimals || 0)
      });
    },

    _measure: function (cb) {
      this.createSelectorQuery()
        .select('.ffslider__track')
        .boundingClientRect(function (rect) {
          this._rect = rect;
          if (cb) cb();
        }.bind(this))
        .exec();
    },

    _valueAt: function (clientX) {
      const rect = this._rect;
      if (!rect || !rect.width) return this.data.value;
      const ratio = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
      const raw = this.data.min + ratio * (this.data.max - this.data.min);
      const step = this.data.step || 1;
      return Math.round(raw / step) * step;
    },

    _apply: function (value, phase) {
      const clamped = Math.max(this.data.min, Math.min(this.data.max, value));
      const ratio = this.data.max <= this.data.min ? 0 : (clamped - this.data.min) / (this.data.max - this.data.min);
      this.setData({
        percent: Number((ratio * 100).toFixed(2)),
        display: clamped.toFixed(this.data.decimals || 0)
      });
      this.triggerEvent(phase, { value: clamped });
    },

    onStart: function (e) {
      if (this.data.disabled) return;
      this._dragging = true;
      this._measure(function () {
        this._apply(this._valueAt(e.touches[0].clientX), 'changing');
      }.bind(this));
    },

    onMove: function (e) {
      if (!this._dragging || this.data.disabled) return;
      if (!this._rect) {
        this._measure();
        return;
      }
      this._apply(this._valueAt(e.touches[0].clientX), 'changing');
    },

    onEnd: function (e) {
      if (!this._dragging) return;
      this._dragging = false;
      const touch = (e.changedTouches && e.changedTouches[0]) || null;
      if (touch) {
        this._apply(this._valueAt(touch.clientX), 'change');
      }
    }
  }
});
