import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_algorithms.dart';
import 'package:framefilm_ark/frame_codec.dart';
import 'package:framefilm_ark/frame_palettes.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final source = Uint8List.fromList([
    36,
    88,
    160,
    255,
    140,
    54,
    66,
    255,
    225,
    206,
    30,
    255,
    77,
    174,
    122,
    255,
    44,
    41,
    36,
    255,
    207,
    182,
    212,
    255,
  ]);

  for (final (algorithm, palette) in [
    (FrameAlgorithm.colorQualMeasured, colorQualMeasuredPalette),
    (FrameAlgorithm.colorQualCalibrated, colorQualCalibratedPalette),
  ]) {
    test(
      '${algorithm.name} keeps 8bpp Film 2.0 indices and preview colors',
      () async {
        final rgba = Uint8List.fromList(source);
        final codes = await quantizeFramePixels(
          rgba,
          3,
          2,
          algorithm: algorithm,
          dither: false,
        );
        expect(codes, everyElement(isIn(colorQualCandidates)));
        for (var i = 0; i < codes.length; i++) {
          expect(rgba.sublist(i * 4, i * 4 + 3), palette[codes[i]]);
        }
        final film = packFramePixels(
          codes,
          width: 3,
          height: 2,
          format: FrameFormat.colorQual,
        );
        expect(film[9], 2);
        expect(ByteData.sublistView(film).getUint32(0, Endian.little), 6);
        expect(film.sublist(32), [
          codes[3],
          codes[0],
          codes[4],
          codes[1],
          codes[5],
          codes[2],
        ]);
      },
    );

    test(
      '${algorithm.name} diffusion is deterministic and disabled strength is inert',
      () async {
        Future<List<int>> run(bool dither, double strength) async {
          final rgba = Uint8List.fromList(source);
          return quantizeFramePixels(
            rgba,
            3,
            2,
            algorithm: algorithm,
            dither: dither,
            strength: strength,
          );
        }

        expect(await run(true, 1.3), await run(true, 1.3));
        expect(await run(false, 0), await run(false, 3));
      },
    );

    test('${algorithm.name} obeys row cancellation', () async {
      var checks = 0;
      await expectLater(
        quantizeFramePixels(
          Uint8List(16 * 16 * 4),
          16,
          16,
          algorithm: algorithm,
          checkCancelled: () {
            if (++checks == 2) throw StateError('cancelled');
          },
        ),
        throwsStateError,
      );
      expect(checks, 2);
    });
  }

  test(
    'convertFrame preserves measured algorithm while writing format 0x02',
    () async {
      final recorder = ui.PictureRecorder();
      final canvas = ui.Canvas(recorder);
      canvas.drawColor(const ui.Color(0xff628bb2), ui.BlendMode.src);
      final picture = recorder.endRecording();
      final image = await picture.toImage(1, 1);
      final png = (await image.toByteData(format: ui.ImageByteFormat.png))!
          .buffer
          .asUint8List();
      image.dispose();
      picture.dispose();
      for (final algorithm in [
        FrameAlgorithm.colorQualMeasured,
        FrameAlgorithm.colorQualCalibrated,
      ]) {
        final result = await convertFrame(
          png,
          FrameOptions(algorithm: algorithm, dither: false),
        );
        expect(result.algorithm, algorithm);
        expect(result.format, FrameFormat.colorQual);
        expect(result.film[9], 2);
        expect(result.film.length, 32 + 480 * 720);
      }
    },
  );

  testWidgets('indexed generated film imports in the preview gateway', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await tester.pump();
    final film = packFramePixels(
      Uint8List(480 * 720),
      format: FrameFormat.colorQual,
    );
    await controller.command('importGeneratedFilm', {
      'bytes': film,
      'name': 'measured.film',
    });
    await tester.pump();
    expect(controller.snapshot.importedFile?.size, 32 + 480 * 720);
  });
}
