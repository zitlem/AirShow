# Architecture Research

**Domain:** AirShow v2.0 — Companion Sender App integration with existing C++17 receiver
**Researched:** 2026-03-30
**Confidence:** MEDIUM-HIGH — receiver architecture is HIGH confidence (existing code); Flutter sender architecture is MEDIUM confidence (platform screen-capture APIs verified via pub.dev and official Android/iOS docs; custom AirShow protocol design is a greenfield decision with no prior implementation)

---

## Context: What Already Exists

The v1 receiver is a C++17 / Qt 6.8 / GStreamer 1.26 desktop application with these components:

| Component | Status | Key Interface |
|-----------|--------|---------------|
| `ProtocolManager` | Existing | `addHandler(unique_ptr<ProtocolHandler>)`, `startAll()`, `stopAll()` |
| `ProtocolHandler` (interface) | Existing | `start()`, `stop()`, `name()`, `isRunning()`, `setMediaPipeline()` |
| `MediaPipeline` | Existing | `initAppsrcPipeline()`, `videoAppsrc()`, `audioAppsrc()`, `setAudioCaps()` |
| `DiscoveryManager` | Existing | Advertises `_airplay._tcp`, `_raop._tcp`, `_googlecast._tcp` via Avahi/Bonjour |
| `SecurityManager` | Existing | `checkConnection()` (sync), `checkConnectionAsync()`, `resolveApproval()` |
| `ConnectionBridge` | Existing | Qt-to-QML bridge for connection state + approval dialog |
| `AirPlayHandler` | Existing | ProtocolHandler impl, uses appsrc injection |
| `CastHandler` | Existing | ProtocolHandler impl, WebRTC pipeline |
| `DlnaHandler` | Existing | ProtocolHandler impl, URI pipeline |
| `MiracastHandler` | Existing | ProtocolHandler impl, MPEG-TS/RTP pipeline |

The new work is:
1. **AirShowHandler** — a new `ProtocolHandler` on the receiver side
2. **Flutter sender app** — a cross-platform app (Android, iOS, macOS, Windows, Linux)
3. **AirShow custom protocol** — the wire format between sender and receiver
4. **Monorepo structure** — where the Flutter app lives alongside the C++ receiver

---

## System Overview: v2.0

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         SENDER SIDE (Flutter App)                         │
│                                                                            │
│  ┌────────────────┐   ┌──────────────────┐   ┌────────────────────────┐  │
│  │  Discovery     │   │  Screen Capture  │   │  Connection UI         │  │
│  │  Service       │   │  Service         │   │  (receiver list,       │  │
│  │  (nsd 4.x)     │   │  (platform ch.)  │   │   connect/disconnect)  │  │
│  └───────┬────────┘   └────────┬─────────┘   └───────────┬────────────┘  │
│          │                     │                          │               │
│  ┌───────▼─────────────────────▼──────────────────────────▼────────────┐  │
│  │                    MirrorSession (Dart BLoC)                         │  │
│  │  State: idle → discovering → connecting → streaming → disconnected   │  │
│  └────────────────────────────┬─────────────────────────────────────────┘  │
│                                │                                            │
│  ┌─────────────────────────────▼──────────────────────────────────────┐    │
│  │              AirShowProtocolClient (Dart + platform channel)        │    │
│  │  TCP connection to receiver port 7400                                │    │
│  │  Sends: Handshake → NALU frames (length-prefixed) + audio frames    │    │
│  └─────────────────────────────┬──────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────────┘
                                  │  TCP port 7400 (LAN)
                                  │
┌──────────────────────────────────────────────────────────────────────────┐
│                         RECEIVER SIDE (C++ / Qt / GStreamer)              │
│                                                                            │
│  ┌────────────────────────────────────────────────────────────────────┐   │
│  │                      ProtocolManager                                │   │
│  │  [AirPlayHandler] [CastHandler] [DlnaHandler] [MiracastHandler]    │   │
│  │  [AirShowHandler]  ← NEW                                           │   │
│  └──────────────────────────────┬─────────────────────────────────────┘   │
│                                 │                                          │
│  ┌──────────────────────────────▼─────────────────────────────────────┐   │
│  │                      MediaPipeline (GStreamer)                       │   │
│  │  initAppsrcPipeline() → videoAppsrc / audioAppsrc                   │   │
│  │  AirShowHandler pushes NALUs via gst_app_src_push_buffer()          │   │
│  └──────────────────────────────┬─────────────────────────────────────┘   │
│                                 │                                          │
│  ┌──────────────────────────────▼─────────────────────────────────────┐   │
│  │                      ReceiverWindow (QML)                            │   │
│  │  qml6glsink + ConnectionBridge (shows "AirShow" protocol + device)  │   │
│  └────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 1. Custom AirShow Protocol Design

