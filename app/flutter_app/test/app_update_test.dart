import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:framefilm_ark/app_update.dart';
import 'package:framefilm_ark/app_update_service.dart';
import 'package:framefilm_ark/release_repository.dart';

class TestAppPlatform implements AppUpdatePlatform {
  TestAppPlatform({this.supported = true});
  @override
  final bool supported;
  bool failInfo = false;
  InstalledAppInfo info = InstalledAppInfo(
    packageName: AppReleaseChannel.current.packageName,
    repository: releaseRepository,
    updateAllowed: true,
    versionName: '1.0',
    versionCode: 100,
  );
  AppUpdateState state = const AppUpdateState();
  AppRelease? downloaded;
  @override
  Future<InstalledAppInfo> getInfo() async {
    if (failInfo) throw StateError('cannot read installed package');
    return info;
  }
  @override
  Future<AppUpdateState> getState() async => state;
  @override
  Future<AppUpdateState> download(AppRelease release) async {
    downloaded = release;
    return state = AppUpdateState(
      phase: 'ready', canInstall: true, packageName: release.channel.packageName,
      versionName: release.versionName, versionCode: release.versionCode,
    );
  }
  @override
  Future<AppUpdateState> cancel() async => const AppUpdateState();
  @override
  Future<AppUpdateState> install() async => state;
}

const apk = AppUpdateService.apkName;
const metadata = AppUpdateService.metadataName;
final sha = 'a' * 64;
Map<String, Object?> release(String tag, {
  AppReleaseChannel? channel, String date = '2026-01-01T00:00:00Z',
  bool prerelease = false,
}) {
  channel ??= AppReleaseChannel.current;
  return {
    'tag_name': tag, 'published_at': date, 'prerelease': prerelease,
    'assets': [for (final name in [apk, metadata]) {
      'name': name, 'size': name == apk ? 100 : 20,
      'browser_download_url': releaseAssetUri(channel.repository, tag, name).toString(),
    }],
  };
}
Map<String, Object?> manifest(int versionCode, {AppReleaseChannel? channel}) => {
  'packageName': (channel ?? AppReleaseChannel.current).packageName,
  'versionName': '0.$versionCode.0', 'versionCode': versionCode,
  'apkName': apk, 'sha256': sha, 'size': 100, 'minSdk': 28,
};

