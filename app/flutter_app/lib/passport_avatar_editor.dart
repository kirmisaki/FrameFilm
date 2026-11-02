import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/gestures.dart';

import 'frame_codec.dart';
import 'frame_algorithms.dart' show quantizeFramePixels;
import 'live_image_composition.dart';

const avatarAlgorithms = {
  'adaptive': '自适应（与 Film 相同）',
  'floydSteinberg': 'Floyd-Steinberg',
  'atkinson': 'Atkinson',
  'bayer': 'Bayer 4×4',
};

class _AvatarPan extends PanGestureRecognizer {
  @override
  void addAllowedPointer(PointerDownEvent event) {
    super.addAllowedPointer(event);
    resolve(GestureDisposition.accepted);
  }
}

const avatarDefaults = <String, dynamic>{
  'turns': 0,
  'zoom': 1.0,
  'x': 0.0,
  'y': 0.0,
  'brightness': 0.0,
  'contrast': 1.0,
  'saturation': 1.0,
  'dither': true,
  'strength': 1.0,
  'algorithm': 'adaptive',
};

class PassportAvatarEdit {
  PassportAvatarEdit(this.source, [Map<String, dynamic>? options])
    : options = {...avatarDefaults, ...?options};
  final String source;
  final Map<String, dynamic> options;
  Map<String, dynamic> toJson() => {'source': source, 'options': options};
  factory PassportAvatarEdit.fromJson(Map value) {
    final source = value['source'];
    if (source is! String ||
        !source.startsWith('data:image/png;base64,') ||
        source.length > 6000000) {
      throw const FormatException('头像源图无效');
    }
    final options = {
      ...avatarDefaults,
      ...Map<String, dynamic>.from(value['options'] as Map? ?? {}),
    };
    for (final entry in <String, (double, double)>{
      'turns': (0, 3),
      'zoom': (.1, 8),
      'x': (-1024, 1024),
      'y': (-1024, 1024),
      'brightness': (-1, 1),
      'contrast': (.5, 2),
      'saturation': (0, 3),
      'strength': (0, 3),
    }.entries) {
      final n = options[entry.key];
      if (n is! num ||
          !n.isFinite ||
          n < entry.value.$1 ||
          n > entry.value.$2) {
        throw const FormatException('头像调整参数无效');
      }
    }
    if (options['dither'] is! bool ||
        !avatarAlgorithms.containsKey(options['algorithm'])) {
      throw const FormatException('头像算法无效');
    }
    return PassportAvatarEdit(source, options);
  }
}

Future<String> _png(ui.Image image) async =>
    'data:image/png;base64,${base64Encode((await image.toByteData(format: ui.ImageByteFormat.png))!.buffer.asUint8List())}';
Future<String> prepareAvatarSource(Uint8List bytes) async {
  if (bytes.length > 8 * 1024 * 1024)
    throw const FormatException('请选择不超过8MiB的头像');
  final codec = await ui.instantiateImageCodec(bytes);
  final image = (await codec.getNextFrame()).image;
  codec.dispose();
  try {
    final ratio = math.min(1.0, 640 / math.max(image.width, image.height));
    final w = math.max(1, (image.width * ratio).round()),
        h = math.max(1, (image.height * ratio).round());
    final recorder = ui.PictureRecorder();
    final c = Canvas(recorder);
    c.drawColor(Colors.white, BlendMode.src);
    c.drawImageRect(
      image,
      Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      Rect.fromLTWH(0, 0, w.toDouble(), h.toDouble()),
      Paint()..filterQuality = FilterQuality.high,
    );
    final picture = recorder.endRecording();
    final resized = await picture.toImage(w, h);
    picture.dispose();
    try {
      final source = await _png(resized);
      if (source.length > 6000000) throw const FormatException('头像源图过大，请换一张图片');
      return source;
    } finally {
      resized.dispose();
    }
  } finally {
    image.dispose();
  }
}

