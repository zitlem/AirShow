# Project Research Summary

**Project:** AirShow v2.0 — Flutter Companion Sender App
**Domain:** Cross-platform screen mirroring sender (Flutter) + receiver protocol extension (C++/Qt)
**Researched:** 2026-03-30
**Confidence:** MEDIUM — receiver architecture HIGH confidence (existing codebase); Flutter sender MEDIUM confidence; Linux sender and iOS IPC LOW in specific sub-areas

---

## Executive Summary

AirShow v2.0 extends the existing C++/Qt/GStreamer receiver with a Flutter-based companion sender app that captures and streams device screens to any AirShow receiver on the local network. The receiver already handles AirPlay, Cast, DLNA, and Miracast from third-party senders; the new work is a custom `_airshow._tcp` protocol that gives full control over quality, latency, and features without the legal constraints of Apple or Google protocols. The sender targets all five platforms (Android, iOS, macOS, Windows, Linux) from a single Dart codebase, with per-platform native plugins for screen capture since no cross-platform Flutter plugin handles live frame delivery on desktop.

The recommended approach is layered: receiver changes first (AirShowHandler + mDNS advertisement), then the mobile sender (Android MediaProjection and iOS ReplayKit — the two highest-traffic platforms), then desktop sender (macOS via ScreenCaptureKit, Windows via DXGI, Linux deferred to v2 due to PipeWire variability). The wire protocol is a purpose-built binary TCP framing (16-byte header + length-prefixed payload) implemented in Dart's `dart:io` and Qt's `QTcpSocket` — no third-party transport library is needed. This is simpler and more controllable than WebRTC or RTSP for a controlled sender+receiver pair on a LAN.

The main risks are iOS-specific: the ReplayKit Broadcast Upload Extension runs in a separate process with a hard 50 MB memory cap and cannot directly access the main app's network socket — the IPC architecture between extension and app must be designed correctly before any networking code is written. A second significant risk is the Android API 34 MediaProjection consent model change that breaks any implementation that caches or reuses the `Intent` across sessions. Both risks are well-documented and avoidable with the correct architecture from the start.

---

## Key Findings

### Recommended Stack

The existing receiver stack (C++17 + Qt 6.8 LTS + GStreamer 1.26.x + OpenSSL 3.x + CMake + vcpkg) is unchanged and validated. The v2.0 sender app adds Flutter 3.41.5 (Feb 2026 stable) as the cross-platform framework. Flutter is the only option that targets all five sender platforms (Android, iOS, macOS, Windows, Linux) from one codebase with real `dart:ffi` and MethodChannel access to native screen capture APIs. React Native lacks stable Linux desktop support; Kotlin Multiplatform covers mobile well but not Linux desktop.

The receiver side requires only one new component: `AirShowHandler` implementing the existing `ProtocolHandler` interface with a `QTcpServer` on port 7400. This uses the same `appsrc` GStreamer injection pattern as `AirPlayHandler` — no new C++ libraries are needed.

**Core technologies:**
- **Flutter 3.41.5**: Sender app framework — only cross-platform option covering all five sender targets
- **Dart `dart:io` Socket**: TCP transport for custom AirShow protocol — no third-party transport library needed
- **`multicast_dns` 0.3.3** (flutter.dev): mDNS discovery of `_airshow._tcp` — covers all five platforms including Linux (unlike `nsd` which drops Linux)
- **Platform-native H.264 encoders**: Android MediaCodec, iOS/macOS VideoToolbox, Windows Media Foundation MFT, Linux GStreamer x264enc/vaapih264enc — no uniform cross-platform encoding library; each platform needs its own native plugin
- **Qt `QTcpServer` / `QTcpSocket`**: Receiver-side AirShowHandler transport — already in the stack, no new dependency
- **`pigeon`** (pub.dev): Type-safe platform channel code generation for Android (Kotlin) and iOS (Swift) native interfaces
- **`ffigen`** (pub.dev): dart:ffi binding generation for Windows/macOS/Linux native encode plugins

