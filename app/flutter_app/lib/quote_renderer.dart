import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

/// Draws the Frame quote card at the device's logical portrait resolution.
Future<Uint8List> renderFrameQuote(
  String text,
  String author, {
  int? battery,
  DateTime? date,
  int? colorScheme,
}) async {
  const w = 480.0, h = 720.0;
  final accent = <Color>[
    const Color(0xffff0000),
    const Color(0xff0000ff),
    const Color(0xff29cc14),
  ][(colorScheme ?? math.Random().nextInt(3)) % 3];
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder);
  canvas.drawColor(Colors.white, BlendMode.src);
  final accentPaint = Paint()..color = accent;
  _drawText(canvas, '“', 20, 15, 90, size: 80, color: accent, italic: true);
  canvas.drawLine(
    Offset(35, 105),
    Offset(90, 105),
    Paint()
      ..color = accent
      ..strokeWidth = 3,
  );

  final body = TextPainter(
    text: TextSpan(
      text: text,
      style: const TextStyle(
        color: Colors.black,
        fontSize: 30,
        height: 44 / 30,
        fontWeight: FontWeight.bold,
        fontFamily: 'Noto Serif SC',
        fontFamilyFallback: ['Songti SC', 'SimSun', 'serif'],
      ),
    ),
    textAlign: TextAlign.center,
    textDirection: TextDirection.ltr,
  )..layout(maxWidth: w - 80);
  final bottomSpace = author.isEmpty ? 60.0 : 130.0;
  final available = h - 100 - bottomSpace;
  final bodyTop = 100 + (available - body.height) / 2;
  body.paint(canvas, Offset((w - body.width) / 2, math.max(100, bodyTop)));
  body.dispose();

  if (author.isNotEmpty) {
    _drawText(
      canvas,
      '—— $author',
      w / 2,
      h - 130,
      w - 70,
      size: 18,
      bold: true,
      center: true,
    );
  }
  canvas.drawLine(
    Offset(w / 2 - 30, h - 65),
    Offset(w / 2 + 30, h - 65),
    Paint()
      ..color = accent
      ..strokeWidth = 4,
  );

  final batteryRect = RRect.fromRectAndRadius(
    const Rect.fromLTWH(w - 55, 20, 35, 18),
    const Radius.circular(4),
  );
  canvas.drawRRect(
    batteryRect,
    Paint()
      ..color = accent
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.5,
  );
  canvas.drawRect(const Rect.fromLTWH(w - 19, 25, 3, 8), accentPaint);
  final level = ((battery ?? 0).clamp(0, 100)) / 100;
  if (level > 0) {
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromLTWH(w - 53, 22, 31 * level, 14),
        const Radius.circular(2),
      ),
      accentPaint,
    );
  }

  final today = date ?? DateTime.now();
  final dateText =
      '${today.year} 年 ${today.month.toString().padLeft(2, '0')} 月 '
      '${today.day.toString().padLeft(2, '0')} 日';
  _drawText(
    canvas,
    dateText,
    w / 2,
    h - 30,
    w - 80,
    size: 16,
    color: accent,
    center: true,
  );

  final picture = recorder.endRecording();
  try {
    final image = await picture.toImage(w.toInt(), h.toInt());
    try {
      final data = await image.toByteData(format: ui.ImageByteFormat.png);
      return data!.buffer.asUint8List();
    } finally {
      image.dispose();
    }
  } finally {
    picture.dispose();
  }
}

void _drawText(
  Canvas canvas,
  String text,
  double x,
  double y,
  double maxWidth, {
  required double size,
  Color color = Colors.black,
  bool bold = false,
  bool italic = false,
  bool center = false,
}) {
  final painter = TextPainter(
    text: TextSpan(
      text: text,
      style: TextStyle(
        color: color,
        fontSize: size,
        fontWeight: bold ? FontWeight.bold : FontWeight.normal,
        fontStyle: italic ? FontStyle.italic : FontStyle.normal,
        fontFamily: 'Noto Serif SC',
        fontFamilyFallback: const ['Songti SC', 'SimSun', 'serif'],
      ),
    ),
    textAlign: center ? TextAlign.center : TextAlign.left,
    textDirection: TextDirection.ltr,
  )..layout(maxWidth: maxWidth);
  painter.paint(
    canvas,
    Offset(center ? x - painter.width / 2 : x, y - painter.height / 2),
  );
  painter.dispose();
}
