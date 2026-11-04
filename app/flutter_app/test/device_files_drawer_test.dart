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
  testWidgets('single selection can be cleared and refresh clears selection', (
    tester,
  ) async {
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
            child: DeviceFilesPage(controller: controller),
          ),
        ),
      ),
    );
    void emit({
      String device = 'one',
      String directory = 'film',
      bool loaded = true,
      bool connected = true,
      int id = 7,
    }) {
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': connected,
          'deviceId': device,
          'files': {
            'loaded': loaded,
            'directory': directory,
            'canDelete': true,
            'currentId': id,
            'entries': loaded
                ? [
                    {'id': id, 'name': 'file.film'},
                    {'id': id + 1, 'name': 'other.film'},
                  ]
                : [],
          },
        }),
      );
    }

    final display = find.widgetWithText(OutlinedButton, '选择显示');
    final delete = find.widgetWithText(OutlinedButton, '删除文件');
    Checkbox checkbox(int id) => tester.widget<Checkbox>(
      find.descendant(
        of: find.byKey(ValueKey('device-file-$id')),
        matching: find.byType(Checkbox),
      ),
    );
    emit();
    await tester.pumpAndSettle();
    expect(find.text('FILM设置'), findsOneWidget);
    expect(find.byType(ExpansionTile), findsNothing);
    expect(find.byType(SegmentedButton<String>), findsNothing);
    expect(tester.widget<OutlinedButton>(display).onPressed, isNotNull);
    await tester.tap(display);
    await tester.pumpAndSettle();
    expect(find.text('请先选择要显示的文件'), findsOneWidget);
    await tester.tap(delete);
    await tester.pumpAndSettle();
    expect(find.text('请先选择要删除的文件'), findsOneWidget);
    expect(gateway.arguments['displayDeviceFile'], isNull);
    expect(gateway.arguments['deleteDeviceFile'], isNull);
    final first = find.byKey(const ValueKey('device-file-7'));
    await tester.tap(first);
    await tester.pumpAndSettle();
    expect(checkbox(7).value, isTrue);
    expect(gateway.arguments['displayDeviceFile'], isNull);
    await tester.tap(first);
    await tester.pumpAndSettle();
    expect(checkbox(7).value, isFalse);
    await tester.tap(first);
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('device-file-8')));
    await tester.pumpAndSettle();
    expect(checkbox(7).value, isFalse);
    expect(checkbox(8).value, isTrue);
    gateway.events.add(DeviceSnapshot.fromMap({'battery': 75}));
    await tester.pumpAndSettle();
    expect(checkbox(8).value, isTrue);
    await tester.tap(find.byTooltip('刷新'));
    await tester.pumpAndSettle();
    expect(gateway.calls, contains('listDeviceFiles'));
    expect(checkbox(8).value, isFalse);
    await tester.tap(first);
    await tester.pumpAndSettle();
    emit(loaded: false);
    await tester.pumpAndSettle();
    expect(checkbox(7).value, isFalse);
    expect(checkbox(7).onChanged, isNull);
    emit(id: 91);
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('device-file-91')));
    await tester.pumpAndSettle();
    await tester.tap(display);
    await tester.pumpAndSettle();
    expect(gateway.arguments['displayDeviceFile'], {
      'id': 91,
      'name': 'file.film',
    });
    emit(directory: 'animation');
    await tester.pumpAndSettle();
    expect(checkbox(7).value, isFalse);
    await tester.tap(first);
    await tester.pumpAndSettle();
    emit(device: 'two', directory: 'animation', loaded: false);
    await tester.pumpAndSettle();
    expect(find.byKey(const Key('device-files-list')), findsNothing);
    emit(device: 'two', directory: 'animation', connected: false);
    await tester.pumpAndSettle();
    expect(tester.widget<OutlinedButton>(display).onPressed, isNull);
    expect(tester.widget<OutlinedButton>(delete).onPressed, isNull);
    expect(find.byKey(const Key('device-files-list')), findsNothing);
    await tester.pumpWidget(const SizedBox());
  });

  for (final size in [const Size(390, 844), const Size(1024, 768)]) {
    testWidgets('80 files are always visible and scroll internally at $size', (
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
      expect(find.text('FILM设置'), findsOneWidget);
      expect(find.text('file0.film'), findsOneWidget);
      final viewport = find.byKey(const Key('device-files-list-viewport'));
      expect(tester.getSize(viewport).height, lessThanOrEqualTo(180));
      expect(find.text('file79.film'), findsNothing);
      final list = find.byKey(const Key('device-files-list'));
      final inner = find.descendant(
        of: list,
        matching: find.byType(Scrollable),
      );
      final last = find.byKey(const ValueKey('device-file-244'));
      await tester.scrollUntilVisible(last, 300, scrollable: inner);
      await tester.pumpAndSettle();
      expect(find.text('file79.film'), findsOneWidget);
      await tester.tap(last);
      await tester.pumpAndSettle();
      final display = find.widgetWithText(OutlinedButton, '选择显示');
      await tester.tap(display);
      await tester.pumpAndSettle();
      expect(gateway.arguments['displayDeviceFile'], {
        'id': 244,
        'name': 'file79.film',
      });
      expect(find.text('页面后续设置'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }

  for (final directory in [0, 3]) {
    testWidgets(
      'connection browse queries directory $directory and opens settings',
      (tester) async {
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
        expect(gateway.arguments['setDeviceFileDirectory'], {
          'appId': directory,
        });
        expect(find.text('FILM设置'), findsOneWidget);
        expect(find.byKey(const Key('device-files-list')), findsOneWidget);
        expect(find.text('file0.film'), findsOneWidget);
        expect(gateway.arguments['displayDeviceFile'], isNull);
        expect(tester.takeException(), isNull);
        await tester.pumpWidget(const SizedBox());
      },
    );
  }
}
