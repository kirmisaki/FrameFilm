import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/painting.dart';

import 'frame_abc512.dart';
import 'frame_codec.dart';
import 'frame_algorithms.dart' show quantizeFramePixels, FrameAlgorithm;
import 'frame_palettes.dart' show colorFastPalette;
import 'passport_color.dart';
import 'passport_profile.dart';

const studentPassportPalettes = {
  'amiya': '阿米娅',
  'sniper': '狙击',
  'medic': '医疗',
  'vanguard': '先锋',
  'guard': '近卫',
  'defender': '重装',
  'caster': '术士',
  'supporter': '辅助',
  'specialist': '特种',
};

String studentPassportTemplateAsset(String palette) =>
    'assets/student_passport/$palette.png';

const _width = 720, _height = 480, _pixels = _width * _height;
const _avatarRect = Rect.fromLTWH(41, 111, 239, 299);
const _fields = {'codename': 32, 'studentNumber': 12, 'race': 60};

class StudentPassportProfile {
  const StudentPassportProfile({
    this.codename = '',
    this.codenameUnset = false,
    this.studentNumber = '',
    this.studentNumberUnset = false,
    this.race = '',
    this.avatar,
    this.avatarMode = 'color',
    this.palette = 'amiya',
    String studentOutput = 'i006',
    String? output,
    this.version = 3,
  }) : studentOutput = output ?? studentOutput;

  final int version;
  String get output => studentOutput;

  static const template = 'rhodes-academy-student';
  final String codename,
      studentNumber,
      race,
      avatarMode,
      palette,
      studentOutput;
  final bool codenameUnset, studentNumberUnset;
  final String? avatar;

  StudentPassportProfile withAvatar(String? avatar, {String? output}) => StudentPassportProfile(
    codename: codename,
    codenameUnset: codenameUnset,
    studentNumber: studentNumber,
    studentNumberUnset: studentNumberUnset,
    race: race,
    avatar: avatar,
    palette: palette,
    output: output ?? this.output,
  );

  factory StudentPassportProfile.fromJson(String raw) {
    if (utf8.encode(raw).length > 350000) {
      throw const FormatException('JSON 超过 350 KB');
    }
    final value = jsonDecode(raw);
    if (value is! Map<String, dynamic> ||
        value['version'] is! int ||
        !(value['version'] == 2 || value['version'] == 3) ||
        value['template'] != template ||
        (value['version'] == 3 && value['kind'] != 'student')) {
      throw const FormatException('不支持的学生证模板或版本');
    }
    for (final field in _fields.entries) {
      final text = value[field.key];
      if (text is! String || text.length > field.value) {
        throw FormatException('${field.key} 类型或长度无效');
      }
    }
    if (value['codenameUnset'] is! bool ||
        value['studentNumberUnset'] is! bool ||
        !value.containsKey('avatar')) {
      throw const FormatException('尚未设置标记或头像类型无效');
    }
    final palette = value.containsKey('palette') ? value['palette'] : 'amiya';
    final modern = value['version'] == 3;
    final output = modern ? value['output']
        : value.containsKey('studentOutput') ? value['studentOutput'] : 'i006';
    if (!studentPassportPalettes.containsKey(palette) ||
        !(modern ? passportOutputs : const ['i006', 'ffui']).contains(output) ||
        (modern && value.containsKey('studentOutput') && value['studentOutput'] != output)) {
      throw const FormatException('学生证配色或输出模式无效');
    }
    final avatar = value['avatar'];
    if (avatar != null && avatar is! String) {
      throw const FormatException('头像类型无效');
    }
    passportPngBytes(avatar as String?, modern ? 239 : 128, modern ? 299 : 160);
    final mode = value.containsKey('avatarMode')
        ? value['avatarMode']
        : (modern ? 'color' : 'mono');
    if (!(modern ? const ['color'] : const ['mono', 'grayscale', 'color'])
        .contains(mode)) {
      throw const FormatException('头像显示模式无效');
    }
    return StudentPassportProfile(
      codename: value['codename'] as String,
      codenameUnset: value['codenameUnset'] as bool,
      studentNumber: value['studentNumber'] as String,
      studentNumberUnset: value['studentNumberUnset'] as bool,
      race: value['race'] as String,
      avatar: avatar,
      avatarMode: mode as String,
      palette: palette as String,
      studentOutput: output as String,
      version: value['version'] as int,
    );
  }

