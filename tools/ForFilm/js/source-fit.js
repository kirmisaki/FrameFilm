/* SPDX-License-Identifier: GPL-3.0-or-later */
// Shared source-only adjustment. Loading this module never initializes a page.
(function(root) {
    'use strict';
    // Same-screen camera-relative references, not colorimetric measurements.
    // Native/Fast reference: manual exposure 1, gain 48. Defaults remain off.
    var profiles = {
        native6: {yb: .08143727637491299, yw: 1},
        fast: {yb: .1034168376244516, yw: 1},
        legacyQual: {yb: .09957615092384647, yw: 1}
    };
    function decode(v) { v /= 255; return v <= .04045 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4); }
    function encode(v) { v = Math.max(0, Math.min(1, v)); return Math.floor(255 * (v <= .0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - .055) + .5); }
    function light(y) { return y > Math.pow(6 / 29, 3) ? 116 * Math.cbrt(y) - 16 : Math.pow(29 / 3, 3) * y; }
    function inverse(l) { return l > 8 ? Math.pow((l + 16) / 116, 3) : l / Math.pow(29 / 3, 3); }
    function smooth(a, b, x) { var t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); }
    function luminance(c) { return .2126 * c[0] + .7152 * c[1] + .0722 * c[2]; }
    function measured(black, white) { return {black: black.map(decode), white: white.map(decode)}; }
    function apply(image, rangeFit, warmChroma, profile) {
        var r = Math.max(0, Math.min(1, rangeFit || 0)), w = Math.max(0, Math.min(1, warmChroma || 0));
        if ((!r && !w) || !profile) return image;
        var measuredMode = !!profile.black;
        var yb = measuredMode ? luminance(profile.black) : profile.yb;
        var yw = measuredMode ? luminance(profile.white) : profile.yw;
        if (!(yw > yb)) return image;
        var data = image.data;
        for (var i = 0; i < data.length; i += 4) {
            var rgb = [decode(data[i]), decode(data[i + 1]), decode(data[i + 2])], y = luminance(rgb);
            var yf = inverse(light(yb) + (light(yw) - light(yb)) * light(y) / 100);
            var q = Math.max(0, Math.min(1, (yf - yb) / (yw - yb)));
            var nf = measuredMode ? profile.black.map(function(b, j) { return (1 - q) * b + q * profile.white[j]; }) : [q, q, q];
            var d = rgb.map(function(c) { return (c - y) * (measuredMode ? yw - yb : 1); });
            var k = 1;
            for (var j = 0; j < 3; j++) k = Math.min(k, d[j] > 0 ? (1 - nf[j]) / d[j] : d[j] < 0 ? nf[j] / -d[j] : Infinity);
            var mask = smooth(.015, .10, Math.min(rgb[0] - rgb[2], (rgb[0] + rgb[1]) / 2 - rgb[2])) * smooth(.02, .12, Math.max.apply(null, rgb) - Math.min.apply(null, rgb)) * smooth(.10, .45, y);
            for (var j = 0; j < 3; j++) {
                var t = (1 - r) * rgb[j] + r * (nf[j] + k * d[j]);
                var n = (1 - r) * y + r * nf[j];
                data[i + j] = encode(n + (t - n) * (1 - w * mask));
            }
        }
        return image;
    }
    root.FrameFilmSourceFit = {apply: apply, profiles: profiles, measured: measured};
})(typeof window !== 'undefined' ? window : globalThis);
