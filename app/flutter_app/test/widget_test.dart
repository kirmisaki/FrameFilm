import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/interface_theme_preferences.dart';

class FakeGateway implements DeviceGateway {
  FakeGateway({this.supported = true});
  final events = StreamController<DeviceSnapshot>.broadcast(sync: true);
  final calls = <String>[];

  @override
  final bool supported;

  @override
  Stream<DeviceSnapshot> get snapshots => events.stream;

  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    calls.add(method);
  }
}

class EnabledThemePreferences implements InterfaceThemePreferences {
  EnabledThemePreferences([this.enabled = true]);
  final bool enabled;

  @override
  Future<bool> load() async => enabled;

  @override
  Future<void> save(bool enabled) async {}
}

Future<void> openPage(WidgetTester tester, String title) async {
  final bottom = find.widgetWithText(NavigationDestination, title);
  if (bottom.evaluate().isNotEmpty) {
    await tester.tap(bottom);
  } else {
    await tester.tap(
      find.descendant(
        of: find.byType(NavigationRail),
        matching: find.text(title),
      ),
    );
  }
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('six pages, offline boundary, theme selection and form state', (
    tester,
  ) async {
    final gateway = FakeGateway(supported: false);
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    expect(find.textContaining('当前平台仅支持界面预览'), findsOneWidget);
    expect(find.text('扫描设备'), findsNothing);

    for (final title in ['Frame', 'Film', '动画', '通行证', '设置']) {
      await openPage(tester, title);
      expect(find.text(title), findsWidgets);
    }
    await tester.tap(find.text('明日方舟预览'));
    await tester.pumpAndSettle();
    expect(find.text('RHODES ISLAND / PRTS'), findsOneWidget);
    await tester.tap(find.text('原 ForFilm'));
    await tester.pumpAndSettle();
    expect(find.text('FrameFilm'), findsOneWidget);

    await openPage(tester, '通行证');
    await tester.enterText(find.byKey(const Key('operator-name')), 'Amiya');
    await tester.pump();
    await openPage(tester, 'Frame');
    await openPage(tester, '通行证');
    expect(
      tester
          .widget<TextField>(find.byKey(const Key('operator-name')))
          .controller!
          .text,
      'Amiya',
    );
    expect(find.textContaining('草稿自动保存在本机'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets(
    'real gateway event starts transition and disconnect cancels it',
    (tester) async {
      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      await tester.pump();
      gateway.events.add(const DeviceSnapshot(connected: true));
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsOneWidget);
      expect(find.text('RHODES ISLAND / PRTS'), findsOneWidget);
      await tester.pump(const Duration(milliseconds: 1000));
      gateway.events.add(
        const DeviceSnapshot(
          connected: true,
          name: 'FRAMEFILMARK-123',
          battery: 75,
          width: 720,
          height: 480,
        ),
      );
      await tester.pump(const Duration(milliseconds: 700));
      expect(find.byKey(const Key('connection-transition')), findsNothing);
      gateway.events.add(const DeviceSnapshot());
      await tester.pump();
      gateway.events.add(const DeviceSnapshot(connected: true));
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsOneWidget);
      gateway.events.add(const DeviceSnapshot());
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsNothing);
      expect(find.text('FrameFilm'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
    },
  );

  testWidgets('ota_reconnect_theme', (tester) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(
      FrameFilmApp(
        gateway: gateway,
        themePreferences: EnabledThemePreferences(),
      ),
    );
    await tester.pumpAndSettle();

    // Confirmation owns the transport, so the device-name refresh is deferred.
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'advertisedName': 'FRAMEFILMARK-123',
        'name': null,
        'transfer': {'phase': 'confirming'},
      }),
    );
    await tester.pump();
    expect(
      tester.widget<MaterialApp>(find.byType(MaterialApp)).theme!.brightness,
      Brightness.dark,
    );

    // Partial transfer updates must not erase the advertised identity.
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'transfer': {'phase': 'done', 'success': true},
      }),
    );
    await tester.pump();
    expect(
      tester.widget<MaterialApp>(find.byType(MaterialApp)).theme!.brightness,
      Brightness.dark,
    );

    gateway.events.add(DeviceSnapshot.fromMap({'connected': false}));
    await tester.pump();
    expect(
      tester.widget<MaterialApp>(find.byType(MaterialApp)).theme!.brightness,
      Brightness.light,
    );

    // A different advertised identity cannot inherit the previous Ark theme.
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'advertisedName': 'FRAMEFILMSTD-456',
        'name': 'FRAMEFILMARK-123',
      }),
    );
    await tester.pump();
    expect(
      tester.widget<MaterialApp>(find.byType(MaterialApp)).theme!.brightness,
      Brightness.light,
    );
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('ota_reconnect_respects_disabled_theme', (tester) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(
      FrameFilmApp(
        gateway: gateway,
        themePreferences: EnabledThemePreferences(false),
      ),
    );
    await tester.pumpAndSettle();

    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'advertisedName': 'FRAMEFILMARK-123',
        'name': null,
        'transfer': {'phase': 'confirming'},
      }),
    );
    await tester.pump();
    expect(
      tester.widget<MaterialApp>(find.byType(MaterialApp)).theme!.brightness,
      Brightness.light,
    );
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('explicit ForFilm choice survives Ark connection', (
    tester,
  ) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    await openPage(tester, '设置');
    await tester.tap(find.text('原 ForFilm'));
    await tester.pumpAndSettle();
    gateway.events.add(
      const DeviceSnapshot(connected: true, name: 'FRAMEFILMARK-123'),
    );
    await tester.pump();
    expect(find.byKey(const Key('connection-transition')), findsNothing);
    expect(find.text('FrameFilm'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
  });
  testWidgets('film import, native progress, cancellation and retry', (
    tester,
  ) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    gateway.events.add(const DeviceSnapshot(connected: true));
    await tester.pump(const Duration(seconds: 2));
    await openPage(tester, 'Film');
    expect(
      tester
          .widget<FilledButton>(find.byKey(const Key('start-transfer')))
          .onPressed,
      isNull,
    );
    await tester.tap(find.byKey(const Key('pick-film')));
    await tester.pump();
    expect(gateway.calls, contains('pickFilm'));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'importedFile': {
          'name': 'test.film',
          'size': 172832,
          'width': 720,
          'height': 480,
        },
        'importing': false,
      }),
    );
    await tester.pump();
    expect(find.text('test.film'), findsOneWidget);
    await tester.tap(find.byKey(const Key('start-transfer')));
    await tester.pump();
    expect(gateway.calls, contains('startTransfer'));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'transfer': {
          'phase': 'downloading',
          'message': '设备正在接收',
          'received': 25,
          'total': 100,
          'canCancel': true,
        },
      }),
    );
    await tester.pump();
    expect(find.text('test.film'), findsOneWidget);
    expect(
      tester
          .widget<LinearProgressIndicator>(
            find.byKey(const Key('transfer-progress')),
          )
          .value,
      .25,
    );
    expect(
      tester
          .widget<OutlinedButton>(find.byKey(const Key('pick-film')))
          .onPressed,
      isNull,
    );
    await tester.ensureVisible(find.byKey(const Key('cancel-transfer')));
    await tester.pump(const Duration(milliseconds: 300));
    expect(
      find.byKey(const Key('cancel-transfer')).hitTestable(),
      findsOneWidget,
    );
    await tester.tap(find.byKey(const Key('cancel-transfer')));
    await tester.pump();
    expect(gateway.calls, contains('cancelTransfer'));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'transfer': {
          'phase': 'cancelled',
          'message': '取消完成',
          'canRetry': true,
          'cleanupCompleted': true,
        },
      }),
    );
    await tester.pump();
    await tester.ensureVisible(find.byKey(const Key('retry-transfer')));
    await tester.pump(const Duration(milliseconds: 300));
    expect(
      find.byKey(const Key('retry-transfer')).hitTestable(),
      findsOneWidget,
    );
    await tester.tap(find.byKey(const Key('retry-transfer')));
    await tester.pump();
    expect(gateway.calls, contains('retryTransfer'));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'transfer': {
          'phase': 'restoring',
          'message': '保存与恢复中',
          'received': 100,
          'total': 100,
          'canCancel': true,
        },
      }),
    );
    await tester.pump();
    expect(find.text('传输完成'), findsNothing);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('film preview has no native actions', (tester) async {
    final gateway = FakeGateway(supported: false);
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    await openPage(tester, 'Film');
    expect(find.textContaining('需要 Android 原生客户端'), findsOneWidget);
    expect(
      tester
          .widget<OutlinedButton>(find.byKey(const Key('pick-film')))
          .onPressed,
      isNull,
    );
    expect(
      tester
          .widget<FilledButton>(find.byKey(const Key('start-transfer')))
          .onPressed,
      isNull,
    );
    expect(gateway.calls, isEmpty);
    await tester.pumpWidget(const SizedBox());
  });

  test(
    'cancel bypasses a pending command and partial snapshots retain film',
    () async {
      final gateway = BlockingGateway();
      final controller = DeviceController(gateway);
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'importedFile': {'name': 'keep.film', 'size': 100},
          'transfer': {'phase': 'downloading', 'canCancel': true},
        }),
      );
      gateway.events.add(const DeviceSnapshot(connected: true));
      expect(controller.snapshot.importedFile?.name, 'keep.film');
      await controller.command('cancelTransfer');
      expect(gateway.calls, contains('cancelTransfer'));
      gateway.pending.complete();
      await Future<void>.delayed(Duration.zero);
      controller.dispose();
      await gateway.events.close();
    },
  );
}

class BlockingGateway extends FakeGateway {
  final pending = Completer<void>();
  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    calls.add(method);
    if (method == 'snapshot') await pending.future;
  }
}
