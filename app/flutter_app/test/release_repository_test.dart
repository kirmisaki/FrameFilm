import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:framefilm_ark/release_repository.dart';

void main() {
  test('only two exact repositories canonicalize; unknown never becomes dev', () {
    expect(canonicalReleaseRepository('KIRMISAKI/FRAMEFILM'), stableReleaseRepository);
    expect(canonicalReleaseRepository('rafael-ikaros/framefilm'), developmentReleaseRepository);
    for (final bad in ['', ' kirmisaki/FrameFilm', 'Rafael-ban/FrameFilm', 'owner/FrameFilm', 'kirmisaki/FrameFilm/']) {
      expect(() => canonicalReleaseRepository(bad), throwsArgumentError);
      expect(() => releaseListUri(20, repository: bad), throwsArgumentError);
    }
    expect(AppReleaseChannel.forPackage('org.framefilm.ark.flutter.dev.debug'), isNull);
  });

  test('release source binds exact tag/name, not a similar host or another channel', () {
    const tag = 'ark/v1+2';
    const name = 'framefilm-ark-firmware.json';
    final good = releaseAssetUri(releaseRepository, tag, name);
    expect(isReleaseAssetUri(good, repository: releaseRepository, tag: tag, name: name), isTrue);
    for (final bad in [
      good.replace(scheme: 'http'), good.replace(host: 'github.com.example.org'),
      good.replace(userInfo: 'user'), good.replace(port: 444),
      good.replace(query: 'x=1'), good.replace(fragment: 'x'),
      releaseAssetUri(releaseRepository, 'other', name),
      releaseAssetUri(releaseRepository, tag, 'other.json'),
      releaseAssetUri(AppReleaseChannel.values.firstWhere((c) => c != AppReleaseChannel.current).repository, tag, name),
    ]) {
      expect(isReleaseAssetUri(bad, repository: releaseRepository, tag: tag, name: name), isFalse);
    }
  });

  test('metadata allows normal GitHub CDN but rejects unexpected redirects before request', () async {
    const tag = 'v1';
    const name = 'framefilm-ark-firmware.json';
    final origin = releaseAssetUri(releaseRepository, tag, name);
    for (final destination in [
      'https://release-assets.githubusercontent.com/github-production-release-asset/1/id?sig=ok',
      'https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/id?sig=ok',
      'https://evil.example/file',
      'http://release-assets.githubusercontent.com/github-production-release-asset/1/id',
      'https://release-assets.githubusercontent.com/other/file',
      'https://github.com/other/FrameFilm/releases/download/v1/$name',
    ]) {
      var calls = 0;
      final client = MockClient((request) async {
        calls++;
        expect(request.followRedirects, isFalse);
        return calls == 1 ? http.Response('', 302, headers: {'location': destination}) : http.Response('{}', 200);
      });
      final result = getReleaseAsset(client, origin, repository: releaseRepository, tag: tag, name: name);
      if (isReleaseCdnUri(Uri.parse(destination))) {
        expect((await result).statusCode, 200);
        expect(calls, 2);
      } else {
        await expectLater(result, throwsFormatException);
        expect(calls, 1);
      }
      client.close();
    }
  });
}
