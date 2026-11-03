import 'dart:typed_data';

bool isAbc9Film(List<int> bytes) => bytes.length >= 10 && bytes[9] == 0x7f;

/// Experimental single frame, native 720x480, uint16 LE ABC tuple.
void validateAbc9Film(Uint8List bytes) {
  if (bytes.length != 32 + 720 * 480 * 2) {
    throw const FormatException('ABC9 文件长度无效');
  }
  final h = ByteData.sublistView(bytes);
  if (h.getUint32(0, Endian.little) != 720 * 480 * 2 ||
      h.getUint16(4, Endian.little) != 720 ||
      h.getUint16(6, Endian.little) != 480 ||
      bytes[8] != 0 ||
      bytes[9] != 0x7f ||
      h.getUint16(10, Endian.little) != 1 ||
      bytes[12] != 65 ||
      bytes[13] != 66 ||
      bytes[14] != 67 ||
      bytes[15] != 57) {
    throw const FormatException('ABC9 仅支持单帧 720×480 实验格式');
  }
  for (var offset = 32; offset < bytes.length; offset += 2) {
    if (h.getUint16(offset, Endian.little) > 511) {
      throw const FormatException('ABC9 代码须为 0–511');
    }
  }
}
