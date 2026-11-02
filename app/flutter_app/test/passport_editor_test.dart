import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/passport_editor.dart';
import 'package:framefilm_ark/passport_profile.dart';

void main() {
  testWidgets(
    'read completed off page applies once and disconnected open is disabled',
    (tester) async {
      final controller = PassportEditorController()
        ..initialized = true
        ..consumedRevision = 1
        ..profile = const PassportProfile(codename: '本地草稿');
      Widget editor() => MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: PassportEditor(
              controller: controller,
              canOperate: false,
              busy: false,
              operation: {
                'revision': 2,
                'json': const PassportProfile(codename: '已读取').toJson(),
              },
              onRead: () async {},
              onSave: (_, _) async {},
              onCancel: () {},
              onOpen: () {},
              loadDraft: () async => null,
              saveDraft: (_) async {},
            ),
          ),
        ),
      );
      await tester.pumpWidget(editor());
      await tester.pump();
      expect(controller.profile!.codename, '已读取');
      expect(controller.consumedRevision, 2);
      expect(
        tester
            .widget<TextButton>(find.widgetWithText(TextButton, '在设备上打开'))
            .onPressed,
        isNull,
      );
      await tester.enterText(find.byKey(const Key('operator-name')), '继续编辑');
      await tester.pumpWidget(const SizedBox());
      await tester.pumpWidget(editor());
      await tester.pump();
      expect(controller.profile!.codename, '继续编辑');
      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(() async {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      });
    },
  );
  testWidgets(
    'local draft survives page disposal and historic read revision is not reapplied',
    (tester) async {
      final controller = PassportEditorController();
      final drafts = <String>[];
      Widget editor() => MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: PassportEditor(
              controller: controller,
              canOperate: true,
              busy: false,
              operation: {
                'revision': 1,
                'json': const PassportProfile(codename: '旧资料').toJson(),
              },
              onRead: () async {},
              onSave: (_, _) async {},
              onCancel: () {},
              onOpen: () {},
              loadDraft: () async => null,
              saveDraft: (s) async {
                drafts.add(s);
              },
            ),
          ),
        ),
      );
      await tester.pumpWidget(editor());
      await tester.enterText(find.byKey(const Key('operator-name')), '新资料');
      await tester.pump();
      await tester.pumpWidget(const SizedBox());
      await tester.pumpWidget(editor());
      await tester.pump();
      expect(
        tester
            .widget<TextField>(find.byKey(const Key('operator-name')))
            .controller!
            .text,
        '新资料',
      );
      expect(PassportProfile.fromJson(drafts.last).codename, '新资料');
      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(() async {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      });
    },
  );
}
