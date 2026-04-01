import 'dart:async';
import 'package:flutter_bloc/flutter_bloc.dart';
import '../discovery/discovery_state.dart';
import 'airshow_channel.dart';
import 'session_state.dart';

class SessionCubit extends Cubit<SessionState> {
  final AirShowChannel _channel;
  StreamSubscription<Map<String, dynamic>>? _eventSubscription;

  SessionCubit(this._channel) : super(const SessionIdle()) {
    _eventSubscription = _channel.sessionEvents.listen(_onNativeEvent);
  }

  Future<void> startMirroring(ReceiverInfo receiver) async {
    emit(SessionConnecting(receiver));
    try {
      await _channel.startCapture(receiver.host, receiver.port);
      // SessionMirroring is emitted by the native EventChannel on CONNECTED event
    } catch (e) {
      emit(SessionIdle(error: e.toString()));
    }
  }

  Future<void> stopMirroring() async {
    emit(const SessionStopping());
    await _channel.stopCapture();
    emit(const SessionIdle());
  }

  void _onNativeEvent(Map<String, dynamic> event) {
    switch (event['type']) {
      case 'CONNECTED':
        if (state is SessionConnecting) {
          emit(SessionMirroring((state as SessionConnecting).receiver));
        }
      case 'DISCONNECTED':
        emit(SessionIdle(error: event['reason'] as String?));
      case 'ERROR':
        emit(SessionIdle(error: event['message'] as String?));
    }
  }

  @override
  Future<void> close() {
    _eventSubscription?.cancel();
    return super.close();
  }
}