### Rationale for a Custom Protocol

Use a custom protocol rather than re-using AirPlay or Cast for sender-to-receiver because:
- AirPlay is reverse-engineered and could break at any iOS update
- Google Cast requires Google-issued device certificates
- A custom protocol can be minimal, simple to implement in both C++ and Dart, and fully under our control
- It enables future features (bidirectional control, pause, resolution negotiation) without third-party constraints

### Transport: TCP with Length-Prefixed Framing

**Use TCP, not UDP, not WebRTC, not RTSP.**

Rationale:
- UDP requires application-level reliability/retransmit for screen content (unacceptable on Wi-Fi where packet loss is common)
- WebRTC adds STUN/TURN/ICE complexity that buys nothing for a local-LAN-only use case; the receiver already has a WebRTC pipeline for Cast but it's heavyweight for a controlled protocol
- RTSP is well-understood but adds a negotiation round-trip and requires an RTSP library on the Flutter side (no good cross-platform option in 2026)
- TCP with length-prefix framing is implemented in 30 lines of C++ and 30 lines of Dart using `dart:io`; it is reliable, ordered, and works on all target platforms

**Wire format — every frame is a tagged length-value message:**

```
┌─────────────────────────────────────────────────────────┐
│  Frame Header (16 bytes, big-endian)                     │
│  ┌───────────┬──────────┬───────────────┬─────────────┐  │
│  │  type (1) │ flags(1) │  length (4)   │  pts_ns (8) │  │
│  └───────────┴──────────┴───────────────┴─────────────┘  │
│                                                           │
│  Payload (length bytes)                                   │
└─────────────────────────────────────────────────────────┘

type:
  0x01 = HANDSHAKE_REQUEST  (sender → receiver, JSON UTF-8 payload)
  0x02 = HANDSHAKE_RESPONSE (receiver → sender, JSON UTF-8 payload)
  0x03 = VIDEO_NALU         (sender → receiver, Annex-B H.264 NALU)
  0x04 = AUDIO_FRAME        (sender → receiver, AAC-LC frame)
  0x05 = KEEPALIVE          (both directions, empty payload)
  0x06 = DISCONNECT         (both directions, empty payload)

flags:
  0x01 = keyframe (for VIDEO_NALU: receiver can start decode)
  0x02 = end-of-stream

pts_ns: presentation timestamp in nanoseconds, monotonic sender clock
        (receiver maps to GstClockTime; use GST_CLOCK_TIME_NONE = 0xFFFFFFFFFFFFFFFF for unknown)
```

**Handshake payload (JSON):**

Sender HANDSHAKE_REQUEST:
```json
{
  "version": 1,
  "device_name": "Sanya's iPhone",
  "device_id": "uuid-v4",
  "video_codec": "h264",
  "video_profile": "baseline",
  "video_width": 1280,
  "video_height": 720,
  "video_fps": 30,
  "audio_codec": "aac-lc",
  "audio_sample_rate": 44100,
  "audio_channels": 2
}
```

Receiver HANDSHAKE_RESPONSE:
```json
{
  "accepted": true,
  "reason": null,
  "receiver_name": "Living Room PC"
}
```

**Discovery: mDNS service type `_airshow._tcp`**

The receiver advertises `_airshow._tcp` on port 7400 alongside its existing `_airplay._tcp` and `_googlecast._tcp` records. The Flutter sender queries for `_airshow._tcp` using the `nsd` package.

TXT records on the receiver side:
```
ver=1
name=Living Room PC
```

### Why Not RTSP

RTSP requires a request-response negotiation cycle (DESCRIBE → SETUP → PLAY) that adds 3 network round-trips before the first frame. For a controlled sender-receiver pair on a LAN, the custom handshake above achieves the same negotiation in one round-trip and is simpler to implement.

### Why Annex-B for Video NALUs

GStreamer's `h264parse` element accepts both AVC (length-prefixed NAL units) and Annex-B (start-code-prefixed). Annex-B is what Android's `MediaCodec` and iOS's `VideoToolbox` naturally produce for H.264 Byte Stream output. Avoiding an AVC-to-Annex-B conversion step on the sender saves CPU and complexity. Set `stream-format=byte-stream` on the `h264parse` element on the receiver side.

