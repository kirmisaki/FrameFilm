import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

void main() {
  testWidgets('preview connects, cancels, retries and clears film metadata', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    await tester.pump();
    await controller.command('scan');
    await tester.pump();
    expect(controller.snapshot.devices.single.address, 'DEMO-ARK');
    await controller.command('connect');
    await controller.command('pickFilm');
    await tester.pump();
    expect(controller.snapshot.importedFile?.width, 720);
    await controller.command('startTransfer');
    await tester.pump();
    await controller.command('cancelTransfer');
    await tester.pump();
    expect(controller.snapshot.transfer.phase, 'cancelled');
    await controller.command('retryTransfer');
    for (var i = 0; i < 6; i++) {
      await tester.pump(const Duration(milliseconds: 10));
    }
    expect(controller.snapshot.transfer.success, isTrue);
    await controller.command('clearFilm');
    await controller.command('disconnect');
    await tester.pump();
    expect(controller.snapshot.importedFile, isNull);
    expect(controller.snapshot.connected, isFalse);
    controller.dispose();
    gateway.dispose();
  });

  testWidgets('a simulated BLE photo stays on BLE through retry', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    await tester.pump();
    await controller.command('connect');
    await controller.command('pickFilm');
    await tester.pump();
    await controller.command('startTransfer', {'transportPreference': 'ble'});
    await tester.pump();
    expect(controller.snapshot.transfer.transport, 'ble');
    await controller.command('cancelTransfer');
    await tester.pump();
    expect(controller.snapshot.transfer.phase, 'cancelled');
    await controller.command('retryTransfer', {'transportPreference': 'auto'});
    for (var i = 0; i < 6; i++) {
      await tester.pump(const Duration(milliseconds: 10));
    }
    expect(controller.snapshot.transfer.transport, 'ble');
    expect(controller.snapshot.transfer.confirmation, 'directory_presence');
    expect(controller.snapshot.transfer.success, isTrue);
    controller.dispose();
    gateway.dispose();
  });

  testWidgets('simulated failure is recoverable by retry', (tester) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    gateway.reset(PreviewScenario.transferFailure);
    await tester.pump();
    await controller.command('connect');
    await controller.command('pickFilm');
    await tester.pump();
    await controller.command('startTransfer');
    for (var i = 0; i < 4; i++) {
      await tester.pump(const Duration(milliseconds: 10));
    }
    expect(controller.snapshot.transfer.phase, 'error');
    expect(controller.snapshot.transfer.cleanupCompleted, isTrue);
    await controller.command('retryTransfer');
    for (var i = 0; i < 6; i++) {
      await tester.pump(const Duration(milliseconds: 10));
    }
    expect(controller.snapshot.transfer.success, isTrue);
    controller.dispose();
    gateway.dispose();
  });

  testWidgets('OTA awaits confirmation and same build skips upload', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    gateway.reset(PreviewScenario.unconfirmed);
    await tester.pump();
    await controller.command('connect');
    await controller.command('pickFirmware');
    await tester.pump();
    await controller.command('startFirmwareTransfer');
    for (var i = 0; i < 9; i++) {
      await tester.pump(const Duration(milliseconds: 10));
    }
    expect(controller.snapshot.transfer.phase, 'unconfirmed');
    expect(controller.snapshot.transfer.success, isFalse);
    await controller.command('confirmFirmwareTransfer');
    await tester.pump();
    expect(controller.snapshot.transfer.success, isTrue);
    expect(controller.snapshot.connected, isTrue);
    gateway.reset(PreviewScenario.sameBuild);
    await tester.pump();
    await controller.command('connect');
    await controller.command('pickFirmware');
    await tester.pump();
    await controller.command('startFirmwareTransfer');
    await tester.pump();
    expect(controller.snapshot.transfer.message, contains('跳过升级'));
    expect(controller.snapshot.transfer.total, 0);
    controller.dispose();
    gateway.dispose();
  });

  testWidgets('BLE OTA is advertised and verifies the running build', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    addTearDown(gateway.dispose);
    addTearDown(controller.dispose);
    await tester.pump();
    await controller.command('connect');
    await controller.command('pickFirmware');
    await tester.pump();
    expect(controller.snapshot.bleFirmwareAvailable, isTrue);
    await controller.command('startFirmwareTransfer', {
      'transportPreference': 'ble',
    });
    await tester.pump(const Duration(milliseconds: 20));
    expect(controller.snapshot.transfer.transport, 'ble');
    expect(controller.snapshot.transfer.phase, 'sending');
    await tester.pump(const Duration(milliseconds: 80));
    expect(controller.snapshot.transfer.confirmation, 'elf_sha256');
    expect(controller.snapshot.transfer.success, isTrue);
    await controller.command('startFirmwareTransfer', {
      'transportPreference': 'ble',
    });
    await tester.pump();
    expect(controller.snapshot.transfer.transport, 'ble');
    expect(controller.snapshot.transfer.message, contains('跳过升级'));
  });

  testWidgets('legacy Auto routes ordinary photos through BLE', (tester) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    addTearDown(gateway.dispose);
    addTearDown(controller.dispose);
    gateway.reset(PreviewScenario.legacyFirmware);
    await tester.pump();
    await controller.command('connect');
    await controller.command('pickFilm');
    await tester.pump();
    expect(controller.snapshot.effectivePhotoTransport, 'ble');
    expect(controller.snapshot.effectiveFirmwareTransport, 'wifi');
    expect(controller.snapshot.bleFirmwareAvailable, isFalse);
    await controller.command('startTransfer', {'transportPreference': 'auto'});
    await tester.pump(const Duration(milliseconds: 60));
    expect(controller.snapshot.transfer.transport, 'ble');
    expect(controller.snapshot.transfer.success, isTrue);
  });

  testWidgets('firmware download can cancel and retry before OTA', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway(
      stepDuration: const Duration(milliseconds: 10),
    );
    final controller = DeviceController(gateway);
    addTearDown(gateway.dispose);
    addTearDown(controller.dispose);
    await tester.pump();
    await controller.command('connect');
    await tester.pump();
    final hardware = HardwareIdentity.n4r2;
    final package = {
      'targetId': hardware.targetId,
      'chipId': hardware.chipId,
      'flashBytes': hardware.flashBytes,
      'psramBytes': hardware.psramBytes,
      'psramBus': hardware.psramBus,
    };
    await controller.command('downloadFirmware', package);
    await tester.pump();
    expect(controller.snapshot.firmwareDownload.canCancel, isTrue);
    await controller.command('cancelFirmwareDownload');
    await tester.pump();
    expect(controller.snapshot.firmwareDownload.phase, 'cancelled');
    expect(controller.snapshot.importedFirmware, isNull);
    await controller.command('downloadFirmware', package);
    await tester.pump(const Duration(milliseconds: 50));
    expect(controller.snapshot.firmwareDownload.phase, 'done');
    expect(
      controller.snapshot.importedFirmware!.matchesHardware(hardware),
      isTrue,
    );
    await controller.command('startFirmwareTransfer', {
      'transportPreference': 'wifi',
    });
    await tester.pump(const Duration(milliseconds: 90));
    expect(controller.snapshot.transfer.transport, 'wifi');
    expect(controller.snapshot.transfer.success, isTrue);
  });

  testWidgets(
    'photo confirmation checks the simulated SD catalog and files can be displayed or deleted',
    (tester) async {
      final gateway = PreviewDeviceGateway(
        stepDuration: const Duration(milliseconds: 10),
      );
      final controller = DeviceController(gateway);
      addTearDown(gateway.dispose);
      addTearDown(controller.dispose);
      gateway.reset(PreviewScenario.unconfirmed);
      await tester.pump();
      await controller.command('connect');
      await controller.command('pickFilm');
      await tester.pump();
      final file = controller.snapshot.importedFile!;
      await controller.command('startTransfer', {'transportPreference': 'ble'});
      await tester.pump(const Duration(milliseconds: 60));
      expect(controller.snapshot.transfer.canConfirm, isTrue);
      expect(controller.snapshot.connected, isFalse);
      await controller.command('connect');
      await tester.pump();
      await controller.command('confirmTransfer');
      await tester.pump();
      expect(controller.snapshot.transfer.confirmation, 'directory_presence');
      final entries = controller.snapshot.files['entries'] as List;
      final added = entries.whereType<Map>().singleWhere(
        (entry) => entry['name'] == file.name,
      );
      final command = {'id': added['id'], 'name': added['name']};
      await controller.command('openDevicePage', {'appId': 5});
      await tester.pump();
      expect(controller.snapshot.files['canDelete'], isFalse);
      await controller.command('displayDeviceFile', command);
      await tester.pump();
      expect(controller.snapshot.files['currentId'], added['id']);
      final before =
          (controller.snapshot.files['storage'] as Map)['freeBytes'] as int;
      await controller.command('deleteDeviceFile', command);
      await tester.pump();
      expect(
        (controller.snapshot.files['storage'] as Map)['freeBytes'],
        before + file.size,
      );
    },
  );

  testWidgets(
    'simulated passport cancels, retries, reads and opens the saved document',
    (tester) async {
      final gateway = PreviewDeviceGateway(
        stepDuration: const Duration(milliseconds: 10),
      );
      final controller = DeviceController(gateway);
      addTearDown(gateway.dispose);
      addTearDown(controller.dispose);
      await tester.pump();
      await controller.command('connect');
      await tester.pump();
      const document = '{"version":1}';
      await controller.command('savePassport', {
        'json': document,
        'bin': Uint8List(33456),
      });
      await tester.pump();
      await controller.command('cancelPassport');
      await tester.pump();
      expect(controller.snapshot.passport['canRetry'], isTrue);
      await controller.command('retryPassport');
      await tester.pump(const Duration(milliseconds: 50));
      expect(controller.snapshot.passport['json'], document);
      await controller.command('readPassport');
      await tester.pump(const Duration(milliseconds: 50));
      expect(controller.snapshot.passport['json'], document);
      expect(controller.snapshot.passport['revision'], 2);
      await controller.command('openDevicePage', {'appId': 6});
      await tester.pump();
      expect(controller.snapshot.settingsMessage, contains('通行证'));
    },
  );

  testWidgets('all pages retain demo warning and reset cancels activity', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway();
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    await tester.pump();
    expect(find.byKey(const Key('preview-warning')), findsOneWidget);
    await tester.tap(find.byKey(const Key('preview-scenario')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('传输失败（重试恢复）').last);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(gateway.scenario, PreviewScenario.transferFailure);
    expect(
      tester
          .widget<DropdownButton<PreviewScenario>>(
            find.byKey(const Key('preview-scenario')),
          )
          .value,
      PreviewScenario.transferFailure,
    );
    await tester.tap(find.text('Film').last);
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(
      find.byKey(const Key('pick-film')),
      300,
      scrollable: find
          .descendant(
            of: find.byKey(const Key('workbench-content')),
            matching: find.byType(Scrollable),
          )
          .first,
    );
    expect(find.byKey(const Key('pick-film')), findsOneWidget);
    expect(find.text(PreviewDeviceGateway.warning), findsOneWidget);
    await tester.tap(find.byKey(const Key('reset-preview')));
    await tester.pump();
    await tester.pumpWidget(const SizedBox());
    await tester.pump(const Duration(seconds: 2));
    expect(tester.takeException(), isNull);
  });
}
