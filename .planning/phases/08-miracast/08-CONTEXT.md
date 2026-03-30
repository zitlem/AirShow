# Phase 8: Miracast - Context

**Gathered:** 2026-03-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement Miracast over Infrastructure (MS-MICE) receiver: accept screen mirroring from Windows 10/11 devices and compatible Android devices over the existing LAN. Per CLAUDE.md, Wi-Fi Direct Miracast is explicitly skipped (too fragile, wpa_supplicant P2P API not stable). MS-MICE uses standard TCP/UDP networking — no Wi-Fi P2P required. This phase creates a `MiracastHandler : ProtocolHandler` that implements the MS-MICE protocol (RTSP session setup + RTP H.264/AAC streaming).

</domain>

<decisions>
## Implementation Decisions

### Miracast Variant
- **D-01:** Implement Miracast over Infrastructure (MS-MICE) ONLY. Do NOT implement Wi-Fi Direct Miracast. CLAUDE.md explicitly states: "v1: Skip Wi-Fi Direct Miracast entirely. Too fragile."
- **D-02:** MS-MICE works over the existing LAN (TCP for RTSP signaling, UDP for RTP media). No Wi-Fi P2P, no wpa_supplicant, no MiracleCast dependency.
- **D-03:** Primary target is Windows 10/11 which has built-in MS-MICE support via "Connect" / "Wireless Display" settings. Android support for infrastructure-mode Miracast is limited and varies by OEM — document as best-effort.

### Discovery
- **D-04:** Advertise the MS-MICE receiver via mDNS using `_display._tcp` service type (the standard mDNS service type for wireless display receivers over infrastructure). Add to DiscoveryManager alongside existing AirPlay/Cast/DLNA advertisements.
- **D-05:** Also advertise via DNS-SD TXT records with device capabilities: source-coupling, sink capabilities, supported codecs.

### Protocol
- **D-06:** Implement the MS-MICE RTSP signaling layer: SETUP (negotiate transport), PLAY (start streaming), PAUSE, TEARDOWN. The MS-MICE protocol is documented in MS-MICE specification (public Microsoft protocol doc).
- **D-07:** Handle the WFD (Wi-Fi Display) capability negotiation that Windows sends in the RTSP OPTIONS/SETUP exchange. Return supported codecs (H.264 CBP/CHP) and audio formats (AAC-LC, LPCM).
- **D-08:** The RTSP server listens on a dedicated port (default 7236, the standard WFD port for infrastructure mode).

### Media Pipeline
- **D-09:** Miracast sends H.264 video + AAC-LC audio over MPEG-TS wrapped in RTP. Use GStreamer's `rtpmp2tdepay` → `tsdemux` → `h264parse` → hardware decode (`vaapidecodebin` on Linux, fallback `avdec_h264`) for video, and `aacparse` → `avdec_aac` → `audioconvert` → `autoaudiosink` for audio.
- **D-10:** Create a new pipeline mode `initMiracastPipeline()` in MediaPipeline for MPEG-TS/RTP demuxing, following the established pattern of initAppsrcPipeline, initUriPipeline, initWebrtcPipeline.
- **D-11:** Use the existing `qml6glsink` video sink and `autoaudiosink` audio sink chain for output, consistent with all other protocols.

### Architecture
- **D-12:** Create `MiracastHandler : ProtocolHandler` in `src/protocol/MiracastHandler.h` following the established pattern.
- **D-13:** MiracastHandler owns the RTSP server socket on port 7236, manages WFD capability negotiation, and controls the GStreamer pipeline for each session.
- **D-14:** Single-session model consistent with all other protocols (Phase 4 D-08/D-09).
- **D-15:** SecurityManager integration: checkConnection() called before session establishment. Network filter (RFC1918) applies.
- **D-16:** ConnectionBridge HUD: shows "Miracast" as protocol and sender device name.

### Claude's Discretion
- RTSP server implementation details (Qt QTcpServer vs raw sockets)
- Exact WFD capability exchange format and values
- HDCP handling (skip for v1 — open-source receiver can't do HDCP)
- Whether to use GStreamer's `rtspsrc` element or manual RTSP parsing
- Exact MPEG-TS demux pipeline element chain
- Error handling for unsupported codec profiles
- UDP port negotiation for RTP media transport

</decisions>

<canonical_refs>
## Canonical References

### Protocol Pattern Reference
- `src/protocol/ProtocolHandler.h` — Interface
- `src/protocol/ProtocolManager.h` — Registration
- `src/protocol/AirPlayHandler.h` — RTSP-based protocol reference (closest pattern)
- `src/protocol/CastHandler.h` — TLS server socket pattern

### Pipeline Integration
- `src/pipeline/MediaPipeline.h` — Add initMiracastPipeline() mode
- `src/pipeline/MediaPipeline.cpp` — Existing modes: appsrc, uridecodebin, webrtcbin

### Discovery
- `src/discovery/DiscoveryManager.cpp` — Add _display._tcp advertisement
- `src/discovery/AvahiAdvertiser.cpp` — mDNS advertisement

### Security
- `src/security/SecurityManager.h` — checkConnection() for device approval

### UI
- `src/ui/ConnectionBridge.h` — HUD updates

### External References
- MS-MICE spec: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice
- WFD (Wi-Fi Display) Technical Specification (Wi-Fi Alliance)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- ProtocolHandler interface + ProtocolManager registration
- SecurityManager (SEC-01/02/03) already wired for all handlers
- ConnectionBridge HUD
- MediaPipeline with multiple modes
- AvahiAdvertiser for mDNS
- GStreamer H.264 decode already used by AirPlay (similar pipeline)

### Integration Points
- DiscoveryManager::startAll() — add _display._tcp advertisement
- ProtocolManager::addHandler() — register MiracastHandler
- main.cpp — create and wire MiracastHandler
- SecurityManager::checkConnection() — approval flow

</code_context>

<specifics>
## Specific Ideas

- AirPlay already uses RTSP for signaling — MiracastHandler can follow the same RTSP server pattern
- H.264 decode pipeline is already proven from AirPlay (Phase 4) — reuse the same decoder chain
- MS-MICE uses MPEG-TS container over RTP which is well-supported by GStreamer (tsdemux)
- Windows "Connect" app discovers receivers via mDNS _display._tcp — straightforward with existing AvahiAdvertiser
- HDCP is skipped for v1 — Windows will fall back to unencrypted stream when receiver doesn't advertise HDCP support

</specifics>

<deferred>
## Deferred Ideas

- Wi-Fi Direct Miracast (deferred to v2 per CLAUDE.md)
- HDCP content protection (deferred — requires licensed HDCP keys)

</deferred>

---

*Phase: 08-miracast*
*Context gathered: 2026-03-30*
