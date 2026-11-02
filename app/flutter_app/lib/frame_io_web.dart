import 'dart:js_interop';
import 'dart:typed_data';

@JS('Blob')
extension type _Blob._(JSObject _) implements JSObject {
  external factory _Blob(JSArray<JSAny?> parts);
}

@JS('URL.createObjectURL')
external String _url(_Blob blob);
@JS('URL.revokeObjectURL')
external void _revoke(String url);
@JS('document.createElement')
external _Anchor _create(String tag);

extension type _Anchor(JSObject _) implements JSObject {
  external set href(String value);
  external set download(String value);
  external void click();
}

bool get canDownloadFrame => true;
Future<void> downloadFrame(Uint8List bytes, String name) async {
  final url = _url(_Blob([bytes.toJS].toJS));
  try {
    final link = _create('a');
    link.href = url;
    link.download = name;
    link.click();
    await Future<void>.delayed(const Duration(seconds: 1));
  } finally {
    _revoke(url);
  }
}
