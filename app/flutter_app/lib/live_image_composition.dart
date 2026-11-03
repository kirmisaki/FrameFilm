import 'image_resampling.dart';
import 'source_image_fit.dart';

import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import 'frame_codec.dart';

/// Uses the fast transform painter with fitting off; enabled source fitting
/// shares export composition and replaces the raster only for the latest options.
class LiveImageComposition extends StatefulWidget {
  const LiveImageComposition({
    super.key,
    required this.source,
    required this.options,
    this.width = 480,
    this.height = 720,
    this.sourceProfile,
    this.legacyQuality = ui.FilterQuality.medium,
    this.legacyDecodeLimit = 2048,
  });
  final Uint8List source;
  final FrameOptions options;
  final double width, height;
  final SourceFitProfile? sourceProfile;
  final ui.FilterQuality legacyQuality;
  final int legacyDecodeLimit;
  @override
  State<LiveImageComposition> createState() => _LiveImageCompositionState();
}

class _LiveImageCompositionState extends State<LiveImageComposition> {
  ui.Image? image;
  int revision = 0, fitRevision = 0;
  ui.Image? fitted;
  bool fitPending = false;
  bool get useFit =>
      widget.options.algorithm != FrameAlgorithm.abc512I006 &&
      widget.options.format != FrameFormat.monoFast &&
      (widget.options.warmChroma != 0 ||
          (!sourceFitIsSz(widget.options) && widget.options.rangeFit != 0));
  @override
  void initState() {
    super.initState();
    load();
    refreshFit();
  }

  @override
  void didUpdateWidget(LiveImageComposition old) {
    super.didUpdateWidget(old);
    final sourceChanged = !identical(old.source, widget.source);
    if (sourceChanged) {
      final previous = fitted;
      fitted = null;
      previous?.dispose();
      load();
    }
    if (old.options != widget.options ||
        sourceChanged ||
        old.sourceProfile != widget.sourceProfile) {
      refreshFit();
    }
  }

  Future<void> refreshFit() async {
    final r = ++fitRevision;
    fitPending = useFit;
    if (!useFit) {
      final previous = fitted;
      fitted = null;
      previous?.dispose();
      return;
    }
    final o = widget.options;
    bool cancelled() => !mounted || r != fitRevision;
    ui.Image? composition, next;
    try {
      composition = await renderImageComposition(
        widget.source,
        width: widget.width.toInt(),
        height: widget.height.toInt(),
        cover: o.fit == FrameFit.cover,
        quarterTurns: o.quarterTurns,
        zoom: o.zoom,
        panX: o.panX,
        panY: o.panY,
        resampling: o.resampling,
        legacyQuality: widget.legacyQuality,
        legacyDecodeLimit: widget.legacyDecodeLimit,
        checkCancelled: () {
          if (cancelled()) throw const FrameCancelledException();
        },
      );
      final rgba = (await composition.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      ))!.buffer.asUint8List();
      adjustFrameCompositionRgba(rgba, o, profile: widget.sourceProfile);
      next = await imageFromRgba(
        rgba,
        widget.width.toInt(),
        widget.height.toInt(),
      );
      if (cancelled()) return;
      final previous = fitted;
      setState(() {
        fitPending = false;
        fitted = next;
        next = null;
        failed = false;
      });
      previous?.dispose();
    } on FrameCancelledException {
      // A newer composition owns the preview.
    } catch (_) {
      if (!cancelled()) {
        final previous = fitted;
        setState(() {
          fitPending = false;
          fitted = null;
          failed = true;
        });
        previous?.dispose();
      }
    } finally {
      composition?.dispose();
      next?.dispose();
    }
  }

  bool failed = false;
  Future<void> load() async {
    final r = ++revision;
    ui.Codec? codec;
    ui.Image? next;
    try {
      if (widget.source.isEmpty) throw const FormatException('图片为空');
      codec = await ui.instantiateImageCodec(
        widget.source,
        targetWidth: 1024,
        allowUpscaling: false,
      );
      next = (await codec.getNextFrame()).image;
      if (!mounted || r != revision) return;
      final previous = image;
      setState(() {
        image = next;
        next = null;
        failed = false;
      });
      previous?.dispose();
    } catch (_) {
      if (mounted && r == revision) setState(() => failed = true);
    } finally {
      next?.dispose();
      codec?.dispose();
    }
  }

  @override
  void dispose() {
    revision++;
    fitRevision++;
    fitted?.dispose();
    image?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Stack(
    fit: StackFit.expand,
    children: [
      if (useFit)
        if (fitted != null)
          RawImage(image: fitted, fit: BoxFit.fill)
        else
          const Center(child: Text('正在更新图片调整'))
      else
        CustomPaint(
          painter: _CompositionPainter(
            image,
            widget.options,
            widget.width,
            widget.height,
          ),
        ),
      if (useFit && fitPending && fitted != null)
        const Align(
          alignment: Alignment.bottomCenter,
          child: LinearProgressIndicator(minHeight: 2),
        ),
      if (image == null || failed)
        Center(child: Text(failed ? '图片预览不可用' : '正在加载图片')),
    ],
  );
}

