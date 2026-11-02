import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/interface_theme_preferences.dart';

import 'widget_test.dart' show FakeGateway;

class MemoryThemePreferences implements InterfaceThemePreferences {
  MemoryThemePreferences(this.enabled);
  bool enabled;
  final saves = <bool>[];
  @override
  Future<bool> load() async => enabled;
  @override
  Future<void> save(bool value) async {
    enabled = value;
    saves.add(value);
  }
}

void main() {
  testWidgets(
    'web theme rules require an identified connected Ark and welcome once',
    (tester) async {
      final gateway = FakeGateway();
      final preferences = MemoryThemePreferences(true);
      addTearDown(gateway.events.close);
      await tester.pumpWidget(
        FrameFilmApp(gateway: gateway, themePreferences: preferences),
      );
      await tester.pump();
      final dynamic state = tester.state(find.byType(FrameFilmApp));
      expect(state.ark, isFalse);
      gateway.events.add(
        const DeviceSnapshot(connected: true, name: 'FRAMEFILM-PRO'),
      );
      await tester.pump();
      expect(state.ark, isFalse);
      expect(find.byKey(const Key('connection-transition')), findsNothing);
      gateway.events.add(
        const DeviceSnapshot(connected: true, name: 'FRAMEFILMARK-TEST'),
      );
      await tester.pump();
      expect(state.ark, isTrue);
      expect(find.byKey(const Key('connection-transition')), findsOneWidget);
      await tester.tap(find.byKey(const Key('skip-ark-intro')));
      await tester.pump();
      expect(state.loading, isFalse);
      gateway.events.add(
        const DeviceSnapshot(
          connected: true,
          name: 'FRAMEFILMARK-TEST',
          battery: 80,
        ),
      );
      await tester.pump();
      expect(state.loading, isFalse);
      state.changeThemePreference(false);
      await tester.pump();
      expect(state.ark, isFalse);
      state.changeThemePreference(true);
      await tester.pump();
      expect(state.ark, isTrue);
      expect(state.loading, isFalse);
      expect(preferences.saves, [false, true]);
      gateway.events.add(const DeviceSnapshot());
      await tester.pump();
      expect(state.ark, isFalse);
      gateway.events.add(
        const DeviceSnapshot(connected: true, name: 'FRAMEFILMARK-TEST'),
      );
      await tester.pump();
      expect(state.loading, isTrue);
      gateway.events.add(const DeviceSnapshot());
      await tester.pump();
      expect(find.byKey(const Key('connection-transition')), findsNothing);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  testWidgets(
    'disabled preference survives app recreation and does not force an offline theme',
    (tester) async {
      final preferences = MemoryThemePreferences(false);
      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(
        FrameFilmApp(gateway: gateway, themePreferences: preferences),
      );
      await tester.pump();
      dynamic state = tester.state(find.byType(FrameFilmApp));
      gateway.events.add(
        const DeviceSnapshot(connected: true, name: 'FRAMEFILMARK-TEST'),
      );
      await tester.pump();
      expect(state.arkThemeEnabled, isFalse);
      expect(state.ark, isFalse);
      state.changeThemePreference(true);
      await tester.pump();
      expect(preferences.enabled, isTrue);
      await tester.pumpWidget(const SizedBox.shrink());
      final next = FakeGateway();
      addTearDown(next.events.close);
      await tester.pumpWidget(
        FrameFilmApp(gateway: next, themePreferences: preferences),
      );
      await tester.pump();
      state = tester.state(find.byType(FrameFilmApp));
      expect(state.arkThemeEnabled, isTrue);
      expect(state.ark, isFalse);
      next.events.add(
        const DeviceSnapshot(connected: true, name: 'FRAMEFILMARK-TEST'),
      );
      await tester.pump();
      expect(state.ark, isTrue);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  testWidgets('a delayed load cannot overwrite a newer user selection', (
    tester,
  ) async {
    final load = Completer<bool>();
    final preferences = DelayedThemePreferences(load.future);
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(
      FrameFilmApp(gateway: gateway, themePreferences: preferences),
    );
    final dynamic state = tester.state(find.byType(FrameFilmApp));
    state.changeThemePreference(false);
    load.complete(true);
    await tester.pump();
    expect(state.arkThemeEnabled, isFalse);
    expect(state.themePreferenceReady, isTrue);
    await tester.pumpWidget(const SizedBox.shrink());
  });
}

class DelayedThemePreferences implements InterfaceThemePreferences {
  DelayedThemePreferences(this.pending);
  final Future<bool> pending;
  @override
  Future<bool> load() => pending;
  @override
  Future<void> save(bool enabled) async {}
}
