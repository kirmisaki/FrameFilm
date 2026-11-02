import 'dart:typed_data';
import 'dart:ui' as ui;

import 'frame_codec.dart';

/// Decodes all GIF frames as PNGs, and a single frame for static images.
/// Partial imports are returned only on success, so retry cannot duplicate frames.
Future<List<Uint8List>> decodeAnimationImages(
  Uint8List source, {
  int limit = 48,
  bool Function()? isCancelled,
}) async {
  void check() {
    if (isCancelled?.call() ?? false) throw const FrameCancelledException();
  }

  check();
  if (limit <= 0) return [];
  final codec = await ui.instantiateImageCodec(source, targetWidth: 480);
  final frames = <Uint8List>[];
  final count = codec.frameCount < limit ? codec.frameCount : limit;
  final sampled = {
    for (var i = 0; i < count; i++) (i * codec.frameCount / count).floor(),
  };
  try {
    for (var i = 0; i < codec.frameCount; i++) {
      check();
      final frame = await codec.getNextFrame();
      try {
        if (!sampled.contains(i)) continue;
        final png = await frame.image.toByteData(
          format: ui.ImageByteFormat.png,
        );
        check();
        frames.add(png!.buffer.asUint8List());
      } finally {
        frame.image.dispose();
      }
      await Future<void>.delayed(Duration.zero);
    }
    check();
    return frames;
  } finally {
    codec.dispose();
  }
}