---

## 2. AirShowHandler — Receiver Side

### Fits the ProtocolHandler Interface Cleanly

`AirShowHandler` implements `ProtocolHandler` exactly as `AirPlayHandler` and `CastHandler` do:

```cpp
// src/protocol/AirShowHandler.h
class AirShowHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    explicit AirShowHandler(ConnectionBridge* connectionBridge,
                            QObject* parent = nullptr);
    ~AirShowHandler() override;

    // ProtocolHandler interface
    bool        start() override;     // binds QTcpServer on port 7400
    void        stop() override;
    std::string name() const override { return "airshow"; }
    bool        isRunning() const override;
    void        setMediaPipeline(MediaPipeline* pipeline) override;

    // Security integration — same pattern as CastHandler
    void        setSecurityManager(SecurityManager* sm);

    static constexpr uint16_t kPort = 7400;

private slots:
    void onNewConnection();
    void onDataReady();
    void onDisconnected();

private:
    void processFrame(uint8_t type, uint8_t flags, uint64_t ptsNs,
                      const QByteArray& payload);
    void sendHandshakeResponse(bool accepted, const QString& reason = {});

    QTcpServer*       m_server          = nullptr;
    QTcpSocket*       m_client          = nullptr;
    MediaPipeline*    m_pipeline        = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    SecurityManager*  m_securityManager  = nullptr;
    QByteArray        m_readBuffer;     // accumulate partial TCP reads
    bool              m_handshakeDone  = false;
    bool              m_running        = false;
    bool              m_audioCapsSet   = false;
};
```

**Key design choices matching existing handlers:**
- `QTcpServer` / `QTcpSocket` (same as `CastHandler` and `MiracastHandler`) — all I/O on Qt event loop, no manual threading
- Single-session model: new connection replaces existing session (same as CastHandler D-14 rule)
- `SecurityManager::checkConnectionAsync()` — same async approval path as CastHandler (Qt event-loop safe)
- `ConnectionBridge::setConnected()` — updates QML HUD with protocol="airshow", deviceName from handshake
- Frame pushes via `gst_app_src_push_buffer()` into `m_pipeline->videoAppsrc()` and `m_pipeline->audioAppsrc()` — identical to AirPlayHandler

**mDNS advertisement change in DiscoveryManager:**

`DiscoveryManager::start()` must advertise a third service type:
```cpp
advertiser->advertise("_airshow._tcp", name, AirShowHandler::kPort,
                      {{"ver", "1"}, {"name", name}});
```

This is a `DiscoveryManager` modification (not a new component). The `ServiceAdvertiser` interface already supports multiple `advertise()` calls.

**Pipeline mode:**

`AirShowHandler` uses `initAppsrcPipeline()` — the same mode used by AirPlayHandler. No new pipeline mode is needed. Audio caps are set via `setAudioCaps()` on the first audio frame using the codec parameters from the handshake.

---

## 3. Flutter App Architecture

### App Structure (Feature-First BLoC)

```
sender/                              # Flutter app root
├── lib/
│   ├── main.dart
│   ├── features/
│   │   ├── discovery/
│   │   │   ├── bloc/
│   │   │   │   ├── discovery_bloc.dart   # emits ReceiverFound, ReceiverLost
│   │   │   │   └── discovery_event.dart
│   │   │   ├── services/
│   │   │   │   └── nsd_discovery_service.dart  # wraps nsd 4.x package
│   │   │   └── models/
│   │   │       └── airshow_receiver.dart  # {name, host, port, deviceId}
│   │   │
│   │   ├── mirror/
│   │   │   ├── bloc/
│   │   │   │   ├── mirror_bloc.dart       # Idle→Connecting→Streaming→Disconnected
│   │   │   │   ├── mirror_event.dart      # Connect, Disconnect, FrameError
│   │   │   │   └── mirror_state.dart
│   │   │   ├── services/
│   │   │   │   ├── airshow_protocol_service.dart  # TCP framing, handshake
│   │   │   │   └── screen_capture_service.dart    # dispatches to platform channel
│   │   │   └── models/
│   │   │       └── mirror_session.dart
│   │   │
│   │   └── settings/
│   │       ├── bloc/
│   │       │   └── settings_bloc.dart
│   │       └── services/
│   │           └── settings_service.dart  # SharedPreferences
│   │
│   ├── ui/
│   │   ├── screens/
│   │   │   ├── receiver_list_screen.dart  # shows discovered receivers
│   │   │   ├── mirroring_screen.dart      # active session overlay
│   │   │   └── settings_screen.dart
│   │   └── widgets/
│   │       ├── receiver_card.dart
│   │       └── connection_status_bar.dart
│   │
│   └── platform/
│       └── screen_capture_channel.dart    # MethodChannel wrapper
│
├── android/
│   └── app/src/main/kotlin/
│       └── ScreenCapturePlugin.kt         # MediaProjection + MediaCodec (H.264)
│
├── ios/
│   └── Runner/
│       ├── ScreenCapturePlugin.swift      # RPSystemBroadcastPickerView launcher
│       └── AirShowBroadcastExtension/     # Broadcast Upload Extension (separate target)
│           └── SampleHandler.swift        # ReplayKit capture + H.264 encoding
│
├── macos/
│   └── Runner/
│       └── ScreenCapturePlugin.swift      # ScreenCaptureKit (macOS 12.3+) / AVFoundation
│
├── windows/
│   └── runner/
│       └── screen_capture_plugin.cpp      # Windows.Graphics.Capture API
│
└── linux/
    └── runner/
        └── screen_capture_plugin.cc       # PipeWire portal (xdg-desktop-portal)
```

