import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

void main() {
  testWidgets('动画和文件管理入口可以打开，导航不丢当前页', (tester) async {
    tester.view.physicalSize = const Size(1440, 1080);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final gateway = PreviewDeviceGateway();
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    await tester.pumpAndSettle();
    await tester.tap(find.text('动画').first);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(find.text('导入图片 / GIF'), findsOneWidget);
    await gateway.invoke('connect');
    await tester.pumpAndSettle();
    await tester.pump(const Duration(seconds: 2));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(
      find.text('应用参数并播放设备动画'),
      500,
      scrollable: find
          .descendant(
            of: find.byKey(const Key('workbench-content')),
            matching: find.byType(Scrollable),
          )
          .first,
    );
    final play = tester.widget<OutlinedButton>(
      find.widgetWithText(OutlinedButton, '应用参数并播放设备动画'),
    );
    expect(play.onPressed, isNotNull);
    await tester.tap(find.text('设置').first);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect((tester.state(find.byType(FrameFilmApp)) as dynamic).page, 5);

    expect(find.text('设备文件'), findsOneWidget);
    await tester.pumpWidget(const SizedBox.shrink());
  });
}
