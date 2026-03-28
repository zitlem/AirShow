# Project Research Summary

**Project:** MyAirShow
**Domain:** Cross-platform wireless display receiver (AirPlay + Google Cast + Miracast + DLNA)
**Researched:** 2026-03-28
**Confidence:** MEDIUM — AirPlay and DLNA are HIGH confidence; Google Cast and Miracast carry significant technical and legal constraints that reduce overall confidence to MEDIUM.

## Executive Summary

MyAirShow is a cross-platform screen mirroring receiver targeting the unoccupied market position of a free, open-source application that handles all four major wireless display protocols (AirPlay, Google Cast, Miracast, DLNA) on Linux, macOS, and Windows. No existing free tool covers all four protocols; the commercial alternatives (AirServer, Reflector 4) cost $15–20+ per device and exclude Linux entirely. The recommended implementation approach is C++17 with Qt 6.8 LTS for the UI, GStreamer 1.26.x for cross-platform media decoding, and protocol-specific open-source libraries embedded per protocol (UxPlay for AirPlay, openscreen/pupnp for Cast/DLNA). All media paths converge on a shared GStreamer pipeline via `appsrc` injection — this is the key architectural decision that makes multi-protocol support tractable.

The recommended build strategy is to ship AirPlay + DLNA first (both are HIGH confidence and well-documented), add Google Cast in a second phase, and treat Miracast as a later milestone. Google Cast's device authentication requires a Google-signed certificate chain that independent receivers cannot obtain legitimately — the only viable workarounds are legally grey. Miracast's standard implementation (Wi-Fi Direct) is practically broken on desktop Linux and kills the host machine's internet connection, making Miracast over Infrastructure (MS-MICE, Windows-only) the only sensible v1 path. These two protocols must be scoped carefully to avoid blocking the rest of the product.

The highest-risk pitfall is attempting to build all four protocols before any one is end-to-end proven. The architecture research is unambiguous: establish the GStreamer pipeline and receiver window first, prove AirPlay end-to-end, then extend to other protocols. A clean protocol abstraction layer (a `ProtocolHandler` interface implemented before any protocol code is written) is the single most important structural decision — retrofitting this after two protocols are shipped is a multi-week refactor. Secondary risks are mDNS/firewall discovery failures (the number-one user-facing support issue for all receiver apps) and audio/video sync drift from incorrect AirPlay NTP clock handling.

---

## Key Findings

### Recommended Stack

The core stack is C++17 + Qt 6.8 LTS + GStreamer 1.26.x + OpenSSL 3.x, built with CMake 3.28+ and Ninja. This mirrors what AirServer itself uses (confirmed: AirServer is built with Qt Core, Quick, Network, Multimedia). Qt 6.8 is the current LTS release (support through 2029) and is the correct choice over Qt 5 (commercial LTS ended) or Electron (cannot inject raw A/V frames). GStreamer is the only cross-platform pipeline library that provides hardware-accelerated H.264/H.265 decode (VAAPI on Linux, VideoToolbox on macOS, D3D11 on Windows) from a single API surface. The `qml6glsink` element is the correct integration pattern for rendering GStreamer video into a Qt QML window — `QMediaPlayer` cannot accept raw encoded frames from protocol callbacks.

Protocol libraries per protocol: UxPlay 1.73.6 (AirPlay — GPL, actively maintained, cross-platform), openscreen/libcast (Google Cast streaming layer — authentication unsolved), pupnp/libupnp 1.14.x (DLNA/UPnP DMR), and no viable cross-platform library for Miracast. Service discovery uses Avahi on Linux, native Bonjour on macOS, and bundled Bonjour SDK on Windows. openscreen uses the Chromium build toolchain (GN + Ninja), not CMake — it requires a separate build step to produce static libs that a CMake project can link.

**Core technologies:**
- **C++17**: Implementation language — all reference libraries (UxPlay, pupnp, openscreen, OpenSSL) are C/C++; any other language adds FFI friction
- **Qt 6.8 LTS**: GUI + windowing — used by commercial AirServer; LTS until 2029; native on Linux/macOS/Windows; QML + qml6glsink for GStreamer video rendering
- **GStreamer 1.26.x**: Media pipeline — the only cross-platform library enabling hardware-accelerated decode + protocol-agnostic `appsrc` injection
- **OpenSSL 3.x**: Crypto — required for AirPlay FairPlay auth, Cast TLS; OpenSSL 1.1.1 is EOL since September 2023
- **UxPlay 1.73.6**: AirPlay protocol — embed the `lib/` subfolder (raop, mdns, playfair), not the top-level application binary
- **pupnp/libupnp 1.14.x**: DLNA/UPnP — actively maintained; Platinum UPnP SDK is dead (last release July 2020)
- **openscreen (libcast)**: Google Cast streaming layer — use for the streaming module; authentication remains the unsolved problem
- **CMake 3.28+ + vcpkg**: Build system — manifest mode (`vcpkg.json`) for reproducible cross-platform builds
- **protobuf 3.x**: Google Cast CASTV2 protocol framing — pin version to match openscreen's DEPS

