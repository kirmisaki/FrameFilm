import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

import 'widget_test.dart' show FakeGateway, openPage;

void main() {
  test('device suffix follows firmware UTF-8 byte and scalar boundaries', () {
    expect(validateDeviceSuffix(''), isNotNull);
    expect(validateDeviceSuffix('1234567890123456'), isNull);
    expect(validateDeviceSuffix('12345678901234567'), isNotNull);
    expect(validateDeviceSuffix('中文名字好a'), isNull);
    expect(validateDeviceSuffix('中文名字好ab'), isNotNull);
    expect(validateDeviceSuffix('😀😀😀😀'), isNull);
    expect(validateDeviceSuffix('😀😀😀😀a'), isNotNull);
    expect(validateDeviceSuffix('a\n'), isNotNull);
    expect(validateDeviceSuffix('a\u0085'), isNotNull);
    expect(
      validateDeviceSuffix('a/b'),
      isNull,
    ); // Firmware permits printable punctuation.
  });

  test('partial snapshots preserve device values; disconnect clears them', () {
    const previous = DeviceSnapshot(
      connected: true,
      name: 'FRAMEFILMARK-A',
      width: 720,
      height: 480,
      battery: 80,
      autoSleep: true,
      timedWake: false,
      wakeMinutes: 60,
      syncedAt: 123,
    );
    final partial = DeviceSnapshot.fromMap({'message': 'progress'})
        .retainingTransfer(previous);
    expect(partial.connected, isTrue);
    expect(partial.name, previous.name);
    expect(partial.battery, 80);
    expect(partial.width, 720);
    expect(partial.autoSleep, isTrue);
    expect(partial.timedWake, isFalse);
    expect(partial.wakeMinutes, 60);
    final cleared = DeviceSnapshot.fromMap({'connected': false})
        .retainingTransfer(partial);
    expect(cleared.name, isNull);
    expect(cleared.autoSleep, isNull);
    expect(cleared.wakeMinutes, isNull);
    expect(cleared.syncedAt, isNull);
  });

  testWidgets('settings commands respect transfer/import/confirmation locks', (
    tester,
  ) async {
    final gateway = FakeGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.events.close();
    });
    await tester.pump();
    for (final data in [
      {'connected': false},
      {'connected': true, 'importing': true},
      {'connected': true, 'importing': false, 'settingsBusy': true},
      {'connected': true, 'settingsBusy': false, 'importing': true},
      {
        'connected': true,
        'importing': false,
        'transfer': {'phase': 'downloading'},
      },
      {
        'connected': true,
        'transfer': {'phase': 'unconfirmed', 'canConfirm': true},
      },
      {
        'connected': true,
        'transfer': {
          'phase': 'error',
          'canRetry': true,
          'cleanupCompleted': false,
        },
      },
      {
        'connected': true,
        'transfer': {'phase': 'idle'},
        'firmwareDownload': {'canCancel': true},
      },
    ]) {
      gateway.events.add(DeviceSnapshot.fromMap(data));
      await tester.pump();
      final before = gateway.calls.length;
      for (final method in deviceSettingsMethods) {
        await controller.command(method);
      }
      expect(gateway.calls.length, before);
    }
  });

  testWidgets('preview settings save, refresh, disconnect and reset', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await tester.pump();
    await controller.command('connect');
    await tester.pump();
    expect(controller.snapshot.autoSleep, isTrue);
    await controller.command('setAutoSleep', {'enabled': false});
    await tester.pump();
    await controller.command('setTimedWake', {'enabled': true});
    await tester.pump();
    await controller.command('setWakeMinutes', {'minutes': 120});
    await tester.pump();
    await controller.command('renameDevice', {'suffix': '中文'});
    await tester.pump();
    await controller.command('syncTime');
    await tester.pump();
    expect(controller.snapshot.name, 'FRAMEFILMARK-中文');
    expect(controller.snapshot.autoSleep, isFalse);
    expect(controller.snapshot.timedWake, isTrue);
    expect(controller.snapshot.wakeMinutes, 120);
    expect(controller.snapshot.syncedAt, isNotNull);
    await controller.command('disconnect');
    await tester.pump();
    expect(controller.snapshot.name, isNull);
    expect(controller.snapshot.wakeMinutes, isNull);
    await controller.command('connect');
    await tester.pump();
    expect(controller.snapshot.wakeMinutes, 120);
    gateway.reset();
    await tester.pump();
    await controller.command('connect');
    await tester.pump();
    expect(controller.snapshot.wakeMinutes, 60);
    expect(controller.snapshot.name, 'FRAMEFILMARK-DEMO');
  });

  testWidgets('settings UI gates unread values and validates UTF-8', (
    tester,
  ) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    gateway.events.add(const DeviceSnapshot(connected: true));
    await tester.pump(const Duration(seconds: 2));
    await openPage(tester, '设置');
    await Scrollable.ensureVisible(
      tester.element(find.byKey(const Key('save-device-name'))),
      alignment: 0.5,
    );
    await tester.pumpAndSettle();
    expect(
      tester
          .widget<TextButton>(find.byKey(const Key('save-device-name')))
          .onPressed,
      isNull,
    );
    gateway.events.add(
      const DeviceSnapshot(
        connected: true,
        name: 'FRAMEFILMARK-TEST',
        autoSleep: true,
        timedWake: false,
        wakeMinutes: 60,
      ),
    );
    await tester.pump();
    await tester.enterText(
      find.byKey(const Key('device-name-suffix')),
      '😀😀😀😀a',
    );
    await tester.pump();
    expect(
      tester
          .widget<TextButton>(find.byKey(const Key('save-device-name')))
          .onPressed,
      isNull,
    );
    await tester.enterText(find.byKey(const Key('device-name-suffix')), '中文');
    await tester.pump();
    await Scrollable.ensureVisible(
      tester.element(find.byKey(const Key('save-device-name'))),
      alignment: 0.5,
    );
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const Key('save-device-name')));
    await tester.pump();
    expect(gateway.calls, contains('renameDevice'));
    await tester.pumpWidget(const SizedBox());
  });
}
