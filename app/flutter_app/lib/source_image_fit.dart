import 'dart:math' as math;
import 'dart:typed_data';

/// Source-only nominal sRGB processing. Camera-derived profiles are approximate,
/// not spectral calibration. Warm masking is not face detection.
class SourceFitProfile {
  const SourceFitProfile.nominal(this.blackY, this.whiteY)
    : black = null,
      white = null;
  const SourceFitProfile.measured(this.black, this.white)
    : blackY = null,
      whiteY = null;
  final double? blackY, whiteY;
  final List<int>? black, white;
}

// Same-panel camera-relative references, shared white normalized to one.
// Native/Fast: manual1/gain48 acquisition; earlier underexposed data discarded.
// Legacy Qual uses original A/A/C control black3/white15, not ABC minimum.
// Low-light capture includes channel clipping: these are provisional relative
// references, not calibrated colorimetry. User strengths stay zero by default.
const nativeSixSourceProfile = SourceFitProfile.nominal(.08143727637491299, 1);
const fastSourceProfile = SourceFitProfile.nominal(.1034168376244516, 1);
const legacyQualSourceProfile = SourceFitProfile.nominal(.09957615092384647, 1);

const _yw = [.2126, .7152, .0722];
double _decode(num value) {
  final x = value / 255;
  return x <= .04045 ? x / 12.92 : math.pow((x + .055) / 1.055, 2.4).toDouble();
}

double _lightness(double y) => y > math.pow(6 / 29, 3)
    ? (116 * math.pow(y, 1 / 3) - 16).toDouble()
    : (math.pow(29 / 3, 3) * y).toDouble();
double _inverse(double l) {
  final f = (l + 16) / 116;
  return f > 6 / 29
      ? f * f * f
      : (3 * math.pow(6 / 29, 2) * (f - 4 / 29)).toDouble();
}

double _smooth(double low, double high, double x) {
  final t = ((x - low) / (high - low)).clamp(0.0, 1.0);
  return t * t * (3 - 2 * t);
}

double _y(List<double> rgb) =>
    rgb[0] * _yw[0] + rgb[1] * _yw[1] + rgb[2] * _yw[2];

void fitSourceRgba(
  Uint8List rgba, {
  double rangeFit = 0,
  double warmChroma = 0,
  SourceFitProfile? profile,
}) {
  // Preserve the established byte path, including alpha, when disabled.
  if (rangeFit == 0 && warmChroma == 0) return;
  if (!rangeFit.isFinite ||
      !warmChroma.isFinite ||
      rangeFit < 0 ||
      rangeFit > 1 ||
      warmChroma < 0 ||
      warmChroma > 1 ||
      rgba.length % 4 != 0) {
    throw ArgumentError('无效图片拟合参数');
  }
  if (rangeFit > 0 && profile == null) throw StateError('此模式的屏幕亮度端点尚未测量');
  final measured = profile?.black != null;
  final b = measured
      ? profile!.black!.map(_decode).toList()
      : <double>[0, 0, 0];
  final w = measured
      ? profile!.white!.map(_decode).toList()
      : <double>[1, 1, 1];
  final yb = measured ? _y(b) : profile?.blackY ?? 0;
  final yw = measured ? _y(w) : profile?.whiteY ?? 1;
  if (!(0 <= yb && yb < yw && yw <= 1)) throw ArgumentError('无效屏幕亮度端点');
  for (var i = 0; i < rgba.length; i += 4) {
    final rgb = [_decode(rgba[i]), _decode(rgba[i + 1]), _decode(rgba[i + 2])];
    final y = _y(rgb);
    final yf = _inverse(
      _lightness(yb) + (_lightness(yw) - _lightness(yb)) * _lightness(y) / 100,
    );
    final q = ((yf - yb) / (yw - yb)).clamp(0.0, 1.0);
    final nf = List<double>.generate(
      3,
      (c) => measured ? (1 - q) * b[c] + q * w[c] : q,
    );
    final d = List<double>.generate(
      3,
      (c) => (rgb[c] - y) * (measured ? yw - yb : 1),
    );
    var k = 1.0;
    for (var c = 0; c < 3; c++) {
      if (d[c] > 0) k = math.min(k, (1 - nf[c]) / d[c]);
      if (d[c] < 0) k = math.min(k, nf[c] / -d[c]);
    }
    final mask =
        _smooth(
          .015,
          .10,
          math.min(rgb[0] - rgb[2], (rgb[0] + rgb[1]) / 2 - rgb[2]),
        ) *
        _smooth(.02, .12, rgb.reduce(math.max) - rgb.reduce(math.min)) *
        _smooth(.10, .45, y);
    for (var c = 0; c < 3; c++) {
      final f = nf[c] + k * d[c];
      final t = (1 - rangeFit) * rgb[c] + rangeFit * f;
      final n = (1 - rangeFit) * y + rangeFit * nf[c];
      final out = (n + (t - n) * (1 - warmChroma * mask)).clamp(0.0, 1.0);
      final encoded = out <= .0031308
          ? 12.92 * out
          : 1.055 * math.pow(out, 1 / 2.4) - .055;
      rgba[i + c] = (255 * encoded + .5).floor().clamp(0, 255);
    }
  }
}
