import 'dart:convert';

import 'package:http/http.dart' as http;

import 'hardware_identity.dart';

class GitHubFirmwareRelease {
  const GitHubFirmwareRelease({
    required this.name,
    required this.tag,
    required this.publishedAt,
    required this.url,
    required this.size,
    required this.sha256,
    required this.elfSha256,
    required this.asset,
    required this.hardware,
    required this.displayVersion,
  });
  final String name, tag, url, sha256, elfSha256, asset, displayVersion;
  final DateTime publishedAt;
  final int size;
  final HardwareIdentity hardware;
  static GitHubFirmwareRelease demoFor(HardwareIdentity hardware) =>
      GitHubFirmwareRelease(
        name: 'DEMO 在线固件（演示）',
        tag: 'DEMO',
        publishedAt: DateTime.utc(2026, 10, 8),
        url: 'demo://firmware',
        size: 1048576,
        sha256: 'a' * 64,
        elfSha256: 'b' * 64,
        asset: hardware.targetId == HardwareIdentity.n16r8.targetId
            ? 'frame_film_ark-n16r8.bin'
            : 'frame_film_ark-n4r2.bin',
        hardware: hardware,
        displayVersion: '3.2.5 / DEMO',
      );
  static final demo = demoFor(HardwareIdentity.n4r2);
  Map<String, Object?> get downloadArguments => {
    'url': url,
    'name': asset,
    'size': size,
    'sha256': sha256,
    'elfSha256': elfSha256,
    'displayVersion': displayVersion,
    ...hardware.packageMetadata,
  };
}

class GitHubReleaseService {
  GitHubReleaseService({http.Client? client})
    : _client = client ?? http.Client();
  final http.Client _client;
  static final _sha = RegExp(r'^[0-9a-fA-F]{64}$');
  Future<Object?> _get(Uri url) async {
    final response = await _client
        .get(url, headers: {'Accept': 'application/vnd.github+json'})
        .timeout(const Duration(seconds: 20));
    if (response.statusCode == 403 || response.statusCode == 429) {
      throw Exception('GitHub 请求受限，请稍后重试（${response.statusCode}）');
    }
    if (response.statusCode != 200) {
      throw Exception('GitHub 请求失败（${response.statusCode}），请稍后重试');
    }
    return jsonDecode(response.body);
  }

  Future<GitHubFirmwareRelease?> latest({HardwareIdentity? hardware}) async {
    if (hardware?.trusted != true) {
      throw StateError(HardwareIdentity.unavailableMessage);
    }
    final data = await _get(
      Uri.https('api.github.com', '/repos/kirmisaki/FrameFilm/releases', {
        'per_page': '100',
      }),
    );
    final release = _latestFormal(data);
    if (release == null) return null;
    final asset = _asset(release, 'framefilm-ark-firmware.json');
    final manifest = await _get(
      Uri.parse(asset['browser_download_url'] as String),
    );
    return selectFirmware(release, manifest, hardware!);
  }

  static Map? _latestFormal(Object? data) {
    if (data is! List) throw const FormatException('GitHub 发布响应格式无效');
    final releases = data
        .whereType<Map>()
        .where(
          (r) =>
              r['draft'] != true &&
              r['prerelease'] != true &&
              // 经典固件（frame-film-v*）与 Ark 共用仓库，不能抢选 Ark 固件源。
              r['tag_name']?.toString().startsWith('frame-film-') != true &&
              DateTime.tryParse(r['published_at']?.toString() ?? '') != null,
        )
        .toList();
    releases.sort(
      (a, b) =>
          DateTime.parse(b['published_at'] as String)
              .compareTo(DateTime.parse(a['published_at'] as String)),
    );
    return releases.isEmpty ? null : releases.first;
  }

  static Map _asset(Map release, String name) {
    final assets =
        (release['assets'] is List ? release['assets'] as List : const [])
            .whereType<Map>()
            .where((a) => a['name'] == name)
            .toList();
    if (assets.length != 1) {
      throw FormatException('本次发布缺少或重复资产 $name，请联系维护者补全发布');
    }
    final asset = assets.single,
        url = Uri.tryParse(
          assets.single['browser_download_url']?.toString() ?? '',
        );
    if (url == null ||
        url.scheme != 'https' ||
        url.host.isEmpty ||
        asset['size'] is! int ||
        (asset['size'] as int) <= 0) {
      throw FormatException('$name 的下载地址或大小无效，请联系维护者修复发布');
    }
    return asset;
  }

  /// Pure manifest selection for this exact release; no old-binary fallback.
  static GitHubFirmwareRelease selectFirmware(
    Map release,
    Object? manifest,
    HardwareIdentity hardware,
  ) {
    if (!hardware.trusted) {
      throw StateError(HardwareIdentity.unavailableMessage);
    }
    if (manifest is! Map ||
        manifest['schemaVersion'] != 1 ||
        manifest['project'] != 'frame_film_ark' ||
        manifest['firmwares'] is! List) {
      throw const FormatException('固件清单格式无效，请联系维护者重新发布');
    }
    final entries = <String, Map>{};
    for (final entry in manifest['firmwares'] as List) {
      if (entry is! Map ||
          entry['targetId'] is! String ||
          entries.containsKey(entry['targetId'])) {
        throw const FormatException('固件清单含无效或重复 targetId，请联系维护者修复');
      }
      final target = HardwareIdentity.target(entry['targetId'] as String);
      if (target == null ||
          !target.matchesPackage(entry) ||
          entry['size'] is! int ||
          (entry['size'] as int) <= 0 ||
          !_sha.hasMatch(entry['sha256']?.toString() ?? '') ||
          !_sha.hasMatch(entry['elfSha256']?.toString() ?? '') ||
          entry['displayVersion'] is! String) {
        throw const FormatException('固件清单的硬件参数或 SHA / ELF 指纹无效，请联系维护者修复');
      }
      final expectedAsset = target.targetId == HardwareIdentity.n4r2.targetId
          ? 'frame_film_ark-n4r2.bin'
          : 'frame_film_ark-n16r8.bin';
      if (entry['asset'] != expectedAsset) {
        throw const FormatException('固件清单目标与资产名称不匹配');
      }
      final asset = _asset(release, expectedAsset);
      if (asset['size'] != entry['size']) {
        throw const FormatException('固件清单与发布资产大小不一致，请重新发布');
      }
      final digest = asset['digest'];
      if (digest != null &&
          digest != '' &&
          digest.toString().toLowerCase() !=
              'sha256:${entry['sha256'].toString().toLowerCase()}') {
        throw const FormatException('固件清单与发布资产 SHA-256 不一致');
      }
      entries[target.targetId] = entry;
    }
    final entry = entries[hardware.targetId];
    if (entry == null || !hardware.matchesPackage(entry)) {
      throw FormatException('本次发布没有匹配 ${hardware.label} 的固件，请等待维护者补全');
    }
    final asset = _asset(release, entry['asset'] as String);
    return GitHubFirmwareRelease(
      name: release['name']?.toString() ?? '',
      tag: release['tag_name']?.toString() ?? '',
      publishedAt: DateTime.parse(release['published_at'] as String),
      url: asset['browser_download_url'] as String,
      size: entry['size'] as int,
      sha256: entry['sha256'].toString().toLowerCase(),
      elfSha256: entry['elfSha256'].toString().toLowerCase(),
      asset: entry['asset'] as String,
      hardware: hardware,
      displayVersion: entry['displayVersion'] as String,
    );
  }

  void dispose() => _client.close();
}
