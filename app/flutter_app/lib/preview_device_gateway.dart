import 'frame_abc9.dart';

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'device_gateway.dart';

enum PreviewScenario {
  normal,
  transferFailure,
  unconfirmed,
  sameBuild,
  legacyFirmware,
}

/// In-memory demo: never reads files or invokes platform channels.
class PreviewDeviceGateway implements DeviceGateway {
  PreviewDeviceGateway({
    this.stepDuration = const Duration(milliseconds: 450),
    this.demoHardware = HardwareIdentity.n4r2,
  }) {
    _seedFiles();
  }
  HardwareIdentity demoHardware;
  final Duration stepDuration;
  final _events = StreamController<DeviceSnapshot>.broadcast();
  Timer? _timer;
  Timer? _passportTimer;
  Map<String, Object?> _passport = const {};
  String? _savedPassport;
  String? _retryPassport;
  String _retryPassportTransport = 'wifi';
  int _retryPassportBinLength = 0;
  int _passportRevision = 0;
  bool _passportFailed = false;
  bool _disposed = false,
      _connected = false,
      _scanned = false,
      _failedOnce = false;
  ImportedFilm? _film;
  bool _importedAnimation = false;
  ImportedFirmware? _firmware;
  FilmTransfer _transfer = const FilmTransfer();
  FirmwareDownload _download = const FirmwareDownload();
  bool _downloadFailed = false;
  bool _photoCommitted = false;
  String _runningBuild = _initialBuild;
  int _attempt = 0;
  bool _autoSleep = true, _timedWake = false;
  int _wakeMinutes = 60;
  String _name = 'FRAMEFILMARK-DEMO';
  int? _syncedAt;
  String? _settingsMessage;
  PreviewScenario scenario = PreviewScenario.normal;
  static const warning = '模拟设备 / 演示数据，不会连接或修改真实设备';
  static const _build = 'demo-build-20261008';
  static const _initialBuild = 'demo-build-20261001';
  static const _photoSize = 172832;
  @override
  bool get supported => true;
  @override
  Stream<DeviceSnapshot> get snapshots => _events.stream;

  int _fileDirectory = 0;
  int _activePage = 0;
  int? _displayedFile;
  final _deviceFiles = <int, List<({String name, int size})>>{};
  int get _fileBytes => _deviceFiles.values
      .expand((directory) => directory)
      .fold(0, (total, file) => total + file.size);
  bool get _bleFirmwareAvailable =>
      _connected && scenario != PreviewScenario.legacyFirmware;
  bool get _hasPhotoWifi => scenario != PreviewScenario.legacyFirmware;
  void _seedFiles() {
    _deviceFiles
      ..clear()
      ..addAll({
        0: [
          (name: '演示画面.film', size: _photoSize),
          (name: '风景.film', size: _photoSize),
        ],
        3: [(name: '演示动画.film', size: 345632)],
      });
  }

  Map<String, Object?> get _fileState => {
    'directory': _fileDirectory == 3 ? 'animation' : 'film',
    'loaded': true,
    'canDelete': _activePage == _fileDirectory,
    'storage': {
      'phase': 'ready',
      'totalBytes': 8 * 1024 * 1024 * 1024,
      'freeBytes': 6 * 1024 * 1024 * 1024 - _fileBytes,
      'filmCount': _deviceFiles[0]!.length,
      'animationCount': _deviceFiles[3]!.length,
      'message': warning,
    },
    'currentId': _activePage == _fileDirectory ? _displayedFile : null,
    'message': warning,
    'entries': [
      for (var i = 0; i < _deviceFiles[_fileDirectory]!.length; i++)
        {'id': i, 'name': _deviceFiles[_fileDirectory]![i].name},
    ],
  };

