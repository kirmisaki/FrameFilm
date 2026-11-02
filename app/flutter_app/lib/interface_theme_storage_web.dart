import 'dart:js_interop';

@JS('window.localStorage')
external _Storage get _storage;

extension type _Storage(JSObject _) implements JSObject {
  external String? getItem(String key);
  external void setItem(String key, String value);
}

// Use the same preference key and values as tools/ForFilm/js/ark-theme.js.
const _key = 'framefilm.arkTheme';
Future<String?> readInterfaceTheme() async => _storage.getItem(_key);
Future<void> writeInterfaceTheme(String value) async =>
    _storage.setItem(_key, value);