### BLoC State Machine (MirrorBloc)

```
Idle
  ├─[Connect(receiver)]──→ Connecting
  │                            ├─[HandshakeOk]──→ Streaming
  │                            └─[HandshakeFailed/Timeout]──→ Idle (with error)
  │
Streaming
  ├─[Disconnect]────────────────────────────────────→ Idle
  ├─[ReceiverDropped]──→ Idle (with reconnect prompt)
  └─[FrameError]─────→ Streaming (log, continue)
```

### Key Services

**DiscoveryService** wraps `nsd` 4.1.0:
- `startDiscovery()`: queries `_airshow._tcp` records
- Emits stream of `AirshowReceiver` objects
- `nsd` package supports Android/iOS/macOS/Windows (HIGH confidence — verified on pub.dev)
- Linux: `nsd` falls back to socket-based mDNS; test separately

**AirShowProtocolService** (Dart, no native dependency):
- Opens `Socket` via `dart:io`
- Sends/receives frames per the 16-byte header format defined above
- Length-prefix reassembly in a buffer (`Uint8List`) — handles partial TCP reads
- `Stream<Frame>` for incoming frames; `Future<void> sendFrame(Frame)` for outgoing
- On `DISCONNECT` frame received or TCP error: notifies MirrorBloc

**ScreenCaptureService** dispatches via `MethodChannel('com.airshow/screen_capture')`:
- `startCapture({width, height, fps})` → triggers native permission request
- `onFrameAvailable(callback)` → receives `Uint8List` of Annex-B H.264 NALU + PTS
- Platform implementations (one per target):

| Platform | Native API | Codec |
|----------|-----------|-------|
| Android | `MediaProjection` + `MediaCodec` H.264 encoder | H.264 Baseline Level 3.1 |
| iOS | ReplayKit `RPBroadcastUploadExtension` | VideoToolbox H.264 encoder |
| macOS | `ScreenCaptureKit` (macOS 12.3+) or `AVFoundation` | VideoToolbox H.264 encoder |
| Windows | `Windows.Graphics.Capture` + MFT H.264 encoder | H.264 Baseline Level 3.1 |
| Linux | `xdg-desktop-portal` PipeWire + FFmpeg/GStreamer encoding | H.264 via libx264 |

**Confidence note on platform screen capture:**
- Android MediaProjection: HIGH confidence — well-documented, `flutter_media_projection_creator` exists as reference
- iOS ReplayKit: HIGH confidence — `flutter_replay_kit_launcher` exists as reference; requires Broadcast Extension (separate app target)
- macOS ScreenCaptureKit: MEDIUM confidence — requires macOS 12.3+; `screen_capturer` pub.dev package provides reference
- Windows Graphics Capture: MEDIUM confidence — API is stable but Flutter Windows plugin infrastructure for video frames needs careful memory management
- Linux PipeWire: LOW confidence — portal-based capture varies by compositor; requires testing on GNOME/KDE separately; consider making Linux sender a v2 stretch goal

---

## 4. Transport Layer Decision

### Chosen: TCP with 16-byte Length-Prefix Header

**Why not WebRTC:**
- WebRTC requires ICE/STUN negotiation — unnecessary for LAN where both parties' IPs are already known from mDNS discovery
- GStreamer `webrtcbin` already used for Cast; using it again for AirShow would require multiplexing logic in `MediaPipeline` to route frames to the right pipeline
- WebRTC's DTLS-SRTP encryption adds CPU overhead with no security benefit on a local network with a SecurityManager approval dialog

