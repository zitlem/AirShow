# Stack Research

**Domain:** AirShow v2.0 — Flutter Companion Sender App (cross-platform screen mirror sender)
**Researched:** 2026-03-30
**Confidence:** MEDIUM — Flutter stack is HIGH confidence; platform-specific screen capture on desktop Linux is the weak link; receiver-side protocol additions are straightforward given existing GStreamer/appsrc pattern.

---

## Scope

This document covers NEW stack additions for the v2.0 companion sender app only. The existing receiver stack (C++17 + Qt 6.8 LTS + GStreamer 1.26 + OpenSSL 3.x + CMake + vcpkg) is validated and unchanged.

---

## Recommended Stack

### Sender App — Core Framework

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| **Flutter** | 3.41.5 (stable) | Cross-platform sender app UI + app logic | The only framework that targets Android + iOS + Windows + macOS + Linux from one Dart codebase with real native platform channel and dart:ffi access. React Native and Kotlin Multiplatform both have weaker Linux desktop support. Flutter 3.41 is the Feb 2026 stable release. |
| **Dart** | 3.x (bundled with Flutter 3.41) | Application language | Ships with Flutter. dart:io provides raw TCP sockets for the custom AirShow protocol transport. |

### Sender App — mDNS Discovery

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| **multicast_dns** | 0.3.3 | Discover `_airshow._tcp` services on LAN | Use this. It is the flutter.dev-published, BSD-3-licensed package (3.2M+ downloads). Supports Android, iOS, Linux, macOS, Windows — all five sender targets. It does pure-Dart mDNS queries over multicast UDP (RFC 6762). |
| **nsd** | 4.1.0 | mDNS service discovery AND registration | Use only if registration is also needed from the sender side (unlikely — sender discovers, receiver advertises). Does NOT support Linux. Avoid if Linux sender is a target. |

**Decision: use `multicast_dns` 0.3.3.** It covers all five platforms including Linux. `nsd` is excluded because it drops Linux.

### Sender App — Screen Capture

Screen capture is the most platform-fragmented piece. There is no single Flutter plugin that provides live frame callbacks on all five platforms. The correct architecture is: platform-native capture via a thin Flutter plugin + dart:ffi for high-throughput pixel data transfer.

| Platform | API | Flutter Integration | Confidence |
|----------|-----|---------------------|------------|
| **Android** | MediaProjection + foreground service (`FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION`) | `media_projection_creator` plugin OR custom MethodChannel. Android 14+ requires calling `createScreenCaptureIntent()` BEFORE starting the foreground service — many existing plugins handle this incorrectly for API 34+. | HIGH |
| **iOS** | ReplayKit 2 Broadcast Upload Extension | Custom app extension target + `RPSystemBroadcastPickerView` launcher. The broadcast extension is a separate process with a 50 MB memory cap. It cannot capture AVPlayer/DRM audio. Raw `CMSampleBuffer` frames are delivered as uncompressed YUV. | HIGH |
| **Windows** | DXGI Desktop Duplication API (`IDXGIOutputDuplication`) + Media Foundation H.264 MFT encoder | C++ native plugin via dart:ffi. DXGI Desktop Duplication is the standard since Windows 8, gives GPU-accessible `ID3D11Texture2D` frames. Media Foundation `H264EncoderMFT` hardware-encodes them. | HIGH |
| **macOS** | `ScreenCaptureKit` (macOS 12.3+) + VideoToolbox `VTCompressionSession` | C++ or ObjC native plugin via dart:ffi or MethodChannel. ScreenCaptureKit replaced deprecated CGDisplayStream in 2022 and is the current Apple-recommended API. Requires `NSScreenCaptureUsageDescription` in Info.plist. | HIGH |
| **Linux** | X11: `XShm`/`XComposite`; Wayland: `xdg-desktop-portal` + PipeWire | C native plugin. X11 is straightforward (XShm shared memory). Wayland requires the portal; PipeWire delivers a video stream that must be consumed per-frame. VAAPI (`VA-API`) for H.264 hardware encode on Intel/AMD. | MEDIUM — Wayland portal UX is disruptive; each capture session requires user consent. |

**No existing Flutter plugin does continuous live-frame delivery for the desktop platforms (Windows/macOS/Linux).** The `screen_capturer` plugin (0.2.3) only takes static screenshots. `flutter_webrtc` (1.4.1) handles `getDisplayMedia` on all five platforms and internally wraps these same OS APIs, but it encodes to WebRTC's internal codec pipeline — frames are not accessible as raw H.264 NAL units for reuse. See "Transport" section for why this matters.

