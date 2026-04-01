import 'package:flutter/material.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import '../session/session_cubit.dart';
import '../session/session_state.dart';

class MirroringScreen extends StatelessWidget {
  const MirroringScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return BlocConsumer<SessionCubit, SessionState>(
      listener: (context, state) {
        if (state is SessionIdle) {
          // Navigate back to receiver list
          if (Navigator.canPop(context)) {
            Navigator.pop(context);
          }
          // Show error snackbar if there is one
          if (state.error != null) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text('Error: ${state.error}'),
                backgroundColor: Colors.red,
              ),
            );
          }
        }
      },
      builder: (context, state) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('Mirroring'),
            automaticallyImplyLeading: false,
          ),
          body: switch (state) {
            SessionConnecting(:final receiver) => Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const CircularProgressIndicator(),
                  const SizedBox(height: 16),
                  Text('Connecting to ${receiver.name}...'),
                ],
              ),
            ),
            SessionMirroring(:final receiver) => Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.cast_connected, size: 80, color: Colors.blue),
                  const SizedBox(height: 24),
                  Text(
                    receiver.name,
                    style: const TextStyle(
                      fontSize: 24,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Mirroring active',
                    style: TextStyle(color: Colors.grey, fontSize: 16),
                  ),
                  const SizedBox(height: 48),
                  ElevatedButton.icon(
                    onPressed: () =>
                        context.read<SessionCubit>().stopMirroring(),
                    icon: const Icon(Icons.stop),
                    label: const Text('Stop'),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.red,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 48,
                        vertical: 16,
                      ),
                    ),
                  ),
                ],
              ),
            ),
            SessionStopping() => const Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  CircularProgressIndicator(),
                  SizedBox(height: 16),
                  Text('Stopping...'),
                ],
              ),
            ),
            // SessionIdle is handled in listener (navigation back)
            _ => const Center(child: CircularProgressIndicator()),
          },
        );
      },
    );
  }
}
