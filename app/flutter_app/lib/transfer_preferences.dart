import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'transfer_preferences_stub.dart'
    if (dart.library.js_interop) 'transfer_preferences_web.dart'
    as web;

enum TransferPreference { auto, ble }

abstract interface class TransferPreferences {
  Future<TransferPreference> load();
  Future<void> save(TransferPreference preference);
}

class LocalTransferPreferences implements TransferPreferences {
  const LocalTransferPreferences();

  static const _channel = MethodChannel('org.framefilm.ark/methods');
  bool get _isAndroid =>
      !kIsWeb && defaultTargetPlatform == TargetPlatform.android;

  @override
  Future<TransferPreference> load() async {
    final value = _isAndroid
        ? await _channel.invokeMethod<String>('loadTransferPreference')
        : web.loadTransferPreference();
    return value == 'ble' ? TransferPreference.ble : TransferPreference.auto;
  }

  @override
  Future<void> save(TransferPreference preference) async {
    if (_isAndroid) {
      await _channel.invokeMethod<void>('saveTransferPreference', {
        'preference': preference.name,
      });
    } else {
      web.saveTransferPreference(preference.name);
    }
  }
}
