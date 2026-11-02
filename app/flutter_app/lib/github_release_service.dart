import 'dart:convert';

import 'package:http/http.dart' as http;

class GitHubFirmwareRelease {
  const GitHubFirmwareRelease({
    required this.name,
    required this.tag,
    required this.publishedAt,
    required this.url,
    required this.size,
    this.sha256,
  });
  final String name, tag, url;
  final DateTime publishedAt;
  final int size;
  final String? sha256;
  static final demo = GitHubFirmwareRelease(
    name: 'DEMO 在线固件（演示）',
    tag: 'DEMO',
    publishedAt: DateTime.utc(2026, 10, 8),
    url: 'demo://frame_film_ark.bin',
    size: 1048576,
  );
  Map<String, Object?> get downloadArguments => {
    'url': url,
    'name': 'frame_film_ark.bin',
    'size': size,
    'sha256': sha256,
  };
}

class GitHubReleaseService {
  GitHubReleaseService({http.Client? client})
    : _client = client ?? http.Client();
  final http.Client _client;
  Future<GitHubFirmwareRelease?> latest() async {
    final response = await _client
        .get(
          Uri.https(
            'api.github.com',
            '/repos/Rafael-Ikaros/FrameFilm/releases',
            {'per_page': '20'},
          ),
          headers: {'Accept': 'application/vnd.github+json'},
        )
        .timeout(const Duration(seconds: 20));
    if (response.statusCode == 403 || response.statusCode == 429) {
      throw Exception('GitHub 请求受限，请稍后手动重试（${response.statusCode}）');
    }
    if (response.statusCode != 200) {
      throw Exception('GitHub 请求失败（${response.statusCode}）');
    }
    return parseLatest(jsonDecode(response.body));
  }

  static GitHubFirmwareRelease? parseLatest(Object? data) {
    if (data is! List) throw const FormatException('GitHub 发布响应格式无效');
    final releases = <GitHubFirmwareRelease>[];
    for (final item in data) {
      if (item is! Map || item['draft'] == true || item['prerelease'] == true) {
        continue;
      }
      final date = DateTime.tryParse(item['published_at']?.toString() ?? '');
      if (date == null || item['assets'] is! List) continue;
      for (final asset in item['assets'] as List) {
        if (asset is! Map || asset['name'] != 'frame_film_ark.bin') continue;
        final url = asset['browser_download_url'];
        final size = asset['size'];
        if (url is! String ||
            !url.startsWith('https://') ||
            size is! num ||
            size <= 0) {
          continue;
        }
        final digest = asset['digest']?.toString() ?? '';
        releases.add(
          GitHubFirmwareRelease(
            name: item['name']?.toString() ?? '',
            tag: item['tag_name']?.toString() ?? '',
            publishedAt: date,
            url: url,
            size: size.toInt(),
            sha256: RegExp(r'^sha256:[0-9a-fA-F]{64}$').hasMatch(digest)
                ? digest.substring(7).toLowerCase()
                : null,
          ),
        );
        break;
      }
    }
    releases.sort((a, b) => b.publishedAt.compareTo(a.publishedAt));
    return releases.isEmpty ? null : releases.first;
  }

  void dispose() => _client.close();
}
