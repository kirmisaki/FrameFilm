import 'dart:convert';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/animation_media.dart';
import 'package:framefilm_ark/frame_codec.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final gif = base64Decode(
    'R0lGODlhAgACAIEAAAAAAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBCgABACwAAAAAAgACAIH///8AAAAAAAAAAAAIBgABCAQQEAA7',
  );
  test(
    'GIF imports distinct composited frames and respects frame limit',
    () async {
      final frames = await decodeAnimationImages(gif);
      expect(frames, hasLength(2));
      for (var i = 0; i < frames.length; i++) {
        final codec = await ui.instantiateImageCodec(frames[i]);
        final image = (await codec.getNextFrame()).image;
        final rgba = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
        expect(rgba!.getUint8(0), i == 0 ? 0 : 255);
        image.dispose();
        codec.dispose();
      }
      expect(await decodeAnimationImages(gif, limit: 1), hasLength(1));
    },
  );
  test('cancelled GIF import does not return partial frames', () async {
    await expectLater(
      decodeAnimationImages(gif, isCancelled: () => true),
      throwsA(isA<FrameCancelledException>()),
    );
  });
}
