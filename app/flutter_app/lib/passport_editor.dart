import 'dart:typed_data';
import 'dart:convert';

import 'package:flutter/material.dart';

import 'passport_profile.dart';
import 'passport_avatar_editor.dart';
import 'passport_storage.dart' as storage;

class PassportEditorController {
  PassportProfile? profile;
  PassportAvatarEdit? avatarEdit;
  Object? consumedRevision;
  bool initialized = false;
  Future<void> saving = Future.value();
}

class PassportEditor extends StatefulWidget {
  const PassportEditor({
    super.key,
    required this.canOperate,
    required this.busy,
    required this.operation,
    required this.onRead,
    required this.onSave,
    required this.onCancel,
    required this.onOpen,
    this.onRetry,
    this.loadDraft,
    this.saveDraft,
    this.pickAvatar,
    this.controller,
  });
  final PassportEditorController? controller;
  final bool canOperate, busy;
  final Map<String, Object?> operation;
  final Future<void> Function() onRead;
  final Future<void> Function(String, Uint8List) onSave;
  final VoidCallback onCancel, onOpen;
  final Future<void> Function()? onRetry;
  final Future<String?> Function()? loadDraft;
  final Future<void> Function(String)? saveDraft;
  final Future<Uint8List?> Function()? pickAvatar;
  @override
  State<PassportEditor> createState() => _PassportEditorState();
}

class _PassportEditorState extends State<PassportEditor> {
  final _fields = {
    for (final name in ['codename', 'number', 'affiliation', 'signature'])
      name: TextEditingController(),
  };
  bool _codeUnset = false, _numberUnset = false, _working = false;
  String? _avatar;
  String _avatarMode = 'mono';
  PassportAvatarEdit? _avatarEdit;
  String _status = '';
  PassportRaster? _raster;
  int _rasterGeneration = -1;
  int _generation = 0, _editRevision = 0;
  Object? _seenRevision;
  Future<void> _saving = Future.value();
  bool get _locked => widget.busy || _working;
  PassportProfile get _profile => PassportProfile(
    codename: _fields['codename']!.text,
    number: _fields['number']!.text,
    affiliation: _fields['affiliation']!.text,
    signature: _fields['signature']!.text,
    codenameUnset: _codeUnset,
    numberUnset: _numberUnset,
    avatar: _avatar,
    avatarMode: _avatarMode,
  );
  @override
  void initState() {
    super.initState();
    _seenRevision = widget.operation['revision'];
    final controller = widget.controller;
    final pendingRead =
        controller != null &&
        controller.initialized &&
        controller.consumedRevision != _seenRevision &&
        widget.operation['json'] is String;
    if (controller != null && !controller.initialized) {
      controller.initialized = true;
      controller.consumedRevision = _seenRevision;
    }
    final saved = widget.controller?.profile;
    if (saved != null) {
      _replace(saved, edit: widget.controller?.avatarEdit);
    } else {
      if (!pendingRead) _load();
      _refresh();
    }
    if (pendingRead && _seenRevision != null) {
      _acceptRead(widget.operation['json'] as String, _seenRevision!);
    }
  }

  Future<void> _load() async {
    final revision = _editRevision;
    try {
      final raw = await (widget.loadDraft ?? storage.loadPassportDraft)();
      if (raw != null) {
        final value = jsonDecode(raw);
        final wrapper = value is Map && value['draftVersion'] == 2;
        final profile = PassportProfile.fromJson(
          wrapper ? jsonEncode(value['profile']) : raw,
        );
        final edit = wrapper && value['avatarEditor'] != null
            ? PassportAvatarEdit.fromJson(value['avatarEditor'] as Map)
            : null;
        final avatar = await validatePassportAvatar(profile);
        avatar?.dispose();
        if (mounted && revision == _editRevision) _replace(profile, edit: edit);
      }
    } catch (e) {
      if (mounted) setState(() => _status = '本地草稿读取失败：$e');
    }
  }

  @override
  void didUpdateWidget(covariant PassportEditor oldWidget) {
    super.didUpdateWidget(oldWidget);
    final revision = widget.operation['revision'];
    final raw = widget.operation['json'];
    if (revision != null && revision != _seenRevision) {
      _seenRevision = revision;
      if (raw is String) _acceptRead(raw, revision);
    }
  }

  Future<void> _acceptRead(String raw, Object revision) async {
    final editRevision = _editRevision;
    try {
      final profile = PassportProfile.fromJson(raw);
      final avatar = await validatePassportAvatar(profile);
      avatar?.dispose();
      if (mounted && widget.operation['revision'] == revision) {
        widget.controller?.consumedRevision = revision;
        if (editRevision == _editRevision) {
          _replace(profile);
          _persist();
        } else {
          setState(() => _status = '校验期间资料已编辑，保留当前草稿。');
        }
      }
    } catch (e) {
      if (mounted) setState(() => _status = '读取资料校验失败，当前草稿已保留：$e');
    }
  }

