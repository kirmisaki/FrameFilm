import 'dart:typed_data';

import 'frame_codec.dart';

int animationPixelColor(Uint8List rgba, int width, int height, int x, int y) {
  if (width <= 0 ||
      height <= 0 ||
      rgba.length != width * height * 4 ||
      x < 0 ||
      y < 0 ||
      x >= width ||
      y >= height) {
    throw ArgumentError('绘图坐标超出画布');
  }
  final p = (y * width + x) * 4;
  return (rgba[p + 3] << 24) |
      (rgba[p] << 16) |
      (rgba[p + 1] << 8) |
      rgba[p + 2];
}

Future<Uint8List> floodAnimationPixels(
  Uint8List rgba,
  int width,
  int height,
  int x,
  int y,
  int argb, {
  bool Function()? isCancelled,
}) async {
  void check() {
    if (isCancelled?.call() ?? false) throw const FrameCancelledException();
  }

  check();
  final target = animationPixelColor(rgba, width, height, x, y);
  final output = Uint8List.fromList(rgba);
  if (target == argb) return output;
  final pending = <int>[y * width + x];
  var processed = 0;
  while (pending.isNotEmpty) {
    final pixel = pending.removeLast(), px = pixel % width, py = pixel ~/ width;
    if (animationPixelColor(output, width, height, px, py) != target) continue;
    final offset = pixel * 4;
    output[offset] = (argb >> 16) & 255;
    output[offset + 1] = (argb >> 8) & 255;
    output[offset + 2] = argb & 255;
    output[offset + 3] = (argb >> 24) & 255;
    if (px > 0) pending.add(pixel - 1);
    if (px + 1 < width) pending.add(pixel + 1);
    if (py > 0) pending.add(pixel - width);
    if (py + 1 < height) pending.add(pixel + width);
    if (++processed % 4096 == 0) {
      check();
      await Future<void>.delayed(Duration.zero);
    }
  }
  check();
  return output;
}