class _CompositionPainter extends CustomPainter {
  _CompositionPainter(this.image, this.o, this.width, this.height);
  final ui.Image? image;
  final FrameOptions o;
  final double width, height;
  @override
  void paint(Canvas canvas, Size size) {
    canvas.clipRect(Offset.zero & size);
    canvas.drawColor(Colors.white, BlendMode.src);
    final source = image;
    if (source == null) return;
    canvas.scale(size.width / width, size.height / height);
    final odd = o.quarterTurns.isOdd;
    final sw = odd ? source.height : source.width;
    final sh = odd ? source.width : source.height;
    final scale =
        (o.fit == FrameFit.cover
            ? math.max(width / sw, height / sh)
            : math.min(width / sw, height / sh)) *
        o.zoom;
    canvas.translate(width / 2 + o.panX, height / 2 + o.panY);
    canvas.rotate(o.quarterTurns * math.pi / 2);
    canvas.scale(scale);
    final sat = o.saturation;
    canvas.saveLayer(
      null,
      Paint()
        ..colorFilter = ColorFilter.matrix([
          .299 * (1 - sat) + sat,
          .587 * (1 - sat),
          .114 * (1 - sat),
          0,
          0,
          .299 * (1 - sat),
          .587 * (1 - sat) + sat,
          .114 * (1 - sat),
          0,
          0,
          .299 * (1 - sat),
          .587 * (1 - sat),
          .114 * (1 - sat) + sat,
          0,
          0,
          0,
          0,
          0,
          1,
          0,
        ]),
    );
    final shift = 128 * (1 - o.contrast);
    canvas.saveLayer(
      null,
      Paint()
        ..colorFilter = ColorFilter.matrix([
          o.contrast,
          0,
          0,
          0,
          shift,
          0,
          o.contrast,
          0,
          0,
          shift,
          0,
          0,
          o.contrast,
          0,
          shift,
          0,
          0,
          0,
          1,
          0,
        ]),
    );
    final bright = o.brightness * 255;
    canvas.saveLayer(
      null,
      Paint()
        ..colorFilter = ColorFilter.matrix([
          1,
          0,
          0,
          0,
          bright,
          0,
          1,
          0,
          0,
          bright,
          0,
          0,
          1,
          0,
          bright,
          0,
          0,
          0,
          1,
          0,
        ]),
    );
    canvas.drawImage(
      source,
      Offset(-source.width / 2, -source.height / 2),
      Paint()..filterQuality = FilterQuality.medium,
    );
    canvas.restore();
    canvas.restore();
    canvas.restore();
  }

  @override
  bool shouldRepaint(_CompositionPainter old) =>
      old.image != image || old.o != o;
}
