import 'package:flutter_bloc/flutter_bloc.dart';
import 'discovery_state.dart';
import 'mdns_service.dart';

class DiscoveryCubit extends Cubit<DiscoveryState> {
  final MdnsService _mdnsService;

  DiscoveryCubit(this._mdnsService) : super(const DiscoveryIdle());

  Future<void> startDiscovery() async {
    emit(const DiscoverySearching());
    try {
      final results = await _mdnsService.discover(
        timeout: const Duration(seconds: 10),
      );
      if (results.isNotEmpty) {
        emit(DiscoveryFound(results));
      } else {
        emit(const DiscoveryTimeout());
      }
    } catch (_) {
      emit(const DiscoveryTimeout());
    }
  }
}
