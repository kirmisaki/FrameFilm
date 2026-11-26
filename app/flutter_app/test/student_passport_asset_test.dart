import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

const _palettes = [
  'amiya',
  'caster',
  'defender',
  'guard',
  'medic',
  'sniper',
  'specialist',
  'supporter',
  'vanguard',
];

const _width = 720;
const _height = 480;
const _bandLeft = 185;
const _stripeHeight = 10;
const _stripeRuns = [
  (271, 274),
  (294, 299),
  (315, 324),
  (335, 352),
  (369, 407),
  (414, 490),
  (497, 572),
  (576, 720),
];
const _avatar = (left: 41, top: 111, right: 280, bottom: 410);

Uint8List _rgb(String palette) {
  final file = File('assets/student_passport/$palette.png');
  expect(file.existsSync(), isTrue, reason: '${file.path} is missing');
  final decoded = img.decodePng(file.readAsBytesSync());
  expect(decoded, isNotNull, reason: '${file.path} is not a PNG');
  expect((decoded!.width, decoded.height), (_width, _height));
  return decoded.getBytes(order: img.ChannelOrder.rgb);
}

void main() {
  test(
    'student card files preserve the palette band, avatar and watermark',
    () {
      final images = {for (final palette in _palettes) palette: _rgb(palette)};
      final first = images[_palettes.first]!;

      int spreadAt(Uint8List image, int i) {
        var spread = 0;
        for (var k = 0; k < 3; k++) {
          var lo = image[i + k], hi = image[i + k];
          for (final palette in _palettes) {
            final v = images[palette]![i + k];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
          }
          if (hi - lo > spread) spread = hi - lo;
        }
        return spread;
      }

      var varyingBandPixels = 0;
      for (var y = 0; y < _height; y++) {
        for (var x = 0; x < _width; x++) {
          final i = (y * _width + x) * 3;
          if (spreadAt(first, i) <= 8) continue;
          if (y < _stripeHeight) continue;
          final inAvatar =
              y >= _avatar.top &&
              y < _avatar.bottom &&
              x >= _avatar.left &&
              x < _avatar.right;
          if (inAvatar) continue; // avatar slot tint keeps its own variance
          expect(x, lessThan(_bandLeft), reason: 'background escaped at $x,$y');
          varyingBandPixels++;
        }
      }
      expect(varyingBandPixels, greaterThan(40000));

      // The vacated strip is plain card stock in every palette.
      for (final image in images.values) {
        for (var y = _stripeHeight; y < _height; y++) {
          final inAvatar = y >= _avatar.top && y < _avatar.bottom;
          for (var x = _bandLeft; x < 218; x++) {
            if (inAvatar) continue; // avatar slot covers these columns
            final i = (y * _width + x) * 3;
            expect(
              (image[i], image[i + 1], image[i + 2]),
              (255, 255, 255),
              reason: 'vacated strip not white at $x,$y',
            );
          }
        }
      }

      // The avatar slot's left border stays at x=41.
      for (final image in images.values) {
        final i = (300 * _width + _avatar.left) * 3;
        expect((image[i], image[i + 1], image[i + 2]), isNot((255, 255, 255)));
      }

      // The watermark ROI is static and never crossed by the shift.
      for (var y = 130; y < _height; y++) {
        for (var x = 385; x < _width; x++) {
          expect(
            spreadAt(first, (y * _width + x) * 3),
            lessThanOrEqualTo(8),
            reason: 'watermark pixel $x,$y varies across palettes',
          );
        }
      }
    },
  );

  test('avatar seams join the palette band without a tint or overhang', () {
    for (final palette in _palettes) {
      final image = _rgb(palette);
      for (var y = _avatar.top - 1; y <= _avatar.bottom; y++) {
        for (var x = 1; x < _avatar.left; x++) {
          final i = (y * _width + x) * 3;
          for (var channel = 0; channel < 3; channel++) {
            expect(
              (image[i + channel] - image[i + channel - 3]).abs(),
              lessThanOrEqualTo(5),
              reason: '$palette colour step beside the avatar at $x,$y',
            );
          }
        }
      }
      final edges = [
        (_avatar.top - 1, _avatar.top - 2),
        (_avatar.top, _avatar.top + 1),
        (_avatar.bottom - 1, _avatar.bottom - 2),
        (_avatar.bottom, _avatar.bottom + 1),
      ];
      for (final (edge, adjacent) in edges) {
        final start = (edge * _width + _avatar.left) * 3;
        final reference = (adjacent * _width + _avatar.left) * 3;
        final width = (_avatar.right - _avatar.left) * 3;
        expect(
          image.sublist(start, start + width),
          image.sublist(reference, reference + width),
          reason: '$palette retained an old avatar edge at row $edge',
        );
      }
    }
  });

  test(
    'each palette has eight top stripe segments and clean paper below them',
    () {
      for (final palette in _palettes) {
        final image = _rgb(palette);
        // Check all paper rows through the last row before the avatar edge.
        for (var y = 0; y < _avatar.top - 1; y++) {
          for (var x = _bandLeft; x < _width; x++) {
            final i = (y * _width + x) * 3;
            final isWhite =
                image[i] == 255 && image[i + 1] == 255 && image[i + 2] == 255;
            final inStripe =
                y < _stripeHeight &&
                _stripeRuns.any((segment) => x >= segment.$1 && x < segment.$2);
            expect(isWhite, !inStripe, reason: '$palette stripe at $x,$y');
          }
        }
      }
    },
  );
}
