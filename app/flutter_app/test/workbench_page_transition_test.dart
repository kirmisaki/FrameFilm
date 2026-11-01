import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/workbench_page_transition.dart';

void main() {
  for (final reduced in [false, true]) {
    testWidgets('rapid navigation, reduced motion: $reduced', (tester) async {
      var index = 0;
      late StateSetter update;
      final controller = TextEditingController(text: 'draft');
      addTearDown(controller.dispose);
      final fieldKey = GlobalKey();
      await tester.pumpWidget(
        MaterialApp(
          home: MediaQuery(
            data: MediaQueryData(disableAnimations: reduced),
            child: StatefulBuilder(
              builder: (context, setState) {
                update = setState;
                return WorkbenchPageTransition(
                  index: index,
                  child: KeyedSubtree(
                    key: ValueKey(index),
                    child: index == 0
                        ? Material(
                            child: TextField(
                              key: fieldKey,
                              controller: controller,
                            ),
                          )
                        : Text('page $index'),
                  ),
                );
              },
            ),
          ),
        ),
      );
      update(() => index = 1);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 60));
      final opacity = tester
          .widget<Opacity>(find.byType(Opacity).first)
          .opacity;
      if (reduced) {
        expect(opacity, 1);
        expect(tester.hasRunningAnimations, isFalse);
      } else {
        expect(opacity, greaterThan(0));
        expect(opacity, lessThan(1));
      }
      update(() => index = 2);
      await tester.pump();
      update(() => index = 0);
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 240));
      expect(find.text('draft'), findsOneWidget);
      expect(tester.widget<Opacity>(find.byType(Opacity).first).opacity, 1);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    });
  }
}
