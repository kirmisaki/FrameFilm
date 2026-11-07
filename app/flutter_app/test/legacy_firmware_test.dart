import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/github_release_service.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

import 'widget_test.dart' show FakeGateway, openPage;

class LegacyGateway extends FakeGateway {
  final arguments = <String, Map<String, Object?>?>{};
  @override
  Future<void> invoke(String method, [Map<String, Object?>? values]) async {
    arguments[method] = values;
    await super.invoke(method, values);
  }
}

Map<Object?, Object?> legacyState({
  String deviceId = 'old-a',
  HardwareIdentity target = HardwareIdentity.n4r2,
  bool supported = false,
  bool retry = false,
}) => {
  'connected': true,
  'deviceId': deviceId,
  'hardware': null,
  'hardwareIdentitySupported': supported,
  'importedFirmware': {
    'name': '${target.targetId}.bin',
    'project': 'frame_film_ark',
    'size': 123,
    'elfSha256': 'b' * 64,
    'fileSha256': 'a' * 64,
    ...target.packageMetadata,
  },
  'transfer': {
    'kind': 'firmware',
    'phase': retry ? 'error' : 'idle',
    'canRetry': retry,
    'cleanupCompleted': true,
  },
};

Future<void> reveal(WidgetTester tester, Finder finder) async {
  await tester.scrollUntilVisible(
    finder,
    240,
    scrollable: find
        .descendant(
          of: find.byKey(const Key('workbench-content')),
          matching: find.byType(Scrollable),
        )
        .first,
  );
  await tester.ensureVisible(finder);
  await tester.pumpAndSettle();
}

