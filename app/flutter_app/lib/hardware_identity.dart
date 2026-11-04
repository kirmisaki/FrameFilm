/// Device-reported identity. Screen size, advertisement and display version
/// deliberately play no part in firmware compatibility.
class HardwareIdentity {
  const HardwareIdentity({
    required this.schemaVersion,
    required this.chipId,
    required this.chipRevision,
    required this.flashBytes,
    required this.psramBytes,
    required this.psramBus,
    required this.targetId,
  });
  factory HardwareIdentity.fromMap(Map data) => HardwareIdentity(
    schemaVersion: _integer(data['schemaVersion']),
    chipId: _integer(data['chipId']),
    chipRevision: _integer(data['chipRevision']),
    flashBytes: _integer(data['flashBytes']),
    psramBytes: _integer(data['psramBytes']),
    psramBus: data['psramBus'] is String ? data['psramBus'] as String : '',
    targetId: data['targetId'] is String ? data['targetId'] as String : '',
  );
  static int _integer(Object? value) => value is int ? value : -1;
  final int schemaVersion, chipId, chipRevision, flashBytes, psramBytes;
  final String psramBus, targetId;
  static const n4r2 = HardwareIdentity(
    schemaVersion: 1,
    chipId: 9,
    chipRevision: 0,
    flashBytes: 4194304,
    psramBytes: 2097152,
    psramBus: 'quad',
    targetId: 'ark-esp32s3-n4r2',
  );
  static const n16r8 = HardwareIdentity(
    schemaVersion: 1,
    chipId: 9,
    chipRevision: 0,
    flashBytes: 16777216,
    psramBytes: 8388608,
    psramBus: 'octal',
    targetId: 'ark-esp32s3-n16r8',
  );
  static HardwareIdentity? target(String id) => switch (id) {
    'ark-esp32s3-n4r2' => n4r2,
    'ark-esp32s3-n16r8' => n16r8,
    _ => null,
  };
  bool get trusted {
    final expected = target(targetId);
    return schemaVersion == 1 &&
        chipId == 9 &&
        chipRevision >= 0 &&
        expected != null &&
        matches(expected);
  }

  bool matches(HardwareIdentity other) =>
      targetId == other.targetId &&
      chipId == other.chipId &&
      flashBytes == other.flashBytes &&
      psramBytes == other.psramBytes &&
      psramBus == other.psramBus;
  bool matchesPackage(Map data) =>
      data['targetId'] == targetId &&
      data['chipId'] == chipId &&
      data['flashBytes'] == flashBytes &&
      data['psramBytes'] == psramBytes &&
      data['psramBus'] == psramBus;
  Map<String, Object?> get packageMetadata => {
    'targetId': targetId,
    'chipId': chipId,
    'flashBytes': flashBytes,
    'psramBytes': psramBytes,
    'psramBus': psramBus,
  };
  Map<String, Object?> toMap() => {
    'schemaVersion': schemaVersion,
    'chipRevision': chipRevision,
    ...packageMetadata,
  };
  String get label => switch (targetId) {
    'ark-esp32s3-n4r2' => 'Ark N4R2',
    'ark-esp32s3-n16r8' => 'Ark N16R8',
    _ => '未识别型号',
  };
  static const unavailableMessage =
      '无法确认硬件型号。请重连并读取设备信息；旧固件可在设置页确认实物配置后首次升级，其他功能可继续使用。';
}
