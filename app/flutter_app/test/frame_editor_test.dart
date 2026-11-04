import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/frame_codec.dart';
import 'package:framefilm_ark/frame_editor.dart';

void main() {
  testWidgets(
    'continuous tone survives editing selection and rotation, reset opts out',
    (tester) async {
      expect(const FrameOptions().continuousTone, false);
      final recorder = ui.PictureRecorder();
      final canvas = ui.Canvas(recorder);
      canvas.drawColor(const ui.Color(0xff906050), ui.BlendMode.src);
      final picture = recorder.endRecording();
      final image = await tester.runAsync(() => picture.toImage(2, 2));
      final encoded = await tester.runAsync(
        () => image!.toByteData(format: ui.ImageByteFormat.png),
      );
      final bytes = encoded!.buffer.asUint8List();
      image!.dispose();
      picture.dispose();
      final c = FrameEditorController()
        ..options = const FrameOptions(algorithm: FrameAlgorithm.abc512I006);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(
              child: FrameEditor(
                controller: c,
                canImportFilm: false,
                onImportFilm: (_, _) async {},
                pickImage: () async => bytes,
              ),
            ),
          ),
        ),
      );
      final toggle = find.byKey(const ValueKey('film-continuous-tone'));
      await tester.ensureVisible(toggle);
      await tester.tap(toggle);
      await tester.pump();
      expect(c.options.continuousTone, true);
      final contrast = tester.widget<Slider>(find.byType(Slider).first);
      contrast.onChanged!(1.2);
      await tester.pump();
      expect(c.options.continuousTone, true);
      await tester.ensureVisible(find.text('上传图像'));
      await tester.runAsync(() async {
        await tester.tap(find.text('上传图像'));
        for (var i = 0; i < 200 && c.source == null; i++) {
          await Future<void>.delayed(const Duration(milliseconds: 10));
        }
      });
      expect(c.source, bytes);
      expect(c.options.continuousTone, true);
      await tester.pump();
      await tester.ensureVisible(find.text('旋转90度'));
      await tester.tap(find.text('旋转90度'));
      await tester.pump();
      expect(c.options.quarterTurns, 1);
      expect(c.options.continuousTone, true);
      await tester.ensureVisible(find.text('重置'));
      await tester.tap(find.text('重置'));
      await tester.pump();
      expect(c.options.continuousTone, false);
      expect(c.source, isNull);
      await tester.pumpWidget(const SizedBox());
      await tester.pump(const Duration(seconds: 1));
      c.dispose();
    },
  );
  testWidgets('重置后调整参数提示上传图像，不显示预览处理中', (tester) async {
    final c = FrameEditorController();
    c.source = Uint8List.fromList([1]);
    c.result = FrameResult(
      png: Uint8List(0),
      film: Uint8List.fromList([55]),
      format: FrameFormat.sixColor,
    );
    c.resultRevision = c.revision;
    addTearDown(c.dispose);
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: FrameEditor(
              controller: c,
              canImportFilm: true,
              onImportFilm: (_, _) async {},
            ),
          ),
        ),
      ),
    );
    // Reset discards the loaded photo, but controls can still preset options.
    await tester.ensureVisible(find.text('重置'));
    await tester.tap(find.text('重置'));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('扩展选项'));
    await tester.tap(find.text('扩展选项'));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('黑白 MonoFast'));
    await tester.tap(find.text('黑白 MonoFast'));
    await tester.pumpAndSettle();
    expect(c.options.format, FrameFormat.monoFast);
    expect(c.source, isNull);
    expect(find.text('参数已更新，请上传图像以生成预览'), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsNothing);
    expect(c.hasCurrentResult, isFalse);
    await tester.ensureVisible(find.text('导入直传区'));
    final button = tester.widget<OutlinedButton>(
      find.ancestor(
        of: find.text('导入直传区'),
        matching: find.byType(OutlinedButton),
      ),
    );
    expect(button.onPressed, isNull);
  });

  testWidgets('选择失败保留当前结果，参数变化禁用旧结果导入', (tester) async {
    final c = FrameEditorController();
    c.result = FrameResult(
      png: Uint8List(0),
      film: Uint8List.fromList([55]),
      format: FrameFormat.sixColor,
    );
    c.resultRevision = c.revision;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: FrameEditor(
              controller: c,
              canImportFilm: true,
              onImportFilm: (_, name) async {},
              pickImage: () async => throw const FormatException('坏图'),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('上传图像'));
    await tester.pumpAndSettle();
    expect(c.hasCurrentResult, isTrue);
    expect(find.textContaining('加载失败'), findsOneWidget);
    await tester.ensureVisible(find.text('扩展选项'));
    await tester.tap(find.text('扩展选项'));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('黑白 MonoFast'));
    await tester.tap(find.text('黑白 MonoFast'));
    await tester.pumpAndSettle();
    expect(c.hasCurrentResult, isFalse);
    await tester.ensureVisible(find.text('导入直传区'));
    final button = tester.widget<OutlinedButton>(
      find.ancestor(
        of: find.text('导入直传区'),
        matching: find.byType(OutlinedButton),
      ),
    );
    expect(button.onPressed, isNull);
  });
  testWidgets('切页取消未完成加载并保留旧内容', (tester) async {
    final c = FrameEditorController();
    c.result = FrameResult(
      png: Uint8List(0),
      film: Uint8List.fromList([55]),
      format: FrameFormat.sixColor,
    );
    c.resultRevision = c.revision;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: FrameEditor(
              controller: c,
              canImportFilm: false,
              onImportFilm: (_, name) async {},
              pickImage: () async {
                await Future<void>.delayed(const Duration(seconds: 1));
                return Uint8List(2);
              },
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('上传图像'));
    await tester.pumpWidget(const MaterialApp(home: SizedBox()));
    await tester.pump(const Duration(seconds: 2));
    expect(c.source, isNull);
    expect(c.hasCurrentResult, isTrue);
    expect(tester.takeException(), isNull);
    c.dispose();
    expect(c.result, isNull);
  });
}
