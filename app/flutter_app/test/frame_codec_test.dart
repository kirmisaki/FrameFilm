import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';

import 'package:framefilm_ark/frame_codec.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('portrait corners, little endian header and high nibble first', () {
    // logical rows: black white / yellow red / blue green
    // physical rows: blue yellow black / green red white
    final film = packFramePixels(
      Uint8List.fromList([0, 1, 2, 3, 4, 5]),
      width: 2,
      height: 3,
    );
    final header = ByteData.sublistView(film);
    expect(header.getUint32(0, Endian.little), 3);
    expect(header.getUint16(4, Endian.little), 3);
    expect(header.getUint16(6, Endian.little), 2);
    expect(film.sublist(32), [0x42, 0x05, 0x31]);
    expect(film.sublist(16, 22), [0, 255, 252, 224, 3, 28]);
  });
  test('MonoFast black is one, MSB first, fixed Ark dimensions', () {
    final pixels = Uint8List(480 * 720)..fillRange(0, 480 * 720, 1);
    pixels[719 * 480] = 0; // physical first bit
    pixels[718 * 480] = 0; // physical second bit
    final film = packFramePixels(pixels, format: FrameFormat.monoFast);
    expect(film.length, 43232);
    expect(film[9], 1);
    expect(film[32], 0xc0);
    expect(ByteData.sublistView(film).getUint16(4, Endian.little), 720);
  });
  test('PNG preview agrees with film and conversion can cancel', () async {
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    canvas.drawColor(const ui.Color(0xffffffff), ui.BlendMode.src);
    canvas.drawRect(
      const ui.Rect.fromLTWH(0, 0, 1, 2),
      ui.Paint()..color = const ui.Color(0xff000000),
    );
    final picture = recorder.endRecording();
    final image = await picture.toImage(2, 2);
    final source = (await image.toByteData(format: ui.ImageByteFormat.png))!
        .buffer
        .asUint8List();
    image.dispose();
    picture.dispose();
    final result = await convertFrame(
      source,
      const FrameOptions(format: FrameFormat.monoFast),
    );
    final codec = await ui.instantiateImageCodec(result.png);
    final preview = (await codec.getNextFrame()).image;
    final rgba = (await preview.toByteData(format: ui.ImageByteFormat.rawRgba))!
        .buffer
        .asUint8List();
    for (var y = 0; y < 720; y += 71) {
      for (var x = 0; x < 480; x += 47) {
        final physical = x * 720 + 719 - y;
        final black =
            result.film[32 + physical ~/ 8] & (128 >> (physical % 8)) != 0;
        expect(rgba[(y * 480 + x) * 4], black ? 0 : 255);
      }
    }
    preview.dispose();
    codec.dispose();
    var cancel = false;
    await expectLater(
      convertFrame(
        source,
        const FrameOptions(),
        onProgress: (_) {
          cancel = true;
        },
        isCancelled: () => cancel,
      ),
      throwsA(isA<FrameCancelledException>()),
    );
  });
}
