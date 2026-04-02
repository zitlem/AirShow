# Roadmap: AirShow

## Milestones

- ✅ **v1.0 Multi-Protocol Receiver** - Phases 1-8 (shipped 2026-03-30)
- 🚧 **v2.0 Companion Sender** - Phases 9-14 (in progress)

## Phases

<details>
<summary>✅ v1.0 Multi-Protocol Receiver (Phases 1-8) - SHIPPED 2026-03-30</summary>

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [x] **Phase 1: Foundation** - Build system, GStreamer pipeline, Qt receiver window, audio output, and hardware decode (completed 2026-03-28)
- [x] **Phase 2: Discovery & Protocol Abstraction** - mDNS/SSDP advertisement for all protocols, protocol interfaces, receiver name, firewall rules (completed 2026-03-28)
- [x] **Phase 3: Display & Receiver UI** - Fullscreen mirroring window with correct aspect ratio, connection status HUD, and idle screen (completed 2026-03-28)
- [x] **Phase 4: AirPlay** - iOS and macOS screen mirroring via AirPlay with synchronized A/V and session management (completed 2026-03-28)
- [x] **Phase 5: DLNA** - DLNA Digital Media Renderer for video and audio file push from controller apps (completed 2026-03-29)
- [x] **Phase 6: Google Cast** - Android and Chrome browser casting with synchronized A/V and swappable auth backend (completed 2026-03-29)
- [x] **Phase 7: Security & Hardening** - Connection approval, PIN pairing, LAN-only binding, and 30-minute A/V stability (completed 2026-03-30)
- [x] **Phase 8: Miracast** - Windows and Android screen mirroring via Miracast over Infrastructure (completed 2026-03-30)

### Phase 1: Foundation
**Goal**: The application builds and runs on all three platforms with a working media pipeline that can receive and display video frames and audio
**Depends on**: Nothing (first phase)
**Requirements**: FOUND-01, FOUND-02, FOUND-03, FOUND-04, FOUND-05
**Success Criteria** (what must be TRUE):
  1. Running `cmake --build` on Linux, macOS, and Windows produces a launchable binary without manual dependency steps
  2. The application window opens fullscreen and renders a GStreamer test video source with visible moving frames
  3. Audio from a GStreamer test audio source plays through system speakers
  4. The mute/unmute toggle silences and restores audio during playback
  5. When hardware H.264 decode is unavailable, the application falls back to software decode and logs which decoder is active
**Plans**: 3 plans
Plans:
- [x] 01-01-PLAN.md — CMake build system, project skeleton, test scaffold, GitHub Actions CI
- [x] 01-02-PLAN.md — Two-branch GStreamer pipeline, Qt QML fullscreen window, audio mute toggle
- [x] 01-03-PLAN.md — Hardware H.264 decoder detection with software fallback logging

### Phase 2: Discovery & Protocol Abstraction
**Goal**: The receiver is visible in device pickers on sender devices and protocol handler interfaces are defined before any protocol code is written
**Depends on**: Phase 1
**Requirements**: DISC-01, DISC-02, DISC-03, DISC-04, DISC-05
**Success Criteria** (what must be TRUE):
  1. AirShow appears in the AirPlay menu on an iOS or macOS device on the same network
  2. AirShow appears in the Cast menu on an Android device or Chrome browser on the same network
  3. AirShow appears as a Media Renderer in a DLNA controller app on the same network
  4. User can change the receiver name in settings and the new name immediately appears in device pickers on sender devices
  5. On Windows, discovery works without the user manually opening firewall ports after a fresh install
**Plans**: 3 plans
Plans:
- [x] 02-01-PLAN.md — Wave 0 test scaffold + protocol/discovery/settings interface contracts
- [x] 02-02-PLAN.md — mDNS advertisement: AvahiAdvertiser, DiscoveryManager (AirPlay + Cast), AppSettings wiring
- [x] 02-03-PLAN.md — DLNA SSDP: UpnpAdvertiser + MediaRenderer.xml; WindowsFirewall first-launch stub

### Phase 3: Display & Receiver UI
**Goal**: The receiver window displays mirrored content correctly at all aspect ratios and communicates connection state to the user
**Depends on**: Phase 2
**Requirements**: DISP-01, DISP-02, DISP-03
**Success Criteria** (what must be TRUE):
  1. Mirrored content fills the window fullscreen with letterboxing when the sender aspect ratio differs from the display
  2. The receiver window shows which device is connected and which protocol is in use while a session is active
  3. When no device is connected, the window displays an idle screen rather than a black frame