### Expected Features

**Must have (table stakes — MVP launch blockers):**
- mDNS auto-discovery of `_airshow._tcp` receivers via `multicast_dns` package
- Device picker list UI (scrollable list, refresh, empty state)
- QR code fallback for corporate/school networks where mDNS multicast is blocked — industry-standard from day one, not a later addition
- Screen capture + H.264 encode + stream: Android (MediaProjection), iOS (ReplayKit extension), macOS (ScreenCaptureKit)
- Connection status indicator: idle / connecting / mirroring / error
- Stop mirroring: in-app button + Android foreground notification with "Stop" action
- Auto-reconnect with backoff (1s, 2s, 5s) up to ~30s

**Should have (competitive differentiators — v1.x after validation):**
- Adjustable stream quality presets (Low/Medium/High) — free equivalent of what competitors charge for; requires quality negotiation field in AirShow protocol handshake designed in from the start
- Desktop sender: macOS first (ScreenCaptureKit stable), then Windows (DXGI duplication); Linux deferred
- Audio mirroring on macOS (CoreAudio loopback) and Windows (WASAPI loopback) — not Android (no system audio API without root)
- Multi-display source selection (desktop only, depends on desktop sender)

**Defer to v2+:**
- Low-latency mode toggle — requires receiver-side jitter buffer tuning
- Stream to multiple receivers simultaneously — high GPU/CPU cost, classroom use case only
- Receiver health/stats display
- Linux sender screen capture (PipeWire/xdg-desktop-portal too variable across distros)
- Android system audio (no viable API without root; document limitation clearly)

**Anti-features (never build):**
- Cloud relay for internet mirroring — contradicts LAN-only constraint
- Account/login system — contradicts no-backend constraint
- Native AirPlay/Cast/Miracast sender — legal risk or certification requirement
- Reverse control (remote input injection) — security surface, impossible on iOS without jailbreak

### Architecture Approach

The sender app follows a feature-first BLoC architecture with three feature modules: `discovery` (wrapping `multicast_dns`), `mirror` (BLoC state machine: Idle→Connecting→Streaming→Disconnected), and `settings`. The critical data-flow rule is: **native code handles all media (capture, encode, send over socket); Dart handles only control flow and session state**. Video frame data must never pass through `MethodChannel` or `EventChannel` — the overhead at 30fps causes visible lag. Instead, the native plugin opens its own TCP socket to the receiver and sends encoded NAL units directly, with Dart managing only session lifecycle.

The custom AirShow protocol uses a 16-byte binary header (type 1B + flags 1B + length 4B + PTS 8B) followed by a length-prefixed payload. Frame types: HANDSHAKE_REQUEST/RESPONSE (JSON), VIDEO_NALU (Annex-B H.264), AUDIO_FRAME (AAC-LC), KEEPALIVE, DISCONNECT. Handshake negotiates codec, resolution, and FPS before streaming begins. This single-round-trip design is simpler than RTSP and avoids WebRTC's ICE/STUN overhead entirely.

**Major components:**
1. **AirShowHandler (C++/Qt receiver)** — new `ProtocolHandler` impl; `QTcpServer` on port 7400; pushes NALUs via `gst_app_src_push_buffer()` to existing `MediaPipeline`; fits existing interface without changes to `ProtocolManager`
2. **DiscoveryManager extension (C++)** — adds `_airshow._tcp` advertisement on port 7400 to existing Avahi/Bonjour advertiser; TXT records: `ver=1`, `name=<hostname>`
3. **Flutter sender app** — feature-first BLoC; `DiscoveryService` (multicast_dns), `AirShowProtocolService` (dart:io TCP), `ScreenCaptureService` (platform channel dispatch)
4. **Per-platform native plugins** — Android: `ScreenCapturePlugin.kt` (MediaProjection + MediaCodec + Surface input API); iOS: `SampleHandler.swift` in a separate Broadcast Upload Extension target (ReplayKit + VideoToolbox); macOS: `ScreenCapturePlugin.swift` (ScreenCaptureKit); Windows: `screen_capture_plugin.cpp` (Windows.Graphics.Capture + MF H264 MFT, DXGI fallback); Linux: `screen_capture_plugin.cc` (PipeWire portal, v2)
5. **iOS Broadcast Upload Extension** — separate Xcode app target; captures screen in its own process; must send encoded NALUs directly over its own outbound socket to receiver (cannot call main app network stack)

