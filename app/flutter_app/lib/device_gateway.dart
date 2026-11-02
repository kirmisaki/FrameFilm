import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

String? validateDeviceSuffix(String suffix) {
  final bytes = utf8.encode(suffix);
  if (bytes.isEmpty || bytes.length > 16) {
    return '后缀需为 1–16 UTF-8 字节（中文通常每字 3 字节）';
  }
  if (suffix.runes.any(
    (c) => c < 0x20 || (c >= 0x7f && c <= 0x9f) || (c >= 0xd800 && c <= 0xdfff),
  )) {
    return '后缀不能包含控制字符';
  }
  return null;
}

const deviceSettingsMethods = {
  'refresh',
  'playAnimation',
  'listDeviceFiles',
  'refreshDeviceStorage',
  'displayDeviceFile',
  'deleteDeviceFile',
  'setDeviceFileDirectory',
  'setAutoSleep',
  'setTimedWake',
  'setWakeMinutes',
  'renameDevice',
  'syncTime',
  'remoteKey',
  'openDevicePage',
  'readPassport',
  'savePassport',
};

class DeviceInfo {
  const DeviceInfo({required this.address, required this.name});
  final String address;
  final String name;
}

class ImportedFilm {
  const ImportedFilm({
    required this.name,
    required this.size,
    this.width,
    this.height,
  });
  factory ImportedFilm.fromMap(Map data) => ImportedFilm(
    name: data['name'] as String? ?? 'film',
    size: (data['size'] as num?)?.toInt() ?? 0,
    width: (data['width'] as num?)?.toInt(),
    height: (data['height'] as num?)?.toInt(),
  );
  final String name;
  final int size;
  final int? width;
  final int? height;
}

class ImportedFirmware {
  const ImportedFirmware({
    required this.name,
    required this.size,
    required this.version,
    required this.project,
    required this.elfSha256,
    required this.fileSha256,
  });
  factory ImportedFirmware.fromMap(Map data) => ImportedFirmware(
    name: data['name'] as String? ?? 'firmware.bin',
    size: (data['size'] as num?)?.toInt() ?? 0,
    version: data['version'] as String? ?? '',
    project: data['project'] as String? ?? '',
    elfSha256: data['elfSha256'] as String? ?? '',
    fileSha256: data['fileSha256'] as String? ?? '',
  );
  final String name;
  final int size;
  final String version;
  final String project;
  final String elfSha256;
  final String fileSha256;
}

class FilmTransfer {
  const FilmTransfer({
    this.phase = 'idle',
    this.message = '选择 film 文件后开始直传',
    this.received = 0,
    this.total = 0,
    this.canCancel = false,
    this.canRetry = false,
    this.cleanupCompleted = false,
    this.success = false,
    this.kind = 'film',
    this.targetVersion,
    this.targetBuild,
    this.canConfirm = false,
  });
  factory FilmTransfer.fromMap(Map data) => FilmTransfer(
    phase: data['phase'] as String? ?? 'idle',
    message: data['message'] as String? ?? '',
    received: (data['received'] as num?)?.toInt() ?? 0,
    total: (data['total'] as num?)?.toInt() ?? 0,
    canCancel: data['canCancel'] == true,
    canRetry: data['canRetry'] == true,
    cleanupCompleted: data['cleanupCompleted'] == true,
    success: data['success'] == true,
    kind: data['kind'] as String? ?? 'film',
    targetVersion: data['targetVersion'] as String?,
    targetBuild: data['targetBuild'] as String?,
    canConfirm: data['canConfirm'] == true,
  );
  final String phase;
  final String message;
  final int received;
  final int total;
  final bool canCancel;
  final bool canRetry;
  final bool cleanupCompleted;
  final bool success;
  final String kind;
  final String? targetVersion;
  final String? targetBuild;
  final bool canConfirm;
  bool get active =>
      canCancel ||
      const {
        'preparing',
        'connecting',
        'downloading',
        'restoring',
        'cancelling',
        'cleanup',
        'validating',
        'ready',
        'applying',
        'rebooting',
        'confirming',
      }.contains(phase);
  double? get progress => total > 0 ? (received / total).clamp(0.0, 1.0) : null;
}