  String toJson() {
    return encodePassportJson({
      'version': version,
      if (version == 3) 'kind': 'student',
      if (version == 3) 'output': output,
      'template': template,
      'palette': palette,
      if (version == 2) 'studentOutput': studentOutput,
      'codename': codename,
      'codenameUnset': codenameUnset,
      'studentNumber': studentNumber,
      'studentNumberUnset': studentNumberUnset,
      'race': race,
      'avatar': avatar,
      'avatarMode': avatarMode,
    });
  }
}

TextPainter _text(
  String value,
  double size,
  FontWeight weight, {
  bool mono = false,
  Color color = const Color(0xff000000),
}) => TextPainter(
  text: TextSpan(
    text: value,
    style: TextStyle(
      fontSize: size,
      fontWeight: weight,
      color: color,
      fontFamily: mono ? 'monospace' : null,
    ),
  ),
  textDirection: TextDirection.ltr,
  maxLines: 1,
)..layout();

bool _validStudentNumber(String value) {
  if (!RegExp(r'^[0-9]{12}$').hasMatch(value)) return false;
  bool validDate(int year, int offset) {
    final month = int.parse(value.substring(offset, offset + 2));
    final day = int.parse(value.substring(offset + 2, offset + 4));
    final date = DateTime.utc(year, month, day);
    return year > 0 && date.month == month && date.day == day;
  }

  return validDate(int.parse(value.substring(0, 4)), 4) && validDate(2000, 8);
}

Map<String, String> studentPassportLayoutErrors(StudentPassportProfile p) {
  final codename = p.codename.trim();
  final number = p.studentNumber.trim();
  final race = p.race.trim();
  bool tooWide(TextPainter painter, double width) {
    try {
      return painter.width > width || painter.didExceedMaxLines;
    } finally {
      painter.dispose();
    }
  }

  return {
    if (codename.isEmpty && !p.codenameUnset)
      'codename': '请输入代号或勾选尚未设置'
    else if (p.codename.length > 32 ||
        tooWide(_text(codename, 35, FontWeight.w700), 395))
      'codename': '代号超出预览区域',
    if (number.isEmpty && !p.studentNumberUnset)
      'studentNumber': '请输入学号或勾选尚未设置'
    else if (p.studentNumber.length > 12 ||
        (number.isNotEmpty && !_validStudentNumber(number)))
      'studentNumber': '请输入 12 位有效日期（YYYYMMDD + MMDD）',
    if (p.race.length > 60 || tooWide(_text(race, 20, FontWeight.w600), 235))
      'race': '种族超出预览区域',
  };
}

Future<ui.Image?> validateStudentPassportAvatar(
  StudentPassportProfile p,
) async {
  final bytes = passportPngBytes(
    p.avatar,
    p.version == 3 ? 239 : 128,
    p.version == 3 ? 299 : 160,
  );
  if (bytes == null) return null;
  final codec = await ui.instantiateImageCodec(bytes);
  try {
    final image = (await codec.getNextFrame()).image;
    if (image.width != (p.version == 3 ? 239 : 128) ||
        image.height != (p.version == 3 ? 299 : 160)) {
      image.dispose();
      throw const FormatException('头像尺寸无效');
    }
    return image;
  } finally {
    codec.dispose();
  }
}