### Expected Features

AirPlay and Google Cast reception are equal P1 priorities — they cover Apple users and Android/Chrome users respectively, the two dominant sender ecosystems. mDNS/Bonjour advertisement is a hard prerequisite for both; nothing works without it. Audio playback with correct A/V sync is a P1 requirement, not a nice-to-have. Miracast and DLNA are P2, not blocking launch. Multi-device simultaneous display is P3 (complex tiling/compositor layer; defer until the single-stream experience is validated). Recording and sender mode are explicit out-of-scope anti-features — document them clearly to deflect support noise.

The combination of (AirPlay + Cast + Miracast + DLNA) + Linux + free + open source is unoccupied by any existing product. This is the differentiator and the positioning must be clear from launch.

**Must have (table stakes):**
- AirPlay screen mirroring reception (iOS/macOS) — the highest-volume use case; UxPlay provides the implementation foundation
- Google Cast screen mirroring reception (Android/Chrome) — covers non-Apple users; authentication is the constraint
- mDNS/Bonjour advertisement — prerequisite for any protocol discovery; without this nothing is visible
- Audio playback with A/V sync and mute toggle — silent or drifting audio is a fatal usability bug
- Fullscreen receiver window with connection status HUD — the core display experience
- Cross-platform: Linux, macOS, Windows — the product's core promise
- Customizable receiver name — needed so users can identify this receiver in multi-device environments

**Should have (competitive):**
- Miracast reception (Miracast over Infrastructure on Windows first; Wi-Fi Direct deferred) — adds Windows/Android coverage
- DLNA media push (DMR) — covers smart-TV "push video" use case; architecturally simpler than live mirroring
- Connection approval prompt (allow/deny) — required for professional/shared-space deployments
- PIN-based pairing — security layer on top of approval; important for classroom and office use

**Defer (v2+):**
- Multi-device simultaneous display (picture-in-picture / tiling) — high complexity; validate single-stream use case first
- Annotation/drawing overlay — only if education market is confirmed as primary audience
- Remote/internet mirroring — requires relay infrastructure; contradicts the local-network-only positioning

### Architecture Approach

The architecture is a layered system: a Discovery/Advertisement layer (mDNS + SSDP, shared across protocols) feeds into independent Protocol Handler modules (one per protocol), each of which implements a common `ProtocolHandler` interface. All handlers push encoded video/audio bytes with PTS timestamps into a shared GStreamer pipeline via `appsrc` elements. The Session Manager arbitrates between protocols — only one session renders at a time in v1. The UI layer creates the window and passes the native window handle to GStreamer's video sink; it never imports protocol-specific types. This design is the direct conclusion of how UxPlay, RPiPlay, and shanocast are structured, and is the pattern the architecture research recommends explicitly.

**Major components:**
1. **Discovery / Advertisement Layer** — mDNS (Avahi/Bonjour) + SSDP multicast; shared service, not owned by any protocol handler; advertises all service records simultaneously
2. **Protocol Handler Module (per protocol)** — implements `advertise()`, `start_session()`, `stop_session()` interface; owns protocol-specific networking (RTSP, TLS, UPnP); delivers encoded bytes + PTS to `appsrc`
3. **Shared GStreamer Media Pipeline** — single pipeline with `appsrc` injection points for video and audio; manages decode (H.264/H.265/AAC/ALAC), hardware acceleration, A/V sync via shared clock, and sink output
4. **Session Manager** — state machine (Idle → Connecting → Active → Disconnecting → Idle); starts/stops pipeline; rejects or queues concurrent connection attempts
5. **Receiver Window / UI** — Qt QML fullscreen window with `qml6glsink` video surface and minimal HUD (protocol name, device name, connection status); never references protocol internals
6. **Configuration / Persistence** — TOML/JSON key-value store for device name, audio device, display preferences; read at startup, no global singleton