void main() {
  test(
    'manual target is explicit, connection scoped and cannot override identity',
    () async {
      final gateway = LegacyGateway();
      final device = DeviceController(gateway);
      addTearDown(() async {
        device.dispose();
        await gateway.events.close();
      });
      await Future<void>.delayed(Duration.zero);
      gateway.events.add(DeviceSnapshot.fromMap(legacyState()));
      expect(device.firmwareTarget, isNull);
      await device.command('startFirmwareTransfer', {
        'legacyHardwareTarget': HardwareIdentity.n4r2.targetId,
      });
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      device.selectLegacyHardware(HardwareIdentity.n16r8.targetId);
      await device.command('startFirmwareTransfer', {
        'legacyHardwareTarget': HardwareIdentity.n16r8.targetId,
      });
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      device.selectLegacyHardware(HardwareIdentity.n4r2.targetId);
      await device.command('startFirmwareTransfer');
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      await device.command('startFirmwareTransfer', {
        'legacyHardwareTarget': HardwareIdentity.n4r2.targetId,
      });
      expect(
        gateway.arguments['startFirmwareTransfer']!['legacyHardwareTarget'],
        HardwareIdentity.n4r2.targetId,
      );
      gateway.events.add(
        DeviceSnapshot.fromMap(legacyState(deviceId: 'old-b')),
      );
      expect(device.legacyHardwareTarget, isNull);
      device.selectLegacyHardware(HardwareIdentity.n16r8.targetId);
      await device.command('downloadFirmware', {
        ...GitHubFirmwareRelease.demoFor(HardwareIdentity.n16r8)
            .downloadArguments,
        'legacyHardwareTarget': HardwareIdentity.n16r8.targetId,
      });
      expect(gateway.calls, contains('downloadFirmware'));
      gateway.events.add(DeviceSnapshot.fromMap({'connected': false}));
      expect(device.legacyHardwareTarget, isNull);
      gateway.events.add(DeviceSnapshot.fromMap(legacyState(supported: true)));
      device.selectLegacyHardware(HardwareIdentity.n4r2.targetId);
      expect(device.firmwareTarget, isNull);
      gateway.events.add(
        DeviceSnapshot.fromMap({
          ...legacyState(),
          'hardware': HardwareIdentity.n16r8.toMap(),
        }),
      );
      device.selectLegacyHardware(HardwareIdentity.n4r2.targetId);
      expect(device.firmwareTarget!.targetId, HardwareIdentity.n16r8.targetId);
    },
  );

  testWidgets(
    'start and retry each require confirmation; cancel sends nothing',
    (tester) async {
      final gateway = LegacyGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      gateway.events.add(DeviceSnapshot.fromMap(legacyState()));
      await tester.pumpAndSettle();
      await openPage(tester, '设置');
      final choice = find.byKey(
        Key('legacy-${HardwareIdentity.n4r2.targetId}'),
      );
      await reveal(tester, choice);
      expect(tester.widget<ChoiceChip>(choice).selected, false);
      await tester.tap(choice);
      await tester.pumpAndSettle();
      final start = find.byKey(const Key('start-firmware'));
      await reveal(tester, start);
      await tester.tap(start);
      await tester.pumpAndSettle();
      expect(find.textContaining('旧固件无法自动验证硬件型号'), findsOneWidget);
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      await tester.tap(find.widgetWithText(TextButton, '取消'));
      await tester.pumpAndSettle();
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      await tester.tap(start);
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const Key('confirm-legacy-firmware')));
      await tester.pumpAndSettle();
      expect(
        gateway.arguments['startFirmwareTransfer']!['legacyHardwareTarget'],
        HardwareIdentity.n4r2.targetId,
      );
      gateway.events.add(DeviceSnapshot.fromMap(legacyState(retry: true)));
      await tester.pumpAndSettle();
      final retry = find.byKey(const Key('retry-transfer'));
      await reveal(tester, retry);
      await tester.tap(retry);
      await tester.pumpAndSettle();
      expect(gateway.calls, isNot(contains('retryTransfer')));
      await tester.tap(find.byKey(const Key('confirm-legacy-firmware')));
      await tester.pumpAndSettle();
      expect(
        gateway.arguments['retryTransfer']!['legacyHardwareTarget'],
        HardwareIdentity.n4r2.targetId,
      );
      await tester.pumpWidget(const SizedBox());
    },
  );

  testWidgets(
    'connection replacement during confirmation invalidates consent',
    (tester) async {
      final gateway = LegacyGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      gateway.events.add(DeviceSnapshot.fromMap(legacyState()));
      await tester.pumpAndSettle();
      await openPage(tester, '设置');
      final choice = find.byKey(
        Key('legacy-${HardwareIdentity.n4r2.targetId}'),
      );
      await reveal(tester, choice);
      await tester.tap(choice);
      await tester.pumpAndSettle();
      await reveal(tester, find.byKey(const Key('start-firmware')));
      await tester.tap(find.byKey(const Key('start-firmware')));
      await tester.pumpAndSettle();
      gateway.events.add(
        DeviceSnapshot.fromMap(legacyState(deviceId: 'replacement')),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const Key('confirm-legacy-firmware')));
      await tester.pumpAndSettle();
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      await tester.pumpWidget(const SizedBox());
    },
  );

  testWidgets('preview legacy download uses manually chosen n16r8 descriptor', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final device = DeviceController(gateway);
    gateway.reset(PreviewScenario.legacyFirmware);
    await tester.pump();
    await device.command('connect');
    await tester.pump();
    expect(device.snapshot.hardware, isNull);
    device.selectLegacyHardware(HardwareIdentity.n16r8.targetId);
    await device.command('downloadFirmware', {
      ...GitHubFirmwareRelease.demoFor(HardwareIdentity.n16r8)
          .downloadArguments,
      'legacyHardwareTarget': HardwareIdentity.n16r8.targetId,
    });
    for (var i = 0; i < 6; i++) {
      await tester.pump(const Duration(milliseconds: 10));
    }
    expect(
      device.snapshot.importedFirmware!.matchesHardware(HardwareIdentity.n16r8),
      true,
    );
    device.dispose();
    gateway.dispose();
  });
}
