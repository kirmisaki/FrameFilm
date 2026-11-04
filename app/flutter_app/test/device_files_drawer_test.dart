import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_files.dart';
import 'package:framefilm_ark/device_gateway.dart';

import 'widget_test.dart' show FakeGateway;

class CatalogGateway extends FakeGateway {
  final arguments = <String, Map<String, Object?>?>{};
  @override
  Future<void> invoke(String method, [Map<String, Object?>? values]) async {
    arguments[method] = values;
    await super.invoke(method, values);
    if (method == 'setDeviceFileDirectory') {
      events.add(
        catalog(directory: values!['appId'] == 3 ? 'animation' : 'film'),
      );
    }
  }
}

DeviceSnapshot catalog({String directory = 'film'}) => DeviceSnapshot.fromMap({
  'connected': true,
  'deviceId': 'catalog-device',
  'files': {
    'loaded': true,
    'directory': directory,
    'canDelete': false,
    'storage': {'filmCount': 80, 'animationCount': 80},
    'entries': [
      for (var i = 0; i < 80; i++) {'id': i * 3 + 7, 'name': 'file$i.film'},
    ],
  },
});

void main() {
  for (final size in [const Size(390, 844), const Size(1024, 768)]) {
    testWidgets('80 files stay collapsed and scroll internally at $size', (
      tester,
    ) async {
      await tester.binding.setSurfaceSize(size);
      addTearDown(() => tester.binding.setSurfaceSize(null));
      final gateway = CatalogGateway();
      final controller = DeviceController(gateway);
      addTearDown(() async {
        controller.dispose();
        await gateway.events.close();
      });
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(
              child: Column(
                children: [
                  DeviceFilesPage(controller: controller),
                  const Text('页面后续设置'),
                ],
              ),
            ),
          ),
        ),
      );
      gateway.events.add(catalog());
      await tester.pumpAndSettle();
      final drawer = find.byKey(const Key('device-files-drawer'));
      expect(find.text('图片 / film · 80 个文件'), findsOneWidget);
      expect(find.byKey(const Key('device-files-list')), findsNothing);
      expect(find.text('file0.film'), findsNothing);
      await tester.tap(drawer);
      await tester.pumpAndSettle();
      final viewport = find.byKey(const Key('device-files-list-viewport'));
      expect(tester.getSize(viewport).height, lessThanOrEqualTo(320));
      expect(find.text('file79.film'), findsNothing);
      await tester.ensureVisible(viewport);
      await tester.pumpAndSettle();
      final list = find.byKey(const Key('device-files-list'));
      final inner = find.descendant(
        of: list,
        matching: find.byType(Scrollable),
      );
      final last = find.byKey(const ValueKey('device-file-244'));
      await tester.scrollUntilVisible(last, 300, scrollable: inner);
      await tester.pumpAndSettle();
      expect(find.text('file79.film'), findsOneWidget);
      final display = find.descendant(
        of: last,
        matching: find.widgetWithText(TextButton, '显示'),
      );
      await tester.ensureVisible(display);
      await tester.tap(display);
      await tester.pumpAndSettle();
      expect(gateway.arguments['displayDeviceFile'], {
        'id': 244,
        'name': 'file79.film',
      });
      await tester.ensureVisible(drawer);
      await tester.pumpAndSettle();
      await tester.tap(drawer);
      await tester.pumpAndSettle();
      await controller.command('listDeviceFiles');
      gateway.events.add(DeviceSnapshot.fromMap({'battery': 75}));
      await tester.pumpAndSettle();
      expect(find.byKey(const Key('device-files-list')), findsNothing);
      expect(find.text('图片 / film · 80 个文件'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }

  for (final directory in [0, 3]) {
    testWidgets('connection browse opens directory $directory drawer', (
      tester,
    ) async {
      final gateway = CatalogGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      gateway.events.add(catalog());
      await tester.pumpAndSettle();
      final browse = find.text('${directory == 3 ? '动画' : '图片'} 80 个 · 查看');
      await tester.scrollUntilVisible(
        browse,
        200,
        scrollable: find
            .descendant(
              of: find.byKey(const Key('workbench-content')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      await tester.ensureVisible(browse);
      await tester.pumpAndSettle();
      await tester.tap(browse);
      await tester.pumpAndSettle();
      expect(gateway.arguments['setDeviceFileDirectory'], {'appId': directory});
      expect(
        find.text(
          '${directory == 3 ? '动画 / animation' : '图片 / film'} · 80 个文件',
        ),
        findsOneWidget,
      );
      expect(find.byKey(const Key('device-files-list')), findsOneWidget);
      expect(find.text('点击收起文件列表'), findsOneWidget);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    });
  }
}
