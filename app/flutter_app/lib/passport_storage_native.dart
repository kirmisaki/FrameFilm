import 'package:flutter/services.dart';

const _channel = MethodChannel('org.framefilm.ark/methods');
Future<String?> loadPassportDraft() =>
    _channel.invokeMethod<String>('loadPassportDraft');
Future<void> savePassportDraft(String json) =>
    _channel.invokeMethod<void>('savePassportDraft', {'json': json});
Future<Uint8List?> pickPassportAvatar() =>
    _channel.invokeMethod<Uint8List>('pickPassportAvatar');