### Sender App — H.264 Encoding

| Platform | API | Notes |
|----------|-----|-------|
| **Android** | `MediaCodec` (`android.media.MediaCodec`) | Part of the Android multimedia framework since API 16. Request `video/avc` (H.264) encoder, configure with target bitrate and keyframe interval. Runs in hardware on virtually all Android devices since 2014. |
| **iOS** | `VideoToolbox` (`VTCompressionSession`) | Apple's hardware H.264 encoder. Called from the ReplayKit extension's `processSampleBuffer` callback. Configure `kVTProfileLevel_H264_High_AutoLevel` and `kVTEncodeFrameOptionKey_ForceKeyFrame` for IDR on demand. |
| **Windows** | Media Foundation H.264 MFT (`CLSID_MSH264EncoderMFT`) | Hardware encoder on all modern Windows systems. DXGI texture flows directly into the MFT input via `IMFDXGIBuffer` — no CPU copy needed. |
| **macOS** | `VideoToolbox` (`VTCompressionSession`) | Same as iOS. Apple Silicon has dedicated H.264 encoder hardware. |
| **Linux** | GStreamer `vaapih264enc` or `x264enc` (software fallback) | For Linux sender, the simplest approach is to run a local GStreamer pipeline (`ximagesrc ! videoconvert ! vaapih264enc` or `x264enc`) and funnel the encoded NAL units over the custom AirShow protocol. Re-uses the same GStreamer already present on the system. Avoids needing a separate encoding library. |

**Encoding abstraction strategy:** Write a thin Dart abstract class `H264Encoder` with platform implementations. On Android/iOS/Windows/macOS use native platform channel or dart:ffi to the OS encoder. On Linux desktop use a GStreamer subprocess or FFI to the existing GStreamer pipeline since GStreamer is already a project dependency.

### Sender App — Transport Protocol (Custom AirShow Protocol)

Do NOT implement WebRTC, RTP/RTSP, or RTMP for the sender-to-AirShow-receiver path. Those are correct for sending to third-party receivers. For sender-to-AirShow-receiver, use a purpose-built binary TCP protocol.

**Rationale:** WebRTC adds libwebrtc dependency (~20 MB), signaling infrastructure, ICE/STUN complexity, and codec negotiation overhead. RTSP requires implementing an RTSP state machine. The AirShow receiver is controlled software — design the wire format to be simple.

**AirShow Wire Protocol (recommended design):**

```
Frame header (8 bytes):
  [0..3] uint32 BE  — payload length in bytes
  [4]    uint8      — frame type: 0x01=H264_NAL, 0x02=AUDIO_AAC, 0x03=KEEPALIVE
  [5]    uint8      — flags: bit0=keyframe, bit1=SPS/PPS prefix present
  [6..7] uint16 BE  — sequence number (for receiver-side drop detection)

Followed by `payload length` bytes of:
  - H.264 NAL units in Annex B format (start codes) OR AVCC length-prefix format (negotiate at handshake)
  - AAC-LC raw frames for audio
```

Handshake on TCP connect: sender sends a JSON greeting over the same connection before switching to binary frames. Receiver responds with session parameters (resolution caps, codec profile).

This is simpler than AirPlay's 128-byte binary header and avoids the complexity of RTP sequence/timestamp for a local-network-only scenario with ~1 ms RTT.

**Dart transport implementation:** `dart:io` `Socket` class for TCP. Use `Uint8List` + `ByteData` for binary framing. No third-party transport library needed.

| Option | Verdict | Why |
|--------|---------|-----|
| Custom TCP binary framing | **USE THIS** | Simple, no dependencies, designed for the AirShow receiver, ~1 ms LAN RTT |
| WebRTC (flutter_webrtc) | Avoid for AirShow protocol | ICE/STUN overhead, codec locked to WebRTC's negotiation, 20 MB lib | 
| RTSP over TCP | Avoid | RTSP state machine complexity not justified for controlled sender+receiver pair |
| RTP/UDP | Avoid as primary | Packet loss on busy Wi-Fi causes visible artifacts; TCP with keepalive sufficient for LAN |

### Receiver Side — AirShowHandler

The receiver needs a new protocol handler alongside the existing `AirPlayHandler`, `CastHandler`, `DlnaHandler`, `MiracastHandler`. The integration pattern is identical to AirPlay/Cast:

