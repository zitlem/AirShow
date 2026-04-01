import 'package:flutter_test/flutter_test.dart';
import 'package:airshow_sender/discovery/discovery_cubit.dart';
import 'package:airshow_sender/discovery/discovery_state.dart';
import 'package:airshow_sender/discovery/mdns_service.dart';

class MockMdnsService extends MdnsService {
  final List<ReceiverInfo> results;
  final bool shouldThrow;

  MockMdnsService({this.results = const [], this.shouldThrow = false});

  @override
  Future<List<ReceiverInfo>> discover({
    Duration timeout = const Duration(seconds: 10),
  }) async {
    if (shouldThrow) throw Exception('mDNS error');
    return results;
  }
}

void main() {
  group('DiscoveryCubit', () {
    test('initial state is DiscoveryIdle', () {
      final cubit = DiscoveryCubit(MockMdnsService());
      expect(cubit.state, isA<DiscoveryIdle>());
      cubit.close();
    });

    test(
      'startDiscovery emits [DiscoverySearching, DiscoveryFound] when receivers found',
      () async {
        final receiver = const ReceiverInfo(
          name: 'MyPC',
          host: '192.168.1.10',
          port: 7400,
        );
        final cubit = DiscoveryCubit(MockMdnsService(results: [receiver]));

        final statesFuture = cubit.stream.take(2).toList();
        await cubit.startDiscovery();
        final states = await statesFuture;

        expect(states, hasLength(2));
        expect(states[0], isA<DiscoverySearching>());
        expect(states[1], isA<DiscoveryFound>());
        expect((states[1] as DiscoveryFound).receivers, [receiver]);
        await cubit.close();
      },
    );

    test(
      'startDiscovery emits [DiscoverySearching, DiscoveryTimeout] when no receivers',
      () async {
        final cubit = DiscoveryCubit(MockMdnsService(results: []));

        final statesFuture = cubit.stream.take(2).toList();
        await cubit.startDiscovery();
        final states = await statesFuture;

        expect(states, hasLength(2));
        expect(states[0], isA<DiscoverySearching>());
        expect(states[1], isA<DiscoveryTimeout>());
        await cubit.close();
      },
    );

    test(
      'startDiscovery emits [DiscoverySearching, DiscoveryTimeout] on exception',
      () async {
        final cubit = DiscoveryCubit(MockMdnsService(shouldThrow: true));

        final statesFuture = cubit.stream.take(2).toList();
        await cubit.startDiscovery();
        final states = await statesFuture;

        expect(states, hasLength(2));
        expect(states[0], isA<DiscoverySearching>());
        expect(states[1], isA<DiscoveryTimeout>());
        await cubit.close();
      },
    );
  });
}
