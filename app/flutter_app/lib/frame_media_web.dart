import 'dart:async';
import 'dart:convert';
import 'dart:js_interop';

import 'package:flutter/material.dart';

import 'frame_quick_page.dart';

@JS('document.createElement')
external JSObject _create(String tag);
@JS('document.body')
external _Body get _body;

extension type _Body(JSObject _) implements JSObject {
  external void appendChild(JSObject node);
}

extension type _Style(JSObject _) implements JSObject {
  external set display(String value);
  external set width(String value);
  external set height(String value);
  external set objectFit(String value);
}

extension type _Input(JSObject _) implements JSObject {
  external set type(String value);
  external set accept(String value);
  external set multiple(bool value);
  external set onchange(JSFunction value);
  external set oncancel(JSFunction value);
  external _Files? get files;
  external _Style get style;
  external void click();
  external void remove();
}

extension type _Files(JSObject _) implements JSObject {
  external int get length;
  external _File? item(int index);
}

extension type _File(JSObject _) implements JSObject {
  external int get size;
  external String get name;
  external JSPromise<JSArrayBuffer> arrayBuffer();
}

Future<List<FramePhoto>> pickFramePhotos({required bool multiple}) async {
  final completer = Completer<List<FramePhoto>>();
  final input = _Input(_create('input'));
  input.type = 'file';
  input.accept = 'image/*';
  input.multiple = multiple;
  input.style.display = 'none';
  input.oncancel = (() {
    if (!completer.isCompleted) completer.complete([]);
  }).toJS;
  input.onchange = (() {
    Future<void>(() async {
      try {
        final files = input.files;
        final count = files?.length ?? 0;
        if (count > 20) throw const FormatException('每次最多选择 20 张照片');
        var total = 0;
        for (var i = 0; i < count; i++) {
          final file = files!.item(i)!;
          if (file.size > 8 * 1024 * 1024) {
            throw FormatException('${file.name} 超过 8 MiB');
          }
          total += file.size;
        }
        if (total > 64 * 1024 * 1024) {
          throw const FormatException('本次所选照片合计超过 64 MiB，请分次选择');
        }
        final photos = <FramePhoto>[];
        for (var i = 0; i < count; i++) {
          final file = files!.item(i)!;
          final buffer = await file.arrayBuffer().toDart;
          photos.add(
            FramePhoto(name: file.name, bytes: buffer.toDart.asUint8List()),
          );
        }
        if (!completer.isCompleted) completer.complete(photos);
      } catch (error, stack) {
        if (!completer.isCompleted) completer.completeError(error, stack);
      }
    });
  }).toJS;
  _body.appendChild(input);
  try {
    input.click();
    return await completer.future;
  } finally {
    input.remove();
  }
}

@JS('navigator.mediaDevices')
external _Devices? get _devices;

extension type _Devices(JSObject _) implements JSObject {
  external JSPromise<_Stream> getUserMedia(JSObject constraints);
}

extension type _Stream(JSObject _) implements JSObject {
  external JSArray<_Track> getTracks();
}

extension type _Track(JSObject _) implements JSObject {
  external void stop();
}

extension type _Video(JSObject _) implements JSObject {
  external set srcObject(JSObject? value);
  external set muted(bool value);
  external set autoplay(bool value);
  external set playsInline(bool value);
  external _Style get style;
  external int get videoWidth;
  external int get videoHeight;
  external JSPromise<JSAny?> play();
}

extension type _Canvas(JSObject _) implements JSObject {
  external set width(int value);
  external set height(int value);
  external _Context getContext(String kind);
  external String toDataURL(String type, double quality);
}

extension type _Context(JSObject _) implements JSObject {
  external void drawImage(JSObject image, int x, int y);
}

Future<FramePhoto?> captureFramePhoto(BuildContext context) =>
    showDialog<FramePhoto>(context: context, builder: (_) => const _Camera());

class _Camera extends StatefulWidget {
  const _Camera();
  @override
  State<_Camera> createState() => _CameraState();
}

class _CameraState extends State<_Camera> {
  _Video? video;
  _Stream? stream;
  String? error;
  bool ready = false;
  void stop(_Stream value) {
    for (final track in value.getTracks().toDart) {
      track.stop();
    }
  }

  Future<void> start() async {
    setState(() {
      error = null;
      ready = false;
    });
    try {
      final devices = _devices;
      if (devices == null) throw StateError('当前浏览器不支持相机，请使用 HTTPS 或 localhost');
      final acquired = await devices
          .getUserMedia(
            {
                  'video': {
                    'facingMode': 'environment',
                    'width': {'ideal': 1280},
                    'height': {'ideal': 960},
                  },
                  'audio': false,
                }.jsify()
                as JSObject,
          )
          .toDart;
      if (!mounted) {
        stop(acquired);
        return;
      }
      stream = acquired;
      video!.srcObject = acquired;
      await video!.play().toDart;
      if (mounted) setState(() => ready = true);
    } catch (e) {
      if (stream case final value?) {
        stop(value);
        stream = null;
      }
      if (mounted) setState(() => error = '无法访问相机：$e');
    }
  }

  void capture() {
    final v = video!;
    if (v.videoWidth == 0 || v.videoHeight == 0) return;
    final canvas = _Canvas(_create('canvas'));
    canvas.width = v.videoWidth;
    canvas.height = v.videoHeight;
    canvas.getContext('2d').drawImage(v, 0, 0);
    final bytes = base64Decode(
      canvas.toDataURL('image/jpeg', .9).split(',').last,
    );
    Navigator.pop(context, FramePhoto(name: 'camera.jpg', bytes: bytes));
  }

  @override
  void dispose() {
    if (stream case final value?) stop(value);
    video?.srcObject = null;
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
    title: const Text('定影 · 拍照'),
    content: SizedBox(
      width: 640,
      height: 360,
      child: Stack(
        children: [
          Positioned.fill(
            child: HtmlElementView.fromTagName(
              tagName: 'video',
              onElementCreated: (element) {
                video = _Video(element as JSObject)
                  ..muted = true
                  ..autoplay = true
                  ..playsInline = true;
                video!.style
                  ..width = '100%'
                  ..height = '100%'
                  ..objectFit = 'contain';
                unawaited(start());
              },
            ),
          ),
          if (error != null) Center(child: Text(error!)),
        ],
      ),
    ),
    actions: [
      TextButton(
        onPressed: () => Navigator.pop(context),
        child: const Text('取消'),
      ),
      if (error != null) TextButton(onPressed: start, child: const Text('重试')),
      FilledButton(onPressed: ready ? capture : null, child: const Text('拍照')),
    ],
  );
}
