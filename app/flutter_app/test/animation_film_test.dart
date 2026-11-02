import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/animation_film.dart';

Uint8List frame(List<int> pixels) {
  final bytes = Uint8List(32 + pixels.length);
  final header = ByteData.sublistView(bytes);
  header.setUint32(0, pixels.length, Endian.little);
  header.setUint16(4, 4, Endian.little);
  header.setUint16(6, 2, Endian.little);
  bytes[9] = 1;
  bytes.setRange(32, bytes.length, pixels);
  return bytes;
}

void main() {
  test('animation preserves oriented pixel bodies and updates header', () {
    final joined = assembleAnimationFilm([
      frame([0x80]),
      frame([0x01]),
    ]);
    final header = ByteData.sublistView(joined);
    expect(header.getUint32(0, Endian.little), 2);
    expect(header.getUint16(10, Endian.little), 2);
    expect(joined.sublist(32), [0x80, 0x01]);
    expect(header.getUint16(4, Endian.little), 4);
  });
  test('rejects partial, mismatched and single-frame animations', () {
    expect(
      () => assembleAnimationFilm([
        frame([0]),
      ]),
      throwsArgumentError,
    );
    final other = frame([0]);
    other[9] = 3;
    expect(
      () => assembleAnimationFilm([
        frame([0]),
        other,
      ]),
      throwsArgumentError,
    );
    final partial = frame([0]);
    partial[0] = 2;
    expect(
      () => assembleAnimationFilm([
        frame([0]),
        partial,
      ]),
      throwsArgumentError,
    );
  });
}
