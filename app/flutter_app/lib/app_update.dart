import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'app_update_service.dart';

class InstalledAppInfo {
  const InstalledAppInfo({
    required this.packageName,
    required this.versionName,
    required this.versionCode,
  });

  factory InstalledAppInfo.fromMap(Map data) => InstalledAppInfo(
    packageName: data['packageName'] as String? ?? '',
    versionName: data['versionName'] as String? ?? '',
    versionCode: (data['versionCode'] as num?)?.toInt() ?? 0,
  );

  final String packageName, versionName;
  final int versionCode;
}

class AppUpdateState {
  const AppUpdateState({
    this.phase = 'idle',
    this.received = 0,
    this.total = 0,
    this.message = '',
    this.canCancel = false,
    this.canInstall = false,
  });

  factory AppUpdateState.fromMap(Map data) => AppUpdateState(
    phase: data['phase'] as String? ?? 'idle',
    received: (data['received'] as num?)?.toInt() ?? 0,
    total: (data['total'] as num?)?.toInt() ?? 0,
    message: data['message'] as String? ?? '',
    canCancel: data['canCancel'] == true,
    canInstall: data['canInstall'] == true,
  );

  final String phase, message;
  final int received, total;
  final bool canCancel, canInstall;
  bool get active => phase == 'downloading' || phase == 'validating';
  double? get progress =>
      total > 0 ? (received / total).clamp(0.0, 1.0).toDouble() : null;
}

abstract class AppUpdatePlatform {
  bool get supported;
  Future<InstalledAppInfo> getInfo();
  Future<AppUpdateState> getState();
  Future<AppUpdateState> download(AppRelease release);
  Future<AppUpdateState> cancel();
  Future<AppUpdateState> install();
}

class ChannelAppUpdatePlatform implements AppUpdatePlatform {
  const ChannelAppUpdatePlatform();
  static const _channel = MethodChannel('org.framefilm.ark/methods');

  @override
  bool get supported =>
      !kIsWeb && defaultTargetPlatform == TargetPlatform.android;

  Future<AppUpdateState> _state(
    String method, [
    Map<String, Object?>? arguments,
  ]) async {
    final data = await _channel.invokeMethod<Map<Object?, Object?>>(
      method,
      arguments,
    );
    return AppUpdateState.fromMap(data ?? const {});
  }

  @override
  Future<InstalledAppInfo> getInfo() async {
    final data = await _channel.invokeMethod<Map<Object?, Object?>>(
      'getAppInfo',
    );
    return InstalledAppInfo.fromMap(data ?? const {});
  }

  @override
  Future<AppUpdateState> getState() => _state('getAppUpdateState');

  @override
  Future<AppUpdateState> download(AppRelease release) =>
      _state('downloadAppUpdate', {
        'url': release.url,
        'name': AppUpdateService.apkName,
        'size': release.size,
        'sha256': release.sha256,
      });

  @override
  Future<AppUpdateState> cancel() => _state('cancelAppUpdate');

  @override
  Future<AppUpdateState> install() => _state('installAppUpdate');
}

class AppUpdateController extends ChangeNotifier {
  AppUpdateController({
    this.preview = false,
    AppUpdateService? service,
    AppUpdatePlatform? platform,
  }) : _service = service ?? AppUpdateService(),
       _ownsService = service == null,
       _platform = platform ?? const ChannelAppUpdatePlatform();

  final bool preview;
  final AppUpdateService _service;
  final bool _ownsService;
  final AppUpdatePlatform _platform;
  Timer? _poller, _demoTimer;
  bool _started = false, _disposed = false, _polling = false;
  bool checking = false, acting = false;
  String message = '';
  InstalledAppInfo? installed;
  AppRelease? release;
  AppUpdateState state = const AppUpdateState();

  bool get supported => _platform.supported;
  bool get canCheck => !checking && !acting && !state.active;
  bool get canDownload =>
      release != null &&
      !checking &&
      !acting &&
      (state.phase == 'idle' ||
          state.phase == 'error' ||
          state.phase == 'cancelled') &&
      (preview ||
          (supported &&
              installed != null &&
              installed!.packageName == AppUpdateService.applicationId &&
              release!.isNewerThan(installed!.versionCode)));

