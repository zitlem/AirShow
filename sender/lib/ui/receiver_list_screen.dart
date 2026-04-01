import 'package:flutter/material.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import '../discovery/discovery_cubit.dart';
import '../discovery/discovery_state.dart';
import '../session/session_cubit.dart';
import '../session/session_state.dart';
import 'mirroring_screen.dart';

class ReceiverListScreen extends StatefulWidget {
  const ReceiverListScreen({super.key});

  @override
  State<ReceiverListScreen> createState() => _ReceiverListScreenState();
}

class _ReceiverListScreenState extends State<ReceiverListScreen> {
  final _ipController = TextEditingController();
  final _portController = TextEditingController(text: '7400');

  @override
  void dispose() {
    _ipController.dispose();
    _portController.dispose();
    super.dispose();
  }

  void _connectManual(BuildContext context) {
    final ip = _ipController.text.trim();
    final portText = _portController.text.trim();
    if (ip.isEmpty) return;
    final port = int.tryParse(portText) ?? 7400;
    final receiver = ReceiverInfo(name: ip, host: ip, port: port);
    context.read<SessionCubit>().startMirroring(receiver);
    Navigator.push(
      context,
      MaterialPageRoute<void>(
        builder: (_) => const MirroringScreen(),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return BlocListener<SessionCubit, SessionState>(
      listenWhen: (previous, current) => current is SessionConnecting,
      listener: (context, state) {
        if (state is SessionConnecting) {
          Navigator.push(
            context,
            MaterialPageRoute<void>(
              builder: (_) => const MirroringScreen(),
            ),
          );
        }
      },
      child: Scaffold(
        appBar: AppBar(
          title: const Text('AirShow Sender'),
        ),
        body: BlocBuilder<DiscoveryCubit, DiscoveryState>(
          builder: (context, discoveryState) {
            return switch (discoveryState) {
              DiscoveryIdle() => _buildIdleContent(context),
              DiscoverySearching() => _buildSearchingContent(),
              DiscoveryFound(:final receivers) =>
                _buildFoundContent(context, receivers),
              DiscoveryTimeout() => _buildTimeoutContent(context),
            };
          },
        ),
      ),
    );
  }

  Widget _buildIdleContent(BuildContext context) {
    return Center(
      child: ElevatedButton.icon(
        onPressed: () => context.read<DiscoveryCubit>().startDiscovery(),
        icon: const Icon(Icons.search),
        label: const Text('Scan for Receivers'),
      ),
    );
  }

  Widget _buildSearchingContent() {
    return const Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(),
          SizedBox(height: 16),
          Text('Searching for AirShow receivers...'),
        ],
      ),
    );
  }

  Widget _buildFoundContent(BuildContext context, List<ReceiverInfo> receivers) {
    return ListView.builder(
      itemCount: receivers.length,
      itemBuilder: (context, index) {
        final receiver = receivers[index];
        return ListTile(
          leading: const Icon(Icons.cast),
          title: Text(receiver.name),
          subtitle: Text('${receiver.host}:${receiver.port}'),
          onTap: () {
            context.read<SessionCubit>().startMirroring(receiver);
            Navigator.push(
              context,
              MaterialPageRoute<void>(
                builder: (_) => const MirroringScreen(),
              ),
            );
          },
        );
      },
    );
  }

  Widget _buildTimeoutContent(BuildContext context) {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const Icon(Icons.wifi_off, size: 64, color: Colors.grey),
          const SizedBox(height: 16),
          const Text(
            'No receivers found',
            textAlign: TextAlign.center,
            style: TextStyle(fontSize: 18),
          ),
          const SizedBox(height: 8),
          const Text(
            'Make sure AirShow is running on your computer and connected to the same network.',
            textAlign: TextAlign.center,
            style: TextStyle(color: Colors.grey),
          ),
          const SizedBox(height: 32),
          const Text(
            'Connect manually',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _ipController,
            decoration: const InputDecoration(
              labelText: 'IP Address',
              hintText: '192.168.1.100',
              border: OutlineInputBorder(),
            ),
            keyboardType: const TextInputType.numberWithOptions(decimal: true),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _portController,
            decoration: const InputDecoration(
              labelText: 'Port',
              hintText: '7400',
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.number,
          ),
          const SizedBox(height: 16),
          ElevatedButton(
            onPressed: () => _connectManual(context),
            child: const Text('Connect'),
          ),
          const SizedBox(height: 16),
          TextButton.icon(
            onPressed: () => context.read<DiscoveryCubit>().startDiscovery(),
            icon: const Icon(Icons.refresh),
            label: const Text('Scan Again'),
          ),
        ],
      ),
    );
  }
}
