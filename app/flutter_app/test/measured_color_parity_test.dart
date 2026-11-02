import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_algorithms.dart';

void main() {
  final fixtures = jsonDecode(
    File('test/fixtures/measured_color.json').readAsStringSync(),
  ) as List;
  for (final raw in fixtures) {
    final fixture = raw as Map<String, dynamic>;
    test(
      'measured Web/Dart pixels: ${fixture['algorithm']} '
      'dither=${fixture['dither']} strength=${fixture['strength']}',
      () async {
        final rgba = Uint8List.fromList(
          (fixture['source'] as List).cast<int>(),
        );
        final codes = await quantizeFramePixels(
          rgba,
          fixture['width'] as int,
          fixture['height'] as int,
          algorithm: FrameAlgorithm.values.byName(
            fixture['algorithm'] as String,
          ),
          dither: fixture['dither'] as bool,
          strength: (fixture['strength'] as num).toDouble(),
        );
        expect(codes, (fixture['codes'] as List).cast<int>());
        expect(rgba, (fixture['preview'] as List).cast<int>());
      },
    );
  }
}