Future<ui.Image> _decodeTemplate(Uint8List bytes) async {
  const magic = [137, 80, 78, 71, 13, 10, 26, 10];
  if (bytes.length < 33 ||
      bytes.length > 8 * 1024 * 1024 ||
      !List.generate(8, (i) => bytes[i] == magic[i]).every((valid) => valid) ||
      String.fromCharCodes(bytes.sublist(12, 16)) != 'IHDR' ||
      ByteData.sublistView(bytes).getUint32(16) != _width ||
      ByteData.sublistView(bytes).getUint32(20) != _height) {
    throw const FormatException('请选择 720×480 且不超过 8 MiB 的 PNG 底图');
  }
  final codec = await ui.instantiateImageCodec(bytes);
  try {
    final image = (await codec.getNextFrame()).image;
    if (image.width != _width || image.height != _height) {
      image.dispose();
      throw const FormatException('学生证底图尺寸无效');
    }
    return image;
  } finally {
    codec.dispose();
  }
}

Future<PassportRaster> renderStudentPassport(
  StudentPassportProfile profile,
  Uint8List templatePng, {
  bool Function()? isCancelled,
}) async {
  void checkCancelled() {
    if (isCancelled?.call() == true) throw StateError('预览已取消');
  }

  checkCancelled();
  if (!(profile.version == 2 || profile.version == 3) ||
      (profile.version == 3 && !passportOutputs.contains(profile.output))) {
    throw const FormatException('学生证版本或输出模式无效');
  }
  final template = await _decodeTemplate(templatePng);
  late Uint8List base, composed;
  try {
    final avatar = await validateStudentPassportAvatar(profile);
    try {
      base = await _drawStudent(template);
      checkCancelled();
      composed = await _drawStudent(template, profile: profile, avatar: avatar);
    } finally {
      avatar?.dispose();
    }
  } finally {
    template.dispose();
  }
  checkCancelled();

  final logical = Uint8List(_pixels * 4);
  for (var y = 0; y < _height; y++) {
    for (var x = 0; x < _width; x++) {
      final source = (y * _width + x) * 4;
      final target = (x * _height + _height - 1 - y) * 4;
      logical.setRange(target, target + 4, composed, source);
    }
  }
  final abc = profile.output != 'colorfast';
  final List<int> codes;
  if (abc) {
    final abcCodes = await quantizeAbc512(
      logical,
      _height,
      _width,
      continuousTone: true,
      checkCancelled: checkCancelled,
    );
    _applyWatermark(abcCodes, base, composed);
    if (profile.palette == 'amiya') _applyRedFade(abcCodes, base);
    codes = abcCodes;
  } else {
    codes = await quantizeFramePixels(
      logical,
      _height,
      _width,
      algorithm: FrameAlgorithm.colorFast55,
      checkCancelled: checkCancelled,
    );
  }
  checkCancelled();
  final bin = packFramePixels(
    codes,
    width: _height,
    height: _width,
    format: abc ? FrameFormat.abc512 : FrameFormat.colorFast55,
  );
  final readerCodes = Uint16List(_pixels);
  for (var y = 0; y < _height; y++) {
    for (var x = 0; x < _width; x++) {
      readerCodes[y * _width + x] = codes[x * _height + _height - 1 - y];
    }
  }
  if (abc) {
    paintAbc512Codes(composed, readerCodes);
  } else {
    for (var i = 0; i < _pixels; i++) {
      final rgb = colorFastPalette[readerCodes[i]];
      composed.setRange(i * 4, i * 4 + 4, [...rgb, 255]);
    }
  }
  final png = await _encodePreview(composed);
  checkCancelled();
  return PassportRaster(bin, png);
}

// The aligned Amiya stripe occupies reader coordinates x=[414,490), y=[0,10).
const _redFadeX = 414, _redFadeWidth = 76;
const _redFadeSamples = [(414, 0xf6bfb7), (453, 0xdf7163), (489, 0xd19595)];

