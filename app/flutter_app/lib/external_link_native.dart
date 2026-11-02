import 'package:flutter/services.dart';

Future<void> openExternalLink(String url) async {
  await const MethodChannel('org.framefilm.ark/methods')
      .invokeMethod<void>('openExternalLink', {'url': url});
}
