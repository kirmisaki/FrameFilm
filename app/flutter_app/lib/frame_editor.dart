import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

import 'frame_codec.dart';
import 'live_image_composition.dart';
import 'workbench_widgets.dart';
import 'frame_quick_page.dart';
import 'frame_io.dart' as io;
import 'passport_storage.dart' as storage;

class _EditorScaleRecognizer extends ScaleGestureRecognizer {
  @override
  void addAllowedPointer(PointerDownEvent event) {
    super.addAllowedPointer(event);
    resolve(GestureDisposition.accepted);
  }
}

class _EditorGestures extends StatelessWidget {
  const _EditorGestures({
    super.key,
    required this.child,
    this.onScaleStart,
    this.onScaleUpdate,
  });
  final Widget child;
  final GestureScaleStartCallback? onScaleStart;
  final GestureScaleUpdateCallback? onScaleUpdate;
  @override
  Widget build(BuildContext context) => RawGestureDetector(
    behavior: HitTestBehavior.opaque,
    gestures: onScaleStart == null
        ? const {}
        : {
            _EditorScaleRecognizer:
                GestureRecognizerFactoryWithHandlers<_EditorScaleRecognizer>(
                  () => _EditorScaleRecognizer(),
                  (r) {
                    r.onStart = onScaleStart;
                    r.onUpdate = onScaleUpdate;
                  },
                ),
          },
    child: child,
  );
}

class FrameEditorController {
  Uint8List? source, crop;
  FrameOptions options = const FrameOptions(fit: FrameFit.contain);
  FrameResult? result;
  final fileName = TextEditingController(text: 'output.film');
  int revision = 0, resultRevision = -1;
  bool get hasCurrentResult => result != null && resultRevision == revision;
  void clear() {
    source = null;
    crop = null;
    result = null;
    options = const FrameOptions(fit: FrameFit.contain);
    fileName.text = 'output.film';
    revision++;
    resultRevision = -1;
  }

  void dispose() {
    clear();
    fileName.dispose();
  }
}

const frameAlgorithmLabels = {
  FrameAlgorithm.atkinsonEnhanced: 'Atkinson 增强',
  FrameAlgorithm.floydSteinberg: 'Floyd-Steinberg',
  FrameAlgorithm.atkinson: 'Atkinson',
  FrameAlgorithm.stucki: 'Stucki',
  FrameAlgorithm.jarvis: 'Jarvis-Judice-Ninke',
  FrameAlgorithm.gammaFloydSteinberg: 'Gamma 感知 FS（线性）',
  FrameAlgorithm.bayer: 'Bayer 4×4 有序抖动',
  FrameAlgorithm.adaptive: '自适应',
  FrameAlgorithm.colorFast55: 'ColorFast · 55 色',
  FrameAlgorithm.colorQual: 'ColorQual · 46 色',
  FrameAlgorithm.szEnhanced: 'SZ 增强',
  FrameAlgorithm.atkinsonSzCalib: 'Atkinson SZ 校色',
  FrameAlgorithm.colorQualMeasured: '实测校色 · 原值',
  FrameAlgorithm.colorQualCalibrated: '实测校色 · 校正 2.5%',
};

class FrameEditor extends StatefulWidget {
  const FrameEditor({
    super.key,
    required this.controller,
    required this.canImportFilm,
    required this.onImportFilm,
    this.pickImage,
    this.pickPhoto,
    this.canSend = false,
    this.onSendFilm,
    this.onCancelSend,
  });
  final FrameEditorController controller;
  final bool canImportFilm, canSend;
  final Future<void> Function(Uint8List, String) onImportFilm;
  final Future<void> Function(Uint8List, String)? onSendFilm;
  final Future<void> Function()? onCancelSend;
  final Future<Uint8List?> Function()? pickImage;
  final Future<FramePhoto?> Function()? pickPhoto;
  @override
  State<FrameEditor> createState() => _FrameEditorState();
}

class _FrameEditorState extends State<FrameEditor> {
  FrameEditorController get c => widget.controller;
  int _generation = 0;
  bool _working = false, _sending = false, _exporting = false;
  double _progress = 0, _gestureZoom = 1;
  Offset _gesturePan = Offset.zero, _gestureStart = Offset.zero;
  String _status = '';
  Timer? _previewTimer;
  bool get locked => _sending || _exporting;
  bool _converting = false, _previewRequested = false;
  @override
  void initState() {
    super.initState();
    if (c.source != null && !c.hasCurrentResult) unawaited(_generate());
  }