### Critical Pitfalls

1. **No protocol abstraction before first protocol ships** — If AirPlay is implemented directly against the UI layer, adding Cast requires a multi-week refactor. Define `ProtocolHandler` and `MirroringSession` interfaces before writing any protocol code. Every protocol must satisfy these interfaces from day one.

2. **mDNS/firewall discovery failure silently makes the receiver invisible** — The single most common user-facing issue for all receiver apps. The receiver simply does not appear in the sender's menu with no error. Register Windows Firewall rules programmatically at install time; check Avahi daemon status at startup on Linux; show a persistent "visible as [Name] on network" indicator that turns red with a diagnostic link when mDNS is not working.

3. **Audio/video sync drift from missing NTP implementation** — AirPlay mandates a 2-second audio latency and uses NTP on port 7010 as the A/V sync reference. Using system time instead of AirPlay NTP produces permanent desync. Use a single GStreamer pipeline clock shared between audio and video branches; implement AirPlay NTP reply correctly from day one; never set `sync=false` on the video sink in production.

4. **Apple protocol fragility from iOS updates** — Apple changes AirPlay authentication flows in security patches without notice; third-party receivers break silently. Monitor shairport-sync and UxPlay GitHub issue trackers as an early-warning system. Isolate the AirPlay protocol layer behind the `ProtocolHandler` interface so pairing code can be patched independently. The AirBorne vulnerability family (23 CVEs, 2025) confirms that skipping authentication steps to simplify implementation creates real security exposure.

5. **Google Cast certificate authentication is an unresolved legal/technical problem** — Cast requires a Google-issued device certificate chain. Open-source receivers use pre-computed challenge signatures extracted from APKs (legally grey). Design the Cast module as a plugin with a swappable authentication backend so this can be addressed as the situation evolves without touching the rest of the architecture.

---

## Implications for Roadmap

Based on combined research, the architecture's own suggested build order maps directly to phases. The pipeline and window must exist before any protocol can be validated. AirPlay is the best-documented protocol and the highest-traffic use case — it proves the full end-to-end path. Session Manager and protocol abstraction must be in place before the second protocol is added. DLNA is the simplest protocol (pull model, no encryption, mature library) and is a good second milestone. Cast is the next highest value but carries the certificate constraint. Miracast is last due to OS-level complexity.

### Phase 1: Foundation — Build System, Pipeline, and Receiver Window

**Rationale:** The GStreamer pipeline and Qt receiver window are the integration target for every protocol. Without them, no protocol can be validated end-to-end. This phase de-risks the core render path before any networking work begins.
**Delivers:** A fullscreen Qt QML window that renders a GStreamer test source (video + audio); hardware decoder detection and logging; `appsrc` injection point confirmed working; build system with CMake + vcpkg producing binaries on Linux, macOS, and Windows.
**Addresses:** Cross-platform build requirement; hardware decoder fallback (Pitfall 5); `qml6glsink` integration pattern confirmed.
**Avoids:** The pitfall of building protocol code before knowing the render path works; hardcoded GStreamer pipeline strings that break on systems without vaapi.
**Research flag:** Standard patterns — GStreamer + Qt integration is well-documented; no additional research needed.

### Phase 2: Discovery Layer and Protocol Abstraction Interfaces

**Rationale:** mDNS/SSDP advertisement is a prerequisite for all protocols to be discoverable. The protocol abstraction interfaces must exist before any protocol is implemented — retrofitting them after AirPlay is shipped is the single most expensive architectural mistake identified in research.
**Delivers:** `ProtocolHandler` and `MirroringSession` interfaces defined; mDNS advertisement working on all three platforms (Avahi on Linux, native Bonjour on macOS, Bonjour SDK on Windows); SSDP multicast working; receiver appears in AirPlay and Cast menus on test devices; firewall rule registration at install time (Windows); diagnostic "visible on network" status indicator in UI.
**Addresses:** mDNS discovery failure (Pitfall 3); protocol abstraction missing (Pitfall 7); receiver name customization; firewall port registration.
**Avoids:** Coupling discovery code inside protocol handlers (anti-pattern 3 from architecture research); silently invisible receiver on fresh installs.
**Research flag:** Needs research for Windows Bonjour SDK bundling approach and Windows Firewall API integration details.

### Phase 3: AirPlay Protocol Handler