**Plans**: 3 plans
Plans:
- [x] 03-01-PLAN.md — Interface contracts (ConnectionBridge.h, SettingsBridge.h) + test_display scaffold
- [x] 03-02-PLAN.md — ConnectionBridge.cpp + SettingsBridge.cpp implementations; wire context properties in ReceiverWindow
- [x] 03-03-PLAN.md — QML UI: HudOverlay.qml, IdleScreen.qml, main.qml updates, CMakeLists.txt QML_FILES

### Phase 4: AirPlay
**Goal**: iPhone, iPad, and Mac users can mirror their screen to AirShow via AirPlay with stable, synchronized audio and video
**Depends on**: Phase 3
**Requirements**: AIRP-01, AIRP-02, AIRP-03, AIRP-04
**Success Criteria** (what must be TRUE):
  1. An iPhone or iPad can select AirShow from AirPlay screen mirroring and the mirrored screen appears on the receiver within 3 seconds
  2. A Mac can select AirShow from AirPlay and mirror its desktop to the receiver
  3. Audio from the mirroring device plays through the receiver's speakers in sync with the video — no persistent drift observable after 5 minutes
  4. A mirroring session lasting 30 minutes shows no A/V sync drift and no dropped-connection recovery needed
**Plans**: 3 plans
Plans:
- [x] 04-01-PLAN.md — UxPlay submodule + CMake integration + MediaPipeline appsrc mode + discovery TXT update API + test scaffold
- [x] 04-02-PLAN.md — AirPlayHandler implementation: RAOP lifecycle, raop_callbacks_t wiring, appsrc frame injection, session management
- [x] 04-03-PLAN.md — main.cpp wiring, plugin checks, real tests, end-to-end verification with Apple device

### Phase 5: DLNA
**Goal**: Users with DLNA controller apps can push video and audio files to AirShow for playback
**Depends on**: Phase 4
**Requirements**: DLNA-01, DLNA-02, DLNA-03
**Success Criteria** (what must be TRUE):
  1. A DLNA controller app (e.g., BubbleUPnP, foobar2000) can see AirShow listed as a Media Renderer
  2. Pushing a video file from the controller causes it to play on the receiver with video and audio
  3. Pushing an audio file from the controller causes it to play through the receiver's speakers
**Plans**: 3 plans
Plans:
- [x] 05-01-PLAN.md — DlnaHandler skeleton, UpnpAdvertiser SOAP routing, SCPD XMLs, MediaPipeline URI mode, test scaffold
- [x] 05-02-PLAN.md — DlnaHandler SOAP action implementations (AVTransport + RenderingControl + ConnectionManager)
- [x] 05-03-PLAN.md — main.cpp wiring, integration tests, end-to-end DLNA playback verification

### Phase 6: Google Cast
**Goal**: Android devices and Chrome browser tabs can cast their screen to AirShow with synchronized audio and video
**Depends on**: Phase 5
**Requirements**: CAST-01, CAST-02, CAST-03
**Success Criteria** (what must be TRUE):
  1. An Android device can select AirShow from the Cast menu and mirror its screen to the receiver
  2. Chrome browser's "Cast tab" option sends a browser tab to AirShow for display
  3. Audio from the casting device plays through the receiver's speakers in sync with the video
**Plans**: 3 plans
Plans:
- [x] 06-01-PLAN.md — Protobuf codegen, CastHandler TLS server, CastSession CASTV2 framing and namespace dispatch, test scaffold
- [x] 06-02-PLAN.md — MediaPipeline WebRTC mode (webrtcbin), Cast OFFER/ANSWER SDP translation, VP8/Opus decode, AES-CTR decrypt
- [x] 06-03-PLAN.md — main.cpp wiring, plugin checks, integration tests, end-to-end Chrome tab cast verification

### Phase 7: Security & Hardening
**Goal**: Users control which devices can connect, credentials are stored safely, and the receiver is not exposed beyond the local network
**Depends on**: Phase 6
**Requirements**: SEC-01, SEC-02, SEC-03
**Success Criteria** (what must be TRUE):
  1. When a new device attempts to connect, the user sees an allow/deny prompt before any mirroring begins
  2. When PIN pairing is enabled, a device without the correct PIN cannot start a mirroring session
  3. The application does not accept connections from IP addresses outside the local network (RFC1918 ranges), even when a VPN is active
**Plans**: 3 plans
Plans:
- [x] 07-01-PLAN.md — SecurityManager class, AppSettings/SettingsBridge/ConnectionBridge security extensions, test scaffold
- [x] 07-02-PLAN.md — Protocol handler integration (AirPlay/DLNA/Cast security checks), main.cpp wiring
- [x] 07-03-PLAN.md — QML approval dialog overlay, PIN display on idle screen, visual verification

