import 'dart:js_interop';

@JS('window.open')
external JSAny? _open(String url, String target, String features);
Future<void> openExternalLink(String url) async {
  _open(url, '_blank', 'noopener,noreferrer');
}