**Rationale:** AirPlay is the highest-traffic use case, the most thoroughly reverse-engineered protocol, and has the most mature open-source reference (UxPlay 1.73.6). It proves the complete path: discovery → authentication → RTP decode → GStreamer → display. All subsequent protocols follow the same shape.
**Delivers:** iOS and macOS devices can mirror their screen to MyAirShow; audio and video in sync (AirPlay NTP implemented correctly); AES-CTR decryption working; DRM refusal produces a user-visible error message; Session Manager state machine in place.
**Addresses:** AirPlay reception (P1 table stakes); audio playback with A/V sync; DRM content handling (Pitfall 6); AirPlay protocol fragility isolation (Pitfall 1); AirBorne vulnerability family (authentication not skipped).
**Implements:** AirPlay Protocol Handler satisfying `ProtocolHandler` interface; Session Manager; shared GStreamer clock for A/V sync.
**Avoids:** Implementing AirPlay directly against the UI layer; `sync=false` shortcut; skipping NTP implementation.
**Research flag:** Needs research during planning — UxPlay embedding approach (lib/ subfolder extraction vs. submodule), exact RAOP authentication handshake steps for current iOS version.

### Phase 4: DLNA/UPnP Media Push (DMR)

**Rationale:** DLNA is the simplest protocol (pull model, no encryption, mature pupnp library) and adds meaningful coverage for smart-TV users without the complexity of Cast or Miracast. Adding it second validates the protocol abstraction layer with a very different protocol shape (HTTP pull vs. RTP push).
**Delivers:** DLNA Digital Media Renderer visible to DLNA controller apps; media URL received via UPnP AVTransport; GStreamer `filesrc`/`souphttpsrc` fetches and plays the media; confirms `ProtocolHandler` interface is genuinely protocol-agnostic.
**Addresses:** DLNA media push reception (P2); SSDP binding to RFC1918 addresses only (CallStranger CVE-2020-12695 security mitigation).
**Avoids:** SSDP bound to all interfaces including internet-routed VPN interfaces; Platinum UPnP SDK (dead — use pupnp).
**Research flag:** Standard patterns — pupnp/libupnp is well-documented; no additional research needed.

### Phase 5: Google Cast Protocol Handler

**Rationale:** Cast is the second highest-value protocol and covers the entire Android and Chrome ecosystem. It must come after AirPlay is stable because the authentication problem requires a deliberate pluggable design. The Cast streaming layer (openscreen) is usable even if the authentication backend is initially the legally-grey workaround.
**Delivers:** Android and Chrome browser can cast to MyAirShow; TLS on port 8009 working; protobuf CASTV2 framing; Cast streaming session via openscreen; authentication backend is swappable without touching the rest of the handler.
**Addresses:** Google Cast reception (P1 table stakes); Cast certificate authentication constraint (designed as a plugin, not hardcoded).
**Avoids:** Building Cast's authentication as an inseparable part of the handler; openscreen GN/Ninja build toolchain not integrated with CMake project.
**Research flag:** Needs deep research — openscreen integration with a CMake project (separate build step + static lib linking), exact protobuf version pinning, authentication workaround options and legal assessment.

### Phase 6: Security, UX Polish, and v1 Hardening

**Rationale:** After two-to-three protocols are working, the user-facing security and UX gaps become the primary quality driver before any public release. The AirBorne vulnerability family shows that security cannot be deferred.
**Delivers:** Connection approval prompt (allow/deny on incoming connections); PIN-based pairing for AirPlay; pairing tokens stored in OS keychain (not plaintext); receiver bound to LAN interfaces only (not all interfaces including VPN); "Press ESC to exit mirroring" corner hint; automatic reconnection after brief network interruption; long-session stability validated (30-minute A/V drift test).
**Addresses:** Connection approval (P2); PIN pairing (P2); AirBorne CVEs (security); UX pitfalls from PITFALLS.md (black window before first frame, no ESC hint, no audio confirmation).
**Avoids:** Accepting connections from any IP without pairing; storing tokens in plaintext; binding to 0.0.0.0.
**Research flag:** Standard patterns for OS keychain APIs (Keychain on macOS, DPAPI on Windows, libsecret on Linux).

### Phase 7: Miracast (Windows — Miracast over Infrastructure First)

