import 'dart:math' as math;
import 'dart:typed_data';

import 'frame_abc512_data.dart';

// Exact 3D nearest-neighbor search, with stable first-entry ties.
class _Tree {
  _Tree(this.colors) {
    root = _build(List.generate(colors.length, (i) => i), 0);
  }
  final List<List<double>> colors;
  late final _Node? root;
  _Node? _build(List<int> ids, int depth) {
    if (ids.isEmpty) return null;
    final axis = depth % 3;
    ids.sort((a, b) => colors[a][axis].compareTo(colors[b][axis]));
    final mid = ids.length ~/ 2;
    return _Node(
      ids[mid],
      axis,
      _build(ids.sublist(0, mid), depth + 1),
      _build(ids.sublist(mid + 1), depth + 1),
    );
  }

  int nearest(List<double> v) {
    var best = 0;
    var distance = double.infinity;
    void visit(_Node? node) {
      if (node == null) return;
      final p = colors[node.id];
      final dr = v[0] - p[0], dg = v[1] - p[1], db = v[2] - p[2];
      final d = dr * dr + dg * dg + db * db;
      if (d < distance || (d == distance && node.id < best)) {
        best = node.id;
        distance = d;
      }
      final delta = v[node.axis] - p[node.axis];
      visit(delta < 0 ? node.left : node.right);
      if (delta * delta <= distance) visit(delta < 0 ? node.right : node.left);
    }

    visit(root);
    return best;
  }
}

class _Node {
  const _Node(this.id, this.axis, this.left, this.right);
  final int id, axis;
  final _Node? left, right;
}

final _recipes = _Tree(abc512RecipeMeans);
final _outputs = _Tree(abc512OutputColors);
final _lookup = <int, List<double>>{
  for (var i = 0; i < abc512OutputCodes.length; i++)
    abc512OutputCodes[i]: abc512OutputColors[i],
};
final _linearLut = List<double>.generate(256, (i) {
  final v = i / 255;
  return v <= .04045 ? v / 12.92 : math.pow((v + .055) / 1.055, 2.4).toDouble();
});
double _y(List<double> v) => v[0] * .2126 + v[1] * .7152 + v[2] * .0722;
double _smooth(double low, double high, double v) {
  final t = ((v - low) / (high - low)).clamp(0.0, 1.0);
  return t * t * (3 - 2 * t);
}

double _lightness(double y) => y > math.pow(6 / 29, 3)
    ? (116 * math.pow(y, 1 / 3) - 16).toDouble()
    : (math.pow(29 / 3, 3) * y).toDouble();
double _inverseLightness(double l) {
  final f = (l + 16) / 116;
  return f > 6 / 29
      ? f * f * f
      : (3 * math.pow(6 / 29, 2) * (f - 4 / 29)).toDouble();
}

int _srgb(double linear) {
  final v = linear.clamp(0.0, 1.0);
  final s = v <= .0031308 ? 12.92 * v : 1.055 * math.pow(v, 1 / 2.4) - .055;
  final x = (s * 255).clamp(0.0, 255.0), n = x.floor();
  return x - n > .5 || (x - n == .5 && n.isOdd) ? n + 1 : n;
}

/// Camera-relative simulation, from exactly the exported ABC tuple codes.
void paintAbc512Codes(Uint8List rgba, Uint16List codes) {
  if (rgba.length != codes.length * 4) throw ArgumentError('像素尺寸不匹配');
  for (var i = 0; i < codes.length; i++) {
    final color = _lookup[codes[i]];
    if (color == null) throw ArgumentError('不在 I006 冻结色板中的 ABC9 代码');
    for (var c = 0; c < 3; c++) {
      rgba[i * 4 + c] = _srgb(color[c]);
    }
    rgba[i * 4 + 3] = 255;
  }
}

