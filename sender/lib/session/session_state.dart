import 'package:equatable/equatable.dart';
import '../discovery/discovery_state.dart';

sealed class SessionState extends Equatable {
  const SessionState();
}

final class SessionIdle extends SessionState {
  final String? error;
  const SessionIdle({this.error});
  @override
  List<Object?> get props => [error];
}

final class SessionConnecting extends SessionState {
  final ReceiverInfo receiver;
  const SessionConnecting(this.receiver);
  @override
  List<Object?> get props => [receiver];
}

final class SessionMirroring extends SessionState {
  final ReceiverInfo receiver;
  const SessionMirroring(this.receiver);
  @override
  List<Object?> get props => [receiver];
}

final class SessionStopping extends SessionState {
  const SessionStopping();
  @override
  List<Object?> get props => [];
}