  void _emit() {
    if (_disposed) return;
    _events.add(
      DeviceSnapshot(
        files: _connected ? _fileState : const {},
        connected: _connected,
        hardware: _connected && scenario != PreviewScenario.legacyFirmware
            ? demoHardware
            : null,
        deviceId: _connected ? 'DEMO-ARK' : null,
        hardwareIdentitySupported:
            _connected && scenario != PreviewScenario.legacyFirmware,
        bleFirmwareAvailable: _bleFirmwareAvailable,
        effectivePhotoTransport: _hasPhotoWifi ? 'wifi' : 'ble',
        effectiveFirmwareTransport: 'wifi',
        transferTransportReason: _hasPhotoWifi
            ? '模拟运行构建支持 Wi-Fi 照片传输'
            : '模拟旧版运行构建仅支持普通照片蓝牙兼容传输',
        presentKeys: const {
          'bleFirmwareAvailable',
          'effectivePhotoTransport',
          'effectiveFirmwareTransport',
          'transferTransportReason',
          'settingsMessage',
          'syncedAt',
        },
        passport: {
          ..._passport,
          'wifiAvailable': _connected,
          'bleAvailable':
              _connected && scenario != PreviewScenario.legacyFirmware,
        },
        hasPassportState: true,
        message: warning,
        devices: _scanned && !_connected
            ? const [
                DeviceInfo(address: 'DEMO-ARK', name: 'FRAMEFILMARK / 模拟设备'),
              ]
            : const [],
        name: _connected ? _name : null,
        autoSleep: _connected ? _autoSleep : null,
        timedWake: _connected ? _timedWake : null,
        wakeMinutes: _connected ? _wakeMinutes : null,
        syncedAt: _connected ? _syncedAt : null,
        settingsMessage: _connected ? _settingsMessage : null,
        abc9PreviewSupported: _connected,
        studentPassportSupported: _connected,
        battery: _connected ? 86 : null,
        width: _connected ? 720 : null,
        height: _connected ? 480 : null,
        importedFile: _film,
        importedFirmware: _firmware,
        transfer: _transfer,
        firmwareDownload: _download,
        hasDownloadState: true,
        importing: _download.canCancel,
        hasFilmState: true,
        hasFirmwareState: true,
        hasTransferState: true,
        hasImportingState: true,
      ),
    );
  }