class FirmwareDownload {
  const FirmwareDownload({
    this.phase = 'idle',
    this.received = 0,
    this.total = 0,
    this.message = '',
    this.canCancel = false,
  });
  factory FirmwareDownload.fromMap(Map d) => FirmwareDownload(
    phase: d['phase'] as String? ?? 'idle',
    received: (d['received'] as num?)?.toInt() ?? 0,
    total: (d['total'] as num?)?.toInt() ?? 0,
    message: d['message'] as String? ?? '',
    canCancel: d['canCancel'] == true,
  );
  final String phase, message;
  final int received, total;
  final bool canCancel;
  double? get progress => total > 0 ? (received / total).clamp(0.0, 1.0) : null;
}

class DeviceSnapshot {
  const DeviceSnapshot({
    this.files = const {},
    this.connected = false,
    this.hasConnectionState = true,
    this.message = '尚未连接设备',
    this.devices = const [],
    this.name,
    this.advertisedName,
    this.battery,
    this.width,
    this.height,
    this.autoSleep,
    this.timedWake,
    this.wakeMinutes,
    this.syncedAt,
    this.settingsMessage,
    this.settingsBusy = false,
    this.presentKeys = const {},
    this.passport = const {},
    this.hasPassportState = false,
    this.importedFile,
    this.importedFirmware,
    this.importing = false,
    this.transfer = const FilmTransfer(),
    this.firmwareDownload = const FirmwareDownload(),
    this.hasDownloadState = false,
    this.hasFilmState = false,
    this.hasFirmwareState = false,
    this.hasImportingState = false,
    this.hasTransferState = false,
  });

  factory DeviceSnapshot.fromMap(Map<Object?, Object?> data) {
    return DeviceSnapshot(
      files: data['files'] is Map
          ? Map<String, Object?>.from(data['files'] as Map)
          : const {},
      connected: data['connected'] == true,
      hasConnectionState: data.containsKey('connected'),
      message: data['message'] as String? ?? '尚未连接设备',
      devices: (data['devices'] as List? ?? const []).map((value) {
        final item = value as Map;
        return DeviceInfo(
          address: item['address'] as String? ?? '',
          name: item['name'] as String? ?? '未命名设备',
        );
      }).toList(),
      name: data['name'] as String?,
      advertisedName: data['advertisedName'] as String?,
      battery: (data['battery'] as num?)?.toInt(),
      width: (data['width'] as num?)?.toInt(),
      height: (data['height'] as num?)?.toInt(),
      autoSleep: data['autoSleep'] as bool?,
      timedWake: data['timedWake'] as bool?,
      wakeMinutes: (data['wakeMinutes'] as num?)?.toInt(),
      syncedAt: (data['syncedAt'] as num?)?.toInt(),
      settingsMessage: data['settingsMessage'] as String?,
      settingsBusy: data['settingsBusy'] == true,
      presentKeys: data.keys.toSet(),
      passport: data['passport'] is Map
          ? Map<String, Object?>.from(data['passport'] as Map)
          : const {},
      hasPassportState: data.containsKey('passport'),
      importedFile: data['importedFile'] is Map
          ? ImportedFilm.fromMap(data['importedFile'] as Map)
          : null,
      importing: data['importing'] == true,
      importedFirmware: data['importedFirmware'] is Map
          ? ImportedFirmware.fromMap(data['importedFirmware'] as Map)
          : null,
      transfer: data['transfer'] is Map
          ? FilmTransfer.fromMap(data['transfer'] as Map)
          : const FilmTransfer(),
      firmwareDownload: data['firmwareDownload'] is Map
          ? FirmwareDownload.fromMap(data['firmwareDownload'] as Map)
          : const FirmwareDownload(),
      hasDownloadState: data.containsKey('firmwareDownload'),
      hasFilmState: data.containsKey('importedFile'),
      hasFirmwareState: data.containsKey('importedFirmware'),
      hasImportingState: data.containsKey('importing'),
      hasTransferState: data.containsKey('transfer'),
    );
  }

