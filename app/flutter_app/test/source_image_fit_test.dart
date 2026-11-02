import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:framefilm_ark/source_image_fit.dart';

void main() {
  const nominal = SourceFitProfile.nominal(.04, .64);
  const measured = SourceFitProfile.measured([40, 48, 56], [200, 208, 216]);
  List<int> run(List<int> rgb, double r, double w, SourceFitProfile profile) {
    final bytes = Uint8List.fromList([...rgb, 73]);
    fitSourceRgba(bytes, rangeFit: r, warmChroma: w, profile: profile);
    expect(bytes[3], 73);
    return bytes.sublist(0, 3);
  }

  test('disabled is exact and requires no measured endpoints', () {
    final bytes = Uint8List.fromList([1, 127, 255, 49]);
    fitSourceRgba(bytes);
    expect(bytes, [1, 127, 255, 49]);
  });
  test('nominal mathematical fixtures are not device measurements', () {
    expect(run([128, 128, 128], 1, 0, nominal), [156, 156, 156]);
    expect(run([128, 128, 128], .5, .5, nominal), [143, 143, 143]);
    expect(run([230, 175, 105], 1, 0, nominal), [243, 193, 136]);
    expect(run([230, 175, 105], 1, 1, nominal), [202, 202, 202]);
    expect(run([230, 175, 105], 0, 1, nominal), [185, 185, 185]);
    expect(run([65, 120, 235], 1, 0, nominal), [114, 149, 249]);
    expect(run([65, 120, 235], 1, 1, nominal), [114, 149, 249]);
  });
  test('measured fixture endpoints and warm neutral axis', () {
    expect(run([0, 0, 0], 1, 0, measured), [40, 48, 56]);
    expect(run([255, 255, 255], 1, 0, measured), [200, 208, 216]);
    expect(run([128, 128, 128], 1, 0, measured), [122, 129, 135]);
    expect(run([230, 175, 105], 1, 0, measured), [191, 158, 125]);
    expect(run([230, 175, 105], 1, 1, measured), [158, 165, 172]);
  });
}
