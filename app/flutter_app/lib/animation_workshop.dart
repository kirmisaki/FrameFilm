import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

import 'animation_film.dart';
import 'animation_drawing.dart';
import 'animation_media.dart';
import 'frame_codec.dart';
import 'frame_io.dart' as frame_io;
import 'frame_quick_page.dart';

typedef PlayAnimationOnDevice = Future<void> Function({
  required int playMode,
  required int intervalMs,
  required int loopSeconds,
});

// Claim pen strokes before an ancestor Scrollable can win a vertical drag.
// This recognizer is installed only over the active, idle pen canvas.
class _CanvasPanGestureRecognizer extends PanGestureRecognizer {
  @override
  void addAllowedPointer(PointerDownEvent event) {
    super.addAllowedPointer(event);
    resolve(GestureDisposition.accepted);
  }
}

class AnimationFrameTransform {
  AnimationFrameTransform(this.source);
  final Uint8List source;
  int turns = 0;
  double zoom = 1, x = 0, y = 0;
  FrameOptions get options => FrameOptions(
    fit: FrameFit.contain,
    quarterTurns: turns,
    zoom: zoom,
    panX: x,
    panY: y,
  );
  void copyFrom(AnimationFrameTransform other) {
    turns = other.turns;
    zoom = other.zoom;
    x = other.x;
    y = other.y;
  }
}

class AnimationWorkshopController extends ChangeNotifier {
  bool _busy = false, _notificationPending = false, _disposed = false;
  bool get busy => _busy;
  set busy(bool value) {
    if (_busy == value || _disposed) return;
    _busy = value;
    // A page can be disposed during its parent's build. Deliver listeners after
    // that synchronous lifecycle work, while navigation guards read busy now.
    if (_notificationPending) return;
    _notificationPending = true;
    scheduleMicrotask(() {
      _notificationPending = false;
      if (!_disposed) notifyListeners();
    });
  }

  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }

  final frames = <Uint8List>[];
  final transforms = <Uint8List, AnimationFrameTransform>{};
  bool transformAll = false;
  final strokes = <List<Offset>>[];
  final strokeColors = <Color>[];
  final strokeWidths = <double>[];
  final strokeSymmetry = <bool>[];
  final rasterUndo = <Uint8List>[];
  String drawingTool = 'pen';
  Uint8List? editingBase;
  int? editingIndex;
  bool onionSkin = false, symmetry = false;
  int playMode = 0;
  bool drawing = false;
  String fileName = 'animation.film';
  String? status;
  int selected = 0;
  double fps = 2, loopSeconds = 0;
  bool dither = true;
  FrameFormat format = FrameFormat.monoFast;
  void clear() {
    frames.clear();
    transforms.clear();
    strokes.clear();
    strokeColors.clear();
    strokeWidths.clear();
    strokeSymmetry.clear();
    rasterUndo.clear();
    editingBase = null;
    editingIndex = null;
    drawing = false;
    selected = 0;
  }
}

class AnimationWorkshop extends StatefulWidget {
  const AnimationWorkshop({
    super.key,
    required this.pickImages,
    required this.sendFilm,
    this.cancelSend,
    this.saveFilm,
    this.playOnDevice,
    this.captureImage,
    this.connected = false,
    this.disabledReason,
    this.controller,
  });
  final AnimationWorkshopController? controller;
  final PickFrameImages pickImages;
  final CaptureFrameImage? captureImage;
  final SendFrameFilm sendFilm;
  final CancelFrameSend? cancelSend;
  final SendFrameFilm? saveFilm;
  final PlayAnimationOnDevice? playOnDevice;
  final bool connected;
  final String? disabledReason;
  @override
  State<AnimationWorkshop> createState() => _AnimationWorkshopState();
}