| Component | What to Add |
|-----------|-------------|
| **mDNS advertisement** | Add `_airshow._tcp` service type to the existing Avahi/mDNSResponder advertisement. Port TBD (suggest 7777 to avoid collisions with 5000/AirPlay, 8008/Cast, 7250/Miracast). |
| **TCP accept loop** | `QTcpServer` listening on the AirShow port. One `QTcpSocket` per sender connection. |
| **Frame parser** | Read 8-byte header, accumulate payload, dispatch to GStreamer `appsrc`. Identical pattern to the existing AirPlay appsrc injection. |
| **GStreamer pipeline** | `appsrc name=airshow_src ! h264parse ! avdec_h264 ! qml6glsink` — same pipeline skeleton as AirPlay. Hardware decode via `vaapidecodebin` or `d3d11h264dec` same as existing. |
| **Session management** | AirShow sender will send a JSON handshake on connect. Parse with `QJsonDocument`. |

No new C++ libraries are needed on the receiver side. All required pieces (Qt TCP, GStreamer appsrc, h264parse, qml6glsink) are already in the validated receiver stack.

### Sender App — Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| **multicast_dns** | 0.3.3 | mDNS browse for `_airshow._tcp` | Always — receiver discovery |
| **flutter_webrtc** | 1.4.1 | getDisplayMedia for screen capture on all platforms IF raw pixel access suffices for WebRTC path | Only if abandoning custom protocol for a WebRTC-based streaming path (see "Alternatives") |
| **pigeon** | latest (pub.dev) | Type-safe platform channel code generation | Use for Android (Kotlin) and iOS (Swift) platform channel wrappers for MediaProjection + VideoToolbox calls |
| **ffigen** | 15.x (pub.dev) | Generate dart:ffi bindings from C/C++ headers | Use for Windows/macOS/Linux native encode plugins |

### Development Tools — Sender App Additions

| Tool | Purpose | Notes |
|------|---------|-------|
| **Flutter 3.41 SDK** | Build sender app | `flutter create --platforms=android,ios,windows,macos,linux` |
| **Android Studio / Gradle** | Android native plugin development | Required for MediaProjection foreground service implementation |
| **Xcode 16+** | iOS/macOS native plugin + Broadcast Extension target | ReplayKit Broadcast Extension requires a separate app extension target in the same Xcode project |
| **flutter pub run pigeon** | Generate platform channel bindings | Keeps Kotlin/Swift/C++ native interface type-safe |
| **flutter pub run ffigen** | Generate dart:ffi bindings for Windows/Linux/macOS native code | Config via `ffigen.yaml` |
| **ADB + Android 14 test device** | Validate MediaProjection foreground service API 34+ behavior | The API 34 foreground service change is the most common source of crash in this domain |

---

## Integration Points

### How sender connects to receiver

```
Sender (Flutter app)
  1. multicast_dns: browse _airshow._tcp
  2. User selects receiver from list
  3. dart:io Socket.connect(host, 7777)
  4. Send JSON handshake: { "version": "1", "codec": "h264", "profile": "high", "level": "4.1" }
  5. Receive JSON response: { "max_width": 1920, "max_height": 1080, "fps": 30 }
  6. Start capture loop → encode → send binary frames

Receiver (Qt/C++ AirShowHandler)
  1. QTcpServer::listen(7777)
  2. On newConnection: accept QTcpSocket
  3. Read JSON handshake
  4. Create GStreamer pipeline with appsrc
  5. On readyRead: parse frame header → push to appsrc
  6. qml6glsink renders into existing window
```

### Platform channel architecture for screen capture

```
Dart (AirShowSender)
  └─ ScreenCaptureService (abstract)
       ├─ AndroidScreenCapture  (MethodChannel → Kotlin → MediaProjection → MediaCodec → H.264 bytes)
       ├─ IosScreenCapture      (MethodChannel → Swift → ReplayKit Extension → VideoToolbox → H.264 bytes)
       ├─ WindowsScreenCapture  (dart:ffi → C++ DLL → DXGI Duplication → MF H264 MFT → H.264 bytes)
       ├─ MacosScreenCapture    (dart:ffi → ObjC dylib → ScreenCaptureKit → VideoToolbox → H.264 bytes)
       └─ LinuxScreenCapture    (dart:ffi → C SO → XShm/PipeWire → GStreamer x264enc → H.264 bytes)
```

