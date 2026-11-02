import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_algorithms.dart';
import 'package:framefilm_ark/frame_codec.dart';

void main() {
  final fixtures = jsonDecode(
    File('test/fixtures/forfilm_remaining.json').readAsStringSync(),
  ) as List;
  for (final raw in fixtures) {
    final f = raw as Map<String, dynamic>;
    test('original web converter parity: ${f['name']}', () async {
      final pixels = Uint8List.fromList((f['source'] as List).cast<int>());
      final codes = await quantizeFramePixels(
        pixels,
        f['width'] as int,
        f['height'] as int,
        algorithm: FrameAlgorithm.values.byName(f['algorithm'] as String),
        dither: f['dither'] as bool,
        strength: (f['strength'] as num).toDouble(),
      );
      expect(codes, (f['codes'] as List).cast<int>());
    });
  }
  test('8bpp film header and physical orientation', () {
    for (final format in [FrameFormat.colorFast55, FrameFormat.colorQual]) {
      final film = packFramePixels(
        Uint8List.fromList([0, 3, 15, 21, 32, 63]),
        width: 2,
        height: 3,
        format: format,
      );
      final head = ByteData.sublistView(film);
      expect(head.getUint32(0, Endian.little), 6);
      expect(head.getUint16(4, Endian.little), 3);
      expect(head.getUint16(6, Endian.little), 2);
      expect(film[8], 0);
      expect(film[9], format == FrameFormat.colorFast55 ? 3 : 2);
      expect(film.sublist(10, 32), everyElement(0));
      expect(film.sublist(32), [32, 15, 0, 63, 21, 3]);
    }
  });
  test('SZ and adaptive cooperate with cancellation', () async {
    for (final algorithm in [
      FrameAlgorithm.szEnhanced,
      FrameAlgorithm.adaptive,
      FrameAlgorithm.colorFast55,
      FrameAlgorithm.atkinsonSzCalib,
    ]) {
      var checks = 0;
      final data = Uint8List(48 * 48 * 4)..fillRange(0, 48 * 48 * 4, 128);
      await expectLater(
        quantizeFramePixels(
          data,
          48,
          48,
          algorithm: algorithm,
          checkCancelled: () {
            if (++checks == 2) throw StateError('cancel');
          },
        ),
        throwsStateError,
      );
      expect(checks, 2);
    }
  });
  test('adaptive is deterministic and produces valid film codes', () async {
    final source = Uint8List.fromList(
      List.generate(36 * 33 * 4, (i) => i % 4 == 3 ? 255 : (i * 31) % 256),
    );
    final a = await quantizeFramePixels(
      Uint8List.fromList(source),
      36,
      33,
      algorithm: FrameAlgorithm.adaptive,
    );
    final b = await quantizeFramePixels(
      Uint8List.fromList(source),
      36,
      33,
      algorithm: FrameAlgorithm.adaptive,
    );
    expect(a, b);
    expect(a, everyElement(inInclusiveRange(0, 5)));
  });
}