class _AnimationWorkshopState extends State<AnimationWorkshop> {
  late final AnimationWorkshopController _controller =
      widget.controller ?? AnimationWorkshopController();
  List<Uint8List> get _frames => _controller.frames;
  late final _name = TextEditingController(text: _controller.fileName);
  List<List<Offset>> get _strokes => _controller.strokes;
  List<Color> get _strokeColors => _controller.strokeColors;
  Timer? _timer;
  int get _selected => _controller.selected;
  set _selected(int value) => _controller.selected = value;
  double get _fps => _controller.fps;
  set _fps(double value) => _controller.fps = value;
  double get _loopSeconds => _controller.loopSeconds;
  set _loopSeconds(double value) => _controller.loopSeconds = value;
  double _brush = 12, _progress = 0;
  Color _color = Colors.black;
  bool get _drawing => _controller.drawing;
  set _drawing(bool value) => _controller.drawing = value;
  bool get _busy => _controller.busy;
  set _busy(bool value) => _controller.busy = value;
  bool _cancelled = false, _sending = false;
  bool get _dither => _controller.dither;
  set _dither(bool value) => _controller.dither = value;
  bool _afterPlay = true;
  String? get _status => _controller.status;
  set _status(String? value) => _controller.status = value;
  FrameFormat get _format => _controller.format;
  set _format(FrameFormat value) => _controller.format = value;
  Uint8List? _result;
  final _devicePreviews = <Uint8List, Uint8List>{};
  void _invalidate() {
    _result = null;
    _devicePreviews.clear();
  }

  void _stop() {
    _timer?.cancel();
    _timer = null;
  }

  void _check() {
    if (_cancelled || !mounted) throw const FrameCancelledException();
  }

  @override
  void dispose() {
    _cancelled = true;
    _stop();
    _controller.fileName = _name.text;
    _controller.busy = false;
    if (widget.controller == null) _controller.dispose();
    _name.dispose();
    super.dispose();
  }

