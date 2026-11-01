import 'interface_theme_storage_native.dart'
    if (dart.library.js_interop) 'interface_theme_storage_web.dart'
    as storage;

abstract interface class InterfaceThemePreferences {
  Future<bool> load();
  Future<void> save(bool enabled);
}

class LocalInterfaceThemePreferences implements InterfaceThemePreferences {
  const LocalInterfaceThemePreferences();

  @override
  Future<bool> load() async => await storage.readInterfaceTheme() != '0';

  @override
  Future<void> save(bool enabled) =>
      storage.writeInterfaceTheme(enabled ? '1' : '0');
}
