import 'dart:convert';

import 'package:http/http.dart' as http;

import 'release_repository.dart';

class AppRelease {
  const AppRelease({
    required this.channel,
    required this.tag,
    required this.versionName,
    required this.versionCode,
    required this.minSdk,
    required this.publishedAt,
    required this.url,
    required this.size,
    required this.sha256,
  });

  final String tag, versionName, url, sha256;
  final AppReleaseChannel channel;
  final int versionCode, minSdk, size;
  final DateTime publishedAt;

  bool isNewerThan(int installedCode) => versionCode > installedCode;
}

class AppUpdateService {
  AppUpdateService({http.Client? client})
    : _client = client ?? http.Client(),
      _ownsClient = client == null;

  static const apkName = 'framefilm-ark-flutter.apk';
  static const metadataName = 'framefilm-ark-flutter.json';
  static final _digest = RegExp(r'^[0-9a-fA-F]{64}$');

  final http.Client _client;
  final bool _ownsClient;

  Future<AppRelease?> latest(AppReleaseChannel channel) async {
    final response = await _client
        .get(
          releaseListUri(20, repository: channel.repository),
          headers: {'Accept': 'application/vnd.github+json'},
        )
        .timeout(const Duration(seconds: 20));
    _checkResponse(response);
    final data = jsonDecode(response.body);
    if (data is! List) throw const FormatException('GitHub 发布响应格式无效');

    AppRelease? newest;
    for (final release in data) {
      if (release is! Map ||
          release['draft'] == true ||
          release['prerelease'] == true) {
        continue;
      }
      final assets = release['assets'];
      final tag = release['tag_name'];
      final date = DateTime.tryParse(release['published_at']?.toString() ?? '');
      if (assets is! List || tag is! String || tag.isEmpty || date == null) {
        continue;
      }
      final apk = _asset(assets, channel, tag, apkName);
      final metadata = _asset(assets, channel, tag, metadataName);
      if (apk == null || metadata == null) continue;

      final metadataResponse = await _client
          .get(Uri.parse(metadata.url))
          .timeout(const Duration(seconds: 20));
      _checkResponse(metadataResponse);
      final details = jsonDecode(metadataResponse.body);
      if (details is Map &&
          AppReleaseChannel.values.any(
            (other) =>
                other != channel && details['packageName'] == other.packageName,
          )) {
        continue;
      }
      if (details is! Map ||
          details['packageName'] != channel.packageName ||
          details['apkName'] != apkName ||
          details['size'] != apk.size ||
          details['versionName'] is! String ||
          (details['versionName'] as String).isEmpty ||
          details['versionCode'] is! int ||
          (details['versionCode'] as int) <= 0 ||
          details['minSdk'] is! int ||
          (details['minSdk'] as int) <= 0 ||
          details['sha256'] is! String ||
          !_digest.hasMatch(details['sha256'] as String)) {
        throw const FormatException('应用发布元数据无效');
      }
      final candidate = AppRelease(
        channel: channel,
        tag: tag,
        versionName: details['versionName'] as String,
        versionCode: details['versionCode'] as int,
        minSdk: details['minSdk'] as int,
        publishedAt: date,
        url: apk.url,
        size: apk.size,
        sha256: (details['sha256'] as String).toLowerCase(),
      );
      if (newest == null ||
          candidate.versionCode > newest.versionCode ||
          (candidate.versionCode == newest.versionCode &&
              candidate.publishedAt.isAfter(newest.publishedAt))) {
        newest = candidate;
      }
    }
    return newest;
  }

  static ({String url, int size})? _asset(
    List assets,
    AppReleaseChannel channel,
    String tag,
    String expectedName,
  ) {
    final repository = channel.repository.toLowerCase().split('/');
    for (final asset in assets) {
      if (asset is! Map || asset['name'] != expectedName) continue;
      final rawUrl = asset['browser_download_url'];
      final rawSize = asset['size'];
      if (rawUrl is! String || rawSize is! int || rawSize <= 0) continue;
      final uri = Uri.tryParse(rawUrl);
      if (uri == null ||
          uri.scheme != 'https' ||
          uri.host != 'github.com' ||
          uri.userInfo.isNotEmpty ||
          uri.port != 443 ||
          uri.hasQuery ||
          uri.hasFragment) {
        continue;
      }
      final path = uri.pathSegments;
      if (path.length != 6 ||
          path[0].toLowerCase() != repository[0] ||
          path[1].toLowerCase() != repository[1] ||
          path[2] != 'releases' ||
          path[3] != 'download' ||
          path[4] != tag ||
          path[5] != expectedName) {
        continue;
      }
      return (url: rawUrl, size: rawSize);
    }
    return null;
  }

  static void _checkResponse(http.Response response) {
    if (response.statusCode == 403 || response.statusCode == 429) {
      throw Exception('GitHub 请求受限，请稍后重试（${response.statusCode}）');
    }
    if (response.statusCode != 200) {
      throw Exception('GitHub 请求失败（${response.statusCode}）');
    }
  }

  void dispose() {
    if (_ownsClient) _client.close();
  }
}
