import 'dart:js_interop';

@JS('window.localStorage')
external _Storage get _storage;

extension type _Storage(JSObject _) implements JSObject {
  external String? getItem(String key);
  external void setItem(String key, String value);
}

const _key = 'framefilm.arkTransferPreference';
String? loadTransferPreference() => _storage.getItem(_key);
void saveTransferPreference(String value) => _storage.setItem(_key, value);
