import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/animation_drawing.dart';
import 'package:framefilm_ark/frame_codec.dart';

void main() {
  test(
    'bucket fills connected pixels while retaining boundaries and source',
    () async {
      final pixels = Uint8List.fromList([
        255,
        255,
        255,
        255,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        255,
        0,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
      ]);
      final filled = await floodAnimationPixels(pixels, 3, 2, 0, 0, 0xffff0000);
      expect(animationPixelColor(filled, 3, 2, 0, 0), 0xffff0000);
      expect(animationPixelColor(filled, 3, 2, 0, 1), 0xffff0000);
      expect(animationPixelColor(filled, 3, 2, 1, 0), 0xff000000);
      expect(animationPixelColor(filled, 3, 2, 2, 0), 0xffffffff);
      expect(animationPixelColor(pixels, 3, 2, 0, 0), 0xffffffff);
      expect(
        await floodAnimationPixels(filled, 3, 2, 0, 0, 0xffff0000),
        filled,
      );
    },
  );
  test(
    'bucket respects cancellation and rejects outside pixel coordinates',
    () async {
      final pixels = Uint8List(4);
      await expectLater(
        floodAnimationPixels(
          pixels,
          1,
          1,
          0,
          0,
          0xffffffff,
          isCancelled: () => true,
        ),
        throwsA(isA<FrameCancelledException>()),
      );
      expect(
        () => animationPixelColor(pixels, 1, 1, 1, 0),
        throwsArgumentError,
      );
    },
  );
}
