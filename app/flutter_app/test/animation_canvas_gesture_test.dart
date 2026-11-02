import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/animation_workshop.dart';

void main() {
  testWidgets(
    'pen canvas owns vertical and diagonal drags while outside scrolls',
    (tester) async {
      final controller = AnimationWorkshopController()..drawing = true;
      final scroll = ScrollController();
      addTearDown(controller.dispose);
      addTearDown(scroll.dispose);
      Widget page() => MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            controller: scroll,
            child: AnimationWorkshop(
              controller: controller,
              pickImages: ({required multiple}) async => [],
              sendFilm: (_, _) async {},
            ),
          ),
        ),
      );
      await tester.pumpWidget(page());
      final canvas = find.byWidgetPredicate(
      (w) => w is RawGestureDetector &&
          w.gestures.keys.any((type) => type.toString() == '_CanvasPanGestureRecognizer'),
      );
      // Use a point inside the visible portion of the canvas.
      final start = tester.getTopLeft(canvas) + const Offset(100, 80);
      await tester.dragFrom(start, const Offset(0, -60));
      await tester.pump();
      expect(controller.strokes, hasLength(1));
      expect(controller.strokes.single.length, greaterThan(1));
      expect(scroll.offset, 0);
      await tester.dragFrom(start, const Offset(50, -60));
      await tester.pump();
      expect(controller.strokes, hasLength(2));
      expect(scroll.offset, 0);
      // The margin beside the constrained canvas still belongs to the page.
      await tester.dragFrom(const Offset(20, 400), const Offset(0, -80));
      await tester.pumpAndSettle();
      expect(scroll.offset, greaterThan(0));
      expect(controller.strokes, hasLength(2));
      scroll.jumpTo(0);
      controller.busy = true;
      await tester.pumpWidget(page());
      await tester.dragFrom(start, const Offset(0, -60));
      await tester.pump();
      expect(controller.strokes, hasLength(2));
      scroll.jumpTo(0);
      controller.busy = false;
      controller.drawingTool = 'pick';
      await tester.pumpWidget(page());
      await tester.dragFrom(start, const Offset(0, -60));
      await tester.pumpAndSettle();
      expect(controller.strokes, hasLength(2));
      await tester.pumpWidget(const SizedBox());
    },
  );
}