  final Map<String, Object?> files;
  final bool connected;
  final bool hasConnectionState;
  final String message;
  final List<DeviceInfo> devices;
  final String? name;
  final String? advertisedName;
  final int? battery;
  final int? width;
  final int? height;
  final bool? autoSleep;
  final bool? timedWake;
  final int? wakeMinutes;
  final int? syncedAt;
  final String? settingsMessage;
  final bool settingsBusy;
  final Set<Object?> presentKeys;
  final Map<String, Object?> passport;
  final bool hasPassportState;
  bool get passportBusy =>
      const {'reading', 'sending', 'verifying'}.contains(passport['phase']);
  final ImportedFilm? importedFile;
  final ImportedFirmware? importedFirmware;
  final bool importing;
  final FilmTransfer transfer;
  final FirmwareDownload firmwareDownload;
  final bool hasDownloadState;
  final bool hasFilmState;
  final bool hasFirmwareState;
  final bool hasImportingState;
  final bool hasTransferState;

  DeviceSnapshot retainingTransfer(DeviceSnapshot previous) {
    final keepConnected = hasConnectionState ? connected : previous.connected;
    return DeviceSnapshot(
      firmwareDownload: hasDownloadState || firmwareDownload.phase != 'idle'
          ? firmwareDownload
          : previous.firmwareDownload,
      passport: hasPassportState ? passport : previous.passport,
      hasPassportState: true,
      files: !keepConnected
          ? const {}
          : (presentKeys.contains('files') || files.isNotEmpty)
          ? files
          : previous.files,
      connected: keepConnected,
      advertisedName: !keepConnected
          ? null
          : presentKeys.contains('advertisedName')
          ? advertisedName
          : advertisedName ?? previous.advertisedName,
      message: message,
      devices: devices,
      name: !keepConnected
          ? null
          : presentKeys.contains('name')
          ? name
          : name ?? previous.name,
      battery: !keepConnected
          ? null
          : presentKeys.contains('battery')
          ? battery
          : battery ?? previous.battery,
      width: !keepConnected
          ? null
          : presentKeys.contains('width')
          ? width
          : width ?? previous.width,
      height: !keepConnected
          ? null
          : presentKeys.contains('height')
          ? height
          : height ?? previous.height,
      settingsBusy:
          keepConnected &&
          (presentKeys.contains('settingsBusy')
              ? settingsBusy
              : settingsBusy || previous.settingsBusy),
      autoSleep: !keepConnected
          ? null
          : presentKeys.contains('autoSleep')
          ? autoSleep
          : autoSleep ?? previous.autoSleep,
      timedWake: !keepConnected
          ? null
          : presentKeys.contains('timedWake')
          ? timedWake
          : timedWake ?? previous.timedWake,
      wakeMinutes: !keepConnected
          ? null
          : presentKeys.contains('wakeMinutes')
          ? wakeMinutes
          : wakeMinutes ?? previous.wakeMinutes,
      syncedAt: !keepConnected
          ? null
          : presentKeys.contains('syncedAt')
          ? syncedAt
          : syncedAt ?? previous.syncedAt,
      settingsMessage: !keepConnected
          ? null
          : presentKeys.contains('settingsMessage')
          ? settingsMessage
          : settingsMessage ?? previous.settingsMessage,
      importedFile: hasFilmState || importedFile != null
          ? importedFile
          : previous.importedFile,
      importing: hasImportingState || importing
          ? importing
          : previous.importing,
      importedFirmware: hasFirmwareState || importedFirmware != null
          ? importedFirmware
          : previous.importedFirmware,
      transfer: hasTransferState || transfer.phase != 'idle'
          ? transfer
          : previous.transfer,
    );
  }
}

