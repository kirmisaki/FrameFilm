import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/passport_profile.dart';

void main() {
  Uint8List pixels() =>
      Uint8List(440 * 608 * 4)..fillRange(0, 440 * 608 * 4, 255);
  void rgb(Uint8List rgba, int x, int y, List<int> color) =>
      rgba.setRange((y * 440 + x) * 4, (y * 440 + x) * 4 + 3, color);
  test('legacy JSON defaults mono and selected modes round trip', () {
    final old =
        jsonDecode(const PassportProfile().toJson()) as Map<String, dynamic>;
    old.remove('avatarMode');
    expect(PassportProfile.fromJson(jsonEncode(old)).avatarMode, 'mono');
    for (final mode in ['mono', 'grayscale', 'color']) {
      final p = PassportProfile(avatarMode: mode);
      expect(PassportProfile.fromJson(p.toJson()).avatarMode, mode);
    }
    expect(
      () => PassportProfile.fromJson(
        jsonEncode({...old, 'avatarMode': 'invalid'}),
      ),
      throwsFormatException,
    );
  });
  test('color payload uses native nibbles and limits colors to the avatar', () {
    final rgba = pixels();
    final colors = [
      [0, 0, 0],
      [255, 255, 255],
      [255, 255, 0],
      [255, 0, 0],
      [0, 0, 255],
      [41, 204, 20],
    ];
    for (var i = 0; i < colors.length; i++) {
      rgb(rgba, 20 + i, 143, colors[i]);
    }
    rgb(rgba, 0, 0, [255, 0, 0]);
    final bin = encodePassportPixels(rgba, 'color', hasAvatar: true);
    expect(bin.length, 133776);
    expect(bin.take(16), [
      70,
      70,
      85,
      73,
      2,
      2,
      184,
      1,
      96,
      2,
      0,
      0,
      0,
      0,
      0,
      0,
    ]);
    final offset = 16 + (143 * 440 + 20) ~/ 2;
    expect(bin.sublist(offset, offset + 3), [0x01, 0x23, 0x56]);
    expect(bin[16] >> 4, 0);
    expect(rgba.take(3), [0, 0, 0]);
  });
  test(
    'grayscale makes black white dots inside avatar and keeps body threshold',
    () {
      final rgba = pixels();
      for (var y = 143; y < 147; y++) {
        for (var x = 20; x < 24; x++) {
          rgb(rgba, x, y, [128, 128, 128]);
        }
      }
      rgb(rgba, 0, 0, [128, 128, 128]);
      final bin = encodePassportPixels(rgba, 'grayscale', hasAvatar: true);
      expect(bin.length, 33456);
      var black = 0;
      for (var y = 143; y < 147; y++) {
        for (var x = 20; x < 24; x++) {
          if (rgba[(y * 440 + x) * 4] == 0) black++;
        }
      }
      expect(black, 8);
      expect(rgba[0], 0);
      final legacy = pixels();
      rgb(legacy, 20, 143, [128, 128, 128]);
      encodePassportPixels(legacy, 'mono', hasAvatar: true);
      expect(legacy[(143 * 440 + 20) * 4], 0);
    },
  );
}
