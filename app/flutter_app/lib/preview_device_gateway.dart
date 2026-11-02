import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'device_gateway.dart';

enum PreviewScenario { normal, transferFailure, unconfirmed, sameBuild }

/// In-memory demo: never reads files or invokes platform channels.
class PreviewDeviceGateway implements DeviceGateway {
  PreviewDeviceGateway({this.stepDuration = const Duration(milliseconds: 450)});
  final Duration stepDuration;
  final _events = StreamController<DeviceSnapshot>.broadcast();
  Timer? _timer;
  Timer? _passportTimer;
  Map<String, Object?> _passport = const {};
  String? _savedPassport;
  String? _retryPassport;
  int _passportRevision = 0;
  bool _passportFailed = false;
  bool _disposed = false,
      _connected = false,
      _scanned = false,
      _failedOnce = false;
  ImportedFilm? _film;
  ImportedFirmware? _firmware;
  FilmTransfer _transfer = const FilmTransfer();
  FirmwareDownload _download = const FirmwareDownload();
  bool _downloadFailed = false;
  bool _autoSleep = true, _timedWake = false;
  int _wakeMinutes = 60;
  String _name = 'FRAMEFILMARK-DEMO';
  int? _syncedAt;
  String? _settingsMessage;
  PreviewScenario scenario = PreviewScenario.normal;
  static const warning = '模拟设备 / 演示数据，不会连接或修改真实设备';
  static const _build = 'demo-build-20261008';
  @override
  bool get supported => true;
  @override
  Stream<DeviceSnapshot> get snapshots => _events.stream;

  int _fileDirectory = 0;
  int _activeFileDirectory = 0;
  int? _displayedFile;
  final _deviceFiles = <int, List<String>>{
    0: ['演示画面.film', '风景.film'],
    3: ['演示动画.film'],
  };
  Map<String, Object?> get _fileState => {
    'directory': _fileDirectory == 3 ? 'animation' : 'film',
    'loaded': true,
    'canDelete': _activeFileDirectory == _fileDirectory,
    'storage': {
      'phase': 'ready',
      'totalBytes': 8 * 1024 * 1024 * 1024,
      'freeBytes': 6 * 1024 * 1024 * 1024,
      'filmCount': _deviceFiles[0]!.length,
      'animationCount': _deviceFiles[3]!.length,
      'message': warning,
    },
    'currentId': _activeFileDirectory == _fileDirectory ? _displayedFile : null,
    'message': warning,
    'entries': [
      for (var i = 0; i < _deviceFiles[_fileDirectory]!.length; i++)
        {'id': i, 'name': _deviceFiles[_fileDirectory]![i]},
    ],
  };

