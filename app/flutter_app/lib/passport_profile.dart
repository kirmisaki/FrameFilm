import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/painting.dart';

import 'frame_algorithms.dart' show filmPalette;

class PassportProfile {
  const PassportProfile({
    this.codename = '',
    this.codenameUnset = false,
    this.number = '',
    this.numberUnset = false,
    this.affiliation = '',
    this.signature = '',
    this.avatar,
    this.avatarMode = 'mono',
  });
  final String codename, number, affiliation, signature;
  final bool codenameUnset, numberUnset;
  final String? avatar;
  final String avatarMode;

  factory PassportProfile.fromJson(String raw) {
    if (utf8.encode(raw).length > 350000) {
      throw const FormatException('JSON 超过 350 KB');
    }
    final value = jsonDecode(raw);
    if (value is! Map<String, dynamic> || value['version'] != 1) {
      throw const FormatException('不支持的通行证 JSON 版本');
    }
    for (final field in {
      'codename': 32,
      'number': 36,
      'affiliation': 60,
      'signature': 200,
    }.entries) {
      if (value[field.key] is! String ||
          (value[field.key] as String).length > field.value) {
        throw FormatException('${field.key} 类型或长度无效');
      }
    }
    if (value['codenameUnset'] is! bool ||
        value['numberUnset'] is! bool ||
        !value.containsKey('avatar') ||
        (value['avatar'] != null && value['avatar'] is! String)) {
      throw const FormatException('尚未设置标记或头像类型无效');
    }
    final avatar = value['avatar'] as String?;
    final avatarMode = value['avatarMode'] ?? 'mono';
    if (!['mono', 'grayscale', 'color'].contains(avatarMode)) {
      throw const FormatException('头像显示模式无效');
    }
    if (avatar != null) {
      if (!avatar.startsWith('data:image/png;base64,') ||
          avatar.length > 300000) {
        throw const FormatException('头像须为不超过 300 KB 的 PNG data URL');
      }
      final bytes = base64Decode(avatar.substring(22));
      const magic = [137, 80, 78, 71, 13, 10, 26, 10];
      if (bytes.length < 33 ||
          !List.generate(8, (i) => bytes[i] == magic[i]).every((v) => v) ||
          ascii.decode(bytes.sublist(12, 16)) != 'IHDR' ||
          ByteData.sublistView(bytes).getUint32(16) != 128 ||
          ByteData.sublistView(bytes).getUint32(20) != 160) {
        throw const FormatException('头像必须为 128×160 PNG');
      }
    }
    return PassportProfile(
      codename: value['codename'],
      codenameUnset: value['codenameUnset'],
      number: value['number'],
      numberUnset: value['numberUnset'],
      affiliation: value['affiliation'],
      signature: value['signature'],
      avatar: avatar,
      avatarMode: avatarMode as String,
    );
  }
  String toJson() =>
      '${const JsonEncoder.withIndent('  ').convert({'version': 1, 'codename': codename, 'codenameUnset': codenameUnset, 'number': number, 'numberUnset': numberUnset, 'affiliation': affiliation, 'signature': signature, 'avatar': avatar, 'avatarMode': avatarMode})}\n';
  Uint8List? get avatarBytes =>
      avatar == null ? null : base64Decode(avatar!.substring(22));
}

TextPainter _text(
  String text,
  double size,
  int weight, {
  bool mono = false,
  Color color = const Color(0xff000000),
}) => TextPainter(
  text: TextSpan(
    text: text,
    style: TextStyle(
      fontSize: size,
      fontWeight: FontWeight.values[weight ~/ 100 - 1],
      color: color,
      fontFamily: mono ? 'monospace' : null,
    ),
  ),
  textDirection: TextDirection.ltr,
)..layout();

List<String> passportSignatureLines(String value) {
  final rows = <String>[];
  for (final paragraph in value.split(RegExp(r'\r?\n'))) {
    var row = '';
    for (final rune in paragraph.runes) {
      final char = String.fromCharCode(rune);
      if (row.isNotEmpty && _text(row + char, 17, 500).width > 370) {
        rows.add(row);
        row = char;
      } else {
        row += char;
      }
    }
    rows.add(row);
  }
  return rows;
}

