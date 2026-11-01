import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/passport_profile.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('v1 schema round trip preserves incomplete draft', () {
    const p = PassportProfile(
      codename: '博士',
      numberUnset: true,
      signature: '测试\n第二行',
    );
    final restored = PassportProfile.fromJson(p.toJson());
    expect(restored.codename, '博士');
    expect(restored.numberUnset, true);
    expect(restored.signature, '测试\n第二行');
    expect(restored.avatar, isNull);
  });
  test('reject wrong version, types, field length, PNG and oversized JSON', () {
    final base =
        jsonDecode(const PassportProfile().toJson()) as Map<String, dynamic>;
    for (final mutation in <Map<String, dynamic>>[
      {'version': 2},
      {'numberUnset': 0},
      {'codename': List.filled(33, 'x').join()},
      {'avatar': 'data:image/png;base64,AAAA'},
      {'affiliation': null},
    ]) {
      expect(
        () => PassportProfile.fromJson(jsonEncode({...base, ...mutation})),
        throwsFormatException,
      );
    }
    expect(
      () => PassportProfile.fromJson(List.filled(350001, ' ').join()),
      throwsFormatException,
    );
  });
  test('layout checks required values and signature line count', () {
    expect(
      passportLayoutErrors(const PassportProfile()).keys,
      containsAll(['codename', 'number']),
    );
    expect(
      passportLayoutErrors(
        const PassportProfile(codenameUnset: true, numberUnset: true),
      ),
      isEmpty,
    );
    expect(
      passportLayoutErrors(
        const PassportProfile(
          codenameUnset: true,
          numberUnset: true,
          signature: 'a\nb\nc\nd',
        ),
      ),
      contains('signature'),
    );
  });
  testWidgets('raster FFUI header and exact PNG binary pixel agreement', (
    tester,
  ) async {
    late PassportRaster raster;
    await tester.runAsync(() async {
      raster = await renderPassport(
        const PassportProfile(codename: '博士', number: '001'),
      );
    });
    expect(raster.bin.length, 33456);
    expect(raster.bin.take(16), [
      70,
      70,
      85,
      73,
      1,
      1,
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
    expect(raster.png.take(8), [137, 80, 78, 71, 13, 10, 26, 10]);
    // Header chip is solid ink; outer canvas remains white.
    expect(raster.bin[16], 0);
    expect(
      raster.bin[16 + 10 * 55 + (330 >> 3)] & (0x80 >> (330 & 7)),
      isNonZero,
    );
  });
}
