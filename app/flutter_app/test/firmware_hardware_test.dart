import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:framefilm_ark/app.dart';
import 'package:framefilm_ark/device_gateway.dart';
import 'package:framefilm_ark/github_release_service.dart';
import 'package:framefilm_ark/preview_device_gateway.dart';

import 'widget_test.dart' show FakeGateway, openPage;

Map<String, Object?> entry(HardwareIdentity hardware) => {
  ...hardware.packageMetadata,
  'asset': hardware.targetId == HardwareIdentity.n4r2.targetId
      ? 'frame_film_ark-n4r2.bin'
      : 'frame_film_ark-n16r8.bin',
  'size': 123,
  'sha256': 'a' * 64,
  'elfSha256': 'b' * 64,
  'displayVersion': '3.2.5',
};
Map<String, Object?> manifest() => {
  'schemaVersion': 1,
  'project': 'frame_film_ark',
  'firmwares': [entry(HardwareIdentity.n4r2), entry(HardwareIdentity.n16r8)],
};
Map<String, Object?> release({
  String date = '2026-11-04',
  bool prerelease = false,
}) => {
  'published_at': date,
  'name': 'Ark',
  'tag_name': 'v3.2.5',
  'prerelease': prerelease,
  'assets': [
    for (final name in [
      'framefilm-ark-firmware.json',
      'frame_film_ark-n4r2.bin',
      'frame_film_ark-n16r8.bin',
    ])
      {
        'name': name,
        'size': 123,
        'browser_download_url': 'https://example.com/$name',
        if (name.endsWith('.bin')) 'digest': 'sha256:${'a' * 64}',
      },
  ],
};

