import 'package:flutter/material.dart';
import 'package:flutter_bloc/flutter_bloc.dart';
import 'discovery/discovery_cubit.dart';
import 'discovery/mdns_service.dart';
import 'session/session_cubit.dart';
import 'session/airshow_channel.dart';
import 'ui/receiver_list_screen.dart';

class AirShowSenderApp extends StatelessWidget {
  const AirShowSenderApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiBlocProvider(
      providers: [
        BlocProvider(create: (_) => DiscoveryCubit(MdnsService())),
        BlocProvider(create: (_) => SessionCubit(AirShowChannel())),
      ],
      child: MaterialApp(
        title: 'AirShow Sender',
        theme: ThemeData(
          colorSchemeSeed: Colors.blue,
          useMaterial3: true,
        ),
        home: const ReceiverListScreen(),
      ),
    );
  }
}