---

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| Custom TCP binary protocol | WebRTC via flutter_webrtc | If the sender also needs to stream to non-AirShow receivers (Chromecast, etc.). WebRTC is universally understood but adds 20+ MB and ICE complexity. |
| Custom TCP binary protocol | RTSP | If a third-party RTSP player (VLC) needs to receive the stream directly. Overkill for a controlled sender+receiver pair. |
| Custom TCP binary protocol | RTP/UDP | If you need sub-50 ms latency and can tolerate frame drops. For local LAN, TCP with Nagle disabled achieves <5 ms with no drop risk. |
| multicast_dns (flutter.dev) | nsd 4.1.0 | If you drop Linux desktop as a sender target. `nsd` uses native OS APIs (Bonjour, Avahi, NsdManager) which may be more robust but it explicitly excludes Linux. |
| Platform-native H.264 encoders | FFmpeg via ffmpeg_kit_flutter_new | If uniform cross-platform encoding API is more important than zero binary bloat. `ffmpeg_kit_flutter_new` 4.1.0 supports h264_mediacodec (Android) and h264_videotoolbox (iOS/macOS) but bundles a large FFmpeg binary (~30-50 MB) and adds GPL/LGPL licensing surface. |
| ScreenCaptureKit (macOS 12.3+) | AVFoundation CGDisplayStream | CGDisplayStream is deprecated since macOS 14.0 (Sonoma). Do not use. |
| GStreamer pipeline (Linux) | V4L2 loopback + custom encode | v4l2loopback works but requires kernel module installation — bad UX for end users. GStreamer `ximagesrc ! x264enc` runs in userspace with no extra kernel dependencies. |

---

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| **flutter_screen_recording** (file-based) | Records to MP4 file only — no live frame callback. Cannot stream to a TCP socket in real time. | Platform-native approach per OS (see Screen Capture table) |
| **screen_capturer** (desktop screenshot) | Takes static screenshots, not continuous frame capture. No video pipeline. | DXGI/ScreenCaptureKit/XShm native plugin |
| **nsd** for Linux sender | Does not support Linux platform. Will fail at build time on `flutter run -d linux`. | `multicast_dns` 0.3.3 (flutter.dev, all platforms) |
| **CGDisplayStream (macOS)** | Deprecated since macOS 14.0 Sonoma. Apple will remove in a future OS release. | `ScreenCaptureKit` (macOS 12.3+) |
| **MiracleCast / Wi-Fi Direct** | Already excluded from receiver for the same reason — stalled project, no stable P2P API. | Not applicable for sender either. |
| **Platinum UPnP SDK** | Already excluded from receiver. Dead project. | Not needed in sender either. |
| **React Native / KMP for sender** | React Native has no stable Linux desktop target. KMP mobile targets are strong but Linux desktop Flutter support is the only cross-platform option covering all five targets. | Flutter 3.41 |
| **WebRTC as primary transport to AirShow receiver** | Introduces ICE/STUN/DTLS-SRTP stack purely for local LAN streaming where TCP is sufficient. Locks codec to WebRTC's internal VP8/VP9/H264 negotiation and loses direct NAL access needed for appsrc injection. | Custom AirShow TCP binary protocol |
| **flutter_quick_video_encoder** | Outputs MP4 files, not streaming NAL units. Not designed for live streaming. Last published 18 months ago (before 2026). | Direct MediaCodec/VideoToolbox platform channel calls |

---

## Stack Patterns by Platform

**Android sender:**
- Custom MethodChannel → Kotlin → `MediaProjectionManager` + `MediaRecorder`/`MediaCodec`
- Target SDK 34+ requires `FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION` permission in manifest AND calling `createScreenCaptureIntent()` before foreground service start
- Audio: `AudioRecord` with `REMOTE_SUBMIX` source captures system audio alongside screen on Android 10+

**iOS sender:**
- Broadcast Upload Extension (separate app target) handles ReplayKit delivery
- Extension has 50 MB memory limit — keep encoded H.264 buffer pool small
- `RPSystemBroadcastPickerView` triggers system picker UI; sender app cannot start capture programmatically without user interaction
- No DRM audio: AVPlayer/Safari playback audio is blocked from capture

**Windows sender:**
- C++ DLL plugin: DXGI Desktop Duplication → `ID3D11Texture2D` → GPU-copy to `NV12` staging surface → Media Foundation H264 MFT encoder
- No cursor capture permission required; DXGI Duplication is available to any foreground-capable app
- Bundle the C++ DLL as a Flutter plugin native asset (Flutter 3.x supports `--build-mode=release` with native plugins)

**macOS sender:**
- ObjC/Swift plugin: `SCShareableContent` → `SCStream` (ScreenCaptureKit) → `VTCompressionSession` H.264 encoder
- Requires `NSScreenCaptureUsageDescription` in Info.plist and user approval
- App must be notarized for distribution

