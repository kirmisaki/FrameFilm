import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/device_files.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

void main() {
  test('preview browsing preserves displayed file and inactive deletion is refused', () async {
    final gateway = PreviewDeviceGateway();
    final controller = DeviceController(gateway);
    addTearDown(() {
      controller.dispose();
      gateway.dispose();
    });
    await Future<void>.delayed(Duration.zero);
    await controller.command('connect', {'address': 'DEMO-ARK'});
    await Future<void>.delayed(Duration.zero);
    await controller.command('displayDeviceFile', {'id': 1, 'name': '风景.film'});
    await controller.command('setDeviceFileDirectory', {'appId': 3});
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['canDelete'], false);
    expect(controller.snapshot.files['currentId'], null);
    await controller.command('deleteDeviceFile', {
      'id': 0,
      'name': '演示动画.film',
    });
    expect(controller.errorMessage, contains('对应 app'));
    await controller.command('setDeviceFileDirectory', {'appId': 0});
    await Future<void>.delayed(Duration.zero);
    expect(controller.snapshot.files['currentId'], 1);
    final stats = controller.snapshot.files['storage'] as Map;
    expect(stats['filmCount'], 2);
    expect(stats['animationCount'], 1);
    expect(stats['message'], contains('演示数据'));
  });
  testWidgets(
    'summary distinguishes unqueried and partial counts from actual zero',
    (tester) async {
      final gateway = PreviewDeviceGateway();
      final controller = DeviceController(gateway);
      addTearDown(() {
        controller.dispose();
        gateway.dispose();
      });
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: DeviceStorageSummary(controller: controller)),
        ),
      );
      await tester.pumpAndSettle();
      expect(find.textContaining('未读取'), findsWidgets);
      controller.snapshot = DeviceSnapshot.fromMap({
        'connected': true,
        'files': {
          'storage': {
            'phase': 'partial',
            'filmCount': 0,
            'animationCount': null,
            'message': '部分统计未读取',
          },
        },
      });
      controller.notifyListeners();
      await tester.pump();
      expect(find.text('图片 0 个 · 查看'), findsOneWidget);
      expect(find.text('动画 未读取 · 查看'), findsOneWidget);
      expect(find.textContaining('总容量 未读取'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
    },
  );
}