  @override
  void dispose() {
    _generation++;
    _previewRequested = false;
    _previewTimer?.cancel();
    // A device transfer belongs to the application, and can be cancelled in Film's status card.
    super.dispose();
  }

  void _cancel() {
    _generation++;
    _previewRequested = false;
    _previewTimer?.cancel();
    setState(() {
      _working = false;
      _status = '已取消，可重新生成';
    });
  }

  Future<void> _pick() async {
    _previewTimer?.cancel();
    _previewRequested = false;
    final generation = ++_generation;
    setState(() {
      _working = true;
      _status = '正在加载照片';
    });
    try {
      Uint8List? bytes;
      String name = 'output';
      if (widget.pickPhoto != null) {
        final photo = await widget.pickPhoto!();
        bytes = photo?.bytes;
        if (photo != null) {
          name = photo.name.replaceFirst(RegExp(r'\.[^.]+$'), '');
        }
      } else {
        bytes = await (widget.pickImage ?? storage.pickPassportAvatar)();
      }
      if (!mounted || generation != _generation) return;
      if (bytes == null) {
        setState(() {
          _working = false;
          _status = '已取消选择';
        });
        return;
      }
      final decoder = await ui.instantiateImageCodec(bytes);
      late bool landscape;
      ui.Image? image;
      try {
        image = (await decoder.getNextFrame()).image;
        landscape = image.width > image.height;
      } finally {
        image?.dispose();
        decoder.dispose();
      }
      if (!mounted || generation != _generation) return;
      final previous = c.options;
      final options = FrameOptions(
        fit: FrameFit.contain,
        quarterTurns: landscape ? 3 : 0,
        format: previous.format,
        contrast: previous.contrast,
        saturation: previous.saturation,
        algorithm: previous.algorithm,
        dither: previous.dither,
        ditherStrength: previous.ditherStrength,
      );
      final crop = await prepareFrame(
        bytes,
        options,
        isCancelled: () => !mounted || generation != _generation,
      );
      if (!mounted || generation != _generation) return;
      setState(() {
        c.source = bytes;
        c.options = options;
        c.crop = crop;
        c.revision++;
        c.fileName.text = '${name.isEmpty ? 'output' : name}.film';
        _working = false;
      });
      await _generate();
    } catch (e) {
      if (mounted && generation == _generation) {
        setState(() {
          _working = false;
          _status = '加载失败：$e';
        });
      }
    }
  }

  void _change({
    FrameAlgorithm? algorithm,
    double? strength,
    FrameFormat? format,
    FrameFit? fit,
    int? turns,
    double? zoom,
    double? x,
    double? y,
    double? brightness,
    double? contrast,
    double? saturation,
    bool? dither,
  }) {
    final o = c.options;
    setState(() {
      c.options = FrameOptions(
        format: algorithm != null ? FrameFormat.sixColor : (format ?? o.format),
        fit: fit ?? o.fit,
        quarterTurns: turns ?? o.quarterTurns,
        zoom: zoom ?? o.zoom,
        panX: x ?? o.panX,
        panY: y ?? o.panY,
        brightness: brightness ?? o.brightness,
        contrast: contrast ?? o.contrast,
        saturation: saturation ?? o.saturation,
        dither: dither ?? o.dither,
        algorithm: format != null
            ? FrameAlgorithm.atkinsonEnhanced
            : (algorithm ?? o.algorithm),
        ditherStrength: strength ?? o.ditherStrength,
      );
      c.revision++;

      _status = c.source == null ? '参数已更新，请上传图像以生成预览' : '参数已更新，正在更新预览';
    });
    _previewRequested = c.source != null;
    if (c.source != null &&
        !_converting &&
        !(_previewTimer?.isActive ?? false)) {
      _previewTimer = Timer(const Duration(milliseconds: 80), _generate);
    }
  }

