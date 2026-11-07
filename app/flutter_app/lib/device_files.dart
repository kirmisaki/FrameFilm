import 'package:flutter/material.dart';

import 'device_gateway.dart';

/// Browses an independent, read-only SD catalog.
class DeviceFilesPage extends StatefulWidget {
  const DeviceFilesPage({super.key, required this.controller});
  final DeviceController controller;

  @override
  State<DeviceFilesPage> createState() => _DeviceFilesPageState();
}

class _DeviceFilesPageState extends State<DeviceFilesPage> {
  final _scroll = ScrollController();
  String? _deviceId, _directory;
  Map? _selection;
  List<Map> _entries = [];
  DeviceController get controller => widget.controller;

  @override
  void initState() {
    super.initState();
    controller.addListener(_catalogChanged);
    _syncCatalog();
  }

  @override
  void didUpdateWidget(covariant DeviceFilesPage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.controller != controller) {
      oldWidget.controller.removeListener(_catalogChanged);
      controller.addListener(_catalogChanged);
      _deviceId = null;
      _directory = null;
      _selection = null;
      _entries = [];
      _syncCatalog();
    }
  }

  @override
  void dispose() {
    controller.removeListener(_catalogChanged);
    _scroll.dispose();
    super.dispose();
  }

  void _catalogChanged() => setState(_syncCatalog);

  void _syncCatalog() {
    final snapshot = controller.snapshot;
    final directory = snapshot.files['directory'] as String?;
    if (!snapshot.connected ||
        _deviceId != snapshot.deviceId ||
        _directory != directory) {
      _entries = [];
      _selection = null;
      if (_scroll.hasClients) _scroll.jumpTo(0);
    }
    _deviceId = snapshot.deviceId;
    _directory = directory;
    if (!snapshot.connected) return;
    if (snapshot.files['loaded'] != true) {
      _selection = null;
      return;
    }
    _entries = (snapshot.files['entries'] as List? ?? const [])
        .whereType<Map>()
        .toList();
    if (!_entries.any(
      (entry) =>
          entry['id'] == _selection?['id'] &&
          entry['name'] == _selection?['name'],
    )) {
      _selection = null;
    }
  }

  void _message(String text) {
    final messenger = ScaffoldMessenger.of(context);
    messenger.hideCurrentSnackBar();
    messenger.showSnackBar(SnackBar(content: Text(text)));
  }

  Future<void> _command(
    String method, [
    Map<String, Object?>? arguments,
  ]) async {
    await controller.command(method, arguments);
    if (!mounted) return;
    final message =
        controller.errorMessage ?? controller.snapshot.settingsMessage;
    if (message?.trim().isNotEmpty == true) _message(message!);
  }

  void _refresh() {
    setState(() => _selection = null);
    _command('listDeviceFiles');
  }

  void _operate({required bool delete}) {
    final entry = _selection;
    if (entry == null) {
      _message(delete ? '请先选择要删除的文件' : '请先选择要显示的文件');
      return;
    }
    if (delete) {
      _delete(context, entry);
    } else if (_directory == 'film' || _directory == 'animation') {
      _command('displayDeviceFile', {'id': entry['id'], 'name': entry['name']});
    } else {
      _message('请从连接页选择图片或动画目录后重试');
    }
  }

  Future<void> _delete(BuildContext context, Map entry) async {
    final files = controller.snapshot.files;
    final canDelete = files['canDelete'] == true;
    final knownDirectory =
        files['directory'] == 'film' || files['directory'] == 'animation';
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(canDelete ? '删除设备文件？' : '请先打开对应 app'),
        content: Text(
          canDelete
              ? '“${entry['name']}”将从设备 SD 卡删除。'
              : knownDirectory
              ? '设备尚未打开此目录对应的 app，暂时不能删除。“显示此文件”会切换设备画面；显示完成后，请再次点击删除。也可在设备上打开对应 app，再刷新列表。'
              : '请先选择图片目录或动画目录，再在设备打开对应 app 并刷新列表后删除。',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          if (canDelete || knownDirectory)
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: Text(canDelete ? '删除' : '显示此文件'),
            ),
        ],
      ),
    );
    if (confirmed == true) {
      await _command(canDelete ? 'deleteDeviceFile' : 'displayDeviceFile', {
        'id': entry['id'],
        'name': entry['name'],
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final available = controller.canEditSettings;
    final files = controller.snapshot.files;
    final ready = available && files['loaded'] == true;
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            const Icon(Icons.movie_filter_outlined),
            const SizedBox(width: 8),
            Expanded(child: Text('FILM设置', style: theme.textTheme.titleLarge)),
            IconButton(
              tooltip: '刷新',
              onPressed: available ? _refresh : null,
              icon: const Icon(Icons.refresh),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Container(
          decoration: BoxDecoration(
            border: Border.all(color: theme.colorScheme.outline),
            borderRadius: BorderRadius.circular(4),
          ),
          clipBehavior: Clip.antiAlias,
          child: _entries.isEmpty
              ? Padding(
                  padding: const EdgeInsets.all(24),
                  child: Center(
                    child: Text(
                      controller.busy || controller.snapshot.settingsBusy
                          ? '正在加载...'
                          : '暂无文件',
                    ),
                  ),
                )
              : SizedBox(
                  key: const Key('device-files-list-viewport'),
                  height: (_entries.length * 48.0).clamp(48.0, 180.0),
                  child: Scrollbar(
                    controller: _scroll,
                    thumbVisibility: true,
                    child: ListView.builder(
                      key: const Key('device-files-list'),
                      controller: _scroll,
                      primary: false,
                      itemExtent: 48,
                      itemCount: _entries.length,
                      itemBuilder: (context, index) {
                        final entry = _entries[index];
                        final name = entry['name'] as String? ?? '';
                        final selected = _selection?['id'] == entry['id'];
                        final current = files['currentId'] == entry['id'];
                        void select() => setState(
                          () => _selection = selected ? null : entry,
                        );
                        return Material(
                          key: ValueKey('device-file-${entry['id']}'),
                          color: current
                              ? theme.colorScheme.tertiaryContainer
                              : theme.colorScheme.surfaceContainerLow,
                          child: InkWell(
                            onTap: ready ? select : null,
                            child: Row(
                              children: [
                                Checkbox(
                                  value: selected,
                                  onChanged: ready ? (_) => select() : null,
                                ),
                                Container(
                                  padding: const EdgeInsets.symmetric(
                                    horizontal: 6,
                                    vertical: 2,
                                  ),
                                  decoration: BoxDecoration(
                                    color: theme
                                        .colorScheme
                                        .surfaceContainerHighest,
                                    borderRadius: BorderRadius.circular(4),
                                  ),
                                  child: Text(
                                    '${entry['id']}',
                                    style: theme.textTheme.labelSmall?.copyWith(
                                      color: theme.colorScheme.onSurface,
                                    ),
                                  ),
                                ),
                                const SizedBox(width: 8),
                                Expanded(
                                  child: Tooltip(
                                    message: name,
                                    child: Text(
                                      name,
                                      maxLines: 1,
                                      overflow: TextOverflow.ellipsis,
                                      style: current
                                          ? TextStyle(
                                              color: theme
                                                  .colorScheme
                                                  .onTertiaryContainer,
                                            )
                                          : null,
                                    ),
                                  ),
                                ),
                                const SizedBox(width: 12),
                              ],
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                ),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: OutlinedButton.icon(
                onPressed: available ? () => _operate(delete: true) : null,
                icon: const Icon(Icons.delete_outline),
                label: const Text('删除文件'),
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: OutlinedButton.icon(
                onPressed: available ? () => _operate(delete: false) : null,
                icon: const Icon(Icons.check_circle_outline),
                label: const Text('选择显示'),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

/// Actual SD statistics, with unknown and partial reads kept distinct from zero.
class DeviceStorageSummary extends StatelessWidget {
  const DeviceStorageSummary({
    super.key,
    required this.controller,
    this.onBrowseDirectory,
    this.embedded = false,
  });
  final DeviceController controller;
  final ValueChanged<int>? onBrowseDirectory;
  final bool embedded;

  static String _bytes(Object? value) {
    if (value is! num) return '未读取';
    if (value >= 1024 * 1024 * 1024)
      return '${(value / (1024 * 1024 * 1024)).toStringAsFixed(2)} GiB';
    return '${(value / (1024 * 1024)).toStringAsFixed(1)} MiB';
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: controller,
    builder: (context, _) {
      final raw = controller.snapshot.files['storage'];
      final storage = raw is Map ? raw : const {};
      final phase = storage['phase'];
      String count(String key) =>
          storage[key] is num ? '${storage[key]} 个' : '未读取';
      final available = controller.canEditSettings;
      final content = Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Icon(Icons.sd_storage_outlined),
              const SizedBox(width: 8),
              const Expanded(child: Text('设备 SD 卡')),
              IconButton(
                tooltip: '刷新 SD 卡统计',
                onPressed: available
                    ? () => controller.command('refreshDeviceStorage')
                    : null,
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
          Text(
            '总容量 ${_bytes(storage['totalBytes'])} · 剩余 ${_bytes(storage['freeBytes'])}',
          ),
          const SizedBox(height: 8),
          Wrap(
            spacing: 12,
            runSpacing: 8,
            children: [
              TextButton.icon(
                onPressed: available && onBrowseDirectory != null
                    ? () => onBrowseDirectory!(0)
                    : null,
                icon: const Icon(Icons.image_outlined),
                label: Text('图片 ${count('filmCount')} · 查看'),
              ),
              TextButton.icon(
                onPressed: available && onBrowseDirectory != null
                    ? () => onBrowseDirectory!(3)
                    : null,
                icon: const Icon(Icons.animation),
                label: Text('动画 ${count('animationCount')} · 查看'),
              ),
            ],
          ),
          if (phase == 'loading') const LinearProgressIndicator(),
          Text(
            storage['message'] as String? ??
                (controller.snapshot.connected
                    ? '尚未读取 SD 卡统计'
                    : '连接设备后读取 SD 卡统计'),
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      );
      return embedded
          ? content
          : Card(
              child: Padding(padding: const EdgeInsets.all(16), child: content),
            );
    },
  );
}
