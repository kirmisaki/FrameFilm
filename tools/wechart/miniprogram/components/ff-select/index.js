Component({
  options: {
    styleIsolation: 'apply-shared'
  },
  properties: {
    label: { type: String, value: '' },
    value: { type: String, value: '' },
    options: { type: Array, value: [] },
    placeholder: { type: String, value: '请选择' },
    disabled: { type: Boolean, value: false }
  },

  data: {
    open: false,
    items: [],
    current: '请选择'
  },

  observers: {
    'value, options, placeholder': function () {
      this._sync();
    }
  },

  lifetimes: {
    attached: function () {
      this._sync();
    }
  },

  methods: {
    _normalize: function () {
      return (this.data.options || []).map(function (item) {
        if (typeof item === 'string') return { label: item, value: item };
        return item;
      });
    },

    _sync: function () {
      const value = this.data.value;
      const items = this._normalize().map(function (item) {
        return {
          label: item.label,
          value: item.value,
          disabled: !!item.disabled,
          selected: String(item.value) === String(value)
        };
      });
      let current = this.data.placeholder;
      items.forEach(function (item) {
        if (item.selected) current = item.label;
      });
      this.setData({ items: items, current: current });
    },

    open: function () {
      if (this.data.disabled) return;
      this.setData({ open: true });
    },

    close: function () {
      this.setData({ open: false });
    },

    noop: function () {},

    choose: function (e) {
      const index = Number(e.currentTarget.dataset.index);
      const item = this.data.items[index];
      if (!item || item.disabled) return;
      this.setData({ open: false, current: item.label });
      this.triggerEvent('change', { value: item.value, index: index });
    }
  }
});