abstract class DeviceGateway {
  bool get supported;
  Stream<DeviceSnapshot> get snapshots;
  Future<void> invoke(String method, [Map<String, Object?>? arguments]);
}

class PlatformDeviceGateway implements DeviceGateway {
  static const _methods = MethodChannel('org.framefilm.ark/methods');
  static const _events = EventChannel('org.framefilm.ark/events');

  @override
  bool get supported =>
      !kIsWeb && defaultTargetPlatform == TargetPlatform.android;

  @override
  Stream<DeviceSnapshot> get snapshots => supported
      ? _events.receiveBroadcastStream().map(
          (event) => DeviceSnapshot.fromMap(event as Map<Object?, Object?>),
        )
      : const Stream.empty();

  @override
  Future<void> invoke(String method, [Map<String, Object?>? arguments]) async {
    if (supported) await _methods.invokeMethod<Object?>(method, arguments);
  }
}

class DeviceController extends ChangeNotifier {
  DeviceController(this.gateway) {
    if (gateway.supported) {
      _subscription = gateway.snapshots.listen(
        (value) {
          if (_disposed) return;
          final wasConnected = snapshot.connected;
          final wasTransferring = snapshot.transfer.active;
          snapshot = value.retainingTransfer(snapshot);
          if (snapshot.connected != wasConnected) {
            _connectionSession++;
            automaticFilesPending = snapshot.connected;
            automaticFilesLoading = false;
            automaticFilesFailed = false;
            _automaticFilesInitialized = false;
          }
          if (wasTransferring &&
              !snapshot.transfer.active &&
              snapshot.transfer.success &&
              snapshot.connected) {
            automaticFilesPending = true;
          }
          // Android announces the connection before starting its settings read.
          // Wait for that initialization, or an already initialized snapshot.
          _automaticFilesInitialized =
              _automaticFilesInitialized ||
              snapshot.settingsBusy ||
              ((snapshot.width ?? 0) > 0 && (snapshot.height ?? 0) > 0);
          notifyListeners();
          _scheduleAutomaticFiles();
        },
        onError: (Object error) {
          errorMessage = '蓝牙状态读取失败：$error';
          notifyListeners();
        },
      );
      unawaited(command('snapshot'));
    }
  }

  final DeviceGateway gateway;
  DeviceSnapshot snapshot = const DeviceSnapshot();
  String? errorMessage;
  bool busy = false;
  bool _disposed = false;
  int _connectionSession = 0;
  bool _automaticFilesScheduled = false;
  bool _automaticFilesInitialized = false;
  bool automaticFilesPending = false;
  bool automaticFilesLoading = false;
  bool automaticFilesFailed = false;
  StreamSubscription<DeviceSnapshot>? _subscription;

  void _scheduleAutomaticFiles() {
    if (_disposed || _automaticFilesScheduled || !automaticFilesPending) return;
    _automaticFilesScheduled = true;
    scheduleMicrotask(() async {
      _automaticFilesScheduled = false;
      if (_disposed ||
          !automaticFilesPending ||
          !_automaticFilesInitialized ||
          !canEditSettings)
        return;
      final session = _connectionSession;
      automaticFilesPending = false;
      automaticFilesLoading = true;
      await command('listDeviceFiles');
      if (_disposed || session != _connectionSession) return;
      automaticFilesLoading = false;
      automaticFilesFailed = snapshot.files['loaded'] != true;
      notifyListeners();
    });
  }

  bool get canImportFilm =>
      gateway.supported &&
      !busy &&
      !snapshot.settingsBusy &&
      !snapshot.passportBusy &&
      !snapshot.importing &&
      !snapshot.firmwareDownload.canCancel &&
      !snapshot.transfer.active &&
      !snapshot.transfer.canConfirm &&
      (!snapshot.transfer.canRetry || snapshot.transfer.cleanupCompleted);