**Rationale:** Miracast is the most complex protocol and is architecturally isolated from all others (Wi-Fi Direct vs. standard network). Wi-Fi Direct Miracast is explicitly deferred — MiracleCast is stalled, and standard Miracast kills the host's internet connection. Miracast over Infrastructure (MS-MICE) on Windows is the achievable v1 path.
**Delivers:** Windows 10/11 sources can Miracast to MyAirShow over the existing LAN (no Wi-Fi Direct); RTSP capability exchange; RTP/H.264 stream decoded by existing GStreamer pipeline.
**Addresses:** Miracast reception (P2 on Windows); Miracast Wi-Fi adapter conflict documented (Pitfall 2); MS-MICE protocol (publicly documented by Microsoft).
**Avoids:** Wi-Fi Direct Miracast on Linux (MiracleCast is stalled); standard Miracast as the default (kills internet).
**Research flag:** Needs deep research — MS-MICE protocol implementation details, Windows WFD API surface, wpa_supplicant P2P interface status for future Linux work.

### Phase Ordering Rationale

- **Pipeline before protocols:** Every protocol converges at the GStreamer `appsrc` boundary. Building pipeline first means each protocol can be validated immediately on completion rather than waiting for infrastructure.
- **Abstraction before AirPlay, not after:** The pitfall research is explicit — "the single most important structural decision" is the protocol interface, and it costs a multi-week refactor if deferred. Two sprints defining interfaces upfront prevents this.
- **AirPlay before Cast:** UxPlay is more mature than openscreen for Cast, AirPlay has cleaner documentation, and the authentication problem in Cast means it needs more design time. AirPlay proves the path; Cast follows the same shape with a known-hard problem.
- **DLNA before Cast:** DLNA is simpler and tests the protocol abstraction with a fundamentally different protocol model (pull vs. push) before tackling Cast's complexity.
- **Miracast last:** OS-level Wi-Fi Direct complexity is orthogonal to everything else. Implementing it last means the rest of the product is proven and stable before tackling the hardest protocol.

### Research Flags

Phases likely needing `/gsd:research-phase` during planning:
- **Phase 2 (Discovery Layer):** Windows Bonjour SDK bundling, Windows Firewall API for programmatic rule registration at install time.
- **Phase 3 (AirPlay):** UxPlay lib/ subfolder extraction approach for embedding; current RAOP authentication handshake compatibility with latest iOS; shairport-sync vs. UxPlay as the better base.
- **Phase 5 (Google Cast):** openscreen GN+Ninja build integration with a CMake project; protobuf version pinning against openscreen DEPS; legal assessment of authentication workaround approaches.
- **Phase 7 (Miracast):** MS-MICE protocol implementation details from Microsoft spec; Windows WFD API surface for Miracast over Infrastructure; current wpa_supplicant P2P API status for future Linux Miracast.

Phases with standard patterns (skip research-phase):
- **Phase 1 (Foundation):** GStreamer + Qt qml6glsink integration is well-documented with official examples.
- **Phase 4 (DLNA):** pupnp/libupnp is documented and its UPnP AVTransport implementation is straightforward.
- **Phase 6 (Security/UX):** OS keychain APIs are well-documented on all three platforms.

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Core stack (C++/Qt/GStreamer/OpenSSL/CMake) verified against commercial reference (AirServer) and active open-source projects; UxPlay and pupnp dependencies well-understood. openscreen build toolchain integration is MEDIUM — it does not use CMake. |
| Features | MEDIUM-HIGH | Competitor feature matrix verified against official product pages. Protocol feature boundaries (AirPlay vs AirPlay 2, DLNA push vs. live mirroring) confirmed against specs. Cast authentication constraints confirmed HIGH by multiple independent sources. |
| Architecture | HIGH | Architecture research is grounded in actual open-source implementations (UxPlay, RPiPlay, shanocast, MiracleCast). Component boundaries, data flow, and anti-patterns all derive from observed behavior of production code, not speculation. |
| Pitfalls | HIGH (critical) / MEDIUM (moderate) | Critical pitfalls (Apple fragility, mDNS firewall, AV sync, Cast auth) are HIGH confidence from multiple independent sources including CVE disclosures. Miracast and DLNA edge cases are MEDIUM — sparse recent sources. |

**Overall confidence:** MEDIUM-HIGH

### Gaps to Address

- **Google Cast authentication:** The legally/technically correct path for an open-source Cast receiver is unresolved. The openscreen demo and shanocast workaround exist, but neither is a clean production solution. This must be explicitly investigated in Phase 5 research and the legal risk assessed before shipping. Design the authentication backend as a plugin from day one so it can be swapped.

