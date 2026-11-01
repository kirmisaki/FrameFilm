
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';

import 'widget_test.dart' show FakeGateway, openPage;

Map<Object?, Object?> firmwareState({
  String phase = 'idle',
  bool connected = true,
}) => {
  'connected': connected,
  'importedFirmware': {
    'name': 'frame_film_ark.bin',
    'size': 1853248,
    'version': '3.2.5',
    'project': 'frame_film_ark',
    'elfSha256': 'abcd1234abcd1234',
    'fileSha256': 'a' * 64,
  },
  'transfer': {'kind': 'firmware', 'phase': phase, 'cleanupCompleted': true},
};

void main() {
  testWidgets(
    'firmware import and confirmation do not report download as success',
    (tester) async {
      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      gateway.events.add(DeviceSnapshot.fromMap(firmwareState()));
      await tester.pump(const Duration(seconds: 2));
      await openPage(tester, '设置');
      await tester.ensureVisible(find.byKey(const Key('start-firmware')));
      await tester.pump();
      expect(find.text('固件版本：3.2.5'), findsOneWidget);
      await tester.tap(find.byKey(const Key('start-firmware')));
      await tester.pump();
      expect(gateway.calls, contains('startFirmwareTransfer'));
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': false,
          'transfer': {
            'kind': 'firmware',
            'phase': 'rebooting',
            'received': 100,
            'total': 100,
          },
        }),
      );
      await tester.pump();
      expect(find.text('固件构建已确认'), findsNothing);
      expect(
        tester
            .widget<FilledButton>(find.byKey(const Key('start-firmware')))
            .onPressed,
        isNull,
      );
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': false,
          'transfer': {
            'kind': 'firmware',
            'phase': 'unconfirmed',
            'canConfirm': true,
            'cleanupCompleted': true,
          },
        }),
      );
      await tester.pump();
      await tester.ensureVisible(find.byKey(const Key('confirm-firmware')));
      await tester.pump();
      await tester.tap(find.byKey(const Key('confirm-firmware')));
      await tester.pump();
      expect(gateway.calls, contains('confirmFirmwareTransfer'));
      expect(
        tester
            .widget<FilledButton>(find.byKey(const Key('retry-transfer')))
            .onPressed,
        isNull,
      );
      await tester.pumpWidget(const SizedBox());
    },
  );

  test('firmware controller preserves target during reboot and only confirms after apply', () async {
    final gateway = FakeGateway();
    final controller = DeviceController(gateway);
    await Future<void>.delayed(Duration.zero);
    gateway.events.add(DeviceSnapshot.fromMap(firmwareState()));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': false,
        'transfer': {
          'kind': 'firmware',
          'phase': 'unconfirmed',
          'canConfirm': true,
          'cleanupCompleted': true,
        },
      }),
    );
    expect(controller.snapshot.importedFirmware?.elfSha256, 'abcd1234abcd1234');
    for (final method in [
      'startTransfer',
      'startFirmwareTransfer',
      'pickFirmware',
      'clearFirmware',
      'retryTransfer',
      'cancelTransfer',
    ]) {
      await controller.command(method);
      expect(gateway.calls, isNot(contains(method)));
    }
    await controller.command('confirmFirmwareTransfer');
    expect(gateway.calls, contains('confirmFirmwareTransfer'));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'transfer': {
          'kind': 'firmware',
          'phase': 'cancelled',
          'canRetry': true,
          'cleanupCompleted': true,
        },
      }),
    );
    await controller.command('retryTransfer');
    expect(gateway.calls, contains('retryTransfer'));
    controller.dispose();
    await gateway.events.close();
  });
}
