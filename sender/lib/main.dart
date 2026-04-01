import 'package:flutter/material.dart';

void main() {
  runApp(const AirShowSenderApp());
}

class AirShowSenderApp extends StatelessWidget {
  const AirShowSenderApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'AirShow Sender',
      theme: ThemeData(
        colorSchemeSeed: Colors.blue,
        useMaterial3: true,
      ),
      home: const Scaffold(
        body: Center(
          child: Text(
            'AirShow Sender\nDiscovery & mirroring coming soon',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 24),
          ),
        ),
      ),
    );
  }
}
