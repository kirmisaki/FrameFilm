import 'frame_abc512.dart';
import 'source_image_fit.dart';
import 'frame_palettes.dart';

import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'frame_algorithms.dart';
import 'frame_lut.dart';
import 'image_resampling.dart';
export 'image_resampling.dart' show ImageResampling;
export 'frame_algorithms.dart' show FrameAlgorithm;

enum FrameFormat { sixColor, monoFast, colorFast55, colorQual, abc512 }

enum FrameFit { cover, contain }

class FrameOptions {
  const FrameOptions({
    this.format = FrameFormat.sixColor,
    this.fit = FrameFit.cover,
    // Editors opt in; keep batch animation and other existing callers unchanged.
    this.resampling = ImageResampling.legacyCanvas,
    this.quarterTurns = 0,
    this.zoom = 1,
    this.panX = 0,
    this.panY = 0,
    this.brightness = 0,
    this.contrast = 1,
    this.saturation = 1,
    this.rangeFit = 0,
    this.warmChroma = 0,
    this.i006WarmChroma = .25,
    this.dither = false,
    this.algorithm = FrameAlgorithm.atkinsonEnhanced,
    this.ditherStrength = 1,
    this.continuousTone = true,
  });
  final FrameFormat format;
  final FrameFit fit;
  final ImageResampling resampling;
  final int quarterTurns;
  final double zoom, panX, panY, brightness, contrast, saturation;
  final double rangeFit, warmChroma;
  final double i006WarmChroma;
  final bool dither;
  final bool continuousTone;
  final FrameAlgorithm algorithm;
  final double ditherStrength;
}

class FrameResult {
  const FrameResult({
    required this.png,
    required this.film,
    required this.format,
    this.cropPng,
    this.algorithm = FrameAlgorithm.atkinsonEnhanced,
    this.width = 480,
    this.height = 720,
  });
  final Uint8List png, film;

  /// Tone-adjusted composition before quantization, from the same resampling.
  final Uint8List? cropPng;
  final int width, height;
  final FrameFormat format;
  final FrameAlgorithm algorithm;
}

class FrameCancelledException implements Exception {
  const FrameCancelledException();
  @override
  String toString() => '转换已取消';
}

void _check(bool Function()? cancelled) {
  if (cancelled?.call() ?? false) throw const FrameCancelledException();
}

Future<Uint8List> quantizeFrameMono(
  Uint8List rgba,
  bool dither,
  bool Function()? isCancelled,
  void Function(double)? onProgress, {
  int width = 480,
  int height = 720,
}) async {
  final indices = Uint8List(width * height);
  var current = Float64List((width + 2) * 3);
  var next = Float64List((width + 2) * 3);
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final pixel = y * width + x, offset = (x + 1) * 3;
      final channels = <double>[
        for (var c = 0; c < 3; c++)
          (rgba[pixel * 4 + c] + current[offset + c]).clamp(0, 255),
      ];
      final black =
          channels[0] * channels[0] +
          channels[1] * channels[1] +
          channels[2] * channels[2];
      final white =
          (channels[0] - 255) * (channels[0] - 255) +
          (channels[1] - 255) * (channels[1] - 255) +
          (channels[2] - 255) * (channels[2] - 255);
      final color = black < white ? 0 : 255;
      indices[pixel] = color == 0 ? 0 : 1;
      for (var c = 0; c < 3; c++) {
        rgba[pixel * 4 + c] = color;
        if (dither) {
          final error = channels[c] - color;
          current[offset + 3 + c] += error * 7 / 16;
          next[offset - 3 + c] += error * 3 / 16;
          next[offset + c] += error * 5 / 16;
          next[offset + 3 + c] += error / 16;
        }
      }
      rgba[pixel * 4 + 3] = 255;
    }
    final old = current;
    current = next;
    next = old;
    next.fillRange(0, next.length, 0);
    _check(isCancelled);
    onProgress?.call(.05 + .85 * (y + 1) / height);
    _check(isCancelled);
    if ((y + 1) % 8 == 0 || y == height - 1) {
      await Future<void>.delayed(Duration.zero);
      _check(isCancelled);
    }
  }
  return indices;
}