  Future<void> _generate() async {
    if (!mounted || c.source == null) return;
    _previewRequested = true;
    if (_converting) return;
    final source = c.source!;
    _previewRequested = false;
    _converting = true;
    _previewTimer?.cancel();
    final generation = _generation, revision = c.revision;
    final options = c.options;
    setState(() {
      _working = true;
      _progress = 0;
      _status = '正在更新预览';
    });
    bool cancelled() => !mounted || generation != _generation;
    try {
      final crop = await prepareFrame(source, options, isCancelled: cancelled);
      final result = await convertFrame(
        source,
        options,
        isCancelled: cancelled,
        onProgress: (v) {
          if (!cancelled()) setState(() => _progress = v);
        },
      );
      if (cancelled() || revision != c.revision) return;
      setState(() {
        c.crop = crop;
        c.result = result;
        c.resultRevision = revision;
        _working = false;
        _status = '预览已更新，可以下载或发送';
      });
    } catch (e) {
      if (!cancelled()) {
        setState(() {
          _working = false;
          _status = '生成失败：$e';
        });
      }
    } finally {
      _converting = false;
      if (mounted &&
          c.source != null &&
          (_previewRequested ||
              (generation == _generation && revision != c.revision))) {
        _previewTimer = Timer(const Duration(milliseconds: 80), _generate);
      }
    }
  }

  Future<void> _export(String action) async {
    if (locked || !c.hasCurrentResult) return;
    var name = c.fileName.text.trim();
    if (name.isEmpty) name = 'output.film';
    if (!name.toLowerCase().endsWith('.film')) name += '.film';
    c.fileName.text = name;
    setState(() {
      _sending = action == 'send';
      _exporting = !_sending;
    });
    try {
      if (action == 'download') {
        await io.downloadFrame(c.result!.film, name);
      } else if (action == 'send') {
        await widget.onSendFilm!(c.result!.film, name);
      } else {
        await widget.onImportFilm(c.result!.film, name);
      }
      if (mounted) {
        setState(
          () => _status = action == 'send'
              ? '发送完成'
              : action == 'download'
              ? '已下载'
              : '已导入直传区',
        );
      }
    } catch (e) {
      if (mounted) setState(() => _status = '操作未完成：$e');
    } finally {
      if (mounted) {
        setState(() {
          _sending = false;
          _exporting = false;
        });
      }
    }
  }

