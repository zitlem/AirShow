import 'package:equatable/equatable.dart';

class ReceiverInfo extends Equatable {
  final String name;
  final String host;
  final int port;
  const ReceiverInfo({required this.name, required this.host, required this.port});
  @override
  List<Object?> get props => [name, host, port];
}

sealed class DiscoveryState extends Equatable {
  const DiscoveryState();
}

final class DiscoveryIdle extends DiscoveryState {
  const DiscoveryIdle();
  @override
  List<Object?> get props => [];
}

final class DiscoverySearching extends DiscoveryState {
  const DiscoverySearching();
  @override
  List<Object?> get props => [];
}

final class DiscoveryFound extends DiscoveryState {
  final List<ReceiverInfo> receivers;
  const DiscoveryFound(this.receivers);
  @override
  List<Object?> get props => [receivers];
}

final class DiscoveryTimeout extends DiscoveryState {
  const DiscoveryTimeout();
  @override
  List<Object?> get props => [];
}
