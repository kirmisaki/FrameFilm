import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'passport_avatar_editor.dart';
import 'passport_profile.dart';
import 'passport_storage.dart' as storage;
import 'student_passport_profile.dart';

class StudentPassportEditorController {
  StudentPassportProfile? profile;
  PassportAvatarEdit? avatarEdit;
  ({Object? revision})? pendingRead;
  Future<void> saving = Future.value();
}

class StudentPassportEditor extends StatefulWidget {
  const StudentPassportEditor({
    super.key,
    required this.controller,
    required this.canOperate,
    required this.supported,
    required this.busy,
    required this.operation,
    required this.onRead,
    required this.onSave,
    required this.onCancel,
    required this.onOpen,
    this.onRetry,
    this.canSend = true,
  });

  final StudentPassportEditorController controller;
  final bool canOperate, supported, busy;
  final bool canSend;
  final Map<String, Object?> operation;
  final Future<void> Function() onRead;
  final Future<void> Function(String, Uint8List) onSave;
  final VoidCallback onCancel, onOpen;
  final Future<void> Function()? onRetry;

  @override
  State<StudentPassportEditor> createState() => _StudentPassportEditorState();
}

class _StudentPassportEditorState extends State<StudentPassportEditor> {
  static const _labels = {
    'codename': '代号',
    'studentNumber': '学号',
    'race': '种族',
  };
  static const _limits = {'codename': 32, 'studentNumber': 12, 'race': 60};
  final _fields = {
    for (final name in _labels.keys) name: TextEditingController(),
  };

  StudentPassportProfile _saved = const StudentPassportProfile();
  StudentPassportEditorController get _cache => widget.controller;
  bool _codeUnset = false, _numberUnset = false, _working = false;
  bool _loading = true, _rendering = false;
  String? _avatar;
  String _palette = 'amiya';
  PassportRaster? _raster;
  String? _rasterPalette;
  String _status = '';
  int _renderGeneration = 0;
  bool get _locked => widget.busy || _working || _loading;
  StudentPassportProfile get _profile => StudentPassportProfile(
    codename: _fields['codename']!.text,
    studentNumber: _fields['studentNumber']!.text,
    race: _fields['race']!.text,
    codenameUnset: _codeUnset,
    studentNumberUnset: _numberUnset,
    avatar: _avatar,
    avatarMode: _saved.avatarMode,
    palette: _palette,
  );

  @override
  void initState() {
    super.initState();
    _load();
  }

  void _setProfile(StudentPassportProfile profile) {
    _saved = profile;
    _fields['codename']!.text = profile.codename;
    _fields['studentNumber']!.text = profile.studentNumber;
    _fields['race']!.text = profile.race;
    _codeUnset = profile.codenameUnset;
    _numberUnset = profile.studentNumberUnset;
    _avatar = profile.avatar;
    _palette = profile.palette;
  }

  Future<void> _load() async {
    try {
      final cached = _cache.profile;
      if (cached != null) {
        _setProfile(cached);
      } else {
        final raw = await storage.loadStudentPassportDraft();
        if (!mounted) return;
        if (raw != null) {
          final profile = StudentPassportProfile.fromJson(raw);
          (await validateStudentPassportAvatar(profile))?.dispose();
          if (!mounted) return;
          _setProfile(profile);
        }
      }
    } catch (e) {
      _status = '学生证草稿读取失败：$e';
    } finally {
      if (mounted) {
        setState(() => _loading = false);
        _refresh();
        _consumeRead();
      }
    }
  }

  @override
  void didUpdateWidget(covariant StudentPassportEditor oldWidget) {
    super.didUpdateWidget(oldWidget);
    _consumeRead();
  }

  void _consumeRead() {
    final pending = _cache.pendingRead;
    if (_loading || pending == null) return;
    final revision = widget.operation['revision'];
    final raw = widget.operation['json'];
    if (revision != null && revision != pending.revision && raw is String) {
      _cache.pendingRead = null;
      _acceptRead(raw, _renderGeneration);
    } else if (const {
      'error',
      'cancelled',
    }.contains(widget.operation['phase'])) {
      _cache.pendingRead = null;
    }
  }

  Future<void> _acceptRead(String raw, int generation) async {
    if (generation != _renderGeneration) return;
    try {
      final data = jsonDecode(raw);
      if (data is Map && data['version'] == 1) {
        setState(() => _status = '设备当前保存的是干员档案，学生证草稿已保留。');
        return;
      }
      final profile = StudentPassportProfile.fromJson(raw);
      (await validateStudentPassportAvatar(profile))?.dispose();
      if (!mounted || generation != _renderGeneration) return;
      _cache.avatarEdit = null;
      _setProfile(profile);
      _changed(
        status: profile.studentOutput == 'ffui'
            ? '已读取资料。下次发送将使用 I006。'
            : '已读取学生证资料。',
      );
    } catch (e) {
      if (mounted) setState(() => _status = '资料校验失败，学生证草稿已保留：$e');
    }
  }

  void _changed({String status = ''}) {
    final profile = _profile;
    _cache.pendingRead = null;
    _cache.profile = profile;
    final json = profile.toJson();
    _cache.saving = _cache.saving
        .then((_) => storage.saveStudentPassportDraft(json))
        .catchError((Object e) {
          if (mounted) setState(() => _status = '学生证草稿保存失败：$e');
        });
    setState(() => _status = status);
    _refresh();
  }

