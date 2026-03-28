# Phase 2: Discovery & Protocol Abstraction - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

mDNS/Bonjour advertisement for AirPlay and Google Cast, UPnP/SSDP advertisement for DLNA, abstract ProtocolHandler interface for future protocol implementations, user-configurable receiver name with persistence, and Windows firewall rule registration. No protocol-specific logic (AirPlay, Cast, etc.) — only the discovery and abstraction layers.

</domain>

<decisions>
## Implementation Decisions

### mDNS/Service Discovery
- **D-01:** Use platform-native mDNS libraries with a thin C++ abstraction layer: Avahi (via libavahi-client) on Linux, dns_sd (built-in) on macOS, Bonjour SDK for Windows
- **D-02:** Advertise AirPlay as `_airplay._tcp.local` with required TXT records (deviceid, features, model, srcvers)
- **D-03:** Advertise Google Cast as `_googlecast._tcp.local` with required TXT records (id, cd, md, fn, rs, st)
- **D-04:** Advertise DLNA via UPnP/SSDP using libupnp (pupnp) — DLNA uses SSDP discovery, not mDNS
- **D-05:** All advertisements use the same user-configurable receiver name

### Protocol Handler Interface
- **D-06:** Define an abstract `ProtocolHandler` base class in `src/protocol/ProtocolHandler.h` with virtual methods: `start()`, `stop()`, `name()`, `isRunning()`
- **D-07:** Each protocol handler will feed decoded media data into the shared `MediaPipeline` via its `appsrc` injection point (from Phase 1 D-05)
- **D-08:** A `ProtocolManager` class owns all registered `ProtocolHandler` instances, starts/stops them as a group, and routes session events

### Receiver Name
- **D-09:** Store receiver name in QSettings (platform-native: registry on Windows, plist on macOS, XDG config on Linux)
- **D-10:** Default name is the system hostname. User can change it via a settings mechanism (exact UI deferred to Phase 3)
- **D-11:** When name changes, all active service advertisements must be re-registered immediately

### Firewall (Windows)
- **D-12:** On Windows, register firewall rules at runtime using the Windows Firewall COM API (INetFwPolicy2) on first launch
- **D-13:** If elevated permissions are unavailable, display a user-friendly prompt explaining which ports need opening
- **D-14:** On Linux and macOS, rely on system defaults (mDNS typically works without firewall changes)

### Claude's Discretion
- Exact TXT record values for AirPlay and Cast advertisements (research will determine current required fields)
- UPnP device description XML structure for DLNA DMR
- Whether to use a ServiceAdvertiser abstraction or separate classes per discovery protocol
- Internal threading model for discovery services

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 1 Foundation Code
- `src/pipeline/MediaPipeline.h` — Pipeline interface with appsrc injection point that protocol handlers will feed into
- `src/pipeline/MediaPipeline.cpp` — Pipeline implementation with init(), start(), stop() lifecycle
- `src/main.cpp` — Application entry point, QGuiApplication setup
- `CMakeLists.txt` — Build system to extend with new source files and dependencies

### Project Research
- `.planning/research/STACK.md` — mDNS library recommendations, pupnp for DLNA
- `.planning/research/ARCHITECTURE.md` — Component boundaries, protocol plugin layer design
- `.planning/research/PITFALLS.md` — mDNS/firewall discovery failures, platform-specific gotchas

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `MediaPipeline` class (`src/pipeline/MediaPipeline.h`) — the target pipeline that protocol handlers will feed data into
- `myairshow` namespace — established in Phase 1, all new code goes here
- CMake build system with find_package pattern — extend for new dependencies (Avahi, pupnp)

### Established Patterns
- C++17, Qt Quick/QML, GStreamer
- Forward declarations in headers, implementations in .cpp
- GTest for testing

### Integration Points
- `MediaPipeline::appsrc` — protocol handlers push decoded frames/audio here
- `CMakeLists.txt` — add new source files and pkg_check_modules for Avahi, pupnp
- `src/main.cpp` — instantiate ProtocolManager and start discovery services

</code_context>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches. Research should investigate current mDNS TXT record requirements for AirPlay and Cast.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 02-discovery-protocol-abstraction*
*Context gathered: 2026-03-28*
