import 'package:http/http.dart' as http;

const stableReleaseRepository = 'kirmisaki/FrameFilm';
const developmentReleaseRepository = 'Rafael-Ikaros/FrameFilm';

/// Omitted local configuration is development; explicit unknown sources fail.
String canonicalReleaseRepository(String repository) {
  for (final channel in AppReleaseChannel.values) {
    if (repository.toLowerCase() == channel.repository.toLowerCase()) {
      return channel.repository;
    }
  }
  throw ArgumentError.value(repository, 'repository', '不支持的发布仓库');
}

final releaseRepository = canonicalReleaseRepository(
  const String.fromEnvironment(
    'FRAMEFILM_APP_RELEASE_REPO',
    defaultValue: developmentReleaseRepository,
  ),
);
final releaseRepositoryUrl = 'https://github.com/$releaseRepository';
final isDevelopmentRelease = releaseRepository == developmentReleaseRepository;
final releaseChannelLabel = AppReleaseChannel.current.label;

enum AppReleaseChannel {
  stable('正式版', stableReleaseRepository, 'org.framefilm.ark.flutter'),
  development(
    '开发版',
    developmentReleaseRepository,
    'org.framefilm.ark.flutter.dev',
  );

  const AppReleaseChannel(this.label, this.repository, this.packageName);
  final String label, repository, packageName;

  static AppReleaseChannel? forPackage(String packageName) {
    for (final channel in values) {
      if (channel.packageName == packageName) return channel;
    }
    return null;
  }

  static AppReleaseChannel get current => values.firstWhere(
    (channel) => channel.repository == releaseRepository,
  );
}

Uri releaseListUri(int pageSize, {String? repository}) => Uri.https(
  'api.github.com',
  '/repos/${canonicalReleaseRepository(repository ?? releaseRepository)}/releases',
  {'per_page': '$pageSize'},
);

Uri releaseAssetUri(String repository, String tag, String name) => Uri(
  scheme: 'https',
  host: 'github.com',
  pathSegments: [
    ...canonicalReleaseRepository(repository).split('/'),
    'releases', 'download', tag, name,
  ],
);

bool isReleaseAssetUri(
  Uri uri, {
  required String repository,
  required String tag,
  required String name,
}) {
  final expected = canonicalReleaseRepository(repository).split('/');
  final path = uri.pathSegments;
  return _safeHttps(uri) &&
      uri.host == 'github.com' &&
      !uri.hasQuery &&
      tag.isNotEmpty &&
      path.length == 6 &&
      path[0].toLowerCase() == expected[0].toLowerCase() &&
      path[1].toLowerCase() == expected[1].toLowerCase() &&
      path[2] == 'releases' && path[3] == 'download' &&
      path[4] == tag && path[5] == name;
}

bool _safeHttps(Uri uri) => uri.scheme == 'https' && uri.port == 443 &&
    uri.userInfo.isEmpty && !uri.hasFragment;

/// Only GitHub's release-asset CDN paths, not arbitrary HTTPS redirects.
bool isReleaseCdnUri(Uri uri) => _safeHttps(uri) &&
    ((uri.host == 'release-assets.githubusercontent.com' &&
      uri.path.startsWith('/github-production-release-asset/')) ||
     ((uri.host == 'objects.githubusercontent.com' ||
       uri.host == 'github-releases.githubusercontent.com') &&
      uri.path.startsWith('/github-production-release-asset-2e65be/')));

Future<http.Response> getReleaseAsset(
  http.Client client,
  Uri uri, {
  required String repository,
  required String tag,
  required String name,
}) async {
  if (!isReleaseAssetUri(uri, repository: repository, tag: tag, name: name)) {
    throw const FormatException('资产必须来自当前仓库的精确发布标签与文件名');
  }
  var target = uri;
  for (var redirects = 0; redirects <= 8; redirects++) {
    final request = http.Request('GET', target)..followRedirects = false;
    final response = await client.send(request).timeout(const Duration(seconds: 20));
    if ([301, 302, 303, 307, 308].contains(response.statusCode)) {
      await response.stream.drain<void>().timeout(const Duration(seconds: 20));
      final location = response.headers['location'];
      if (location == null || redirects == 8) {
        throw const FormatException('GitHub 资产重定向无效或过多');
      }
      target = target.resolve(location);
      if (!isReleaseAssetUri(target, repository: repository, tag: tag, name: name) &&
          !isReleaseCdnUri(target)) {
        throw const FormatException('GitHub 资产重定向到了非预期地址');
      }
      continue;
    }
    return http.Response.fromStream(response).timeout(const Duration(seconds: 20));
  }
  throw const FormatException('GitHub 资产重定向过多');
}
