# Phase 5: DLNA - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement DLNA Digital Media Renderer (DMR) playback: handle AVTransport and RenderingControl SOAP actions so DLNA controller apps (BubbleUPnP, foobar2000, etc.) can push video and audio files to AirShow for playback. SSDP advertisement is already done (Phase 2). This phase replaces the stub 501 SOAP callback with real media playback logic.

</domain>

<decisions>
## Implementation Decisions

### Architecture
- **D-01:** Create `DlnaHandler : ProtocolHandler` in `src/protocol/DlnaHandler.h` — follows established AirPlayHandler pattern. Owns SOAP action dispatch, GStreamer pipeline lifecycle for DLNA sessions, and session state.
- **D-02:** `UpnpAdvertiser` stays focused on SSDP discovery. Route SOAP action events from `UpnpAdvertiser::upnpCallback` to `DlnaHandler` for processing (replace the current 501 stub).
- **D-03:** `DlnaHandler` is registered with `ProtocolManager` via `addHandler()` like AirPlayHandler.

### Media Playback
- **D-04:** Use GStreamer `uridecodebin` for DLNA media playback. DLNA controllers send a media URI via SetAVTransportURI; `uridecodebin` handles HTTP fetch, container demux, format detection, and codec selection automatically.
- **D-05:** `uridecodebin` replaces `appsrc` for DLNA — unlike AirPlay (which pushes raw frames), DLNA provides a URL that GStreamer fetches directly. The pipeline switches from appsrc-based to URI-based when a DLNA session is active.
- **D-06:** Use the same `autoaudiosink` and video sink chain from the shared pipeline for output.

### Supported Formats
- **D-07:** Advertise broad format support in SinkProtocolInfo — `uridecodebin` handles format detection via GStreamer's plugin registry, so no extra code per format:
  - Video: `video/mp4`, `video/mpeg`, `video/x-matroska`, `video/avi`
  - Audio: `audio/mpeg` (MP3), `audio/mp4` (AAC), `audio/flac`, `audio/wav`, `audio/x-ms-wma`
  - Transport: `http-get:*:mime:*` DLNA protocol info format

### AVTransport Implementation
- **D-08:** Implement full AVTransport:1 action set:
  - `SetAVTransportURI` — receive media URL, prepare pipeline
  - `Play` — start/resume playback
  - `Stop` — stop playback, tear down pipeline
  - `Pause` — pause playback
  - `Seek` — seek to position (REL_TIME mode)
  - `GetTransportInfo` — return current transport state (PLAYING/PAUSED/STOPPED)
  - `GetPositionInfo` — return current position and track duration
  - `GetMediaInfo` — return current media URI and metadata
- **D-09:** Implement RenderingControl:1 actions:
  - `SetVolume` / `GetVolume` — map to GStreamer volume element
  - `SetMute` / `GetMute` — map to existing AudioBridge mute toggle
- **D-10:** Implement ConnectionManager:1 actions:
  - `GetProtocolInfo` — return SinkProtocolInfo with supported MIME types

### SCPD Service Descriptions
- **D-11:** Create SCPD XML files for each UPnP service: `avt-scpd.xml`, `rc-scpd.xml`, `cm-scpd.xml`. These define the actions and state variables each service supports. Serve via libupnp's built-in HTTP server.

### Session Lifecycle
- **D-12:** Single-session model consistent with Phase 4 (D-08/D-09): one DLNA playback at a time; new SetAVTransportURI replaces current playback.
- **D-13:** Session events (connect with device info, disconnect) routed to `ConnectionBridge` for HUD display — shows "DLNA" as protocol and controller name if available from metadata.
- **D-14:** Clean teardown: Stop pipeline, clear connection state, return to idle screen on Stop or controller disconnect.