### Critical Pitfalls

1. **iOS ReplayKit extension — 50 MB hard memory limit** — A single 2560x1600 iPad frame at 32bpp is ~16 MB raw. The extension must encode immediately with VideoToolbox H.264 rather than buffering raw frames. Extension should open its own outbound socket to the receiver directly rather than routing through the main app via XPC (XPC deserialization contributes to memory pressure). Defeat: design encode-then-send architecture before writing any extension code.

2. **Android MediaProjection API 34 consent non-reuse** — Caching the `Intent` across sessions throws `SecurityException` on Android 14+. Each session must call `createScreenCaptureIntent()` fresh, start a foreground service of type `mediaProjection`, and call `createVirtualDisplay()` once per token. The Surface input API (`createInputSurface()`) must be used — not raw buffer input — to avoid color format mismatches across SoCs (Qualcomm vs Nvidia vs TI). Defeat: correct architecture from Phase 1 of Android work; retrofitting requires full lifecycle rewrite.

3. **Flutter MethodChannel too slow for video frame data** — At 30fps, per-frame MethodChannel invocations cause visible lag. Native plugins must handle the entire encode+send pipeline; Dart code controls only session state. This architecture must be established before any data path is implemented — retrofitting it later requires rewriting the entire capture pipeline.

4. **iOS app extension cannot call main app network stack** — The Broadcast Upload Extension is a completely separate process with a separate address space. IPC must be via App Groups shared memory or, better, the extension opens its own outbound socket to the receiver. XPC with large payloads must be avoided. Decide the IPC architecture before writing any networking code.

5. **mDNS multicast blocked by AP client isolation** — On corporate/guest networks, multicast packets are silently dropped between devices on the same AP. Manual IP entry + QR code fallback must ship at v1. Show a helpful message after 10 seconds of no discovery; do not show an indefinite spinner.

6. **macOS TCC screen recording permission lost on re-sign** — TCC binds grants to the signing identity hash. Ad-hoc debug builds each get a new identity; stored permission is reset on every rebuild. Sign all builds (including debug) with a stable Developer ID from the start.

7. **Windows capture — UAC/elevated windows produce blank frames** — `Windows.Graphics.Capture` (primary path) cannot capture elevated windows or the UAC secure desktop. `OutputDuplication` (DXGI) handles DX12 apps and elevated contexts. Both paths needed before shipping; the dual-path architecture must be decided at design time.

---

## Implications for Roadmap

Based on the research, a 6-phase structure is recommended. The first phase is receiver-only (no Flutter work), establishing the protocol and transport before the sender exists. The remaining phases implement the sender per platform in descending confidence order, ending with v1.x polish features.

### Phase 1: Receiver Protocol Foundation
**Rationale:** Nothing in the sender app is testable until the receiver advertises `_airshow._tcp` and accepts connections. This is a prerequisite for all sender work. Receiver-side changes are low-risk (fit existing `ProtocolHandler` interface) and should be validated with a manual TCP test client before any Flutter development begins. The protocol handshake JSON must include quality negotiation fields now — they cannot be added later without breaking sender/receiver version compatibility.
**Delivers:** `AirShowHandler` on port 7400; `DiscoveryManager` advertising `_airshow._tcp`; handshake JSON parsing with quality fields; NALU + audio push to existing GStreamer appsrc pipeline; QML HUD showing "AirShow" + device name; monorepo structure (`sender/` Flutter app alongside `src/` C++ receiver)
**Avoids:** Protocol design that cannot accommodate quality negotiation (design it in now)

