import 'dart:async';
import 'dart:js_interop';
import 'dart:typed_data';

@JS('window.localStorage')
external _Storage get _storage;

extension type _Storage(JSObject _) implements JSObject {
  external String? getItem(String key);
  external void setItem(String key, String value);
}

@JS('document.createElement')
external _Input _create(String tag);

@JS('document.body')
external _Body get _body;

extension type _Body(JSObject _) implements JSObject {
  external void appendChild(_Input input);
}

extension type _Input(JSObject _) implements JSObject {
  external set type(String value);
  external set accept(String value);
  external set hidden(bool value);
  external set onchange(JSFunction value);
  external set oncancel(JSFunction value);
  external _Files? get files;
  external void click();
  external void remove();
}

extension type _Files(JSObject _) implements JSObject {
  external int get length;
  external _File? item(int index);
}

extension type _File(JSObject _) implements JSObject {
  external int get size;
  external JSPromise<JSArrayBuffer> arrayBuffer();
}

const _key = 'framefilm-ark-pass-profile-v1';
Future<String?> loadPassportDraft() async => _storage.getItem(_key);
Future<void> savePassportDraft(String json) async =>
    _storage.setItem(_key, json);
Future<Uint8List?> pickPassportAvatar() {
  final result = Completer<Uint8List?>();
  final input = _create('input');
  input.type = 'file';
  input.accept = 'image/*';
  // Keep the chooser's input attached until selection finishes. Detached
  // inputs can lose their browser node while the native chooser is open.
  input.hidden = true;
  _body.appendChild(input);
  input.oncancel = (() {
    if (!result.isCompleted) result.complete(null);
  }).toJS;
  input.onchange = (() {
    if (result.isCompleted) return;
    final file = input.files?.item(0);
    if (file == null) {
      result.complete(null);
      return;
    }
    if (file.size > 8 * 1024 * 1024) {
      result.completeError(const FormatException('图片超过 8 MiB'));
      return;
    }
    file.arrayBuffer().toDart.then(
      (v) => result.complete(v.toDart.asUint8List()),
      onError: result.completeError,
    );
  }).toJS;
  try {
    input.click();
  } catch (error, stack) {
    result.completeError(error, stack);
  }
  return result.future.whenComplete(() => input.remove());
}