Future<({String crop, String processed})> renderAvatarEdit(
  PassportAvatarEdit edit,
  String mode, {
  void Function()? checkCancelled,
}) async {
  final o = edit.options;
  final codec = await ui.instantiateImageCodec(
    base64Decode(edit.source.substring(22)),
  );
  final source = (await codec.getNextFrame()).image;
  codec.dispose();
  final recorder = ui.PictureRecorder();
  final c = Canvas(recorder);
  c.drawColor(Colors.white, BlendMode.src);
  final turns = (o['turns'] as num).toInt();
  final rotatedW = turns.isOdd ? source.height : source.width;
  final rotatedH = turns.isOdd ? source.width : source.height;
  final scale = math.max(128 / rotatedW, 160 / rotatedH) * (o['zoom'] as num);
  c.translate(64 + (o['x'] as num).toDouble(), 80 + (o['y'] as num).toDouble());
  c.rotate(turns * math.pi / 2);
  c.scale(scale);
  c.drawImage(
    source,
    Offset(-source.width / 2, -source.height / 2),
    Paint()..filterQuality = FilterQuality.high,
  );
  final picture = recorder.endRecording();
  final crop = await picture.toImage(128, 160);
  picture.dispose();
  source.dispose();
  final cropPng = await _png(crop);
  final rgba = (await crop.toByteData(format: ui.ImageByteFormat.rawRgba))!
      .buffer
      .asUint8List();
  crop.dispose();
  adjustFrameRgba(
    rgba,
    FrameOptions(
      brightness: (o['brightness'] as num).toDouble(),
      contrast: (o['contrast'] as num).toDouble(),
      saturation: mode == 'color' ? (o['saturation'] as num).toDouble() : 0,
    ),
  );
  if (mode == 'mono') {
    await quantizeFrameMono(
      rgba,
      o['dither'] as bool,
      () {
        checkCancelled?.call();
        return false;
      },
      null,
      width: 128,
      height: 160,
    );
  } else if (mode == 'color' || o['dither'] == true) {
    await quantizeFramePixels(
      rgba,
      128,
      160,
      algorithm: FrameAlgorithm.values.byName(o['algorithm'] as String),
      dither: o['dither'] as bool,
      strength: (o['strength'] as num).toDouble(),
      checkCancelled: checkCancelled,
    );
  } else {
    await quantizeFrameMono(rgba, false, null, null, width: 128, height: 160);
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(rgba);
  final d = ui.ImageDescriptor.raw(
    buffer,
    width: 128,
    height: 160,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final outCodec = await d.instantiateCodec();
  final image = (await outCodec.getNextFrame()).image;
  try {
    return (crop: cropPng, processed: await _png(image));
  } finally {
    image.dispose();
    outCodec.dispose();
    d.dispose();
    buffer.dispose();
  }
}

class PassportAvatarDialog extends StatefulWidget {
  const PassportAvatarDialog({
    super.key,
    required this.edit,
    required this.mode,
  });
  final PassportAvatarEdit edit;
  final String mode;
  @override
  State<PassportAvatarDialog> createState() => _PassportAvatarDialogState();
}

class _PassportAvatarDialogState extends State<PassportAvatarDialog> {
  late Map<String, dynamic> options = {...widget.edit.options};
  String? crop, processed, error;
  int generation = 0;
  bool pending = true, rendering = false;
  late final Uint8List sourceBytes = base64Decode(
    widget.edit.source.substring(22),
  );
  @override
  void initState() {
    super.initState();
    update();
  }

  @override
  void dispose() {
    generation++;
    super.dispose();
  }

  Future<void> update() async {
    final g = ++generation;
    if (rendering) {
      setState(() => pending = true);
      return;
    }
    rendering = true;
    final snapshot = {...options};
    setState(() {
      pending = true;
      error = null;
    });
    try {
      final result = await renderAvatarEdit(
        PassportAvatarEdit(widget.edit.source, snapshot),
        widget.mode,
        checkCancelled: () {
          if (!mounted) {
            throw StateError('头像预览已取消');
          }
        },
      );
      if (mounted && g == generation)
        setState(() {
          crop = result.crop;
          processed = result.processed;
          pending = false;
        });
    } catch (e) {
      if (mounted && g == generation)
        setState(() {
          error = '$e';
          pending = false;
        });
    } finally {
      rendering = false;
      if (mounted && g != generation) update();
    }
  }

  void change(String name, dynamic value) {
    options[name] = value;
    update();
  }

  void drag(DragUpdateDetails d) {
    options['x'] = ((options['x'] as num) + d.delta.dx).clamp(-1024, 1024);
    options['y'] = ((options['y'] as num) + d.delta.dy).clamp(-1024, 1024);
    update();
  }

  Widget slider(String name, String title, double min, double max) => Column(
    children: [
      Text('$title ${(options[name] as num).toStringAsFixed(1)}'),
      Slider(
        value: (options[name] as num).toDouble(),
        min: min,
        max: max,
        onChanged: (v) => change(name, v),
      ),
    ],
  );
  @override
  Widget build(BuildContext context) => AlertDialog(
    title: const Text('调整头像'),
    content: SizedBox(
      width: 440,
      child: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text('构图预览可拖动；设备预览显示最终像素'),
            Wrap(
              alignment: WrapAlignment.center,
              spacing: 8,
              runSpacing: 8,
              children: [
                RawGestureDetector(
                  behavior: HitTestBehavior.opaque,
                  gestures: {
                    _AvatarPan:
                        GestureRecognizerFactoryWithHandlers<_AvatarPan>(
                          () => _AvatarPan(),
                          (r) {
                            r.onUpdate = drag;
                          },
                        ),
                  },
                  child: SizedBox(
                    width: 128,
                    height: 160,
                    child: LiveImageComposition(
                      key: const ValueKey('avatar-live-composition'),
                      source: sourceBytes,
                      width: 128,
                      height: 160,
                      options: FrameOptions(
                        quarterTurns: (options['turns'] as num).toInt(),
                        zoom: (options['zoom'] as num).toDouble(),
                        panX: (options['x'] as num).toDouble(),
                        panY: (options['y'] as num).toDouble(),
                        brightness: (options['brightness'] as num).toDouble(),
                        contrast: (options['contrast'] as num).toDouble(),
                        saturation: widget.mode == 'color'
                            ? (options['saturation'] as num).toDouble()
                            : 0,
                      ),
                    ),
                  ),
                ),
                SizedBox(
                  width: 128,
                  height: 160,
                  child: processed == null
                      ? const SizedBox()
                      : Image.memory(
                          base64Decode(processed!.substring(22)),
                          gaplessPlayback: true,
                        ),
                ),
              ],
            ),
            if (pending) const LinearProgressIndicator(),
            if (error != null) Text(error!),
            Wrap(
              children: [
                TextButton(
                  onPressed: () {
                    options['turns'] =
                        ((options['turns'] as num).toInt() + 1) % 4;
                    options['x'] = 0.0;
                    options['y'] = 0.0;
                    update();
                  },
                  child: const Text('旋转90度'),
                ),
                TextButton(
                  onPressed: () {
                    options = {...avatarDefaults};
                    update();
                  },
                  child: const Text('重置调整'),
                ),
              ],
            ),
            slider('zoom', '缩放', .1, 8),
            slider('brightness', '亮度', -1, 1),
            slider('contrast', '对比度', .5, 2),
            slider('saturation', '饱和度', 0, 3),
            SwitchListTile(
              title: const Text('抖动'),
              value: options['dither'] as bool,
              onChanged: (v) => change('dither', v),
            ),
            if (widget.mode == 'mono')
              const Text(
                '黑白头像使用 Film MonoFast：开启抖动使用 Floyd-Steinberg，关闭时直接黑白量化。',
              )
            else ...[
              if (widget.mode == 'grayscale')
                const Text('灰度头像先转为灰阶，再使用所选 Film 算法生成黑白空间半色调。'),
              DropdownButton<String>(
                value: options['algorithm'] as String,
                isExpanded: true,
                items: avatarAlgorithms.entries
                    .map(
                      (e) =>
                          DropdownMenuItem(value: e.key, child: Text(e.value)),
                    )
                    .toList(),
                onChanged: options['dither'] == true
                    ? (v) {
                        if (v != null) change('algorithm', v);
                      }
                    : null,
              ),
              if (options['dither'] == true)
                if (options['algorithm'] == 'adaptive')
                  const Text('根据头像细节和色彩自动选择抖动算法与强度。可继续调整构图、亮度和对比度。')
                else
                  slider('strength', '抖动强度', 0, 3),
            ],
          ],
        ),
      ),
    ),
    actions: [
      TextButton(
        onPressed: () => Navigator.pop(context),
        child: const Text('取消'),
      ),
      FilledButton(
        onPressed: pending || processed == null || error != null
            ? null
            : () => Navigator.pop(context, (
                edit: PassportAvatarEdit(widget.edit.source, {...options}),
                png: processed!,
              )),
        child: const Text('应用头像'),
      ),
    ],
  );
}