**Why not RTSP:**
- RTSP requires 3 round-trips before first frame (DESCRIBE/SETUP/PLAY)
- No first-class RTSP client library in Flutter's pub.dev ecosystem for 2026 (only server-side or VLC-dependent options)
- RTSP's session model assumes a server that has pre-existing content; sender-driven live capture does not fit the model cleanly

**Why not RTP/UDP:**
- Packet loss on home Wi-Fi causes visible artifacts in screen mirroring; TCP's reliability guarantee avoids needing an application-level retransmit layer
- UDP's out-of-order delivery requires resequencing; adds complexity for identical result on LAN

**TCP framing details:**
- `dart:io` `Socket` class sends raw `Uint8List` — confirmed HIGH confidence (official Dart API docs)
- Partial read handling: accumulate bytes in a `BytesBuilder`, check if header (16 bytes) is available, then check if header+payload is available; only consume a frame when complete
- `QTcpSocket::readyRead` signal on receiver: same accumulation pattern already used in `MiracastHandler` (see `m_rtspBuffer` pattern)

**GStreamer pipeline mode for AirShowHandler:**
- Uses `initAppsrcPipeline()` — identical to AirPlayHandler
- `h264parse` element receives Annex-B byte-stream from appsc; set `stream-format=byte-stream, alignment=nal` caps on the appsrc
- Audio: `audio/mpeg,mpegversion=4,stream-format=raw` caps for AAC-LC; set via `setAudioCaps()` on first audio frame

---

## 5. Monorepo Structure

### Layout

```
MyAirShow/                        # git root
├── CMakeLists.txt                 # existing C++ receiver build
├── CMakePresets.json              # existing platform presets
├── src/                           # existing C++ source
│   ├── protocol/
│   │   ├── AirShowHandler.h       # NEW — add here
│   │   ├── AirShowHandler.cpp     # NEW — add here
│   │   ├── AirPlayHandler.h/cpp   # existing
│   │   └── ...
│   └── discovery/
│       └── DiscoveryManager.cpp   # MODIFIED — add _airshow._tcp advertisement
├── tests/                         # existing C++ tests
│   └── protocol/
│       └── AirShowHandlerTest.cpp # NEW — unit tests for handler
├── sender/                        # NEW — Flutter app
│   ├── pubspec.yaml
│   ├── lib/
│   ├── android/
│   ├── ios/
│   ├── macos/
│   ├── windows/
│   └── linux/
├── docs/
│   └── airshow-protocol-v1.md    # NEW — wire format spec
├── .planning/
│   └── research/
│       └── ARCHITECTURE.md       # this file
└── README.md
```

### Rationale

- **`sender/` at the root:** Keeps Flutter app visible without deep nesting. `cd sender && flutter run` works naturally.
- **Not `apps/sender/` or `packages/`:** There is only one Flutter app; a Melos-style monorepo structure (for multiple packages) is overkill here.
- **Shared nothing:** The Flutter app and C++ receiver share zero source files. They communicate only over the network protocol. No FFI, no shared headers.
- **Separate CI jobs:** GitHub Actions matrix already builds C++ on Linux/macOS/Windows. Add a parallel Flutter job: `flutter build apk`, `flutter build ipa`, `flutter build macos`, `flutter build windows`. They are independent — C++ build failure does not block Flutter CI.
- **`docs/airshow-protocol-v1.md`:** The wire protocol spec lives in `docs/` as a standalone document, not buried in source code. Both the C++ receiver and Flutter sender should reference it; it is the contract between them.

### Build Independence

The Flutter app and C++ receiver do NOT depend on each other's build systems. They connect at runtime over TCP. This means:
- A developer can work on the Flutter sender without building the C++ receiver (connect to a pre-built receiver binary)
- A developer can work on the receiver without a Flutter build (use any TCP client that speaks the AirShow protocol, or write a Python test harness)

---

## New vs Modified Components

### New (create from scratch)