// I6RFW101.film, reader-order crop (415, 0, 78, 10). A–S index the ABC9 codes.
const _wCodes = [
  22, 32, 33, 34, 46, 47, 87, 97, 110, 111,
  173, 174, 238, 295, 303, 366, 430, 487, 495,
];
const _wRows = [
  'JOEONORORERROOERORERRRRSOROERRORPOEOSORORERSOOSSOSSSSESOOSOOOOOQQOKIKOKMKKQKLM',
  'GOHNNREROOOOEROEROREOORSERSSORSOESRREROSOOOOSROESOSSOOSOOEOOOSKIKOQQKQIKMIMMIA',
  'GNOOFOOERRERSROROEOBESORRORORESRSROSOSOERSSESROSOSOCSSOSOOSOSOOQOQIOIOMIQKIKIA',
  'JFHORORSOEOROEORERROORESOSEROSROEOSORESSOEOROSOCSSEOOCOOOOOEOOQIQIKOQOKIMIMQMM',
  'GRHRNNORROREOREORORERORORPSREOEOSREREROSPORSOESOEOSSSOSOEQOOOOKOOQQQKQMKQMKIIM',
  'GOOOFQROOROSROSREOEOREOESOOERERROESOOSROOSOSROSDSOOOESOOOOOOSOQQIOOIIOKMIKMIKM',
  'JOHNOOESRERRORRORERREORRREROROSESORESOESSOSEOSSOSSCSSOKOSOSOSOOOKOQQOOIIKIKKMM',
  'JNONNRRORSOOSRORORORORSOESSRSERORRESRPSOREOOSPOSOSOSOEOOOOOKOOQIQQOKQQKMQIMLIA',
  'GSONFORSORRREROEREOSDREROROESROROEOEOOSSOSSSSSOSSESSSSOSOSOSOSOQOOOKLOKLMIKKIM',
  'JRHDORREROROOERCORREOOSERESOROSESRRRSEDSESDSOESRSSSESOOSEOEOEOKKOQQQOKQLKLQLMM',
];

void _applyRedFade(Uint16List codes, Uint8List base) {
  for (final (x, expected) in _redFadeSamples) {
    final i = x * 4;
    final rgb = (base[i] << 16) | (base[i + 1] << 8) | base[i + 2];
    if (rgb != expected) return;
  }
  // Match the narrower PSD layer with nearest-neighbor ABC9 sampling.
  for (var y = 0; y < _wRows.length; y++) {
    final row = _wRows[y];
    for (var x = 0; x < _redFadeWidth; x++) {
      final sourceX = (2 * x + 1) * row.length ~/ (2 * _redFadeWidth);
      codes[(_redFadeX + x) * _height + _height - 1 - y] =
          _wCodes[row.codeUnitAt(sourceX) - 65];
    }
  }
}

Future<Uint8List> _drawStudent(
  ui.Image template, {
  StudentPassportProfile? profile,
  ui.Image? avatar,
}) async {
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder);
  canvas.drawColor(const Color(0xffffffff), BlendMode.src);
  canvas.drawImage(template, Offset.zero, Paint());
  if (avatar != null) {
    canvas.drawImageRect(
      avatar,
      Rect.fromLTWH(0, 0, avatar.width.toDouble(), avatar.height.toDouble()),
      _avatarRect,
      Paint()..filterQuality = FilterQuality.high,
    );
  }
  if (profile != null) {
    void paint(
      TextPainter text,
      double x,
      double baseline, {
      double? fitWidth,
    }) {
      final ascent = text.computeDistanceToActualBaseline(
        TextBaseline.alphabetic,
      );
      canvas.save();
      canvas.clipRect(
        Rect.fromLTWH(x, baseline - ascent, 720 - x, text.height),
      );
      canvas.translate(x, baseline - ascent);
      if (fitWidth != null && text.width > fitWidth) {
        canvas.scale(fitWidth / text.width, 1);
      }
      text.paint(canvas, Offset.zero);
      canvas.restore();
      text.dispose();
    }

    String display(String value) => value.trim().isEmpty ? '未设置' : value.trim();
    paint(
      _text(
        display(profile.codename),
        35,
        FontWeight.w700,
        color: const Color(0xffffffff),
      ),
      305,
      293,
    );
    paint(
      _text(display(profile.studentNumber), 20, FontWeight.w600, mono: true),
      308,
      355,
      fitWidth: 235,
    );
    paint(_text(display(profile.race), 20, FontWeight.w600), 304, 408);
  }
  final picture = recorder.endRecording();
  try {
    final image = await picture.toImage(_width, _height);
    try {
      return (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!
          .buffer
          .asUint8List();
    } finally {
      image.dispose();
    }
  } finally {
    picture.dispose();
  }
}