void main() {
  testWidgets('single-channel preview cancels, retries and simulates own install', (tester) async {
    final controller = AppUpdateController(preview: true);
    await controller.initialize();
    controller.loadDownloadDemo();
    expect(controller.canDownload, isTrue);
    await controller.download();
    await tester.pump(const Duration(milliseconds: 900));
    expect(controller.state.received, greaterThan(0));
    expect(controller.canCheck, isFalse);
    await controller.cancel();
    expect(controller.state.phase, 'cancelled');
    expect(controller.canDownload, isTrue);
    await tester.pump(const Duration(seconds: 7));
    expect(controller.state.phase, 'cancelled');
    await controller.download();
    await tester.pump(const Duration(seconds: 6));
    expect(controller.state.canInstall, isTrue);
    await controller.install();
    expect(controller.installed?.versionCode, 14);
    expect(controller.installed?.packageName, controller.channel.packageName);
    await controller.check();
    expect(controller.canDownload, isFalse);
    controller.dispose();
  });

  testWidgets('panel has no channel selector or cross-install action', (tester) async {
    final controller = AppUpdateController(preview: true);
    await tester.pumpWidget(MaterialApp(home: Scaffold(body: AppUpdatePanel(controller: controller))));
    expect(find.byType(SegmentedButton<AppReleaseChannel>), findsNothing);
    expect(find.text(releaseRepository), findsOneWidget);
    expect(find.textContaining('仅更新当前应用'), findsOneWidget);
    await tester.pumpWidget(const SizedBox());
    controller.dispose();
  });

  for (final channel in AppReleaseChannel.values) {
    test('exact source and encoded tag for ${channel.name}', () async {
      final service = AppUpdateService(client: MockClient((request) async {
        if (request.url.host == 'api.github.com') {
          expect(request.url.path, '/repos/${channel.repository}/releases');
          return http.Response(jsonEncode([release('ark/v4+5', channel: channel)]), 200);
        }
        expect(isReleaseAssetUri(request.url, repository: channel.repository, tag: 'ark/v4+5', name: metadata), isTrue);
        expect(request.followRedirects, isFalse);
        return http.Response(jsonEncode(manifest(4, channel: channel)), 200);
      }));
      addTearDown(service.dispose);
      final found = await service.latest(channel);
      expect(found?.channel, channel);
      expect(found?.versionCode, 4);
      expect(found?.sha256, sha);
    });
  }

  test('highest versionCode, formal releases and historical opposite packages', () async {
    final service = AppUpdateService(client: MockClient((request) async {
      if (request.url.host == 'api.github.com') {
        return http.Response(jsonEncode([
          release('older-high'), release('newer-low', date: '2026-02-01T00:00:00Z'),
          release('opposite'), release('pre', prerelease: true),
        ]), 200);
      }
      return http.Response(jsonEncode(manifest(
        request.url.path.contains('older-high') ? 108 : 107,
        channel: request.url.path.contains('opposite')
            ? AppReleaseChannel.values.firstWhere((c) => c != AppReleaseChannel.current) : null,
      )), 200);
    }));
    addTearDown(service.dispose);
    final found = await service.latest(AppReleaseChannel.current);
    expect(found?.versionCode, 108);
    expect(found?.isNewerThan(108), isFalse);
  });

  test('rejects duplicate assets, wrong URL/tag and conflicting metadata', () async {
    final source = release('v4');
    for (final variant in [
      {...source, 'assets': [...source['assets'] as List, (source['assets'] as List).first]},
      {...source, 'tag_name': 'wrong-tag'},
      {...source, 'assets': [for (final asset in source['assets'] as List)
        {...asset as Map, 'digest': 'sha256:${'b' * 64}'}]},
      {...source, 'assets': [for (final asset in source['assets'] as List)
        {...asset as Map, 'browser_download_url': 'https://example.com/${asset['name']}'}]},
    ]) {
      final service = AppUpdateService(client: MockClient((request) async => http.Response(
        jsonEncode(request.url.host == 'api.github.com' ? [variant] : manifest(4)), 200,
      )));
      await expectLater(service.latest(AppReleaseChannel.current), throwsFormatException);
      service.dispose();
    }
    for (final bad in [
      {...manifest(4), 'packageName': 'unknown.app'},
      {...manifest(4), 'repository': 'unknown/FrameFilm'},
      {...manifest(4), 'tag': 'another-tag'},
      {...manifest(4), 'sha256': 'bad'},
      {...manifest(4), 'size': 101},
      {...manifest(4), 'minSdk': 0},
    ]) {
      final service = AppUpdateService(client: MockClient((request) async => http.Response(
        jsonEncode(request.url.host == 'api.github.com' ? [source] : bad), 200,
      )));
      await expectLater(service.latest(AppReleaseChannel.current), throwsFormatException);
      service.dispose();
    }
    final service = AppUpdateService(client: MockClient((request) async => http.Response(jsonEncode(
      request.url.host == 'api.github.com' ? [release('a'), release('b')] : {
        ...manifest(4), if (request.url.path.contains('/b/')) 'sha256': 'b' * 64,
      },
    ), 200)));
    await expectLater(service.latest(AppReleaseChannel.current), throwsFormatException);
    service.dispose();
  });

  test('query failure or empty release clears candidate, keeps separately verified cache', () async {
    var status = 200;
    var empty = false;
    final platform = TestAppPlatform();
    final service = AppUpdateService(client: MockClient((request) async => http.Response(
      jsonEncode(request.url.host == 'api.github.com' ? (empty ? [] : [release('v104')]) : manifest(104)), status,
    )));
    final controller = AppUpdateController(service: service, platform: platform);
    addTearDown(service.dispose);
    addTearDown(controller.dispose);
    await controller.check();
    expect(controller.canDownload, isTrue);
    status = 500;
    await controller.check();
    expect(controller.release, isNull);
    expect(controller.canDownload, isFalse);
    status = 200;
    await controller.check();
    await controller.download();
    expect(platform.downloaded?.channel, AppReleaseChannel.current);
    empty = true;
    await controller.check();
    expect(controller.release, isNull);
    expect(controller.state.canInstall, isTrue);
    expect(controller.state.versionCode, 104);
  });

  test('unknown, debug, mismatched repo and unreadable own package cannot query or install', () async {
    var requests = 0;
    final service = AppUpdateService(client: MockClient((_) async {
      requests++;
      return http.Response('[]', 200);
    }));
    addTearDown(service.dispose);
    for (final info in [
      const InstalledAppInfo(packageName: 'unknown', versionName: '1', versionCode: 1),
      InstalledAppInfo(packageName: '${AppReleaseChannel.current.packageName}.debug', repository: releaseRepository, versionName: '1', versionCode: 1),
      InstalledAppInfo(packageName: AppReleaseChannel.current.packageName, repository: 'unknown/FrameFilm', updateAllowed: true, versionName: '1', versionCode: 1),
      InstalledAppInfo(packageName: AppReleaseChannel.current.packageName, repository: releaseRepository, updateAllowed: false, versionName: '1', versionCode: 1),
    ]) {
      final platform = TestAppPlatform()..info = info;
      final controller = AppUpdateController(service: service, platform: platform);
      await controller.check();
      expect(controller.release, isNull);
      expect(controller.canDownload, isFalse);
      expect(controller.state.canInstall, isFalse);
      controller.dispose();
    }
    final platform = TestAppPlatform();
    final controller = AppUpdateController(service: service, platform: platform);
    await controller.initialize();
    platform.failInfo = true;
    await controller.check();
    expect(controller.installed, isNull);
    expect(requests, 0);
    controller.dispose();
  });

  test('same or older own release cannot download; another package cache stays hidden', () async {
    for (final version in [99, 100]) {
      final service = AppUpdateService(client: MockClient((request) async => http.Response(jsonEncode(
        request.url.host == 'api.github.com' ? [release('old')] : manifest(version),
      ), 200)));
      final platform = TestAppPlatform()..state = AppUpdateState(
        phase: 'ready', canInstall: true,
        packageName: AppReleaseChannel.values.firstWhere((c) => c != AppReleaseChannel.current).packageName,
        versionCode: 999, versionName: 'wrong channel',
      );
      final controller = AppUpdateController(service: service, platform: platform);
      await controller.check();
      expect(controller.canDownload, isFalse);
      expect(controller.state.canInstall, isFalse);
      await controller.download();
      expect(platform.downloaded, isNull);
      controller.dispose();
      service.dispose();
    }
  });

  test('web is read only, real controller cannot load demo', () async {
    final service = AppUpdateService(client: MockClient((request) async => http.Response(jsonEncode(
      request.url.host == 'api.github.com' ? [release('v104')] : manifest(104),
    ), 200)));
    final controller = AppUpdateController(service: service, platform: TestAppPlatform(supported: false));
    await controller.check();
    expect(controller.release?.versionCode, 104);
    expect(controller.canDownload, isFalse);
    expect(controller.message, contains('网页版'));
    final found = controller.release;
    controller.loadDownloadDemo();
    expect(controller.release, same(found));
    controller.dispose();
    service.dispose();
  });
}
