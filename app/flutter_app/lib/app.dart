import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';

import 'device_gateway.dart';
import 'app_update.dart';
import 'about_panel.dart';
import 'preview_device_gateway.dart';
import 'github_release_service.dart';
import 'passport_editor.dart';
import 'frame_editor.dart';
import 'animation_workshop.dart';
import 'device_files.dart';
import 'forfilm_theme.dart';
import 'interface_theme_preferences.dart';
import 'workbench_widgets.dart';
import 'workbench_page_transition.dart';
import 'frame_io.dart' as frame_io;
import 'frame_quick_page.dart';
import 'frame_media.dart' as frame_media;

class FrameFilmApp extends StatefulWidget {
  const FrameFilmApp({super.key, this.gateway, this.themePreferences});
  final DeviceGateway? gateway;
  final InterfaceThemePreferences? themePreferences;

  @override
  State<FrameFilmApp> createState() => _FrameFilmAppState();
}

class _FrameFilmAppState extends State<FrameFilmApp> {
  late DeviceController device;
  late AppUpdateController appUpdate;
  final releases = GitHubReleaseService();
  GitHubFirmwareRelease? onlineFirmware;
  bool checkingReleases = false;
  String? releaseMessage;
  Future<void> checkReleases() async {
    setState(() {
      checkingReleases = true;
      releaseMessage = null;
      onlineFirmware = null;
    });
    try {
      final release = await releases.latest();
      if (mounted) {
        setState(() {
          onlineFirmware = release;
          releaseMessage = release == null
              ? '暂无包含 frame_film_ark.bin 的正式发布。'
              : null;
        });
      }
    } catch (error) {
      if (mounted) setState(() => releaseMessage = '发布查询失败：$error');
    } finally {
      if (mounted) setState(() => checkingReleases = false);
    }
  }

  bool arkThemeEnabled = true;
  bool themePreferenceReady = false;
  bool themeWelcomed = false;
  String? themePreferenceMessage;
  int themePreferenceRevision = 0;
  Future<void> themeSave = Future<void>.value();
  late final InterfaceThemePreferences themePreferences =
      widget.themePreferences ?? const LocalInterfaceThemePreferences();
  int page = 0;
  final scrollControllers = List.generate(6, (_) => ScrollController());
  bool loading = false;
  Timer? loadingTimer;
  final passportEditor = PassportEditorController();
  final frameEditor = FrameEditorController();
  final frameQuick = FrameQuickController();
  final animation = AnimationWorkshopController();
  int frameSendEpoch = 0;

  final deviceSuffix = TextEditingController();
  final wakeInterval = TextEditingController();
  bool? autoSleepDraft, timedWakeDraft;
  DeviceSnapshot settingsSnapshot = const DeviceSnapshot();

  static const destinations = [
    ('连接', Icons.bluetooth_rounded),
    ('Frame', Icons.dashboard_outlined),
    ('Film', Icons.image_outlined),
    ('动画', Icons.animation_outlined),
    ('通行证', Icons.badge_outlined),
    ('设置', Icons.tune_rounded),
  ];

  bool get arkConnected =>
      device.snapshot.connected &&
      ((device.snapshot.advertisedName ?? device.snapshot.name)
              ?.toUpperCase()
              .startsWith('FRAMEFILMARK') ??
          false);
  bool get ark => themePreferenceReady && arkThemeEnabled && arkConnected;

  Future<void> loadThemePreference() async {
    final revision = themePreferenceRevision;
    bool enabled = true;
    try {
      enabled = await themePreferences.load();
    } catch (_) {
      // Match the web: unavailable preferences do not prevent session use.
    }
    if (!mounted || revision != themePreferenceRevision) return;
    setState(() {
      arkThemeEnabled = enabled;
      themePreferenceReady = true;
      syncThemePresentation();
    });
  }

  void syncThemePresentation() {
    if (!device.snapshot.connected) themeWelcomed = false;
    if (!ark) {
      loadingTimer?.cancel();
      loading = false;
    } else if (!themeWelcomed) {
      themeWelcomed = true;
      showThemeIntro();
    }
  }

  void showThemeIntro() {
    if (!ark) return;
    loading = true;
    loadingTimer?.cancel();
    loadingTimer = Timer(const Duration(milliseconds: 1600), () {
      if (mounted) setState(() => loading = false);
    });
  }

  void changeThemePreference(bool enabled) {
    final revision = ++themePreferenceRevision;
    setState(() {
      arkThemeEnabled = enabled;
      themePreferenceReady = true;
      themePreferenceMessage = null;
      syncThemePresentation();
    });
    // Serialize saves so rapid toggles persist the user's last choice.
    themeSave = themeSave.then((_) async {
      try {
        await themePreferences.save(enabled);
      } catch (_) {
        if (mounted && revision == themePreferenceRevision) {
          setState(() => themePreferenceMessage = '本次切换已生效，偏好未能保存。');
        }
      }
    });
  }

  @override
  void initState() {
    super.initState();
    frameQuick.addListener(onQuickChanged);
    animation.addListener(onQuickChanged);
    device = DeviceController(widget.gateway ?? PlatformDeviceGateway());
    appUpdate = AppUpdateController(
      preview: device.gateway is PreviewDeviceGateway,
    );
    device.addListener(onDeviceChanged);
    unawaited(loadThemePreference());
  }

