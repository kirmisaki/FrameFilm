import 'dart:ui' as ui;

import 'package:framefilm_ark/frame_algorithms.dart'
    show quantizeFramePixels, adjustFrameColors;

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_abc512.dart';
import 'package:framefilm_ark/frame_abc512_data.dart';
import 'package:framefilm_ark/frame_abc9.dart';
import 'package:framefilm_ark/frame_codec.dart';
import 'package:framefilm_ark/animation_film.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

void main() {
  test('I006 frozen physical tuple aliases preserve exact measured colors', () {
    expect(abc512CodeAliases, {16: 468, 59: 60, 252: 253, 333: 396});
    for (final alias in abc512CodeAliases.entries) {
      expect(
        abc512OutputColors[abc512OutputCodes.indexOf(alias.key)],
        abc512OutputColors[abc512OutputCodes.indexOf(alias.value)],
      );
    }
    expect([387, 6, 407].any(abc512CodeAliases.containsKey), false);
  });
  test('ABC9 preserves all nine bits and portrait physical rotation', () {
    final bytes = packFramePixels(
      Uint16List.fromList([0, 256, 387, 511, 6, 407]),
      width: 2,
      height: 3,
      format: FrameFormat.abc512,
    );
    final data = ByteData.sublistView(bytes);
    expect(bytes[9], 0x7f);
    expect(bytes.sublist(12, 16), [65, 66, 67, 57]);
    expect(
      [
        for (var p = 32; p < bytes.length; p += 2)
          data.getUint16(p, Endian.little),
      ],
      [6, 387, 0, 407, 511, 256],
    );
    expect(
      () => packFramePixels(
        [512],
        width: 1,
        height: 1,
        format: FrameFormat.abc512,
      ),
      throwsArgumentError,
    );
    expect(() => assembleAnimationFilm([bytes, bytes]), throwsArgumentError);
  });
  test(
    'ABC9 strictly rejects malformed framing, multi-frame and high bits',
    () {
      final film = packFramePixels(
        Uint16List(480 * 720),
        format: FrameFormat.abc512,
      );
      validateAbc9Film(film);
      for (final offset in [0, 4, 6, 8, 9, 10, 12, 13, 14, 15, 33]) {
        final broken = Uint8List.fromList(film);
        broken[offset] = offset == 33 ? 2 : broken[offset] ^ 1;
        expect(
          () => validateAbc9Film(broken),
          throwsFormatException,
          reason: 'offset $offset',
        );
      }
    },
  );
  test('I006 yields for cancellation before producing output', () async {
    var cancelled = false;
    final rgba = Uint8List(16 * 16 * 4);
    final job = quantizeAbc512(
      rgba,
      16,
      16,
      checkCancelled: () {
        if (cancelled) throw const FrameCancelledException();
      },
    );
    cancelled = true;
    await expectLater(job, throwsA(isA<FrameCancelledException>()));
  });
  test(
    'I006 cannot enter uint8 quantizer and skips generic source fit',
    () async {
      await expectLater(
        quantizeFramePixels(
          Uint8List(4),
          1,
          1,
          algorithm: FrameAlgorithm.abc512I006,
        ),
        throwsArgumentError,
      );
      final original = Uint8List.fromList([120, 100, 70, 255]);
      final fitted = Uint8List.fromList(original);
      adjustFrameCompositionRgba(
        fitted,
        const FrameOptions(
          algorithm: FrameAlgorithm.abc512I006,
          rangeFit: 1,
          warmChroma: 1,
        ),
      );
      expect(fitted, original);
    },
  );
  test(
    'I006 preview is derived from the same uint16 codes packed into Film',
    () async {
      final rgba = Uint8List.fromList([
        for (var i = 0; i < 48; i++) ...[
          (i * 19) % 256,
          (i * 7) % 256,
          (i * 3) % 256,
          255,
        ],
      ]);
      final codes = await quantizeAbc512(rgba, 6, 8);
      expect(codes.every(abc512OutputCodes.contains), true);
      expect(codes.any(abc512CodeAliases.containsKey), false);
      final film = packFramePixels(
        codes,
        width: 6,
        height: 8,
        format: FrameFormat.abc512,
      );
      final unpacked = Uint16List(48), data = ByteData.sublistView(film);
      for (var y = 0; y < 8; y++) {
        for (var x = 0; x < 6; x++) {
          unpacked[y * 6 + x] = data.getUint16(
            32 + (x * 8 + 7 - y) * 2,
            Endian.little,
          );
        }
      }
      final expected = Uint8List(48 * 4);
      paintAbc512Codes(expected, unpacked);
      expect(rgba, expected);
      expect(codes.any((v) => v > 255), true);
    },
  );
  test(
    'I006 codec applies controls and contrast/saturation to PNG and film once',
    () async {
      final recorder = ui.PictureRecorder();
      ui.Canvas(recorder)
          .drawColor(const ui.Color(0xff906050), ui.BlendMode.src);
      final picture = recorder.endRecording(),
          sourceImage = await picture.toImage(2, 2);
      final source = (await sourceImage.toByteData(
        format: ui.ImageByteFormat.png,
      ))!.buffer.asUint8List();
      sourceImage.dispose();
      picture.dispose();
      final films = <Uint8List>[];
      for (final adjustment in [(1.0, 1.0), (1.6, 1.0), (1.0, 0.0)]) {
        final result = await convertFrame(
          source,
          FrameOptions(
            algorithm: FrameAlgorithm.abc512I006,
            continuousTone: true,
            i006WarmChroma: .8,
            rangeFit: .6,
            ditherStrength: 0,
            warmChroma: 1,
            contrast: adjustment.$1,
            saturation: adjustment.$2,
          ),
        );
        validateAbc9Film(result.film);
        films.add(result.film);
        final reference = Uint8List.fromList([144, 96, 80, 255]);
        adjustFrameColors(
          reference,
          contrast: adjustment.$1,
          saturation: adjustment.$2,
        );
        final one = await quantizeAbc512(
          reference,
          1,
          1,
          continuousTone: true,
          warmChroma: .8,
          rangeFit: .6,
          ditherStrength: 0,
        );
        final packed = ByteData.sublistView(result.film);
        var mismatches = 0;
        for (var i = 32; i < result.film.length; i += 2) {
          if (packed.getUint16(i, Endian.little) != one[0]) mismatches++;
        }
        expect(
          mismatches,
          0,
          reason: 'core parameters must match codec; generic warm must not apply twice',
        );
        final codec = await ui.instantiateImageCodec(result.png),
            frame = await codec.getNextFrame();
        final actual = (await frame.image.toByteData(
          format: ui.ImageByteFormat.rawRgba,
        ))!.buffer.asUint8List();
        mismatches = 0;
        for (var i = 0; i < actual.length; i++) {
          if (actual[i] != reference[i % 4]) mismatches++;
        }
        expect(
          mismatches,
          0,
          reason: 'PNG must paint exactly the exported ABC9 code',
        );
        frame.image.dispose();
        codec.dispose();
      }
      expect(
        films[1],
        isNot(films[0]),
        reason: 'contrast must change the actual film',
      );
      expect(
        films[2],
        isNot(films[0]),
        reason: 'saturation must change the actual film',
      );
    },
  );
  for (final continuousTone in [false, true]) {
    test(
      'encoded I006 PNG pixels agree with exported native Film continuous=$continuousTone',
      () async {
        final recorder = ui.PictureRecorder();
        final drawing = ui.Canvas(recorder);
        drawing.drawColor(const ui.Color(0xff906050), ui.BlendMode.src);
        final picture = recorder.endRecording(),
            image = await picture.toImage(2, 2);
        final source = (await image.toByteData(format: ui.ImageByteFormat.png))!
            .buffer
            .asUint8List();
        image.dispose();
        picture.dispose();
        final result = await convertFrame(
          source,
          FrameOptions(
            algorithm: FrameAlgorithm.abc512I006,
            continuousTone: continuousTone,
          ),
        );
        validateAbc9Film(result.film);
        expect(result.format, FrameFormat.abc512);
        final codec = await ui.instantiateImageCodec(result.png),
            frame = await codec.getNextFrame();
        final rgba = (await frame.image.toByteData(
          format: ui.ImageByteFormat.rawRgba,
        ))!.buffer.asUint8List();
        final codes = Uint16List(480 * 720),
            data = ByteData.sublistView(result.film);
        for (var y = 0; y < 720; y++) {
          for (var x = 0; x < 480; x++) {
            codes[y * 480 + x] = data.getUint16(
              32 + (x * 720 + 719 - y) * 2,
              Endian.little,
            );
          }
        }
        final expected = Uint8List(rgba.length);
        paintAbc512Codes(expected, codes);
        expect(rgba, expected);
        frame.image.dispose();
        codec.dispose();
      },
    );
  }
  testWidgets(
    'ABC9 device staging requires explicit capability and clears on disconnect',
    (tester) async {
      final gateway = PreviewDeviceGateway();
      final controller = DeviceController(gateway);
      addTearDown(() {
        controller.dispose();
        gateway.dispose();
      });
      await tester.pump();
      final film = packFramePixels(
        Uint16List(480 * 720),
        format: FrameFormat.abc512,
      );
      await expectLater(
        controller.command('importGeneratedFilm', {
          'bytes': film,
          'name': 'i006.film',
        }),
        throwsStateError,
      );
      await controller.command('connect');
      await tester.pump();
      expect(controller.snapshot.abc9PreviewSupported, true);
      await controller.command('importGeneratedFilm', {
        'bytes': film,
        'name': 'i006.film',
      });
      await tester.pump();
      expect(controller.snapshot.importedFile?.name, 'i006.film');
      expect(controller.errorMessage, isNull);
      await controller.command('disconnect');
      await tester.pump();
      expect(controller.snapshot.abc9PreviewSupported, false);
      final old = const DeviceSnapshot(
        connected: true,
        abc9PreviewSupported: true,
      );
      expect(
        DeviceSnapshot.fromMap({
          'connected': true,
          'abc9PreviewSupported': false,
        }).retainingTransfer(old).abc9PreviewSupported,
        false,
      );
    },
  );
}