- **UxPlay embedding boundary:** UxPlay is structured as an application, not a library. The correct extraction of the `lib/` subfolder (raop, mdns, playfair) for embedding in a larger application is not fully documented. Phase 3 research must resolve exactly which files to embed and which callbacks to hook.

- **Miracast over Infrastructure implementation complexity:** MS-MICE is publicly documented but no open-source implementation was identified during research. The implementation effort is unknown. Phase 7 research must assess whether an MS-MICE implementation is feasible within a reasonable timeframe or whether Windows Miracast should be deferred to v2.

- **OpenSSL + GStreamer + Qt linkage on Windows:** On Windows via MSYS2/MinGW-64, mixing OpenSSL 3.x (required by UxPlay) with GStreamer's own OpenSSL linkage can produce symbol conflicts. This needs validation during Phase 1 build system setup.

- **Hardware decoder test matrix:** The research identifies the decoder matrix (vaapi, nvdec, v4l2, VideoToolbox, D3D11, avdec fallback) but does not confirm which GStreamer plugin names to use for explicit decoder selection (vs. `decodebin` auto-selection). Phase 1 pipeline work must produce an explicit decoder selection strategy with tested fallback.

---

## Sources

### Primary (HIGH confidence)
- [UxPlay GitHub (FDH2/UxPlay)](https://github.com/FDH2/UxPlay) — AirPlay protocol implementation, dependency chain, platform support, version 1.73.6
- [GStreamer Releases](https://gstreamer.freedesktop.org/releases/) — 1.26.x current stable, qml6glsink integration pattern
- [Qt 6.8 LTS Release Blog](https://www.qt.io/blog/qt-6.8-released) — LTS status, platform support
- [AirServer Built with Qt](https://www.qt.io/development/airserver-universal-screen-mirroring-receiver-built-with-qt) — Commercial reference stack confirmation
- [Chromecast Device Authentication (Penman, 2025-03-22)](https://tristanpenman.com/blog/posts/2025/03/22/chromecast-device-authentication/) — Cast certificate chain requirement
- [AirBorne: 23 AirPlay CVEs (Oligo Security, 2025)](https://www.oligo.security/blog/critical-vulnerabilities-in-airplay-protocol-affecting-multiple-apple-devices) — Security pitfalls, authentication requirements
- [MS-MICE Miracast over Infrastructure (Microsoft Docs)](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/9598ca72-d937-466c-95f6-70401bb10bdb) — Miracast over Infrastructure protocol spec
- [FairPlay not licensed for third-party platforms (Apple Developer)](https://developer.apple.com/streaming/fps/) — DRM limitation confirmed
- [OpenSSL 1.1.1 EOL](https://www.openssl.org/blog/blog/2023/09/11/eol-111/) — OpenSSL version requirement
- [MiracleCast stalled (maintainer statement)](https://github.com/albfan/miraclecast) — Miracast P2P API blocker

### Secondary (MEDIUM confidence)
- [Shanocast / Making a Chromecast receiver](https://xakcop.com/post/shanocast/) — Cast authentication workaround approach
- [openscreen libcast README](https://chromium.googlesource.com/openscreen/+/HEAD/cast/README.md) — Cast streaming module capabilities
- [pupnp/libupnp GitHub](https://github.com/pupnp/pupnp) — Active maintenance status, API surface
- [AirPlay protocol specification (openairplay.github.io)](https://openairplay.github.io/airplay-spec/) — Community reverse-engineered spec
- [AirPlay 2 internals (emanuelecozzi.net)](https://emanuelecozzi.net/docs/airplay2) — AirPlay 2 vs AirPlay 1 distinction
- [Cross-platform receiver developer guide (brightcoding, Dec 2025)](https://www.blog.brightcoding.dev/2025/12/31/the-ultimate-developer-guide-building-cross-platform-wireless-display-solutions-with-airplay-miracast-google-cast-sdks/) — Architecture patterns validation
- [AirServer feature list](https://www.airserver.com/Overview) — Competitor feature matrix
- [Reflector 4 features](https://www.airsquirrels.com/reflector/features/mac-and-windows) — Competitor feature matrix

### Tertiary (LOW confidence / needs validation)
- DLNA incompatibility edge cases — sparse recent sources; test against real devices during Phase 4
- Miracast over Infrastructure implementation effort — no open-source reference found; must be assessed in Phase 7 research
- Linux wpa_supplicant P2P API future status — stated as blocked by MiracleCast maintainer; no authoritative timeline for resolution

---
*Research completed: 2026-03-28*
*Ready for roadmap: yes*