### Phase 8: Miracast
**Goal**: Windows and Android devices can mirror their screen to AirShow via Miracast over Infrastructure with synchronized audio and video
**Depends on**: Phase 7
**Requirements**: MIRA-01, MIRA-02, MIRA-03
**Success Criteria** (what must be TRUE):
  1. A Windows 10 or 11 device can select AirShow from "Connect" / wireless display and mirror its desktop to the receiver over the existing LAN
  2. An Android device that supports Miracast can mirror its screen to AirShow
  3. Audio from the Miracast source plays through the receiver's speakers in sync with the video
**Plans**: 3 plans
Plans:
- [x] 08-01-PLAN.md — MiracastHandler skeleton, initMiracastPipeline() MPEG-TS/RTP mode, _display._tcp mDNS, test scaffold
- [x] 08-02-PLAN.md — MS-MICE SOURCE_READY + WFD RTSP M1-M7 client state machine, SecurityManager integration
- [x] 08-03-PLAN.md — main.cpp wiring, GStreamer plugin checks, integration tests, end-to-end Windows verification

</details>

---

## v2.0 Companion Sender (Phases 9-14)

**Milestone Goal:** Build a Flutter-based companion sender app that discovers AirShow receivers on the local network and mirrors the device screen to them, with a custom AirShow protocol giving full control over quality and latency. Targets Android, iOS, macOS, and Windows from a single Dart codebase.

- [ ] **Phase 9: Receiver Protocol Foundation** - AirShowHandler on port 7400, `_airshow._tcp` mDNS advertisement, handshake with quality negotiation, monorepo structure
- [x] **Phase 10: Android Sender MVP** - Flutter app skeleton (BLoC, discovery, connection UI), Android screen capture + H.264 encode, audio streaming, auto-discovery and manual IP entry (completed 2026-04-02)
- [ ] **Phase 11: iOS Sender MVP** - iOS Broadcast Upload Extension with VideoToolbox encode, ReplayKit launcher, QR code connect flow
- [ ] **Phase 12: macOS Sender** - ScreenCaptureKit native plugin, VideoToolbox H.264 encode, stable Developer ID signing
- [ ] **Phase 13: Windows Sender** - Windows.Graphics.Capture primary + DXGI fallback, Media Foundation H.264 MFT encoder
- [ ] **Phase 14: Web Interface & Distribution** - Qt HTTP server on port 7401, installer download page, QR code display on receiver idle screen

## Phase Details

### Phase 9: Receiver Protocol Foundation
**Goal**: The AirShow receiver accepts connections from the companion sender app via a custom protocol, advertises itself via mDNS, and the monorepo structure exists so Flutter and C++ development can proceed in parallel
**Depends on**: Phase 8
**Requirements**: RECV-01, RECV-02, RECV-03
**Success Criteria** (what must be TRUE):
  1. A raw TCP client connecting to port 7400 and sending a valid handshake JSON receives a handshake response with codec, resolution, and bitrate fields
  2. AirShow appears in a `dns-sd` or `avahi-browse` listing as `_airshow._tcp` on the local network
  3. The handshake round-trip includes quality negotiation fields (resolution cap, target bitrate, FPS) that the receiver echoes back with accepted values
  4. A `sender/` Flutter project directory exists in the repo alongside `src/` with a passing `flutter analyze` and placeholder screen
  5. NAL units pushed through the established connection appear on the receiver display via the existing GStreamer appsrc pipeline
**Plans**: 2 plans
Plans:
- [x] 09-01-PLAN.md — AirShowHandler TCP server, JSON handshake with quality negotiation, 16-byte binary frame parser, appsrc NAL injection, test scaffold
- [x] 09-02-PLAN.md — _airshow._tcp mDNS advertisement, main.cpp handler wiring, Flutter sender scaffold, end-to-end verification

### Phase 10: Android Sender MVP
**Goal**: An Android user can open the companion app, see AirShow receivers on the network, tap one, and have their screen mirrored to the receiver with audio
**Depends on**: Phase 9
**Requirements**: SEND-01, SEND-05, DISC-06, DISC-07
**Success Criteria** (what must be TRUE):
  1. An Android device opens the sender app and within 10 seconds shows a list of AirShow receivers discovered on the local network
  2. Tapping a receiver starts mirroring — the Android screen appears on the receiver display within 5 seconds
  3. Audio from the Android device plays through the receiver's speakers alongside the video mirror
  4. A "Stop" button (and Android notification action) ends mirroring and returns the app to the receiver list
  5. When mDNS discovery finds nothing after 10 seconds, the user sees a manual IP entry field and can connect by entering the receiver's IP address
**Plans**: 3 plans
Plans:
- [x] 10-01-PLAN.md — Flutter Dart layer: BLoC cubits (discovery + session), mDNS service, AirShowChannel bridge, UI screens, unit tests
- [x] 10-02-PLAN.md �� Android native Kotlin: AndroidManifest permissions, MainActivity channels, AirShowCaptureService, H264Encoder, audio capture
- [x] 10-03-PLAN.md — Receiver audio wiring (AirShowHandler.cpp type=0x02), end-to-end device verification
**UI hint**: yes

