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
    await tester.pump();
    expect(find.byKey(const Key('pick-film')), findsOneWidget);
    expect(find.text(PreviewDeviceGateway.warning), findsOneWidget);
    await tester.tap(find.byKey(const Key('reset-preview')));
    await tester.pump();
    await tester.pumpWidget(const SizedBox());
    await tester.pump(const Duration(seconds: 2));
    expect(tester.takeException(), isNull);
  });
}
