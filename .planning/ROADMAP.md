# Roadmap: MyAirShow

## Overview

MyAirShow is built from the ground up as a multi-protocol screen mirroring receiver. The journey starts with the render pipeline and window that all protocols target, then layers in discovery/advertisement infrastructure, then proves the full end-to-end path with AirPlay, extends to DLNA (simpler second protocol to validate the abstraction), then Google Cast and Miracast. Security and hardening runs last to lock down the complete product before v1 release.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [x] **Phase 1: Foundation** - Build system, GStreamer pipeline, Qt receiver window, audio output, and hardware decode (completed 2026-03-28)
- [x] **Phase 2: Discovery & Protocol Abstraction** - mDNS/SSDP advertisement for all protocols, protocol interfaces, receiver name, firewall rules (completed 2026-03-28)
- [x] **Phase 3: Display & Receiver UI** - Fullscreen mirroring window with correct aspect ratio, connection status HUD, and idle screen (completed 2026-03-28)
- [x] **Phase 4: AirPlay** - iOS and macOS screen mirroring via AirPlay with synchronized A/V and session management (completed 2026-03-28)
- [x] **Phase 5: DLNA** - DLNA Digital Media Renderer for video and audio file push from controller apps (completed 2026-03-29)
- [ ] **Phase 6: Google Cast** - Android and Chrome browser casting with synchronized A/V and swappable auth backend
- [ ] **Phase 7: Security & Hardening** - Connection approval, PIN pairing, LAN-only binding, and 30-minute A/V stability
- [ ] **Phase 8: Miracast** - Windows and Android screen mirroring via Miracast over Infrastructure

## Phase Details

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
  1. MyAirShow appears in the AirPlay menu on an iOS or macOS device on the same network
  2. MyAirShow appears in the Cast menu on an Android device or Chrome browser on the same network
  3. MyAirShow appears as a Media Renderer in a DLNA controller app on the same network
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
**Goal**: iPhone, iPad, and Mac users can mirror their screen to MyAirShow via AirPlay with stable, synchronized audio and video
**Depends on**: Phase 3
**Requirements**: AIRP-01, AIRP-02, AIRP-03, AIRP-04
**Success Criteria** (what must be TRUE):
  1. An iPhone or iPad can select MyAirShow from AirPlay screen mirroring and the mirrored screen appears on the receiver within 3 seconds
  2. A Mac can select MyAirShow from AirPlay and mirror its desktop to the receiver
  3. Audio from the mirroring device plays through the receiver's speakers in sync with the video — no persistent drift observable after 5 minutes
  4. A mirroring session lasting 30 minutes shows no A/V sync drift and no dropped-connection recovery needed
**Plans**: 3 plans
Plans:
- [x] 04-01-PLAN.md — UxPlay submodule + CMake integration + MediaPipeline appsrc mode + discovery TXT update API + test scaffold
- [x] 04-02-PLAN.md — AirPlayHandler implementation: RAOP lifecycle, raop_callbacks_t wiring, appsrc frame injection, session management
- [x] 04-03-PLAN.md — main.cpp wiring, plugin checks, real tests, end-to-end verification with Apple device

### Phase 5: DLNA
**Goal**: Users with DLNA controller apps can push video and audio files to MyAirShow for playback
**Depends on**: Phase 4
**Requirements**: DLNA-01, DLNA-02, DLNA-03
**Success Criteria** (what must be TRUE):
  1. A DLNA controller app (e.g., BubbleUPnP, foobar2000) can see MyAirShow listed as a Media Renderer
  2. Pushing a video file from the controller causes it to play on the receiver with video and audio
  3. Pushing an audio file from the controller causes it to play through the receiver's speakers
**Plans**: 3 plans
Plans:
- [x] 05-01-PLAN.md — DlnaHandler skeleton, UpnpAdvertiser SOAP routing, SCPD XMLs, MediaPipeline URI mode, test scaffold
- [x] 05-02-PLAN.md — DlnaHandler SOAP action implementations (AVTransport + RenderingControl + ConnectionManager)
- [x] 05-03-PLAN.md — main.cpp wiring, integration tests, end-to-end DLNA playback verification

### Phase 6: Google Cast
**Goal**: Android devices and Chrome browser tabs can cast their screen to MyAirShow with synchronized audio and video
**Depends on**: Phase 5
**Requirements**: CAST-01, CAST-02, CAST-03
**Success Criteria** (what must be TRUE):
  1. An Android device can select MyAirShow from the Cast menu and mirror its screen to the receiver
  2. Chrome browser's "Cast tab" option sends a browser tab to MyAirShow for display
  3. Audio from the casting device plays through the receiver's speakers in sync with the video
**Plans**: TBD

### Phase 7: Security & Hardening
**Goal**: Users control which devices can connect, credentials are stored safely, and the receiver is not exposed beyond the local network
**Depends on**: Phase 6
**Requirements**: SEC-01, SEC-02, SEC-03
**Success Criteria** (what must be TRUE):
  1. When a new device attempts to connect, the user sees an allow/deny prompt before any mirroring begins
  2. When PIN pairing is enabled, a device without the correct PIN cannot start a mirroring session
  3. The application does not accept connections from IP addresses outside the local network (RFC1918 ranges), even when a VPN is active
**Plans**: TBD

### Phase 8: Miracast
**Goal**: Windows and Android devices can mirror their screen to MyAirShow via Miracast over Infrastructure with synchronized audio and video
**Depends on**: Phase 7
**Requirements**: MIRA-01, MIRA-02, MIRA-03
**Success Criteria** (what must be TRUE):
  1. A Windows 10 or 11 device can select MyAirShow from "Connect" / wireless display and mirror its desktop to the receiver over the existing LAN
  2. An Android device that supports Miracast can mirror its screen to MyAirShow
  3. Audio from the Miracast source plays through the receiver's speakers in sync with the video
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Foundation | 3/3 | Complete   | 2026-03-28 |
| 2. Discovery & Protocol Abstraction | 3/3 | Complete   | 2026-03-28 |
| 3. Display & Receiver UI | 3/3 | Complete   | 2026-03-28 |
| 4. AirPlay | 3/3 | Complete   | 2026-03-28 |
| 5. DLNA | 3/3 | Complete   | 2026-03-29 |
| 6. Google Cast | 0/? | Not started | - |
| 7. Security & Hardening | 0/? | Not started | - |
| 8. Miracast | 0/? | Not started | - |
