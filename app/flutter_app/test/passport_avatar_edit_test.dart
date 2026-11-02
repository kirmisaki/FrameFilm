import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/passport_avatar_editor.dart';
import 'package:framefilm_ark/passport_editor.dart';
import 'package:framefilm_ark/passport_profile.dart';
import 'package:framefilm_ark/frame_algorithms.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  Future<String> source() async {
    final r = ui.PictureRecorder();
    final c = Canvas(r);
    c.drawColor(const Color(0xffd5a58c), BlendMode.src);
    c.drawRect(
      const Rect.fromLTWH(0, 0, 100, 160),
      Paint()..color = Colors.blue,
    );
    final p = r.endRecording();
    final image = await p.toImage(320, 160);
    p.dispose();
    final bytes = (await image.toByteData(format: ui.ImageByteFormat.png))!
        .buffer
        .asUint8List();
    image.dispose();
    return prepareAvatarSource(bytes);
  }

  Future<List<int>> rgba(String png) async {
    final codec = await ui.instantiateImageCodec(
      base64Decode(png.substring(22)),
    );
    final image = (await codec.getNextFrame()).image;
    final bytes = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!
        .buffer
        .asUint8List()
        .toList();
    image.dispose();
    codec.dispose();
    return bytes;
  }

  test('source/options roundtrip and composition remain separate from quantized output', () async {
    final src = await source();
    final edit = PassportAvatarEdit.fromJson(
      jsonDecode(
        jsonEncode(
          PassportAvatarEdit(src, {
            'x': 30.0,
            'zoom': 1.2,
            'turns': 1,
          }).toJson(),
        ),
      ) as Map,
    );
    expect(edit.source, src);
    expect(edit.options['x'], 30);
    expect(edit.options['turns'], 1);
    final original = await renderAvatarEdit(PassportAvatarEdit(src), 'color');
    final shifted = await renderAvatarEdit(
      PassportAvatarEdit(src, {'x': 60.0}),
      'color',
    );
    expect(shifted.crop, isNot(original.crop));
    final noDither = await renderAvatarEdit(
      PassportAvatarEdit(src, {'dither': false}),
      'color',
    );
    expect(original.processed, isNot(noDither.processed));
    expect(original.crop, noDither.crop);
    final colors = <String>{};
    final pixels = await rgba(original.processed);
    for (var i = 0; i < pixels.length; i += 4) {
      colors.add('${pixels[i]},${pixels[i + 1]},${pixels[i + 2]}');
    }
    expect(colors.length, greaterThan(2));
    final gray = await renderAvatarEdit(PassportAvatarEdit(src), 'grayscale');
    final g = await rgba(gray.processed);
    for (var i = 0; i < g.length; i += 4) {
      expect(g[i], anyOf(0, 255));
      expect(g[i + 1], g[i]);
      expect(g[i + 2], g[i]);
    }
  });
  test(
    'adaptive avatar uses Film quantization and preserves saved algorithms',
    () async {
      final src = await source();
      final edit = PassportAvatarEdit(src);
      expect(edit.options['algorithm'], 'adaptive');
      final rendered = await renderAvatarEdit(edit, 'color');
      final expected = Uint8List.fromList(await rgba(rendered.crop));
      adjustFrameColors(expected, contrast: 1, saturation: 1);
      await quantizeFramePixels(
        expected,
        128,
        160,
        algorithm: FrameAlgorithm.adaptive,
      );
      expect(await rgba(rendered.processed), orderedEquals(expected));
      final saved = PassportAvatarEdit.fromJson({
        'source': src,
        'options': {'algorithm': 'atkinson', 'strength': .8},
      });
      expect(saved.options['algorithm'], 'atkinson');
      expect(saved.options['strength'], .8);
      var checks = 0;
      await expectLater(
        renderAvatarEdit(
          edit,
          'color',
          checkCancelled: () {
            checks++;
            throw StateError('cancelled');
          },
        ),
        throwsStateError,
      );
      expect(checks, greaterThan(0));
    },
  );
  test('unsupported palette modes and invalid stored options rejected', () {
    expect(
      () => PassportAvatarEdit.fromJson({
        'source': 'data:image/png;base64,AA==',
        'options': {'algorithm': 'colorFast55'},
      }),
      throwsFormatException,
    );
    expect(
      () => PassportAvatarEdit.fromJson({
        'source': 'data:image/png;base64,AA==',
        'options': {'zoom': double.nan},
      }),
      throwsFormatException,
    );
  });
  testWidgets(
    'local draft restores source and settings across page recreation; device read clears them',
    (tester) async {
      late String src, png;
      await tester.runAsync(() async {
        src = await source();
        png = (await renderAvatarEdit(
          PassportAvatarEdit(src),
          'color',
        )).processed;
      });
      final profile = PassportProfile(avatar: png, avatarMode: 'color');
      final draft = jsonEncode({
        'draftVersion': 2,
        'profile': jsonDecode(profile.toJson()),
        'avatarEditor': PassportAvatarEdit(src, {'x': 24.0}).toJson(),
      });
      final controller = PassportEditorController();
      Widget page({Map<String, Object?> operation = const {}}) => MaterialApp(
        home: Scaffold(
          body: SingleChildScrollView(
            child: PassportEditor(
              controller: controller,
              canOperate: false,
              busy: false,
              operation: operation,
              onRead: () async {},
              onSave: (_, _) async {},
              onCancel: () {},
              onOpen: () {},
              loadDraft: () async => draft,
              saveDraft: (_) async {},
            ),
          ),
        ),
      );
      await tester.pumpWidget(page());
      await tester.runAsync(() async {
        await Future<void>.delayed(const Duration(milliseconds: 150));
      });
      await tester.pump();
      expect(controller.avatarEdit?.source, src);
      expect(controller.avatarEdit?.options['x'], 24);
      for (final mode in ['grayscale', 'color']) {
        await tester.runAsync(() async {
          tester
              .widget<DropdownButtonFormField<String>>(
                find.byType(DropdownButtonFormField<String>),
              )
              .onChanged!(mode);
          await Future<void>.delayed(const Duration(milliseconds: 150));
        });
        await tester.pump();
        expect(controller.profile?.avatarMode, mode);
        expect(controller.avatarEdit?.source, src);
      }
      late String expectedColor;
      await tester.runAsync(() async {
        expectedColor = (await renderAvatarEdit(
          PassportAvatarEdit(src, {'x': 24.0}),
          'color',
        )).processed;
      });
      expect(controller.profile?.avatar, expectedColor);
      await tester.pumpWidget(const SizedBox());
      await tester.pumpWidget(page());
      expect(controller.avatarEdit?.source, src);
      await tester.pumpWidget(
        page(
          operation: {
            'revision': 1,
            'json': const PassportProfile(codename: '设备').toJson(),
          },
        ),
      );
      await tester.runAsync(() async {
        await Future<void>.delayed(const Duration(milliseconds: 150));
      });
      await tester.pump();
      expect(controller.avatarEdit, isNull);
      expect(controller.profile?.codename, '设备');
      await tester.pumpWidget(const SizedBox());
      await tester.runAsync(() async {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      });
    },
  );
}
