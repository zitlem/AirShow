# Phase 10: Android Sender MVP - Research

**Researched:** 2026-04-01
**Domain:** Flutter Android (Kotlin native plugin, MediaProjection, MediaCodec, TCP streaming, mDNS discovery, BLoC)
**Confidence:** HIGH

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SEND-01 | User can mirror their Android device screen to AirShow via the companion sender app | MediaProjection + VirtualDisplay + MediaCodec H.264 pipeline confirmed; native Kotlin plugin via MethodChannel is the correct integration path; foreground service with mediaProjection type required on Android 14+ |
| SEND-05 | Sender app captures and streams device audio alongside screen mirror (Android: system audio not available without root — documented limitation) | AudioPlaybackCapture (API 29+) captures system audio from apps that allow it; microphone fallback via AudioRecord for devices < API 29 or when apps opt out; limitation documented in REQUIREMENTS.md |
| DISC-06 | Sender app auto-discovers AirShow receivers on local network via mDNS | multicast_dns 0.3.3 (flutter.dev, locked in STATE.md) with flutter_multicast_lock 1.2.0 workaround for Android physical device multicast filtering; NsdManager alternative available but multicast_dns is the locked cross-platform package |
| DISC-07 | Sender app supports manual IP entry for networks where mDNS is blocked | Simple text field with IP + port entry; connect flow identical to discovered connection once address is known |
</phase_requirements>

---

## Summary

Phase 10 builds the Android sender half of the AirShow companion app. The Flutter scaffold exists at `sender/` (Phase 9). This phase adds everything to make it functional on Android: mDNS discovery with a receiver list UI, a manual IP fallback, and the core capture-encode-stream pipeline.

The key architectural decision from STATE.md — "native-handles-media" — determines the entire design. Dart/Flutter handles only UI state and session lifecycle. All screen capture, H.264 encoding, audio capture, and TCP socket writes happen in a Kotlin foreground service. Flutter communicates with the native layer via MethodChannel (control commands: start/stop) and EventChannel (session status events back to Dart). This division keeps Dart out of the 30fps encoding hot path where MethodChannel serialization overhead would cause frame drops.

The mDNS discovery uses `multicast_dns` 0.3.3 (the locked STATE.md choice). On Android physical devices, multicast packets are filtered by the WiFi driver unless the app holds a multicast lock — `flutter_multicast_lock` 1.2.0 fixes this. `flutter_foreground_task` 9.2.1 manages the persistent foreground service with notification and Stop button.

Audio on Android is more constrained than on other platforms. `AudioPlaybackCapture` (API 29+) can only capture audio from apps that declare `android:allowAudioPlaybackCapture="true"` or target API 29+ (which allows capture by default). Apps that explicitly opt out (music players, banking apps) cannot be captured. Microphone audio is always capturable. This matches the REQUIREMENTS.md note: "Android: system audio not available without root — documented limitation."

**Primary recommendation:** Build a single Kotlin `AirShowCaptureService` that owns MediaProjection, VirtualDisplay, MediaCodec (H.264 encoder), AudioRecord/AudioPlaybackCapture, and a `java.net.Socket` client. Flutter's Dart layer manages the BLoC state machine for discovery → connecting → mirroring → stopped. The foreground service posts a persistent notification with a Stop action button.

---

## User Constraints (from STATE.md Decisions)

### Locked Decisions
- **v2.0 stack:** Flutter 3.41.6 (3.41.5 was locked, 3.41.6 is installed) for sender app; receiver stack unchanged (C++17 + Qt 6.8 + GStreamer)
- **Protocol transport:** Custom 16-byte binary TCP framing: `type (1B) | flags (1B) | length (4B) | PTS (8B)` — no third-party transport library
- **Native-handles-media rule:** Dart controls only session state; native plugin handles capture + encode + socket send (MethodChannel too slow for 30fps frame data)
- **mDNS package:** `multicast_dns` 0.3.3 (flutter.dev) — covers all 5 platforms
- **Port 7400** for AirShow protocol

