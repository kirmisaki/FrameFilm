import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_abc9.dart';
import 'package:framefilm_ark/student_passport_profile.dart';
import 'package:image/image.dart' as img;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('student JSON keeps its v2 schema and defaults', () {
    final raw = jsonDecode(const StudentPassportProfile().toJson()) as Map;
    raw.remove('palette');
    raw.remove('studentOutput');
    final profile = StudentPassportProfile.fromJson(jsonEncode(raw));
    expect(profile.palette, 'amiya');
    expect(profile.studentOutput, 'i006');
    expect(
      jsonDecode(profile.toJson()),
      containsPair('template', 'rhodes-academy-student'),
    );
    expect(
      StudentPassportProfile.fromJson(
        jsonEncode({...raw, 'studentOutput': 'ffui'}),
      ).studentOutput,
      'ffui',
    );
    expect(
      () => StudentPassportProfile.fromJson(
        jsonEncode({...raw, 'palette': null}),
      ),
      throwsFormatException,
    );
    expect(
      studentPassportLayoutErrors(
        const StudentPassportProfile(
          codename: 'A',
          studentNumber: '202302290229',
        ),
      ),
      contains('studentNumber'),
    );
    expect(
      studentPassportLayoutErrors(
        const StudentPassportProfile(
          codename: 'A',
          studentNumber: '202402290229',
        ),
      ),
      isEmpty,
    );
  });

  test('student I006 preserves A watermark and native rotation', () async {
    final source = img.Image(width: 720, height: 480, numChannels: 4);
    img.fill(source, color: img.ColorRgba8(255, 255, 255, 255));
    // This isolated pale neutral patch is entirely in the A mask.
    img.fillRect(
      source,
      x1: 490,
      y1: 200,
      x2: 509,
      y2: 219,
      color: img.ColorRgba8(244, 244, 244, 255),
    );
    final rendered = await renderStudentPassport(
      const StudentPassportProfile(
        codenameUnset: true,
        studentNumberUnset: true,
      ),
      img.encodePng(source),
    );
    validateAbc9Film(rendered.bin);
    final payload = ByteData.sublistView(rendered.bin);
    int code(int x, int y) => payload.getUint16(
      32 + 2 * (720 * 480 - 1 - (720 * y + x)),
      Endian.little,
    );
    var grayCount = 0;
    for (var y = 200; y < 220; y++) {
      for (var x = 490; x < 510; x++) {
        final value = code(x, y);
        expect(value, anyOf(75, 407));
        if (value == 407) grayCount++;
      }
    }
    expect(grayCount, inInclusiveRange(125, 155));
    final codec = await ui.instantiateImageCodec(rendered.png);
    final image = (await codec.getNextFrame()).image;
    expect((image.width, image.height), (720, 480));
    image.dispose();
    codec.dispose();
  });
}
