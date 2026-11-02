import 'package:framefilm_ark/source_image_fit.dart';

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_codec.dart';

void main() {
  test('zero fitting options preserve existing byte adjustment', () {
    final original = Uint8List.fromList([20, 80, 160, 93, 230, 175, 105, 255]);
    final a = Uint8List.fromList(original), b = Uint8List.fromList(original);
    adjustFrameRgba(a, const FrameOptions(contrast: 1.2, saturation: .8));
    adjustFrameRgba(
      b,
      const FrameOptions(
        contrast: 1.2,
        saturation: .8,
        rangeFit: 0,
        warmChroma: 0,
      ),
    );
    expect(b, a);
  });
  test('mono ignores color fitting and SZ disables range fitting', () {
    final a = Uint8List.fromList([230, 175, 105, 79]);
    final b = Uint8List.fromList(a);
    adjustFrameRgba(
      a,
      const FrameOptions(
        format: FrameFormat.monoFast,
        rangeFit: 1,
        warmChroma: 1,
      ),
    );
    adjustFrameRgba(b, const FrameOptions(format: FrameFormat.monoFast));
    expect(a, b);
    for (final algorithm in [
      FrameAlgorithm.szEnhanced,
      FrameAlgorithm.atkinsonSzCalib,
    ]) {
      expect(
        sourceFitIsSz(FrameOptions(dither: true, algorithm: algorithm)),
        true,
      );
      final c = Uint8List.fromList(b);
      adjustFrameSourceFit(
        c,
        FrameOptions(dither: true, algorithm: algorithm, rangeFit: 1),
      );
      expect(c, b);
    }
  });
  test('SZ exact preview and export share raw warm input; calibrated SZ retains manual adjustments', () {
    final source = Uint8List.fromList(
      List.generate(64, (_) => [230, 175, 105, 79]).expand((p) => p).toList(),
    );
    for (final algorithm in [
      FrameAlgorithm.szEnhanced,
      FrameAlgorithm.atkinsonSzCalib,
    ]) {
      final o = FrameOptions(
        dither: true,
        algorithm: algorithm,
        brightness: .12,
        contrast: 1.3,
        saturation: .7,
        rangeFit: 1,
        warmChroma: .8,
      );
      final expected = Uint8List.fromList(source);
      if (algorithm != FrameAlgorithm.szEnhanced) {
        adjustFrameRgba(expected, o, applySourceFit: false);
      }
      fitSourceRgba(expected, warmChroma: .8, profile: sourceFitProfile(o));
      final shared = Uint8List.fromList(source);
      adjustFrameCompositionRgba(shared, o);
      expect(shared, expected);
    }
  });
}