  void reset([PreviewScenario? value]) {
    _timer?.cancel();
    _passportTimer?.cancel();
    _passport = const {};
    _savedPassport = null;
    _retryPassport = null;
    _retryPassportTransport = 'wifi';
    _retryPassportBinLength = 0;
    _passportRevision = 0;
    _passportFailed = false;
    scenario = value ?? scenario;
    _connected = false;
    _fileDirectory = 0;
    _activePage = 0;
    _displayedFile = null;
    _seedFiles();
    _scanned = false;
    _failedOnce = false;
    _film = null;
    _importedAnimation = false;
    _firmware = null;
    _transfer = const FilmTransfer();
    _download = const FirmwareDownload();
    _downloadFailed = false;
    _photoCommitted = false;
    _runningBuild = scenario == PreviewScenario.sameBuild
        ? _build
        : _initialBuild;
    _autoSleep = true;
    _timedWake = false;
    _wakeMinutes = 60;
    _name = 'FRAMEFILMARK-DEMO';
    _syncedAt = null;
    _settingsMessage = null;
    _emit();
  }

  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    if (_disposed) return;
    if (_passport['canCancel'] == true &&
        method != 'cancelPassport' &&
        method != 'snapshot') {
      throw StateError('通行证操作进行中，请等待或取消');
    }
    if (deviceSettingsMethods.contains(method) &&
        (!_connected ||
            _transfer.active ||
            _transfer.canConfirm ||
            _download.canCancel ||
            (_transfer.canRetry && !_transfer.cleanupCompleted))) {
      throw StateError('请先连接模拟设备并等待当前操作完成');
    }
    switch (method) {
      case 'playAnimation':
        final interval = arguments!['intervalMs'] as int;
        final loop = arguments['loopSeconds'] as int;
        final mode = arguments['playMode'] as int? ?? 0;
        if (mode != 0 && mode != 1) throw ArgumentError('不支持的动画播放模式');
        if (interval < 100 || interval > 2000 || loop < 0 || loop > 600) {
          throw ArgumentError('动画播放参数超出范围');
        }
        _fileDirectory = 3;
        _activePage = 3;
        _displayedFile = null;
        _settingsMessage = '模拟动画播放设置已保存；未操作真实设备';
      case 'refreshDeviceStorage':
      case 'listDeviceFiles':
        break;
      case 'setDeviceFileDirectory':
        final appId = arguments!['appId'] as int;
        if (appId != 0 && appId != 3) {
          throw ArgumentError('不支持的目录');
        }
        _fileDirectory = appId;
        _settingsMessage = '只读浏览模拟目录；未操作真实设备';
      case 'displayDeviceFile':
      case 'deleteDeviceFile':
        final id = arguments!['id'] as int;
        final entries = _deviceFiles[_fileDirectory]!;
        if (id < 0 ||
            id >= entries.length ||
            entries[id].name != arguments['name']) {
          throw StateError('列表已变化，请刷新');
        }
        if (method == 'deleteDeviceFile') {
          if (_activePage != _fileDirectory) {
            throw StateError('请先在设备打开对应 app 后删除');
          }
          entries.removeAt(id);
          _displayedFile = null;
        } else {
          _activePage = _fileDirectory;
          _displayedFile = id;
        }
      case 'readPassport':
        _startPassport();
      case 'savePassport':
        final json = arguments!['json'] as String;
        final bin = arguments['bin'] as Uint8List;
        final transport = arguments['transportPreference'] == 'ble'
            ? 'ble'
            : 'wifi';
        if (transport == 'ble' && scenario == PreviewScenario.legacyFirmware) {
          throw StateError('当前模拟固件不支持通行证蓝牙发送');
        }
        if (utf8.encode(json).length > 350000) {
          throw ArgumentError('通行证文件大小无效');
        }
        if (_isStudentPassport(json)) {
          validateAbc9Film(bin);
        } else if (bin.length != 33456 && bin.length != 133776) {
          throw ArgumentError('通行证文件大小无效');
        }
        _retryPassport = json;
        _retryPassportBinLength = bin.length;
        _retryPassportTransport = transport;
        _startPassport(json: json, binLength: bin.length, transport: transport);
      case 'retryPassport':
        if (!_connected ||
            _passport['canRetry'] != true ||
            _retryPassport == null) {
          throw StateError('请先重连模拟设备并确认有可重试的通行证上传');
        }
        _startPassport(
          json: _retryPassport,
          binLength: _retryPassportBinLength,
          transport: _retryPassportTransport,
        );
      case 'cancelPassport':
        if (_passport['canCancel'] != true) return;
        _passportTimer?.cancel();
        final saving = _passport['phase'] != 'reading';
        _passport = {
          ..._passport,
          'phase': 'cancelled',
          'canCancel': false,
          'canRetry': saving,
          'message': saving ? '模拟上传已取消并完成清理，草稿保留，可重试上次上传。' : '模拟读取已取消，草稿保留。',
        };
      case 'remoteKey':
        final key = arguments!['key'] as int;
        if (key < 0 || key > 4) throw ArgumentError('无效按键');
        _settingsMessage = '模拟按键已收到；未操作真实设备';
        if (key == 1) _connected = false;
        if (key == 4) {
          _activePage = 5;
          _displayedFile = null;
        }
      case 'openDevicePage':
        final appId = arguments?['appId'];
        const pages = {0: '图片', 3: '动画', 4: '系统设置', 5: '主菜单', 6: '通行证'};
        if (!pages.containsKey(appId)) throw ArgumentError('无效设备页面');
        _activePage = appId as int;
        _displayedFile = null;
        _settingsMessage = '已打开模拟设备的${pages[appId]}页面';
      case 'setAutoSleep':
        _autoSleep = arguments!['enabled'] as bool;
        _settingsMessage = '模拟保存并回读确认';
      case 'setTimedWake':
        _timedWake = arguments!['enabled'] as bool;
        _settingsMessage = '模拟保存并回读确认';
      case 'setWakeMinutes':
        final minutes = arguments!['minutes'] as int;
        if (minutes < 10 || minutes > 2880) {
          throw ArgumentError('唤醒间隔必须为 10–2880 分钟');
        }
        _wakeMinutes = minutes;
        _settingsMessage = '模拟保存并回读确认';
      case 'renameDevice':
        final suffix = arguments!['suffix'] as String;
        final error = validateDeviceSuffix(suffix);
        if (error != null) throw ArgumentError(error);
        _name = 'FRAMEFILMARK-$suffix';
        _settingsMessage = '模拟已保存，真实设备需重启后广播名生效';
      case 'syncTime':
        _syncedAt = DateTime.now().millisecondsSinceEpoch;
        _settingsMessage = '模拟时间同步完成';
      case 'downloadFirmware':
        _startDownload(arguments);
      case 'cancelFirmwareDownload':
        if (!_download.canCancel) return;
        _timer?.cancel();
        _download = const FirmwareDownload(
          phase: 'cancelled',
          message: '模拟下载已取消，原已导入固件保留，可重新下载',
        );
      case 'scan':
        _scanned = true;
      case 'connect':
        final address = arguments?['address'];
        if (address != null && address != 'DEMO-ARK') {
          throw StateError('请选择模拟设备');
        }
        _connected = true;
      case 'disconnect':
        _connected = false;
        _syncedAt = null;
        _settingsMessage = null;
      case 'importGeneratedFilm':
        _requireIdleTransfer();
        final bytes = arguments!['bytes'] as Uint8List;
        if (bytes.length < 32) throw ArgumentError('film 文件头不完整');
        final header = ByteData.sublistView(bytes);
        final format = bytes[9];
        final colors = bytes[8];
        final width = header.getUint16(4, Endian.little);
        final height = header.getUint16(6, Endian.little);
        final frameCount = header.getUint16(10, Endian.little);
        final abc = isAbc9Film(bytes);
        if (abc) validateAbc9Film(bytes);
        final frameBody = abc
            ? width * height * 2
            : format == 0
            ? width * height ~/ 2
            : format == 1
            ? width * height ~/ 8
            : format == 2 || format == 3
            ? width * height
            : -1;
        final body = frameBody * (frameCount > 1 ? frameCount : 1);
        if (width != 720 ||
            height != 480 ||
            (format == 0 && (colors < 2 || colors > 6)) ||
            frameBody < 0 ||
            header.getUint32(0, Endian.little) != body ||
            bytes.length != body + 32) {
          throw ArgumentError('生成的 film 格式无效');
        }
        _film = ImportedFilm(
          name: arguments['name'] as String,
          size: bytes.length,
          width: 720,
          height: 480,
          blePhotoCompatible:
              scenario != PreviewScenario.legacyFirmware ||
              (!abc &&
                  frameCount <= 1 &&
                  ((format == 0 && (colors == 2 || colors == 6)) ||
                      (format == 1 && colors == 2))),
          mediaDirectory: frameCount > 1 ? 'animation' : 'film',
        );
        _importedAnimation = frameCount > 1;
        _transfer = const FilmTransfer(message: '已接入真实转换结果；后续发送为模拟操作');
      case 'pickFilm':
        _requireIdleTransfer();
        _film = const ImportedFilm(
          name: '演示上传.film',
          size: _photoSize,
          width: 720,
          height: 480,
          blePhotoCompatible: true,
          mediaDirectory: 'film',
        );
        _importedAnimation = false;
        _transfer = const FilmTransfer();
      case 'clearFilm':
        _requireIdleTransfer();
        _film = null;
        _importedAnimation = false;
        _transfer = const FilmTransfer();
      case 'pickFirmware':
      case 'importFirmware':
        _requireIdleTransfer();
        _firmware = ImportedFirmware(
          name: '演示固件.bin',
          targetId: demoHardware.targetId,
          chipId: demoHardware.chipId,
          flashBytes: demoHardware.flashBytes,
          psramBytes: demoHardware.psramBytes,
          psramBus: demoHardware.psramBus,
          size: 1048576,
          version: 'demo-1.0',
          project: 'frame_film_ark',
          elfSha256: _build,
          fileSha256: 'demo-file-fingerprint',
        );
        _transfer = const FilmTransfer(
          kind: 'firmware',
          message: '已选择演示固件元信息，未读取真实文件',
        );
      case 'clearFirmware':
        _requireIdleTransfer();
        _firmware = null;
        _transfer = const FilmTransfer();
      case 'startTransfer':
        _start(false, arguments);
      case 'startFirmwareTransfer':
        _start(true, arguments);
      case 'retryTransfer':
        if (!_transfer.canRetry || !_transfer.cleanupCompleted) return;
        _start(
          _transfer.kind == 'firmware',
          arguments,
          transport: _transfer.transport,
        );
      case 'cancelTransfer':
        if (!_transfer.canCancel) return;
        _timer?.cancel();
        final cancelled = _transfer;
        _transfer = FilmTransfer(
          phase: 'cancelled',
          kind: cancelled.kind,
          message: '演示操作已取消，可重试',
          transport: cancelled.transport,
          total: cancelled.total,
          received: cancelled.received,
          targetBuild: cancelled.targetBuild,
          targetVersion: cancelled.targetVersion,
          attemptId: cancelled.attemptId,
          canRetry: true,
          cleanupCompleted: true,
        );
      case 'confirmFirmwareTransfer':
      case 'confirmTransfer':
        _confirmTransfer();
      case 'dismissPhotoConfirmation':
        if (!_transfer.canConfirm || _transfer.kind != 'film') return;
        _transfer = FilmTransfer(
          phase: 'cancelled',
          message: '已关闭模拟照片确认；保留 SD 卡中的文件',
          transport: 'ble',
          cleanupCompleted: true,
        );
      case 'acknowledgeBleFirmwareRestart':
        throw StateError('当前模拟升级没有待处理的旧版本手动重启记录');
      case 'snapshot':
      case 'refresh':
        break;
      default:
        throw UnsupportedError('演示模式未实现：$method');
    }
    _emit();
  }

  void _startDownload(Map<String, Object?>? arguments) {
    _requireIdleTransfer();
    if (!_connected) throw StateError('请先连接模拟设备');
    final target = HardwareIdentity.target(
      arguments?['targetId'] as String? ?? '',
    );
    if (target == null ||
        !target.matches(demoHardware) ||
        !target.matchesPackage(arguments ?? const {}) ||
        (scenario == PreviewScenario.legacyFirmware &&
            arguments?['legacyHardwareTarget'] != target.targetId)) {
      throw StateError('演示固件目标无效');
    }
    _timer?.cancel();
    var step = 0;
    void advance() {
      step++;
      if (step == 2 &&
          scenario == PreviewScenario.transferFailure &&
          !_downloadFailed) {
        _downloadFailed = true;
        _timer?.cancel();
        _download = const FirmwareDownload(
          phase: 'error',
          message: '模拟下载失败，原固件保留；重新下载可恢复',
        );
      } else if (step >= 5) {
        _timer?.cancel();
        _firmware = ImportedFirmware(
          name: '${target.targetId} / DEMO',
          targetId: target.targetId,
          chipId: target.chipId,
          flashBytes: target.flashBytes,
          psramBytes: target.psramBytes,
          psramBus: target.psramBus,
          size: 1048576,
          version: '3.2.5 / DEMO',
          project: 'frame_film_ark',
          elfSha256: _build,
          fileSha256: 'demo-file-fingerprint',
        );
        _transfer = const FilmTransfer(
          kind: 'firmware',
          message: '演示固件已导入，未执行升级',
        );
        _download = const FirmwareDownload(
          phase: 'done',
          received: 1048576,
          total: 1048576,
          message: '模拟下载与校验完成，仅导入演示固件',
        );
      } else {
        _download = FirmwareDownload(
          phase: step == 4 ? 'validating' : 'downloading',
          received: 1048576 * step ~/ 4,
          total: 1048576,
          canCancel: true,
          message: step == 4 ? '模拟校验镜像…' : '模拟下载中…',
        );
      }
      _emit();
    }

    advance();
    _timer = Timer.periodic(stepDuration, (_) => advance());
  }

  void _requireIdleTransfer() {
    if (_transfer.active || _transfer.canConfirm || _download.canCancel) {
      throw StateError('请等待当前模拟操作结束');
    }
  }

  void _saveFilmOnDevice(ImportedFilm film, int directory) {
    final files = _deviceFiles[directory]!;
    final existing = files.indexWhere((entry) => entry.name == film.name);
    final entry = (name: film.name, size: film.size);
    if (existing < 0) {
      files.add(entry);
    } else {
      files[existing] = entry;
    }
  }

  void _confirmTransfer() {
    if (!_transfer.canConfirm) return;
    final pending = _transfer;
    final firmware = pending.kind == 'firmware';
    if (!firmware && !_connected) {
      throw StateError('请先重新连接模拟设备，再核对 SD 目录');
    }
    final confirmed = firmware
        ? pending.targetBuild == _runningBuild
        : _photoCommitted &&
              _deviceFiles[0]!.any((file) => file.name == _film?.name);
    if (!confirmed) throw StateError('模拟设备尚未确认本次传输');
    _connected = true;
    _transfer = FilmTransfer(
      phase: 'done',
      kind: pending.kind,
      transport: pending.transport,
      targetVersion: pending.targetVersion,
      targetBuild: pending.targetBuild,
      attemptId: pending.attemptId,
      received: pending.received,
      total: pending.total,
      confirmation: firmware ? 'elf_sha256' : 'directory_presence',
      message: firmware ? '模拟重连完成，运行构建与目标一致' : '已从模拟 SD 目录确认照片',
      success: true,
      cleanupCompleted: true,
    );
  }

  void _start(
    bool firmware,
    Map<String, Object?>? arguments, {
    String? transport,
  }) {
    _requireIdleTransfer();
    final preference = arguments?['transportPreference'];
    if (preference != null &&
        !const {'auto', 'wifi', 'ble'}.contains(preference)) {
      throw ArgumentError('无效传输模式');
    }
    final link =
        transport ??
        (preference == 'ble' ||
                (!firmware && preference == 'auto' && !_hasPhotoWifi)
            ? 'ble'
            : 'wifi');
    final ble = link == 'ble';
    final image = _firmware;
    final film = _film;
    final total = firmware ? image?.size : film?.size;
    if (!_connected || total == null) {
      throw StateError('请先连接模拟设备并选择演示文件');
    }
    if (firmware) {
      if (ble && !_bleFirmwareAvailable) {
        throw StateError('模拟运行构建未提供可验证 BLE OTA 能力');
      }
      final target = scenario == PreviewScenario.legacyFirmware
          ? HardwareIdentity.target(
              arguments?['legacyHardwareTarget'] as String? ?? '',
            )
          : demoHardware;
      if (target == null ||
          !target.matches(demoHardware) ||
          image?.matchesHardware(target) != true) {
        throw StateError('演示升级需要本次确认与匹配的固件目标');
      }
    } else if (ble) {
      if (film?.blePhotoCompatible != true) {
        throw StateError('当前模拟固件不支持这个文件的蓝牙传输');
      }
      if (_deviceFiles[_importedAnimation ? 3 : 0]!.any(
        (file) => file.name == film!.name,
      )) {
        throw StateError('模拟设备已有同名文件，请删除或改名后再发送');
      }
    }
    _photoCommitted = false;
    _timer?.cancel();
    final attempt = ++_attempt;
    final kind = firmware ? 'firmware' : 'film';
    final targetBuild = firmware ? image!.elfSha256 : null;
    final targetVersion = firmware ? image!.version : null;
    if (firmware && targetBuild == _runningBuild) {
      _transfer = FilmTransfer(
        phase: 'done',
        kind: kind,
        message: '模拟设备已运行相同构建，跳过升级',
        success: true,
        cleanupCompleted: true,
        targetBuild: targetBuild,
        targetVersion: targetVersion,
        transport: link,
        attemptId: attempt,
        confirmation: 'elf_sha256',
      );
      return;
    }
    var step = 0;
    final phases = firmware && ble
        ? [
            'validating',
            'preparing',
            'sending',
            'sending',
            'verifying',
            'applying',
            'rebooting',
            'confirming',
          ]
        : firmware
        ? [
            'validating',
            'connecting',
            'downloading',
            'downloading',
            'restoring',
            'applying',
            'rebooting',
            'confirming',
          ]
        : ble
        ? ['preparing', 'sending', 'sending', 'sending', 'verifying']
        : [
            'preparing',
            'connecting',
            'downloading',
            'downloading',
            'restoring',
          ];
    void advance() {
      if (step == 3 &&
          scenario == PreviewScenario.transferFailure &&
          !_failedOnce) {
        _failedOnce = true;
        _timer?.cancel();
        _transfer = FilmTransfer(
          phase: 'error',
          kind: kind,
          message: ble ? '模拟蓝牙发送中断；重试可恢复' : '模拟传输中断，临时连接已清理；重试可恢复',
          transport: link,
          canRetry: true,
          cleanupCompleted: true,
          total: total,
          received: total ~/ 2,
          attemptId: attempt,
          targetVersion: targetVersion,
          targetBuild: targetBuild,
        );
      } else if (step >= phases.length) {
        _timer?.cancel();
        final pending =
            (firmware || ble) && scenario == PreviewScenario.unconfirmed;
        if (firmware) {
          _runningBuild = targetBuild!;
        } else {
          // Multiple frames are stored with animations by the simulated device.
          _saveFilmOnDevice(film!, _importedAnimation ? 3 : 0);
          _photoCommitted = true;
        }
        _connected = !pending;
        _transfer = FilmTransfer(
          phase: pending ? 'unconfirmed' : 'done',
          kind: kind,
          message: pending
              ? firmware
                    ? '模拟设备重启后暂未回应，请重连并确认升级'
                    : '模拟蓝牙断开，请重新连接并核对 SD 目录'
              : '演示完成，未传输或修改真实设备',
          total: total,
          received: total,
          success: !pending,
          transport: link,
          confirmation: pending
              ? 'pending'
              : firmware
              ? 'elf_sha256'
              : ble
              ? 'directory_presence'
              : null,
          cleanupCompleted: true,
          canConfirm: pending,
          targetBuild: targetBuild,
          targetVersion: targetVersion,
          attemptId: attempt,
        );
      } else {
        final phase = phases[step];
        final progress = switch (phase) {
          'validating' => '核对模拟固件与设备',
          'preparing' => '准备模拟${ble ? '蓝牙' : 'Wi-Fi'}传输',
          'connecting' => '建立模拟 Wi-Fi 临时连接',
          'downloading' => '模拟设备正在接收文件',
          'sending' => '通过模拟蓝牙发送数据',
          'restoring' => '恢复模拟网络连接',
          'verifying' => firmware ? '核对模拟固件完整性' : '核对模拟 SD 目录',
          'applying' => '模拟设备正在应用固件',
          'rebooting' => '模拟设备正在重启',
          'confirming' => '核对模拟设备运行构建',
          _ => '模拟传输进行中',
        };
        _transfer = FilmTransfer(
          phase: phase,
          kind: kind,
          message: progress,
          transport: link,
          total: total,
          received: step < 2
              ? 0
              : (total * ((step - 1) / 3).clamp(0, 1)).round(),
          canCancel: firmware
              ? !const {'applying', 'rebooting', 'confirming'}.contains(phase)
              : phase != 'verifying',
          targetBuild: targetBuild,
          targetVersion: targetVersion,
          attemptId: attempt,
        );
        step++;
      }
      _emit();
    }

    advance();
    _timer = Timer.periodic(stepDuration, (_) => advance());
  }

  bool _isStudentPassport(String json) {
    final data = jsonDecode(json);
    return data is Map && data['version'] == 2;
  }

  void _startPassport({
    String? json,
    int binLength = 0,
    String transport = 'ble',
  }) {
    final saving = json != null;
    final retry = _retryPassport;
    final oldJson = _passport['json'];
    final total = saving
        ? binLength + utf8.encode(json).length
        : utf8.encode(_savedPassport ?? '').length;
    _passport = {
      'phase': saving ? 'sending' : 'reading',
      'transport': transport,
      if (retry != null)
        'kind': _isStudentPassport(retry) ? 'student' : 'operator',
      'completed': 0,
      'total': total,
      'canCancel': true,
      'canRetry': false,
      'message': saving
          ? (transport == 'ble' ? '模拟蓝牙上传通行证' : '模拟 Wi-Fi 上传通行证')
          : '模拟读取设备资料',
      'json': oldJson,
      'revision': _passportRevision,
    };
    var step = 0;
    _passportTimer?.cancel();
    _passportTimer = Timer.periodic(stepDuration, (timer) {
      if (_disposed) {
        timer.cancel();
        return;
      }
      step++;
      if (step == 2 &&
          scenario == PreviewScenario.transferFailure &&
          !_passportFailed) {
        _passportFailed = true;
        timer.cancel();
        _passport = {
          ..._passport,
          'phase': 'error',
          'canCancel': false,
          'canRetry': saving,
          'message': '模拟连接中断，可能只发送了部分文件；草稿保留，重新连接后从头发送。',
        };
        _connected = false;
      } else if (step >= 4) {
        timer.cancel();
        if (saving) _savedPassport = json;
        final document = _savedPassport;
        if (document == null) {
          _passport = {
            ..._passport,
            'phase': 'error',
            'canCancel': false,
            'message': '模拟设备尚未配置资料，当前草稿保留。',
          };
        } else {
          _passportRevision++;
          _passport = {
            'phase': 'done',
            'transport': transport,
            'completed': total,
            'total': total,
            'canCancel': false,
            'json': document,
            'revision': _passportRevision,
            'message': saving ? '模拟发送及资料回读完成；未修改真实设备。' : '模拟资料读取完成。',
          };
        }
      } else {
        _passport = {
          ..._passport,
          'completed': (total * step / 4).round(),
          'phase': saving && step == 3
              ? 'verifying'
              : saving
              ? 'sending'
              : 'reading',
        };
      }
      _emit();
    });
  }

  void dispose() {
    _disposed = true;
    _timer?.cancel();
    _passportTimer?.cancel();
    unawaited(_events.close());
  }
}