| Component | Location | Type |
|-----------|----------|------|
| `AirShowHandler` | `src/protocol/AirShowHandler.h/.cpp` | C++ class |
| Flutter sender app | `sender/` | Flutter project |
| `DiscoveryService` | `sender/lib/features/discovery/` | Dart service |
| `AirShowProtocolService` | `sender/lib/features/mirror/services/` | Dart service |
| `ScreenCaptureService` | `sender/lib/features/mirror/services/` + platform dirs | Dart + native |
| `MirrorBloc` | `sender/lib/features/mirror/bloc/` | Dart BLoC |
| AirShow protocol spec | `docs/airshow-protocol-v1.md` | Documentation |
| `AirShowHandlerTest` | `tests/protocol/AirShowHandlerTest.cpp` | C++ test |

### Modified (change existing)

| Component | Location | Change |
|-----------|----------|--------|
| `DiscoveryManager` | `src/discovery/DiscoveryManager.cpp` | Add `_airshow._tcp` advertisement on port 7400 |
| `main.cpp` | `src/main.cpp` | Instantiate and register `AirShowHandler` (4 lines, same pattern as other handlers) |
| `CMakeLists.txt` | root | Add `AirShowHandler.cpp` to source list |
| GitHub Actions CI | `.github/workflows/` | Add Flutter build job |

### Unchanged (zero modification required)

| Component | Why unchanged |
|-----------|--------------|
| `ProtocolHandler` interface | AirShowHandler satisfies existing interface without modification |
| `ProtocolManager` | No changes — `addHandler()` accepts any `ProtocolHandler` |
| `MediaPipeline` | `initAppsrcPipeline()` already supports the frame injection pattern |
| `SecurityManager` | AirShowHandler uses `checkConnectionAsync()` — same as CastHandler |
| `ConnectionBridge` | AirShowHandler calls `setConnected("airshow", deviceName)` — same API |
| All existing handlers | AirPlayHandler, CastHandler, DlnaHandler, MiracastHandler — untouched |

---

## Data Flow: AirShow Sender → Receiver

```
[Flutter Sender App]                    [C++ Receiver]
         │                                      │
         │  mDNS browse _airshow._tcp           │
         │─────────────────────────────────────>│ DiscoveryManager advertises _airshow._tcp
         │<── PTR/SRV/A/TXT responses ──────────│
         │                                      │
         │  TCP connect → port 7400             │
         │─────────────────────────────────────>│ AirShowHandler::onNewConnection()
         │                                      │   SecurityManager::checkConnectionAsync()
         │                                      │   → ConnectionBridge::showApprovalRequest()
         │                                      │   ← user approves in QML
         │  HANDSHAKE_REQUEST (JSON)            │
         │─────────────────────────────────────>│ AirShowHandler::processFrame(0x01)
         │                                      │   parse video/audio codec params
         │                                      │   MediaPipeline::initAppsrcPipeline()
         │                                      │   setAudioCaps("audio/mpeg,mpegversion=4...")
         │<── HANDSHAKE_RESPONSE (accepted) ────│
         │                                      │
         │  ScreenCaptureService.startCapture() │
         │  [native: MediaProjection/ReplayKit] │
         │                                      │
         │  VIDEO_NALU (keyframe, pts=0)        │
         │─────────────────────────────────────>│ AirShowHandler::processFrame(0x03)
         │                                      │   GstBuffer b = gst_buffer_new()
         │                                      │   GST_BUFFER_PTS(b) = ptsNs
         │                                      │   gst_app_src_push_buffer(videoAppsrc, b)
         │  AUDIO_FRAME (pts=N)                 │
         │─────────────────────────────────────>│ AirShowHandler::processFrame(0x04)
         │                                      │   gst_app_src_push_buffer(audioAppsrc, b)
         │                                      │
         │  ... stream of NALU + AUDIO_FRAME ...│ GStreamer decodes → QML display
         │                                      │
         │  DISCONNECT (0x06)                   │
         │─────────────────────────────────────>│ AirShowHandler::onDisconnected()
         │                                      │   ConnectionBridge::setConnected(false)
         │                                      │   pipeline stays initialized (PAUSED)
```

---

## Build Order for v2.0 Milestone

This order minimizes blocked work and enables independent testing at each step.