### Claude's Discretion
- Internal threading model for SOAP action handling (libupnp callback thread vs Qt event loop)
- Exact GStreamer pipeline element chain for uridecodebin output
- SCPD XML content details (state variable defaults, allowed value ranges)
- UPnP eventing implementation (GENA subscription for LastChange notifications)
- Error handling for unsupported codecs or unreachable media URLs
- Whether to use playbin vs custom uridecodebin pipeline

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing DLNA Infrastructure
- `src/discovery/UpnpAdvertiser.h` — SSDP advertiser with stub SOAP callback to be extended
- `src/discovery/UpnpAdvertiser.cpp` — libupnp init, device registration, 501 stub callback (line 105-116)
- `resources/dlna/MediaRenderer.xml` — Device description XML with AVTransport, RenderingControl, ConnectionManager service declarations

### Protocol Pattern Reference
- `src/protocol/ProtocolHandler.h` — Interface that DlnaHandler must implement
- `src/protocol/ProtocolManager.h` — Handler registration and lifecycle
- `src/protocol/AirPlayHandler.h` — Reference implementation showing ProtocolHandler pattern with ConnectionBridge and pipeline integration
- `src/protocol/AirPlayHandler.cpp` — Implementation details: session lifecycle, frame injection, HUD updates

### Pipeline Integration
- `src/pipeline/MediaPipeline.h` — Shared pipeline; DLNA will use uridecodebin instead of appsrc
- `src/pipeline/MediaPipeline.cpp` — Pipeline init/start/stop, audio sink, video sink chain

### UI Integration
- `src/ui/ConnectionBridge.h` — setConnected(deviceName, protocol) for HUD updates

### Build System
- `CMakeLists.txt` — Add new DlnaHandler sources; libupnp already linked

### Project Research
- `.planning/research/STACK.md` — pupnp/libupnp recommendations
- `.planning/research/ARCHITECTURE.md` — Protocol handler data flow
- `.planning/research/PITFALLS.md` — VPN interface binding issue (UpnpAdvertiser line 31 comment)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `UpnpAdvertiser` — SSDP advertisement already working; SOAP callback needs routing to DlnaHandler
- `MediaRenderer.xml` — Device description already declares all three required UPnP services
- `ProtocolHandler` interface — DlnaHandler implements this directly
- `ProtocolManager` — registers DlnaHandler, provides shared pipeline
- `ConnectionBridge` — HUD session state display (device name + protocol)
- `AudioBridge` — mute toggle already wired; RenderingControl SetMute/GetMute can delegate here
- `MediaPipeline` — shared pipeline with video/audio sink chain

### Established Patterns
- C++17, Qt Quick/QML, GStreamer 1.26.x
- Pure virtual ProtocolHandler with no base state
- File-scope C trampolines for C callback APIs (used in AirPlayHandler for UxPlay callbacks; same pattern applies for libupnp SOAP callbacks)
- Context properties for C++ to QML bridges
- pkg_check_modules for libupnp already in CMakeLists.txt

### Integration Points
- `UpnpAdvertiser::upnpCallback` — currently returns 501; must route UPNP_CONTROL_ACTION_REQUEST to DlnaHandler
- `ProtocolManager::addHandler()` in `main.cpp` — register DlnaHandler
- `ConnectionBridge::setConnected()` — update HUD on DLNA session start/end
- `MediaPipeline` — may need a new mode or method for URI-based playback vs appsrc-based

</code_context>

<specifics>
## Specific Ideas

- The UpnpAdvertiser SOAP callback (line 105-116) currently returns 501 for all actions — Phase 5 replaces this with real dispatch logic
- MediaRenderer.xml already references `/avt-scpd.xml`, `/rc-scpd.xml`, `/cm-scpd.xml` SCPD URLs — these files must be created and served
- The libupnp built-in HTTP server (started by UpnpInit2) can serve the SCPD files from the same temp directory or via virtual directory callbacks
- DLNA is a "pull" model unlike AirPlay's "push" — the controller sends a URL and the renderer fetches it, which is why uridecodebin is the right approach instead of appsrc

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 05-dlna*
*Context gathered: 2026-03-28*
