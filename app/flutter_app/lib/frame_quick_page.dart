import 'workbench_widgets.dart';
import 'forfilm_theme.dart';

import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;

import 'frame_codec.dart';
import 'frame_io.dart' as frame_io;
import 'quote_renderer.dart';

typedef PickFrameImages = Future<List<FramePhoto>> Function({
  required bool multiple,
});
typedef CaptureFrameImage = Future<FramePhoto?> Function();
typedef SendFrameFilm = Future<void> Function(Uint8List bytes, String name);
typedef CancelFrameSend = Future<void> Function();
typedef FetchFrameQuote = Future<FrameQuote> Function();

class FramePhoto {
  const FramePhoto({required this.name, required this.bytes});
  final String name;
  final Uint8List bytes;
}

class FrameQuote {
  const FrameQuote(this.text, this.author);
  final String text, author;
}

enum FrameQuickSection { photo, camera, quote, batch }

enum FrameBatchStatus { waiting, converting, sending, done, failed }

class FrameBatchItem {
  FrameBatchItem(this.id, FramePhoto photo)
    : name = photo.name,
      bytes = photo.bytes;
  final int id;
  final String name;
  Uint8List? bytes;
  String? outputName;
  FrameBatchStatus status = FrameBatchStatus.waiting;
}

const _photoOptions = FrameOptions(
  fit: FrameFit.contain,
  contrast: 1.2,
  dither: true,
);
const _cameraOptions = FrameOptions(
  fit: FrameFit.cover,
  contrast: 1.2,
  dither: true,
);

Future<FrameQuote> fetchFrameQuote() async {
  final client = http.Client();
  try {
    final response = await client
        .get(
          Uri.parse(
            'https://international.v1.hitokoto.cn/?c=d&c=h&c=k&c=i&encode=json',
          ),
        )
        .timeout(const Duration(seconds: 15));
    if (response.statusCode != 200) {
      throw Exception('HTTP ${response.statusCode}');
    }
    final data =
        jsonDecode(utf8.decode(response.bodyBytes)) as Map<String, dynamic>;
    final text = data['hitokoto'] as String?;
    if (text == null || text.trim().isEmpty) {
      throw const FormatException('一言内容为空');
    }
    final author = data['from_who'];
    final source = author == null || author == '' ? data['from'] : author;
    return FrameQuote(text, (source ?? '').toString());
  } finally {
    client.close();
  }
}

/// Retained by the parent app so returning to Frame keeps previews and the queue.
class FrameQuickController extends ChangeNotifier {
  static const maxBatchBytes = 64 * 1024 * 1024;
  FrameQuickSection? section;
  FrameResult? photoResult, cameraResult, quoteResult;
  FrameQuote? quote;
  final List<FrameBatchItem> batch = [];
  String status = '';
  String photoName = 'frame.film';
  String cameraName = 'camera.film';
  String quoteName = 'quote.film';
  String batchPrefix = 'batch_';
  String customText = '';
  String customAuthor = '';
  bool showCustom = false;
  bool quoteDither = true;
  bool? _ditherBeforeUpdate;
  bool busy = false;
  bool cancelling = false;
  int _epoch = 0, _nextId = 0;
  bool _disposed = false;

  bool get hasPendingBatch =>
      batch.any((item) => item.status != FrameBatchStatus.done);

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  void open(FrameQuickSection next) {
    if (busy) return;
    section = next;
    status = '';
    _notify();
  }

  void back() {
    if (busy) return;
    section = null;
    status = '';
    _notify();
  }

  Future<FrameResult> _convert(
    FramePhoto photo,
    FrameOptions options,
    int epoch,
  ) async {
    final codec = await ui.instantiateImageCodec(photo.bytes);
    late final ui.Image image;
    try {
      image = (await codec.getNextFrame()).image;
    } finally {
      codec.dispose();
    }
    final landscape = image.width > image.height;
    image.dispose();
    if (_epoch != epoch || _disposed) throw const FrameCancelledException();
    return convertFrame(
      photo.bytes,
      FrameOptions(
        fit: options.fit,
        contrast: options.contrast,
        dither: options.dither,
        quarterTurns: landscape ? 3 : 0,
      ),
      isCancelled: () => _epoch != epoch || _disposed,
    );
  }

