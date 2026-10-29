import 'dart:async';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:image/image.dart' as img;

enum ImageResampling { highQuality, legacyCanvas }

/// Decode through Flutter to retain platform format/orientation support. The
/// ceiling bounds working memory; it does not enlarge the persisted avatar.
Future<ui.Image> decodeCompositionSource(
  Uint8List bytes, {
  int maxDimension = 4096,
}) async {
  final codec = await ui.instantiateImageCodecWithSize(
    await ui.ImmutableBuffer.fromUint8List(bytes),
    getTargetSize: (w, h) {
      final ratio = math.min(1.0, maxDimension / math.max(w, h));
      return ui.TargetImageSize(
        width: math.max(1, (w * ratio).round()),
        height: math.max(1, (h * ratio).round()),
      );
    },
  );
  try {
    return (await codec.getNextFrame()).image;
  } finally {
    codec.dispose();
  }
}

Future<ui.Image> imageFromRgba(Uint8List rgba, int width, int height) {
  final result = Completer<ui.Image>();
  ui.decodeImageFromPixels(
    rgba,
    width,
    height,
    ui.PixelFormat.rgba8888,
    result.complete,
  );
  return result.future;
}

// Native platforms run this in an isolate. Web compute uses the same function
// on its event loop; checks around the await prevent publishing obsolete work.
Uint8List _averageResize((Uint8List, int, int, int, int) job) {
  final (rgba, sw, sh, width, height) = job;
  // rawRgba is premultiplied. Composite on white BEFORE filtering so invisible
  // RGB cannot darken transparent edges. All pipeline output is opaque.
  for (var i = 0; i < rgba.length; i += 4) {
    final white = 255 - rgba[i + 3];
    for (var c = 0; c < 3; c++) {
      rgba[i + c] = math.min(255, rgba[i + c] + white);
    }
    rgba[i + 3] = 255;
  }
  final source = img.Image.fromBytes(
    width: sw,
    height: sh,
    bytes: rgba.buffer,
    bytesOffset: rgba.offsetInBytes,
    numChannels: 4,
  );
  return img
      .copyResize(
        source,
        width: width,
        height: height,
        interpolation: img.Interpolation.average,
      )
      .getBytes(order: img.ChannelOrder.rgba);
}

/// Explicit box/area averaging for downsampling (not Lanczos). Canvas then
/// handles only the fractional placement and any enlargement. Integer rounding
/// of the intermediate size never changes the original composition geometry.
Future<ui.Image> resizeCompositionSource(
  ui.Image source,
  int width,
  int height, {
  void Function()? checkCancelled,
}) async {
  checkCancelled?.call();
  final rgba = (await source.toByteData(format: ui.ImageByteFormat.rawRgba))!
      .buffer
      .asUint8List();
  checkCancelled?.call();
  final resized = await compute(_averageResize, (
    rgba,
    source.width,
    source.height,
    width,
    height,
  ));
  checkCancelled?.call();
  return imageFromRgba(resized, width, height);
}

Future<ui.Image> renderImageComposition(
  Uint8List bytes, {
  required int width,
  required int height,
  bool cover = true,
  int quarterTurns = 0,
  double zoom = 1,
  double panX = 0,
  double panY = 0,
  ImageResampling resampling = ImageResampling.highQuality,
  ui.FilterQuality legacyQuality = ui.FilterQuality.medium,
  int legacyDecodeLimit = 2048,
  void Function()? checkCancelled,
}) async {
  checkCancelled?.call();
  final source = await decodeCompositionSource(
    bytes,
    maxDimension: resampling == ImageResampling.legacyCanvas
        ? legacyDecodeLimit
        : 4096,
  );
  ui.Image? resized;
  ui.Picture? picture;
  try {
    checkCancelled?.call();
    final turns = quarterTurns % 4;
    final sw = turns.isOdd ? source.height : source.width;
    final sh = turns.isOdd ? source.width : source.height;
    final scale =
        (cover
            ? math.max(width / sw, height / sh)
            : math.min(width / sw, height / sh)) *
        zoom;
    if (resampling == ImageResampling.highQuality && scale < 1) {
      resized = await resizeCompositionSource(
        source,
        math.max(1, (source.width * scale).ceil()),
        math.max(1, (source.height * scale).ceil()),
        checkCancelled: checkCancelled,
      );
    }
    checkCancelled?.call();
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    canvas.drawColor(const ui.Color(0xffffffff), ui.BlendMode.src);
    canvas.translate(width / 2 + panX, height / 2 + panY);
    canvas.rotate(turns * math.pi / 2);
    canvas.scale(scale);
    if (resampling == ImageResampling.legacyCanvas) {
      canvas.drawImage(
        source,
        ui.Offset(-source.width / 2, -source.height / 2),
        ui.Paint()..filterQuality = legacyQuality,
      );
    } else {
      canvas.drawImageRect(
        resized ?? source,
        ui.Rect.fromLTWH(
          0,
          0,
          (resized ?? source).width.toDouble(),
          (resized ?? source).height.toDouble(),
        ),
        ui.Rect.fromLTWH(
          -source.width / 2,
          -source.height / 2,
          source.width.toDouble(),
          source.height.toDouble(),
        ),
        ui.Paint()..filterQuality = ui.FilterQuality.high,
      );
    }
    picture = recorder.endRecording();
    return await picture.toImage(width, height);
  } finally {
    picture?.dispose();
    resized?.dispose();
    source.dispose();
  }
}
