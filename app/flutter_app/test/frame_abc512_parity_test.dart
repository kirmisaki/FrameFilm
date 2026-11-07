import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_abc512.dart';

void main() {
  test(
    'I006 controls match independent Python fixed-display-L and FS reference',
    () async {
      final f = jsonDecode(
        File('test/fixtures/ark512_controls.json').readAsStringSync(),
      ) as Map<String, dynamic>;
      for (final item in f['cases'] as List) {
        final o = item['options'] as Map<String, dynamic>;
        final codes = await quantizeAbc512(
          Uint8List.fromList((f['rgba'] as List).cast<int>()),
          f['width'] as int,
          f['height'] as int,
          continuousTone: o['continuousTone'] as bool? ?? false,
          warmChroma: (o['warmChroma'] as num?)?.toDouble() ?? .25,
          rangeFit: (o['rangeFit'] as num?)?.toDouble() ?? 0,
          ditherStrength: (o['ditherStrength'] as num?)?.toDouble() ?? 1,
        );
        expect(
          codes,
          (item['expectedCodes'] as List).cast<int>(),
          reason: item['name'] as String,
        );
      }
    },
  );
  test('I006 controls reject nonfinite and out-of-range values', () async {
    for (final value in [double.nan, double.infinity, -.1, 1.1]) {
      await expectLater(
        quantizeAbc512(Uint8List(4), 1, 1, warmChroma: value),
        throwsArgumentError,
      );
      await expectLater(
        quantizeAbc512(Uint8List(4), 1, 1, rangeFit: value),
        throwsArgumentError,
      );
    }
    for (final value in [double.nan, double.infinity, -.1, 5.1]) {
      await expectLater(
        quantizeAbc512(Uint8List(4), 1, 1, ditherStrength: value),
        throwsArgumentError,
      );
    }
  });
  test(
    'continuous tone is opt in and agrees with shared JS synthetic pixels',
    () async {
      final f = jsonDecode(
        File('test/fixtures/ark512_continuous_tone.json').readAsStringSync(),
      ) as Map<String, dynamic>;
      final source = (f['rgba'] as List).cast<int>();
      final defaults = await quantizeAbc512(
        Uint8List.fromList(source),
        f['width'] as int,
        f['height'] as int,
      );
      final frozen = await quantizeAbc512(
        Uint8List.fromList(source),
        f['width'] as int,
        f['height'] as int,
        continuousTone: false,
      );
      final continuous = await quantizeAbc512(
        Uint8List.fromList(source),
        f['width'] as int,
        f['height'] as int,
        continuousTone: true,
      );
      expect(defaults, (f['expectedCodes'] as List).cast<int>());
      expect(frozen, defaults);
      expect(continuous, (f['continuousCodes'] as List).cast<int>());
      expect(continuous, isNot(defaults));
    },
  );
  test('I006 independently generated Python reference parity', () async {
    final f = jsonDecode(
      File('../../tools/epd-panel-lab/profiles/ark_512_i006_fixture.json')
          .readAsStringSync(),
    ) as Map<String, dynamic>;
    final rgba = Uint8List.fromList((f['rgba'] as List).cast<int>());
    final codes = await quantizeAbc512(
      rgba,
      f['width'] as int,
      f['height'] as int,
    );
    expect(codes, (f['expectedCodes'] as List).cast<int>());
  });
}
