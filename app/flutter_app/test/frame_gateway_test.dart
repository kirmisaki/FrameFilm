import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';
import 'package:framefilm_ark/frame_codec.dart';

void main() {
  testWidgets(
    'generated mono file can be staged offline then sent by existing preview flow',
    (tester) async {
      final gateway = PreviewDeviceGateway(
        stepDuration: const Duration(milliseconds: 1),
      );
      final controller = DeviceController(gateway);
      addTearDown(() {
        controller.dispose();
        gateway.dispose();
      });
      await tester.pump();
      final film = packFramePixels(
        Uint8List(480 * 720)..fillRange(0, 480 * 720, 1),
        format: FrameFormat.monoFast,
      );
      expect(controller.canImportFilm, true);
      await controller.command('importGeneratedFilm', {
        'bytes': film,
        'name': 'ark_frame.film',
      });
      await tester.pump();
      expect(controller.snapshot.importedFile!.size, 43232);
      expect(controller.snapshot.connected, false);
      await controller.command('connect');
      await tester.pump();
      await controller.command('startTransfer');
      await tester.pump();
      expect(controller.canImportFilm, false);
      await controller.command('importGeneratedFilm', {
        'bytes': film,
        'name': 'should_not_replace.film',
      });
      expect(controller.snapshot.importedFile!.name, 'ark_frame.film');
      await tester.pump(const Duration(milliseconds: 20));
      expect(controller.snapshot.transfer.success, true);
      expect(controller.snapshot.transfer.total, 43232);
    },
  );
  testWidgets('invalid generated import retains the previous staged film', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await tester.pump();
    await controller.command('pickFilm');
    await tester.pump();
    final name = controller.snapshot.importedFile!.name;
    await controller.command('importGeneratedFilm', {
      'bytes': Uint8List(34),
      'name': 'broken.film',
    });
    await tester.pump();
    expect(controller.errorMessage, isNotNull);
    expect(controller.snapshot.importedFile!.name, name);
  });
}