  Future<void> _refresh() async {
    final generation = ++_renderGeneration;
    final profile = _profile;
    setState(() => _rendering = true);
    bool cancelled() => !mounted || generation != _renderGeneration;
    try {
      final template = await rootBundle.load(
        studentPassportTemplateAsset(profile.palette),
      );
      if (cancelled()) return;
      final raster = await renderStudentPassport(
        profile,
        Uint8List.sublistView(template),
        isCancelled: cancelled,
      );
      if (!cancelled()) {
        setState(() {
          _raster = raster;
          _rasterPalette = profile.palette;
          _rendering = false;
        });
      }
    } catch (e) {
      if (!cancelled()) {
        setState(() {
          _raster = null;
          _rasterPalette = null;
          _rendering = false;
          _status = '预览生成失败：$e';
        });
      }
    }
  }

  Future<void> _run(Future<void> Function() action) async {
    setState(() {
      _working = true;
      _status = '';
    });
    try {
      await action();
    } catch (e) {
      if (mounted) setState(() => _status = '$e');
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  Future<void> _editPhoto({required bool pick}) => _run(() async {
    var edit = _cache.avatarEdit;
    if (pick) {
      final image = await storage.pickPassportAvatar();
      if (image == null || !mounted) return;
      edit = PassportAvatarEdit(await prepareAvatarSource(image));
    }
    if (edit == null && _avatar != null) edit = PassportAvatarEdit(_avatar!);
    if (edit == null || !mounted) return;
    final source = edit;
    final result = await showDialog<({PassportAvatarEdit edit, String png})>(
      context: context,
      builder: (_) =>
          PassportAvatarDialog(edit: source, mode: _saved.avatarMode),
    );
    if (result != null && mounted) {
      _cache.avatarEdit = result.edit;
      _avatar = result.png;
      _changed();
    }
  });

  Future<void> _read() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('读取并替换学生证草稿？'),
        content: const Text('设备上的学生证通过校验后会替换当前草稿。'),
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
    if (confirmed != true || !mounted || _locked || !widget.canOperate) return;
    await _run(() async {
      _cache.pendingRead = (revision: widget.operation['revision']);
      await widget.onRead();
    });
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
    final errors = studentPassportLayoutErrors(_profile);
    final currentRaster = _rasterPalette == _palette ? _raster : null;
    final op = widget.operation;
    final total = op['total'] as num? ?? 0;
    final completed = op['completed'] as num? ?? 0;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('学生证', style: Theme.of(context).textTheme.titleLarge),
        Align(
          alignment: Alignment.centerLeft,
          child: DropdownButton<String>(
            value: _palette,
            items: [
              for (final palette in studentPassportPalettes.entries)
                DropdownMenuItem(
                  value: palette.key,
                  child: Text(palette.value),
                ),
            ],
            onChanged: _locked
                ? null
                : (value) {
                    if (value == null || value == _palette) return;
                    _palette = value;
                    _changed();
                  },
          ),
        ),
        Text('卡面预览', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        Align(
          alignment: Alignment.centerLeft,
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 560),
            child: AspectRatio(
              aspectRatio: 3 / 2,
              child: currentRaster == null
                  ? Image.asset(
                      studentPassportTemplateAsset(_palette),
                      semanticLabel: '学生证底图',
                    )
                  : Image.memory(
                      currentRaster.png,
                      semanticLabel: '学生证设备预览',
                      filterQuality: FilterQuality.none,
                      gaplessPlayback: true,
                    ),
            ),
          ),
        ),
        if (_rendering) const LinearProgressIndicator(),
        for (final entry in _labels.entries) ...[
          const SizedBox(height: 12),
          TextField(
            key: Key('student-${entry.key}'),
            controller: _fields[entry.key],
            enabled: !_locked,
            maxLength: _limits[entry.key],
            onChanged: (_) => _changed(),
            decoration: InputDecoration(
              labelText: entry.value,
              errorText: errors[entry.key],
            ),
          ),
          if (entry.key != 'race')
            CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              title: Text('${entry.value}尚未设置'),
              value: entry.key == 'codename' ? _codeUnset : _numberUnset,
              onChanged: _locked
                  ? null
                  : (value) {
                      if (entry.key == 'codename') {
                        _codeUnset = value!;
                      } else {
                        _numberUnset = value!;
                      }
                      _changed();
                    },
            ),
        ],
        Wrap(
          spacing: 8,
          children: [
            OutlinedButton(
              onPressed: _locked ? null : () => _editPhoto(pick: true),
              child: const Text('导入照片'),
            ),
            TextButton(
              onPressed: _locked || _avatar == null
                  ? null
                  : () => _editPhoto(pick: false),
              child: const Text('调整照片'),
            ),
            TextButton(
              onPressed: _locked || _avatar == null
                  ? null
                  : () {
                      _avatar = null;
                      _cache.avatarEdit = null;
                      _changed();
                    },
              child: const Text('移除照片'),
            ),
          ],
        ),
        const SizedBox(height: 12),
        if (!widget.supported) const Text('连接支持 I006 学生证的 Ark 固件后即可发送。'),
        const Text('发送后会替换设备上的通行证。'),
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
                      widget.canSend &&
                      widget.supported &&
                      !_locked &&
                      !_rendering &&
                      errors.isEmpty &&
                      currentRaster != null
                  ? () => _run(() {
                      _cache.pendingRead = null;
                      return widget.onSave(
                        _profile.toJson(),
                        currentRaster.bin,
                      );
                    })
                  : null,
              child: const Text('发送学生证'),
            ),
            if (op['canRetry'] == true && op['kind'] == 'student')
              OutlinedButton(
                onPressed:
                    !_locked && widget.supported && widget.onRetry != null
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
          LinearProgressIndicator(
            value: total > 0 ? (completed / total).clamp(0, 1) : null,
          ),
        if (op['message'] is String) Text(op['message'] as String),
        if (_status.isNotEmpty) Text(_status),
      ],
    );
  }
}
