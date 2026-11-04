import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_abc512.dart';

void main() {
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