void _applyWatermark(Uint16List codes, Uint8List base, Uint8List composed) {
  const white = 75, neutralGray = 407;
  final luma = Uint8List(_pixels);
  for (var i = 0; i < _pixels; i++) {
    final k = i * 4;
    // Pillow RGB.convert('L') rounding used by the reviewed A trial.
    luma[i] =
        (19595 * base[k] + 38470 * base[k + 1] + 7471 * base[k + 2] + 32768) >>
        16;
  }
  final mask = Uint8List(_pixels), coverage = Float64List(_pixels);
  bool hasMargin(int x, int y) {
    for (var dy = -2; dy <= 2; dy++) {
      final row = (y + dy).clamp(0, _height - 1) * _width;
      for (var dx = -2; dx <= 2; dx++) {
        if (luma[row + (x + dx).clamp(0, _width - 1)] < 225) return false;
      }
    }
    return true;
  }

  for (var y = 130; y < _height; y++) {
    for (var x = 385; x < _width; x++) {
      final i = y * _width + x, k = i * 4, value = luma[i];
      if (value < 232 || value == 255) continue;
      final r = base[k], g = base[k + 1], b = base[k + 2];
      if ((r - g).abs() > 3 ||
          (g - b).abs() > 3 ||
          (r - b).abs() > 3 ||
          r != composed[k] ||
          g != composed[k + 1] ||
          b != composed[k + 2] ||
          base[k + 3] != composed[k + 3] ||
          !hasMargin(x, y)) {
        continue;
      }
      // The avatar ends at x=280, outside the watermark ROI.
      mask[i] = 1;
      coverage[i] = ((255 - value) / 11).clamp(0, 1) * .35;
    }
  }
  void diffuse(int x, int y, double error, int weight) {
    if (x < 0 || x >= _width || y >= _height) return;
    final i = y * _width + x;
    if (mask[i] != 0) coverage[i] += error * weight / 16;
  }

  for (var y = 130; y < _height; y++) {
    final direction = y.isEven ? 1 : -1;
    for (
      var x = y.isEven ? 385 : _width - 1;
      x >= 385 && x < _width;
      x += direction
    ) {
      final i = y * _width + x;
      if (mask[i] == 0) continue;
      final value = coverage[i].clamp(0.0, 1.0);
      final gray = value >= .5;
      // Quantizer order is clockwise portrait. ABC9 packing rotates it again.
      codes[x * _height + _height - 1 - y] = gray ? neutralGray : white;
      final error = (value - (gray ? 1 : 0)).toDouble();
      diffuse(x + direction, y, error, 7);
      diffuse(x - direction, y + 1, error, 3);
      diffuse(x, y + 1, error, 5);
      diffuse(x + direction, y + 1, error, 1);
    }
  }
}

Future<Uint8List> _encodePreview(Uint8List rgba) async {
  final buffer = await ui.ImmutableBuffer.fromUint8List(rgba);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: _width,
    height: _height,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  try {
    final codec = await descriptor.instantiateCodec();
    try {
      final image = (await codec.getNextFrame()).image;
      try {
        return (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer
            .asUint8List();
      } finally {
        image.dispose();
      }
    } finally {
      codec.dispose();
    }
  } finally {
    descriptor.dispose();
    buffer.dispose();
  }
}