```
Step 1: AirShow protocol spec (docs/airshow-protocol-v1.md)
  — No code. Defines the wire format that both sides implement.
  — Unblocks Steps 2 and 3 in parallel.

Step 2: AirShowHandler (C++ receiver side)
  — Implements ProtocolHandler interface
  — QTcpServer on port 7400
  — Handshake parsing and response
  — NALU + audio frame injection via existing appsrc
  — Depends on: protocol spec (Step 1), existing MediaPipeline, ProtocolHandler interface
  — Can be tested with a Python/Netcat TCP client before any Flutter code exists

Step 3: DiscoveryManager modification (C++ receiver)
  — Add _airshow._tcp advertisement
  — Depends on: existing ServiceAdvertiser interface
  — Can be tested: run receiver, check with dns-sd -B _airshow._tcp

Step 4: Flutter sender — Discovery feature
  — DiscoveryService + nsd 4.x
  — ReceiverListScreen
  — Depends on: Step 3 (receiver must advertise)
  — No screen capture needed yet — just list receivers

Step 5: Flutter sender — Protocol client (no video)
  — AirShowProtocolService (TCP + framing)
  — MirrorBloc (Connect/Disconnect state machine)
  — Send synthetic NALU from a test video file
  — Depends on: Steps 2, 4

Step 6: Android screen capture platform channel
  — ScreenCapturePlugin.kt (MediaProjection + MediaCodec)
  — Wire into ScreenCaptureService
  — Depends on: Step 5

Step 7: iOS screen capture platform channel
  — Broadcast Extension + RPBroadcastUploadExtension
  — Depends on: Step 5

Step 8: macOS screen capture platform channel
  — ScreenCaptureKit
  — Depends on: Step 5

Step 9: Windows screen capture platform channel
  — Windows.Graphics.Capture + MFT encoder
  — Depends on: Step 5

Step 10: End-to-end integration + UI polish
  — MirroringScreen (in-session overlay, disconnect button)
  — Error handling (rejection, timeout, DRM)
  — ConnectionBridge QML updates on receiver for "airshow" protocol

[Linux sender: stretch goal, after Step 10]
```

**Parallelization opportunities:**
- Steps 2 and 3 can be done simultaneously (different files)
- Steps 6, 7, 8, 9 can be done by different people simultaneously (different platform dirs)
- Flutter UI (Steps 4–5) can proceed while platform channels are being built (use a mock ScreenCaptureService)

---

## Anti-Patterns to Avoid

### Anti-Pattern 1: Implementing the Sender as a Full AirPlay Sender

**What:** Build the Flutter sender to speak the AirPlay protocol (RAOP + RTSP + AES), targeting the existing AirPlayHandler.

**Why wrong:** AirPlay's sender side is undocumented, reverse-engineered, and requires FairPlay SRP authentication that no Flutter library implements. There is no maintained Dart/Flutter AirPlay sender library. This would take months of protocol reverse-engineering work.

**Do instead:** Custom AirShow protocol on port 7400 with a new AirShowHandler. Simple to implement, no reverse engineering required.

### Anti-Pattern 2: Sharing Code Between Flutter and C++ via FFI

**What:** Create a shared C++ library of utility functions (framing, protocol parsing) that both the receiver and Flutter app link against via FFI.

**Why wrong:** The protocol is simple enough (16-byte header + JSON handshake) that reimplementing it in Dart takes 100 lines. FFI across a monorepo adds build complexity, requires matching calling conventions, and creates a coupling that makes both sides harder to change independently.

**Do instead:** Both sides implement the protocol from the spec document. Keep them decoupled — they communicate only over TCP.

### Anti-Pattern 3: Custom Pipeline Mode in MediaPipeline for AirShow

**What:** Add `initAirShowPipeline()` to `MediaPipeline` specifically for the AirShow protocol.

**Why wrong:** `initAppsrcPipeline()` already does exactly what AirShowHandler needs: it provides `videoAppsrc()` and `audioAppsrc()` endpoints for injecting encoded frames. AirPlayHandler already uses this exact mode. Adding a new pipeline mode would duplicate existing code.

**Do instead:** `AirShowHandler::setMediaPipeline()` stores the pipeline pointer and calls `initAppsrcPipeline()` during handshake, then injects frames via the existing appsrc accessors. Zero changes to `MediaPipeline`.

### Anti-Pattern 4: iOS ReplayKit Without Broadcast Extension

**What:** Capture screen on iOS using `RPScreenRecorder` directly from the main app process.

**Why wrong:** `RPScreenRecorder` captures only the host app's content, not the full system screen. Full system screen capture on iOS requires a Broadcast Upload Extension — a separate app target that Apple sandboxes to run during a system broadcast session. This is a non-negotiable iOS architectural requirement.

**Do instead:** Create an `AirShowBroadcastExtension` iOS target. The extension captures the screen via ReplayKit; it communicates frames to the main app via an App Group shared container or CFNotificationCenter (the pattern used by commercial screen sharing SDKs).

### Anti-Pattern 5: UDP for Video Transport

**What:** Use UDP for video frame delivery to reduce latency.