/// Portrait logical coordinates map to physical (height - 1 - y, x).
/// The small dimensions are also useful for testing orientation and bit order.
Uint8List packFramePixels(
  List<int> indices, {
  int width = 480,
  int height = 720,
  FrameFormat format = FrameFormat.sixColor,
}) {
  if (width <= 0 ||
      height <= 0 ||
      width > 65535 ||
      height > 65535 ||
      indices.length != width * height) {
    throw ArgumentError('像素尺寸不匹配');
  }
  final abc = format == FrameFormat.abc512;
  final mono = format == FrameFormat.monoFast;
  final indexed =
      format == FrameFormat.colorFast55 || format == FrameFormat.colorQual;
  final bodySize = abc
      ? indices.length * 2
      : indexed
      ? indices.length
      : (indices.length + (mono ? 7 : 1)) ~/ (mono ? 8 : 2);
  final result = Uint8List(32 + bodySize);
  final header = ByteData.sublistView(result);
  header.setUint32(0, bodySize, Endian.little);
  header.setUint16(4, height, Endian.little);
  header.setUint16(6, width, Endian.little);
  result[8] = abc
      ? 0
      : indexed
      ? 0
      : (mono ? 2 : 6);
  result[9] = abc
      ? 0x7f
      : indexed
      ? (format == FrameFormat.colorFast55 ? 3 : 2)
      : (mono ? 1 : 0);
  header.setUint16(10, indexed ? 0 : 1, Endian.little);
  if (abc) result.setRange(12, 16, [65, 66, 67, 57]);
  if (!abc && !mono && !indexed) {
    result.setRange(16, 22, [0, 255, 252, 224, 3, 28]);
  }
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final value = indices[y * width + x];
      if (value < 0 ||
          value >=
              (abc
                  ? 512
                  : indexed
                  ? 64
                  : (mono ? 2 : 6))) {
        throw ArgumentError('无效颜色索引');
      }
      final physical = x * height + height - 1 - y;
      if (abc) {
        header.setUint16(32 + physical * 2, value, Endian.little);
      } else if (indexed) {
        result[32 + physical] = value;
      } else if (mono) {
        if (value == 0) result[32 + physical ~/ 8] |= 128 >> (physical % 8);
      } else {
        result[32 + physical ~/ 2] |= value << (physical.isEven ? 4 : 0);
      }
    }
  }
  return result;
}

Future<ui.Image> _render(
  Uint8List source,
  FrameOptions options,
  bool Function()? cancelled,
) async {
  _check(cancelled);
  if (source.isEmpty || source.length > 8 * 1024 * 1024) {
    throw ArgumentError('图片须大于0且不超过8 MiB');
  }
  final values = [
    options.zoom,
    options.panX,
    options.panY,
    options.brightness,
    options.contrast,
    options.saturation,
    options.ditherStrength,
    options.rangeFit,
    options.warmChroma,
    options.i006WarmChroma,
  ];
  if (values.any((v) => !v.isFinite) || options.zoom <= 0) {
    throw ArgumentError('无效图片调整参数');
  }
  return renderImageComposition(
    source,
    width: 480,
    height: 720,
    cover: options.fit == FrameFit.cover,
    quarterTurns: options.quarterTurns,
    zoom: options.zoom,
    panX: options.panX,
    panY: options.panY,
    resampling: options.resampling,
    checkCancelled: () => _check(cancelled),
  );
}

