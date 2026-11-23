import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';

import 'device_gateway.dart';
import 'app_update.dart';
import 'about_panel.dart';
import 'preview_device_gateway.dart';
import 'github_release_service.dart';
import 'passport_editor.dart';
import 'student_passport_editor.dart';
import 'frame_editor.dart';
import 'animation_workshop.dart';
import 'device_files.dart';
import 'forfilm_theme.dart';
import 'interface_theme_preferences.dart';
import 'transfer_preferences.dart';
import 'workbench_widgets.dart';
import 'workbench_page_transition.dart';
import 'frame_io.dart' as frame_io;
import 'frame_quick_page.dart';
import 'frame_media.dart' as frame_media;

class FrameFilmApp extends StatefulWidget {
  const FrameFilmApp({
    super.key,
    this.gateway,
    this.themePreferences,
    this.transferPreferences,
  });
  final DeviceGateway? gateway;
  final InterfaceThemePreferences? themePreferences;
  final TransferPreferences? transferPreferences;

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
  int releaseQuery = 0;
  Future<void> checkReleases() async {
    final hardware = device.firmwareTarget;
    if (hardware == null) return;
    final query = ++releaseQuery;
    final connection = device.connectionSession;
    setState(() {
      checkingReleases = true;
      releaseMessage = null;
      onlineFirmware = null;
    });
    try {
      final release = device.gateway is PreviewDeviceGateway
          ? GitHubFirmwareRelease.demoFor(hardware)
          : await releases.latest(hardware: hardware);
      if (mounted &&
          device.snapshot.connected &&
          query == releaseQuery &&
          connection == device.connectionSession &&
          device.firmwareTarget?.matches(hardware) == true) {
        setState(() {
          onlineFirmware = release;
          releaseMessage = release == null ? '暂无正式发布。' : null;
        });
      }
    } catch (error) {
      if (mounted &&
          query == releaseQuery &&
          connection == device.connectionSession) {
        setState(() => releaseMessage = '发布查询失败：$error');
      }
    } finally {
      if (mounted && query == releaseQuery) {
        setState(() => checkingReleases = false);
      }
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
  late final TransferPreferences transferPreferences =
      widget.transferPreferences ?? const LocalTransferPreferences();
  TransferPreference transferPreference = TransferPreference.auto;
  bool transferPreferenceReady = false;
  String? transferPreferenceMessage;
  int transferPreferenceRevision = 0;
  Future<void> transferPreferenceSave = Future<void>.value();
  int page = 0;
  final scrollControllers = List.generate(6, (_) => ScrollController());
  bool loading = false;
  Timer? loadingTimer;
  final passportEditor = PassportEditorController();
  final studentPassportEditor = StudentPassportEditorController();
  bool editStudentPassport = false;
  final frameEditor = FrameEditorController();
  final frameQuick = FrameQuickController();
  final animation = AnimationWorkshopController();
  int frameSendEpoch = 0;

  final deviceSuffix = TextEditingController();
  final wakeInterval = TextEditingController();
  bool? autoSleepDraft, timedWakeDraft;
  DeviceSnapshot settingsSnapshot = const DeviceSnapshot();
  int firmwareConnection = -1;
  String? firmwareTargetId;

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

  Future<void> loadTransferPreference() async {
    final revision = transferPreferenceRevision;
    var preference = TransferPreference.auto;
    try {
      preference = await transferPreferences.load();
    } catch (_) {
      // Use the Wi-Fi-first default when local settings are unavailable.
    }
    if (!mounted || revision != transferPreferenceRevision) return;
    setState(() {
      transferPreference = preference;
      transferPreferenceReady = true;
    });
  }

  void changeTransferPreference(TransferPreference preference) {
    final revision = ++transferPreferenceRevision;
    setState(() {
      transferPreference = preference;
      transferPreferenceReady = true;
      transferPreferenceMessage = null;
    });
    transferPreferenceSave = transferPreferenceSave.then((_) async {
      try {
        await transferPreferences.save(preference);
      } catch (_) {
        if (mounted && revision == transferPreferenceRevision) {
          setState(() => transferPreferenceMessage = '本次设置已生效，但未能保存到本机。');
        }
      }
    });
  }

  bool get prefersBlePhoto =>
      transferPreference == TransferPreference.ble ||
      device.snapshot.effectivePhotoTransport == 'ble';

  Map<String, Object?> get transferArguments => {
    'transportPreference': transferPreference.name,
  };

  Widget mediaSendMode() => Align(
    alignment: Alignment.centerLeft,
    child: SegmentedButton<TransferPreference>(
      segments: const [
        ButtonSegment(value: TransferPreference.auto, label: Text('自动传输')),
        ButtonSegment(value: TransferPreference.ble, label: Text('蓝牙模式')),
      ],
      selected: {transferPreference},
      onSelectionChanged:
          transferPreferenceReady &&
              !device.snapshot.transfer.active &&
              !device.snapshot.passportBusy
          ? (selection) => changeTransferPreference(selection.single)
          : null,
    ),
  );

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
    if (widget.transferPreferences != null ||
        device.gateway is PlatformDeviceGateway) {
      unawaited(loadTransferPreference());
    } else {
      // Preview and injected gateways have no native preference channel.
      transferPreferenceReady = true;
    }
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
    if (firmwareConnection != device.connectionSession ||
        firmwareTargetId != device.firmwareTarget?.targetId) {
      firmwareConnection = device.connectionSession;
      firmwareTargetId = device.firmwareTarget?.targetId;
      releaseQuery++;
      checkingReleases = false;
      onlineFirmware = null;
      releaseMessage = null;
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
                        DropdownMenuItem(
                          value: PreviewScenario.legacyFirmware,
                          child: Text('旧固件首次升级（手动选型）'),
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
      if (device.gateway.supported && device.snapshot.connected) mediaSendMode(),
      panel(
        context,
        child: AnimationWorkshop(
          controller: animation,
          pickImages: frame_media.pickFramePhotos,
          captureImage: () => frame_media.captureFramePhoto(context),
           sendFilm: (bytes, name) =>
               sendFrame(bytes, name, mediaDirectory: 'animation'),
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

  Future<void> sendFrame(
    Uint8List bytes,
    String name, {
    String? mediaDirectory,
  }) async {
    if (!device.snapshot.connected) throw StateError('请先连接 Ark');
    if (!transferPreferenceReady) throw StateError('正在读取传输偏好，请稍后再发送。');
    final epoch = ++frameSendEpoch;
    final connection = device.connectionSession;
    final arguments = {
      ...transferArguments,
      'mediaDirectory': ?mediaDirectory,
    };
    await importFrame(bytes, name);
    if (epoch != frameSendEpoch) throw StateError('发送已取消');
    if (connection != device.connectionSession) {
      throw StateError('设备连接已变化，请重新确认发送');
    }
    final before = device.snapshot.transfer;
    await device.command('startTransfer', arguments);
    if (device.errorMessage != null) throw StateError(device.errorMessage!);
    final completed = Completer<void>();
    final startDeadline = Timer(const Duration(seconds: 20), () {
      if (!completed.isCompleted) {
        completed.completeError(StateError('未收到传输启动状态，请先查看设备状态。'));
      }
    });
    int? attemptId;
    void changed() {
      if (completed.isCompleted) return;
      final t = device.snapshot.transfer;
      if (identical(t, before) ||
          t.phase == 'idle' ||
          t.kind != 'film' ||
          (t.attemptId != null && t.attemptId == before.attemptId)) {
        return;
      }
      startDeadline.cancel();
      attemptId ??= t.attemptId;
      if ((attemptId != null && t.attemptId != attemptId) || t.active) return;
      if (t.success && t.cleanupCompleted) {
        completed.complete();
      } else {
        completed.completeError(StateError(t.message));
      }
    }

    device.addListener(changed);
    try {
      changed();
      // The coordinator enforces transport-specific deadlines.
      await completed.future;
    } finally {
      startDeadline.cancel();
      device.removeListener(changed);
    }
  }

  List<Widget> framePage(BuildContext context) => [
    if (device.gateway is PreviewDeviceGateway)
      const Padding(
        padding: EdgeInsets.only(bottom: 12),
        child: Text('图片转换为真实文件；当前连接和发送为模拟操作。'),
      ),
    if (device.gateway.supported && device.snapshot.connected) mediaSendMode(),
    FrameQuickPage(
      controller: frameQuick,
      pickImages: frame_media.pickFramePhotos,
      captureImage: () => frame_media.captureFramePhoto(context),
      sendFilm: sendFrame,
      sendBatchFilm: sendFrame,
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
      if (snapshot.connected) ...[
        const SizedBox(height: 20),
        connectionInfoPanel(context),
      ],
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
    final bleEligible = file?.blePhotoCompatible == true;
    final useBle = transferPreference == TransferPreference.ble ||
        (bleEligible && prefersBlePhoto);
    return [
      if (supported && snapshot.connected) mediaSendMode(),
      FrameEditor(
        controller: frameEditor,
        canImportFilm: device.canImportFilm,
        abc9PreviewSupported:
            device.snapshot.connected && device.snapshot.abc9PreviewSupported,
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
              useBle ? 'film 文件 · 蓝牙传输' : 'film 文件 · Wi-Fi 直传',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 12),
            Text(
              !supported
                  ? '当前平台仅供界面预览。文件导入与传输需要 Android 原生客户端。'
                   : transferPreference == TransferPreference.ble &&
                         file != null && !bleEligible
                   ? '当前 Ark 固件不支持通过蓝牙发送此文件。可升级固件或切回自动传输。'
                  : useBle
                  ? '使用已连接的 Ark 蓝牙发送此文件，无需 Wi-Fi。'
                  : '导入已有 .film 文件，通过已连接的 Ark 建立 Wi-Fi 直传。',
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
                           transferPreferenceReady &&
                           (transferPreference != TransferPreference.ble ||
                               bleEligible) &&
                           !(transfer.kind == 'film' &&
                               transfer.canRetry &&
                               transfer.transport == (useBle ? 'ble' : 'wifi'))
                      ? () => device.command(
                          'startTransfer',
                           transferArguments,
                        )
                      : null,
                  icon: Icon(useBle ? Icons.bluetooth : Icons.wifi),
                  label: Text(useBle ? '蓝牙发送' : 'Wi-Fi 直传'),
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
    final ble = transfer.transport == 'ble';
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
      'sending' => '正在通过蓝牙发送',
      'erasing' => '等待固件分区准备',
      'interrupted' => transfer.confirmation == 'staging_cleanup'
          ? '待确认固件暂存已清理'
          : '请先手动重启设备',
      'verifying' => firmware ? '校验固件' : '核对 SD 图片目录',
      'restoring' => firmware ? '校验固件 / 恢复 Wi-Fi' : '设备保存 / 恢复 Wi-Fi',
      'validating' => '检查目标固件',
      'ready' => '固件已校验',
      'applying' => '提交升级',
      'rebooting' => '等待设备重启',
      'confirming' => firmware ? '核对运行构建' : '核对 SD 图片目录',
      'unconfirmed' => firmware ? '升级结果待确认' : '已发送，保存待确认',
      'cancelling' => '正在取消',
      'cleanup' => ble ? '结束蓝牙传输' : '清理临时连接',
      'done' =>
        transfer.success && transfer.cleanupCompleted
            ? (firmware
                  ? '固件构建已确认'
                  : ble
                  ? '已在 SD 目录确认文件'
                  : '传输完成')
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
        if (transfer.phase != 'idle') Text(ble ? '本次传输：蓝牙' : '本次传输：Wi-Fi'),
        if (ble && transfer.confirmation == 'directory_presence')
          const Text('目录已找到本次文件，未回读文件内容或确认屏幕显示。'),
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
            ble
                ? (transfer.cleanupCompleted ? '蓝牙传输已结束' : '蓝牙传输尚未结束，请查看上方原因。')
                : transfer.cleanupCompleted
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
                key: Key(
                  firmware ? 'confirm-firmware' : 'confirm-photo-transfer',
                ),
                onPressed: supported && !device.busy && !transfer.active
                    ? () => device.command(
                        firmware
                            ? 'confirmFirmwareTransfer'
                            : 'confirmTransfer',
                      )
                    : null,
                child: Text(firmware ? '重连并确认升级' : '重新核对 SD 目录'),
              ),
            if (firmware && ble && transfer.confirmation == 'legacy_restart')
              OutlinedButton(
                onPressed: supported && snapshot.connected && !device.busy
                    ? () async {
                        final accepted = await showDialog<bool>(
                          context: context,
                          builder: (context) => AlertDialog(
                            title: const Text('确认原设备已手动重启'),
                            content: const Text(
                              '重新连接蓝牙不能清除旧 OTA。请先手动重启原 Ark，再确认；此操作不发送固件。',
                            ),
                            actions: [
                              TextButton(
                                onPressed: () => Navigator.pop(context, false),
                                child: const Text('取消'),
                              ),
                              TextButton(
                                onPressed: () => Navigator.pop(context, true),
                                child: const Text('已手动重启'),
                              ),
                            ],
                          ),
                        );
                        if (mounted && accepted == true) {
                          await device.command(
                            'acknowledgeBleFirmwareRestart',
                            {'manuallyRestarted': true},
                          );
                        }
                      }
                    : null,
                child: const Text('确认已手动重启'),
              ),
            if (transfer.canConfirm && !firmware)
              OutlinedButton(
                key: const Key('dismiss-photo-confirmation'),
                onPressed: supported && !device.busy && !transfer.active
                    ? () => device.command('dismissPhotoConfirmation')
                    : null,
                child: const Text('关闭待确认（不重新发送）'),
              ),
            FilledButton.tonal(
              key: const Key('retry-transfer'),
              onPressed:
                  available &&
                      hasFile &&
                      snapshot.connected &&
                      (!firmware ||
                          snapshot.importedFirmware?.matchesHardware(
                                device.firmwareTarget,
                              ) ==
                              true) &&
                      transfer.canRetry
                  ? () => firmware
                        ? startFirmware(context, retry: true)
                        : device.command('retryTransfer')
                  : null,
              child: Text(firmware ? '重试升级' : '重试传输'),
            ),
          ],
        ),
      ],
    );
    return embedded ? content : panel(context, child: content);
  }

  Future<void> startFirmware(
    BuildContext context, {
    bool retry = false,
    bool overBle = false,
  }) async {
    final firmwareBle = retry
        ? device.snapshot.transfer.transport == 'ble'
        : overBle;
    if (firmwareBle && !device.snapshot.bleFirmwareAvailable) {
      return;
    }
    final target = device.firmwareTarget;
    final file = device.snapshot.importedFirmware;
    final connection = device.connectionSession;
    if (target == null || file?.matchesHardware(target) != true) return;
    final legacy = device.snapshot.hardware?.trusted != true;
    if (legacy) {
      final confirmed = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: const Text('确认首次升级的实物配置'),
          content: Text(
            '旧固件无法自动验证硬件型号。请根据主控模块标识、购买记录或板卡提供方确认，实物为 ${target.label}。\n\n'
            '${target.flashBytes ~/ 1048576} MB Flash / ${target.psramBytes ~/ 1048576} MB ${target.psramBus == 'octal' ? 'Octal' : 'Quad'} PSRAM\n'
            '固件：${file!.name}\n\n'
            '选错配置可能导致设备无法启动，需要通过 USB 重新烧录恢复。型号不确定时请取消并核实。每次重试都需重新确认。',
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: const Text('取消'),
            ),
            FilledButton(
              key: const Key('confirm-legacy-firmware'),
              onPressed: () => Navigator.pop(context, true),
              child: Text('实物是 ${target.label.replaceFirst('Ark ', '')}，继续升级'),
            ),
          ],
        ),
      );
      if (confirmed != true || !mounted) return;
    }
    if (connection != device.connectionSession ||
        device.firmwareTarget?.matches(target) != true ||
        device.snapshot.importedFirmware?.fileSha256 != file!.fileSha256 ||
        device.snapshot.importedFirmware?.elfSha256 != file.elfSha256) {
      return;
    }
    await device.command(retry ? 'retryTransfer' : 'startFirmwareTransfer', {
      if (!retry) 'transportPreference': firmwareBle ? 'ble' : 'wifi',
      if (legacy) 'legacyHardwareTarget': target.targetId,
    });
  }

  Widget firmwarePanel(BuildContext context) {
    final snapshot = device.snapshot;
    final file = snapshot.importedFirmware;
    final hardware = device.firmwareTarget;
    final trustedHardware = snapshot.connected && hardware != null;
    final compatibleFile = file?.matchesHardware(hardware) == true;
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
    final canStart =
        editable &&
        file != null &&
        trustedHardware &&
        compatibleFile;
    bool canStartWith(String transport) =>
        canStart &&
        !(transfer.kind == 'firmware' &&
            transfer.canRetry &&
            transfer.transport == transport);
    return panel(
      context,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('固件升级', style: Theme.of(context).textTheme.titleLarge),
          Text(
            snapshot.hardware?.trusted == true
                ? '为 ${hardware!.label.replaceFirst('Ark ', '')} 查找兼容固件'
                : device.canSelectLegacyHardware
                ? '旧固件尚未识别硬件，请根据实物配置手动选择，默认不选。'
                : HardwareIdentity.unavailableMessage,
          ),
          if (device.canSelectLegacyHardware) ...[
            const SizedBox(height: 12),
            Wrap(
              spacing: 12,
              children: [
                for (final target in [
                  HardwareIdentity.n4r2,
                  HardwareIdentity.n16r8,
                ])
                  ChoiceChip(
                    key: Key('legacy-${target.targetId}'),
                    label: Text(target.label.replaceFirst('Ark ', '')),
                    selected:
                        device.legacyHardwareTarget?.targetId ==
                        target.targetId,
                    onSelected: editable
                        ? (_) => device.selectLegacyHardware(target.targetId)
                        : null,
                  ),
              ],
            ),
            const Text('仅用于旧固件首次升级。按主控模块标识、购买记录或板卡提供方确认；不要凭设备名、屏幕或版本猜测。'),
            if (hardware != null)
              Text(
                '手动选择：${hardware.label} · ${hardware.flashBytes ~/ 1048576} MB Flash / ${hardware.psramBytes ~/ 1048576} MB ${hardware.psramBus == 'octal' ? 'Octal' : 'Quad'} PSRAM（未自动验证）',
              ),
          ],
          const SizedBox(height: 12),
          Wrap(
            spacing: 12,
            runSpacing: 12,
            children: [
              OutlinedButton(
                key: Key(device.gateway is PreviewDeviceGateway
                    ? 'demo-online-firmware' : 'check-github'),
                onPressed: checkingReleases || !trustedHardware
                    ? null
                    : checkReleases,
                child: Text(checkingReleases ? '正在查询…' : '检查 GitHub 发布'),
              ),
            ],
          ),
          if (releaseMessage != null) Text(releaseMessage!),
          if (onlineFirmware case final release?) ...[
            Text('${release.name} · ${release.tag}'),
            Text('已选固件：${release.asset} · ${release.hardware.label}'),
            Text('发布于 ${release.publishedAt.toLocal()} · ${release.size} 字节'),
            Text('下载后核对清单 SHA-256、ELF 构建指纹及设备型号。'),
            const Text('下载完成后，选择 Wi-Fi 或蓝牙升级。'),
            OutlinedButton(
              key: const Key('download-firmware'),
              onPressed:
                  editable &&
                      trustedHardware &&
                      hardware.matches(release.hardware)
                  ? () => device.command('downloadFirmware', {
                      ...release.downloadArguments,
                      if (device.canSelectLegacyHardware)
                        'legacyHardwareTarget': hardware.targetId,
                    })
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
            !supported
                ? '固件导入与升级需要 Android 客户端。'
                : '选择传输方式后开始升级。设备重启并核对构建指纹后，才会确认完成。',
          ),
          if (snapshot.connected && !snapshot.bleFirmwareAvailable)
            const Text('当前连接不支持蓝牙升级，可使用 Wi-Fi。'),
          const SizedBox(height: 16),
          Text(file?.name ?? '尚未选择固件', key: const Key('firmware-file-name')),
          if (file != null) ...[
            const SizedBox(height: 8),
            Text('${file.size} 字节 · ${file.project}'),
            Text('构建指纹：${shortBuild(file.elfSha256)}'),
            Text('固件版本：${file.version}'),
            Text('固件目标：${file.targetId ?? '未识别，请使用带目标元信息的固件'}'),
            if (trustedHardware && !compatibleFile)
              const Text('固件与当前设备型号不匹配，请重新选择。'),
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
                    canStartWith('wifi') ? () => startFirmware(context) : null,
                icon: const Icon(Icons.system_update_alt),
                label: const Text('Wi-Fi 升级'),
              ),
              OutlinedButton.icon(
                key: const Key('start-firmware-ble'),
                onPressed: canStartWith('ble') && snapshot.bleFirmwareAvailable
                    ? () => startFirmware(context, overBle: true)
                    : null,
                icon: const Icon(Icons.bluetooth),
                label: const Text('蓝牙升级'),
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

  List<Widget> passportPage(BuildContext context) {
    final snapshot = device.snapshot;
    final busy = snapshot.passportBusy || device.busy;
    final operation = snapshot.passport;
    final passportSendReady =
        transferPreferenceReady &&
        (transferPreference == TransferPreference.ble
            ? operation['bleAvailable'] != false
            : operation['wifiAvailable'] != false ||
                  operation['bleAvailable'] == true);
    final raw = operation['json'];
    bool studentReadback = false;
    if (raw is String) {
      try {
        final data = jsonDecode(raw);
        studentReadback = data is Map && data['version'] == 2;
      } on FormatException {
        // The editor reports its own profile validation errors.
      }
    }
    Future<void> read() => device.command('readPassport');
    Future<void> save(String json, Uint8List bin) =>
        device.command('savePassport', {
          'json': json,
          'bin': bin,
          ...transferArguments,
        });
    void cancel() => device.command('cancelPassport');
    void open() => device.command('openDevicePage', {'appId': 6});
    final retry =
        snapshot.connected &&
            !device.busy &&
            !snapshot.settingsBusy &&
            !snapshot.transfer.active &&
            !snapshot.transfer.canConfirm
        ? () => device.command('retryPassport')
        : null;
    return [
      if (device.gateway.supported && snapshot.connected) mediaSendMode(),
      if (device.gateway is PreviewDeviceGateway)
        const Text('模拟设备：发送仅保存到演示内存，重置演示会清除；本地草稿单独保留。'),
      if (device.errorMessage != null)
        Text(
          device.errorMessage!,
          style: TextStyle(color: Theme.of(context).colorScheme.error),
        ),
      Align(
        alignment: Alignment.centerLeft,
        child: SegmentedButton<bool>(
          segments: const [
            ButtonSegment(value: false, label: Text('干员档案')),
            ButtonSegment(value: true, label: Text('学生证')),
          ],
          selected: {editStudentPassport},
          onSelectionChanged: busy
              ? null
              : (value) => setState(() => editStudentPassport = value.single),
        ),
      ),
      panel(
        context,
        child: editStudentPassport
            ? StudentPassportEditor(
                controller: studentPassportEditor,
                canOperate: device.canEditSettings,
                supported: snapshot.studentPassportSupported,
                 canSend: passportSendReady,
                busy: busy,
                operation: operation,
                onRead: read,
                onSave: save,
                onCancel: cancel,
                onRetry: retry,
                onOpen: open,
              )
            : PassportEditor(
                controller: passportEditor,
                canOperate: device.canEditSettings,
                 canSend: passportSendReady,
                busy: busy,
                operation: {
                  ...operation,
                  if (studentReadback) ...{
                    'json': null,
                    'revision': null,
                    if (operation['phase'] == 'done')
                      'message': '设备最近回读的是学生证。可切换到学生证读取资料。',
                  },
                  if (operation['kind'] == 'student') 'canRetry': false,
                },
                onRead: read,
                onSave: save,
                onCancel: cancel,
                onRetry: retry,
                onOpen: open,
              ),
      ),
    ];
  }

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

  Widget transferPreferencePanel(BuildContext context) => panel(
    context,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('传输方式', style: Theme.of(context).textTheme.titleLarge),
        SwitchListTile.adaptive(
          key: const Key('ble-compatibility-switch'),
          contentPadding: EdgeInsets.zero,
          title: const Text('蓝牙兼容模式'),
           subtitle: const Text('开启后使用蓝牙发送图片、动画和通行证；关闭后自动选择。'),
          value: transferPreference == TransferPreference.ble,
          onChanged: device.gateway.supported
              ? (enabled) => changeTransferPreference(
                  enabled ? TransferPreference.ble : TransferPreference.auto,
                )
              : null,
        ),
        if (device.snapshot.connected) ...[
          Text(prefersBlePhoto ? '单张照片：蓝牙' : '单张照片：Wi-Fi'),
          if (transferPreference == TransferPreference.auto &&
              device.snapshot.transferTransportReason != null)
            Text(device.snapshot.transferTransportReason!),
        ],
         const Text('固件升级可以单独选择传输方式。正在进行的任务会继续使用原链路。'),
        if (transferPreferenceMessage != null) Text(transferPreferenceMessage!),
      ],
    ),
  );

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
        Wrap(
          spacing: 24,
          runSpacing: 16,
          children: [
            if (device.snapshot.hardware?.trusted == true) ...[
              information('主控', 'ESP32-S3'),
              information(
                '硬件配置',
                device.snapshot.hardware!.label.replaceFirst('Ark ', ''),
              ),
              information(
                'Flash',
                '${device.snapshot.hardware!.flashBytes ~/ 1048576} MB',
              ),
              information(
                'PSRAM',
                '${device.snapshot.hardware!.psramBytes ~/ 1048576} MB · ${device.snapshot.hardware!.psramBus == 'octal' ? 'Octal' : 'Quad'}',
              ),
            ] else
              information('硬件配置', '尚未读取 / 旧固件不支持'),
          ],
        ),
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
    transferPreferencePanel(context),
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