void main() {
  test(
    'same displayed 3.2.5 selects exact n4r2/n16r8 manifest assets',
    () async {
      for (final hardware in [HardwareIdentity.n4r2, HardwareIdentity.n16r8]) {
        var requests = 0;
        final service = GitHubReleaseService(
          client: MockClient((request) async {
            requests++;
            if (request.url.host == 'api.github.com') {
              expect(
                request.url.path,
                '/repos/Rafael-Ikaros/FrameFilm/releases',
              );
              return http.Response(
                jsonEncode([
                  release(date: '2027-01-01', prerelease: true),
                  release(),
                ]),
                200,
              );
            }
            expect(request.url.path, '/framefilm-ark-firmware.json');
            return http.Response(jsonEncode(manifest()), 200);
          }),
        );
        final selected = await service.latest(hardware: hardware);
        expect(selected!.hardware.targetId, hardware.targetId);
        expect(selected.asset, entry(hardware)['asset']);
        expect(selected.displayVersion, '3.2.5');
        expect(selected.elfSha256, 'b' * 64);
        expect(selected.downloadArguments['targetId'], hardware.targetId);
        expect(requests, 2);
        service.dispose();
      }
    },
  );
  test(
    'unknown or mismatched device never defaults by name, screen or 3.2.5',
    () async {
      var requests = 0;
      final service = GitHubReleaseService(
        client: MockClient((_) async {
          requests++;
          return http.Response('[]', 200);
        }),
      );
      await expectLater(service.latest(), throwsStateError);
      final wrong = HardwareIdentity.fromMap({
        ...HardwareIdentity.n4r2.toMap(),
        'flashBytes': 16777216,
      });
      expect(wrong.trusted, false);
      await expectLater(service.latest(hardware: wrong), throwsStateError);
      expect(requests, 0);
      service.dispose();
      final unsupported = DeviceSnapshot.fromMap({
        'connected': true,
        'name': 'FRAMEFILMARK',
        'width': 720,
        'height': 480,
        'version': '3.2.5',
      });
      expect(unsupported.hardware, isNull);
    },
  );
  test(
    'malformed, duplicate, wrong target or asset metadata has no fallback',
    () {
      final variants = <Map<String, Object?>>[
        {...manifest(), 'schemaVersion': 2},
        {
          ...manifest(),
          'firmwares': [
            entry(HardwareIdentity.n4r2),
            entry(HardwareIdentity.n4r2),
          ],
        },
        {
          ...manifest(),
          'firmwares': [
            {...entry(HardwareIdentity.n4r2), 'sha256': 'bad'},
          ],
        },
        {
          ...manifest(),
          'firmwares': [
            {...entry(HardwareIdentity.n4r2), 'elfSha256': 'bad'},
          ],
        },
        {
          ...manifest(),
          'firmwares': [
            {...entry(HardwareIdentity.n4r2), 'psramBus': 'octal'},
          ],
        },
        {
          ...manifest(),
          'firmwares': [
            {...entry(HardwareIdentity.n4r2), 'size': 124},
          ],
        },
        {
          ...manifest(),
          'firmwares': [entry(HardwareIdentity.n16r8)],
        },
        {
          ...manifest(),
          'firmwares': [
            {...entry(HardwareIdentity.n4r2), 'asset': 'frame_film_ark.bin'},
          ],
        },
      ];
      for (final bad in variants) {
        expect(
          () => GitHubReleaseService.selectFirmware(
            release(),
            bad,
            HardwareIdentity.n4r2,
          ),
          throwsFormatException,
        );
      }
    },
  );
  test(
    'latest formal missing manifest fails; HTTP errors remain retryable',
    () async {
      for (final status in [403, 429, 500]) {
        final service = GitHubReleaseService(
          client: MockClient((_) async => http.Response('', status)),
        );
        await expectLater(
          service.latest(hardware: HardwareIdentity.n4r2),
          throwsException,
        );
        service.dispose();
      }
      final service = GitHubReleaseService(
        client: MockClient(
          (_) async => http.Response(
            jsonEncode([
              {...release(), 'assets': []},
            ]),
            200,
          ),
        ),
      );
      await expectLater(
        service.latest(hardware: HardwareIdentity.n4r2),
        throwsFormatException,
      );
      service.dispose();
    },
  );
  test(
    'hardware retains partial state, explicit unknown and disconnect clear it',
    () {
      final old = DeviceSnapshot.fromMap({
        'connected': true,
        'hardware': HardwareIdentity.n16r8.toMap(),
      });
      expect(
        DeviceSnapshot.fromMap({'message': 'battery'})
            .retainingTransfer(old)
            .hardware!
            .targetId,
        HardwareIdentity.n16r8.targetId,
      );
      expect(
        DeviceSnapshot.fromMap({'hardware': null})
            .retainingTransfer(old)
            .hardware,
        isNull,
      );
      expect(
        DeviceSnapshot.fromMap({'connected': false})
            .retainingTransfer(old)
            .hardware,
        isNull,
      );
    },
  );
  test(
    'wrong target blocks imported firmware send and online download',
    () async {
      final gateway = FakeGateway();
      final device = DeviceController(gateway);
      await Future<void>.delayed(Duration.zero);
      gateway.events.add(
        DeviceSnapshot.fromMap({
          'connected': true,
          'hardware': HardwareIdentity.n4r2.toMap(),
          'importedFirmware': {
            'name': 'n16.bin',
            'size': 123,
            'version': '3.2.5',
            'project': 'frame_film_ark',
            'elfSha256': 'b' * 64,
            'fileSha256': 'a' * 64,
            ...HardwareIdentity.n16r8.packageMetadata,
          },
        }),
      );
      await device.command('startFirmwareTransfer');
      expect(gateway.calls, isNot(contains('startFirmwareTransfer')));
      expect(device.errorMessage, contains('不匹配'));
      await device.command(
        'downloadFirmware',
        GitHubFirmwareRelease.demoFor(HardwareIdentity.n16r8).downloadArguments,
      );
      expect(gateway.calls, isNot(contains('downloadFirmware')));
      expect(device.errorMessage, contains('不匹配'));
      device.dispose();
      await gateway.events.close();
    },
  );
  testWidgets(
    'connection card exposes hardware and settings only selected target',
    (tester) async {
      final gateway = PreviewDeviceGateway(
        demoHardware: HardwareIdentity.n16r8,
      );
      addTearDown(gateway.dispose);
      await tester.pumpWidget(FrameFilmApp(gateway: gateway));
      await gateway.invoke('connect');
      await tester.pumpAndSettle();
      await tester.scrollUntilVisible(
        find.text('N16R8'),
        200,
        scrollable: find
            .descendant(
              of: find.byKey(const Key('workbench-content')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      expect(find.text('N16R8'), findsOneWidget);
      expect(find.text('ESP32-S3'), findsOneWidget);
      expect(find.text('16 MB'), findsOneWidget);
      expect(find.text('8 MB · Octal'), findsOneWidget);
      await openPage(tester, '设置');
      await tester.scrollUntilVisible(
        find.text('为 N16R8 查找兼容固件'),
        300,
        scrollable: find
            .descendant(
              of: find.byKey(const Key('workbench-content')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      expect(find.text('为 N16R8 查找兼容固件'), findsOneWidget);
      await tester.ensureVisible(find.byKey(const Key('demo-online-firmware')));
      await tester.tap(find.byKey(const Key('demo-online-firmware')));
      await tester.pump();
      expect(find.textContaining('frame_film_ark-n16r8.bin'), findsOneWidget);
      await tester.pumpWidget(const SizedBox());
    },
  );
}
