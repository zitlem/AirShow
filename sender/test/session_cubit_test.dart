import 'dart:async';
import 'package:flutter_test/flutter_test.dart';
import 'package:airshow_sender/session/session_cubit.dart';
import 'package:airshow_sender/session/session_state.dart';
import 'package:airshow_sender/session/airshow_channel.dart';
import 'package:airshow_sender/discovery/discovery_state.dart';

class MockAirShowChannel extends AirShowChannel {
  final StreamController<Map<String, dynamic>> _controller =
      StreamController<Map<String, dynamic>>.broadcast();

  final List<String> methodCalls = [];

  void emitEvent(Map<String, dynamic> event) {
    _controller.add(event);
  }

  void dispose() {
    _controller.close();
  }

  @override
  Stream<Map<String, dynamic>> get sessionEvents => _controller.stream;

  @override
  Future<void> startCapture(String host, int port) async {
    methodCalls.add('startCapture($host:$port)');
  }

  @override
  Future<void> stopCapture() async {
    methodCalls.add('stopCapture');
  }
}

void main() {
  group('SessionCubit', () {
    late MockAirShowChannel mockChannel;
    late SessionCubit cubit;

    setUp(() {
      mockChannel = MockAirShowChannel();
      cubit = SessionCubit(mockChannel);
    });

    tearDown(() async {
      await cubit.close();
      mockChannel.dispose();
    });

    test('initial state is SessionIdle', () {
      expect(cubit.state, isA<SessionIdle>());
      expect((cubit.state as SessionIdle).error, isNull);
    });

    test(
      'startMirroring emits SessionConnecting then SessionMirroring on CONNECTED event',
      () async {
        final receiver = const ReceiverInfo(
          name: 'TestPC',
          host: '192.168.1.50',
          port: 7400,
        );

        final statesFuture = cubit.stream.take(2).toList();

        // Start mirroring — emits SessionConnecting synchronously, then awaits startCapture
        unawaited(cubit.startMirroring(receiver));

        // Allow SessionConnecting to be emitted
        await Future<void>.delayed(Duration.zero);

        // Emit CONNECTED event from native side (triggers SessionMirroring)
        mockChannel.emitEvent({'type': 'CONNECTED'});

        final states = await statesFuture;

        expect(states[0], isA<SessionConnecting>());
        expect((states[0] as SessionConnecting).receiver, receiver);
        expect(states[1], isA<SessionMirroring>());
        expect((states[1] as SessionMirroring).receiver, receiver);
      },
    );

    test('stopMirroring emits [SessionStopping, SessionIdle]', () async {
      final statesFuture = cubit.stream.take(2).toList();
      await cubit.stopMirroring();
      final states = await statesFuture;

      expect(states[0], isA<SessionStopping>());
      expect(states[1], isA<SessionIdle>());
      expect(mockChannel.methodCalls, contains('stopCapture'));
    });

    test(
      'startMirroring emits SessionIdle with error on native ERROR event',
      () async {
        final receiver = const ReceiverInfo(
          name: 'TestPC',
          host: '192.168.1.50',
          port: 7400,
        );

        // Collect all states: SessionConnecting + SessionIdle(error)
        final statesFuture = cubit.stream.take(2).toList();

        unawaited(cubit.startMirroring(receiver));

        // Allow SessionConnecting to be emitted
        await Future<void>.delayed(Duration.zero);

        // Emit ERROR event from native side
        mockChannel.emitEvent({'type': 'ERROR', 'message': 'Connection refused'});

        final states = await statesFuture;

        expect(states[0], isA<SessionConnecting>());
        expect(states[1], isA<SessionIdle>());
        expect((states[1] as SessionIdle).error, 'Connection refused');
      },
    );
  });
}
