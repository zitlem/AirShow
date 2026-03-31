# Phase 4: AirPlay - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement AirPlay screen mirroring reception from iOS, iPadOS, and macOS devices. This phase creates an `AirPlayHandler` that implements the `ProtocolHandler` interface, embeds UxPlay's RAOP/mirroring core, handles FairPlay authentication, feeds decoded H.264 video and AAC/ALAC audio into the shared GStreamer `MediaPipeline` via `appsrc`, and wires session state to `ConnectionBridge` for HUD display. Discovery is already handled (Phase 2 advertises `_airplay._tcp` and `_raop._tcp`).

</domain>

<decisions>
## Implementation Decisions

### UxPlay Integration
- **D-01:** Embed UxPlay 1.73.x as a Git submodule and extract its core server logic (RAOP server, mirroring handler, FairPlay auth from the `lib/` directory) into a linkable library target
- **D-02:** Create `AirPlayHandler : ProtocolHandler` in `src/protocol/AirPlayHandler.h` that wraps UxPlay's RAOP server and implements `start()`, `stop()`, `setMediaPipeline()`
- **D-03:** UxPlay's GStreamer rendering code is replaced — instead, decoded A/V frames are fed into AirShow's shared `MediaPipeline` via `appsrc` injection (Phase 1 D-05)
- **D-04:** UxPlay's own service advertisement code is bypassed — AirShow's `DiscoveryManager` (Phase 2) already handles `_airplay._tcp` and `_raop._tcp` advertisement

### AirPlay Authentication
- **D-05:** Use UxPlay's built-in FairPlay SRP authentication implementation — no custom crypto needed
- **D-06:** OpenSSL 3.x (already linked in Phase 1) provides the underlying crypto primitives for FairPlay
- **D-07:** libplist (dependency of UxPlay) is added via vcpkg or system package for Apple property list parsing

### Session Lifecycle
- **D-08:** Single-session model for v1 — one AirPlay mirroring session at a time
- **D-09:** When a new device connects while one is active, the existing session is replaced (UxPlay default behavior)
- **D-10:** Session events (connect, disconnect, device name, protocol) are routed to `ConnectionBridge` to update the HUD overlay (Phase 3)
- **D-11:** Clean teardown on disconnect — stop pipeline input, clear connection state, return to idle screen

### A/V Synchronization
- **D-12:** Use GStreamer's RTP-based clock synchronization — `rtpjitterbuffer` and `rtph264depay` handle timestamp-based A/V sync
- **D-13:** GStreamer's pipeline clock is the master sync reference for both video and audio streams
- **D-14:** AirPlay sends H.264 video via RTP and AAC/ALAC audio via a separate RTP stream — both are demuxed and fed into the shared pipeline with their RTP timestamps preserved

### Claude's Discretion
- Exact UxPlay source files to extract vs exclude (renderer, CLI entry point, etc.)
- CMake integration approach for UxPlay as a submodule library target
- Internal threading model for the RAOP server (UxPlay uses its own event loop)
- GStreamer element chain between UxPlay output and `appsrc` injection
- Whether to use `appsrc` for both audio and video or split into separate injection points
- Error handling and reconnection behavior details

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### UxPlay Source
- UxPlay GitHub: https://github.com/FDH2/UxPlay (v1.73.6) — RAOP server, FairPlay auth, mirroring logic
- Focus on `lib/` directory for extractable core logic

### Existing Protocol Infrastructure
- `src/protocol/ProtocolHandler.h` — Abstract interface that AirPlayHandler must implement
- `src/protocol/ProtocolManager.h` — Owns handlers, routes shared pipeline
- `src/protocol/ProtocolManager.cpp` — Handler registration and lifecycle

### Pipeline Integration
- `src/pipeline/MediaPipeline.h` — Shared pipeline with `appsrc` injection point
- `src/pipeline/MediaPipeline.cpp` — Pipeline init/start/stop, mute toggle, decoder detection

### UI Integration
- `src/ui/ReceiverWindow.h` — Window manager, context property exposure
- `qml/main.qml` — QML UI with HudOverlay and IdleScreen

### Discovery (already done)
- `src/discovery/DiscoveryManager.cpp` — Already advertises `_airplay._tcp` and `_raop._tcp` with TXT records on port 7000

### Build System
- `CMakeLists.txt` — Extend with UxPlay submodule, libplist dependency
- `vcpkg.json` — Add libplist if using vcpkg

### Project Research
- `.planning/research/STACK.md` — UxPlay embedding recommendation, dependency chain
- `.planning/research/ARCHITECTURE.md` — Protocol handler data flow, pipeline integration
- `.planning/research/PITFALLS.md` — AV sync requirements, FairPlay gotchas

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ProtocolHandler` interface — `AirPlayHandler` implements this directly
- `ProtocolManager` — registers and manages `AirPlayHandler` lifecycle
- `MediaPipeline` with `appsrc` — target for decoded A/V frame injection
- `DiscoveryManager` — already advertising AirPlay services with correct TXT records
- `ConnectionBridge` — session state display (device name, protocol, connected status)
- `AudioBridge` — mute toggle already wired

### Established Patterns
- C++17, Qt Quick/QML, GStreamer 1.26.x
- Pure virtual `ProtocolHandler` interface with no base state
- Context properties for C++ to QML bridges
- pkg_check_modules for system library detection in CMake

### Integration Points
- `ProtocolManager::addHandler()` in `main.cpp` — register AirPlayHandler
- `MediaPipeline::appsrc` — feed decoded H.264/AAC frames
- `ConnectionBridge::setConnected()` — update HUD on session start/end
- `DiscoveryManager` — may need TXT record updates once real AirPlay auth values are known (currently uses placeholder `pk` field)

</code_context>

<specifics>
## Specific Ideas

- UxPlay's `lib/` subfolder is the extraction target — its RAOP server and FairPlay implementation are the core reusable pieces
- The placeholder `pk` value in DiscoveryManager's AirPlay TXT records (128-char zeros) needs to be replaced with the real public key once FairPlay auth is wired
- MediaPipeline currently uses `videotestsrc` and `audiotestsrc` — this phase replaces those with real protocol data via `appsrc`
- STATE.md flags: "UxPlay lib/ subfolder embedding approach and current RAOP auth handshake need research before planning"

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 04-airplay*
*Context gathered: 2026-03-28*
