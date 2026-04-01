import 'package:multicast_dns/multicast_dns.dart';
import 'package:flutter_multicast_lock/flutter_multicast_lock.dart';
import 'discovery_state.dart';

class MdnsService {
  final _lock = FlutterMulticastLock();
  final _client = MDnsClient();

  Future<List<ReceiverInfo>> discover({
    Duration timeout = const Duration(seconds: 10),
  }) async {
    await _lock.acquireMulticastLock();
    try {
      await _client.start();
      final receivers = <ReceiverInfo>[];

      await for (final ptr in _client
          .lookup<PtrResourceRecord>(
            ResourceRecordQuery.serverPointer('_airshow._tcp.local'),
          )
          .timeout(timeout, onTimeout: (_) {})) {
        await for (final srv in _client.lookup<SrvResourceRecord>(
          ResourceRecordQuery.service(ptr.domainName),
        )) {
          receivers.add(
            ReceiverInfo(name: srv.name, host: srv.target, port: srv.port),
          );
          break;
        }
      }
      return receivers;
    } finally {
      _client.stop();
      await _lock.releaseMulticastLock();
    }
  }
}
