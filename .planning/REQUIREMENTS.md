# Requirements: MyAirShow

**Defined:** 2026-03-28
**Core Value:** Any device can mirror its screen to any computer, for free

## v1 Requirements

Requirements for initial release. Each maps to roadmap phases.

### Foundation

- [x] **FOUND-01**: Application builds and runs on Linux, macOS, and Windows from a single codebase
- [x] **FOUND-02**: Application renders video frames from GStreamer pipeline in a Qt fullscreen window
- [x] **FOUND-03**: Application plays audio from mirrored device through system speakers
- [x] **FOUND-04**: User can mute/unmute audio with a toggle control
- [x] **FOUND-05**: Application detects and uses hardware H.264 decoder when available, falls back to software gracefully

### Discovery

- [ ] **DISC-01**: Application advertises as an AirPlay receiver via mDNS (`_airplay._tcp.local`)
- [ ] **DISC-02**: Application advertises as a Google Cast receiver via mDNS (`_googlecast._tcp.local`)
- [ ] **DISC-03**: Application advertises as a DLNA Media Renderer via UPnP/SSDP
- [ ] **DISC-04**: User can set a custom receiver name that appears in device pickers on sender devices
- [ ] **DISC-05**: Application registers firewall rules during installation/first-run so discovery works without manual config

### AirPlay

- [ ] **AIRP-01**: User can mirror their iPhone/iPad screen to MyAirShow via AirPlay
- [ ] **AIRP-02**: User can mirror their macOS screen to MyAirShow via AirPlay
- [ ] **AIRP-03**: AirPlay mirroring includes synchronized audio and video
- [ ] **AIRP-04**: AirPlay connection maintains stable A/V sync during extended sessions

### Google Cast

- [ ] **CAST-01**: User can cast their Android device screen to MyAirShow via Google Cast
- [ ] **CAST-02**: User can cast a Chrome browser tab to MyAirShow via Google Cast
- [ ] **CAST-03**: Google Cast mirroring includes synchronized audio and video

### Miracast

- [ ] **MIRA-01**: User can mirror their Windows device screen to MyAirShow via Miracast
- [ ] **MIRA-02**: User can mirror their Android device screen to MyAirShow via Miracast (where supported)
- [ ] **MIRA-03**: Miracast mirroring includes synchronized audio and video

### DLNA

- [ ] **DLNA-01**: User can push video files from a DLNA controller to MyAirShow for playback
- [ ] **DLNA-02**: User can push audio files from a DLNA controller to MyAirShow for playback
- [ ] **DLNA-03**: MyAirShow appears as a DLNA Media Renderer (DMR) in DLNA controller apps

### Display

- [ ] **DISP-01**: Mirrored content displays fullscreen with correct aspect ratio (letterboxed if needed)
- [ ] **DISP-02**: Application shows connection status (waiting/connected/device name/protocol)
- [ ] **DISP-03**: Application shows an idle/waiting screen when no device is connected

### Security

- [ ] **SEC-01**: User is prompted to approve or deny incoming connections before mirroring starts
- [ ] **SEC-02**: User can enable PIN-based pairing so only devices with the PIN can connect
- [ ] **SEC-03**: Application only listens on local network interfaces (not exposed to internet)

## v2 Requirements

### Advanced Display

- **ADV-01**: User can view multiple simultaneous mirrors in a tiled/split-screen layout
- **ADV-02**: User can choose picture-in-picture mode for multiple streams

### UX Polish

- **UX-01**: System tray icon with quick settings access
- **UX-02**: Settings panel for configuring protocols, audio output, display preferences
- **UX-03**: Auto-start on login option

## Out of Scope

| Feature | Reason |
|---------|--------|
| Session recording / screen capture | Use OBS or OS screen recorder to capture the receiver window |
| Streaming FROM computer to devices (sender mode) | Completely different product; doubles scope |
| Remote/internet mirroring | Requires relay infrastructure; contradicts local-network-only design |
| Mobile app receiver (iOS/Android) | Desktop only; different platform target entirely |
| DRM-protected content (Netflix, etc.) | Blocked at protocol level by FairPlay/Widevine; not a bug |
| Annotation / drawing overlay | Out of scope for lean receiver; defer to v2+ |
| Cloud sync / account system | No server infrastructure; contradicts free/open-source positioning |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| FOUND-01 | Phase 1 | Complete |
| FOUND-02 | Phase 1 | Complete |
| FOUND-03 | Phase 1 | Complete |
| FOUND-04 | Phase 1 | Complete |
| FOUND-05 | Phase 1 | Complete |
| DISC-01 | Phase 2 | Pending |
| DISC-02 | Phase 2 | Pending |
| DISC-03 | Phase 2 | Pending |
| DISC-04 | Phase 2 | Pending |
| DISC-05 | Phase 2 | Pending |
| DISP-01 | Phase 3 | Pending |
| DISP-02 | Phase 3 | Pending |
| DISP-03 | Phase 3 | Pending |
| AIRP-01 | Phase 4 | Pending |
| AIRP-02 | Phase 4 | Pending |
| AIRP-03 | Phase 4 | Pending |
| AIRP-04 | Phase 4 | Pending |
| DLNA-01 | Phase 5 | Pending |
| DLNA-02 | Phase 5 | Pending |
| DLNA-03 | Phase 5 | Pending |
| CAST-01 | Phase 6 | Pending |
| CAST-02 | Phase 6 | Pending |
| CAST-03 | Phase 6 | Pending |
| SEC-01 | Phase 7 | Pending |
| SEC-02 | Phase 7 | Pending |
| SEC-03 | Phase 7 | Pending |
| MIRA-01 | Phase 8 | Pending |
| MIRA-02 | Phase 8 | Pending |
| MIRA-03 | Phase 8 | Pending |

**Coverage:**
- v1 requirements: 29 total
- Mapped to phases: 29
- Unmapped: 0

---
*Requirements defined: 2026-03-28*
*Last updated: 2026-03-28 after roadmap creation*