  void _replace(PassportProfile p, {PassportAvatarEdit? edit}) {
    widget.controller?.profile = p;
    _avatarEdit = p.avatar == null ? null : edit;
    widget.controller?.avatarEdit = _avatarEdit;
    _editRevision++;
    for (final entry in {
      'codename': p.codename,
      'number': p.number,
      'affiliation': p.affiliation,
      'signature': p.signature,
    }.entries) {
      _fields[entry.key]!.text = entry.value;
    }
    setState(() {
      _codeUnset = p.codenameUnset;
      _numberUnset = p.numberUnset;
      _avatar = p.avatar;
      _avatarMode = p.avatarMode;
    });
    _refresh();
  }

  void _persist() {
    widget.controller?.profile = _profile;
    widget.controller?.avatarEdit = _avatarEdit;
    final json = _avatarEdit == null
        ? _profile.toJson()
        : jsonEncode({
            'draftVersion': 2,
            'profile': jsonDecode(_profile.toJson()),
            'avatarEditor': _avatarEdit!.toJson(),
          });
    final save = widget.saveDraft ?? storage.savePassportDraft;
    _saving = (widget.controller?.saving ?? _saving)
        .then((_) => save(json))
        .catchError((Object e) {
          if (mounted) setState(() => _status = '草稿保存失败：$e');
        });
    widget.controller?.saving = _saving;
  }

  void _changed() {
    _editRevision++;
    _persist();
    setState(() {});
    _refresh();
  }

  Future<void> _refresh() async {
    final generation = ++_generation;
    final profile = _profile;
    // Keep the last complete raster visible until its replacement is ready.
    try {
      final raster = await renderPassport(profile);
      if (mounted && generation == _generation) {
        setState(() {
          _raster = raster;
          _rasterGeneration = generation;
        });
      }
    } catch (e) {
      if (mounted && generation == _generation) {
        setState(() => _status = '预览生成失败：$e');
      }
    }
  }

  Future<void> _avatarPick() async {
    setState(() => _working = true);
    try {
      final bytes = await (widget.pickAvatar ?? storage.pickPassportAvatar)();
      if (bytes != null) {
        final source = await prepareAvatarSource(bytes);
        if (mounted) await _editAvatar(PassportAvatarEdit(source));
      }
    } catch (e) {
      if (mounted) setState(() => _status = '头像导入失败：$e');
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  Future<void> _editAvatar([PassportAvatarEdit? replacement]) async {
    final edit =
        replacement ??
        _avatarEdit ??
        (_avatar == null ? null : PassportAvatarEdit(_avatar!));
    if (edit == null) return;
    final result = await showDialog<({PassportAvatarEdit edit, String png})>(
      context: context,
      builder: (_) => PassportAvatarDialog(edit: edit, mode: _avatarMode),
    );
    if (mounted && result != null) {
      _avatarEdit = result.edit;
      _avatar = result.png;
      _changed();
    }
  }

  Future<void> _changeAvatarMode(String mode) async {
    if (_avatar == null) {
      _avatarMode = mode;
      _changed();
      return;
    }
    final previousMode = _avatarMode;
    setState(() {
      _working = true;
      _avatarMode = mode;
    });
    _changed();
    try {
      final edit = _avatarEdit ?? PassportAvatarEdit(_avatar!);
      final rendered = await renderAvatarEdit(edit, mode);
      if (mounted) {
        _avatarEdit = edit;
        _avatar = rendered.processed;
        _avatarMode = mode;
        _changed();
      }
    } catch (e) {
      if (mounted) {
        _avatarMode = previousMode;
        _changed();
        setState(() => _status = '头像处理失败：$e');
      }
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  Future<void> _read() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('读取并替换当前草稿？'),
        content: const Text('设备资料完整读取并校验成功后会覆盖当前编辑内容和本地草稿。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('保留草稿'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('读取并替换'),
          ),
        ],
      ),
    );
    if (confirmed == true && mounted && widget.canOperate && !_locked) {
      await _run(widget.onRead);
    }
  }

