# Requirements: AirShow

**Defined:** 2026-03-28
**Core Value:** Any device can mirror its screen to any computer, for free

## v1 Requirements (Complete)

All v1 requirements delivered across Phases 1-8.

### Foundation
- [x] **FOUND-01**: Application builds and runs on Linux, macOS, and Windows from a single codebase
- [x] **FOUND-02**: Application renders video frames from GStreamer pipeline in a Qt fullscreen window
- [x] **FOUND-03**: Application plays audio from mirrored device through system speakers
- [x] **FOUND-04**: User can mute/unmute audio with a toggle control
- [x] **FOUND-05**: Application detects and uses hardware H.264 decoder when available, falls back to software gracefully

### Discovery
- [x] **DISC-01**: Application advertises as an AirPlay receiver via mDNS (`_airplay._tcp.local`)
- [x] **DISC-02**: Application advertises as a Google Cast receiver via mDNS (`_googlecast._tcp.local`)
- [x] **DISC-03**: Application advertises as a DLNA Media Renderer via UPnP/SSDP
- [x] **DISC-04**: User can set a custom receiver name that appears in device pickers on sender devices
- [x] **DISC-05**: Application registers firewall rules during installation/first-run so discovery works without manual config

### AirPlay
- [x] **AIRP-01**: User can mirror their iPhone/iPad screen to AirShow via AirPlay
- [x] **AIRP-02**: User can mirror their macOS screen to AirShow via AirPlay
- [x] **AIRP-03**: AirPlay mirroring includes synchronized audio and video
- [x] **AIRP-04**: AirPlay connection maintains stable A/V sync during extended sessions

### Google Cast
- [x] **CAST-01**: User can cast their Android device screen to AirShow via Google Cast
- [x] **CAST-02**: User can cast a Chrome browser tab to AirShow via Google Cast
- [x] **CAST-03**: Google Cast mirroring includes synchronized audio and video

### Miracast
- [x] **MIRA-01**: User can mirror their Windows device screen to AirShow via Miracast
- [x] **MIRA-02**: User can mirror their Android device screen to AirShow via Miracast (where supported)
- [x] **MIRA-03**: Miracast mirroring includes synchronized audio and video

### DLNA
- [x] **DLNA-01**: User can push video files from a DLNA controller to AirShow for playback
- [x] **DLNA-02**: User can push audio files from a DLNA controller to AirShow for playback
- [x] **DLNA-03**: AirShow appears as a DLNA Media Renderer (DMR) in DLNA controller apps

### Display
- [x] **DISP-01**: Mirrored content displays fullscreen with correct aspect ratio (letterboxed if needed)
- [x] **DISP-02**: Application shows connection status (waiting/connected/device name/protocol)
- [x] **DISP-03**: Application shows an idle/waiting screen when no device is connected

### Security
- [x] **SEC-01**: User is prompted to approve or deny incoming connections before mirroring starts
- [x] **SEC-02**: User can enable PIN-based pairing so only devices with the PIN can connect
- [x] **SEC-03**: Application only listens on local network interfaces (not exposed to internet)

## v2 Requirements

Requirements for companion sender app milestone. Each maps to roadmap phases.

### Receiver Protocol Extension
- [ ] **RECV-01**: AirShow receiver accepts connections from companion sender app via custom AirShow protocol on port 7400
- [ ] **RECV-02**: AirShow receiver advertises `_airshow._tcp` via mDNS so sender apps discover it automatically
- [ ] **RECV-03**: Protocol handshake includes quality negotiation (resolution, bitrate, latency mode)

### Sender App — Mobile
- [ ] **SEND-01**: User can mirror their Android device screen to AirShow via the companion sender app
- [ ] **SEND-02**: User can mirror their iOS device screen to AirShow via the companion sender app

### Sender App — Desktop
- [ ] **SEND-03**: User can mirror their macOS screen to AirShow via the companion sender app
- [ ] **SEND-04**: User can mirror their Windows screen to AirShow via the companion sender app

### Sender App — Audio
- [ ] **SEND-05**: Sender app captures and streams device audio alongside screen mirror (Android: system audio not available without root — documented limitation)

### Discovery & Connection
- [ ] **DISC-06**: Sender app auto-discovers AirShow receivers on local network via mDNS
- [ ] **DISC-07**: Sender app supports manual IP entry for networks where mDNS is blocked
- [ ] **DISC-08**: Receiver displays QR code that sender app can scan to connect

### Local Web Interface
- [ ] **WEB-01**: AirShow receiver serves a local web page (http://receiver-ip:7401) where users can download the companion sender app
- [ ] **WEB-02**: Web page serves pre-built APK (Android) and desktop installers from local storage — no internet required
- [ ] **WEB-03**: Web page shows the receiver name and QR code for quick connection

## Future Requirements

### Advanced Display (v3+)
- **ADV-01**: User can view multiple simultaneous mirrors in a tiled/split-screen layout
- **ADV-02**: User can choose picture-in-picture mode for multiple streams

### UX Polish (v3+)
- **UX-01**: System tray icon with quick settings access
- **UX-02**: Settings panel for configuring protocols, audio output, display preferences
- **UX-03**: Auto-start on login option

### Sender Extensions (v3+)
- Linux sender app (PipeWire/Wayland screen capture)
- Media file sending (photos/videos/music picker)

## Out of Scope

| Feature | Reason |
|---------|--------|
| Cloud/internet mirroring | Requires relay infrastructure; contradicts local-network-only design |
| DRM-protected content (Netflix, etc.) | Blocked at protocol level by FairPlay/Widevine; not a bug |
| Annotation / drawing overlay | Out of scope for lean receiver; defer to v3+ |
| Cloud sync / account system | No server infrastructure; contradicts free/open-source positioning |
| Linux sender app | PipeWire/Wayland screen capture varies too much across distros; deferred to v3 |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| FOUND-01 through FOUND-05 | Phase 1 | Complete |
| DISC-01 through DISC-05 | Phase 2 | Complete |
| DISP-01 through DISP-03 | Phase 3 | Complete |
| AIRP-01 through AIRP-04 | Phase 4 | Complete |
| DLNA-01 through DLNA-03 | Phase 5 | Complete |
| CAST-01 through CAST-03 | Phase 6 | Complete |
| SEC-01 through SEC-03 | Phase 7 | Complete |
| MIRA-01 through MIRA-03 | Phase 8 | Complete |
| RECV-01 through RECV-03 | TBD | Pending |
| SEND-01 through SEND-05 | TBD | Pending |
| DISC-06 through DISC-08 | TBD | Pending |
| WEB-01 through WEB-03 | TBD | Pending |

**Coverage:**
- v1 requirements: 29 total — all complete
- v2 requirements: 14 total — all pending
- Unmapped: 0

---
*Requirements defined: 2026-03-28*
*Last updated: 2026-03-31 — v2.0 requirements added*