  Future<void> pickPhoto(PickFrameImages pick) async {
    if (busy) return;
    final epoch = ++_epoch;
    busy = true;
    status = '正在选择照片…';
    _notify();
    try {
      final photos = await pick(multiple: false);
      if (_epoch != epoch || _disposed) return;
      if (photos.isEmpty) {
        status = '已取消选择';
        return;
      }
      status = '正在转换…';
      _notify();
      final result = await _convert(photos.first, _photoOptions, epoch);
      if (_epoch != epoch || _disposed) return;
      photoResult = result;
      status = '转换完成';
    } catch (error) {
      if (_epoch == epoch && !_disposed) status = '转换失败：$error';
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<void> capture(CaptureFrameImage captureImage) async {
    if (busy) return;
    final epoch = ++_epoch;
    busy = true;
    status = '正在打开相机…';
    _notify();
    try {
      final photo = await captureImage();
      if (_epoch != epoch || _disposed) return;
      if (photo == null) {
        status = '已取消拍照';
        return;
      }
      status = '正在转换…';
      _notify();
      final result = await _convert(photo, _cameraOptions, epoch);
      if (_epoch != epoch || _disposed) return;
      cameraResult = result;
      status = '转换完成';
    } catch (error) {
      if (_epoch == epoch && !_disposed) status = '转换失败：$error';
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<void> changeQuote({FetchFrameQuote? fetch, int? battery}) async {
    if (busy) return;
    final epoch = ++_epoch;
    busy = true;
    status = '正在获取一言…';
    _notify();
    try {
      final next = await (fetch ?? fetchFrameQuote)();
      if (_epoch != epoch || _disposed) return;
      final result = await _renderQuote(next, battery, epoch);
      if (_epoch != epoch || _disposed) return;
      quote = next;
      quoteResult = result;
      status = '一言已更新';
    } catch (error) {
      if (_epoch == epoch && !_disposed) status = '获取失败，请重试：$error';
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<FrameResult> _renderQuote(
    FrameQuote value,
    int? battery,
    int epoch,
  ) async {
    final png = await renderFrameQuote(
      value.text,
      value.author,
      battery: battery,
    );
    if (_epoch != epoch || _disposed) throw const FrameCancelledException();
    return convertFrame(
      png,
      FrameOptions(fit: FrameFit.contain, contrast: 1.2, dither: quoteDither),
      isCancelled: () => _epoch != epoch || _disposed,
    );
  }

  Future<void> setQuoteDither(bool enabled, {int? battery}) async {
    if (busy || enabled == quoteDither) return;
    final previous = quoteDither;
    _ditherBeforeUpdate = previous;
    quoteDither = enabled;
    final value = quote;
    if (value == null) {
      _ditherBeforeUpdate = null;
      _notify();
      return;
    }
    final epoch = ++_epoch;
    busy = true;
    status = '正在更新预览…';
    _notify();
    try {
      final result = await _renderQuote(value, battery, epoch);
      if (_epoch != epoch || _disposed) return;
      quoteResult = result;
      _ditherBeforeUpdate = null;
      status = '预览已更新';
    } catch (error) {
      if (_epoch == epoch && !_disposed) {
        quoteDither = previous;
        _ditherBeforeUpdate = null;
        status = '预览更新失败：$error';
      }
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<void> customQuote(String text, String author, {int? battery}) async {
    if (busy) return;
    text = text.trim();
    author = author.trim();
    if (text.isEmpty || text.runes.length > 100 || author.runes.length > 30) {
      status = '名言须为 1–100 字，作者不超过 30 字';
      _notify();
      return;
    }
    final epoch = ++_epoch;
    busy = true;
    status = '正在生成一言…';
    _notify();
    try {
      final value = FrameQuote(text, author);
      final result = await _renderQuote(value, battery, epoch);
      if (_epoch != epoch || _disposed) return;
      quote = value;
      quoteResult = result;
      status = '一言已生成';
    } catch (error) {
      if (_epoch == epoch && !_disposed) status = '生成失败：$error';
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<void> addBatch(PickFrameImages pick) async {
    if (busy) return;
    final epoch = ++_epoch;
    busy = true;
    status = '正在选择照片…';
    _notify();
    try {
      final photos = await pick(multiple: true);
      if (_epoch != epoch || _disposed) return;
      var retained = batch.fold<int>(
        0,
        (sum, item) => sum + (item.bytes?.length ?? 0),
      );
      var added = 0;
      var skipped = 0;
      for (final photo in photos) {
        if (photo.bytes.isEmpty ||
            photo.bytes.length > 8 * 1024 * 1024 ||
            retained + photo.bytes.length > maxBatchBytes) {
          skipped++;
          continue;
        }
        batch.add(FrameBatchItem(++_nextId, photo));
        retained += photo.bytes.length;
        added++;
      }
      status = photos.isEmpty
          ? '已取消选择'
          : '已添加 $added 张照片${skipped == 0 ? '' : '，$skipped 张超出单张 8 MiB 或片单 64 MiB 限制'}';
    } catch (error) {
      if (_epoch == epoch && !_disposed) status = '选择失败：$error';
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  void removeBatch(int id) {
    if (busy) return;
    final index = batch.indexWhere((item) => item.id == id);
    if (index < 0) return;
    batch[index].bytes = null;
    batch.removeAt(index);
    _notify();
  }

  void clearBatch() {
    if (busy) return;
    for (final item in batch) {
      item.bytes = null;
    }
    batch.clear();
    status = '片单已清空';
    _notify();
  }

  void clearCurrent() {
    if (busy) return;
    switch (section) {
      case FrameQuickSection.photo:
        photoResult = null;
        break;
      case FrameQuickSection.camera:
        cameraResult = null;
        break;
      case FrameQuickSection.quote:
        quoteResult = null;
        quote = null;
        break;
      case FrameQuickSection.batch:
      case null:
        return;
    }
    status = '已清除';
    _notify();
  }

  static String batchName(String prefix, int order, String original) {
    final trimmedPrefix = prefix.trim();
    final base = trimmedPrefix.isEmpty
        ? original.replaceFirst(RegExp(r'\.[^./\\]+$'), '')
        : '$trimmedPrefix${order.toString().padLeft(2, '0')}';
    return filmName(base, 'batch.film');
  }

  static String filmName(String value, String fallback) {
    var result = value.trim();
    if (result.isEmpty) result = fallback;
    if (!result.toLowerCase().endsWith('.film')) result += '.film';
    return result;
  }

  Future<void> send(
    FrameResult? result,
    String name,
    SendFrameFilm sendFilm,
  ) async {
    if (busy || result == null) return;
    final epoch = ++_epoch;
    busy = true;
    status = '正在发送…';
    _notify();
    try {
      await sendFilm(result.film, filmName(name, 'frame.film'));
      if (_epoch == epoch && !_disposed) status = '发送完成';
    } catch (error) {
      if (_epoch == epoch && !_disposed) status = '发送失败：$error';
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<void> sendBatch(String prefix, SendFrameFilm sendFilm) async {
    if (busy || !hasPendingBatch) return;
    final epoch = ++_epoch;
    busy = true;
    status = '正在批量上传…';
    _notify();
    try {
      for (final item in batch.where(
        (value) => value.status != FrameBatchStatus.done,
      )) {
        if (_epoch != epoch || _disposed) return;
        final bytes = item.bytes;
        if (bytes == null) continue;
        item.status = FrameBatchStatus.converting;
        _notify();
        final result = await _convert(
          FramePhoto(name: item.name, bytes: bytes),
          _photoOptions,
          epoch,
        );
        if (_epoch != epoch || _disposed) return;
        item.status = FrameBatchStatus.sending;
        item.outputName ??= batchName(prefix, item.id, item.name);
        status = '正在发送 ${item.name}…';
        _notify();
        try {
          await sendFilm(result.film, item.outputName!);
        } catch (_) {
          if (_epoch == epoch && !_disposed) {
            item.status = FrameBatchStatus.failed;
          }
          rethrow;
        }
        if (_epoch != epoch || _disposed) return;
        item.status = FrameBatchStatus.done;
        item.bytes = null;
        _notify();
      }
      if (_epoch == epoch && !_disposed) status = '批量上传完成';
    } catch (error) {
      if (_epoch == epoch && !_disposed) {
        for (final item in batch) {
          if (item.status == FrameBatchStatus.converting) {
            item.status = FrameBatchStatus.failed;
          }
        }
        status = '批量上传中断：$error';
      }
    } finally {
      if (_epoch == epoch && !_disposed) {
        busy = false;
        _notify();
      }
    }
  }

  Future<void> cancel(CancelFrameSend cancelSend) async {
    if (!busy || cancelling) return;
    _epoch++;
    cancelling = true;
    if (_ditherBeforeUpdate != null) {
      quoteDither = _ditherBeforeUpdate!;
      _ditherBeforeUpdate = null;
    }
    for (final item in batch) {
      if (item.status == FrameBatchStatus.converting ||
          item.status == FrameBatchStatus.sending) {
        item.status = FrameBatchStatus.failed;
      }
    }
    status = '正在取消…';
    _notify();
    try {
      await cancelSend();
      status = '已取消，可重试未完成项';
    } catch (error) {
      status = '取消传输失败：$error';
    } finally {
      cancelling = false;
      busy = false;
      _notify();
    }
  }

  @override
  void dispose() {
    _epoch++;
    _disposed = true;
    for (final item in batch) {
      item.bytes = null;
    }
    batch.clear();
    photoResult = null;
    cameraResult = null;
    quoteResult = null;
    super.dispose();
  }
}

class FrameQuickPage extends StatefulWidget {
  const FrameQuickPage({
    super.key,
    required this.controller,
    required this.pickImages,
    required this.captureImage,
    required this.sendFilm,
    required this.cancelSend,
    required this.canSend,
    this.battery,
    this.quoteFetcher,
  });
  final FrameQuickController controller;
  final PickFrameImages pickImages;
  final CaptureFrameImage captureImage;
  final SendFrameFilm sendFilm;
  final CancelFrameSend cancelSend;
  final bool canSend;
  final int? battery;
  final FetchFrameQuote? quoteFetcher;

  @override
  State<FrameQuickPage> createState() => _FrameQuickPageState();
}

class _FrameQuickPageState extends State<FrameQuickPage> {
  final _photoName = TextEditingController(text: 'frame.film');
  final _cameraName = TextEditingController(text: 'camera.film');
  final _quoteName = TextEditingController(text: 'quote.film');
  final _batchPrefix = TextEditingController(text: 'batch_');
  final _customText = TextEditingController();
  final _customAuthor = TextEditingController();

  @override
  void initState() {
    super.initState();
    final c = widget.controller;
    _photoName.text = c.photoName;
    _cameraName.text = c.cameraName;
    _quoteName.text = c.quoteName;
    _batchPrefix.text = c.batchPrefix;
    _customText.text = c.customText;
    _customAuthor.text = c.customAuthor;
    _photoName.addListener(() => c.photoName = _photoName.text);
    _cameraName.addListener(() => c.cameraName = _cameraName.text);
    _quoteName.addListener(() => c.quoteName = _quoteName.text);
    _batchPrefix.addListener(() => c.batchPrefix = _batchPrefix.text);
    _customText.addListener(() => c.customText = _customText.text);
    _customAuthor.addListener(() => c.customAuthor = _customAuthor.text);
  }

  @override
  void dispose() {
    _photoName.dispose();
    _cameraName.dispose();
    _quoteName.dispose();
    _batchPrefix.dispose();
    _customText.dispose();
    _customAuthor.dispose();
    super.dispose();
  }

  void _open(FrameQuickSection section) {
    widget.controller.open(section);
    if (section == FrameQuickSection.quote &&
        widget.controller.quoteResult == null) {
      widget.controller.changeQuote(
        fetch: widget.quoteFetcher,
        battery: widget.battery,
      );
    }
  }

  Widget _preview(FrameResult? result, {required String emptyText}) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      const Text('预览'),
      const SizedBox(height: 8),
      Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 360),
          child: AspectRatio(
            aspectRatio: 2 / 3,
            child: DecoratedBox(
              decoration: BoxDecoration(
                color: Theme.of(context).colorScheme.surfaceContainerHighest,
                border: Border.all(
                  color: Theme.of(context).colorScheme.outlineVariant,
                ),
              ),
              child: result == null
                  ? Center(child: Text(emptyText))
                  : Image.memory(result.png, fit: BoxFit.contain),
            ),
          ),
        ),
      ),
    ],
  );

  Widget _sendArea(FrameResult? result, TextEditingController name) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      TextField(
        controller: name,
        decoration: const InputDecoration(labelText: '文件名'),
      ),
      const SizedBox(height: 8),
      FilledButton.icon(
        onPressed: widget.controller.busy || !widget.canSend || result == null
            ? null
            : () => widget.controller.send(result, name.text, widget.sendFilm),
        icon: const Icon(Icons.send_outlined),
        label: const Text('发送到设备'),
      ),
      if (result != null && frame_io.canDownloadFrame)
        TextButton(
          onPressed: widget.controller.busy
              ? null
              : () => frame_io.downloadFrame(
                  result.film,
                  FrameQuickController.filmName(name.text, 'frame.film'),
                ),
          child: const Text('下载 Film'),
        ),
    ],
  );

  Widget _single(bool camera) {
    final c = widget.controller;
    final result = camera ? c.cameraResult : c.photoResult;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        FilledButton.icon(
          onPressed: c.busy
              ? null
              : camera
              ? () => c.capture(widget.captureImage)
              : () => c.pickPhoto(widget.pickImages),
          icon: Icon(
            camera
                ? Icons.camera_alt_outlined
                : Icons.add_photo_alternate_outlined,
          ),
          label: Text(camera ? '打开相机' : '选择照片'),
        ),
        if (result != null)
          TextButton(
            onPressed: c.busy ? null : c.clearCurrent,
            child: const Text('清除预览'),
          ),
        Text(camera ? '拍照后自动转换为 film 并显示' : '选择后自动转换为 6 色 film 并预览'),
        const SizedBox(height: 16),
        _preview(result, emptyText: camera ? '请拍摄照片以生成预览' : '请选择照片'),
        const SizedBox(height: 16),
        _sendArea(result, camera ? _cameraName : _photoName),
      ],
    );
  }

  Widget _quote() {
    final c = widget.controller;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Wrap(
          spacing: 8,
          children: [
            FilledButton(
              onPressed: c.busy
                  ? null
                  : () => c.changeQuote(
                      fetch: widget.quoteFetcher,
                      battery: widget.battery,
                    ),
              child: const Text('换一句'),
            ),
            OutlinedButton(
              onPressed: c.busy
                  ? null
                  : () => setState(() => c.showCustom = !c.showCustom),
              child: const Text('自定义'),
            ),
          ],
        ),
        if (c.showCustom) ...[
          TextField(
            controller: _customText,
            maxLength: 100,
            maxLines: 3,
            decoration: const InputDecoration(labelText: '名言内容'),
          ),
          TextField(
            controller: _customAuthor,
            maxLength: 30,
            decoration: const InputDecoration(labelText: '作者（可选）'),
          ),
          FilledButton(
            onPressed: c.busy
                ? null
                : () => c.customQuote(
                    _customText.text,
                    _customAuthor.text,
                    battery: widget.battery,
                  ),
            child: const Text('生成'),
          ),
        ],
        if (c.quote != null)
          Text(
            '「${c.quote!.text}」${c.quote!.author.isEmpty ? '' : '  —— ${c.quote!.author}'}',
          ),
        if (c.quoteResult != null)
          TextButton(
            onPressed: c.busy ? null : c.clearCurrent,
            child: const Text('清除预览'),
          ),
        SwitchListTile(
          contentPadding: EdgeInsets.zero,
          title: const Text('抖动'),
          value: c.quoteDither,
          onChanged: c.busy
              ? null
              : (enabled) => c.setQuoteDither(enabled, battery: widget.battery),
        ),
        const SizedBox(height: 16),
        _preview(c.quoteResult, emptyText: '请获取一言或输入自定义内容以生成预览'),
        const SizedBox(height: 16),
        _sendArea(c.quoteResult, _quoteName),
      ],
    );
  }

  Widget _batch() {
    final c = widget.controller;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        FilledButton.icon(
          onPressed: c.busy ? null : () => c.addBatch(widget.pickImages),
          icon: const Icon(Icons.add_photo_alternate_outlined),
          label: const Text('一次选多张'),
        ),
        const Text('可多选，按顺序逐张转换并写入设备。当前 WiFi 传输每张完成后都会显示。'),
        const SizedBox(height: 16),
        Text('待传片单 · ${c.batch.length} 张'),
        if (c.batch.isEmpty) const Text('还没有选择照片'),
        for (final item in c.batch)
          ListTile(
            contentPadding: EdgeInsets.zero,
            title: Text(item.name),
            subtitle: Text(switch (item.status) {
              FrameBatchStatus.waiting => '等待',
              FrameBatchStatus.converting => '转换中',
              FrameBatchStatus.sending => '发送中',
              FrameBatchStatus.done => '已完成',
              FrameBatchStatus.failed => '失败，可重试',
            }),
            trailing: IconButton(
              tooltip: '移除 ${item.name}',
              onPressed: c.busy ? null : () => c.removeBatch(item.id),
              icon: const Icon(Icons.delete_outline),
            ),
          ),
        if (c.batch.isNotEmpty)
          TextButton(
            onPressed: c.busy ? null : c.clearBatch,
            child: const Text('清空片单'),
          ),
        TextField(
          controller: _batchPrefix,
          decoration: const InputDecoration(labelText: '文件名前缀'),
        ),
        const SizedBox(height: 8),
        FilledButton.icon(
          onPressed: c.busy || !widget.canSend || !c.hasPendingBatch
              ? null
              : () => c.sendBatch(_batchPrefix.text, widget.sendFilm),
          icon: const Icon(Icons.send_outlined),
          label: const Text('开始批量上传'),
        ),
      ],
    );
  }

  Widget _entryGrid() => LayoutBuilder(
    builder: (context, constraints) {
      final width = constraints.maxWidth;
      final columns = width >= 840
          ? 4
          : width >= 420
          ? 2
          : 1;
      const gap = 12.0;
      final cardWidth = (width - gap * (columns - 1)) / columns;
      const entries = <(FrameQuickSection, String, String, IconData)>[
        (FrameQuickSection.photo, '拾光', '从手机相册挑选', Icons.photo_outlined),
        (FrameQuickSection.camera, '定影', '拍一张直接上屏', Icons.camera_alt_outlined),
        (FrameQuickSection.quote, '一言', '名言金句每日换', Icons.format_quote_outlined),
        (
          FrameQuickSection.batch,
          '批量上传',
          '一组照片一次传完',
          Icons.photo_library_outlined,
        ),
      ];
      return Wrap(
        spacing: gap,
        runSpacing: gap,
        children: [
          for (final (section, title, subtitle, icon) in entries)
            SizedBox(
              width: cardWidth,
              height: 142,
              child: WorkbenchPanel(
                padding: EdgeInsets.zero,
                color: Theme.of(context).brightness == Brightness.dark
                    ? null
                    : switch (section) {
                        FrameQuickSection.photo => ForFilmColors.lemon,
                        FrameQuickSection.camera => ForFilmColors.coral,
                        FrameQuickSection.quote => ForFilmColors.sky,
                        _ => ForFilmColors.mint,
                      },
                child: InkWell(
                  onTap: widget.controller.busy ? null : () => _open(section),
                  child: Padding(
                    padding: const EdgeInsets.all(12),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(icon, size: 32),
                        const SizedBox(height: 8),
                        Text(
                          title,
                          style: Theme.of(context).textTheme.titleMedium,
                        ),
                        const SizedBox(height: 4),
                        Text(
                          subtitle,
                          textAlign: TextAlign.center,
                          style: Theme.of(context).textTheme.bodySmall,
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
        ],
      );
    },
  );

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: widget.controller,
    builder: (context, _) {
      final c = widget.controller;
      return Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (c.section == null)
              _entryGrid()
            else ...[
              TextButton.icon(
                onPressed: c.busy ? null : c.back,
                icon: const Icon(Icons.arrow_back),
                label: const Text('返回'),
              ),
              Text(switch (c.section!) {
                FrameQuickSection.photo => '拾光',
                FrameQuickSection.camera => '定影',
                FrameQuickSection.quote => '一言',
                FrameQuickSection.batch => '批量上传',
              }, style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 12),
              switch (c.section!) {
                FrameQuickSection.photo => _single(false),
                FrameQuickSection.camera => _single(true),
                FrameQuickSection.quote => _quote(),
                FrameQuickSection.batch => _batch(),
              },
            ],
            if (c.busy) const LinearProgressIndicator(),
            if (c.busy && !c.cancelling)
              TextButton(
                onPressed: () => c.cancel(widget.cancelSend),
                child: const Text('取消'),
              ),
            if (c.status.isNotEmpty)
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: Text(c.status),
              ),
          ],
        ),
      );
    },
  );
}
