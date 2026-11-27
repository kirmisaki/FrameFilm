import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'passport_avatar_editor.dart';
import 'passport_profile.dart';
import 'passport_storage.dart' as storage;
import 'student_passport_profile.dart';
import 'passport_color.dart';

class StudentPassportEditorController {
  StudentPassportProfile? profile;
  PassportAvatarEdit? avatarEdit;
  bool legacyOutput = false;
  ({Object? revision})? pendingRead;
  Future<void> saving = Future.value();
}

class StudentPassportEditor extends StatefulWidget {
  const StudentPassportEditor({
    super.key,
    required this.controller,
    required this.canOperate,
    required this.connected,
    required this.supported,
    required this.busy,
    required this.operation,
    required this.onRead,
    required this.onSave,
    required this.onCancel,
    required this.onOpen,
    this.onRetry,
    this.canSend = true,
    this.colorFastSupported,
    this.readingCapabilities = false,
    this.transportHint,
  });

  final StudentPassportEditorController controller;
  final bool canOperate, connected, busy, canSend, readingCapabilities;
  // 0x59 bit11 / bit14. Null means the current device's capability is unknown.
  final bool? supported, colorFastSupported;
  final String? transportHint;
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
  String _palette = 'amiya', _output = 'i006';
  bool _legacyAvatar = false, _legacySize = false;
  bool _legacyOutput = false;
  int _rasterGeneration = -1;
  bool? get _modeSupport =>
      _output == 'colorfast' ? widget.colorFastSupported : widget.supported;

