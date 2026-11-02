import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_files.dart';
import 'package:framefilm_ark/device_gateway.dart';

import 'widget_test.dart' show EnabledThemePreferences, FakeGateway;

void main() {
  for (final size in [const Size(393, 851), const Size(1280, 854)]) {
    testWidgets('connection status appears once at $size', (tester) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final gateway = FakeGateway();
      addTearDown(gateway.events.close);
      await tester.pumpWidget(
        FrameFilmApp(
          gateway: gateway,
          themePreferences: EnabledThemePreferences(false),
        ),
      );
      await tester.pumpAndSettle();
      expect(find.byType(DeviceStorageSummary), findsNothing);
      expect(find.text('连接信息'), findsNothing);

      for (final message in ['正在读取设备信息', '设备信息已更新']) {
        gateway.events.add(
          DeviceSnapshot(
            connected: true,
            name: 'FRAMEFILMARK-TEST',
            message: message,
            battery: 82,
            width: 720,
            height: 480,
          ),
        );
        await tester.pumpAndSettle();
        expect(find.text('FRAMEFILMARK-TEST'), findsOneWidget);
        expect(find.text(message), findsOneWidget);
        expect(find.byType(DeviceStorageSummary), findsOneWidget);
        expect(find.text('连接信息'), findsOneWidget);
        expect(tester.takeException(), isNull);
      }

      gateway.events.add(const DeviceSnapshot(message: '请扫描并连接 Ark'));
      await tester.pumpAndSettle();
      expect(find.text('请扫描并连接 Ark'), findsOneWidget);
      expect(find.byType(DeviceStorageSummary), findsNothing);
      expect(find.text('连接信息'), findsNothing);
      await tester.pumpWidget(const SizedBox.shrink());
    });
  }
}