  void _emit() {
    if (_disposed) return;
    _events.add(
      DeviceSnapshot(
        files: _connected ? _fileState : const {},
        connected: _connected,
        passport: _passport,
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
    _passportFailed = false;
    scenario = value ?? scenario;
    _connected = false;
    _fileDirectory = 0;
    _activeFileDirectory = 0;
    _displayedFile = null;
    _deviceFiles[0] = ['演示画面.film', '风景.film'];
    _deviceFiles[3] = ['演示动画.film'];
    _scanned = false;
    _failedOnce = false;
    _film = null;
    _firmware = null;
    _transfer = const FilmTransfer();
    _download = const FirmwareDownload();
    _downloadFailed = false;
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
        _activeFileDirectory = 3;
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
            entries[id] != arguments['name']) {
          throw StateError('列表已变化，请刷新');
        }
        if (method == 'deleteDeviceFile') {
          if (_activeFileDirectory != _fileDirectory)
            throw StateError('请先在设备打开对应 app 后删除');
          entries.removeAt(id);
          _displayedFile = null;
        } else {
          _activeFileDirectory = _fileDirectory;
          _displayedFile = id;
        }
      case 'readPassport':
        _startPassport();
      case 'savePassport':
        final json = arguments!['json'] as String;
        final bin = arguments['bin'] as Uint8List;
        if ((bin.length != 33456 && bin.length != 133776) ||
            utf8.encode(json).length > 350000) {
          throw ArgumentError('通行证文件大小无效');
        }
        _retryPassport = json;
        _startPassport(json: json);
      case 'retryPassport':
        if (_retryPassport == null) throw StateError('没有可重试的通行证上传');
        _startPassport(json: _retryPassport);
      case 'cancelPassport':
        _passportTimer?.cancel();
        _passport = {
          ..._passport,
          'phase': 'cancelled',
          'canCancel': false,
          'canRetry': _retryPassport != null,
          'message': '模拟上传已取消并完成清理，草稿保留，可重试上次上传。',
        };
      case 'remoteKey':
        final key = arguments!['key'] as int;
        if (key < 0 || key > 4) throw ArgumentError('无效按键');
        _settingsMessage = '模拟按键已收到；未操作真实设备';
        if (key == 1) _connected = false;
      case 'openDevicePage':
        _settingsMessage = '模拟页面切换已收到；未操作真实设备';
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
        _startDownload();
      case 'cancelFirmwareDownload':
        _timer?.cancel();
        _download = const FirmwareDownload(
          phase: 'cancelled',
          message: '模拟下载已取消，原已导入固件保留，可重新下载',
        );
      case 'scan':
        _scanned = true;
      case 'connect':
        _connected = true;
      case 'disconnect':
        _connected = false;
        _syncedAt = null;
        _settingsMessage = null;
      case 'importGeneratedFilm':
        if (_transfer.active ||
            _transfer.canConfirm ||
            _download.canCancel ||
            (_transfer.canRetry && !_transfer.cleanupCompleted)) {
          throw StateError('请等待当前传输与清理完成');
        }
        final bytes = arguments!['bytes'] as Uint8List;
        if (bytes.length < 32) throw ArgumentError('film 文件头不完整');
        final header = ByteData.sublistView(bytes);
        final format = bytes[9];
        final width = header.getUint16(6, Endian.little);
        final height = header.getUint16(4, Endian.little);
        final frameCount = header.getUint16(10, Endian.little);
        final frameBody = format == 0
            ? width * height ~/ 2
            : format == 1
            ? width * height ~/ 8
            : format == 2 || format == 3
            ? width * height
            : -1;
        final body = frameBody * (frameCount > 1 ? frameCount : 1);
        if (height != 720 ||
            width != 480 ||
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
        );
        _transfer = const FilmTransfer(message: '已接入真实转换结果；后续发送为模拟操作');
      case 'pickFilm':
        _film = const ImportedFilm(
          name: '演示画面.film',
          size: 172832,
          width: 720,
          height: 480,
        );
        _transfer = const FilmTransfer();
      case 'clearFilm':
        _film = null;
        _transfer = const FilmTransfer();
      case 'pickFirmware':
        _firmware = const ImportedFirmware(
          name: '演示固件.bin',
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
        _firmware = null;
        _transfer = const FilmTransfer();
      case 'startTransfer':
        _start(false);
      case 'startFirmwareTransfer':
        _start(true);
      case 'retryTransfer':
        _start(_transfer.kind == 'firmware');
      case 'cancelTransfer':
        _timer?.cancel();
        _transfer = FilmTransfer(
          phase: 'cancelled',
          kind: _transfer.kind,
          message: '演示操作已取消，可重试',
          canRetry: true,
          cleanupCompleted: true,
        );
      case 'confirmFirmwareTransfer':
        _connected = true;
        _transfer = const FilmTransfer(
          phase: 'done',
          kind: 'firmware',
          message: '模拟重连完成，运行构建与目标一致',
          targetBuild: _build,
          success: true,
          cleanupCompleted: true,
        );
      case 'snapshot':
      case 'refresh':
        break;
      default:
        throw UnsupportedError('演示模式未实现：$method');
    }
    _emit();
  }

  void _startDownload() {
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
        _firmware = const ImportedFirmware(
          name: 'frame_film_ark.bin / DEMO',
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

  void _start(bool firmware) {
    _timer?.cancel();
    final total = firmware ? _firmware?.size : _film?.size;
    if (!_connected || total == null) throw StateError('请先连接模拟设备并选择演示文件');
    if (firmware && scenario == PreviewScenario.sameBuild) {
      _transfer = const FilmTransfer(
        phase: 'done',
        kind: 'firmware',
        message: '模拟设备已运行相同构建，跳过升级',
        success: true,
        cleanupCompleted: true,
        targetBuild: _build,
      );
      return;
    }
    var step = 0;
    final phases = firmware
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
          kind: firmware ? 'firmware' : 'film',
          message: '模拟传输中断，临时连接已清理；重试可恢复',
          canRetry: true,
          cleanupCompleted: true,
          total: total,
          received: total ~/ 2,
        );
      } else if (step >= phases.length) {
        _timer?.cancel();
        final pending = firmware && scenario == PreviewScenario.unconfirmed;
        _connected = !pending;
        _transfer = FilmTransfer(
          phase: pending ? 'unconfirmed' : 'done',
          kind: firmware ? 'firmware' : 'film',
          message: pending ? '模拟设备重启后暂未回应，请重连并确认升级' : '演示完成，未传输或修改真实设备',
          total: total,
          received: total,
          success: !pending,
          cleanupCompleted: true,
          canConfirm: pending,
          targetBuild: firmware ? _build : null,
        );
      } else {
        final phase = phases[step];
        _transfer = FilmTransfer(
          phase: phase,
          kind: firmware ? 'firmware' : 'film',
          message: '演示流程：$phase',
          total: total,
          received: step < 2
              ? 0
              : (total * ((step - 1) / 3).clamp(0, 1)).round(),
          canCancel: step < 5,
          targetBuild: firmware ? _build : null,
        );
        step++;
      }
      _emit();
    }

    advance();
    _timer = Timer.periodic(stepDuration, (_) => advance());
  }

  void _startPassport({String? json}) {
    final saving = json != null;
    final oldJson = _passport['json'];
    final total = saving
        ? ((jsonDecode(json) as Map)['avatarMode'] == 'color'
                  ? 133776
                  : 33456) +
              utf8.encode(json).length
        : utf8.encode(_savedPassport ?? '').length;
    _passport = {
      'phase': saving ? 'sending' : 'reading',
      'completed': 0,
      'total': total,
      'canCancel': true,
      'canRetry': false,
      'message': saving ? '模拟 WiFi 上传通行证' : '模拟读取设备资料',
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
