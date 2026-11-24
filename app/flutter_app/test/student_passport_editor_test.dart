import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/passport_profile.dart';
import 'package:framefilm_ark/student_passport_editor.dart';
import 'package:framefilm_ark/student_passport_profile.dart';

void main() {
  testWidgets('verify readback shows its own progress, not 100 percent', (
    tester,
  ) async {
    const channel = MethodChannel('org.framefilm.ark/methods');
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    messenger.setMockMethodCallHandler(channel, (_) async => null);
    addTearDown(() => messenger.setMockMethodCallHandler(channel, null));
    await tester.binding.setSurfaceSize(const Size(820, 1000));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: StudentPassportEditor(
              controller: StudentPassportEditorController(),
              canOperate: true,
              connected: true,
              supported: true,
              busy: true,
              operation: const {
                'phase': 'verifying',
                'completed': 100,
                'total': 100,
                'readCompleted': 40,
                'readTotal': 80,
              },
              onRead: () async {},
              onSave: (_, _) async {},
              onCancel: () {},
              onOpen: () {},
            ),
          ),
        ),
      ),
    );
    await tester.pump();
    final values = tester
        .widgetList<LinearProgressIndicator>(
          find.byType(LinearProgressIndicator),
        )
        .map((bar) => bar.value);
    expect(values, contains(0.5));
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('student warning distinguishes support from connection and transport', (
    tester,
  ) async {
    const channel = MethodChannel('org.framefilm.ark/methods');
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    messenger.setMockMethodCallHandler(channel, (_) async => null);
    addTearDown(() => messenger.setMockMethodCallHandler(channel, null));
    await tester.binding.setSurfaceSize(const Size(820, 1000));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final controller = StudentPassportEditorController();
    var sent = 0;
    Future<void> show({
      bool connected = true,
      bool reading = false,
      bool? i006,
      bool? colorFast,
      String? transportHint,
    }) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(
              child: StudentPassportEditor(
                controller: controller,
                canOperate: connected && !reading,
                connected: connected,
                supported: i006,
                colorFastSupported: colorFast,
                readingCapabilities: reading,
                canSend: transportHint == null,
                transportHint: transportHint,
                busy: false,
                operation: const {},
                onRead: () async {},
                onSave: (_, _) async { sent++; },
                onCancel: () {},
                onOpen: () {},
              ),
            ),
          ),
        ),
      );
      await tester.pump();
    }

    final unsupported = find.textContaining('当前设备不支持所选学生证模式');
    final overwrite = find.text('发送后会替换设备上的通行证。');
    await show(connected: false);
    expect(find.textContaining('连接 Ark 后可确认'), findsOneWidget);
    expect(unsupported, findsNothing);
    expect(overwrite, findsNothing);
    await show(reading: true);
    expect(find.textContaining('正在读取学生证模式'), findsOneWidget);
    expect(unsupported, findsNothing);
    await show();
    expect(find.textContaining('尚未确认学生证模式'), findsOneWidget);
    expect(unsupported, findsNothing);

    await show(i006: true, colorFast: false);
    expect(unsupported, findsNothing);
    tester.widget<DropdownButtonFormField<String>>(
      find.byKey(const ValueKey('student-i006')),
    ).onChanged!('colorfast');
    await tester.pump();
    expect(controller.profile!.output, 'colorfast');
    expect(unsupported, findsOneWidget);
    expect(overwrite, findsNothing);

    const blocked = '当前连接无法通过蓝牙发送学生证。';
    await show(i006: true, colorFast: true, transportHint: blocked);
    expect(unsupported, findsNothing);
    expect(find.text(blocked), findsOneWidget);
    expect(tester.widget<FilledButton>(
      find.widgetWithText(FilledButton, '发送学生证'),
    ).onPressed, isNull);
    await show(i006: true, colorFast: true);
    expect(unsupported, findsNothing);
    expect(find.text(blocked), findsNothing);
    expect(controller.profile!.output, 'colorfast');
    expect(sent, 0);
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets('student draft and readback stay isolated from operator data', (
    tester,
  ) async {
    final commands = <MethodCall>[];
    const channel = MethodChannel('org.framefilm.ark/methods');
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    messenger.setMockMethodCallHandler(channel, (call) async {
      commands.add(call);
      return null;
    });
    addTearDown(() => messenger.setMockMethodCallHandler(channel, null));
    await tester.binding.setSurfaceSize(const Size(820, 1000));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final controller = StudentPassportEditorController();
    var readCount = 0;
    Widget editor(Map<String, Object?> operation) => MaterialApp(
      home: Scaffold(
        body: SingleChildScrollView(
          child: StudentPassportEditor(
            controller: controller,
            canOperate: true,
            connected: true,
            supported: true,
            busy: false,
            operation: operation,
            onRead: () async => readCount++,
            onSave: (_, _) async {},
            onCancel: () {},
            onOpen: () {},
          ),
        ),
      ),
    );

    Future<void> requestRead() async {
      await tester.ensureVisible(find.text('读取设备资料'));
      await tester.tap(find.text('读取设备资料'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 350));
      await tester.tap(find.text('读取并替换'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 350));
    }

    await tester.pumpWidget(editor({}));
    await tester.pump();
    expect(find.text('卡面预览'), findsOneWidget);
    expect(find.text('导入 PNG 底图'), findsNothing);
    final colorSelector = tester.widget<DropdownButton<String>>(
      find.byKey(const Key('student-palette')),
    );
    expect(colorSelector.value, 'amiya');
    expect(
      colorSelector.items!.map((item) => item.value),
      unorderedEquals(studentPassportPalettes.keys),
    );
    for (final palette in studentPassportPalettes.keys) {
      final png = await rootBundle.load(studentPassportTemplateAsset(palette));
      expect(png.getUint32(16), 720);
      expect(png.getUint32(20), 480);
    }
    await tester.ensureVisible(find.byKey(const Key('student-codename')));
    await tester.enterText(find.byKey(const Key('student-codename')), 'Amiya');
    await tester.pump();
    expect(commands.first.arguments, {'kind': 'student'});
    expect(commands.last.method, 'savePassportDraft');
    expect(commands.last.arguments['kind'], 'student');
    expect(controller.profile!.codename, 'Amiya');
    tester
        .widget<DropdownButton<String>>(find.byKey(const Key('student-palette')))
        .onChanged!('sniper');
    await tester.pump();
    expect(controller.profile!.palette, 'sniper');
    expect(controller.profile!.codename, 'Amiya');
    expect(
      tester
          .widget<FilledButton>(find.widgetWithText(FilledButton, '发送学生证'))
          .onPressed,
      isNull,
    );

    await requestRead();
    final operatorOperation = {
      'revision': 1,
      'phase': 'done',
      'json': const PassportProfile(codename: 'Operator').toJson(),
    };
    await tester.pumpWidget(editor(operatorOperation));
    expect(readCount, 1);
    expect(find.textContaining('设备当前保存的是干员档案'), findsOneWidget);
    expect(controller.profile!.codename, 'Amiya');

    await requestRead();
    await tester.pumpWidget(const SizedBox());
    final studentOperation = {
      'revision': 2,
      'phase': 'done',
      'json': const StudentPassportProfile(codename: 'Student').toJson(),
    };
    await tester.pumpWidget(editor(studentOperation));
    await tester.pump();
    expect(readCount, 2);
    expect(controller.profile!.codename, 'Student');
    expect(
      StudentPassportProfile.fromJson(
        jsonEncode(jsonDecode(commands.last.arguments['json'])['profile']),
      ).codename,
      'Student',
    );
  });
}
