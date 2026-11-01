import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

bool get canDownloadFrame => defaultTargetPlatform == TargetPlatform.android;
Future<void> downloadFrame(Uint8List bytes, String name) async {
  if (!canDownloadFrame) throw UnsupportedError('当前平台不支持保存 film');
  await const MethodChannel('org.framefilm.ark/methods')
      .invokeMethod<void>('saveGeneratedFilm', {'bytes': bytes, 'name': name});
}