  Future<void> _run(Future<void> Function() action) async {
    setState(() => _working = true);
    try {
      await action();
    } catch (e) {
      if (mounted) setState(() => _status = '$e');
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  @override
  void dispose() {
    for (final controller in _fields.values) {
      controller.dispose();
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final errors = passportLayoutErrors(_profile);
    final op = widget.operation;
    final total = op['total'] as num? ?? 0,
        completed = op['completed'] as num? ?? 0;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('通行证资料', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 12),
        const Text('草稿自动保存在本机。读取设备会在完整校验成功后替换草稿。'),
        for (final entry in {
          'codename': '代号',
          'number': '档案编号',
          'affiliation': '所属',
          'signature': '个人签名',
        }.entries) ...[
          const SizedBox(height: 12),
          TextField(
            key: Key(entry.key == 'codename' ? 'operator-name' : entry.key),
            controller: _fields[entry.key],
            enabled: !_locked,
            maxLength: {
              'codename': 32,
              'number': 36,
              'affiliation': 60,
              'signature': 200,
            }[entry.key],
            maxLines: entry.key == 'signature' ? 3 : 1,
            onChanged: (_) => _changed(),
            decoration: InputDecoration(
              labelText: entry.value,
              errorText: errors[entry.key],
            ),
          ),
          if (entry.key == 'codename' || entry.key == 'number')
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              title: Text('${entry.value}尚未设置'),
              value: entry.key == 'codename' ? _codeUnset : _numberUnset,
              onChanged: _locked
                  ? null
                  : (v) {
                      if (entry.key == 'codename') {
                        _codeUnset = v!;
                      } else {
                        _numberUnset = v!;
                      }
                      _changed();
                    },
            ),
        ],
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton(
              onPressed: _locked ? null : _avatarPick,
              child: const Text('导入头像'),
            ),
            TextButton(
              onPressed: _locked || _avatar == null
                  ? null
                  : () => _editAvatar(),
              child: const Text('调整头像'),
            ),
            TextButton(
              onPressed: _locked || _avatar == null
                  ? null
                  : () {
                      _avatar = null;
                      _avatarEdit = null;
                      _changed();
                    },
              child: const Text('移除头像'),
            ),
          ],
        ),
        const SizedBox(height: 16),
        DropdownButtonFormField<String>(
          key: ValueKey(_avatarMode),
          initialValue: _avatarMode,
          decoration: const InputDecoration(labelText: '头像显示'),
          items: const [
            DropdownMenuItem(value: 'mono', child: Text('黑白 · 原有阈值')),
            DropdownMenuItem(value: 'grayscale', child: Text('灰度 · 黑白网点')),
            DropdownMenuItem(value: 'color', child: Text('彩色 · 六色')),
          ],
          onChanged: _locked
              ? null
              : (value) {
                  _changeAvatarMode(value!);
                },
        ),
        if (_avatar != null)
          const Text('设备资料只含最终128×160头像；需要重新构图完整画面时，请导入原图。调整弹窗应用后才更新草稿。'),
        const SizedBox(height: 8),
        Text(
          _avatarMode == 'color'
              ? '设备六色预览 · 页面时间静态；新版固件使用 ColorFast 彩色快刷'
              : _avatarMode == 'grayscale'
              ? '设备黑白预览 · 灰度通过黑白网点表现'
              : '最终黑白预览 · 440×608',
        ),
        Center(
          child: SizedBox(
            width: 330,
            child: AspectRatio(
              aspectRatio: 440 / 608,
              child: _raster == null
                  ? const Center(child: Text('正在生成预览…'))
                  : Image.memory(
                      _raster!.png,
                      gaplessPlayback: true,
                      filterQuality: FilterQuality.none,
                    ),
            ),
          ),
        ),
        const SizedBox(height: 16),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton(
              onPressed: widget.canOperate && !_locked ? _read : null,
              child: const Text('读取设备资料'),
            ),
            FilledButton(
              onPressed:
                  widget.canOperate &&
                      !_locked &&
                      errors.isEmpty &&
                      _raster != null &&
                      _rasterGeneration == _generation
                  ? () => _run(() {
                      final json = _profile.toJson();
                      final bin = _raster!.bin;
                      return widget.onSave(json, bin);
                    })
                  : null,
              child: const Text('WiFi 发送资料'),
            ),
            if (op['canRetry'] == true)
              OutlinedButton(
                key: const Key('retry-passport'),
                onPressed: !_locked && widget.onRetry != null
                    ? () => _run(widget.onRetry!)
                    : null,
                child: const Text('重试上次上传'),
              ),
            OutlinedButton(
              onPressed: op['canCancel'] == true ? widget.onCancel : null,
              child: const Text('取消'),
            ),
            TextButton(
              onPressed: widget.canOperate && !_locked ? widget.onOpen : null,
              child: const Text('在设备上打开'),
            ),
          ],
        ),
        if (widget.busy)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: LinearProgressIndicator(
              value: total > 0 ? (completed / total).clamp(0, 1) : null,
            ),
          ),
        if (op['message'] is String) Text(op['message'] as String),
        if (_status.isNotEmpty) Text(_status),
        const SizedBox(height: 8),
        const Text(
          'App 通过 WiFi 直传档案画面和资料，蓝牙用于控制和回读。取消后等待清理完成；重试会从上次上传的第一份文件开始。需要支持 WiFi 文件上传的固件。',
        ),
      ],
    );
  }
}