### Claude's Discretion
- BLoC vs Cubit for state management (Cubit is appropriate for this phase's linear state machine)
- Whether to add `go_router` or use Flutter's built-in Navigator for screen routing (simple 2-screen app; Navigator.push is sufficient)
- Exact layout of the receiver list UI and manual IP entry UI
- Whether to include a "connecting" progress screen or go directly from list → streaming

### Deferred Ideas (OUT OF SCOPE for Phase 10)
- iOS sender (Phase 11)
- macOS sender (Phase 12)
- Windows sender (Phase 13)
- QR code scan path (Phase 11/14)
- Web interface (Phase 14)
- Quality negotiation arbitration (deferred per Phase 9 plan — echo-back only in v1)

---

## Standard Stack

### Core Flutter Packages

| Package | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `multicast_dns` | 0.3.3 | mDNS service discovery | Locked in STATE.md; flutter.dev maintained; only package covering all 5 platforms |
| `flutter_multicast_lock` | 1.2.0 | Android WiFi multicast lock | Required fix for multicast_dns on physical Android devices; without it, mDNS returns no results |
| `flutter_bloc` | 9.1.1 | BLoC/Cubit state management | Industry standard; used in virtually all serious Flutter apps; Cubit variant suits linear state machine |
| `bloc` | 9.2.0 | Core BLoC library | Peer dependency of flutter_bloc |
| `equatable` | 2.0.8 | Value equality for BLoC states | Eliminates boilerplate equals/hashCode in state classes; standard companion to flutter_bloc |
| `flutter_foreground_task` | 9.2.1 | Android foreground service with notification | Supports `mediaProjection` service type; provides notification Stop button; handles Android 14+ foreground service type requirements |
| `permission_handler` | 12.0.1 | Runtime permission requests | Standard cross-platform permission API; handles RECORD_AUDIO, FOREGROUND_SERVICE_MEDIA_PROJECTION on Android |

### Native Android (Kotlin) — no additional Gradle dependencies

| API | Android Version | Purpose |
|-----|----------------|---------|
| `MediaProjectionManager` | API 21+ (5.0) | Request user consent for screen capture |
| `MediaProjection` + `VirtualDisplay` | API 21+ (5.0) | Create virtual screen that feeds capture Surface |
| `MediaCodec` (hardware H.264) | API 16+ (4.1) | Encode raw frames to H.264 NAL units |
| `AudioPlaybackCapture` | API 29+ (10.0) | Capture system audio (conditional) |
| `AudioRecord` | API 3+ | Capture microphone audio (unconditional fallback) |
| `java.net.Socket` | All | TCP client connection to receiver port 7400 |
| `ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION` | API 29+ | Foreground service type declaration |

### Supporting

| Package | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `cupertino_icons` | ^1.0.8 | iOS-style icons | Already in scaffold; keep for cross-platform UI |
| `flutter_lints` | ^6.0.0 | Lint rules | Already in scaffold; keep |

**Installation:**
```bash
cd /home/sanya/Desktop/MyAirShow/sender
~/flutter/bin/flutter pub add flutter_bloc bloc equatable multicast_dns flutter_multicast_lock flutter_foreground_task permission_handler
```

**Version verification (confirmed 2026-04-01 via pub.dev API):**
- `flutter_bloc`: 9.1.1
- `bloc`: 9.2.0
- `multicast_dns`: 0.3.3
- `flutter_multicast_lock`: 1.2.0
- `flutter_foreground_task`: 9.2.1
- `permission_handler`: 12.0.1
- `equatable`: 2.0.8
- `go_router`: 17.1.0 (available but NOT needed — simple 2-screen push navigation is sufficient)

---

## Architecture Patterns

### Recommended Project Structure

```
sender/
├── lib/
│   ├── main.dart                        # app entry, FlutterForegroundTask.initCommunicationPort()
│   ├── app.dart                         # MaterialApp + theme
│   ├── discovery/
│   │   ├── discovery_cubit.dart         # Cubit: idle → searching → found([receivers]) → error
│   │   ├── discovery_state.dart         # state classes + ReceiverInfo value object
│   │   └── mdns_service.dart            # multicast_dns + multicast_lock wrapper
│   ├── session/
│   │   ├── session_cubit.dart           # Cubit: idle → connecting → mirroring → stopping → idle
│   │   ├── session_state.dart           # state classes
│   │   └── airshow_channel.dart         # MethodChannel + EventChannel bridge to Kotlin
│   └── ui/
│       ├── receiver_list_screen.dart    # shows discovered receivers + manual IP entry
│       └── mirroring_screen.dart        # shows "mirroring active" + Stop button (foreground state)
├── android/
│   └── app/
│       └── src/main/
│           ├── AndroidManifest.xml      # MODIFIED: permissions + service declaration
│           └── kotlin/com/airshow/airshow_sender/
│               ├── MainActivity.kt      # registers MethodChannel + EventChannel
│               ├── AirShowCaptureService.kt   # foreground service: capture + encode + stream
│               └── H264Encoder.kt       # MediaCodec H.264 encoder wrapper
└── test/
    ├── discovery_cubit_test.dart
    └── session_cubit_test.dart
```

### Pattern 1: Discovery with multicast_dns + Multicast Lock

**What:** Acquire Android multicast lock first, then start `MDnsClient.start()`. Query for `_airshow._tcp.local`. Collect PTR → SRV → A/AAAA records. Release lock when discovery is stopped.

**When to use:** Discovery screen is active. Stop when session is established.

```dart
// Source: multicast_dns pub.dev example + flutter_multicast_lock README
import 'package:multicast_dns/multicast_dns.dart';
import 'package:flutter_multicast_lock/flutter_multicast_lock.dart';

class MdnsService {
  final _lock = FlutterMulticastLock();
  final _client = MDnsClient();

  Future<List<ReceiverInfo>> discover({Duration timeout = const Duration(seconds: 10)}) async {
    await _lock.acquireMulticastLock();
    try {
      await _client.start();
      final receivers = <ReceiverInfo>[];
      
      await for (final ptr in _client
          .lookup<PtrResourceRecord>(ResourceRecordQuery.serverPointer('_airshow._tcp.local'))
          .timeout(timeout, onTimeout: (_) {})) {
        await for (final srv in _client
            .lookup<SrvResourceRecord>(ResourceRecordQuery.service(ptr.domainName))) {
          receivers.add(ReceiverInfo(name: srv.name, host: srv.target, port: srv.port));
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
```

**Critical detail:** Service type must be `_airshow._tcp.local` (note: append `.local` for the PTR query). Android NsdManager requires a trailing dot in the service name; multicast_dns adds `.local` automatically for PTR queries.

### Pattern 2: Session State Machine (Cubit)

**What:** Linear state machine: `SessionIdle` → `SessionConnecting(receiver)` → `SessionMirroring(receiver)` → `SessionStopping` → `SessionIdle`. State transitions are driven by MethodChannel results and EventChannel events from Kotlin.

```dart
// Source: flutter_bloc 9.x Cubit pattern (bloclibrary.dev)
class SessionCubit extends Cubit<SessionState> {
  final AirShowChannel _channel;

  SessionCubit(this._channel) : super(const SessionIdle()) {
    _channel.sessionEvents.listen(_onNativeEvent);
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
        emit(SessionIdle(error: event['reason']));
      case 'ERROR':
        emit(SessionIdle(error: event['message']));
    }
  }
}
```

### Pattern 3: MethodChannel + EventChannel Bridge

**What:** Flutter communicates with Kotlin via two channels:
- `MethodChannel('com.airshow/capture')` — control: `startCapture({host, port})`, `stopCapture()`
- `EventChannel('com.airshow/capture_events')` — status stream back to Dart: `CONNECTED`, `DISCONNECTED`, `ERROR`

Native layer NEVER returns frame data to Dart — only status events. The TCP socket and H.264 frames are handled entirely in Kotlin.

```dart
// Source: Flutter platform channels documentation (flutter.dev)
class AirShowChannel {
  static const _method = MethodChannel('com.airshow/capture');
  static const _events = EventChannel('com.airshow/capture_events');

  Stream<Map<String, dynamic>> get sessionEvents =>
      _events.receiveBroadcastStream().cast();

  Future<void> startCapture(String host, int port) =>
      _method.invokeMethod('startCapture', {'host': host, 'port': port});

  Future<void> stopCapture() =>
      _method.invokeMethod('stopCapture');
}
```

```kotlin
// Source: Flutter MethodChannel/EventChannel Android documentation
class MainActivity : FlutterActivity() {
    private val METHOD_CHANNEL = "com.airshow/capture"
    private val EVENT_CHANNEL = "com.airshow/capture_events"

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, METHOD_CHANNEL)
            .setMethodCallHandler { call, result ->
                when (call.method) {
                    "startCapture" -> {
                        val host = call.argument<String>("host")!!
                        val port = call.argument<Int>("port")!!
                        startCaptureService(host, port)
                        result.success(null)
                    }
                    "stopCapture" -> {
                        stopCaptureService()
                        result.success(null)
                    }
                    else -> result.notImplemented()
                }
            }
        
        EventChannel(flutterEngine.dartExecutor.binaryMessenger, EVENT_CHANNEL)
            .setStreamHandler(captureEventStreamHandler)
    }
}
```

### Pattern 4: AirShowCaptureService (Kotlin Foreground Service)

**What:** A bound `Service` that owns the entire capture-encode-stream pipeline. Runs in a background thread/coroutine. Sends status events back to the EventChannel sink.

**Pipeline:**
```
MediaProjection.createVirtualDisplay(surface) 
    → MediaCodec input Surface
    → MediaCodec H.264 encoder (hardware)
    → ByteBuffer output (Annex-B NAL units)
    → Wrap in 16-byte AirShow frame header
    → java.net.Socket.getOutputStream().write(...)
```

**Key configuration values:**
```kotlin
// Source: Android MediaCodec documentation
val format = MediaFormat.createVideoFormat("video/avc", width, height).apply {
    setInteger(MediaFormat.KEY_COLOR_FORMAT, 
               MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
    setInteger(MediaFormat.KEY_BIT_RATE, 4_000_000)     // 4 Mbps default
    setInteger(MediaFormat.KEY_FRAME_RATE, 30)
    setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 2)     // keyframe every 2 seconds
    setInteger(MediaFormat.KEY_PROFILE, 
               MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline)
    setInteger(MediaFormat.KEY_LEVEL,
               MediaCodecInfo.CodecProfileLevel.AVCLevel31)
}
val encoder = MediaCodec.createEncoderByType("video/avc")
encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
val inputSurface = encoder.createInputSurface()  // feed to VirtualDisplay
encoder.start()
```

**SPS/PPS handling:** Android hardware encoders emit SPS+PPS in a dedicated `BUFFER_FLAG_CODEC_CONFIG` output buffer (not in-band before keyframes). This config buffer must be sent as a frame with `type=0x01, flags=0x01` (VIDEO_NAL keyframe) or prepended to every IDR frame. The receiver's GStreamer `h264parse` element will handle both Annex-B stream and codec config data.

**Audio capture (conditional on API level):**
```kotlin
// Source: Android AudioPlaybackCapture documentation
val audioRecord = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
    // System audio via AudioPlaybackCapture
    val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection)
        .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
        .addMatchingUsage(AudioAttributes.USAGE_GAME)
        .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
        .build()
    AudioRecord.Builder()
        .setAudioPlaybackCaptureConfig(config)
        .setAudioFormat(AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .setSampleRate(44100)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build())
        .build()
} else {
    // Microphone fallback for API < 29
    AudioRecord(MediaRecorder.AudioSource.MIC, 44100,
                AudioFormat.CHANNEL_IN_STEREO,
                AudioFormat.ENCODING_PCM_16BIT, bufferSize)
}
```

### Pattern 5: 16-Byte Frame Header (from AirShowHandler.h — receiver side)

**What:** Every outgoing data packet from Kotlin must use the frame format locked in STATE.md and already implemented in `AirShowHandler.h` on the receiver side.

```kotlin
// Source: AirShowHandler.h (receiver frame header spec, confirmed 2026-04-01)
fun buildFrameHeader(
    type: Byte,      // 0x01=VIDEO_NAL, 0x02=AUDIO, 0x03=KEEPALIVE
    flags: Byte,     // 0x01=keyframe, 0x02=end_of_AU
    length: Int,     // payload byte count (NOT including header)
    ptsNs: Long      // nanoseconds
): ByteArray {
    val header = ByteBuffer.allocate(16).order(ByteOrder.BIG_ENDIAN)
    header.put(type)
    header.put(flags)
    header.putInt(length)
    header.putLong(ptsNs)
    header.putShort(0)  // reserved
    return header.array()
}
```

**Newline-terminated JSON handshake (must precede binary frames):**
```kotlin
// Source: AirShowHandler.cpp handshake protocol (Phase 9, confirmed)
val hello = JSONObject().apply {
    put("type", "HELLO")
    put("version", 1)
    put("deviceName", Build.MODEL)
    put("codec", "h264")
    put("maxResolution", "${width}x${height}")
    put("targetBitrate", 4_000_000)
    put("fps", 30)
}
socket.outputStream.write("$hello\n".toByteArray())
// Then read HELLO_ACK JSON line before sending frames
val ack = socket.inputStream.bufferedReader().readLine()
```

### Pattern 6: flutter_foreground_task Stop Button

**What:** The foreground service notification includes a "Stop" button that calls back into `TaskHandler.onNotificationButtonPressed()`.

```dart
// Source: flutter_foreground_task 9.2.1 documentation
await FlutterForegroundTask.startService(
  serviceId: 1001,
  notificationTitle: 'AirShow Mirroring',
  notificationText: 'Mirroring to ${receiver.name}',
  notificationButtons: [
    const NotificationButton(id: 'btn_stop', text: 'Stop'),
  ],
  callback: startAirShowTaskCallback,
);
```

```dart
class AirShowTaskHandler extends TaskHandler {
  @override
  void onNotificationButtonPressed(String id) {
    if (id == 'btn_stop') {
      FlutterForegroundTask.stopService();
      // Send message to main isolate to emit SessionIdle
      FlutterForegroundTask.sendDataToMain({'type': 'STOP_REQUESTED'});
    }
  }
}
```

### Anti-Patterns to Avoid

- **Sending H.264 frame bytes through MethodChannel/EventChannel:** MethodChannel serializes data as Dart-native types. Sending 100KB+ frames per 33ms through the channel bridge will drop frames and stall the event loop. The Kotlin service must write directly to the TCP socket.
- **Requesting MediaProjection token without a foreground service already running (Android 10+):** On API 29+, calling `getMediaProjection()` without a running foreground service throws `SecurityException`. The service must be started before `onActivityResult` processes the projection token.
- **Reusing MediaProjection intent across sessions:** Each `createScreenCaptureIntent()` result can only be used once. Reconnecting requires a new user consent dialog.
- **Not acquiring multicast lock before MDnsClient.start():** multicast_dns silently returns empty results on physical Android devices without the lock. Always wrap mDNS lookup with `flutter_multicast_lock`.
- **Hardcoded video resolution:** Use `WindowMetrics.getBounds()` (API 30+) or `DisplayMetrics` (fallback) to get current screen resolution. Do not hardcode 1920×1080 — it will produce a VirtualDisplay with wrong aspect ratio on small phones.
- **Creating MediaCodec encoder with `createByType("video/avc")` without checking hardware support:** Use `MediaCodecList(MediaCodecList.REGULAR_CODECS).findEncoderForFormat(format)` to find a hardware encoder first; fall back to `createByType` (which may select software) if none is found.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Android multicast lock management | Custom WifiManager multicast lock | `flutter_multicast_lock` 1.2.0 | Handles lifecycle, lock reference counting, and permission declaration automatically |
| mDNS PTR/SRV resolution | Raw DNS-SD socket parsing | `multicast_dns` 0.3.3 | Already the locked STATE.md choice; handles PTR→SRV→A record chaining |
| Foreground service with notification | Custom Kotlin Service subclass + NotificationCompat | `flutter_foreground_task` 9.2.1 | Handles Android 14 service type declarations, notification channels, boot restart, and two-way isolate communication |
| Runtime permission UI dialogs | Manual `ActivityCompat.requestPermissions()` | `permission_handler` 12.0.1 | Handles the Android permission result callback plumbing; provides a clean Dart Future API |
| BLoC state management boilerplate | Custom ChangeNotifier or setState | `flutter_bloc` 9.1.1 | Standard in the ecosystem; testable without UI; `BlocBuilder` handles rebuild efficiently |
| H.264 codec selection | Hardcoded codec name `"OMX.qcom.video.encoder.avc"` | `MediaCodecList.findEncoderForFormat()` | Hardware encoder names vary by OEM; let Android select the best available encoder |

**Key insight:** The Kotlin capture/encode/stream layer is the hardest part and must be custom — no Flutter plugin does the full AirShow-specific pipeline (VirtualDisplay → MediaCodec → custom TCP framing). Everything around it (discovery, permissions, foreground service, state management) has excellent off-the-shelf packages that save significant effort.

---

## Common Pitfalls

### Pitfall 1: multicast_dns Returns Nothing on Android Physical Devices

**What goes wrong:** `MDnsClient.lookup()` returns an empty stream on a physical Android device. Works fine in the Android emulator or on other platforms.

**Why it happens:** Android's WiFi driver filters out multicast packets to conserve battery. Apps that receive multicast must hold a `WifiManager.MulticastLock`. Flutter apps do not hold this lock by default.

**How to avoid:** Acquire `flutter_multicast_lock` before calling `MDnsClient.start()`. Add `CHANGE_WIFI_MULTICAST_STATE` to AndroidManifest.xml (the `flutter_multicast_lock` package adds this automatically via its own manifest merge).

**Warning signs:** Discovery works on emulator or iOS/macOS but returns no results on a real Android device on the same network.

**Issue reference:** [flutter/flutter#155499](https://github.com/flutter/flutter/issues/155499) — confirmed open as of 2026.

### Pitfall 2: MediaProjection Requires Foreground Service Before Token Use (Android 10+)

**What goes wrong:** `getMediaProjection(resultCode, data)` throws `SecurityException: Media projections require a foreground service of type ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION`.

**Why it happens:** From API 29, Android requires the foreground service to be running when `getMediaProjection()` is called — not just before starting the VirtualDisplay. The correct order is:
1. `createScreenCaptureIntent()` → show dialog to user
2. User approves → `onActivityResult` fires
3. Start foreground service (must be running before step 4)
4. Call `getMediaProjection(resultCode, data)` inside the service

**How to avoid:** Pass the `resultCode` and `Intent` data from `onActivityResult` to the service via the start `Intent` extras. Call `getMediaProjection()` from within `Service.onStartCommand()` after the service is in the foreground.

**Warning signs:** Crash with `SecurityException` on Android 10+ devices immediately after user grants permission.

### Pitfall 3: SPS/PPS Not Embedded in IDR Frames on Some Android OEMs

**What goes wrong:** The H.264 stream starts correctly but the receiver's GStreamer decoder (`h264parse`) stalls or produces corrupted video. The first IDR frame lacks SPS/PPS parameters.

**Why it happens:** Android hardware H.264 encoders on most OEMs emit SPS+PPS in a separate output buffer flagged `BUFFER_FLAG_CODEC_CONFIG`. Subsequent IDR (keyframe) buffers do NOT include inline SPS/PPS. This differs from software encoders and some PC encoders. The receiver's GStreamer pipeline needs `h264parse` configured to insert SPS/PPS before IDR frames.

**How to avoid two ways:**
1. (Preferred) Cache the SPS+PPS config buffer and prepend it to every IDR frame before writing to the socket.
2. Send the config buffer as a special control frame (type=0x01, flags=0x03) before the first video frame.

The receiver's GStreamer `h264parse ! avdec_h264` pipeline with `config-interval=-1` on `h264parse` will re-insert SPS/PPS before every IDR automatically if the codec_data caps metadata is set correctly.

**Warning signs:** Video plays for exactly one keyframe interval then freezes; GStreamer logs show `h264parse: no SPS/PPS found`.

### Pitfall 4: AudioPlaybackCapture Silently Captures Nothing from Opted-Out Apps

**What goes wrong:** Audio stream is connected and AudioRecord is recording but the output is silence. No error or exception is thrown.

**Why it happens:** Apps that target Android 10+ and explicitly set `android:allowAudioPlaybackCapture="false"` or use `AudioAttributes.ALLOW_CAPTURE_BY_SYSTEM_ONLY` are excluded from capture silently. This includes most music streaming apps (Spotify, YouTube Music) and banking apps.

**How to avoid:** Document the limitation clearly in the UI ("System audio from some apps may not be captured"). Always offer microphone audio as an alternative. The REQUIREMENTS.md already acknowledges this: "system audio not available without root — documented limitation."

**Warning signs:** The user reports that their music is not being mirrored even though the app shows audio is being captured.

### Pitfall 5: VirtualDisplay Resolution Mismatch Causes Black Screen

**What goes wrong:** The VirtualDisplay is created with a fixed resolution (e.g., 1920×1080) but the phone's screen is 1080×2400 (portrait 20:9 aspect). The VirtualDisplay crops or stretches incorrectly, and the MediaCodec encoder receives black-bordered frames.

**Why it happens:** `createVirtualDisplay()` scales the screen to the provided width/height. If the aspect ratio differs from the physical screen, pixels are wasted or cropped.

**How to avoid:**
```kotlin
// Use actual screen metrics (API 30+) or DisplayMetrics (fallback)
val windowManager = context.getSystemService(WindowManager::class.java)
val metrics = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
    windowManager.currentWindowMetrics
    Pair(windowManager.currentWindowMetrics.bounds.width(),
         windowManager.currentWindowMetrics.bounds.height())
} else {
    val dm = DisplayMetrics()
    @Suppress("DEPRECATION")
    windowManager.defaultDisplay.getMetrics(dm)
    Pair(dm.widthPixels, dm.heightPixels)
}
// Clamp to max resolution agreed in HELLO_ACK
val (w, h) = clampToMaxResolution(metrics.first, metrics.second, maxWidth, maxHeight)
```

**Warning signs:** Receiver shows black bars; output video has wrong aspect ratio.

### Pitfall 6: TCP Socket Blocks Encoding Thread on Network Stall

**What goes wrong:** `socket.outputStream.write(data)` blocks when the receiver's TCP receive buffer fills up (e.g., slow WiFi, busy receiver). This stalls the MediaCodec output drain loop, which eventually causes the encoder to stall and drop frames.

**Why it happens:** `java.net.Socket` output is synchronous by default. A network stall causes `write()` to block indefinitely.

**How to avoid:** Set `socket.setSoTimeout(5000)` on the socket. If `write()` throws `SocketTimeoutException`, signal a session error and close the session cleanly. The bitrate should also be tuned (4 Mbps default is appropriate for most home WiFi; offer a lower-quality mode for slower networks).

**Warning signs:** Encoder frames are dropped; receiver shows frozen video during WiFi congestion.

---

## Code Examples

### AndroidManifest.xml — Required Additions

```xml
<!-- Source: Android foreground service types documentation + flutter_foreground_task README -->
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- Existing permissions (add these) -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE_MEDIA_PROJECTION" />
    <uses-permission android:name="android.permission.RECORD_AUDIO" />
    <uses-permission android:name="android.permission.POST_NOTIFICATIONS" />
    <!-- flutter_multicast_lock adds this automatically via manifest merge: -->
    <!-- android.permission.CHANGE_WIFI_MULTICAST_STATE -->

    <application ...>
        <!-- Existing activity ... -->

        <!-- AirShow capture foreground service -->
        <service
            android:name=".AirShowCaptureService"
            android:foregroundServiceType="mediaProjection"
            android:exported="false" />
    </application>
</manifest>
```

### build.gradle.kts — minSdk Must Be 21

```kotlin
// Source: Flutter 3.41.6 default is minSdk=24; MediaProjection requires API 21+
// AudioPlaybackCapture requires API 29 — feature-flagged in code, not minSdk
// No minSdk change needed: Flutter 3.41.6 default of 24 covers MediaProjection
defaultConfig {
    applicationId = "com.airshow.airshow_sender"
    minSdk = 24  // Flutter 3.41.6 default; do NOT lower (breaks other Flutter plugins)
    targetSdk = 35
    // ...
}
```

### Discovery Cubit States

```dart
// Source: flutter_bloc 9.x Cubit pattern
sealed class DiscoveryState extends Equatable {
  const DiscoveryState();
}

final class DiscoveryIdle extends DiscoveryState {
  const DiscoveryIdle();
  @override List<Object?> get props => [];
}

final class DiscoverySearching extends DiscoveryState {
  const DiscoverySearching();
  @override List<Object?> get props => [];
}

final class DiscoveryFound extends DiscoveryState {
  final List<ReceiverInfo> receivers;
  const DiscoveryFound(this.receivers);
  @override List<Object?> get props => [receivers];
}

final class DiscoveryTimeout extends DiscoveryState {
  const DiscoveryTimeout();  // triggers manual IP entry field
  @override List<Object?> get props => [];
}

class ReceiverInfo extends Equatable {
  final String name;
  final String host;
  final int port;
  const ReceiverInfo({required this.name, required this.host, required this.port});
  @override List<Object?> get props => [name, host, port];
}
```

### MediaProjection Consent Flow (Activity + Service handoff)

```kotlin
// Source: Android MediaProjection documentation
// In MainActivity.kt — request consent
private val projectionResult = registerForActivityResult(
    ActivityResultContracts.StartActivityForResult()
) { result ->
    if (result.resultCode == RESULT_OK) {
        // Start service and pass the token as Intent extras
        val serviceIntent = Intent(this, AirShowCaptureService::class.java).apply {
            putExtra("resultCode", result.resultCode)
            putExtra("data", result.data)
            putExtra("host", pendingHost)
            putExtra("port", pendingPort)
        }
        ContextCompat.startForegroundService(this, serviceIntent)
    }
}

fun requestScreenCapture(host: String, port: Int) {
    pendingHost = host
    pendingPort = port
    val manager = getSystemService(MediaProjectionManager::class.java)
    projectionResult.launch(manager.createScreenCaptureIntent())
}
```

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Flutter SDK | All sender development | Yes | 3.41.6 (at ~/flutter) | — |
| Java 21 (OpenJDK) | Gradle / Android build | Yes | 21.0.10 | — |
| Android SDK | flutter build apk / flutter run | **No** | — | Must install cmdline-tools; see Wave 0 |
| Android device or emulator | End-to-end testing | **Unknown** | — | Emulator via Android SDK; physical device preferred for MediaProjection testing |
| AirShow receiver (Phase 9) | Phase 10 end-to-end test | Partial (Phase 9 plans done) | — | Run binary from Phase 9 build on local machine |

**Missing dependencies with no fallback:**
- **Android SDK:** `flutter build apk` and `flutter run` require the Android SDK. Neither `sdkmanager` nor `adb` are in PATH. Wave 0 must include Android SDK installation.
  - Install path: download `commandlinetools-linux-*.zip` from https://developer.android.com/studio#command-tools, extract to `~/android-sdk/cmdline-tools/latest/`, then:
    ```bash
    ~/android-sdk/cmdline-tools/latest/bin/sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0"
    ~/flutter/bin/flutter config --android-sdk ~/android-sdk
    ```

**Missing dependencies with fallback:**
- Physical Android device: emulator can be used for unit testing and mDNS simulation, but MediaProjection + AudioPlaybackCapture require a physical device for full end-to-end verification. Plan must include a manual verification step on a physical device.

**Note on Flutter PATH:** Flutter is installed at `~/flutter/bin` but is not in PATH. All `flutter` commands in plan tasks must use the absolute path `~/flutter/bin/flutter` or the plan must include a `export PATH=~/flutter/bin:$PATH` step.

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Flutter test (built-in) — `flutter_test` SDK package |
| Config file | `sender/analysis_options.yaml` (existing) |
| Quick run command | `cd /home/sanya/Desktop/MyAirShow/sender && ~/flutter/bin/flutter test test/` |
| Full suite command | `cd /home/sanya/Desktop/MyAirShow/sender && ~/flutter/bin/flutter test --coverage` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DISC-06 | DiscoveryCubit transitions: idle → searching → found | unit | `flutter test test/discovery_cubit_test.dart` | Wave 0 |
| DISC-07 | Manual IP entry connects via same session path as discovered | unit | `flutter test test/session_cubit_test.dart` | Wave 0 |
| SEND-01 | Screen appears on receiver (VirtualDisplay → MediaCodec → TCP → GStreamer display) | smoke (manual) | Run apk on device + observe receiver display | — |
| SEND-05 | Audio plays on receiver alongside video | smoke (manual) | Run apk on device + listen on receiver speakers | — |
| SEND-01 | Stop button ends mirroring, returns to receiver list | smoke (manual) | Tap Stop in notification + observe app state | — |

**Note:** MediaProjection, MediaCodec, and TCP streaming cannot be unit-tested in a Dart test environment — they require a real Android device. All capture/encode/stream logic tests are manual smoke tests.

### Sampling Rate
- **Per task commit:** `~/flutter/bin/flutter test test/` (Cubit unit tests, < 5 seconds)
- **Per wave merge:** `~/flutter/bin/flutter test --coverage && ~/flutter/bin/flutter analyze`
- **Phase gate:** Full suite green + manual device smoke test (screen appears on receiver, audio heard, Stop button works) before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `sender/test/discovery_cubit_test.dart` — covers DISC-06 state machine
- [ ] `sender/test/session_cubit_test.dart` — covers DISC-07 + session lifecycle
- [ ] Android SDK installation: `~/android-sdk/cmdline-tools/latest/bin/sdkmanager ...` — required before `flutter build apk`
- [ ] `sender/pubspec.yaml` — add all Phase 10 dependencies (`flutter pub add ...`)

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `setState()` for all Flutter state | BLoC/Cubit (flutter_bloc 9.x) | Established pattern since 2022 | Testable without UI; clear event/state separation |
| Dart TCP socket for frame streaming | Native Kotlin socket (native-handles-media rule) | Phase 9 locked decision | Avoids MethodChannel serialization overhead at 30fps |
| `WifiManager.MulticastLock` (manual) | `flutter_multicast_lock` plugin | 2023+ | Handles Android foreground/background lifecycle automatically |
| `android.net.nsd.NsdManager` (Android-only) | `multicast_dns` (cross-platform Dart) | Locked in STATE.md | Single Dart codebase for discovery across Android/iOS/macOS/Windows |
| `MediaProjectionManager.getMediaProjection()` before foreground service | Foreground service must be running first (API 29+ hard requirement) | Android 10 (2019) | Sequence error = immediate SecurityException crash |

**Deprecated/outdated:**
- `WifiManager.MulticastLock` direct usage: still works but `flutter_multicast_lock` is the Flutter-idiomatic wrapper.
- `EventChannel` for frame data: explicitly ruled out by the native-handles-media rule. Do not attempt to stream H.264 frames through EventChannel.

---

## Open Questions

1. **Screen resolution negotiation after HELLO_ACK**
   - What we know: The HELLO_ACK echoes back the sender-requested resolution unchanged (Phase 9 "echo-back" approach). The sender constructs its VirtualDisplay with the resolution it requested.
   - What's unclear: Should the sender clamp its requested resolution to 1280×720 by default to reduce encode bitrate and latency, or use the full screen resolution?
   - Recommendation: Default to 1280×720 max (or actual screen res if smaller). The native layer should clamp to this after reading HELLO_ACK's `acceptedResolution`.

2. **Audio encoding: raw PCM vs. AAC in Phase 10**
   - What we know: The 16-byte frame header supports `type=0x02` (AUDIO). The receiver side has not yet wired audio frames from AirShow connections into `audioAppsrc()`. Phase 9 plans only addressed video NAL injection.
   - What's unclear: Whether Phase 10 should add audio (SEND-05 is in scope), and whether to send raw PCM or AAC-encoded audio.
   - Recommendation: Send raw 16-bit PCM (44100Hz stereo) in Phase 10 for simplicity. The receiver's GStreamer pipeline can use `rawaudioparse` or `audioconvert`. AAC encoding via `MediaCodec` can be added in a follow-up without protocol changes (just change the `type` byte).

3. **Receiver-side audio framing not yet implemented**
   - What we know: `AirShowHandler.h` defines `kTypeAudio = 0x02` but Phase 9 plans only inject video NAL units into `MediaPipeline::videoAppsrc()`. Audio frames from the sender will arrive at the receiver but be silently dropped.
   - What's unclear: Whether Phase 10 should also wire the receiver's audio path, or whether that is expected to be a follow-up.
   - Recommendation: Phase 10 plan should include a receiver-side task to wire audio frames (type=0x02) from `AirShowHandler` into `MediaPipeline::audioAppsrc()`. This is a small addition to `AirShowHandler::processFrame()`. Without it, SEND-05 cannot be verified end-to-end.

---

## Sources

### Primary (HIGH confidence)
- `src/protocol/AirShowHandler.h` — Frame header spec, port 7400, handshake protocol (confirmed 2026-04-01)
- `.planning/STATE.md ## Decisions` — Locked: multicast_dns 0.3.3, native-handles-media rule, Flutter 3.41.6
- `~/flutter/packages/flutter_tools/gradle/src/main/kotlin/FlutterExtension.kt` — `minSdkVersion = 24` confirmed (2026-04-01)
- `sender/android/local.properties` → `flutter.sdk=/home/sanya/flutter` — Flutter installation confirmed
- `flutter doctor` output (2026-04-01) — Android SDK absent confirmed
- [Android MediaProjection documentation](https://developer.android.com/media/grow/media-projection) — API 14+ requirements, foreground service type, one-time consent
- [Android AudioPlaybackCapture documentation](https://developer.android.com/media/platform/av-capture) — API 29+ requirement, opt-in/opt-out behavior
- pub.dev API queries (2026-04-01): flutter_bloc=9.1.1, multicast_dns=0.3.3, flutter_multicast_lock=1.2.0, flutter_foreground_task=9.2.1, permission_handler=12.0.1

### Secondary (MEDIUM confidence)
- [flutter/flutter#155499](https://github.com/flutter/flutter/issues/155499) — multicast_dns Android physical device bug confirmed open
- [flutter_foreground_task pub.dev](https://pub.dev/packages/flutter_foreground_task) — mediaProjection service type support confirmed in v9.2.1 docs
- [Flutter platform channels documentation](https://docs.flutter.dev/platform-integration/platform-channels) — MethodChannel/EventChannel patterns
- [bloclibrary.dev](https://bloclibrary.dev/bloc-concepts/) — flutter_bloc 9.x Cubit patterns

### Tertiary (LOW confidence)
- WebSearch results on MediaCodec SPS/PPS behavior — cross-referenced with multiple Android developer blog posts; main risk is OEM variation which cannot be fully tested without physical devices

---

## Metadata

**Confidence breakdown:**
- Standard stack (Flutter packages): HIGH — all versions verified via pub.dev API 2026-04-01
- Architecture (native Kotlin pipeline): HIGH — MediaProjection+MediaCodec+VirtualDisplay is the only viable approach; pattern is well-established
- mDNS discovery: HIGH — multicast_dns 0.3.3 is the locked package; multicast lock requirement confirmed by open Flutter issue
- Audio capture: MEDIUM — AudioPlaybackCapture limitation (opt-out apps silent) confirmed by official docs; OEM variation in implementation is a real risk
- SPS/PPS pitfall: MEDIUM — confirmed for hardware encoders but exact behavior varies by OEM chipset (Qualcomm vs. MediaTek vs. Exynos)
- Environment availability: HIGH — Flutter 3.41.6 installed, Java 21 installed, Android SDK absent confirmed

**Research date:** 2026-04-01
**Valid until:** 2026-07-01 (stable APIs; Flutter/Android SDK versions change slowly; flutter_foreground_task is the most likely package to bump a major version)
