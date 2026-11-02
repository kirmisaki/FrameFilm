import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

const _channel = MethodChannel('org.framefilm.ark/methods');
Future<String?> readInterfaceTheme() async =>
    defaultTargetPlatform == TargetPlatform.android
    ? await _channel.invokeMethod<String>('loadInterfaceTheme')
    : null;
Future<void> writeInterfaceTheme(String value) async {
  if (defaultTargetPlatform == TargetPlatform.android) {
    await _channel.invokeMethod<void>('saveInterfaceTheme', {'theme': value});
  }
}
