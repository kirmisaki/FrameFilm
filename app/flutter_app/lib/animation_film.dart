import 'dart:typed_data';

/// Joins already oriented single-frame films without changing pixel order.
Uint8List assembleAnimationFilm(List<Uint8List> frames) {
  if (frames.length < 2 || frames.length > 48) {
    throw ArgumentError('动画需要 2 至 48 帧');
  }
  final first = frames.first;
  if (first.length < 33) throw ArgumentError('无效 Film 帧');
  final frameSize = first.length - 32;
  for (final frame in frames) {
    if (frame.length != first.length ||
        !List.generate(6, (i) => i + 4).every((i) => frame[i] == first[i])) {
      throw ArgumentError('动画帧的尺寸和格式必须一致');
    }
    if (ByteData.sublistView(frame).getUint32(0, Endian.little) != frameSize) {
      throw ArgumentError('Film 帧主体长度不匹配');
    }
  }
  final result = Uint8List(32 + frameSize * frames.length);
  result.setRange(0, 32, first);
  final header = ByteData.sublistView(result);
  header.setUint32(0, frameSize * frames.length, Endian.little);
  header.setUint16(10, frames.length, Endian.little);
  for (var i = 0; i < frames.length; i++) {
    result.setRange(
      32 + i * frameSize,
      32 + (i + 1) * frameSize,
      frames[i],
      32,
    );
  }
  return result;
}