### Phase 2: Sender Core + Android MVP
**Rationale:** Android has the highest user count, the most mature Flutter ecosystem support, and HIGH-confidence APIs. Android MediaProjection + MediaCodec is the fastest path to a working end-to-end demo. Validate the full pipeline (capture → encode → TCP → receiver → display) on one platform before expanding. The Flutter app skeleton and AirShow protocol client are built here and reused on all subsequent platforms.
**Delivers:** Flutter app skeleton (BLoC architecture, DiscoveryService, AirShowProtocolService, receiver list UI, connection status, stop button, auto-reconnect); Android native plugin (MediaProjection + MediaCodec Surface input API + foreground service of type `mediaProjection`); working end-to-end mirror from an Android device to the receiver
**Uses:** `multicast_dns` 0.3.3, `dart:io` Socket, `pigeon` for Kotlin platform channel
**Avoids:** Android API 34 MediaProjection consent bug (fresh Intent per session, Surface input API mandatory); MethodChannel for video frame data (native plugin handles encode and socket directly)

### Phase 3: iOS Sender MVP
**Rationale:** iOS is the second highest-value platform and the most architecturally complex due to the Broadcast Upload Extension running in a separate OS process. It must be a dedicated phase — not bolted onto the Android phase — because the IPC architecture between extension and main app is a distinct design problem with no analog on any other platform.
**Delivers:** iOS Broadcast Upload Extension with VideoToolbox H.264 encode + direct outbound socket to receiver; `RPSystemBroadcastPickerView` launcher in main app; App Groups setup if needed for session lifecycle coordination; QR code fallback (needed for iOS users on corporate networks, ship here)
**Implements:** iOS screen capture architecture component, iOS app extension, QR code scanner
**Avoids:** 50 MB memory limit (encode immediately in extension, no raw frame buffering); iOS extension IPC problem (extension opens its own outbound socket rather than routing through main app)
**Research flag:** Needs `/gsd:research-phase` — ReplayKit extension + direct outbound socket pattern in a Flutter context has sparse official documentation. The App Groups vs. direct socket decision needs a concrete recommendation before implementation begins.

### Phase 4: macOS Sender
**Rationale:** macOS is the easiest desktop capture platform (ScreenCaptureKit is stable and well-documented) and is the typical development machine — faster iteration than Windows or Linux. Completing macOS sender also validates the desktop plugin FFI architecture before the more complex Windows dual-path implementation.
**Delivers:** macOS ScreenCaptureKit native plugin (Swift, `SCStream` + `VTCompressionSession`); VideoToolbox H.264 encode with Annex-B output; `NSScreenCaptureUsageDescription` Info.plist; stable Developer ID signing from day one
**Avoids:** CGDisplayStream (deprecated macOS 14+); TCC permission loss from ad-hoc signing; `screen_capturer` pub.dev package (its macOS maturity is LOW — custom FFI plugin is safer)

### Phase 5: Windows Sender
**Rationale:** Windows has HIGH-confidence screen capture APIs but the dual-path architecture (WGC primary + DXGI fallback) requires more implementation work than macOS. Keeping it as a dedicated phase after macOS ensures the desktop plugin pattern is proven before adding the Windows-specific complexity.
**Delivers:** Windows native plugin with `Windows.Graphics.Capture` as primary path + DXGI `OutputDuplication` fallback for DX12 apps and elevated windows; Media Foundation H264 MFT hardware encoder; yellow capture indicator border documented for users
**Avoids:** UAC/elevated-window blank-frame surprise (dual-path architecture required); DX12 interop freeze (DXGI fallback covers this)

