import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'external_link.dart';
import 'workbench_widgets.dart';

bool _licenseRegistered = false;
void _registerProjectLicense() {
  if (_licenseRegistered) return;
  _licenseRegistered = true;
  LicenseRegistry.addLicense(() async* {
    yield LicenseEntryWithLineBreaks([
      'FrameFilm',
    ], await rootBundle.loadString('assets/framefilm_license.txt'));
  });
}

class AboutPanel extends StatelessWidget {
  const AboutPanel({super.key, this.embedded = false});
  final bool embedded;
  Future<void> _open(BuildContext context, String url) async {
    try {
      await openExternalLink(url);
    } catch (error) {
      if (context.mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('无法打开链接：$error')));
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final content = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('关于', style: Theme.of(context).textTheme.titleLarge),
        const SizedBox(height: 16),
        Text('FrameFilm Ark', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        const Text('为彩色电子纸制作、管理与传递画面。'),
        const Divider(height: 32),
        const Text('App 开发维护 · Rafael-ban'),
        const SizedBox(height: 4),
        const Text('基于 FrameFilm 开源项目，感谢原项目作者与所有贡献者。'),
        const SizedBox(height: 16),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: [
            OutlinedButton.icon(
              onPressed: () => _open(context, 'https://github.com/Rafael-ban'),
              icon: const Icon(Icons.person_outline),
              label: const Text('作者主页'),
            ),
            OutlinedButton.icon(
              onPressed: () =>
                  _open(context, 'https://github.com/Rafael-ban/FrameFilm'),
              icon: const Icon(Icons.code),
              label: const Text('项目源码'),
            ),
            OutlinedButton.icon(
              onPressed: () => _open(
                context,
                'https://github.com/Rafael-ban/FrameFilm/issues',
              ),
              icon: const Icon(Icons.feedback_outlined),
              label: const Text('反馈问题'),
            ),
            TextButton(
              onPressed: () {
                _registerProjectLicense();
                showLicensePage(
                  context: context,
                  applicationName: 'FrameFilm Ark',
                  applicationLegalese: 'GPL-3.0',
                );
              },
              child: const Text('开源许可 · GPL-3.0'),
            ),
          ],
        ),
      ],
    );
    return embedded ? content : WorkbenchPanel(child: content);
  }
}