  void onQuickChanged() {
    if (mounted) setState(() {});
  }

  void navigate(int value) {
    if (!frameQuick.busy && !animation.busy) setState(() => page = value);
  }

  void onDeviceChanged() {
    final snapshot = device.snapshot;
    final connected = snapshot.connected;
    if (!connected) {
      deviceSuffix.clear();
      wakeInterval.clear();
      autoSleepDraft = null;
      timedWakeDraft = null;
    } else {
      if (snapshot.name != settingsSnapshot.name && snapshot.name != null) {
        deviceSuffix.text = snapshot.name!.startsWith('FRAMEFILMARK-')
            ? snapshot.name!.substring('FRAMEFILMARK-'.length)
            : '';
      }
      if (snapshot.autoSleep != settingsSnapshot.autoSleep) {
        autoSleepDraft = snapshot.autoSleep;
      }
      if (snapshot.timedWake != settingsSnapshot.timedWake) {
        timedWakeDraft = snapshot.timedWake;
      }
      if (snapshot.wakeMinutes != settingsSnapshot.wakeMinutes) {
        wakeInterval.text = snapshot.wakeMinutes?.toString() ?? '';
      }
    }
    settingsSnapshot = snapshot;
    syncThemePresentation();
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    releases.dispose();
    appUpdate.dispose();
    loadingTimer?.cancel();
    device.removeListener(onDeviceChanged);
    device.dispose();
    if (device.gateway case final PreviewDeviceGateway preview) {
      preview.dispose();
    }

    frameQuick.removeListener(onQuickChanged);
    frameQuick.dispose();
    frameEditor.dispose();
    animation.removeListener(onQuickChanged);
    animation.clear();
    animation.dispose();
    for (final controller in scrollControllers) {
      controller.dispose();
    }
    deviceSuffix.dispose();
    wakeInterval.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'FrameFilm',
      debugShowCheckedModeBanner: false,
      theme: buildForFilmTheme(ark),
      home: LayoutBuilder(
        builder: (context, constraints) {
          final wide = constraints.maxWidth >= 760;
          return Scaffold(
            appBar: AppBar(
              title: WorkbenchBrand(ark: ark),
              actions: [
                Padding(
                  padding: const EdgeInsets.only(right: 16),
                  child: Chip(
                    avatar: Icon(
                      device.snapshot.connected
                          ? Icons.bluetooth_connected
                          : Icons.bluetooth_disabled,
                      size: 16,
                    ),
                    label: Text(
                      device.snapshot.connected
                          ? '${device.snapshot.battery == null ? '' : '${device.snapshot.battery}% · '}已连接'
                          : '离线',
                    ),
                  ),
                ),
              ],
            ),
            body: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                if (device.gateway case final PreviewDeviceGateway preview)
                  previewControls(context, preview),
                Expanded(
                  child: Stack(
                    children: [
                      Row(
                        children: [
                          if (wide)
                            WorkbenchNavigation(
                              items: destinations,
                              selected: page,
                              onSelected: navigate,
                            ),
                          Expanded(
                            child: WorkbenchPageTransition(
                              index: page,
                              child: Align(
                                alignment: Alignment.topCenter,
                                child: ConstrainedBox(
                                  constraints: BoxConstraints(
                                    maxWidth: page == 0 ? 720 : 1240,
                                  ),
                                  child: KeyedSubtree(
                                    key: ValueKey(page),
                                    child: ListView(
                                      key: const Key('workbench-content'),
                                      controller: scrollControllers[page],
                                      padding: EdgeInsets.all(wide ? 32 : 20),
                                      children: [
                                        Text(
                                          ark
                                              ? 'TERMINAL / 0${page + 1}'
                                              : '你的电子纸工作台',
                                          style: Theme.of(context)
                                              .textTheme
                                              .labelMedium,
                                        ),
                                        const SizedBox(height: 8),
                                        Text(
                                          destinations[page].$1,
                                          style: Theme.of(context)
                                              .textTheme
                                              .headlineLarge
                                              ?.copyWith(
                                                fontWeight: FontWeight.w700,
                                              ),
                                        ),
                                        const SizedBox(height: 24),
                                        ...pageContent(context),
                                      ],
                                    ),
                                  ),
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                      if (loading)
                        Positioned.fill(child: connectionTransition()),
                    ],
                  ),
                ),
              ],
            ),
            bottomNavigationBar: wide
                ? null
                : NavigationBar(
                    selectedIndex: page,
                    onDestinationSelected: navigate,
                    labelBehavior:
                        NavigationDestinationLabelBehavior.alwaysShow,
                    destinations: destinations
                        .map(
                          (item) => NavigationDestination(
                            icon: Icon(item.$2),
                            label: item.$1,
                          ),
                        )
                        .toList(),
                  ),
          );
        },
      ),
    );
  }

  void enterPreview() {
    device.removeListener(onDeviceChanged);
    device.dispose();
    device = DeviceController(PreviewDeviceGateway());
    appUpdate.dispose();
    appUpdate = AppUpdateController(preview: true);
    device.addListener(onDeviceChanged);
    setState(() {});
  }

  Widget previewControls(BuildContext context, PreviewDeviceGateway preview) =>
      Material(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        child: SafeArea(
          bottom: false,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  PreviewDeviceGateway.warning,
                  key: Key('preview-warning'),
                  style: TextStyle(fontWeight: FontWeight.bold),
                ),
                Wrap(
                  spacing: 16,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    DropdownButton<PreviewScenario>(
                      key: const Key('preview-scenario'),
                      value: preview.scenario,
                      items: const [
                        DropdownMenuItem(
                          value: PreviewScenario.normal,
                          child: Text('正常流程'),
                        ),
                        DropdownMenuItem(
                          value: PreviewScenario.transferFailure,
                          child: Text('传输失败（重试恢复）'),
                        ),
                        DropdownMenuItem(
                          value: PreviewScenario.unconfirmed,
                          child: Text('升级结果待确认'),
                        ),
                        DropdownMenuItem(
                          value: PreviewScenario.sameBuild,
                          child: Text('相同构建（跳过升级）'),
                        ),
                      ],
                      onChanged: (value) {
                        if (value != null) preview.reset(value);
                      },
                    ),
                    TextButton.icon(
                      key: const Key('reset-preview'),
                      onPressed: () => preview.reset(),
                      icon: const Icon(Icons.restart_alt),
                      label: const Text('重置演示'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      );

  Widget panel(BuildContext context, {required Widget child}) =>
      WorkbenchPanel(child: child);

  List<Widget> pageContent(BuildContext context) => switch (page) {
    0 => connectionPage(context),
    1 => framePage(context),
    2 => filmPage(context),
    3 => [
      panel(
        context,
        child: AnimationWorkshop(
          controller: animation,
          pickImages: frame_media.pickFramePhotos,
          captureImage: () => frame_media.captureFramePhoto(context),
          sendFilm: sendFrame,
          cancelSend: cancelFrameSend,
          saveFilm: frame_io.canDownloadFrame ? frame_io.downloadFrame : null,
          connected: device.snapshot.connected && device.canImportFilm,
          disabledReason: !device.snapshot.connected
              ? '连接 Ark 后可发送动画'
              : device.canImportFilm
              ? null
              : '请等待当前设备操作结束',
          playOnDevice:
              ({
                required intervalMs,
                required loopSeconds,
                required playMode,
              }) async {
                await device.command('playAnimation', {
                  'intervalMs': intervalMs,
                  'loopSeconds': loopSeconds,
                  'playMode': playMode,
                });
                if (device.errorMessage != null) {
                  throw StateError(device.errorMessage!);
                }
              },
        ),
      ),
      if (device.snapshot.transfer.kind == 'film' &&
          device.snapshot.transfer.phase != 'idle')
        transferStatus(context, kind: 'film'),
    ],
    4 => passportPage(context),
    _ => settingsPage(context),
  };

  Future<void> importFrame(Uint8List bytes, String name) async {
    if (!device.canImportFilm) throw StateError('请等待当前设备操作结束');
    await device.command('importGeneratedFilm', {'bytes': bytes, 'name': name});
    if (device.errorMessage != null) throw StateError(device.errorMessage!);
  }

  Future<void> cancelFrameSend() async {
    frameSendEpoch++;
    await device.command('cancelTransfer');
  }

  Future<void> sendFrame(Uint8List bytes, String name) async {
    if (!device.snapshot.connected) throw StateError('请先连接 Ark');
    final epoch = ++frameSendEpoch;
    await importFrame(bytes, name);
    if (epoch != frameSendEpoch) throw StateError('发送已取消');
    await device.command('startTransfer');
    if (device.errorMessage != null) throw StateError(device.errorMessage!);
    final completed = Completer<void>();
    void changed() {
      if (completed.isCompleted) return;
      final t = device.snapshot.transfer;
      if (t.kind != 'film' || t.active) return;
      if (t.success && t.cleanupCompleted) {
        completed.complete();
      } else if (t.cleanupCompleted || !device.snapshot.connected) {
        completed.completeError(StateError(t.message));
      }
    }

    device.addListener(changed);
    try {
      changed();
      await completed.future.timeout(const Duration(minutes: 4));
    } on TimeoutException {
      await cancelFrameSend();
      rethrow;
    } finally {
      device.removeListener(changed);
    }
  }

  List<Widget> framePage(BuildContext context) => [
    if (device.gateway is PreviewDeviceGateway)
      const Padding(
        padding: EdgeInsets.only(bottom: 12),
        child: Text('图片转换为真实文件；当前连接和发送为模拟操作。'),
      ),
    FrameQuickPage(
      controller: frameQuick,
      pickImages: frame_media.pickFramePhotos,
      captureImage: () => frame_media.captureFramePhoto(context),
      sendFilm: sendFrame,
      cancelSend: cancelFrameSend,
      canSend: device.canImportFilm && device.snapshot.connected,
      battery: device.snapshot.battery,
    ),
    if (device.snapshot.transfer.kind == 'film' &&
        device.snapshot.transfer.phase != 'idle') ...[
      const SizedBox(height: 12),
      transferStatus(context, kind: 'film'),
    ],
  ];
  List<Widget> connectionPage(BuildContext context) {
    final snapshot = device.snapshot;
    final supported = device.gateway.supported;
    return [
      panel(
        context,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Icon(
              snapshot.connected
                  ? Icons.bluetooth_connected
                  : Icons.bluetooth_searching,
              size: 40,
              color: Theme.of(context).colorScheme.primary,
            ),
            const SizedBox(height: 16),
            Text(
              snapshot.connected
                  ? snapshot.name ?? 'FrameFilm 设备'
                  : '连接你的 FrameFilm',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 8),
            Text(
              supported ? snapshot.message : '当前平台仅支持界面预览，蓝牙连接请使用 Android 客户端。',
            ),
            if (device.errorMessage != null)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Text(
                  device.errorMessage!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
            const SizedBox(height: 20),
            if (kIsWeb && !supported)
              Padding(
                padding: const EdgeInsets.only(bottom: 16),
                child: FilledButton(
                  key: const Key('enter-preview'),
                  onPressed: enterPreview,
                  child: const Text('进入模拟设备演示'),
                ),
              ),
            if (supported)
              Wrap(
                spacing: 12,
                runSpacing: 12,
                children: [
                  if (!snapshot.connected)
                    FilledButton.icon(
                      onPressed:
                          device.busy ||
                              device.snapshot.transfer.active ||
                              device.snapshot.importing
                          ? null
                          : () => device.command('scan'),
                      icon: const Icon(Icons.search),
                      label: Text(device.busy ? '正在处理…' : '扫描设备'),
                    ),
                  if (snapshot.connected) ...[
                    FilledButton.icon(
                      onPressed:
                          device.busy ||
                              device.snapshot.transfer.active ||
                              device.snapshot.importing
                          ? null
                          : () => device.command('refresh'),
                      icon: const Icon(Icons.refresh),
                      label: const Text('刷新信息'),
                    ),
                    OutlinedButton(
                      onPressed:
                          device.busy ||
                              device.snapshot.transfer.active ||
                              device.snapshot.importing
                          ? null
                          : () => device.command('disconnect'),
                      child: const Text('断开连接'),
                    ),
                  ],
                ],
              ),
            if (snapshot.connected) ...[
              const SizedBox(height: 24),
              Wrap(
                spacing: 24,
                runSpacing: 16,
                children: [
                  information(
                    '电量',
                    snapshot.battery == null ? '等待读取' : '${snapshot.battery}%',
                  ),
                  information(
                    '屏幕',
                    snapshot.width == null || snapshot.height == null
                        ? '等待读取'
                        : '${snapshot.width} × ${snapshot.height}',
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
      if (supported && !snapshot.connected) ...[
        const SizedBox(height: 24),
        Text('附近设备', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 12),
        if (snapshot.devices.isEmpty)
          panel(context, child: const Text('暂无扫描结果。打开设备蓝牙后，点击扫描设备。')),
        for (final item in snapshot.devices)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Card(
              child: ListTile(
                leading: const Icon(Icons.developer_board),
                title: Text(item.name),
                subtitle: Text(item.address),
                trailing: FilledButton.tonal(
                  onPressed:
                      device.busy ||
                          device.snapshot.transfer.active ||
                          device.snapshot.importing
                      ? null
                      : () => device.command('connect', {
                          'address': item.address,
                        }),
                  child: const Text('连接'),
                ),
              ),
            ),
          ),
      ],
      const SizedBox(height: 20),
      Text(
        device.gateway is PreviewDeviceGateway
            ? '演示中的扫描、文件选择、传输及升级均为模拟。Film 页体验直传，设置页体验 OTA。'
            : '原生连接与文件传输\n扫描、连接和读取由 Android 蓝牙服务提供。film 文件直传请进入 Film 页。',
      ),
      const SizedBox(height: 20),
      connectionInfoPanel(context),
    ];
  }

  List<Widget> filmPage(BuildContext context) {
    final snapshot = device.snapshot;
    final file = snapshot.importedFile;
    final transfer = snapshot.transfer;
    final supported = device.gateway.supported;
    final available =
        supported &&
        !device.busy &&
        !snapshot.importing &&
        !snapshot.settingsBusy &&
        !snapshot.passportBusy &&
        !transfer.active &&
        !transfer.canConfirm;
    final editable =
        available && !(transfer.canRetry && !transfer.cleanupCompleted);
    return [
      FrameEditor(
        controller: frameEditor,
        canImportFilm: device.canImportFilm,
        canSend: device.canImportFilm && device.snapshot.connected,
        onImportFilm: importFrame,
        onSendFilm: sendFrame,
        onCancelSend: cancelFrameSend,
        pickPhoto: () async {
          final photos = await frame_media.pickFramePhotos(multiple: false);
          return photos.isEmpty ? null : photos.first;
        },
      ),
      const SizedBox(height: 24),
      panel(
        context,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'film 文件 · Wi-Fi 直传',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 12),
            Text(
              supported
                  ? '导入已有 .film 文件，通过已连接的 Ark 建立 Wi-Fi Direct 传输。'
                  : '当前平台仅供界面预览。文件导入与 Wi-Fi Direct 直传需要 Android 原生客户端。',
            ),
            const SizedBox(height: 20),
            Text(
              file?.name ?? '尚未选择 film 文件',
              key: const Key('film-file-name'),
            ),
            if (file != null) ...[
              const SizedBox(height: 8),
              Text('${file.size} 字节'),
              Text(
                file.width != null && file.height != null
                    ? '${file.width} × ${file.height}'
                    : '尺寸信息不可用',
              ),
            ],
            if (snapshot.importing)
              const Padding(
                padding: EdgeInsets.only(top: 12),
                child: Text('正在导入并校验文件…'),
              ),
            const SizedBox(height: 20),
            Wrap(
              spacing: 12,
              runSpacing: 12,
              children: [
                OutlinedButton.icon(
                  key: const Key('pick-film'),
                  onPressed: editable ? () => device.command('pickFilm') : null,
                  icon: const Icon(Icons.file_open_outlined),
                  label: const Text('选择 film 文件'),
                ),
                FilledButton.icon(
                  key: const Key('start-transfer'),
                  onPressed:
                      editable &&
                          file != null &&
                          snapshot.connected &&
                          !(transfer.kind == 'film' && transfer.canRetry)
                      ? () => device.command('startTransfer')
                      : null,
                  icon: const Icon(Icons.wifi),
                  label: const Text('Wi-Fi 直传'),
                ),
                OutlinedButton(
                  key: const Key('clear-film'),
                  onPressed: editable && file != null
                      ? () => device.command('clearFilm')
                      : null,
                  child: const Text('清除文件'),
                ),
              ],
            ),
            if (supported && !snapshot.connected)
              const Padding(
                padding: EdgeInsets.only(top: 12),
                child: Text('请先在连接页连接 Ark 设备。'),
              ),
            if (device.errorMessage != null)
              Padding(
                padding: const EdgeInsets.only(top: 12),
                child: Text(
                  device.errorMessage!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ),
          ],
        ),
      ),
      const SizedBox(height: 20),
      transferStatus(context, kind: 'film'),
    ];
  }

  String shortBuild(String value) =>
      value.length > 16 ? value.substring(0, 16) : value;

  Widget transferStatus(
    BuildContext context, {
    required String kind,
    bool embedded = false,
  }) {
    final snapshot = device.snapshot;
    final globalTransfer = snapshot.transfer;
    final firmware = kind == 'firmware';
    final transfer = globalTransfer.kind == kind
        ? globalTransfer
        : FilmTransfer(
            kind: kind,
            message: firmware ? '选择固件后检查并升级' : '选择 film 文件后开始直传',
          );
    final hasFile = firmware
        ? snapshot.importedFirmware != null
        : snapshot.importedFile != null;
    final supported = device.gateway.supported;
    final available =
        supported &&
        !device.busy &&
        !snapshot.importing &&
        !snapshot.settingsBusy &&
        !snapshot.passportBusy &&
        !globalTransfer.active &&
        !globalTransfer.canConfirm;
    final phase = switch (transfer.phase) {
      'idle' => hasFile ? '文件已就绪' : '等待文件',
      'preparing' => '准备直传',
      'connecting' => '建立 Wi-Fi 连接',
      'downloading' => '设备接收中',
      'restoring' => firmware ? '校验固件 / 恢复 Wi-Fi' : '设备保存 / 恢复 Wi-Fi',
      'validating' => '检查目标固件',
      'ready' => '固件已校验',
      'applying' => '提交升级',
      'rebooting' => '等待设备重启',
      'confirming' => '核对运行构建',
      'unconfirmed' => '升级结果待确认',
      'cancelling' => '正在取消',
      'cleanup' => '清理临时连接',
      'done' =>
        transfer.success && transfer.cleanupCompleted
            ? (firmware ? '固件构建已确认' : '传输完成')
            : '等待完成确认',
      'cancelled' => '已取消',
      'error' => '传输未完成',
      _ => transfer.phase,
    };
    final content = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          phase,
          key: const Key('transfer-phase'),
          style: Theme.of(context).textTheme.titleMedium,
        ),
        const SizedBox(height: 12),
        Text(transfer.message),
        if (globalTransfer.kind != kind &&
            (globalTransfer.active || globalTransfer.canConfirm))
          Text(firmware ? '设备正在传输 film，请等待传输结束。' : '设备正在升级固件，请在设置页处理。'),
        if (firmware && transfer.targetBuild != null)
          Text('目标构建：${shortBuild(transfer.targetBuild!)}'),
        if (transfer.active || transfer.total > 0) ...[
          const SizedBox(height: 16),
          LinearProgressIndicator(
            key: const Key('transfer-progress'),
            value: transfer.progress,
          ),
          const SizedBox(height: 8),
          Text('${transfer.received} / ${transfer.total} 字节'),
        ],
        if (transfer.phase == 'done' ||
            transfer.phase == 'cancelled' ||
            transfer.phase == 'error') ...[
          const SizedBox(height: 12),
          Text(
            transfer.cleanupCompleted
                ? '临时服务与连接清理已确认'
                : '清理尚未确认，请查看上方原因；可重试以重新处理连接。',
          ),
        ],
        const SizedBox(height: 16),
        Wrap(
          spacing: 12,
          runSpacing: 12,
          children: [
            OutlinedButton(
              key: const Key('cancel-transfer'),
              onPressed: supported && transfer.canCancel
                  ? () => device.command('cancelTransfer')
                  : null,
              child: Text(firmware ? '取消升级' : '取消传输'),
            ),
            if (transfer.canConfirm)
              FilledButton(
                key: const Key('confirm-firmware'),
                onPressed: supported && !device.busy && !transfer.active
                    ? () => device.command('confirmFirmwareTransfer')
                    : null,
                child: const Text('重连并确认升级'),
              ),
            FilledButton.tonal(
              key: const Key('retry-transfer'),
              onPressed:
                  available &&
                      hasFile &&
                      snapshot.connected &&
                      transfer.canRetry
                  ? () => device.command('retryTransfer')
                  : null,
              child: Text(firmware ? '重试升级' : '重试传输'),
            ),
          ],
        ),
      ],
    );
    return embedded ? content : panel(context, child: content);
  }

  Widget firmwarePanel(BuildContext context) {
    final snapshot = device.snapshot;
    final file = snapshot.importedFirmware;
    final transfer = snapshot.transfer;
    final supported = device.gateway.supported;
    final editable =
        supported &&
        !device.busy &&
        !snapshot.importing &&
        !snapshot.settingsBusy &&
        !snapshot.passportBusy &&
        !transfer.active &&
        !transfer.canConfirm &&
        !(transfer.canRetry && !transfer.cleanupCompleted);
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('固件升级', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              OutlinedButton(
                key: const Key('check-github'),
                onPressed: checkingReleases ? null : checkReleases,
                child: Text(checkingReleases ? '正在查询…' : '检查 GitHub 发布'),
              ),
              if (device.gateway is PreviewDeviceGateway)
                OutlinedButton(
                  key: const Key('demo-online-firmware'),
                  onPressed: () => setState(() {
                    onlineFirmware = GitHubFirmwareRelease.demo;
                    releaseMessage = null;
                  }),
                  child: const Text('演示在线固件'),
                ),
            ],
          ),
          if (releaseMessage != null) Text(releaseMessage!),
          if (onlineFirmware case final release?) ...[
            Text('${release.name} · ${release.tag}'),
            Text('发布于 ${release.publishedAt.toLocal()} · ${release.size} 字节'),
            Text(
              release.sha256 == null
                  ? '发布未提供有效 SHA-256；下载后检查镜像自身完整性。'
                  : '下载后核对 GitHub 资产 SHA-256 与镜像完整性。',
            ),
            const Text('发布版本仅供展示；升级以 ELF 构建指纹判定。下载完成只导入，仍需点击“检查并升级”。'),
            OutlinedButton(
              key: const Key('download-firmware'),
              onPressed: editable
                  ? () => device.command(
                      'downloadFirmware',
                      release.downloadArguments,
                    )
                  : null,
              child: const Text('下载并校验'),
            ),
            if (!supported) const Text('当前浏览器仅支持查询发布；下载并校验需要 Android 客户端。'),
          ],
          if (snapshot.firmwareDownload.phase != 'idle') ...[
            Text(snapshot.firmwareDownload.message),
            Text(
              '${snapshot.firmwareDownload.received} / ${snapshot.firmwareDownload.total} 字节',
            ),
            if (snapshot.firmwareDownload.canCancel) ...[
              LinearProgressIndicator(
                value: snapshot.firmwareDownload.progress,
              ),
              OutlinedButton(
                key: const Key('cancel-firmware-download'),
                onPressed: () => device.command('cancelFirmwareDownload'),
                child: const Text('取消下载'),
              ),
            ],
          ],
          const SizedBox(height: 12),
          Text(
            supported
                ? '导入 Ark 应用固件 .bin，通过 Wi-Fi 直传升级。设备重启并核对构建指纹后，才会确认完成。'
                : '固件导入与升级需要 Android 客户端。',
          ),
          const SizedBox(height: 16),
          Text(file?.name ?? '尚未选择固件', key: const Key('firmware-file-name')),
          if (file != null) ...[
            const SizedBox(height: 8),
            Text('${file.size} 字节 · ${file.project}'),
            Text('构建指纹：${shortBuild(file.elfSha256)}'),
            Text('固件版本：${file.version}'),
          ],
          if (snapshot.importing)
            const Padding(
              padding: EdgeInsets.only(top: 12),
              child: Text('正在导入并校验文件…'),
            ),
          const SizedBox(height: 16),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              OutlinedButton.icon(
                key: const Key('pick-firmware'),
                onPressed: editable
                    ? () => device.command('pickFirmware')
                    : null,
                icon: const Icon(Icons.file_open_outlined),
                label: const Text('选择固件'),
              ),
              FilledButton.icon(
                key: const Key('start-firmware'),
                onPressed:
                    editable &&
                        file != null &&
                        snapshot.connected &&
                        !(transfer.kind == 'firmware' && transfer.canRetry)
                    ? () => device.command('startFirmwareTransfer')
                    : null,
                icon: const Icon(Icons.system_update_alt),
                label: const Text('检查并升级'),
              ),
              OutlinedButton(
                key: const Key('clear-firmware'),
                onPressed: editable && file != null
                    ? () => device.command('clearFirmware')
                    : null,
                child: const Text('清除固件'),
              ),
            ],
          ),
          if (supported && !snapshot.connected && !transfer.canConfirm)
            const Padding(
              padding: EdgeInsets.only(top: 12),
              child: Text('请先连接 Ark 设备。'),
            ),
          if (device.errorMessage != null)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Text(
                device.errorMessage!,
                style: TextStyle(color: Theme.of(context).colorScheme.error),
              ),
            ),
          const Divider(height: 32),
          transferStatus(context, kind: 'firmware', embedded: true),
        ],
      ),
    );
  }

  Widget information(String label, String value) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(label),
      const SizedBox(height: 4),
      Text(
        value,
        style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w600),
      ),
    ],
  );

  List<Widget> stagePage(
    BuildContext context,
    IconData icon,
    String title,
    String subtitle,
    List<String> steps,
  ) => [
    panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 48, color: Theme.of(context).colorScheme.primary),
          const SizedBox(height: 24),
          Text(title, style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          Text(subtitle),
          const SizedBox(height: 24),
          for (final step in steps)
            Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Icon(Icons.circle_outlined, size: 18),
                  const SizedBox(width: 12),
                  Expanded(child: Text(step)),
                ],
              ),
            ),
          const Divider(),
          const SizedBox(height: 8),
          const Text('阶段说明 · 当前页面未提供编辑或传输操作。'),
        ],
      ),
    ),
  ];

  List<Widget> passportPage(BuildContext context) => [
    if (device.gateway is PreviewDeviceGateway)
      const Text('模拟设备：发送仅保存到演示内存，重置演示会清除；本地草稿单独保留。'),
    if (device.errorMessage != null)
      Text(
        device.errorMessage!,
        style: TextStyle(color: Theme.of(context).colorScheme.error),
      ),
    panel(
      context,
      child: PassportEditor(
        controller: passportEditor,
        canOperate: device.canEditSettings,
        busy: device.snapshot.passportBusy || device.busy,
        operation: device.snapshot.passport,
        onRead: () => device.command('readPassport'),
        onSave: (json, bin) =>
            device.command('savePassport', {'json': json, 'bin': bin}),
        onCancel: () => device.command('cancelPassport'),
        onRetry:
            device.snapshot.connected &&
                !device.busy &&
                !device.snapshot.settingsBusy &&
                !device.snapshot.transfer.active &&
                !device.snapshot.transfer.canConfirm
            ? () => device.command('retryPassport')
            : null,
        onOpen: () => device.command('openDevicePage', {'appId': 6}),
      ),
    ),
  ];

  Widget remotePanel(BuildContext context) {
    Widget key(String label, int value, IconData icon) => OutlinedButton.icon(
      key: Key('remote-key-$value'),
      onPressed: device.canEditSettings
          ? () => device.command('remoteKey', {'key': value})
          : null,
      icon: Icon(icon),
      label: Text(label),
    );
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('设备遥控', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text('回包表示按键已收到，电子纸刷新仍需等待。休眠后需按实体确认键唤醒。'),
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              // Match the existing ForFilm panel and Ark physical key direction.
              key('上', 3, Icons.arrow_upward),
              key('确认', 0, Icons.check),
              key('下', 2, Icons.arrow_downward),
              key('返回主菜单', 4, Icons.keyboard_return),
              key('休眠', 1, Icons.bedtime_outlined),
            ],
          ),
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              for (final entry in const [
                (5, '主菜单'),
                (6, '通行证'),
                (0, '图片'),
                (4, '系统设置'),
              ])
                TextButton(
                  onPressed: device.canEditSettings
                      ? () => device.command('openDevicePage', {
                          'appId': entry.$1,
                        })
                      : null,
                  child: Text('打开${entry.$2}'),
                ),
            ],
          ),
          if (device.snapshot.settingsMessage != null)
            Text(device.snapshot.settingsMessage!),
          if (device.errorMessage != null)
            Text(
              device.errorMessage!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
        ],
      ),
    );
  }

  Widget deviceSettingsPanel(BuildContext context) {
    final snapshot = device.snapshot;
    final enabled = device.canEditSettings;
    final suffixError = validateDeviceSuffix(deviceSuffix.text);
    final minutes = int.tryParse(wakeInterval.text);
    final validMinutes = minutes != null && minutes >= 10 && minutes <= 2880;
    Widget settingSwitch(
      String label,
      bool? draft,
      bool? actual,
      String method,
      ValueChanged<bool> change,
    ) => Row(
      children: [
        Expanded(child: Text(actual == null ? '$label（等待读取）' : label)),
        Switch(
          value: draft ?? false,
          onChanged: enabled && actual != null ? change : null,
        ),
        TextButton(
          onPressed: enabled && actual != null && draft != null
              ? () => device.command(method, {'enabled': draft})
              : null,
          child: const Text('保存'),
        ),
      ],
    );
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('设备设置', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          if (device.gateway is PreviewDeviceGateway)
            const Text('模拟设备设置，不会修改真实设备。'),
          if (!snapshot.connected) const Text('连接 Ark 后读取并修改设备设置。'),
          Align(
            alignment: Alignment.centerRight,
            child: TextButton.icon(
              key: const Key('refresh-device-settings'),
              onPressed: enabled ? () => device.command('refresh') : null,
              icon: const Icon(Icons.refresh),
              label: const Text('刷新设备设置'),
            ),
          ),
          settingSwitch(
            '自动休眠',
            autoSleepDraft,
            snapshot.autoSleep,
            'setAutoSleep',
            (value) => setState(() => autoSleepDraft = value),
          ),
          const Text('开启后按固件规则自动休眠；蓝牙连接期间不会自动计时。'),
          settingSwitch(
            '定时唤醒',
            timedWakeDraft,
            snapshot.timedWake,
            'setTimedWake',
            (value) => setState(() => timedWakeDraft = value),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: wakeInterval,
            key: const Key('wake-minutes'),
            enabled: enabled && snapshot.wakeMinutes != null,
            keyboardType: TextInputType.number,
            onChanged: (_) => setState(() {}),
            decoration: InputDecoration(
              labelText: '唤醒间隔（分钟）',
              helperText: snapshot.wakeMinutes == null
                  ? '等待读取设备值'
                  : '10–2880 分钟',
              errorText: wakeInterval.text.isNotEmpty && !validMinutes
                  ? '请输入 10–2880 的整数'
                  : null,
            ),
          ),
          TextButton(
            onPressed: enabled && snapshot.wakeMinutes != null && validMinutes
                ? () => device.command('setWakeMinutes', {'minutes': minutes})
                : null,
            child: const Text('保存唤醒间隔'),
          ),
          const Divider(height: 28),
          TextField(
            controller: deviceSuffix,
            key: const Key('device-name-suffix'),
            enabled: enabled && snapshot.name != null,
            onChanged: (_) => setState(() {}),
            decoration: InputDecoration(
              labelText: '设备名称后缀',
              prefixText: 'FRAMEFILMARK-',
              helperText: '1–16 UTF-8 字节；保存后重启设备使广播名生效',
              errorText: deviceSuffix.text.isNotEmpty ? suffixError : null,
            ),
          ),
          TextButton(
            key: const Key('save-device-name'),
            onPressed: enabled && snapshot.name != null && suffixError == null
                ? () => device.command('renameDevice', {
                    'suffix': deviceSuffix.text,
                  })
                : null,
            child: const Text('保存设备名称'),
          ),
          const Divider(height: 28),
          OutlinedButton.icon(
            key: const Key('sync-device-time'),
            onPressed: enabled ? () => device.command('syncTime') : null,
            icon: const Icon(Icons.schedule),
            label: const Text('同步手机时间与时区'),
          ),
          if (snapshot.syncedAt != null)
            Text(
              '最近同步完成：${DateTime.fromMillisecondsSinceEpoch(snapshot.syncedAt!).toLocal()}',
            ),
          if (snapshot.settingsMessage != null) ...[
            const SizedBox(height: 8),
            Text(snapshot.settingsMessage!),
          ],
        ],
      ),
    );
  }

  Widget themePanel(BuildContext context) => panel(
    context,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('界面主题', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 8),
        SwitchListTile.adaptive(
          key: const Key('ark-theme-switch'),
          contentPadding: EdgeInsets.zero,
          title: const Text('明日方舟主题'),
          subtitle: Text(
            arkThemeEnabled
                ? '连接 FRAMEFILMARK 成功后自动使用明日方舟主题。'
                : '已关闭：使用默认 ForFilm 主题。',
          ),
          value: arkThemeEnabled,
          onChanged: changeThemePreference,
        ),
        const Text('偏好保存在本机；断开连接时恢复 ForFilm，重连 Ark 后按此开关启用。'),
        if (themePreferenceMessage != null) ...[
          const SizedBox(height: 8),
          Text(themePreferenceMessage!),
        ],
        if (ark) ...[
          const SizedBox(height: 12),
          OutlinedButton(
            key: const Key('replay-ark-intro'),
            onPressed: () => setState(showThemeIntro),
            child: const Text('重看启动界面'),
          ),
        ],
      ],
    ),
  );

  Widget connectionInfoPanel(BuildContext context) => panel(
    context,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('连接信息', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 16),
        Text(
          device.snapshot.connected ? device.snapshot.name ?? '已连接设备' : '未连接设备',
        ),
        const SizedBox(height: 8),
        Text(
          device.gateway.supported
              ? device.snapshot.message
              : '当前平台无蓝牙连接能力，仅供界面预览',
        ),
        if (device.snapshot.connected) ...[
          const Divider(height: 32),
          DeviceStorageSummary(
            controller: device,
            embedded: true,
            onBrowseDirectory: (directory) {
              navigate(5);
              WidgetsBinding.instance.addPostFrameCallback((_) {
                if (mounted && page == 5 && scrollControllers[5].hasClients) {
                  scrollControllers[5].jumpTo(0);
                }
              });
              unawaited(
                device.command('setDeviceFileDirectory', {'appId': directory}),
              );
            },
          ),
        ],
      ],
    ),
  );

  List<Widget> settingsPage(BuildContext context) => [
    panel(context, child: DeviceFilesPage(controller: device)),
    const SizedBox(height: 20),
    deviceSettingsPanel(context),
    const SizedBox(height: 20),
    remotePanel(context),
    const SizedBox(height: 20),
    firmwarePanel(context),
    const SizedBox(height: 20),
    themePanel(context),
    const SizedBox(height: 20),
    panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          AppUpdatePanel(controller: appUpdate),
          const Divider(height: 32),
          const AboutPanel(embedded: true),
        ],
      ),
    ),
  ];

  Widget connectionTransition() => Stack(
    key: const Key('connection-transition'),
    children: [
      Positioned.fill(
        child: PrtsConnectionIntro(
          deviceName: device.snapshot.name ?? 'FrameFilm Ark',
        ),
      ),
      Positioned(
        right: 16,
        top: 16,
        child: TextButton(
          key: const Key('skip-ark-intro'),
          onPressed: () {
            loadingTimer?.cancel();
            setState(() => loading = false);
          },
          child: const Text('跳过'),
        ),
      ),
    ],
  );
}
