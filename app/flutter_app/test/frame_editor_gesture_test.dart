import 'dart:typed_data';

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_codec.dart';
import 'package:framefilm_ark/frame_editor.dart';

void main() {
  Future<FrameEditorController> mount(
    WidgetTester tester,
    ScrollController scroll,
  ) async {
    tester.view.physicalSize = const Size(1200, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final c = FrameEditorController()
      ..source = Uint8List(0)
      ..options = const FrameOptions(zoom: 2, panX: 30, panY: -20)
      ..result = FrameResult(
        png: Uint8List(0),
        film: Uint8List(0),
        format: FrameFormat.sixColor,
      )
      ..resultRevision = 0;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            controller: scroll,
            child: Column(
              children: [
                FrameEditor(
                  controller: c,
                  canImportFilm: false,
                  onImportFilm: (_, _) async {},
                ),
                const SizedBox(height: 2000),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pump();
    return c;
  }

  testWidgets(
    'pinch preserves content under moving focal point including zoom clamp',
    (tester) async {
      final scroll = ScrollController();
      final c = await mount(tester, scroll);
      final target = find.byKey(const ValueKey('film-gestures'));
      final size = tester.getSize(target);
      final dynamic gesture = tester.widget(target);
      final start = Offset(size.width * .25, size.height * .4);
      final focal = start + const Offset(10, 20);
      gesture.onScaleStart(ScaleStartDetails(localFocalPoint: start));
      gesture.onScaleUpdate(
        ScaleUpdateDetails(localFocalPoint: focal, scale: 1.5),
      );
      final factor = 480 / size.width;
      final anchoredBefore = (start * factor - const Offset(270, 340)) / 2;
      var anchoredAfter =
          (focal * factor -
              Offset(240 + c.options.panX, 360 + c.options.panY)) /
          c.options.zoom;
      expect((anchoredAfter - anchoredBefore).distance, lessThan(.00001));
      gesture.onScaleUpdate(
        ScaleUpdateDetails(localFocalPoint: focal, scale: 100),
      );
      anchoredAfter =
          (focal * factor -
              Offset(240 + c.options.panX, 360 + c.options.panY)) /
          c.options.zoom;
      expect(c.options.zoom, 10);
      expect((anchoredAfter - anchoredBefore).distance, lessThan(.00001));
      await tester.pumpWidget(const SizedBox());
      c.dispose();
      scroll.dispose();
    },
  );
  testWidgets('wheel zoom anchors mouse and consumes parent scrolling', (
    tester,
  ) async {
    final scroll = ScrollController();
    final c = await mount(tester, scroll);
    scroll.jumpTo(30);
    await tester.pump();
    final target = find.byKey(const ValueKey('film-preview-events'));
    final rect = tester.getRect(target);
    final position = rect.topLeft + Offset(rect.width * .3, rect.height * .3);
    final local = (position - rect.topLeft) * (480 / rect.width);
    final before = (local - const Offset(270, 340)) / 2;
    final oldOffset = scroll.offset;
    await tester.sendEventToBinding(
      PointerScrollEvent(position: position, scrollDelta: const Offset(0, 60)),
    );
    await tester.pump();
    expect(c.options.zoom, closeTo(1.8, .00001));
    final after =
        (local - Offset(240 + c.options.panX, 360 + c.options.panY)) /
        c.options.zoom;
    expect((after - before).distance, lessThan(.00001));
    expect(scroll.offset, oldOffset);
    await tester.pumpWidget(const SizedBox());
    c.dispose();
    scroll.dispose();
  });
}
