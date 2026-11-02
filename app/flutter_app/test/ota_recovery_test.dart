import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';

import 'widget_test.dart' show FakeGateway;

void main() {
  testWidgets(
    'OTA reconnect cleanup unlocks Film and firmware retry without restart',
    (tester) async {
      tester.view.physicalSize = const Size(1440, 1080);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      Future<void> openPage(WidgetTester tester, String title) async {
        await tester.tap(find.text(title).first);
        await tester.pumpAndSettle();
      }

      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': false,
          'importedFile': {'name': 'HT27A001.film', 'size': 47347232},
          'importedFirmware': {
            'name': 'ark.bin',
            'size': 1939280,
            'elfSha256': 'target-build',
          },
          'transfer': {
            'kind': 'firmware',
            'phase': 'error',
            'message': 'Ark 恢复终态未确认',
            'canRetry': true,
            'cleanupCompleted': false,
          },
        }),
      );
      await tester.pump(const Duration(seconds: 2));
      await openPage(tester, 'Film');
      Future<void> show(Key key) async {
        await tester.scrollUntilVisible(
          find.byKey(key),
          300,
          scrollable: find
              .descendant(
                of: find.byKey(const Key('workbench-content')),
                matching: find.byType(Scrollable),
              )
              .first,
        );
        await tester.pump();
      }

      await show(const Key('start-transfer'));
      expect(
        tester
            .widget<FilledButton>(find.byKey(const Key('start-transfer')))
            .onPressed,
        isNull,
      );

      // A reconnect alone is insufficient while the native cleanup is still running.
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'transfer': {
            'kind': 'firmware',
            'phase': 'cleanup',
            'cleanupCompleted': false,
          },
        }),
      );
      await tester.pump();
      expect(
        tester
            .widget<OutlinedButton>(find.byKey(const Key('pick-film')))
            .onPressed,
        isNull,
      );

      // Only the native terminal-state confirmation releases the controls.
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'transfer': {
            'kind': 'firmware',
            'phase': 'error',
            'message': '上次升级中断，清理完成，可重试',
            'canRetry': true,
            'cleanupCompleted': true,
          },
        }),
      );
      await tester.pump();
      expect(find.text('HT27A001.film'), findsOneWidget);
      expect(
        tester
            .widget<OutlinedButton>(find.byKey(const Key('pick-film')))
            .onPressed,
        isNotNull,
      );
      await tester.tap(find.byKey(const Key('start-transfer')));
      await tester.pump();
      expect(gateway.calls, contains('startTransfer'));

      await openPage(tester, '设置');
      await show(const Key('retry-transfer'));
      expect(find.text('上次升级中断，清理完成，可重试'), findsOneWidget);
      expect(find.text('固件构建已确认'), findsNothing);
      await tester.tap(find.byKey(const Key('retry-transfer')));
      await tester.pump();
      expect(gateway.calls, contains('retryTransfer'));
      await tester.pumpWidget(const SizedBox());
    },
  );
}