**Why wrong:** On home Wi-Fi, 1–5% packet loss is common. For screen mirroring, a lost keyframe makes the video undecodable until the next keyframe. Without application-level retransmit (which re-implements TCP's reliability), the result is frequent freezes. The latency benefit of UDP over TCP on a LAN is measured in single-digit milliseconds — imperceptible for screen mirroring.

**Do instead:** TCP. Set `TCP_NODELAY` on the socket to disable Nagle buffering; this recovers any latency overhead from TCP's reliability guarantee.

---

## Integration Points

### Receiver Side (C++) — New Integration Points

| Boundary | Communication | New or Modified |
|----------|---------------|-----------------|
| `DiscoveryManager` → `ServiceAdvertiser` | `advertise("_airshow._tcp", name, 7400, txt)` | Modified (add one call) |
| `main.cpp` → `ProtocolManager` | `addHandler(make_unique<AirShowHandler>(...))` | Modified (add ~4 lines) |
| `AirShowHandler` → `MediaPipeline` | `initAppsrcPipeline()`, `videoAppsrc()`, `audioAppsrc()`, `setAudioCaps()` | New (existing API) |
| `AirShowHandler` → `SecurityManager` | `checkConnectionAsync()` | New (existing API) |
| `AirShowHandler` → `ConnectionBridge` | `setConnected(true, deviceName, "airshow")` | New (existing API) |

### Sender Side (Flutter) — New Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| `DiscoveryService` → `nsd` package | `startDiscovery(serviceType: "_airshow._tcp")` | 3rd-party package, pub.dev |
| `AirShowProtocolService` → `dart:io` `Socket` | TCP framing over port 7400 | stdlib, no extra package |
| `ScreenCaptureService` → platform channel | `MethodChannel('com.airshow/screen_capture')` | Native per-platform |
| `MirrorBloc` → `AirShowProtocolService` | Service call, `await connect(receiver)` | BLoC service pattern |
| `MirrorBloc` → `ScreenCaptureService` | Service call, `startCapture()`, frame stream | BLoC service pattern |

---

## Sources

**Existing receiver architecture (HIGH confidence — source code verified):**
- `/home/sanya/Desktop/MyAirShow/src/protocol/ProtocolHandler.h` — interface
- `/home/sanya/Desktop/MyAirShow/src/protocol/AirPlayHandler.h` — reference implementation pattern
- `/home/sanya/Desktop/MyAirShow/src/protocol/CastHandler.h` — async approval pattern
- `/home/sanya/Desktop/MyAirShow/src/protocol/MiracastHandler.h` — QTcpServer pattern (same as AirShowHandler will use)
- `/home/sanya/Desktop/MyAirShow/src/pipeline/MediaPipeline.h` — initAppsrcPipeline, videoAppsrc, audioAppsrc
- `/home/sanya/Desktop/MyAirShow/src/main.cpp` — handler registration pattern

**Flutter ecosystem (MEDIUM confidence — verified via pub.dev):**
- [nsd 4.1.0 — pub.dev](https://pub.dev/packages/nsd) — Android/iOS/macOS/Windows, discovery + registration, v4.1.0
- [flutter_media_projection_creator — GitHub](https://github.com/patrick-fu/flutter_media_projection_creator) — Android MediaProjection reference
- [flutter_replay_kit_launcher — GitHub](https://github.com/patrick-fu/flutter_replay_kit_launcher) — iOS ReplayKit reference
- [flutter-webrtc screen capture docs](https://github.com/flutter-webrtc/flutter-webrtc) — confirmed screen capture support on Android/iOS

**Protocol design (HIGH confidence — primary sources):**
- [AirPlay protocol TCP + 128-byte header — openairplay spec](https://openairplay.github.io/airplay-spec/screen_mirroring/index.html) — reference for what AirPlay does (we intentionally differ)
- [H.264 Annex-B vs AVC NAL format — GStreamer h264parse docs](https://gstreamer.freedesktop.org/documentation/codecparsers/gsth264parser.html) — Annex-B byte-stream is the right format for appsrc
- [Length-prefix framing — Eli Bendersky](https://eli.thegreenplace.net/2011/08/02/length-prefix-framing-for-protocol-buffers) — canonical framing pattern
- [dart:io Socket API](https://api.flutter.dev/flutter/dart-io/Socket-class.html) — raw byte TCP in Dart confirmed HIGH confidence

---

*Architecture research for: AirShow v2.0 — Companion Sender App*
*Researched: 2026-03-30*