  Widget _section(String title, List<Widget> children) => WorkbenchPanel(
    padding: EdgeInsets.zero,
    child: Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text(title, style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 16),
          ...children,
        ],
      ),
    ),
  );
  Widget _slider(
    String title,
    double value,
    double min,
    double max,
    ValueChanged<double> callback,
  ) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text('$title  ${value.toStringAsFixed(1)}'),
      Slider(
        value: value.clamp(min, max),
        min: min,
        max: max,
        onChanged: _sending || _exporting ? null : callback,
      ),
    ],
  );

  @override
  Widget build(BuildContext context) {
    final o = c.options;
    final devicePreview =
        o.dither ||
        o.format != FrameFormat.sixColor ||
        o.algorithm == FrameAlgorithm.colorFast55 ||
        o.algorithm == FrameAlgorithm.colorQual ||
        o.algorithm == FrameAlgorithm.colorQualMeasured ||
        o.algorithm == FrameAlgorithm.colorQualCalibrated;
    final png = devicePreview ? c.result?.png : c.crop;
    final sections = <Widget>[
      _section('上传与算法', [
        Wrap(
          spacing: 12,
          runSpacing: 8,
          children: [
            FilledButton.icon(
              onPressed: locked || _working ? null : _pick,
              icon: const Icon(Icons.add_photo_alternate_outlined),
              label: const Text('上传图像'),
            ),
            const Text('支持常见图片格式，选择后自动更新预览'),
          ],
        ),
        const SizedBox(height: 20),
        DropdownButtonFormField<FrameAlgorithm>(
          key: ValueKey(o.algorithm),
          initialValue: o.algorithm,
          isExpanded: true,
          decoration: const InputDecoration(labelText: '抖动算法'),
          items: frameAlgorithmLabels.entries
              .map((e) => DropdownMenuItem(value: e.key, child: Text(e.value)))
              .toList(),
          onChanged: _sending || _exporting
              ? null
              : (v) => _change(algorithm: v),
        ),
        const SizedBox(height: 12),
        if ([
          FrameAlgorithm.floydSteinberg,
          FrameAlgorithm.atkinson,
          FrameAlgorithm.stucki,
          FrameAlgorithm.jarvis,
          FrameAlgorithm.gammaFloydSteinberg,
          FrameAlgorithm.bayer,
          FrameAlgorithm.colorQualMeasured,
          FrameAlgorithm.colorQualCalibrated,
        ].contains(o.algorithm))
          _slider('抖动强度', o.ditherStrength, 0, 5, (v) => _change(strength: v)),
        _slider('对比度', o.contrast, .5, 2, (v) => _change(contrast: v)),
        _slider('饱和度', o.saturation, 0, 3, (v) => _change(saturation: v)),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton(
              onPressed: _sending || _exporting
                  ? null
                  : () => _change(dither: !o.dither),
              child: Text(o.dither ? '禁用抖动' : '启用抖动'),
            ),
            OutlinedButton(
              onPressed: locked || c.source == null
                  ? null
                  : () => _change(
                      turns: (o.quarterTurns + 1) % 4,
                      zoom: 1,
                      x: 0,
                      y: 0,
                    ),
              child: const Text('旋转90度'),
            ),
            TextButton(
              onPressed: locked
                  ? null
                  : () {
                      _generation++;
                      _previewRequested = false;
                      _previewTimer?.cancel();
                      setState(() {
                        c.clear();
                        _status = '已重置';
                      });
                    },
              child: const Text('重置'),
            ),
            TextButton(
              onPressed: locked || c.source == null
                  ? null
                  : () => _change(zoom: 1, x: 0, y: 0),
              child: const Text('重置缩放'),
            ),
          ],
        ),
        const Text('可拖动、双指或滚轮缩放；构图实时更新，设备色彩随后更新。'),
        ExpansionTile(
          title: const Text('扩展选项'),
          tilePadding: EdgeInsets.zero,
          children: [
            SegmentedButton<FrameFormat>(
              segments: const [
                ButtonSegment(value: FrameFormat.sixColor, label: Text('六色')),
                ButtonSegment(
                  value: FrameFormat.monoFast,
                  label: Text('黑白 MonoFast'),
                ),
              ],
              selected: {o.format},
              onSelectionChanged: locked
                  ? null
                  : (v) => _change(format: v.first),
            ),
            const SizedBox(height: 8),
            SegmentedButton<FrameFit>(
              segments: const [
                ButtonSegment(value: FrameFit.cover, label: Text('铺满')),
                ButtonSegment(value: FrameFit.contain, label: Text('完整显示')),
              ],
              selected: {o.fit},
              onSelectionChanged: locked ? null : (v) => _change(fit: v.first),
            ),
            _slider('亮度', o.brightness, -1, 1, (v) => _change(brightness: v)),
          ],
        ),
        if (o.algorithm == FrameAlgorithm.szEnhanced)
          const Text('SZ 增强使用原图色彩进行校正，启用抖动时不叠加亮度、对比度及饱和度。'),
        if (o.algorithm == FrameAlgorithm.colorFast55 ||
            o.algorithm == FrameAlgorithm.colorQual ||
            o.algorithm == FrameAlgorithm.colorQualMeasured ||
            o.algorithm == FrameAlgorithm.colorQualCalibrated)
          const Text('46 / 55 色使用 Ark 的 Film 2.0 索引色格式；关闭抖动仍保留对应色板。'),
        if (o.algorithm == FrameAlgorithm.colorQualMeasured ||
            o.algorithm == FrameAlgorithm.colorQualCalibrated)
          const Text('实测校色使用 ColorQual 三相刷新；关闭抖动仍保留实测色板。'),
      ]),
      const SizedBox(height: 16),
      _section('预览', [
        Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 480),
            child: LayoutBuilder(
              builder: (context, constraints) => Listener(
                key: const ValueKey('film-preview-events'),
                onPointerSignal: (event) {
                  if (event is PointerScrollEvent &&
                      !_sending &&
                      !_exporting &&
                      c.source != null) {
                    GestureBinding.instance.pointerSignalResolver.register(
                      event,
                      (signal) {
                        if (!mounted || locked || c.source == null) return;
                        final current = c.options;
                        final zoom =
                            (current.zoom *
                                    (event.scrollDelta.dy > 0 ? .9 : 1.1))
                                .clamp(.05, 10)
                                .toDouble();
                        final focal =
                            event.localPosition * (480 / constraints.maxWidth);
                        final center =
                            const Offset(240, 360) +
                            Offset(current.panX, current.panY);
                        final nextCenter =
                            focal - (focal - center) * (zoom / current.zoom);
                        _change(
                          zoom: zoom,
                          x: nextCenter.dx - 240,
                          y: nextCenter.dy - 360,
                        );
                      },
                    );
                  }
                },
                child: _EditorGestures(
                  key: const ValueKey('film-gestures'),
                  onScaleStart: _sending || _exporting || c.source == null
                      ? null
                      : (d) {
                          _gestureZoom = o.zoom;
                          _gesturePan = Offset(o.panX, o.panY);
                          _gestureStart = d.localFocalPoint;
                        },
                  onScaleUpdate: _sending || _exporting || c.source == null
                      ? null
                      : (d) {
                          final screenToOutput = 480 / constraints.maxWidth;
                          final start = _gestureStart * screenToOutput;
                          final focal = d.localFocalPoint * screenToOutput;
                          final zoom = (_gestureZoom * d.scale)
                              .clamp(.05, 10)
                              .toDouble();
                          final originalCenter =
                              const Offset(240, 360) + _gesturePan;
                          final nextCenter =
                              focal -
                              (start - originalCenter) * (zoom / _gestureZoom);
                          _change(
                            zoom: zoom,
                            x: nextCenter.dx - 240,
                            y: nextCenter.dy - 360,
                          );
                        },
                  child: AspectRatio(
                    aspectRatio: 2 / 3,
                    child: ColoredBox(
                      color: Colors.white,
                      child: c.source == null
                          ? const Center(
                              child: Icon(
                                Icons.image_outlined,
                                color: Colors.black38,
                              ),
                            )
                          : Stack(
                              fit: StackFit.expand,
                              children: [
                                // Keep the decoded source alive between completed conversions.
                                LiveImageComposition(
                                  key: const ValueKey('film-live-composition'),
                                  source: c.source!,
                                  options: o,
                                ),
                                if (c.hasCurrentResult && png != null)
                                  Image.memory(
                                    png,
                                    fit: BoxFit.contain,
                                    gaplessPlayback: true,
                                    errorBuilder: (_, e, s) =>
                                        const Center(child: Text('预览不可用')),
                                  ),
                              ],
                            ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
        const SizedBox(height: 8),
        Text(
          !c.hasCurrentResult && c.source != null
              ? '实时构图预览 · 正在转换设备色彩'
              : devicePreview
              ? '设备色彩预览'
              : '原色构图预览 · 下载和发送会转换为设备色彩',
        ),
      ]),
      const SizedBox(height: 16),
      _section('输出', [
        TextField(
          controller: c.fileName,
          enabled: !locked,
          decoration: const InputDecoration(labelText: '文件名'),
        ),
        const SizedBox(height: 12),
        if (_working)
          LinearProgressIndicator(value: _progress > 0 ? _progress : null),
        if (_status.isNotEmpty)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Text(_status),
          ),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            if (io.canDownloadFrame)
              OutlinedButton(
                onPressed: !locked && c.hasCurrentResult
                    ? () => _export('download')
                    : null,
                child: const Text('下载'),
              ),
            FilledButton(
              onPressed:
                  !locked &&
                      c.hasCurrentResult &&
                      widget.canSend &&
                      widget.onSendFilm != null
                  ? () => _export('send')
                  : null,
              child: const Text('发送到设备'),
            ),
            OutlinedButton(
              onPressed: !locked && c.hasCurrentResult && widget.canImportFilm
                  ? () => _export('import')
                  : null,
              child: const Text('导入直传区'),
            ),
            if (_working)
              TextButton(onPressed: _cancel, child: const Text('取消生成')),
            if (_sending && widget.onCancelSend != null)
              TextButton(
                onPressed: widget.onCancelSend,
                child: const Text('取消发送'),
              ),
            if (!_working && c.source != null && !c.hasCurrentResult)
              TextButton(onPressed: _generate, child: const Text('重新生成')),
          ],
        ),
      ]),
    ];
    return LayoutBuilder(
      builder: (context, constraints) {
        // Original ForFilm layout: parameters and output on the left, preview on the right.
        if (constraints.maxWidth >= 860) {
          return Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    sections[0],
                    const SizedBox(height: 20),
                    sections[4],
                  ],
                ),
              ),
              const SizedBox(width: 24),
              SizedBox(width: 360, child: sections[2]),
            ],
          );
        }
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: sections,
        );
      },
    );
  }
}
