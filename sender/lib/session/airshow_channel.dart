import 'package:flutter/services.dart';

class AirShowChannel {
  static const _method = MethodChannel('com.airshow/capture');
  static const _events = EventChannel('com.airshow/capture_events');

  Stream<Map<String, dynamic>> get sessionEvents =>
      _events.receiveBroadcastStream().map(
        (e) => Map<String, dynamic>.from(e as Map),
      );

  Future<void> startCapture(String host, int port) =>
      _method.invokeMethod('startCapture', {'host': host, 'port': port});

  Future<void> stopCapture() => _method.invokeMethod('stopCapture');
}
