import 'dart:convert';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:framefilm_ark/live_image_composition.dart';
import 'package:framefilm_ark/frame_codec.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/passport_avatar_editor.dart';

void main() {
  const source = 'data:image/png;base64,AA==';
  test('legacy avatar options acquire disabled fitting defaults', () {
    final edit = PassportAvatarEdit.fromJson({
      'source': source,
      'options': {'algorithm': 'adaptive'},
    });
    expect(edit.options['rangeFit'], 0);
    expect(edit.options['warmChroma'], 0);
    expect(edit.options['algorithm'], 'adaptive');
  });
  test('avatar source fitting options roundtrip and validate bounds', () {
    final edit = PassportAvatarEdit.fromJson(
      PassportAvatarEdit(source, {'rangeFit': .4, 'warmChroma': .7}).toJson(),
    );
    expect(edit.options['rangeFit'], .4);
    expect(edit.options['warmChroma'], .7);
    for (final key in ['rangeFit', 'warmChroma']) {
      for (final value in [-.1, 1.1, double.nan]) {
        expect(
          () => PassportAvatarEdit.fromJson({
            'source': source,
            'options': {key: value},
          }),
          throwsFormatException,
        );
      }
    }
  });
  testWidgets('avatar live composition receives saved resampling', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1600, 2400);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    late String png;
    await tester.runAsync(() async {
      final recorder = ui.PictureRecorder();
      Canvas(recorder).drawColor(Colors.orange, BlendMode.src);
      final picture = recorder.endRecording();
      final image = await picture.toImage(8, 8);
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      png = 'data:image/png;base64,${base64Encode(data!.buffer.asUint8List())}';
      image.dispose();
      picture.dispose();
    });
    for (final sampling in ImageResampling.values) {
      await tester.pumpWidget(
        MaterialApp(
          home: PassportAvatarDialog(
            key: ValueKey(sampling),
            mode: 'color',
            edit: PassportAvatarEdit(png, {
              'resampling': sampling.name,
              'warmChroma': .2,
            }),
          ),
        ),
      );
      final live = tester.widget<LiveImageComposition>(
        find.byKey(const ValueKey('avatar-live-composition')),
      );
      expect(live.options.resampling, sampling);
      await tester.pumpWidget(const SizedBox());
      await tester.pump();
    }
  });
}
