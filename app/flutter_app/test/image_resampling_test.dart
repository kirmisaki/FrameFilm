import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_codec.dart';
import 'package:framefilm_ark/image_resampling.dart';
import 'package:framefilm_ark/passport_avatar_editor.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  Future<Uint8List> pixels(ui.Image image) async =>
      (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!.buffer
          .asUint8List();

  Future<Uint8List> png(ui.Image image) async =>
      (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer
          .asUint8List();

  test('fractional 8/3 reduction integrates horizontal and vertical stripes', () async {
    // Each three output pixels cover eight input cells. The black-first stripe
    // integrals are 255 * [1, 4/3, 5/3] / (8/3), rounded once at the end.
    const expected = [96, 128, 159];
    for (final vertical in [false, true]) {
      final rgba = Uint8List(1080 * 4);
      for (var i = 0; i < 1080; i++) {
        final value = i.isEven ? 0 : 255;
        rgba.setRange(i * 4, i * 4 + 4, [value, value, value, 255]);
      }
      final source = await imageFromRgba(
        rgba,
        vertical ? 1 : 1080,
        vertical ? 1080 : 1,
      );
      final resized = await resizeCompositionSource(
        source,
        vertical ? 1 : 405,
        vertical ? 405 : 1,
      );
      try {
        final actual = await pixels(resized);
        for (var i = 0; i < 405; i++) {
          expect(actual.sublist(i * 4, i * 4 + 4), [
            expected[i % 3],
            expected[i % 3],
            expected[i % 3],
            255,
          ], reason: 'vertical=$vertical pixel=$i');
        }
      } finally {
        resized.dispose();
        source.dispose();
      }
    }
  });

  test(
    'two-dimensional fractional coverage includes edge and center cells',
    () async {
      final rgba = Uint8List(3 * 3 * 4);
      for (var i = 3; i < rgba.length; i += 4) {
        rgba[i] = 255;
      }
      rgba.setRange(0, 4, [90, 0, 0, 255]);
      rgba.setRange(8, 12, [0, 90, 0, 255]);
      rgba.setRange(24, 28, [0, 0, 90, 255]);
      rgba.setRange(16, 20, [255, 180, 90, 255]);
      final source = await imageFromRgba(rgba, 3, 3);
      final resized = await resizeCompositionSource(source, 2, 2);
      try {
        // Each 1.5 x 1.5 box contains 1/4 of the center (1/9 normalized)
        // and a whole corresponding corner (4/9 normalized).
        expect(await pixels(resized), [
          68,
          20,
          10,
          255,
          28,
          60,
          10,
          255,
          28,
          20,
          50,
          255,
          28,
          20,
          10,
          255,
        ]);
      } finally {
        resized.dispose();
        source.dispose();
      }
    },
  );

  test(
    'noninteger reduction preserves every channel of a constant color',
    () async {
      final rgba = Uint8List.fromList([
        for (var i = 0; i < 35; i++) ...[17, 129, 240, 255],
      ]);
      final source = await imageFromRgba(rgba, 7, 5);
      final resized = await resizeCompositionSource(source, 3, 2);
      try {
        expect(await pixels(resized), [
          for (var i = 0; i < 6; i++) ...[17, 129, 240, 255],
        ]);
      } finally {
        resized.dispose();
        source.dispose();
      }
    },
  );

  test('fractional alpha edges composite premultiplied color on white first', () async {
    // Half-red rawRgba is already premultiplied. On white it is [255,127,127].
    final source = await imageFromRgba(
      Uint8List.fromList([0, 0, 255, 255, 128, 0, 0, 128, 0, 0, 0, 0]),
      3,
      1,
    );
    final resized = await resizeCompositionSource(source, 2, 1);
    try {
      // Boxes weigh blue:red 2:1, then red:white 1:2.
      expect(await pixels(resized), [85, 42, 212, 255, 255, 212, 212, 255]);
    } finally {
      resized.dispose();
      source.dispose();
    }
  });

  test(
    'area average retains thin stripes and composites alpha on white',
    () async {
      final rgba = Uint8List(16 * 4);
      for (var p = 0; p < 16; p++) {
        final value = p.isEven ? 0 : 255;
        rgba.setRange(p * 4, p * 4 + 4, [value, value, value, 255]);
      }
      // Last four pixels: alternating opaque black and transparent black.
      for (var p = 13; p < 16; p += 2) {
        rgba.setRange(p * 4, p * 4 + 4, [0, 0, 0, 0]);
      }
      final source = await imageFromRgba(rgba, 16, 1);
      final resized = await resizeCompositionSource(source, 4, 1);
      try {
        final result = await pixels(resized);
        for (var p = 0; p < 4; p++) {
          expect(result[p * 4], inInclusiveRange(127, 128));
          expect(result[p * 4 + 1], result[p * 4]);
          expect(result[p * 4 + 2], result[p * 4]);
          expect(result[p * 4 + 3], 255);
        }
      } finally {
        resized.dispose();
        source.dispose();
      }
    },
  );

  test(
    'rotation fit zoom and pan retain legacy composition geometry',
    () async {
      final rgba = Uint8List(120 * 80 * 4);
      const colors = [
        [255, 0, 0, 255],
        [0, 255, 0, 255],
        [0, 0, 255, 255],
        [255, 255, 0, 255],
      ];
      for (var y = 0; y < 80; y++) {
        for (var x = 0; x < 120; x++) {
          final p = (y * 120 + x) * 4;
          rgba.setRange(
            p,
            p + 4,
            colors[(y >= 40 ? 2 : 0) + (x >= 60 ? 1 : 0)],
          );
        }
      }
      final source = await imageFromRgba(rgba, 120, 80);
      final bytes = await png(source);
      source.dispose();
      for (final cover in [false, true]) {
        for (var turns = 0; turns < 4; turns++) {
          for (final zoom in [.7, 1.3]) {
            final results = <Uint8List>[];
            for (final method in ImageResampling.values) {
              final image = await renderImageComposition(
                bytes,
                width: 48,
                height: 72,
                cover: cover,
                quarterTurns: turns,
                zoom: zoom,
                panX: 3,
                panY: -2,
                resampling: method,
              );
              results.add(await pixels(image));
              image.dispose();
            }
            // Check away from interpolated boundaries. The great majority of
            // pixels must keep exactly the same solid region and white margins.
            var matching = 0;
            for (var i = 0; i < results[0].length; i += 4) {
              if (List.generate(
                4,
                (c) => (results[0][i + c] - results[1][i + c]).abs() <= 2,
              ).every((v) => v)) {
                matching++;
              }
            }
            expect(
              matching / (48 * 72),
              greaterThan(.9),
              reason: 'cover=$cover turns=$turns zoom=$zoom',
            );
          }
        }
      }
    },
  );

  test(
    'old avatar JSON defaults to HQ and source stays within storage bounds',
    () async {
      final image = await imageFromRgba(Uint8List(800 * 200 * 4), 800, 200);
      final source = await prepareAvatarSource(await png(image));
      image.dispose();
      final edit = PassportAvatarEdit.fromJson({
        'source': source,
        'options': {},
      });
      expect(edit.options['resampling'], 'highQuality');
      final decoded = await decodeCompositionSource(
        base64Decode(source.substring(22)),
      );
      expect((decoded.width, decoded.height), (640, 160));
      expect(source.length, lessThanOrEqualTo(6000000));
      decoded.dispose();
      expect(
        () => PassportAvatarEdit.fromJson({
          'source': source,
          'options': {'resampling': 'invalid'},
        }),
        throwsFormatException,
      );
    },
  );

  test('new rendering phase honours cancellation before decoding', () async {
    await expectLater(
      renderImageComposition(
        Uint8List(0),
        width: 128,
        height: 160,
        checkCancelled: () => throw StateError('cancelled'),
      ),
      throwsStateError,
    );
  });

  test(
    'Film crop uses the same resizing and adjustments as prepareFrame',
    () async {
      final image = await imageFromRgba(
        Uint8List.fromList([100, 120, 180, 255]),
        1,
        1,
      );
      final source = await png(image);
      image.dispose();
      const options = FrameOptions(
        resampling: ImageResampling.highQuality,
        brightness: .1,
        contrast: 1.1,
        saturation: .8,
        format: FrameFormat.monoFast,
      );
      final result = await convertFrame(
        source,
        options,
        includeCropPreview: true,
      );
      expect(result.cropPng, await prepareFrame(source, options));
    },
  );
}
