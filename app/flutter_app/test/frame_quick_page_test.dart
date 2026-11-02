import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_quick_page.dart';

void main() {
  testWidgets('Frame four entries open their own pages', (tester) async {
    final controller = FrameQuickController();
    addTearDown(controller.dispose);
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: FrameQuickPage(
              controller: controller,
              pickImages: ({required multiple}) async => [],
              captureImage: () async => null,
              sendFilm: (_, _) async {},
              cancelSend: () async {},
              canSend: false,
            ),
          ),
        ),
      ),
    );
    expect(find.text('拾光'), findsOneWidget);
    expect(find.text('定影'), findsOneWidget);
    expect(find.text('一言'), findsOneWidget);
    expect(find.text('批量上传'), findsOneWidget);
    await tester.tap(find.text('拾光'));
    await tester.pump();
    expect(find.text('选择照片'), findsOneWidget);
    expect(
      tester.widget<TextField>(find.byType(TextField)).controller!.text,
      'frame.film',
    );
    await tester.tap(find.text('返回'));
    await tester.pump();
    await tester.tap(find.text('定影'));
    await tester.pump();
    expect(find.text('打开相机'), findsOneWidget);
    expect(
      tester.widget<TextField>(find.byType(TextField)).controller!.text,
      'camera.film',
    );
  });

  testWidgets('batch picker keeps ordered items and removal releases bytes', (
    tester,
  ) async {
    final controller = FrameQuickController();
    addTearDown(controller.dispose);
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: FrameQuickPage(
              controller: controller,
              pickImages: ({required multiple}) async {
                expect(multiple, isTrue);
                return [
                  FramePhoto(name: 'first.jpg', bytes: Uint8List.fromList([1])),
                  FramePhoto(
                    name: 'second.jpg',
                    bytes: Uint8List.fromList([2]),
                  ),
                ];
              },
              captureImage: () async => null,
              sendFilm: (_, _) async {},
              cancelSend: () async {},
              canSend: true,
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('批量上传'));
    await tester.pump();
    await tester.tap(find.text('一次选多张'));
    await tester.pump();
    expect(controller.batch.map((item) => item.name).toList(), [
      'first.jpg',
      'second.jpg',
    ]);
    expect(
      FrameQuickController.batchName('batch_', 1, 'first.jpg'),
      'batch_01.film',
    );
    final removed = controller.batch.first;
    controller.removeBatch(removed.id);
    await tester.pump();
    expect(removed.bytes, isNull);
    expect(find.text('first.jpg'), findsNothing);
    expect(find.text('second.jpg'), findsOneWidget);
  });
}