Map<String, String> passportLayoutErrors(PassportProfile p) => {
  if (p.codename.trim().isEmpty && !p.codenameUnset) 'codename': '请输入代号或勾选尚未设置',
  if (_text(p.codename.trim(), 29, 700).width > 247) 'codename': '代号超出预览区域',
  if (p.number.trim().isEmpty && !p.numberUnset) 'number': '请输入编号或勾选尚未设置',
  if (_text(p.number.trim(), 17, 600, mono: true).width > 247)
    'number': '编号超出预览区域',
  if (_text(p.affiliation.trim(), 17, 600).width > 376)
    'affiliation': '所属超出预览区域',
  if (passportSignatureLines(p.signature.trim()).length > 3)
    'signature': '签名最多三行',
};

Future<ui.Image?> validatePassportAvatar(PassportProfile profile) async {
  final bytes = profile.avatarBytes;
  if (bytes == null) return null;
  final codec = await ui.instantiateImageCodec(bytes);
  try {
    final image = (await codec.getNextFrame()).image;
    if (image.width != 128 || image.height != 160) {
      image.dispose();
      throw const FormatException('头像尺寸无效');
    }
    return image;
  } finally {
    codec.dispose();
  }
}

Future<String> cropPassportAvatar(Uint8List bytes) async {
  if (bytes.length > 8 * 1024 * 1024) {
    throw const FormatException('请选择不超过 8 MiB 的图片');
  }
  final codec = await ui.instantiateImageCodec(bytes);
  final image = (await codec.getNextFrame()).image;
  codec.dispose();
  try {
    final scale = image.width / 128 < image.height / 160
        ? image.width / 128
        : image.height / 160;
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder);
    canvas.drawColor(const Color(0xffffffff), BlendMode.src);
    canvas.drawImageRect(
      image,
      Rect.fromLTWH(
        (image.width - 128 * scale) / 2,
        (image.height - 160 * scale) / 2,
        128 * scale,
        160 * scale,
      ),
      const Rect.fromLTWH(0, 0, 128, 160),
      Paint()..filterQuality = FilterQuality.high,
    );
    final picture = recorder.endRecording();
    final output = await picture.toImage(128, 160);
    picture.dispose();
    try {
      return 'data:image/png;base64,${base64Encode((await output.toByteData(format: ui.ImageByteFormat.png))!.buffer.asUint8List())}';
    } finally {
      output.dispose();
    }
  } finally {
    image.dispose();
  }
}

class PassportRaster {
  const PassportRaster(this.bin, this.png);
  final Uint8List bin, png;
}

// The exact quantized pixels are shared by PNG preview and device payload.
// Gray is spatial black/white halftone, not a native grayscale panel mode.
Uint8List encodePassportPixels(
  Uint8List rgba,
  String mode, {
  required bool hasAvatar,
}) {
  if (rgba.length != 440 * 608 * 4 ||
      !['mono', 'grayscale', 'color'].contains(mode)) {
    throw ArgumentError('通行证像素或头像模式无效');
  }
  final color = mode == 'color';
  final bin = Uint8List(color ? 133776 : 33456)
    ..setAll(0, [70, 70, 85, 73, color ? 2 : 1, color ? 2 : 1, 184, 1, 96, 2]);
  const bayer = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
  const codes = [0, 1, 2, 3, 5, 6];
  for (var i = 0; i < 440 * 608; i++) {
    final k = i * 4, x = i % 440, y = i ~/ 440;
    final inAvatar = hasAvatar && x >= 20 && x < 148 && y >= 143 && y < 303;
    final luminance = .299 * rgba[k] + .587 * rgba[k + 1] + .114 * rgba[k + 2];
    var index = luminance < 160 ? 0 : 1;
    if (inAvatar && color) {
      var distance = double.infinity;
      for (var p = 0; p < filmPalette.length; p++) {
        final rgb = filmPalette[p];
        final r = rgba[k] - rgb[0],
            g = rgba[k + 1] - rgb[1],
            b = rgba[k + 2] - rgb[2];
        final d = .299 * r * r + .587 * g * g + .114 * b * b;
        if (d < distance) {
          distance = d;
          index = p;
        }
      }
    } else if (inAvatar && mode == 'grayscale') {
      final threshold =
          (bayer[((y - 143) & 3) * 4 + ((x - 20) & 3)] + .5) * 255 / 16;
      index = luminance < threshold ? 0 : 1;
    }
    final rgb = filmPalette[index];
    rgba[k] = rgb[0];
    rgba[k + 1] = rgb[1];
    rgba[k + 2] = rgb[2];
    rgba[k + 3] = 255;
    if (color) {
      bin[16 + (i >> 1)] |= codes[index] << (i.isEven ? 4 : 0);
    } else if (index == 0) {
      bin[16 + (i >> 3)] |= 0x80 >> (i & 7);
    }
  }
  return bin;
}