  Future<void> _import() async {
    _stop();
    setState(() {
      _busy = true;
      _cancelled = false;
      _status = '正在导入图片 / GIF…';
    });
    final added = <Uint8List>[];
    try {
      final photos = await widget.pickImages(multiple: true);
      for (final photo in photos) {
        _check();
        added.addAll(
          await decodeAnimationImages(
            photo.bytes,
            limit: 48 - _frames.length - added.length,
            isCancelled: () => _cancelled || !mounted,
          ),
        );
      }
      _check();
      setState(() {
        _frames.addAll(added);
        _drawing = false;
        _invalidate();
        _status = '已导入 ${added.length} 帧 · 最多 48 帧';
      });
    } catch (e) {
      if (mounted) setState(() => _status = '$e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _capture() async {
    setState(() {
      _busy = true;
      _status = '正在拍照…';
    });
    try {
      final photo = await widget.captureImage?.call();
      if (!mounted || photo == null) return;
      final frames = await decodeAnimationImages(photo.bytes, limit: 1);
      if (!mounted) return;
      setState(() {
        _frames.addAll(frames);
        _selected = _frames.length - 1;
        _drawing = false;
        _invalidate();
        _status = '已添加相机帧';
      });
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  void _beginDrawing({bool edit = false}) {
    setState(() {
      _drawing = true;
      _strokes.clear();
      _strokeColors.clear();
      _controller.strokeWidths.clear();
      _controller.strokeSymmetry.clear();
      _controller.editingBase = edit ? _frames[_selected] : null;
      _controller.editingIndex = edit ? _selected : null;
      _controller.rasterUndo.clear();
    });
  }

  Future<void> _addDrawing() async {
    if (_busy) return;
    setState(() => _busy = true);
    try {
      await _renderDrawing();
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<ui.Image> _drawingImage() async {
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder);
    canvas.drawColor(Colors.white, BlendMode.src);
    final base = _controller.editingBase;
    if (base != null) {
      final png = await prepareFrame(
        base,
        const FrameOptions(fit: FrameFit.contain),
      );
      final codec = await ui.instantiateImageCodec(png);
      try {
        final image = (await codec.getNextFrame()).image;
        canvas.drawImage(image, Offset.zero, Paint());
        image.dispose();
      } finally {
        codec.dispose();
      }
    }
    _paintStrokes(
      canvas,
      _strokes,
      _strokeColors,
      _controller.strokeWidths,
      _controller.strokeSymmetry,
    );
    final picture = recorder.endRecording();
    try {
      return await picture.toImage(480, 720);
    } finally {
      picture.dispose();
    }
  }

  Future<void> _renderDrawing() async {
    final image = await _drawingImage();
    try {
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      if (!mounted) return;
      setState(() {
        final target = _controller.editingIndex;
        if (target != null && target < _frames.length) {
          _controller.transforms.remove(_frames[target]);
          _frames[target] = data!.buffer.asUint8List();
          _selected = target;
        } else {
          _frames.add(data!.buffer.asUint8List());
          _selected = _frames.length - 1;
        }
        _drawing = false;
        _invalidate();
      });
    } finally {
      image.dispose();
    }
  }

  Future<void> _rasterTool(Offset point, Size size) async {
    if (_busy) return;
    setState(() => _busy = true);
    ui.Image? image;
    ui.Image? filledImage;
    try {
      image = await _drawingImage();
      if (!mounted) return;
      final x = (point.dx * 480 / size.width).floor().clamp(0, 479);
      final y = (point.dy * 720 / size.height).floor().clamp(0, 719);
      final rgba = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!
          .buffer
          .asUint8List();
      if (_controller.drawingTool == 'pick') {
        setState(() {
          _color = Color(animationPixelColor(rgba, 480, 720, x, y));
          _controller.drawingTool = 'pen';
        });
        return;
      }
      final before = (await image.toByteData(format: ui.ImageByteFormat.png))!
          .buffer
          .asUint8List();
      final filled = await floodAnimationPixels(
        rgba,
        480,
        720,
        x,
        y,
        _color.toARGB32(),
        isCancelled: () => !mounted,
      );
      final completer = Completer<ui.Image>();
      ui.decodeImageFromPixels(
        filled,
        480,
        720,
        ui.PixelFormat.rgba8888,
        completer.complete,
      );
      filledImage = await completer.future;
      final png = (await filledImage.toByteData(
        format: ui.ImageByteFormat.png,
      ))!.buffer.asUint8List();
      if (!mounted) return;
      setState(() {
        _controller.rasterUndo.add(before);
        if (_controller.rasterUndo.length > 4) {
          _controller.rasterUndo.removeAt(0);
        }
        _controller.editingBase = png;
        _strokes.clear();
        _strokeColors.clear();
        _controller.strokeWidths.clear();
        _controller.strokeSymmetry.clear();
      });
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      image?.dispose();
      filledImage?.dispose();
      if (mounted) setState(() => _busy = false);
    }
  }

  void _play() {
    if (_timer != null) {
      setState(_stop);
      return;
    }
    if (_frames.isEmpty) return;
    _timer = Timer.periodic(Duration(milliseconds: (1000 / _fps).round()), (_) {
      if (mounted) setState(() => _selected = (_selected + 1) % _frames.length);
    });
    setState(() {});
  }

  Future<Uint8List> _generate() async {
    if (_result != null) return _result!;
    _stop();
    final films = <Uint8List>[];
    final previews = <Uint8List, Uint8List>{};
    for (var i = 0; i < _frames.length; i++) {
      _check();
      final converted = await convertFrame(
        _frames[i],
        FrameOptions(format: _format, fit: FrameFit.contain, dither: _dither),
        isCancelled: () => _cancelled || !mounted,
        onProgress: (p) {
          if (mounted) setState(() => _progress = (i + p) / _frames.length);
        },
      );
      films.add(converted.film);
      previews[_frames[i]] = converted.png;
    }
    _check();
    final result = assembleAnimationFilm(films);
    _devicePreviews.addAll(previews);
    return _result = result;
  }

  Future<void> _output(bool send) async {
    setState(() {
      _busy = true;
      _cancelled = false;
      _progress = 0;
      _status = '正在生成动画…';
    });
    try {
      final bytes = await _generate();
      _check();
      final base = _name.text.trim().replaceAll(
        RegExp(r'[^\w\u4e00-\u9fff.-]'),
        '_',
      );
      final name = base.isEmpty
          ? 'animation.film'
          : base.endsWith('.film')
          ? base
          : '$base.film';
      if (send) {
        if (bytes.length > 2.5 * 1024 * 1024) {
          throw ArgumentError('动画超过 2.5 MiB，请减少帧数或选择黑白快刷');
        }
        setState(() {
          _sending = true;
          _status = '正在发送 $name…';
        });
        await widget.sendFilm(bytes, name);
        _check();
        if (_afterPlay && widget.playOnDevice != null) {
          await widget.playOnDevice!(
            playMode: _controller.playMode,
            intervalMs: (1000 / _fps).round(),
            loopSeconds: _loopSeconds.round(),
          );
        }
      } else {
        await (widget.saveFilm ?? frame_io.downloadFrame)(bytes, name);
      }
      if (mounted) {
        setState(
          () => _status =
              '${send ? "已发送" : "已保存"} · ${_frames.length} 帧 · ${(bytes.length / 1024).toStringAsFixed(1)} KiB',
        );
      }
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
          _sending = false;
        });
      }
    }
  }

  Future<void> _applyPlayback() async {
    final apply = widget.playOnDevice;
    if (_busy || apply == null) return;
    _stop();
    setState(() {
      _busy = true;
      _status = '正在应用设备动画播放参数…';
    });
    try {
      await apply(
        playMode: _controller.playMode,
        intervalMs: (1000 / _fps).round(),
        loopSeconds: _loopSeconds.round(),
      );
      if (mounted) setState(() => _status = '已应用参数并切换到设备动画');
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _previewDeviceColors() async {
    setState(() {
      _busy = true;
      _cancelled = false;
      _progress = 0;
      _status = '正在生成设备色预览…';
    });
    try {
      await _generate();
      if (mounted) setState(() => _status = '已显示设备色预览，可播放检查所有帧');
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  void _move(int delta) {
    final target = _selected + delta;
    if (target < 0 || target >= _frames.length) return;
    setState(() {
      final frame = _frames.removeAt(_selected);
      _frames.insert(target, frame);
      _selected = target;
      _invalidate();
    });
  }

  AnimationFrameTransform get _transform => _controller.transforms.putIfAbsent(
    _frames[_selected],
    () => AnimationFrameTransform(_frames[_selected]),
  );

  Future<void> _applyTransform() async {
    if (_busy || _frames.isEmpty) return;
    final selected = _transform;
    setState(() {
      _busy = true;
      _status = '正在调整画面…';
    });
    try {
      final targets = _controller.transformAll
          ? List<Uint8List>.from(_frames)
          : [_frames[_selected]];
      final replacements = <Uint8List, Uint8List>{};
      final settings = <Uint8List, AnimationFrameTransform>{};
      for (final frame in targets) {
        final original = _controller.transforms[frame];
        final transform = AnimationFrameTransform(original?.source ?? frame)
          ..copyFrom(selected);
        final png = await prepareFrame(
          transform.source,
          transform.options,
          isCancelled: () => !mounted,
        );
        replacements[frame] = png;
        settings[png] = transform;
      }
      if (!mounted) return;
      setState(() {
        for (var i = 0; i < _frames.length; i++) {
          final old = _frames[i];
          if (replacements.containsKey(old)) {
            _frames[i] = replacements[old]!;
            _controller.transforms.remove(old);
          }
        }
        _controller.transforms.addAll(settings);
        _invalidate();
        _status = '画面调整已应用';
      });
    } catch (e) {
      if (mounted) setState(() => _status = '$e · 可重试');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final editable = !_busy && _timer == null;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('动画工坊', style: Theme.of(context).textTheme.headlineSmall),
        const SizedBox(height: 8),
        const Text('多图 / GIF 导入，或逐帧绘制。设备按动画参数播放，画面方向与照片一致。'),
        if (widget.disabledReason != null)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Text(widget.disabledReason!),
          ),
        const SizedBox(height: 12),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton.icon(
              onPressed: editable && _frames.length < 48 ? _import : null,
              icon: const Icon(Icons.add_photo_alternate_outlined),
              label: const Text('导入图片 / GIF'),
            ),
            OutlinedButton.icon(
              onPressed:
                  editable && _frames.length < 48 && widget.captureImage != null
                  ? _capture
                  : null,
              icon: const Icon(Icons.photo_camera_outlined),
              label: const Text('拍照添加'),
            ),
            OutlinedButton.icon(
              onPressed: editable && _frames.length < 48
                  ? () => _beginDrawing()
                  : null,
              icon: const Icon(Icons.draw_outlined),
              label: const Text('绘制新帧'),
            ),
            OutlinedButton.icon(
              onPressed: !_busy && _frames.isNotEmpty ? _play : null,
              icon: Icon(_timer == null ? Icons.play_arrow : Icons.pause),
              label: Text(_timer == null ? '播放预览' : '暂停'),
            ),
            OutlinedButton.icon(
              onPressed: editable && _frames.length >= 2
                  ? _previewDeviceColors
                  : null,
              icon: const Icon(Icons.palette_outlined),
              label: const Text('生成设备色预览'),
            ),
          ],
        ),
        const SizedBox(height: 16),
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 320),
            child: AspectRatio(
              aspectRatio: 2 / 3,
              child: ClipRect(
                child: DecoratedBox(
                  decoration: BoxDecoration(
                    color: Colors.white,
                    border: Border.all(color: Colors.grey),
                  ),
                  child: _drawing
                      ? LayoutBuilder(
                          builder: (context, constraints) => RawGestureDetector(
                            behavior: HitTestBehavior.opaque,
                            gestures: {
                              if (!_busy && _controller.drawingTool != 'pen')
                                TapGestureRecognizer:
                                    GestureRecognizerFactoryWithHandlers<
                                      TapGestureRecognizer
                                    >(
                                      () => TapGestureRecognizer(),
                                      (recognizer) =>
                                          recognizer.onTapDown = (d) =>
                                              _rasterTool(
                                                d.localPosition,
                                                Size(
                                                  constraints.maxWidth,
                                                  constraints.maxHeight,
                                                ),
                                              ),
                                    ),
                              if (!_busy && _controller.drawingTool == 'pen')
                                _CanvasPanGestureRecognizer:
                                    GestureRecognizerFactoryWithHandlers<
                                      _CanvasPanGestureRecognizer
                                    >(() => _CanvasPanGestureRecognizer(), (
                                      recognizer,
                                    ) {
                                      recognizer.onStart = (d) => setState(() {
                                        _strokes.add([
                                          Offset(
                                            d.localPosition.dx *
                                                480 /
                                                constraints.maxWidth,
                                            d.localPosition.dy *
                                                720 /
                                                constraints.maxHeight,
                                          ),
                                        ]);
                                        _strokeColors.add(_color);
                                        _controller.strokeWidths.add(_brush);
                                        _controller.strokeSymmetry.add(
                                          _controller.symmetry,
                                        );
                                      });
                                      recognizer.onUpdate = (d) => setState(
                                        () => _strokes.last.add(
                                          Offset(
                                            d.localPosition.dx *
                                                480 /
                                                constraints.maxWidth,
                                            d.localPosition.dy *
                                                720 /
                                                constraints.maxHeight,
                                          ),
                                        ),
                                      );
                                    }),
                            },
                            child: Stack(
                              fit: StackFit.expand,
                              children: [
                                if (_controller.onionSkin && _frames.isNotEmpty)
                                  Opacity(
                                    opacity: .22,
                                    child: Image.memory(
                                      _frames[_selected > 0
                                          ? _selected - 1
                                          : _frames.length - 1],
                                      fit: BoxFit.contain,
                                    ),
                                  ),
                                if (_controller.editingBase != null)
                                  Image.memory(
                                    _controller.editingBase!,
                                    fit: BoxFit.contain,
                                  ),
                                CustomPaint(
                                  painter: _DrawingPainter(
                                    _strokes,
                                    _strokeColors,
                                    _controller.strokeWidths,
                                    _controller.strokeSymmetry,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        )
                      : _frames.isEmpty
                      ? const Center(
                          child: Text(
                            '导入图片或绘制第一帧',
                            style: TextStyle(color: Colors.black54),
                          ),
                        )
                      : Image.memory(
                          _devicePreviews[_frames[_selected]] ??
                              _frames[_selected],
                          fit: BoxFit.contain,
                          gaplessPlayback: true,
                        ),
                ),
              ),
            ),
          ),
        ),
        if (_drawing) ...[
          DropdownButtonFormField<String>(
            key: ValueKey(_controller.drawingTool),
            initialValue: _controller.drawingTool,
            decoration: const InputDecoration(labelText: '绘图工具'),
            items: const [
              DropdownMenuItem(value: 'pen', child: Text('画笔（白色为橡皮）')),
              DropdownMenuItem(value: 'fill', child: Text('油漆桶')),
              DropdownMenuItem(value: 'pick', child: Text('吸管取色')),
            ],
            onChanged: !_busy
                ? (v) => setState(() => _controller.drawingTool = v!)
                : null,
          ),
          TextButton.icon(
            onPressed: () => setState(() {
              _strokes.clear();
              _strokeColors.clear();
              _controller.strokeWidths.clear();
              _controller.strokeSymmetry.clear();
              _controller.editingBase = null;
            }),
            icon: const Icon(Icons.layers_clear),
            label: const Text('清空本帧'),
          ),
          Wrap(
            spacing: 8,
            children: [
              for (final color in [
                Colors.black,
                Colors.white,
                Colors.red,
                Colors.yellow,
                Colors.blue,
                Colors.green,
              ])
                IconButton(
                  onPressed: () => setState(() => _color = color),
                  tooltip: color == Colors.white ? '橡皮' : '画笔颜色',
                  icon: Icon(
                    _color == color ? Icons.radio_button_checked : Icons.circle,
                    color: color == Colors.white ? Colors.grey : color,
                  ),
                ),
              IconButton(
                onPressed:
                    _busy ||
                        (_strokes.isEmpty && _controller.rasterUndo.isEmpty)
                    ? null
                    : () => setState(() {
                        if (_strokes.isEmpty) {
                          _controller.editingBase = _controller.rasterUndo
                              .removeLast();
                          return;
                        }
                        _strokes.removeLast();
                        _strokeColors.removeLast();
                        _controller.strokeWidths.removeLast();
                        _controller.strokeSymmetry.removeLast();
                      }),
                tooltip: '撤销',
                icon: const Icon(Icons.undo),
              ),
            ],
          ),
          Row(
            children: [
              const Text('画笔'),
              Expanded(
                child: Slider(
                  value: _brush,
                  min: 2,
                  max: 40,
                  onChanged: (v) => setState(() => _brush = v),
                ),
              ),
            ],
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('洋葱皮（上一帧参考）'),
            value: _controller.onionSkin,
            onChanged: (v) => setState(() => _controller.onionSkin = v),
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('左右对称绘制'),
            value: _controller.symmetry,
            onChanged: (v) => setState(() => _controller.symmetry = v),
          ),
          FilledButton(
            onPressed: _addDrawing,
            child: Text(_controller.editingIndex == null ? '添加为一帧' : '保存当前帧'),
          ),
        ],
        if (!_drawing && _frames.isNotEmpty && _timer == null) ...[
          Wrap(
            spacing: 8,
            children: [
              OutlinedButton(
                onPressed: editable
                    ? () {
                        _transform.turns = (_transform.turns + 1) % 4;
                        _applyTransform();
                      }
                    : null,
                child: const Text('旋转 90°'),
              ),
              OutlinedButton(
                onPressed: editable
                    ? () {
                        final t = _transform;
                        t.turns = 0;
                        t.zoom = 1;
                        t.x = 0;
                        t.y = 0;
                        _applyTransform();
                      }
                    : null,
                child: const Text('重置画面'),
              ),
            ],
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('画面调整应用到所有帧'),
            value: _controller.transformAll,
            onChanged: editable
                ? (v) => setState(() => _controller.transformAll = v)
                : null,
          ),
          Text('缩放 ${_transform.zoom.toStringAsFixed(1)}×'),
          Slider(
            value: _transform.zoom,
            min: .2,
            max: 8,
            onChanged: editable
                ? (v) => setState(() => _transform.zoom = v)
                : null,
            onChangeEnd: editable ? (_) => _applyTransform() : null,
          ),
          const Text('水平位置'),
          Slider(
            value: _transform.x,
            min: -480,
            max: 480,
            onChanged: editable
                ? (v) => setState(() => _transform.x = v)
                : null,
            onChangeEnd: editable ? (_) => _applyTransform() : null,
          ),
          const Text('垂直位置'),
          Slider(
            value: _transform.y,
            min: -720,
            max: 720,
            onChanged: editable
                ? (v) => setState(() => _transform.y = v)
                : null,
            onChangeEnd: editable ? (_) => _applyTransform() : null,
          ),
        ],
        const SizedBox(height: 12),
        Text('${_frames.isEmpty ? 0 : _selected + 1} / ${_frames.length} 帧'),
        if (_frames.isNotEmpty)
          SizedBox(
            height: 94,
            child: ListView.separated(
              scrollDirection: Axis.horizontal,
              itemCount: _frames.length,
              separatorBuilder: (_, _) => const SizedBox(width: 6),
              itemBuilder: (context, i) => InkWell(
                onTap: editable
                    ? () => setState(() {
                        _selected = i;
                        _drawing = false;
                      })
                    : null,
                child: Container(
                  width: 58,
                  padding: const EdgeInsets.all(3),
                  decoration: BoxDecoration(
                    border: Border.all(
                      color: i == _selected
                          ? Theme.of(context).colorScheme.primary
                          : Colors.grey,
                      width: i == _selected ? 3 : 1,
                    ),
                  ),
                  child: Image.memory(_frames[i], fit: BoxFit.contain),
                ),
              ),
            ),
          ),
        Wrap(
          spacing: 4,
          children: [
            TextButton(
              onPressed: editable && _frames.isNotEmpty
                  ? () => _beginDrawing(edit: true)
                  : null,
              child: const Text('编辑当前帧'),
            ),
            TextButton(
              onPressed: editable && _selected > 0 ? () => _move(-1) : null,
              child: const Text('前移'),
            ),
            TextButton(
              onPressed: editable && _selected + 1 < _frames.length
                  ? () => _move(1)
                  : null,
              child: const Text('后移'),
            ),
            TextButton(
              onPressed: editable && _frames.isNotEmpty && _frames.length < 48
                  ? () => setState(() {
                      _frames.insert(
                        _selected + 1,
                        Uint8List.fromList(_frames[_selected]),
                      );
                      final previous =
                          _controller.transforms[_frames[_selected]];
                      if (previous != null) {
                        _controller.transforms[_frames[_selected + 1]] =
                            AnimationFrameTransform(previous.source)
                              ..copyFrom(previous);
                      }
                      _selected++;
                      _invalidate();
                    })
                  : null,
              child: const Text('复制'),
            ),
            TextButton(
              onPressed: editable && _frames.isNotEmpty
                  ? () => setState(() {
                      _controller.transforms.remove(
                        _frames.removeAt(_selected),
                      );
                      if (_selected >= _frames.length) {
                        _selected = _frames.isEmpty ? 0 : _frames.length - 1;
                      }
                      _invalidate();
                    })
                  : null,
              child: const Text('删除'),
            ),
          ],
        ),
        DropdownButtonFormField<FrameFormat>(
          initialValue: _format,
          decoration: const InputDecoration(labelText: '输出格式'),
          items: [
            for (final f in FrameFormat.values)
              DropdownMenuItem(value: f, child: Text(_formatName(f))),
          ],
          onChanged: editable
              ? (v) => setState(() {
                  _format = v!;
                  _invalidate();
                })
              : null,
        ),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('抖动'),
          value: _dither,
          onChanged: editable
              ? (v) => setState(() {
                  _dither = v;
                  _invalidate();
                })
              : null,
        ),
        Text('预览 / 设备帧率 ${_fps.toStringAsFixed(1)} FPS'),
        Slider(
          value: _fps,
          min: .5,
          max: 10,
          divisions: 19,
          onChanged: editable ? (v) => setState(() => _fps = v) : null,
        ),
        DropdownButtonFormField<int>(
          initialValue: _controller.playMode,
          decoration: const InputDecoration(labelText: '设备播放模式'),
          items: const [
            DropdownMenuItem(value: 0, child: Text('循环当前动画文件')),
            DropdownMenuItem(value: 1, child: Text('顺序播放动画文件')),
          ],
          onChanged: editable
              ? (v) => setState(() => _controller.playMode = v!)
              : null,
        ),
        Text('轮播间隔：${_loopSeconds == 0 ? "不等待" : "${_loopSeconds.round()} 秒"}'),
        Slider(
          value: _loopSeconds,
          min: 0,
          max: 600,
          divisions: 60,
          onChanged: editable ? (v) => setState(() => _loopSeconds = v) : null,
        ),
        TextField(
          controller: _name,
          enabled: editable,
          decoration: const InputDecoration(labelText: '文件名'),
        ),
        if (widget.playOnDevice != null)
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('上传后在设备播放'),
            value: _afterPlay,
            onChanged: editable ? (v) => setState(() => _afterPlay = v) : null,
          ),
        if (widget.playOnDevice != null)
          OutlinedButton.icon(
            onPressed:
                editable && widget.connected && widget.disabledReason == null
                ? _applyPlayback
                : null,
            icon: const Icon(Icons.play_circle_outline),
            label: const Text('应用参数并播放设备动画'),
          ),
        const SizedBox(height: 12),
        if (_busy) ...[
          LinearProgressIndicator(value: _sending ? null : _progress),
          TextButton(
            onPressed: () async {
              _cancelled = true;
              if (_sending) await widget.cancelSend?.call();
            },
            child: const Text('取消'),
          ),
        ],
        Wrap(
          spacing: 8,
          children: [
            OutlinedButton(
              onPressed:
                  !_busy &&
                      _frames.length >= 2 &&
                      (widget.saveFilm != null || frame_io.canDownloadFrame)
                  ? () => _output(false)
                  : null,
              child: const Text('保存 Film'),
            ),
            FilledButton(
              onPressed:
                  !_busy &&
                      _frames.length >= 2 &&
                      widget.connected &&
                      widget.disabledReason == null
                  ? () => _output(true)
                  : null,
              child: const Text('发送到设备'),
            ),
          ],
        ),
        if (_frames.length < 2) const Text('至少添加 2 帧才能生成动画。'),
        if (_status != null)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 12),
            child: Text(_status!),
          ),
      ],
    );
  }
}