  bool get canEditSettings =>
      gateway.supported &&
      snapshot.connected &&
      !busy &&
      !snapshot.settingsBusy &&
      !snapshot.passportBusy &&
      !snapshot.importing &&
      !snapshot.firmwareDownload.canCancel &&
      !snapshot.transfer.active &&
      !snapshot.transfer.canConfirm &&
      (!snapshot.transfer.canRetry || snapshot.transfer.cleanupCompleted);

  Future<void> command(String method, [Map<String, Object?>? arguments]) async {
    if (_disposed) return;
    if (method == 'importGeneratedFilm' && !canImportFilm) return;
    // A failed WiFi passport job may need retry to settle its previous network.
    // Do not use canEditSettings here: it intentionally blocks unclean transfers.
    if (method == 'retryPassport' &&
        (!snapshot.connected ||
            snapshot.passport['canRetry'] != true ||
            snapshot.settingsBusy ||
            snapshot.importing ||
            snapshot.firmwareDownload.canCancel ||
            snapshot.transfer.active ||
            snapshot.transfer.canConfirm))
      return;
    if (deviceSettingsMethods.contains(method) && !canEditSettings) return;
    final cancellingPassport = method == 'cancelPassport';
    if (snapshot.passportBusy && method != 'snapshot' && !cancellingPassport) {
      return;
    }
    if (cancellingPassport && snapshot.passport['canCancel'] != true) return;
    final cancellingDownload = method == 'cancelFirmwareDownload';
    final cancelling =
        method == 'cancelTransfer' || cancellingDownload || cancellingPassport;
    if (!gateway.supported || (busy && !cancelling)) return;
    if ((snapshot.transfer.active || snapshot.transfer.canConfirm) &&
        const {
          'pickFilm',
          'clearFilm',
          'connect',
          'disconnect',
          'scan',
          'startTransfer',
          'pickFirmware',
          'downloadFirmware',
          'clearFirmware',
          'startFirmwareTransfer',
        }.contains(method)) {
      return;
    }
    if (snapshot.importing &&
        const {
          'pickFilm',
          'clearFilm',
          'startTransfer',
          'retryTransfer',
          'pickFirmware',
          'downloadFirmware',
          'clearFirmware',
          'startFirmwareTransfer',
        }.contains(method)) {
      return;
    }
    if (method == 'startTransfer' &&
        (snapshot.importedFile == null || !snapshot.connected)) {
      return;
    }
    if (method == 'startFirmwareTransfer' &&
        (snapshot.importedFirmware == null || !snapshot.connected)) {
      return;
    }
    if (snapshot.transfer.canRetry &&
        !snapshot.transfer.cleanupCompleted &&
        const {
          'pickFilm',
          'clearFilm',
          'startTransfer',
          'pickFirmware',
          'downloadFirmware',
          'clearFirmware',
          'startFirmwareTransfer',
        }.contains(method)) {
      return;
    }
    if (method == 'retryTransfer' &&
        (!snapshot.transfer.canRetry ||
            snapshot.transfer.active ||
            (snapshot.transfer.kind == 'firmware'
                ? snapshot.importedFirmware == null
                : snapshot.importedFile == null) ||
            !snapshot.connected)) {
      return;
    }
    if (method == 'confirmFirmwareTransfer' &&
        (!snapshot.transfer.canConfirm || snapshot.transfer.active)) {
      return;
    }
    if (cancellingDownload && !snapshot.firmwareDownload.canCancel) return;
    if (cancelling &&
        !cancellingDownload &&
        !cancellingPassport &&
        !snapshot.transfer.canCancel) {
      return;
    }
    if (method == 'listDeviceFiles') {
      automaticFilesPending = false;
      automaticFilesFailed = false;
    }
    if (!cancelling) busy = true;
    errorMessage = null;
    notifyListeners();
    try {
      await gateway.invoke(method, arguments);
    } catch (error) {
      errorMessage = '操作未完成：$error';
    } finally {
      if (!cancelling) busy = false;
      if (!_disposed) notifyListeners();
      _scheduleAutomaticFiles();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    unawaited(_subscription?.cancel());
    super.dispose();
  }
}
