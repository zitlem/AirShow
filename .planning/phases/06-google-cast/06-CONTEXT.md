# Phase 6: Google Cast - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement Google Cast receiver: accept screen mirroring from Android devices and tab casting from Chrome browsers. This phase creates a `CastHandler : ProtocolHandler` that implements the CASTV2 protocol over TLS on port 8009, handles Cast session negotiation, receives VP8/VP9+Opus media streams, and feeds them into the shared GStreamer pipeline. Discovery is already handled (Phase 2 advertises `_googlecast._tcp` on port 8009).

</domain>

<decisions>
## Implementation Decisions

### Cast Authentication
- **D-01:** Use a self-signed TLS certificate for the Cast receiver's TLS socket on port 8009. Google Cast requires device certificates signed by Google for "certified" receivers, but this is unavailable for open-source projects.
- **D-02:** Accept that some Android devices may refuse to connect to uncertified receivers. Chrome browser tab casting and older Android versions are more permissive. Document this limitation clearly.
- **D-03:** Structure the auth layer so a real Google certificate can be dropped in later if one becomes available (e.g., via Google's Cast SDK developer program), without changing the protocol implementation.

### CASTV2 Protocol
- **D-04:** Implement the CASTV2 protocol directly using protobuf + OpenSSL TLS, not via openscreen/libcast. Openscreen uses GN+Ninja build system which doesn't integrate with CMake. The CASTV2 protocol is straightforward: length-prefixed protobuf messages over TLS.
- **D-05:** Use `protobuf` (libprotobuf) for CASTV2 message serialization/deserialization. The `.proto` definitions for CastMessage are well-known and small (one message type with namespace, source/destination IDs, payload type, and payload).
- **D-06:** Implement the Cast receiver namespace handlers: `urn:x-cast:com.google.cast.tp.connection` (virtual connection), `urn:x-cast:com.google.cast.tp.heartbeat` (keep-alive PING/PONG), `urn:x-cast:com.google.cast.receiver` (receiver status, launch, stop), and `urn:x-cast:com.google.cast.media` (media control).
- **D-07:** For Cast mirroring (screen cast), implement the `urn:x-cast:com.google.cast.webrtc` namespace to handle WebRTC offer/answer SDP exchange and ICE candidate negotiation.

### Media Pipeline
- **D-08:** Cast screen mirroring uses WebRTC (VP8/VP9 video + Opus audio over SRTP/DTLS). Use GStreamer's RTP depayloading elements (`rtpvp8depay`, `rtpopusdepay`) to decode the media streams into the shared pipeline.
- **D-09:** Cast tab casting from Chrome sends VP8 video + Opus audio via the same WebRTC path. Same pipeline handles both Android screen mirror and Chrome tab cast.
- **D-10:** For Cast media app content (e.g., YouTube URL sent via media namespace), use the `uridecodebin` approach from Phase 5 (DLNA) — the sender provides a media URL that GStreamer fetches and decodes directly.
- **D-11:** Use the existing `autoaudiosink` and `qml6glsink` video sink chain for output, consistent with all other protocols.

### Architecture
- **D-12:** Create `CastHandler : ProtocolHandler` in `src/protocol/CastHandler.h` following the AirPlayHandler/DlnaHandler pattern.
- **D-13:** CastHandler owns the TLS server socket on port 8009, manages CASTV2 session state, and dispatches namespace messages to internal handlers.
- **D-14:** Single-session model consistent with Phase 4 (D-08/D-09): one Cast session at a time; new connection replaces active session.
- **D-15:** Session events routed to ConnectionBridge for HUD display — shows "Cast" as protocol and sender device name.

### Claude's Discretion
- TLS certificate generation approach (runtime self-signed vs bundled)
- Internal threading model for the TLS server (boost::asio, Qt's QTcpServer, or raw sockets)
- Exact protobuf message definitions (copy from public CASTV2 documentation)
- WebRTC SDP negotiation details (ICE-lite vs full ICE)
- GStreamer element chain for VP8/Opus decoding
- Error handling for auth failures and unsupported Cast app types
- Whether to use `webrtcbin` GStreamer element or manual DTLS/SRTP handling

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing Cast Infrastructure
- `src/discovery/DiscoveryManager.cpp` — Already advertises `_googlecast._tcp` on port 8009 with TXT records (id, ve, md, fn, ca, st, rs)

### Protocol Pattern Reference
- `src/protocol/ProtocolHandler.h` — Interface that CastHandler must implement
- `src/protocol/ProtocolManager.h` — Handler registration and lifecycle
- `src/protocol/AirPlayHandler.h` — Reference: TLS-based protocol handler with session lifecycle
- `src/protocol/DlnaHandler.h` — Reference: SOAP dispatch pattern, ConnectionBridge wiring

### Pipeline Integration
- `src/pipeline/MediaPipeline.h` — Shared pipeline; Cast may need new WebRTC pipeline mode
- `src/pipeline/MediaPipeline.cpp` — Existing modes: appsrc (AirPlay), uridecodebin (DLNA)

### UI Integration
- `src/ui/ConnectionBridge.h` — setConnected(deviceName, protocol) for HUD updates

### Build System
- `CMakeLists.txt` — Add CastHandler sources, protobuf dependency

### External References
- CASTV2 protocol: protobuf over TLS on port 8009 (well-documented in open-source Cast implementations)
- Shanocast blog (referenced in CLAUDE.md): authentication bypass approach for uncertified receivers
- Chromecast device authentication blog by Tristan Penman (referenced in CLAUDE.md): certificate chain requirements

### Project Research
- `.planning/research/STACK.md` — openscreen/libcast recommendations, protobuf version requirements
- `.planning/research/ARCHITECTURE.md` — Protocol handler data flow
- `.planning/research/PITFALLS.md` — Cast auth legal assessment note

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ProtocolHandler` interface — CastHandler implements this directly
- `ProtocolManager` — registers CastHandler, provides shared pipeline
- `ConnectionBridge` — HUD session state display
- `MediaPipeline` with appsrc and uridecodebin modes — may need new WebRTC mode
- `DiscoveryManager` — already advertising `_googlecast._tcp` with correct TXT records
- OpenSSL already linked (Phase 1) — use for TLS server socket

### Established Patterns
- C++17, Qt Quick/QML, GStreamer 1.26.x
- Pure virtual ProtocolHandler with no base state
- File-scope C trampolines for C callback APIs
- QMetaObject::invokeMethod for cross-thread GStreamer calls (from Phase 5 DlnaHandler)

### Integration Points
- `ProtocolManager::addHandler()` in `main.cpp` — register CastHandler
- `ConnectionBridge::setConnected()` — update HUD on Cast session start/end
- `MediaPipeline` — needs new mode for WebRTC RTP streams (VP8/Opus)
- Port 8009 — TLS server socket for CASTV2 protocol

</code_context>

<specifics>
## Specific Ideas

- CLAUDE.md flags Google Cast as "MEDIUM confidence, legally/technically constrained" — the certificate issue is the primary risk
- STATE.md notes: "openscreen CMake integration and Cast auth legal assessment need deep research before planning"
- The Shanocast blog documents a working approach to Cast receiver without Google certificates
- Chrome browser tab casting is likely more permissive than Android screen casting for auth
- protobuf version must match what CASTV2 uses — check openscreen's DEPS for exact version

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 06-google-cast*
*Context gathered: 2026-03-29*
