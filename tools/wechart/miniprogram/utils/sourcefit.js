// 源图适配：明暗范围拟合 + 暖色收缩（移植自 tools/ForFilm/js/source-fit.js）
const profiles = {
  native6: { yb: 0.08143727637491299, yw: 1 },
  fast: { yb: 0.1034168376244516, yw: 1 },
  legacyQual: { yb: 0.09957615092384647, yw: 1 }
};

function decode(v) {
  v /= 255;
  return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}

function encode(v) {
  v = Math.max(0, Math.min(1, v));
  return Math.floor(255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055) + 0.5);
}

function light(y) {
  return y > Math.pow(6 / 29, 3) ? 116 * Math.cbrt(y) - 16 : Math.pow(29 / 3, 3) * y;
}

function inverse(l) {
  return l > 8 ? Math.pow((l + 16) / 116, 3) : l / Math.pow(29 / 3, 3);
}

function smooth(a, b, x) {
  const t = Math.max(0, Math.min(1, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
}

function luminance(c) {
  return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

function measured(black, white) {
  return { black: black.map(decode), white: white.map(decode) };
}

function apply(image, rangeFit, warmChroma, profile) {
  const r = Math.max(0, Math.min(1, rangeFit || 0));
  const w = Math.max(0, Math.min(1, warmChroma || 0));
  if ((!r && !w) || !profile) return image;

  const measuredMode = !!profile.black;
  const yb = measuredMode ? luminance(profile.black) : profile.yb;
  const yw = measuredMode ? luminance(profile.white) : profile.yw;
  if (!(yw > yb)) return image;

  const data = image.data;
  for (let i = 0; i < data.length; i += 4) {
    const rgb = [decode(data[i]), decode(data[i + 1]), decode(data[i + 2])];
    const y = luminance(rgb);
    const yf = inverse(light(yb) + (light(yw) - light(yb)) * light(y) / 100);
    const q = Math.max(0, Math.min(1, (yf - yb) / (yw - yb)));
    const nf = measuredMode
      ? profile.black.map(function (b, j) {
        return (1 - q) * b + q * profile.white[j];
      })
      : [q, q, q];
    const d = rgb.map(function (c) {
      return (c - y) * (measuredMode ? yw - yb : 1);
    });
    let k = 1;
    for (let j = 0; j < 3; j++) {
      k = Math.min(k, d[j] > 0 ? (1 - nf[j]) / d[j] : d[j] < 0 ? nf[j] / -d[j] : Infinity);
    }
    const mask = smooth(0.015, 0.10, Math.min(rgb[0] - rgb[2], (rgb[0] + rgb[1]) / 2 - rgb[2]))
      * smooth(0.02, 0.12, Math.max.apply(null, rgb) - Math.min.apply(null, rgb))
      * smooth(0.10, 0.45, y);
    for (let j = 0; j < 3; j++) {
      const t = (1 - r) * rgb[j] + r * (nf[j] + k * d[j]);
      const n = (1 - r) * y + r * nf[j];
      data[i + j] = encode(n + (t - n) * (1 - w * mask));
    }
  }
  return image;
}

module.exports = { apply, profiles, measured };
