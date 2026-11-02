import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'app.dart';
import 'preview_device_gateway.dart';

void main() => runApp(
  FrameFilmApp(
    gateway: kIsWeb && Uri.base.queryParameters['preview'] == '1'
        ? PreviewDeviceGateway()
        : null,
  ),
);
