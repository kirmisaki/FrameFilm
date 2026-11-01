import 'package:flutter/material.dart';

import 'device_gateway.dart';

/// Browses an independent, read-only SD catalog.
class DeviceFilesPage extends StatelessWidget {
  const DeviceFilesPage({super.key, required this.controller});
  final DeviceController controller;

  Future<void> _delete(BuildContext context, Map entry) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('删除设备文件？'),
        content: Text('“${entry['name']}”将从设备 SD 卡删除。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('删除'),
          ),
        ],
      ),
    );
    if (confirmed == true) {
      await controller.command('deleteDeviceFile', {
        'id': entry['id'],
        'name': entry['name'],
      });
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: controller,
    builder: (context, _) {
      final snapshot = controller.snapshot;
      final files = snapshot.files;
      final entries = (files['entries'] as List? ?? const [])
          .whereType<Map>()
          .toList();
      final available = controller.canEditSettings;
      final knownDirectory =
          files['directory'] == 'film' || files['directory'] == 'animation';
      final directory = switch (files['directory']) {
        'film' => '图片 / film',
        'animation' => '动画 / animation',
        _ => '设备当前目录',
      };
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('设备文件', style: Theme.of(context).textTheme.headlineSmall),
          const SizedBox(height: 8),
          Text(snapshot.connected ? directory : '连接设备后读取 SD 卡中的 film 文件'),
          const SizedBox(height: 16),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              FilledButton.tonalIcon(
                onPressed: available
                    ? () => controller.command('listDeviceFiles')
                    : null,
                icon: const Icon(Icons.refresh),
                label: const Text('刷新列表'),
              ),
              OutlinedButton(
                onPressed: available
                    ? () => controller.command('setDeviceFileDirectory', {
                        'appId': 0,
                      })
                    : null,
                child: const Text('图片目录'),
              ),
              OutlinedButton(
                onPressed: available
                    ? () => controller.command('setDeviceFileDirectory', {
                        'appId': 3,
                      })
                    : null,
                child: const Text('动画目录'),
              ),
            ],
          ),
          const SizedBox(height: 12),
          const Text(
            '浏览图片或动画目录只读取 SD 卡，不切换 app，也不刷新设备画面。点击“显示”才会切换到对应 app。删除需设备当前已打开对应 app。旧固件需升级后使用只读浏览。',
          ),
          if (snapshot.connected && !knownDirectory)
            const Padding(
              padding: EdgeInsets.only(top: 8),
              child: Text('显示文件前，请先选择图片目录或动画目录，确认文件所属 app。'),
            ),
          if (controller.busy || snapshot.settingsBusy) ...[
            const SizedBox(height: 16),
            const LinearProgressIndicator(),
          ],
          if (controller.errorMessage != null) ...[
            const SizedBox(height: 12),
            Text(
              controller.errorMessage!,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
          ],
          if (snapshot.settingsMessage != null)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Text(snapshot.settingsMessage!),
            ),
          const SizedBox(height: 16),
          if (entries.isEmpty)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 32),
              child: Column(
                children: [
                  const Icon(Icons.folder_open, size: 40),
                  const SizedBox(height: 12),
                  Text(
                    !snapshot.connected
                        ? '尚未连接设备'
                        : files['loaded'] == true
                        ? '当前目录没有文件'
                        : controller.automaticFilesLoading
                        ? '正在自动读取设备文件…'
                        : controller.automaticFilesPending
                        ? '等待设备就绪后自动读取文件…'
                        : controller.automaticFilesFailed
                        ? '自动读取未完成，请点击刷新重试'
                        : '点击刷新读取设备文件',
                  ),
                ],
              ),
            ),
          for (final entry in entries)
            Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.insert_drive_file_outlined),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Text(
                            entry['name'] as String? ?? '',
                            style: Theme.of(context).textTheme.titleSmall,
                          ),
                        ),
                        if (files['currentId'] == entry['id'])
                          const Chip(label: Text('当前 ID')),
                      ],
                    ),
                    const SizedBox(height: 8),
                    Wrap(
                      spacing: 8,
                      children: [
                        TextButton.icon(
                          onPressed: available && knownDirectory
                              ? () => controller.command('displayDeviceFile', {
                                  'id': entry['id'],
                                  'name': entry['name'],
                                })
                              : null,
                          icon: const Icon(Icons.slideshow),
                          label: const Text('显示'),
                        ),
                        TextButton.icon(
                          onPressed: available && files['canDelete'] == true
                              ? () => _delete(context, entry)
                              : null,
                          icon: const Icon(Icons.delete_outline),
                          label: const Text('删除'),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          const SizedBox(height: 12),
          const Text('目录为读取时的快照。显示命令完成不代表电子纸已完成刷新；删除前会按文件名重新核对设备活跃列表。'),
        ],
      );
    },
  );
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
