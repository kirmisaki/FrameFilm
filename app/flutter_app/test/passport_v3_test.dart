import 'dart:convert';
import 'dart:typed_data';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_abc512.dart';
import 'package:framefilm_ark/frame_palettes.dart';
import 'package:framefilm_ark/passport_avatar_editor.dart';
import 'package:framefilm_ark/passport_color.dart';
import 'package:framefilm_ark/passport_editor.dart';
import 'package:framefilm_ark/passport_profile.dart';
import 'package:framefilm_ark/student_passport_profile.dart';
import 'package:image/image.dart' as img;

String portrait(int width, int height) {
  final image = img.Image(width: width, height: height, numChannels: 3);
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      image.setPixelRgb(x, y, x * 255 ~/ width, y * 255 ~/ height,
          (x + y).isEven ? 220 : 30);
    }
  }
  return 'data:image/png;base64,${base64Encode(img.encodePng(image))}';
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('v3 schemas separate kind, mode and exact portrait dimensions', () {
    final student = StudentPassportProfile(avatar: portrait(239, 299), output: 'colorfast');
    final operator = PassportProfile(avatar: portrait(128, 160));
    expect(jsonDecode(student.toJson()), containsPair('version', 3));
    expect(jsonDecode(student.toJson()), containsPair('kind', 'student'));
    expect(jsonDecode(operator.toJson()), containsPair('kind', 'operator'));
    expect(StudentPassportProfile.fromJson(student.toJson()).output, 'colorfast');
    expect(PassportProfile.fromJson(operator.toJson()).output, 'i006');
    expect(() => PassportProfile.fromJson(student.toJson()), throwsFormatException);
    expect(() => StudentPassportProfile.fromJson(operator.toJson()), throwsFormatException);
    final raw = jsonDecode(student.toJson()) as Map<String, dynamic>;
    for (final changes in [
      {'output': 'ffui'}, {'output': null}, {'kind': 'operator'},
      {'studentOutput': 'i006'},
      {'avatarMode': 'mono'}, {'avatar': portrait(128, 160)},
    ]) {
      expect(() => StudentPassportProfile.fromJson(jsonEncode({
        ...raw, ...changes,
      })), throwsFormatException);
    }
    expect(() => PassportProfile.fromJson(PassportProfile(avatar: portrait(239, 299)).toJson()), throwsFormatException);
    final legacy = StudentPassportProfile(version: 2, avatarMode: 'color', avatar: portrait(128, 160), studentOutput: 'ffui');
    expect(StudentPassportProfile.fromJson(legacy.toJson()).version, 2);
    expect(StudentPassportProfile.fromJson(legacy.toJson()).output, 'ffui');
    expect(PassportProfile.fromJson(const PassportProfile(version: 1, avatarMode: 'mono').toJson()).version, 1);
  });

  test('RGB crop samples full student slot and never depends on output quantizer', () async {
    final edit = PassportAvatarEdit(portrait(478, 598), {'brightness': .1});
    final a = await renderAvatarEdit(edit, 'i006', width: 239, height: 299, quantize: false);
    final b = await renderAvatarEdit(edit, 'colorfast', width: 239, height: 299, quantize: false);
    expect(a.crop, b.crop);
    final bytes = base64Decode(a.crop.substring(22));
    expect(ByteData.sublistView(bytes).getUint32(16), 239);
    expect(ByteData.sublistView(bytes).getUint32(20), 299);
    expect(bytes[25], 2); // RGB, not indexed and not RGBA.
    final image = img.decodePng(bytes)!;
    final color = image.getPixel(30, 200);
    expect(color.r, isNot(color.g));
    expect(utf8.encode(StudentPassportProfile(avatar: a.crop).toJson()).length, lessThanOrEqualTo(350000));
    await expectLater(
      renderAvatarEdit(edit, 'i006', checkCancelled: () { throw StateError('cancel'); }),
      throwsStateError,
    );
  });

  test('JSON UTF-8 limit is enforced on actual encoded bytes', () {
    final overhead = utf8.encode(encodePassportJson({'x': ''})).length;
    expect(utf8.encode(encodePassportJson({'x': 'a' * (350000 - overhead)})).length, 350000);
    expect(() => encodePassportJson({'x': 'a' * (350001 - overhead)}), throwsFormatException);
    expect(() => encodePassportJson({'x': '学' * 120000}), throwsFormatException);
  });

  test('incompressible full-color student PNG stays inside avatar and JSON bounds', () {
    final random = math.Random(47);
    final rgba = Uint8List(239 * 299 * 4);
    for (var i = 0; i < rgba.length; i += 4) {
      for (var c = 0; c < 3; c++) { rgba[i + c] = random.nextInt(256); }
      rgba[i + 3] = 255;
    }
    final avatar = passportRgbPng(rgba, 239, 299);
    expect(avatar.length, lessThanOrEqualTo(300000));
    final raw = StudentPassportProfile(avatar: avatar).toJson();
    expect(utf8.encode(raw).length, lessThanOrEqualTo(350000));
    expect(StudentPassportProfile.fromJson(raw).avatar, avatar);
  });

  for (final output in passportOutputs) {
    test('compact $output exact header, row-major plane and emitted-code preview', () async {
      final rgba = Uint8List(440 * 608 * 4)..fillRange(0, 440 * 608 * 4, 255);
      rgba.setRange(0, 4, [0, 0, 0, 255]);
      for (var y = 143; y < 303; y++) {
        for (var x = 20; x < 148; x++) {
          rgba.setRange((y * 440 + x) * 4, (y * 440 + x + 1) * 4,
              [(x - 20) * 2, y - 143, x.isEven ? 190 : 60, 255]);
        }
      }
      final bin = await encodeColorPassportPixels(rgba, output, hasAvatar: true);
      final abc = output == 'i006';
      expect(bin.take(16), [70, 70, 85, 73, 3, abc ? 127 : 3, 184, 1, 96, 2, 1, 0, 0, 0, 0, 0]);
      expect(bin.length, abc ? 74416 : 53936);
      expect(bin[16], 0x80);
      final data = ByteData.sublistView(bin);
      final codes = Uint16List(20480);
      for (var i = 0; i < codes.length; i++) {
        codes[i] = abc ? data.getUint16(33456 + i * 2, Endian.little) : bin[33456 + i];
        expect(codes[i], lessThanOrEqualTo(abc ? 511 : 63));
      }
      final expected = Uint8List(20480 * 4);
      if (abc) {
        paintAbc512Codes(expected, codes);
      } else {
        for (var i = 0; i < codes.length; i++) {
          expected.setRange(i * 4, i * 4 + 4, [...colorFastPalette[codes[i]], 255]);
        }
      }
      for (var y = 0; y < 160; y++) {
        expect(rgba.sublist(((y + 143) * 440 + 20) * 4, ((y + 143) * 440 + 148) * 4),
            expected.sublist(y * 128 * 4, (y + 1) * 128 * 4));
      }
      final absent = await encodeColorPassportPixels(Uint8List(440 * 608 * 4)..fillRange(0, 440 * 608 * 4, 255), output, hasAvatar: false);
      expect(absent.length, 33456);
      expect(absent[10], 0);
    });
  }

  test('student ColorFast uses native frame indices and matching rotated PNG', () async {
    final template = img.Image(width: 720, height: 480, numChannels: 3);
    img.fill(template, color: img.ColorRgb8(220, 200, 50));
    img.fillRect(template, x1: 0, y1: 0, x2: 30, y2: 90, color: img.ColorRgb8(35, 65, 135));
    final raster = await renderStudentPassport(
      StudentPassportProfile(codenameUnset: true, studentNumberUnset: true,
          output: 'colorfast', avatar: portrait(239, 299)), img.encodePng(template));
    expect(raster.bin.length, 345632);
    expect(raster.bin[9], 3);
    expect(ByteData.sublistView(raster.bin).getUint16(4, Endian.little), 720);
    expect(ByteData.sublistView(raster.bin).getUint16(6, Endian.little), 480);
    expect(raster.bin.skip(32).every((v) => v <= 63), true);
    final png = img.decodePng(raster.png)!;
    for (var y = 0; y < 480; y += 23) {
      for (var x = 0; x < 720; x += 19) {
        final code = raster.bin[32 + 720 * 480 - 1 - (y * 720 + x)];
        final pixel = png.getPixel(x, y);
        expect([pixel.r, pixel.g, pixel.b], colorFastPalette[code]);
      }
    }
  });

  testWidgets('old monochrome operator cannot be promoted by adjusting old crop', (tester) async {
    final controller = PassportEditorController()..profile = PassportProfile(
      version: 1, avatarMode: 'mono', avatar: portrait(128, 160),
      codenameUnset: true, numberUnset: true);
    await tester.pumpWidget(MaterialApp(home: Scaffold(body: SingleChildScrollView(
      child: PassportEditor(controller: controller, canOperate: true,
        colorPassportSupported: true, busy: false, operation: const {},
        onRead: () async {}, onSave: (_, _) async {}, onCancel: () {}, onOpen: () {},
        saveDraft: (_) async {}, loadDraft: () async => null),
    ))));
    await tester.ensureVisible(find.text('调整头像'));
    await tester.tap(find.text('调整头像'));
    await tester.pump();
    expect(find.byType(PassportAvatarDialog), findsNothing);
    expect(controller.profile!.version, 1);
    expect(tester.widget<FilledButton>(find.widgetWithText(FilledButton, '发送资料')).onPressed, isNull);
    await tester.pumpWidget(const SizedBox());
    await tester.runAsync(() async { await Future<void>.delayed(const Duration(milliseconds: 100)); });
  });
}
