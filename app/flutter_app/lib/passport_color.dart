import 'dart:convert';
import 'dart:typed_data';

import 'package:image/image.dart' as img;

import 'frame_abc512.dart';
import 'frame_algorithms.dart';

const passportJsonLimit = 350000;
const passportOutputs = ['i006', 'colorfast'];

String encodePassportJson(Map<String, dynamic> value) {
  final raw = '${const JsonEncoder.withIndent('  ').convert(value)}\n';
  if (utf8.encode(raw).length > passportJsonLimit) {
    throw const FormatException('JSON 超过 350 KB，请重新选择头像');
  }
  return raw;
}

/// Opaque, metadata-free RGB PNG. No palette or photo quantization is applied.
String passportRgbPng(Uint8List rgba, int width, int height) {
  final image = img.Image(width: width, height: height, numChannels: 3);
  for (var y = 0; y < height; y++) {
    for (var x = 0; x < width; x++) {
      final k = (y * width + x) * 4;
      final white = 255 - rgba[k + 3];
      image.setPixelRgb(
        x,
        y,
        (rgba[k] + white).clamp(0, 255),
        (rgba[k + 1] + white).clamp(0, 255),
        (rgba[k + 2] + white).clamp(0, 255),
      );
    }
  }
  return 'data:image/png;base64,${base64Encode(img.encodePng(image))}';
}

String passportPreviewCrop(Uint8List png, int x, int y, int width, int height) {
  final decoded = img.decodePng(png);
  if (decoded == null) throw const FormatException('预览 PNG 无效');
  final crop = img.copyCrop(decoded, x: x, y: y, width: width, height: height);
  return 'data:image/png;base64,${base64Encode(img.encodePng(crop))}';
}

Uint8List? passportPngBytes(String? avatar, int width, int height) {
  if (avatar == null) return null;
  if (!avatar.startsWith('data:image/png;base64,') || avatar.length > 300000) {
    throw const FormatException('头像须为不超过 300 KB 的 PNG data URL');
  }
  final bytes = base64Decode(avatar.substring(22));
  const magic = [137, 80, 78, 71, 13, 10, 26, 10];
  if (bytes.length < 33 ||
      !List.generate(8, (i) => bytes[i] == magic[i]).every((v) => v) ||
      String.fromCharCodes(bytes.sublist(12, 16)) != 'IHDR' ||
      ByteData.sublistView(bytes).getUint32(16) != width ||
      ByteData.sublistView(bytes).getUint32(20) != height) {
    throw FormatException('头像必须为 $width×$height PNG');
  }
  return bytes;
}

/// Returns native indices and paints exactly those indices back into rgba.
Future<List<int>> quantizePassportColor(
  Uint8List rgba,
  int width,
  int height,
  String output, {
  void Function()? checkCancelled,
}) async {
  if (output == 'i006') {
    final codes = await quantizeAbc512(
      rgba,
      width,
      height,
      continuousTone: true,
      checkCancelled: checkCancelled,
    );
    return codes;
  }
  if (output == 'colorfast') {
    return quantizeFramePixels(
      rgba,
      width,
      height,
      algorithm: FrameAlgorithm.colorFast55,
      checkCancelled: checkCancelled,
    );
  }
  throw const FormatException('请选择 I006 或 ColorFast 输出');
}
