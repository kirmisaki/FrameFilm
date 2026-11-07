import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/github_release_service.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';
import 'package:framefilm_ark/app.dart';

import 'widget_test.dart' show FakeGateway, openPage;

void main() {
  test('partial download state retains and explicit idle clears', () {
    final previous = DeviceSnapshot.fromMap({
      'firmwareDownload': {'phase': 'downloading', 'canCancel': true},
      'importing': true,
    });
    final retained = DeviceSnapshot.fromMap({'connected': true})
        .retainingTransfer(previous);
    expect(retained.firmwareDownload.canCancel, isTrue);
    expect(retained.importing, isTrue);
    final cleared = DeviceSnapshot.fromMap({
      'firmwareDownload': {'phase': 'idle'},
      'importing': false,
    }).retainingTransfer(retained);
    expect(cleared.firmwareDownload.phase, 'idle');
    expect(cleared.importing, isFalse);
  });
  testWidgets('download cancel is reachable while controller is busy', (
    tester,
  ) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'importing': true,
        'firmwareDownload': {
          'phase': 'downloading',
          'canCancel': true,
          'total': 100,
          'received': 20,
        },
      }),
    );
    await tester.pump();
    await openPage(tester, '设置');
    await tester.scrollUntilVisible(
      find.byKey(const Key('cancel-firmware-download')),
      300,
      scrollable: find
          .descendant(
            of: find.byKey(const Key('workbench-content')),
            matching: find.byType(Scrollable),
          )
          .first,
    );
    await tester.ensureVisible(
      find.byKey(const Key('cancel-firmware-download')),
    );
    await tester.pump();
    await tester.tap(find.byKey(const Key('cancel-firmware-download')));
    await tester.pump();
    expect(gateway.calls, contains('cancelFirmwareDownload'));
    await tester.pumpWidget(const SizedBox());
  });
  test(
    'preview supports cancelled download and failure retry without OTA',
    () async {
      final gateway = PreviewDeviceGateway(
        stepDuration: const Duration(milliseconds: 1),
      );
      final controller = DeviceController(gateway);
      await Future<void>.delayed(Duration.zero);
      await controller.command('connect');
      await Future<void>.delayed(Duration.zero);
      await controller.command('pickFirmware');
      await Future<void>.delayed(Duration.zero);
      final old = controller.snapshot.importedFirmware;
      await controller.command(
        'downloadFirmware',
        GitHubFirmwareRelease.demo.downloadArguments,
      );
      await Future<void>.delayed(Duration.zero);
      controller.busy = true;
      await controller.command('cancelFirmwareDownload');
      controller.busy = false;
      await Future<void>.delayed(Duration.zero);
      expect(controller.snapshot.importedFirmware, same(old));
      expect(controller.snapshot.firmwareDownload.phase, 'cancelled');
      gateway.scenario = PreviewScenario.transferFailure;
      await controller.command(
        'downloadFirmware',
        GitHubFirmwareRelease.demo.downloadArguments,
      );
      await Future<void>.delayed(const Duration(milliseconds: 10));
      expect(controller.snapshot.firmwareDownload.phase, 'error');
      expect(controller.snapshot.importedFirmware, same(old));
      await controller.command(
        'downloadFirmware',
        GitHubFirmwareRelease.demo.downloadArguments,
      );
      await Future<void>.delayed(const Duration(milliseconds: 15));
      expect(controller.snapshot.firmwareDownload.phase, 'done');
      expect(controller.snapshot.importedFirmware!.version, contains('3.2.5'));
      expect(controller.snapshot.transfer.phase, 'idle');
      controller.dispose();
      gateway.dispose();
    },
  );
}