**Linux sender:**
- C plugin: detect display server at runtime (check `$WAYLAND_DISPLAY` vs `$DISPLAY`)
- X11 path: `XOpenDisplay` → `XShmCreateImage` capture loop → GStreamer `x264enc` for software H.264 encode → VAAPI `vaapih264enc` if Intel/AMD GPU present
- Wayland path: `xdg-desktop-portal` ScreenCast D-Bus API → PipeWire consumer → encode via GStreamer
- Wayland requires user consent dialog per session — no silent capture

---

## Version Compatibility

| Package | Compatible With | Notes |
|---------|-----------------|-------|
| Flutter 3.41.5 | Dart 3.x | Bundled — no separate Dart install needed |
| multicast_dns 0.3.3 | Flutter 3.x, Dart 3.x | flutter.dev package, BSD-3, no native dependencies |
| flutter_webrtc 1.4.1 | Flutter 3.x, Android API 21+, iOS 12+, macOS 10.15+, Windows 10+, Ubuntu 18.04+ | If used. Note: iOS build requires extra Xcode setting post-m104 WebRTC XCF change |
| pigeon (latest) | Flutter 3.x | Use latest; pigeon API changes frequently — pin the version in pubspec.yaml |
| nsd 4.1.0 | Flutter 3.x, Android/iOS/macOS/Windows ONLY | No Linux support — exclude from Linux build |
| GStreamer 1.26.x (receiver) | Qt 6.8.x + qml6glsink | Unchanged from existing receiver stack; AirShowHandler reuses same pipeline pattern |
| Qt 6.8.x QTcpServer | All platforms | Ships with Qt — no new dependency for receiver AirShowHandler |

---

## Sources

- [Flutter SDK Archive — flutter.dev](https://docs.flutter.dev/release/archive) — Flutter 3.41.5 confirmed as Feb 2026 stable HIGH confidence
- [multicast_dns pub.dev](https://pub.dev/packages/multicast_dns) — 0.3.3, all five platforms, flutter.dev published HIGH confidence
- [nsd pub.dev](https://pub.dev/packages/nsd) — 4.1.0, no Linux support confirmed HIGH confidence
- [flutter_webrtc pub.dev](https://pub.dev/packages/flutter_webrtc) — 1.4.1, getDisplayMedia on all five platforms HIGH confidence
- [flutter_screen_recording pub.dev](https://pub.dev/packages/flutter_screen_recording) — 2.0.25, file-only output, Android/iOS only HIGH confidence
- [screen_capturer pub.dev](https://pub.dev/packages/screen_capturer) — 0.2.3, screenshot-only, no streaming HIGH confidence
- [flutter_quick_video_encoder pub.dev](https://pub.dev/packages/flutter_quick_video_encoder) — 1.7.2, uses AVFoundation/MediaCodec, file output only HIGH confidence
- [Android MediaProjection — Android Developers](https://developer.android.com/media/grow/media-projection) — API 34 foreground service type requirement HIGH confidence
- [ReplayKit — Apple Developer Documentation](https://developer.apple.com/documentation/ReplayKit) — Broadcast Extension 50 MB memory cap, 15 fps cap, DRM audio exclusion HIGH confidence
- [DXGI Desktop Duplication + Media Foundation H.264 MFT (alax.info)](https://alax.info/blog/1716) — Windows screen + hardware encode pattern confirmed MEDIUM confidence
- [ScreenCaptureKit — Apple Developer Documentation (via WebSearch)] — macOS 12.3+ API, CGDisplayStream deprecated macOS 14 MEDIUM confidence
- [Linux XShm + ximagesrc — GStreamer docs](https://gstreamer.freedesktop.org/documentation/ximagesrc/index.html) — X11 capture via XShm confirmed HIGH confidence
- [xdg-desktop-portal PipeWire — ArchWiki Screen Capture](https://wiki.archlinux.org/title/Screen_capture) — Wayland portal + PipeWire pattern confirmed MEDIUM confidence
- [Flutter Platform Channels — flutter.dev docs](https://docs.flutter.dev/platform-integration/platform-channels) — MethodChannel + dart:ffi approaches HIGH confidence
- [ffigen — flutter.dev docs](https://docs.flutter.dev/platform-integration/bind-native-code) — dart:ffi build hooks recommended since Flutter 3.38 HIGH confidence
- [H.264 Annex B vs AVCC framing — Membrane.stream](https://membrane.stream/learn/h264/3) — NAL unit framing for TCP transport HIGH confidence
- [ffmpeg_kit_flutter_new GitHub](https://github.com/SerenityS/ffmpeg_kit_flutter_new) — v4.1.0 with FFmpeg 8.0.0, h264_videotoolbox + h264_mediacodec support MEDIUM confidence

---

*Stack research for: AirShow v2.0 Flutter companion sender app*
*Researched: 2026-03-30*