  void _changed() {
    if (!_disposed) notifyListeners();
  }

  Future<void> initialize() async {
    if (_started) return;
    _started = true;
    await refreshNative();
  }

  Future<void> refreshNative() async {
    if (preview || !supported || _disposed) return;
    try {
      installed = await _platform.getInfo();
      state = await _platform.getState();
      _watchState();
      _changed();
    } catch (error) {
      message = '读取应用版本失败：$error';
      _changed();
    }
  }

  Future<void> check() async {
    if (!canCheck || _disposed) return;
    checking = true;
    message = '正在查询 GitHub Releases…';
    _changed();
    try {
      if (!preview && supported) await refreshNative();
      final found = await _service.latest();
      if (_disposed) return;
      release = found;
      if (found == null) {
        message = '近 20 个正式发布中暂无可用的 Flutter APK。';
      } else if (preview) {
        message = '演示模式：查到真实发布；下载和安装仅模拟。';
      } else if (!supported) {
        message = '网页版仅可查看发布；安装版本未知，下载与安装需在 Android App 内操作。';
      } else if (installed == null) {
        message = '无法读取当前安装版本，请重试。';
      } else if (installed!.packageName != AppUpdateService.applicationId) {
        message = '当前是 ${installed!.packageName} 调试包，正式版 APK 无法直接覆盖。';
      } else if (found.isNewerThan(installed!.versionCode)) {
        message = '发现新版 ${found.versionName}（${found.versionCode}）。';
      } else {
        message = '当前已是最新版本（按 versionCode 比较）。';
      }
    } catch (error) {
      if (!_disposed) message = '发布查询失败：$error';
    } finally {
      checking = false;
      _changed();
    }
  }

  void loadDownloadDemo() {
    if (!preview || !canCheck || _disposed) return;
    release = AppRelease(
      tag: '本地演示',
      versionName: '演示安装包',
      versionCode: 1,
      minSdk: 28,
      publishedAt: DateTime(2026),
      url: '',
      size: 20 * 1024 * 1024,
      sha256: '',
    );
    state = const AppUpdateState();
    message = '演示安装包已准备；不会联网下载或安装真实 APK。';
    _changed();
  }

  Future<void> download() async {
    final target = release;
    if (!canDownload || target == null) return;
    if (preview) {
      _demoTimer?.cancel();
      state = AppUpdateState(
        phase: 'downloading',
        total: target.size,
        message: '演示下载中，不会保存 APK',
        canCancel: true,
      );
      _changed();
      var steps = 0;
      _demoTimer = Timer.periodic(const Duration(milliseconds: 300), (timer) {
        steps++;
        if (steps >= 20) {
          timer.cancel();
          state = AppUpdateState(
            phase: 'ready',
            received: target.size,
            total: target.size,
            message: '演示下载完成，可体验安装按钮',
            canInstall: true,
          );
        } else {
          state = AppUpdateState(
            phase: 'downloading',
            received: target.size * steps ~/ 20,
            total: target.size,
            message: '演示下载中，不会保存 APK',
            canCancel: true,
          );
        }
        _changed();
      });
      return;
    }
    acting = true;
    _changed();
    try {
      state = await _platform.download(target);
      _watchState();
    } catch (error) {
      message = '下载未开始：$error';
    } finally {
      acting = false;
      _changed();
    }
  }

  Future<void> cancel() async {
    if (!state.canCancel || _disposed) return;
    if (preview) {
      _demoTimer?.cancel();
      state = AppUpdateState(
        phase: 'cancelled',
        received: state.received,
        total: state.total,
        message: '演示下载已取消，可从头重新下载',
      );
      _changed();
      return;
    }
    try {
      state = await _platform.cancel();
      _watchState();
    } catch (error) {
      message = '取消失败：$error';
    }
    _changed();
  }

  Future<void> install() async {
    if (_disposed ||
        acting ||
        (!state.canInstall && state.phase != 'permission_required')) {
      return;
    }
    if (preview) {
      message = '演示模式：已模拟打开 Android 安装器，未安装应用。';
      _changed();
      return;
    }
    acting = true;
    _changed();
    try {
      state = await _platform.install();
      _watchState();
    } catch (error) {
      message = '无法打开安装器：$error';
    } finally {
      acting = false;
      _changed();
    }
  }