Future<PassportRaster> renderPassport(PassportProfile p) async {
  final avatar = await validatePassportAvatar(p);
  final recorder = ui.PictureRecorder();
  final c = Canvas(recorder);
  c.drawColor(const Color(0xffffffff), BlendMode.src);
  void txt(
    String v,
    double x,
    double y,
    double size, [
    int weight = 500,
    bool mono = false,
  ]) {
    final t = _text(v, size, weight, mono: mono);
    t.paint(
      c,
      Offset(x, y - t.computeDistanceToActualBaseline(TextBaseline.alphabetic)),
    );
  }

  void line(double x, double y, double x2, double y2) => c.drawLine(
    Offset(x + .5, y + .5),
    Offset(x2 + .5, y2 + .5),
    Paint()..color = const Color(0xff000000),
  );
  void chip(String v, double x, double y, double w) {
    c.drawRect(
      Rect.fromLTWH(x, y, w, 23),
      Paint()..color = const Color(0xff000000),
    );
    final t = _text(v, 14, 700, color: const Color(0xffffffff));
    t.paint(
      c,
      Offset(
        x + (w - t.width) / 2,
        y + 17 - t.computeDistanceToActualBaseline(TextBaseline.alphabetic),
      ),
    );
  }

  txt('RHODES ISLAND', 20, 24, 10, 700, true);
  chip('档案 / 01', 325, 9, 95);
  line(20, 34, 420, 34);
  txt('干员档案', 20, 79, 31, 700);
  txt('罗德岛终端 / 个人身份档案', 21, 103, 12, 600);
  line(20, 119, 420, 119);
  if (avatar != null) {
    c.drawImage(avatar, const Offset(20, 143), Paint());
  } else {
    c.drawRect(
      const Rect.fromLTWH(20.5, 143.5, 127, 159),
      Paint()..style = PaintingStyle.stroke,
    );
    line(36, 159, 132, 287);
    line(132, 159, 36, 287);
    c.drawRect(
      const Rect.fromLTWH(38, 209, 92, 27),
      Paint()..color = const Color(0xffffffff),
    );
    txt('未设置头像', 52, 227, 13, 700);
  }
  line(20, 308, 148, 308);
  chip('代号', 173, 143, 65);
  txt(p.codename.trim().isEmpty ? '未设置' : p.codename.trim(), 173, 204, 29, 700);
  line(173, 220, 420, 220);
  txt('档案编号', 173, 246, 13, 600);
  txt(
    p.number.trim().isEmpty ? '未设置' : p.number.trim(),
    173,
    276,
    17,
    600,
    true,
  );
  line(173, 291, 420, 291);
  txt('档案 01 / 身份', 173, 320, 11, 700);
  line(20, 354, 420, 354);
  chip('所属', 20, 371, 65);
  txt(
    p.affiliation.trim().isEmpty ? '未设置' : p.affiliation.trim(),
    21,
    427,
    17,
    600,
  );
  line(20, 447, 420, 447);
  txt('个人签名', 20, 473, 13, 700);
  final rows = passportSignatureLines(
    p.signature.trim().isEmpty ? '未设置' : p.signature.trim(),
  );
  for (var i = 0; i < rows.length && i < 3; i++) {
    txt(rows[i], 21, 506 + i * 27, 17);
  }
  line(20, 580, 420, 580);
  txt('ARKNIGHTS / 个人档案', 20, 597, 10, 700);
  txt('罗德岛', 377, 597, 10, 700);
  final picture = recorder.endRecording();
  final image = await picture.toImage(440, 608);
  picture.dispose();
  avatar?.dispose();
  final rgba = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!
      .buffer
      .asUint8List();
  image.dispose();
  final bin = encodePassportPixels(
    rgba,
    p.avatarMode,
    hasAvatar: p.avatar != null,
  );
  final buffer = await ui.ImmutableBuffer.fromUint8List(rgba);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: 440,
    height: 608,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descriptor.instantiateCodec();
  final monochrome = (await codec.getNextFrame()).image;
  final png = (await monochrome.toByteData(format: ui.ImageByteFormat.png))!
      .buffer
      .asUint8List();
  monochrome.dispose();
  codec.dispose();
  descriptor.dispose();
  buffer.dispose();
  return PassportRaster(bin, png);
}