/// Defaults preserve frozen I006: floor, .25 warm contraction, radial hull, recipe targets,
/// 423 actual tuple FS, then source-derived warm-shadow masked scalar FS.
Future<Uint16List> quantizeAbc512(
  Uint8List rgba,
  int width,
  int height, {
  bool continuousTone = false,
  double warmChroma = .25,
  double rangeFit = 0,
  double ditherStrength = 1,
  void Function()? checkCancelled,
  void Function(double)? onProgress,
}) async {
  if (width <= 0 || height <= 0 || rgba.length != width * height * 4) {
    throw ArgumentError('像素尺寸不匹配');
  }
  if (!warmChroma.isFinite ||
      !rangeFit.isFinite ||
      !ditherStrength.isFinite ||
      warmChroma < 0 ||
      warmChroma > 1 ||
      rangeFit < 0 ||
      rangeFit > 1 ||
      ditherStrength < 0 ||
      ditherStrength > 5) {
    throw ArgumentError('无效 I006 调整参数');
  }
  checkCancelled?.call();
  final blackL = _lightness(abc512Floor);
  final size = width * height,
      commonY = Float64List(width * height),
      mask = Uint8List(width * height);
  final work = Float64List(size * 3), result = Uint16List(size);
  Future<void> row(int y, double start, double span) async {
    checkCancelled?.call();
    onProgress?.call(start + span * (y + 1) / height);
    if (y % 4 == 3 || y == height - 1) {
      await Future<void>.delayed(Duration.zero);
      checkCancelled?.call();
    }
  }

  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final i = y * width + x;
      var v = List<double>.generate(
        3,
        (c) => abc512Floor + (1 - abc512Floor) * _linearLut[rgba[i * 4 + c]],
      );
      // Match epd_source_fit.fit_source: blend the scalar-floor baseline with
      // fixed display-L* fitting before warm contraction, without byte roundtrips.
      if (rangeFit > 0) {
        final rgb = List<double>.generate(
          3,
          (c) => _linearLut[rgba[i * 4 + c]],
        );
        final sourceY = _y(rgb);
        final mappedY = _inverseLightness(
          blackL + (100 - blackL) * _lightness(sourceY) / 100,
        );
        final chroma = rgb
            .map((c) => (c - sourceY) * (1 - abc512Floor))
            .toList();
        var k = 1.0;
        for (final c in chroma) {
          if (c.abs() > 1e-15) {
            k = math.min(k, (c >= 0 ? 1 - mappedY : mappedY) / c.abs());
          }
        }
        for (var c = 0; c < 3; c++) {
          v[c] = v[c] * (1 - rangeFit) + (mappedY + chroma[c] * k) * rangeFit;
        }
      }
      final luminance = _y(v),
          excess = math.min(v[0] - v[2], (v[0] + v[1]) / 2 - v[2]);
      final spread = v.reduce(math.max) - v.reduce(math.min);
      final gate =
          _smooth(.015, .10, excess) *
          _smooth(.02, .12, spread) *
          _smooth(.10, .45, luminance);
      if (gate > 0 && warmChroma > 0) {
        v = v
            .map((c) => luminance + (c - luminance) * (1 - warmChroma * gate))
            .toList();
      }
      final sy = _y(v),
          warm = math.min(v[0] - v[2], (v[0] + v[1]) / 2 - v[2]),
          s = v.reduce(math.max) - v.reduce(math.min);
      mask[i] =
          sy >= .035 &&
              sy <= .14 &&
              warm >= .015 &&
              s >= .02 &&
              s <= .12 &&
              v[0] >= v[1] &&
              v[1] >= v[2] &&
              v[0] - v[1] <= .4 * sy
          ? 1
          : 0;
      final dir = List<double>.generate(3, (c) => v[c] - abc512Center[c]);
      var scale = 1.0;
      for (final p in abc512HullPlanes) {
        final den = p[0] * dir[0] + p[1] * dir[1] + p[2] * dir[2];
        if (den > 0) {
          scale = math.min(
            scale,
            -(p[0] * abc512Center[0] +
                    p[1] * abc512Center[1] +
                    p[2] * abc512Center[2] +
                    p[3]) /
                den,
          );
        }
      }
      if (scale < 1) scale *= 1 - 1e-10;
      final common = List<double>.generate(
        3,
        (c) => abc512Center[c] + dir[c] * scale,
      );
      commonY[i] = _y(common);
      // Opt in to continuous targets; the frozen default retains recipe pre-quantization.
      final target = continuousTone
          ? common
          : abc512RecipeMeans[_recipes.nearest(common)];
      for (var c = 0; c < 3; c++) {
        work[i * 3 + c] = target[c];
      }
    }
    await row(y, 0, .35);
  }
  const taps = [(1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1)];
  for (var y = 0; y < height; y++) {
    final direction = y.isEven ? 1 : -1;
    for (var k = 0; k < width; k++) {
      final x = direction == 1 ? k : width - 1 - k, i = y * width + x;
      final value = List<double>.generate(
        3,
        (c) => work[i * 3 + c].clamp(0.0, 1.0),
      );
      final chosen = _outputs.nearest(value),
          color = abc512OutputColors[chosen];
      final code = abc512OutputCodes[chosen];
      // Frozen SciPy representative for equal measured colors. Keep the exact
      // selected color for FS error; only the physical ABC tuple is aliased.
      result[i] = abc512CodeAliases[code] ?? code;
      for (final tap in taps) {
        final xx = x + tap.$1 * direction, yy = y + tap.$2;
        if (xx < 0 || xx >= width || yy >= height) continue;
        for (var c = 0; c < 3; c++) {
          work[(yy * width + xx) * 3 + c] +=
              (value[c] - color[c]).clamp(-1.0, 1.0) *
              (tap.$3 / 16) *
              ditherStrength;
        }
      }
    }
    await row(y, .35, .4);
  }
  const localCodes = [387, 6, 407];
  final ys = localCodes.map((c) => _y(_lookup[c]!)).toList();
  for (var y = 0; y < height; y++) {
    final direction = y.isEven ? 1 : -1;
    for (var k = 0; k < width; k++) {
      final x = direction == 1 ? k : width - 1 - k, i = y * width + x;
      if (mask[i] == 0) continue;
      final value = commonY[i].clamp(0.0, 1.0);
      var best = 0;
      for (var c = 1; c < 3; c++) {
        if ((ys[c] - value).abs() < (ys[best] - value).abs()) best = c;
      }
      result[i] = localCodes[best];
      final error = (value - ys[best]).clamp(-1.0, 1.0);
      for (final tap in taps) {
        final xx = x + tap.$1 * direction, yy = y + tap.$2;
        if (xx >= 0 &&
            xx < width &&
            yy < height &&
            mask[yy * width + xx] != 0) {
          commonY[yy * width + xx] += error * (tap.$3 / 16) * ditherStrength;
        }
      }
    }
    await row(y, .75, .15);
  }
  paintAbc512Codes(rgba, result);
  checkCancelled?.call();
  return result;
}
