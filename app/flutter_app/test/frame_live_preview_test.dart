import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:framefilm_ark/frame_editor.dart';
import 'package:framefilm_ark/live_image_composition.dart';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_codec.dart';

void main() {
  test('avatar mono shares Film conversion at avatar dimensions', () async {
    final rgba = Uint8List.fromList([0, 0, 0, 255, 255, 255, 255, 255]);
    final indices = await quantizeFrameMono(
      rgba,
      false,
      null,
      null,
      width: 2,
      height: 1,
    );
    expect(indices, [0, 1]);
    expect(rgba, [0, 0, 0, 255, 255, 255, 255, 255]);
  });
  testWidgets(
    'invalid live composition is recoverable and dispose cancels decode',
    (tester) async {
      Future<void> show(Uint8List bytes) => tester.pumpWidget(
        MaterialApp(
          home: SizedBox(
            width: 100,
            height: 150,
            child: LiveImageComposition(
              source: bytes,
              options: const FrameOptions(),
            ),
          ),
        ),
      );
      await show(Uint8List(0));
      await tester.pump();
      expect(find.text('图片预览不可用'), findsOneWidget);
      await tester.runAsync(() async {
        await show(Uint8List.fromList([1, 2, 3]));
        await Future<void>.delayed(const Duration(milliseconds: 30));
      });
      await tester.pump();
      expect(find.text('图片预览不可用'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
      expect(tester.takeException(), isNull);
    },
  );
  testWidgets(
    'cancel then retry while conversion exits generates the latest revision',
    (tester) async {
      late Uint8List png;
      await tester.runAsync(() async {
        final recorder = ui.PictureRecorder();
        Canvas(recorder).drawColor(Colors.red, BlendMode.src);
        final picture = recorder.endRecording();
        final image = await picture.toImage(8, 8);
        png = (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer
            .asUint8List();
        image.dispose();
        picture.dispose();
      });
      final c = FrameEditorController()..source = png;
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(
              child: FrameEditor(
                controller: c,
                canImportFilm: false,
                onImportFilm: (_, _) async {},
              ),
            ),
          ),
        ),
      );
      final cancel = tester.widget<TextButton>(
        find.widgetWithText(TextButton, '取消生成'),
      );
      cancel.onPressed!();
      await tester.pump();
      final retry = tester.widget<TextButton>(
        find.widgetWithText(TextButton, '重新生成'),
      );
      retry.onPressed!();
      for (var i = 0; i < 100 && !c.hasCurrentResult; i++) {
        await tester.pump(const Duration(milliseconds: 80));
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 50)),
        );
      }
      await tester.pump();
      expect(c.hasCurrentResult, isTrue);
      await tester.pumpWidget(const SizedBox());
      c.dispose();
      expect(tester.takeException(), isNull);
    },
  );
}
