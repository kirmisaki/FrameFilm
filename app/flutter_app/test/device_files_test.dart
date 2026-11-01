import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/device_files.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

class FilesGateway implements DeviceGateway {
  final events = StreamController<DeviceSnapshot>.broadcast(sync: true);
  final calls = <String>[];
  Completer<void>? operation;
  bool failListing = false;

  @override
  bool get supported => true;
  @override
  Stream<DeviceSnapshot> get snapshots => events.stream;
  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    calls.add(method);
    if (method == 'refresh') await operation!.future;
    if (method == 'listDeviceFiles') {
      events.add(DeviceSnapshot.fromMap({'settingsBusy': true}));
      events.add(DeviceSnapshot.fromMap({'settingsBusy': false}));
      if (failListing) throw StateError('读取失败');
      events.add(
        DeviceSnapshot.fromMap({
          'files': {'loaded': true, 'directory': 'current', 'entries': []},
        }),
      );
    }
  }
}

void main() {
  test(
    'auto files wait for initialization and commands, read once per session',
    () async {
      final gateway = FilesGateway();
      final controller = DeviceController(gateway);
      addTearDown(() async {
        controller.dispose();
        await gateway.events.close();
      });
      await Future<void>.delayed(Duration.zero);
      gateway.events.add(DeviceSnapshot.fromMap({'connected': true}));
      await Future<void>.delayed(Duration.zero);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles'), isEmpty);
      gateway.events.add(
        DeviceSnapshot.fromMap({'connected': true, 'settingsBusy': true}),
      );
      await Future<void>.delayed(Duration.zero);
      expect(controller.automaticFilesPending, true);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles'), isEmpty);
      gateway.events.add(DeviceSnapshot.fromMap({'settingsBusy': false}));
      // A command can claim the operation lock before the scheduled auto read.
      gateway.operation = Completer<void>();
      final refresh = controller.command('refresh');
      await Future<void>.delayed(Duration.zero);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles'), isEmpty);
      gateway.operation!.complete();
      await refresh;
      await Future<void>.delayed(Duration.zero);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles').length, 1);
      expect(controller.snapshot.files['loaded'], true);
      expect(controller.automaticFilesLoading, false);
      gateway.events.add(DeviceSnapshot.fromMap({'battery': 90}));
      await Future<void>.delayed(Duration.zero);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles').length, 1);
      gateway.events.add(DeviceSnapshot.fromMap({'connected': false}));
      expect(controller.snapshot.files, isEmpty);
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'width': 720,
          'height': 480,
        }),
      );
      await Future<void>.delayed(Duration.zero);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles').length, 2);
      expect(gateway.calls, isNot(contains('setDeviceFileDirectory')));
    },
  );

  test(
    'auto files failure allows manual retry without automatic retries',
    () async {
      final gateway = FilesGateway()..failListing = true;
      final controller = DeviceController(gateway);
      addTearDown(() async {
        controller.dispose();
        await gateway.events.close();
      });
      await Future<void>.delayed(Duration.zero);
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'width': 720,
          'height': 480,
        }),
      );
      await Future<void>.delayed(Duration.zero);
      expect(controller.automaticFilesFailed, true);
      gateway.events.add(DeviceSnapshot.fromMap({'battery': 80}));
      await Future<void>.delayed(Duration.zero);
      expect(gateway.calls.where((c) => c == 'listDeviceFiles').length, 1);
      gateway.failListing = false;
      await controller.command('listDeviceFiles');
      expect(controller.automaticFilesFailed, false);
      expect(controller.snapshot.files['loaded'], true);
    },
  );

  test('dispose cancels a scheduled automatic files read', () async {
    final gateway = FilesGateway();
    final controller = DeviceController(gateway);
    await Future<void>.delayed(Duration.zero);
    gateway.events.add(
      DeviceSnapshot.fromMap({'connected': true, 'width': 720, 'height': 480}),
    );
    controller.dispose();
    await Future<void>.delayed(Duration.zero);
    expect(gateway.calls.where((c) => c == 'listDeviceFiles'), isEmpty);
    await gateway.events.close();
  });

  testWidgets('unknown directory asks for ownership before enabling display', (
    tester,
  ) async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: DeviceFilesPage(controller: controller),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    DeviceSnapshot filesIn(String directory) => DeviceSnapshot.fromMap({
      'connected': true,
      'files': {
        'loaded': true,
        'directory': directory,
        'entries': [
          {'id': 0, 'name': 'a.film'},
        ],
      },
    });
    controller.snapshot = filesIn('current');
    controller.notifyListeners();
    await tester.pump();
    final displays = find.widgetWithText(TextButton, '显示');
    expect(displays, findsWidgets);
    expect(tester.widget<TextButton>(displays.first).onPressed, isNull);
    expect(find.text('显示文件前，请先选择图片目录或动画目录，确认文件所属 app。'), findsOneWidget);
    controller.snapshot = filesIn('film');
    controller.notifyListeners();
    await tester.pumpAndSettle();
    expect(tester.widget<TextButton>(displays.first).onPressed, isNotNull);
    await tester.pumpWidget(const SizedBox());
  });

  test('file state survives partial events and is cleared on disconnect', () {
    final original = DeviceSnapshot.fromMap({
      'connected': true,
      'files': {
        'loaded': true,
        'entries': [
          {'id': 0, 'name': 'a.film'},
        ],
      },
    });
    final retained = DeviceSnapshot.fromMap({'message': 'progress'})
        .retainingTransfer(original);
    expect(retained.files['loaded'], true);
    expect(
      DeviceSnapshot.fromMap({'connected': false})
          .retainingTransfer(retained)
          .files,
      isEmpty,
    );
  });

  test('preview file operations validate IDs, refresh after delete, and gate disconnect', () async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await Future<void>.delayed(Duration.zero);
    await controller.command('listDeviceFiles');
    expect(controller.snapshot.files, isEmpty);
    await controller.command('connect', {'address': 'DEMO-ARK'});
    await Future<void>.delayed(Duration.zero);
    await controller.command('listDeviceFiles');
    await Future<void>.delayed(Duration.zero);
    expect((controller.snapshot.files['entries'] as List).length, 2);
    await controller.command('deleteDeviceFile', {
      'id': 0,
      'name': 'wrong.film',
    });
    expect(controller.errorMessage, contains('列表已变化'));
    await controller.command('deleteDeviceFile', {
      'id': 0,
      'name': '演示画面.film',
    });
    await Future<void>.delayed(Duration.zero);
    expect((controller.snapshot.files['entries'] as List).length, 1);
    await controller.command('playAnimation', {
      'intervalMs': 400,
      'loopSeconds': 1,
      'playMode': 1,
    });
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['directory'], 'animation');
    await controller.command('setDeviceFileDirectory', {'appId': 3});
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['directory'], 'animation');
    await controller.command('displayDeviceFile', {
      'id': 0,
      'name': '演示动画.film',
    });
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['currentId'], 0);
    await controller.command('disconnect');
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files, isEmpty);
  });
}