  void _watchState() {
    _poller?.cancel();
    if (!state.active) return;
    _poller = Timer.periodic(const Duration(milliseconds: 300), (_) async {
      if (_polling || _disposed) return;
      _polling = true;
      try {
        state = await _platform.getState();
        _changed();
        if (!state.active) _poller?.cancel();
      } catch (error) {
        message = '更新状态读取失败：$error';
        _poller?.cancel();
        _changed();
      } finally {
        _polling = false;
      }
    });
  }

  @override
  void dispose() {
    _disposed = true;
    _poller?.cancel();
    _demoTimer?.cancel();
    if (_ownsService) _service.dispose();
    super.dispose();
  }
}

class AppUpdatePanel extends StatefulWidget {
  const AppUpdatePanel({super.key, this.controller, this.preview = false});
  final AppUpdateController? controller;
  final bool preview;

  @override
  State<AppUpdatePanel> createState() => _AppUpdatePanelState();
}

class _AppUpdatePanelState extends State<AppUpdatePanel>
    with WidgetsBindingObserver {
  late final AppUpdateController _controller;

  @override
  void initState() {
    super.initState();
    _controller =
        widget.controller ?? AppUpdateController(preview: widget.preview);
    WidgetsBinding.instance.addObserver(this);
    unawaited(_controller.initialize());
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      unawaited(_controller.refreshNative());
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    if (widget.controller == null) _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: _controller,
    builder: (context, _) {
      final c = _controller;
      final release = c.release;
      return Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('应用更新', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('在应用内下载 APK，无需打开 GitHub 页面。下载完成后，由 Android 系统确认安装。'),
          if (c.preview) const Text('演示模式：发布查询是真实数据；下载与安装仅模拟。'),
          if (!c.preview && !c.supported) const Text('网页版仅可查询正式发布，当前安装版本未知。'),
          Text(
            c.installed == null
                ? '当前版本：未知'
                : '当前版本：${c.installed!.versionName}（${c.installed!.versionCode}）',
          ),
          if (release != null) ...[
            Text(
              '最新发布：${release.versionName}（${release.versionCode}） · ${release.tag}',
            ),
            Text('APK 大小：${(release.size / 1048576).toStringAsFixed(1)} MiB'),
          ],
          if (c.message.isNotEmpty) Text(c.message),
          if (c.state.message.isNotEmpty) Text(c.state.message),
          if (c.state.active) LinearProgressIndicator(value: c.state.progress),
          if (c.state.total > 0)
            Text(
              '${((c.state.progress ?? 0) * 100).toStringAsFixed(0)}% · '
              '${(c.state.received / 1048576).toStringAsFixed(1)} / '
              '${(c.state.total / 1048576).toStringAsFixed(1)} MiB',
            ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              OutlinedButton(
                onPressed: c.canCheck ? c.check : null,
                child: Text(c.checking ? '查询中…' : '检查更新'),
              ),
              if (c.preview)
                OutlinedButton(
                  onPressed: c.canCheck ? c.loadDownloadDemo : null,
                  child: const Text('体验下载流程'),
                ),
              if (c.canDownload)
                FilledButton(
                  onPressed: c.download,
                  child: Text(
                    c.state.phase == 'error' || c.state.phase == 'cancelled'
                        ? '重新下载'
                        : '应用内下载更新',
                  ),
                ),
              if (c.state.canCancel)
                TextButton(onPressed: c.cancel, child: const Text('取消下载')),
              if (c.state.canInstall || c.state.phase == 'permission_required')
                FilledButton(
                  onPressed: c.acting ? null : c.install,
                  child: Text(
                    c.state.phase == 'permission_required'
                        ? '授权后继续安装'
                        : '打开系统安装器',
                  ),
                ),
            ],
          ),
          if (c.state.phase == 'installer_opened')
            const Text('已打开系统安装界面；请按 Android 提示完成安装。'),
        ],
      );
    },
  );
}
