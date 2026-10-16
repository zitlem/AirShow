# Requirements: AirShow

**Defined:** 2026-03-28
**Core Value:** Any device can mirror its screen to any computer, for free

## v1 Requirements — Receiver

### Foundation
- **FOUND-01**: Application builds and runs on Linux, macOS, and Windows from a single codebase
- **FOUND-02**: Application renders video frames from GStreamer pipeline in a Qt fullscreen window
- **FOUND-03**: Application plays audio from mirrored device through system speakers
- **FOUND-04**: User can mute/unmute audio with a toggle control
- **FOUND-05**: Application detects and uses hardware H.264 decoder when available, falls back to software gracefully

### Discovery
- **DISC-01**: Application advertises as an AirPlay receiver via mDNS (`_airplay._tcp.local`)
- **DISC-02**: Application advertises as a Google Cast receiver via mDNS (`_googlecast._tcp.local`)
- **DISC-03**: Application advertises as a DLNA Media Renderer via UPnP/SSDP
- **DISC-04**: User can set a custom receiver name that appears in device pickers on sender devices
- **DISC-05**: Application registers firewall rules during installation/first-run so discovery works without manual config

### AirPlay
- **AIRP-01**: User can mirror their iPhone/iPad screen to AirShow via AirPlay
- **AIRP-02**: User can mirror their macOS screen to AirShow via AirPlay
- **AIRP-03**: AirPlay mirroring includes synchronized audio and video
- **AIRP-04**: AirPlay connection maintains stable A/V sync during extended sessions

### Google Cast
- **CAST-01**: User can cast their Android device screen to AirShow via Google Cast
- **CAST-02**: User can cast a Chrome browser tab to AirShow via Google Cast
- **CAST-03**: Google Cast mirroring includes synchronized audio and video

### Miracast
- **MIRA-01**: User can mirror their Windows device screen to AirShow via Miracast
- **MIRA-02**: User can mirror their Android device screen to AirShow via Miracast (where supported)
- **MIRA-03**: Miracast mirroring includes synchronized audio and video

### DLNA
- **DLNA-01**: User can push video files from a DLNA controller to AirShow for playback
- **DLNA-02**: User can push audio files from a DLNA controller to AirShow for playback
- **DLNA-03**: AirShow appears as a DLNA Media Renderer (DMR) in DLNA controller apps

### Display
- **DISP-01**: Mirrored content displays fullscreen with correct aspect ratio (letterboxed if needed)
- **DISP-02**: Application shows connection status (waiting/connected/device name/protocol)
- **DISP-03**: Application shows an idle/waiting screen when no device is connected

### Security
- **SEC-01**: User is prompted to approve or deny incoming connections before mirroring starts
- **SEC-02**: User can enable PIN-based pairing so only devices with the PIN can connect
- **SEC-03**: Application only listens on local network interfaces (not exposed to internet)

## v2 Requirements

Companion sender app and supporting receiver features.

### Receiver Protocol Extension
- **RECV-01**: AirShow receiver accepts connections from companion sender app via custom AirShow protocol on port 7400
- **RECV-02**: AirShow receiver advertises `_airshow._tcp` via mDNS so sender apps discover it automatically
- **RECV-03**: Protocol handshake includes quality negotiation (resolution, bitrate, latency mode)

### Sender App — Mobile
- **SEND-01**: User can mirror their Android device screen to AirShow via the companion sender app
- **SEND-02**: User can mirror their iOS device screen to AirShow via the companion sender app

### Sender App — Desktop
- **SEND-03**: User can mirror their macOS screen to AirShow via the companion sender app
- **SEND-04**: User can mirror their Windows screen to AirShow via the companion sender app

### Sender App — Audio
- **SEND-05**: Sender app captures and streams device audio alongside screen mirror (Android: system audio not available without root — documented limitation)

### Discovery & Connection
- **DISC-06**: Sender app auto-discovers AirShow receivers on local network via mDNS
- **DISC-07**: Sender app supports manual IP entry for networks where mDNS is blocked
- **DISC-08**: Receiver displays QR code that sender app can scan to connect

### Local Web Interface
- **WEB-01**: AirShow receiver serves a local web page (http://receiver-ip:7401) where users can download the companion sender app
- **WEB-02**: Web page serves pre-built APK (Android) and desktop installers from local storage — no internet required
- **WEB-03**: Web page shows the receiver name and QR code for quick connection

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

Status: ✅ verified on a real device · 🟡 implemented, not device-tested · 🔴 known broken · ⬜ not started. Steps refer to `ROADMAP.md`.

| Requirement | Status | Roadmap step |
|-------------|--------|--------------|
| FOUND-01 through FOUND-05 | 🟡 | 1, 6 |
| DISC-01 through DISC-05 | 🟡 | 2 |
| DISP-01 through DISP-03 | 🟡 | 2 |
| AIRP-01 through AIRP-04 | 🟡 | 2 |
| DLNA-01 through DLNA-03 | 🟡 | 2 |
| CAST-01 (Android screen) | 🔴 likely unachievable with current auth credentials | 4 |
| CAST-02, CAST-03 (Chrome tab + A/V) | 🔴 media receive path wrong | 3, 4 |
| SEC-01 through SEC-03 | 🟡 (Cast approval flow buggy) | 2, 3 |
| MIRA-01, MIRA-03 | 🟡 | 2 |
| MIRA-02 (Android Miracast) | 🟡 most devices are Wi-Fi Direct only | 2 |
| RECV-01, RECV-02, RECV-03 | 🟡 | 2 |
| SEND-01, SEND-05, DISC-06, DISC-07 | 🟡 | 2 |
| SEND-02, DISC-08 | ⬜ | 5 |
| SEND-03 | ⬜ | 5 |
| SEND-04 | ⬜ | 5 |
| WEB-01, WEB-02, WEB-03 | ⬜ | 5 |

---
*Requirements defined: 2026-03-28*
*Last updated: 2026-10-16 — statuses reset to reflect device-verified state; Cast marked broken*