### Phase 6: Quality + Reliability Polish (v1.x)
**Rationale:** Once all three primary sender platforms are streaming, add the differentiating features that require the stable transport to already exist. Quality presets require the protocol quality negotiation fields designed in Phase 1 but cannot be meaningfully exercised until streaming is stable. Audio mirroring on macOS and Windows is a separate complexity layer that should not block the video milestone.
**Delivers:** Quality preset UI (Low 720p/2Mbps, Medium 1080p/4Mbps, High 1080p/8Mbps) wired to protocol handshake quality fields; audio capture on macOS (CoreAudio loopback) and Windows (WASAPI loopback); multi-display source picker (desktop); auto-reconnect polish (exponential backoff, "Reconnecting..." UI state)
**Addresses:** All "Should have" competitive differentiators from FEATURES.md

### Phase Ordering Rationale

- Receiver-before-sender: the receiver must exist and accept connections before any sender code can be tested. This is a hard dependency.
- Android before iOS: higher user count, higher confidence APIs, faster development loop. Validates the full pipeline before tackling the iOS extension complexity.
- iOS as a dedicated phase: the Broadcast Upload Extension IPC architecture is fundamentally different from all other platforms and cannot share an implementation strategy with them. Underestimating this is the most common iOS screen-sharing project failure mode.
- macOS before Windows: ScreenCaptureKit is simpler and macOS is the development machine — easier to iterate the FFI plugin architecture before Windows.
- Quality features last: they depend on protocol handshake fields designed in Phase 1 but cannot be tested meaningfully until streaming is stable on at least two platforms.

### Research Flags

Phases needing deeper research during planning:
- **Phase 3 (iOS sender):** ReplayKit Broadcast Extension direct outbound socket pattern in a Flutter context is the least-documented combination in the research. Needs `/gsd:research-phase` to find concrete implementation examples before architecture is finalized.
- **Phase 4 (macOS signing):** TCC + code-signing identity interaction during CI/CD builds (every GitHub Actions rebuild potentially invalidating TCC permission) needs a concrete CI signing setup decision before implementation starts.

Phases with standard patterns (skip research-phase):
- **Phase 1 (AirShowHandler):** Exact pattern exists in `AirPlayHandler` and `CastHandler`. No unknowns; follow existing code.
- **Phase 2 (Android):** MediaProjection + MediaCodec Surface input API is thoroughly documented on developer.android.com. The API 34 pitfall is known and the fix is a specific, documented call sequence.
- **Phase 5 (Windows):** Windows.Graphics.Capture and DXGI OutputDuplication have thorough Microsoft docs and multiple open-source implementations (OBS, Teams, Discord) to reference.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | MEDIUM-HIGH | Flutter 3.41 and platform-native encoder APIs are HIGH confidence. Linux sender (PipeWire) is LOW — deferred to v2. Receiver stack unchanged and HIGH confidence. `multicast_dns` vs `nsd` decision is HIGH confidence (nsd explicitly drops Linux). |
| Features | MEDIUM | Table stakes and differentiators are well-supported by competitor analysis. iOS and macOS capture API constraints (15fps cap, 50MB limit) are HIGH confidence per Apple docs. Android system audio limitation is a hard platform constraint, not a gap. |
| Architecture | MEDIUM-HIGH | Receiver-side AirShowHandler architecture is HIGH confidence — fits existing interfaces cleanly. Flutter BLoC + native-handles-media data flow rule is validated in principle by the flutter_webrtc issue history but no reference AirShow-style sender exists to cross-check against. |
| Pitfalls | HIGH | Critical pitfalls (iOS 50MB, Android API 34, MethodChannel throughput, iOS extension IPC, AP client isolation, macOS TCC, Windows dual-path) are verified against official documentation and open-source project issue trackers. |

**Overall confidence:** MEDIUM — sufficient to plan and execute all six phases. The receiver extension is LOW risk. The mobile sender has well-documented pitfalls with clear avoidance strategies. Desktop sender is standard engineering with known APIs and open-source reference implementations.

