import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/transfer_preferences.dart';

import 'widget_test.dart' show FakeGateway, openPage;

class _Preferences implements TransferPreferences {
  _Preferences(this.value);
  TransferPreference value;

  @override
  Future<TransferPreference> load() async => value;

  @override
  Future<void> save(TransferPreference preference) async {
    value = preference;
  }
}

class _Gateway extends FakeGateway {
  final arguments = <String, Map<String, Object?>?>{};

  @override
  Future<void> invoke(String method, [Map<String, Object?>? args]) async {
    await super.invoke(method, args);
    arguments[method] = args;
  }
}

Future<void> _scrollTo(WidgetTester tester, Finder finder) async {
  await tester.scrollUntilVisible(
    finder,
    300,
    scrollable: find
        .descendant(
          of: find.byKey(const Key('workbench-content')),
          matching: find.byType(Scrollable),
        )
        .first,
  );
  await Scrollable.ensureVisible(tester.element(finder), alignment: 0.5);
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('manual mode persists and selects BLE for an ordinary Film', (
    tester,
  ) async {
    final preferences = _Preferences(TransferPreference.auto);
    final gateway = _Gateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(
      FrameFilmApp(gateway: gateway, transferPreferences: preferences),
    );
    await openPage(tester, '设置');
    final toggle = find.byKey(const Key('ble-compatibility-switch'));
    await _scrollTo(tester, toggle);
    expect(tester.widget<SwitchListTile>(toggle).value, isFalse);
    await tester.tap(toggle);
    await tester.pumpAndSettle();
    expect(preferences.value, TransferPreference.ble);

    await tester.pumpWidget(const SizedBox.shrink());
    await tester.pumpWidget(
      FrameFilmApp(gateway: gateway, transferPreferences: preferences),
    );
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'importedFile': {
          'name': 'photo.film',
          'size': 172832,
          'blePhotoCompatible': true,
        },
      }),
    );
    await tester.pumpAndSettle();
    await openPage(tester, 'Film');
    final send = find.byKey(const Key('start-transfer'));
    await _scrollTo(tester, send);
    expect(find.text('蓝牙发送'), findsOneWidget);
    await tester.tap(send);
    await tester.pump();
    expect(gateway.arguments['startTransfer'], {'transportPreference': 'ble'});

    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'importedFile': {
          'name': 'animation.film',
          'size': 400000,
          'blePhotoCompatible': false,
        },
      }),
    );
    await tester.pump();
    expect(tester.widget<FilledButton>(send).onPressed, isNull);
    expect(
      gateway.calls.where((method) => method == 'startTransfer'),
      hasLength(1),
    );
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'importedFile': {
          'name': 'animation.film',
          'size': 400000,
          'blePhotoCompatible': true,
          'mediaDirectory': 'animation',
        },
      }),
    );
    await tester.pump();
    await tester.tap(send);
    await tester.pump();
    expect(
      gateway.calls.where((method) => method == 'startTransfer'),
      hasLength(2),
    );
    expect(gateway.arguments['startTransfer'], {'transportPreference': 'ble'});
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('photo confirmation never offers a resend', (tester) async {
    final gateway = _Gateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'connected': true,
        'importedFile': {'name': 'photo.film', 'size': 172832},
        'transfer': {
          'kind': 'film',
          'phase': 'unconfirmed',
          'transport': 'ble',
          'confirmation': 'pending',
          'canConfirm': true,
          'cleanupCompleted': true,
        },
      }),
    );
    await tester.pumpAndSettle();
    await openPage(tester, 'Film');
    final confirm = find.byKey(const Key('confirm-photo-transfer'));
    await _scrollTo(tester, confirm);
    expect(find.text('已发送，保存待确认'), findsOneWidget);
    expect(
      tester
          .widget<FilledButton>(find.byKey(const Key('retry-transfer')))
          .onPressed,
      isNull,
    );
    await tester.tap(confirm);
    await tester.pump();
    expect(gateway.calls, contains('confirmTransfer'));
    expect(gateway.calls, isNot(contains('startTransfer')));
    expect(gateway.calls, isNot(contains('retryTransfer')));
    await tester.pumpWidget(const SizedBox.shrink());
  });

  test('BLE progress is active until the terminal photo confirmation', () {
    final sending = FilmTransfer.fromMap({
      'phase': 'sending',
      'transport': 'ble',
      'attemptId': 7,
    });
    expect(sending.active, isTrue);
    expect(sending.transport, 'ble');
    expect(sending.attemptId, 7);

    final photo = FilmTransfer.fromMap({
      'phase': 'unconfirmed',
      'transport': 'ble',
      'kind': 'film',
      'attemptId': 7,
      'confirmation': 'pending',
      'cleanupCompleted': true,
    });
    expect(photo.active, isFalse);
    expect(photo.success, isFalse);
    expect(photo.canRetry, isFalse);
    expect(photo.canConfirm, isFalse);
    expect(photo.confirmation, 'pending');
  });
}