### Phase 11: iOS Sender MVP
**Goal**: An iOS user can launch the companion app, initiate a broadcast, and have their iPhone or iPad screen mirrored to an AirShow receiver — including a QR code scan path for networks where mDNS is blocked
**Depends on**: Phase 10
**Requirements**: SEND-02, DISC-08
**Success Criteria** (what must be TRUE):
  1. An iPhone or iPad can start mirroring to an AirShow receiver via the companion app's broadcast picker within 10 seconds of tapping "Start Mirroring"
  2. The iOS screen appears on the receiver display while the Broadcast Upload Extension is active, with the receiver showing the device name
  3. The receiver displays a QR code on its idle screen; scanning it with the sender app connects without manual IP entry
  4. Stopping the iOS system broadcast (from Control Center or in-app) cleanly disconnects the session without crashing the extension
**Plans**: TBD
**UI hint**: yes

### Phase 12: macOS Sender
**Goal**: A macOS user can select their screen (or a specific window) in the companion app and mirror it to an AirShow receiver using ScreenCaptureKit
**Depends on**: Phase 11
**Requirements**: SEND-03
**Success Criteria** (what must be TRUE):
  1. A macOS user opens the sender app, grants screen recording permission when prompted, and sees their screen mirrored on the receiver within 5 seconds of clicking "Start"
  2. The macOS sender encodes video using VideoToolbox hardware H.264 acceleration (logged at session start)
  3. TCC screen recording permission granted to a stable Developer ID build persists across app relaunches and does not require re-granting after a rebuild
  4. Stopping mirroring from the macOS app cleanly tears down the session and the receiver returns to its idle screen
**Plans**: TBD
**UI hint**: yes

### Phase 13: Windows Sender
**Goal**: A Windows user can mirror their desktop to an AirShow receiver using the companion app, with a DXGI fallback that handles DX12 applications and elevated windows that the primary capture path cannot capture
**Depends on**: Phase 12
**Requirements**: SEND-04
**Success Criteria** (what must be TRUE):
  1. A Windows user opens the sender app and starts mirroring — the Windows desktop appears on the receiver display within 5 seconds
  2. When a DX12 application (e.g., a game or 3D app) is on screen, the stream continues without blank frames (DXGI fallback active)
  3. The sender app captures and encodes video using Media Foundation hardware H.264 MFT (logged at session start)
  4. Stopping mirroring from the Windows app cleanly tears down the session and the receiver returns to its idle screen
**Plans**: TBD
**UI hint**: yes

### Phase 14: Web Interface & Distribution
**Goal**: Anyone on the local network can open a browser, navigate to the receiver's IP address, see the AirShow landing page, download the companion app installer for their platform, and scan the QR code to connect — all without internet access
**Depends on**: Phase 13
**Requirements**: WEB-01, WEB-02, WEB-03
**Success Criteria** (what must be TRUE):
  1. Opening `http://<receiver-ip>:7401` in a browser on the local network shows the AirShow web page with the receiver name and a QR code
  2. The web page offers download links for the Android APK and desktop installers that work without any internet connection (files served from the receiver's local storage)
  3. Scanning the QR code displayed on the web page (or on the receiver's idle screen) with the sender app connects to the receiver without manual IP entry
  4. The web interface is served by the receiver process itself with no external web server dependency
**Plans**: TBD
**UI hint**: yes

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Foundation | v1.0 | 3/3 | Complete | 2026-03-28 |
| 2. Discovery & Protocol Abstraction | v1.0 | 3/3 | Complete | 2026-03-28 |
| 3. Display & Receiver UI | v1.0 | 3/3 | Complete | 2026-03-28 |
| 4. AirPlay | v1.0 | 3/3 | Complete | 2026-03-28 |
| 5. DLNA | v1.0 | 3/3 | Complete | 2026-03-29 |
| 6. Google Cast | v1.0 | 3/3 | Complete | 2026-03-29 |
| 7. Security & Hardening | v1.0 | 3/3 | Complete | 2026-03-30 |
| 8. Miracast | v1.0 | 3/3 | Complete | 2026-03-30 |
| 9. Receiver Protocol Foundation | v2.0 | 1/2 | In Progress|  |
| 10. Android Sender MVP | v2.0 | 3/3 | Complete   | 2026-04-02 |
| 11. iOS Sender MVP | v2.0 | 0/? | Not started | - |
| 12. macOS Sender | v2.0 | 0/? | Not started | - |
| 13. Windows Sender | v2.0 | 0/? | Not started | - |
| 14. Web Interface & Distribution | v2.0 | 0/? | Not started | - |