### Gaps to Address

- **Quality negotiation protocol fields:** The AirShow wire protocol handshake must include quality negotiation fields (target bitrate, resolution caps, FPS) from Phase 1. These fields cannot be added later without breaking sender/receiver version compatibility. Exact field names and encoding must be finalized before Phase 1 implementation, not during Phase 6.
- **iOS Broadcast Extension IPC architecture decision:** Two viable paths exist (App Groups shared memory vs. extension opens its own outbound socket). The decision affects the entire iOS sender architecture. Needs research before Phase 3 begins.
- **Port 7400 conflict check:** Verify no conflict with other services commonly found on target networks (some SIP systems use ports in the 7400 range). If conflict exists, make port configurable in the receiver handshake response.
- **Linux sender scope:** Linux sender (PipeWire/xdg-desktop-portal) is LOW confidence and deferred to v2. Do not design Phase 6 around it — the Linux plugin stub should compile but return an unsupported error gracefully.

---

## Sources

### Primary (HIGH confidence)
- [Flutter SDK Archive — flutter.dev](https://docs.flutter.dev/release/archive) — Flutter 3.41.5 Feb 2026 stable confirmed
- [multicast_dns pub.dev](https://pub.dev/packages/multicast_dns) — 0.3.3, all five platforms, flutter.dev published
- [Android MediaProjection API — Android Developers](https://developer.android.com/media/grow/media-projection) — API 34 foreground service type, consent non-reuse requirement
- [Apple Developer: ReplayKit](https://developer.apple.com/documentation/ReplayKit) — 50 MB memory cap, ~15fps cap, DRM audio exclusion
- [UxPlay GitHub (FDH2/UxPlay)](https://github.com/FDH2/UxPlay) — Receiver stack reference, 1.73.6
- [GStreamer Releases Page](https://gstreamer.freedesktop.org/releases/) — 1.26.x current stable
- [Qt 6.8 LTS Release Blog](https://www.qt.io/blog/qt-6.8-released) — LTS until 2029
- [OpenSSL 1.1.1 EOL](https://www.openssl.org/blog/blog/2023/09/11/eol-111/) — EOL September 2023

### Secondary (MEDIUM confidence)
- [nsd pub.dev](https://pub.dev/packages/nsd) — 4.1.0, no Linux support confirmed
- [flutter_webrtc pub.dev](https://pub.dev/packages/flutter_webrtc) — historical Android API 34 MediaProjection bug reference (issue #1521, #1813)
- [scrcpy GitHub (Genymobile/scrcpy)](https://github.com/Genymobile/scrcpy) — competitor architecture: ADB + H.264 over socket
- [AirServer Connect support docs](https://support.airserver.com/support/solutions/articles/43000537120) — QR code flow, competitor feature set
- [Apple Developer: ScreenCaptureKit](https://developer.apple.com/documentation/screencapturekit) — macOS 12.3+ API, CGDisplayStream deprecated macOS 14
- [DXGI Desktop Duplication + Media Foundation H.264 MFT (alax.info)](https://alax.info/blog/1716) — Windows screen capture + hardware encode pattern
- [MS-MICE Miracast over Infrastructure (Microsoft Docs)](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/) — infrastructure Miracast spec
- [MiracleCast stalled development](https://github.com/albfan/miraclecast) — Wi-Fi Direct P2P blocker confirmed

### Tertiary (LOW confidence)
- [xdg-desktop-portal PipeWire — ArchWiki Screen Capture](https://wiki.archlinux.org/title/Screen_capture) — Linux sender approach, high variability across distros
- [ffmpeg_kit_flutter_new GitHub](https://github.com/SerenityS/ffmpeg_kit_flutter_new) — alternative encoding approach considered and rejected due to binary size and licensing surface

---

*Research completed: 2026-03-30*
*Ready for roadmap: yes*
