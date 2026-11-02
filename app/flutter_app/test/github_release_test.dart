import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:framefilm_ark/github_release_service.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';
import 'package:framefilm_ark/app.dart';

import 'widget_test.dart' show FakeGateway, openPage;

void main() {
  Map<String, Object?> release(
    String date,
    String tag, {
    bool prerelease = false,
    String asset = 'frame_film_ark.bin',
  }) => {
    'published_at': date,
    'tag_name': tag,
    'name': tag,
    'prerelease': prerelease,
    'assets': [
      {
        'name': asset,
        'size': 123,
        'browser_download_url': 'https://example.com/fw.bin',
        'digest': 'sha256:${'a' * 64}',
      },
    ],
  };
  test(
    'latest compatible formal release uses publication time and exact asset',
    () {
      final result = GitHubReleaseService.parseLatest([
        release('2026-01-01', 'v99'),
        release('2026-02-01', 'v1'),
        release('2026-03-01', 'v100', prerelease: true),
        release('2026-04-01', 'v101', asset: 'dock.bin'),
      ]);
      expect(result!.tag, 'v1');
      expect(result.sha256, 'a' * 64);
      expect(GitHubReleaseService.parseLatest([]), isNull);
      expect(
        GitHubReleaseService.parseLatest([
          release('2026-04-01', 'v101', asset: 'dock.bin'),
        ]),
        isNull,
      );
    },
  );
  test('firmware release API uses the canonical repository', () async {
    final service = GitHubReleaseService(
      client: MockClient((request) async {
        expect(request.url.host, 'api.github.com');
        expect(request.url.path, '/repos/Rafael-Ikaros/FrameFilm/releases');
        return http.Response('[]', 200);
      }),
    );
    expect(await service.latest(), isNull);
    service.dispose();
  });
  test('API errors and malformed responses remain retryable', () async {
    for (final code in [403, 429, 500]) {
      final service = GitHubReleaseService(
        client: MockClient((_) async => http.Response('', code)),
      );
      await expectLater(service.latest(), throwsException);
      service.dispose();
    }
    final service = GitHubReleaseService(
      client: MockClient((_) async => http.Response('{}', 200)),
    );
    await expectLater(service.latest(), throwsFormatException);
    service.dispose();
  });
  test('partial download state retains and explicit idle clears', () {
    final previous = DeviceSnapshot.fromMap({
      'firmwareDownload': {'phase': 'downloading', 'canCancel': true},
      'importing': true,
    });
    final retained = DeviceSnapshot.fromMap({'connected': true})
        .retainingTransfer(previous);
    expect(retained.firmwareDownload.canCancel, isTrue);
    expect(retained.importing, isTrue);
    final cleared = DeviceSnapshot.fromMap({
      'firmwareDownload': {'phase': 'idle'},
      'importing': false,
    }).retainingTransfer(retained);
    expect(cleared.firmwareDownload.phase, 'idle');
    expect(cleared.importing, isFalse);
  });
  testWidgets('download cancel is reachable while controller is busy', (
    tester,
  ) async {
    final gateway = FakeGateway();
    addTearDown(gateway.events.close);
    await tester.pumpWidget(FrameFilmApp(gateway: gateway));
    gateway.events.add(
      DeviceSnapshot.fromMap({
        'importing': true,
        'firmwareDownload': {
          'phase': 'downloading',
          'canCancel': true,
          'total': 100,
          'received': 20,
        },
      }),
    );
    await tester.pump();
    await openPage(tester, '设置');
    await tester.ensureVisible(
      find.byKey(const Key('cancel-firmware-download')),
    );
    await tester.pump();
    await tester.tap(find.byKey(const Key('cancel-firmware-download')));
    await tester.pump();
    expect(gateway.calls, contains('cancelFirmwareDownload'));
    await tester.pumpWidget(const SizedBox());
  });
  test(
    'preview supports cancelled download and failure retry without OTA',
    () async {
      final gateway = PreviewDeviceGateway(
        stepDuration: const Duration(milliseconds: 1),
      );
      final controller = DeviceController(gateway);
      await Future<void>.delayed(Duration.zero);
      await controller.command('pickFirmware');
      await Future<void>.delayed(Duration.zero);
      final old = controller.snapshot.importedFirmware;
      await controller.command('downloadFirmware');
      await Future<void>.delayed(Duration.zero);
      controller.busy = true;
      await controller.command('cancelFirmwareDownload');
      controller.busy = false;
      await Future<void>.delayed(Duration.zero);
      expect(controller.snapshot.importedFirmware, same(old));
      expect(controller.snapshot.firmwareDownload.phase, 'cancelled');
      gateway.scenario = PreviewScenario.transferFailure;
      await controller.command('downloadFirmware');
      await Future<void>.delayed(const Duration(milliseconds: 10));
      expect(controller.snapshot.firmwareDownload.phase, 'error');
      expect(controller.snapshot.importedFirmware, same(old));
      await controller.command('downloadFirmware');
      await Future<void>.delayed(const Duration(milliseconds: 15));
      expect(controller.snapshot.firmwareDownload.phase, 'done');
      expect(controller.snapshot.importedFirmware!.version, contains('3.2.5'));
      expect(controller.snapshot.transfer.phase, 'idle');
      controller.dispose();
      gateway.dispose();
    },
  );
}
