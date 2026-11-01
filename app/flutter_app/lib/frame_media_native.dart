import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'frame_quick_page.dart';

const _channel = MethodChannel('org.framefilm.ark/methods');
FramePhoto _photo(Map data) =>
    FramePhoto(name: data['name'] as String, bytes: data['bytes'] as Uint8List);

Future<List<FramePhoto>> pickFramePhotos({required bool multiple}) async {
  final data = await _channel.invokeListMethod<dynamic>('pickFramePhotos', {
    'multiple': multiple,
  });
  return (data ?? []).map((value) => _photo(value as Map)).toList();
}

Future<FramePhoto?> captureFramePhoto(BuildContext context) async {
  final data = await _channel.invokeMapMethod<String, dynamic>(
    'captureFramePhoto',
  );
  return data == null ? null : _photo(data);
}
