import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:framefilm_ark/app_update.dart';
import 'package:framefilm_ark/app_update_service.dart';

class UnsupportedAppPlatform implements AppUpdatePlatform {
  @override
  bool get supported => false;
  @override
  Future<InstalledAppInfo> getInfo() => throw UnimplementedError();
  @override
  Future<AppUpdateState> getState() => throw UnimplementedError();
  @override
  Future<AppUpdateState> download(AppRelease release) =>
      throw UnimplementedError();
  @override
  Future<AppUpdateState> cancel() => throw UnimplementedError();
  @override
  Future<AppUpdateState> install() => throw UnimplementedError();
}

void main() {
  testWidgets('preview downloader cancels, retries and prepares installation', (
    tester,
  ) async {
    final controller = AppUpdateController(preview: true);
    controller.loadDownloadDemo();
    expect(controller.canDownload, isTrue);
    await controller.download();
    await tester.pump(const Duration(milliseconds: 900));
    expect(controller.state.received, greaterThan(0));
    expect(controller.canCheck, isFalse);
    final release = controller.release;
    controller.loadDownloadDemo();
    expect(controller.release, same(release));
    await controller.cancel();
    expect(controller.state.phase, 'cancelled');
    expect(controller.canDownload, isTrue);
    await tester.pump(const Duration(seconds: 7));
    expect(controller.state.phase, 'cancelled');
    await controller.download();
    expect(controller.state.received, 0);
    await tester.pump(const Duration(seconds: 6));
    expect(controller.state.phase, 'ready');
    expect(controller.state.progress, 1);
    expect(controller.state.canInstall, isTrue);
    await controller.install();
    expect(controller.message, contains('未安装应用'));
    controller.dispose();
  });

  test('download demo cannot replace a real release outside preview', () {
    final controller = AppUpdateController();
    controller.loadDownloadDemo();
    expect(controller.release, isNull);
    expect(controller.canDownload, isFalse);
    controller.dispose();
  });

  const apk = AppUpdateService.apkName;
  const metadata = AppUpdateService.metadataName;
  final sha = 'a' * 64;

  Map<String, Object?> release(
    String tag,
    String published, {
    bool prerelease = false,
    String owner = 'kirmisaki',
  }) => {
    'tag_name': tag,
    'published_at': published,
    'prerelease': prerelease,
    'assets': [
      for (final name in [apk, metadata])
        {
          'name': name,
          'size': name == apk ? 100 : 20,
          'browser_download_url':
              'https://github.com/$owner/FrameFilm/releases/download/$tag/$name',
        },
    ],
  };

  Map<String, Object?> manifest(int versionCode) => {
    'packageName': AppUpdateService.applicationId,
    'versionName': '0.$versionCode.0',
    'versionCode': versionCode,
    'apkName': apk,
    'sha256': sha,
    'size': 100,
    'minSdk': 28,
  };

  for (final owner in [
    'kirmisaki',
    'Rafael-Ikaros',
    'Rafael-ban',
    'unrelated-owner',
  ]) {
    test('release asset repository allowlist: $owner', () async {
      final service = AppUpdateService(
        client: MockClient((request) async {
          if (request.url.host == 'api.github.com') {
            expect(request.url.path, '/repos/kirmisaki/FrameFilm/releases');
            return http.Response(
              jsonEncode([release('v4', '2026-01-01T00:00:00Z', owner: owner)]),
              200,
            );
          }
          expect(request.url.host, 'github.com');
          expect(
            request.url.path,
            '/$owner/FrameFilm/releases/download/v4/$metadata',
          );
          return http.Response(jsonEncode(manifest(4)), 200);
        }),
      );
      final found = await service.latest();
      if (owner == 'unrelated-owner') {
        expect(found, isNull);
      } else {
        expect(
          found?.url,
          'https://github.com/$owner/FrameFilm/releases/download/v4/$apk',
        );
        expect(found?.sha256, sha);
      }
      service.dispose();
    });
  }

  test('selects exact Flutter APK by versionCode, not release date', () async {
    final service = AppUpdateService(
      client: MockClient((request) async {
        if (request.url.host == 'api.github.com') {
          return http.Response(
            jsonEncode([
              release('old-high-code', '2026-01-01T00:00:00Z'),
              release('new-low-code', '2026-02-01T00:00:00Z'),
              release('prerelease', '2026-03-01T00:00:00Z', prerelease: true),
              {
                'tag_name': 'firmware-only',
                'published_at': '2026-04-01T00:00:00Z',
                'assets': [
                  {'name': 'frame_film_ark.bin', 'size': 123},
                ],
              },
            ]),
            200,
          );
        }
        return http.Response(
          jsonEncode(
            manifest(request.url.path.contains('old-high-code') ? 8 : 7),
          ),
          200,
        );
      }),
    );
    final found = await service.latest();
    expect(found?.tag, 'old-high-code');
    expect(found?.versionCode, 8);
    expect(found?.isNewerThan(7), isTrue);
    expect(found?.isNewerThan(8), isFalse);
    service.dispose();
  });

  test(
    'ignores releases without exact APK and reports invalid metadata',
    () async {
      final empty = AppUpdateService(
        client: MockClient(
          (_) async => http.Response(
            jsonEncode([
              {
                'published_at': '2026-01-01T00:00:00Z',
                'assets': [
                  {'name': 'app-release.apk', 'size': 100},
                ],
              },
            ]),
            200,
          ),
        ),
      );
      expect(await empty.latest(), isNull);
      empty.dispose();

      final invalid = AppUpdateService(
        client: MockClient(
          (request) async => http.Response(
            jsonEncode(
              request.url.host == 'api.github.com'
                  ? [release('bad', '2026-01-01T00:00:00Z')]
                  : {...manifest(4), 'packageName': 'other.app'},
            ),
            200,
          ),
        ),
      );
      await expectLater(invalid.latest(), throwsFormatException);
      invalid.dispose();
    },
  );

  test('web read-only state never offers download or installation', () async {
    final service = AppUpdateService(
      client: MockClient(
        (request) async => http.Response(
          jsonEncode(
            request.url.host == 'api.github.com'
                ? [release('v4', '2026-01-01T00:00:00Z')]
                : manifest(4),
          ),
          200,
        ),
      ),
    );
    final controller = AppUpdateController(
      service: service,
      platform: UnsupportedAppPlatform(),
    );
    await controller.check();
    expect(controller.release?.versionCode, 4);
    expect(controller.installed, isNull);
    expect(controller.canDownload, isFalse);
    expect(controller.message, contains('网页版'));
    controller.dispose();
    service.dispose();
  });
}