String _formatName(FrameFormat format) => switch (format.name) {
  'monoFast' => '黑白快刷 MonoFast',
  'colorFast55' => '55 色 ColorFast',
  'colorQual' => '46 色 ColorQual',
  _ => '六色',
};

void _paintStrokes(
  Canvas canvas,
  List<List<Offset>> strokes,
  List<Color> colors,
  List<double> widths,
  List<bool> symmetry,
) {
  for (var i = 0; i < strokes.length; i++) {
    final points = strokes[i];
    if (points.isEmpty) continue;
    final width = widths[i];
    final paint = Paint()
      ..color = colors[i]
      ..strokeWidth = width
      ..strokeCap = StrokeCap.round
      ..style = PaintingStyle.stroke;
    if (points.length == 1) {
      canvas.drawCircle(points.first, width / 2, Paint()..color = colors[i]);
      if (symmetry[i]) {
        canvas.drawCircle(
          Offset(480 - points.first.dx, points.first.dy),
          width / 2,
          Paint()..color = colors[i],
        );
      }
      continue;
    }
    final path = Path()..moveTo(points.first.dx, points.first.dy);
    for (final p in points.skip(1)) {
      path.lineTo(p.dx, p.dy);
    }
    canvas.drawPath(path, paint);
    if (symmetry[i]) {
      canvas.save();
      canvas.translate(480, 0);
      canvas.scale(-1, 1);
      canvas.drawPath(path, paint);
      canvas.restore();
    }
  }
}

class _DrawingPainter extends CustomPainter {
  _DrawingPainter(this.strokes, this.colors, this.widths, this.symmetry);
  final List<List<Offset>> strokes;
  final List<Color> colors;
  final List<double> widths;
  final List<bool> symmetry;
  @override
  void paint(Canvas canvas, Size size) {
    canvas.scale(size.width / 480, size.height / 720);
    _paintStrokes(canvas, strokes, colors, widths, symmetry);
  }

  @override
  bool shouldRepaint(_DrawingPainter oldDelegate) => true;
}