Future<Uint8List> _png(ui.Image image) async =>
    (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer
        .asUint8List();

void adjustFrameRgba(
  Uint8List rgba,
  FrameOptions options, {
  bool applySourceFit = true,
}) {
  if (options.brightness != 0) {
    for (var i = 0; i < rgba.length; i += 4) {
      for (var c = 0; c < 3; c++) {
        rgba[i + c] = (rgba[i + c] + options.brightness * 255)
            .clamp(0, 255)
            .round();
      }
    }
  }
  adjustFrameColors(
    rgba,
    contrast: options.contrast,
    saturation: options.saturation,
  );
  if (applySourceFit) adjustFrameSourceFit(rgba, options);
}

bool sourceFitIsSz(FrameOptions o) =>
    o.dither &&
    (o.algorithm == FrameAlgorithm.szEnhanced ||
        o.algorithm == FrameAlgorithm.atkinsonSzCalib);
SourceFitProfile? sourceFitProfile(FrameOptions o) {
  if (o.format == FrameFormat.monoFast ||
      o.algorithm == FrameAlgorithm.abc512I006) {
    return null;
  }
  if (o.algorithm == FrameAlgorithm.colorQualMeasured) {
    return SourceFitProfile.measured(
      colorQualMeasuredPalette[3],
      colorQualMeasuredPalette[15],
    );
  }
  if (o.algorithm == FrameAlgorithm.colorQualCalibrated) {
    return SourceFitProfile.measured(
      colorQualCalibratedPalette[3],
      colorQualCalibratedPalette[15],
    );
  }
  if (o.algorithm == FrameAlgorithm.colorQual ||
      o.format == FrameFormat.colorQual) {
    return legacyQualSourceProfile;
  }
  if (o.algorithm == FrameAlgorithm.colorFast55 ||
      o.format == FrameFormat.colorFast55) {
    return fastSourceProfile;
  }
  return nativeSixSourceProfile;
}

void adjustFrameSourceFit(Uint8List rgba, FrameOptions options) {
  if (options.format == FrameFormat.monoFast ||
      options.algorithm == FrameAlgorithm.abc512I006) {
    return;
  }
  fitSourceRgba(
    rgba,
    rangeFit: sourceFitIsSz(options) ? 0 : options.rangeFit,
    warmChroma: options.warmChroma,
    profile: sourceFitProfile(options),
  );
}

/// Shared pre-quantization adjustment for exact composition preview and export.
void adjustFrameCompositionRgba(
  Uint8List rgba,
  FrameOptions options, {
  SourceFitProfile? profile,
}) {
  if (!(options.dither && options.algorithm == FrameAlgorithm.szEnhanced)) {
    adjustFrameRgba(rgba, options, applySourceFit: false);
  }
  if (options.format == FrameFormat.monoFast ||
      options.algorithm == FrameAlgorithm.abc512I006) {
    return;
  }
  fitSourceRgba(
    rgba,
    rangeFit: sourceFitIsSz(options) ? 0 : options.rangeFit,
    warmChroma: options.warmChroma,
    profile: profile ?? sourceFitProfile(options),
  );
}

Future<Uint8List> _encodeRgba(Uint8List rgba) async {
  final completer = Completer<ui.Image>();
  ui.decodeImageFromPixels(
    rgba,
    480,
    720,
    ui.PixelFormat.rgba8888,
    completer.complete,
  );
  final preview = await completer.future;
  try {
    return await _png(preview);
  } finally {
    preview.dispose();
  }
}

Future<Uint8List> prepareFrame(
  Uint8List source,
  FrameOptions options, {
  void Function(double)? onProgress,
  bool Function()? isCancelled,
}) async {
  final image = await _render(source, options, isCancelled);
  try {
    final rgba = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!
        .buffer
        .asUint8List();
    adjustFrameRgba(rgba, options);
    final result = await _encodeRgba(rgba);
    _check(isCancelled);
    onProgress?.call(1);
    return result;
  } finally {
    image.dispose();
  }
}

Future<FrameResult> convertFrame(
  Uint8List source,
  FrameOptions options, {
  bool includeCropPreview = false,
  void Function(double)? onProgress,
  bool Function()? isCancelled,
}) async {
  if (options.format == FrameFormat.abc512 &&
      options.algorithm != FrameAlgorithm.abc512I006) {
    throw ArgumentError('ABC9 仅支持 I006 实验算法');
  }
  final image = await _render(source, options, isCancelled);
  late Uint8List rgba;
  try {
    rgba = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!.buffer
        .asUint8List();
  } finally {
    image.dispose();
  }
  adjustFrameCompositionRgba(rgba, options);
  _check(isCancelled);
  Uint8List? cropPng;
  if (includeCropPreview) {
    final cropRgba = Uint8List.fromList(rgba);
    if (options.dither &&
        options.algorithm == FrameAlgorithm.szEnhanced &&
        options.warmChroma == 0) {
      adjustFrameRgba(cropRgba, options, applySourceFit: false);
    }
    cropPng = await _encodeRgba(cropRgba);
    _check(isCancelled);
  }
  final format =
      options.algorithm == FrameAlgorithm.abc512I006 &&
          options.format != FrameFormat.monoFast
      ? FrameFormat.abc512
      : options.format == FrameFormat.monoFast
      ? FrameFormat.monoFast
      : options.algorithm == FrameAlgorithm.colorFast55
      ? FrameFormat.colorFast55
      : options.algorithm == FrameAlgorithm.colorQual ||
            options.algorithm == FrameAlgorithm.colorQualMeasured ||
            options.algorithm == FrameAlgorithm.colorQualCalibrated
      ? FrameFormat.colorQual
      : options.format;
  final algorithm = format == FrameFormat.colorFast55
      ? FrameAlgorithm.colorFast55
      : format == FrameFormat.colorQual &&
            options.algorithm != FrameAlgorithm.colorQualMeasured &&
            options.algorithm != FrameAlgorithm.colorQualCalibrated
      ? FrameAlgorithm.colorQual
      : options.algorithm;
  List<int> indices;
  if (options.format == FrameFormat.monoFast) {
    // MonoFast is a Flutter extension; retain its existing luminance/FS path.
    indices = await quantizeFrameMono(
      rgba,
      options.dither,
      isCancelled,
      onProgress,
    );
  } else if (algorithm == FrameAlgorithm.abc512I006) {
    indices = await quantizeAbc512(
      rgba,
      480,
      720,
      continuousTone: options.continuousTone,
      warmChroma: options.i006WarmChroma,
      rangeFit: options.rangeFit,
      ditherStrength: options.ditherStrength,
      checkCancelled: () => _check(isCancelled),
      onProgress: onProgress,
    );
  } else {
    final usesAe =
        options.dither && options.algorithm == FrameAlgorithm.atkinsonEnhanced;
    _check(isCancelled);
    indices = await quantizeFramePixels(
      rgba,
      480,
      720,
      algorithm: algorithm,
      strength: options.ditherStrength,
      dither: options.dither,
      correctionLut: usesAe ? forFilmCorrectionLut : null,
      selectionLut: usesAe ? forFilmSelectionLut : null,
      checkCancelled: () => _check(isCancelled),
      onProgress: onProgress,
    );
  }
  onProgress?.call(.9);
  _check(isCancelled);
  final png = await _encodeRgba(rgba);
  _check(isCancelled);
  final film = packFramePixels(indices, format: format);
  onProgress?.call(1);
  return FrameResult(
    png: png,
    cropPng: cropPng,
    film: film,
    format: format,
    algorithm: algorithm,
  );
}
