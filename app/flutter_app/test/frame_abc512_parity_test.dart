import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_abc512.dart';

void main() {
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
