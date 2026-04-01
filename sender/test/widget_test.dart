// Widget smoke test for AirShow Sender app.
//
// Verifies that the app can be constructed and renders without error.

import 'package:flutter_test/flutter_test.dart';
import 'package:airshow_sender/app.dart';

void main() {
  testWidgets('AirShow sender app renders without error', (
    WidgetTester tester,
  ) async {
    // Build our app and trigger a frame.
    await tester.pumpWidget(const AirShowSenderApp());

    // Verify the app bar title is shown.
    expect(find.text('AirShow Sender'), findsOneWidget);
  });
}