  String? get _availabilityHint {
    if (!widget.connected) {
      return '连接 Ark 后可确认学生证模式；仍可编辑并保存草稿。';
    }
    final supported = _modeSupport;
    if (supported == null) {
      return widget.readingCapabilities
          ? '正在读取学生证模式支持情况…'
          : '尚未确认学生证模式，请在连接页点「刷新信息」；仍可编辑并保存草稿。';
    }
    if (!supported) {
      return '当前设备不支持所选学生证模式，可切换模式或升级固件；仍可编辑并保存草稿。';
    }
    return widget.transportHint;
  }

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
    avatarMode: _legacyAvatar ? _saved.avatarMode : 'color',
    palette: _palette,
    output: _legacySize ? _saved.studentOutput : _output,
    version: _legacySize ? 2 : 3,
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
    _legacyOutput = profile.output == 'ffui';
    _output = _legacyOutput ? 'i006' : profile.output;
    _legacySize = profile.version == 2 && profile.avatar != null;
    _legacyAvatar =
        _legacySize &&
        profile.avatarMode != 'color' &&
        (_cache.avatarEdit == null || _cache.avatarEdit!.source == profile.avatar);
    if (_legacyAvatar)
      _status = '旧头像已黑白化，请重选原图或移除；无法从旧图恢复颜色。';
    else if (_legacySize && _cache.avatarEdit == null)
      _status = '沿用旧彩色小图；重选原图才能恢复更多细节。';
    if (_legacyOutput) _status = '旧 FFUI 不是 ColorFast，请重新选择输出模式。';
  }

  Future<void> _load() async {
    try {
      final cached = _cache.profile;
      if (cached != null) {
        _setProfile(cached);
        _legacyOutput = _legacyOutput || _cache.legacyOutput;
      } else {
        final raw = await storage.loadStudentPassportDraft();
        if (!mounted) return;
        if (raw != null) {
          final value = jsonDecode(raw);
          final wrapper = value is Map && value['draftVersion'] == 2;
          final profile = StudentPassportProfile.fromJson(
            wrapper ? jsonEncode(value['profile']) : raw,
          );
          _cache.avatarEdit = wrapper && value['avatarEditor'] != null
              ? PassportAvatarEdit.fromJson(value['avatarEditor'] as Map)
              : null;
          (await validateStudentPassportAvatar(profile))?.dispose();
          if (!mounted) return;
          _setProfile(profile);
          _legacyOutput =
              _legacyOutput || (wrapper && value['legacyOutput'] == 'ffui');
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
      if (data is Map && (data['version'] == 1 || data['kind'] == 'operator')) {
        setState(() => _status = '设备当前保存的是干员档案，学生证草稿已保留。');
        return;
      }
      final profile = StudentPassportProfile.fromJson(raw);
      (await validateStudentPassportAvatar(profile))?.dispose();
      if (!mounted || generation != _renderGeneration) return;
      if (!_legacyAvatar && !_legacyOutput && profile.toJson() == _profile.toJson()) {
        _cache.avatarEdit = null;
        setState(() => _status = '设备资料与当前草稿一致，无需重新生成预览。');
        return;
      }
      _cache.avatarEdit = null;
      _setProfile(profile);
      _changed(
        status: _legacyAvatar || _legacyOutput
            ? _status
            : '已读取学生证资料；设备 JSON 只含最终裁切，无法恢复原景。',
      );
    } catch (e) {
      if (mounted) setState(() => _status = '资料校验失败，学生证草稿已保留：$e');
    }
  }

  void _changed({String status = ''}) {
    _cache.pendingRead = null;
    _persist();
    setState(() => _status = status);
    _refresh();
  }

  void _persist() {
    final profile = _profile;
    _cache.profile = profile;
    _cache.legacyOutput = _legacyOutput;
    // profile.toJson() is already an object literal; splice it instead of
    // decoding and re-encoding the (possibly large) avatar payload.
    final json =
        '{"draftVersion":2,"profile":${profile.toJson()}'
        '${_cache.avatarEdit != null ? ',"avatarEditor":${jsonEncode(_cache.avatarEdit!.toJson())}' : ''}'
        '${_legacyOutput ? ',"legacyOutput":"ffui"' : ''}}';
    _cache.saving = _cache.saving
        .then((_) => storage.saveStudentPassportDraft(json))
        .catchError((Object e) {
          if (mounted) setState(() => _status = '学生证草稿保存失败：$e');
        });
  }

  Future<void> _refresh() async {
    final generation = ++_renderGeneration;
    setState(() => _rendering = true);
    bool cancelled() => !mounted || generation != _renderGeneration;
    try {
      final edit =
          _cache.avatarEdit ??
          (_legacySize && !_legacyAvatar && _avatar != null
              ? PassportAvatarEdit(_avatar!)
              : null);
      if (edit != null && !_legacyAvatar) {
        final color = await renderAvatarEdit(
          edit,
          _output,
          width: 239,
          height: 299,
          quantize: false,
          checkCancelled: () {
            if (cancelled()) throw StateError('预览已取消');
          },
        );
        if (cancelled()) return;
        if (_avatar != color.crop || _legacySize) {
          _avatar = color.crop;
          _legacySize = false;
          _legacyAvatar = false;
          _persist();
        }
      }
      final profile = _profile;
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
          _rasterGeneration = generation;
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
    if (!pick && _legacyAvatar) {
      setState(() => _status = '旧头像已黑白化，请导入原图；调整旧图不能恢复颜色。');
      return;
    }
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
      builder: (_) => PassportAvatarDialog(
        edit: source,
        mode: _output,
        width: 239,
        height: 299,
        devicePreview: (avatar, checkCancelled) async {
          final template = await rootBundle.load(
            studentPassportTemplateAsset(_palette),
          );
          checkCancelled();
          final raster = await renderStudentPassport(
            _profile.withAvatar(avatar, output: _output),
            Uint8List.sublistView(template),
            isCancelled: () {
              checkCancelled();
              return false;
            },
          );
          checkCancelled();
          return passportPreviewCrop(raster.png, 41, 111, 239, 299);
        },
      ),
    );
    if (result != null && mounted) {
      _cache.avatarEdit = result.edit;
      _avatar = result.png;
      _legacyAvatar = false;
      _legacySize = false;
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
    final readTotal = (op['readTotal'] as num?)?.toInt() ?? 0;
    final readCompleted = (op['readCompleted'] as num?)?.toInt() ?? 0;
    final reading = op['phase'] == 'reading' || op['phase'] == 'verifying';
    final total = reading && readTotal > 0
        ? readTotal
        : (op['total'] as num? ?? 0);
    final completed = reading && readTotal > 0
        ? readCompleted
        : (op['completed'] as num? ?? 0);
    final availabilityHint = _availabilityHint;
    final send =
        widget.canOperate &&
            widget.canSend &&
            _modeSupport == true &&
            !_legacyAvatar &&
            !_legacyOutput &&
            !_legacySize &&
            _rasterGeneration == _renderGeneration &&
            !_locked &&
            !_rendering &&
            errors.isEmpty &&
            currentRaster != null
        ? () => _run(() {
            _cache.pendingRead = null;
            return widget.onSave(_profile.toJson(), currentRaster.bin);
          })
        : null;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Text('学生证', style: Theme.of(context).textTheme.titleLarge),
        Align(
          alignment: Alignment.centerLeft,
          child: DropdownButton<String>(
            key: const Key('student-palette'),
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
                      _legacyAvatar = false;
                      _legacySize = false;
                      _changed();
                    },
              child: const Text('移除照片'),
            ),
          ],
        ),
        const SizedBox(height: 12),
        DropdownButtonFormField<String>(
          key: ValueKey('student-$_output'),
          initialValue: _output,
          decoration: const InputDecoration(labelText: '输出模式'),
          items: const [
            DropdownMenuItem(value: 'i006', child: Text('I006 / 512 色')),
            DropdownMenuItem(value: 'colorfast', child: Text('ColorFast')),
          ],
          onChanged: _locked
              ? null
              : (value) {
                  if (value == null) return;
                  _output = value;
                  _legacyOutput = false;
                  _changed();
                },
        ),
        if (_legacyAvatar) const Text('旧头像已黑白化，请重选原图或移除；不会自动恢复颜色。'),
        if (_legacyOutput) const Text('旧 FFUI 不是 ColorFast，请重新选择输出模式。'),
        if (availabilityHint != null) Text(availabilityHint),
        if (send != null) const Text('发送后会替换设备上的通行证。'),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton(
              onPressed: widget.canOperate && !_locked ? _read : null,
              child: const Text('读取设备资料'),
            ),
            FilledButton(onPressed: send, child: const Text('发送学生证')),
            if (op['canRetry'] == true && op['kind'] == 'student')
              OutlinedButton(
                onPressed:
                    !_locked && _modeSupport == true && widget.onRetry != null
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
        if (widget.busy && op['canCancel'] == true)
          const Text('取消会结束当前操作；回读中取消会断开并重新连接设备。'),
        if (op['message'] is String) Text(op['message'] as String),
        if (_status.isNotEmpty) Text(_status),
      ],
    );
  }
}
