const releaseRepository = String.fromEnvironment(
  'FRAMEFILM_APP_RELEASE_REPO',
  defaultValue: 'Rafael-Ikaros/FrameFilm',
);

const stableReleaseRepository = 'kirmisaki/FrameFilm';
const developmentReleaseRepository = 'Rafael-Ikaros/FrameFilm';
const releaseRepositoryUrl = 'https://github.com/$releaseRepository';
final isDevelopmentRelease =
    releaseRepository.toLowerCase() != stableReleaseRepository.toLowerCase();
final releaseChannelLabel = isDevelopmentRelease ? '开发版' : '正式版';

enum AppReleaseChannel {
  stable('正式版', stableReleaseRepository, 'org.framefilm.ark.flutter'),
  development(
    '开发版',
    developmentReleaseRepository,
    'org.framefilm.ark.flutter.dev',
  );

  const AppReleaseChannel(this.label, this.repository, this.packageName);

  final String label, repository, packageName;

  static AppReleaseChannel get current =>
      isDevelopmentRelease ? development : stable;
}

Uri releaseListUri(int pageSize, {String repository = releaseRepository}) =>
    Uri.https('api.github.com', '/repos/$repository/releases', {
      'per_page': '$pageSize',
    });
