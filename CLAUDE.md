<!-- GSD:project-start source:PROJECT.md -->
## Project

**AirShow**

A free, open-source, cross-platform screen mirroring receiver — an AirServer alternative that costs nothing. It turns any computer (Linux, macOS, Windows) into a wireless display that accepts screen mirrors from phones, tablets, and other computers via AirPlay, Google Cast, Miracast, and DLNA.

**Core Value:** Any device can mirror its screen to any computer, for free — no licenses, no subscriptions, no paywalls.

### Constraints

- **Cost**: Must be completely free — no freemium, no ads, no license keys
- **Cross-platform**: Must work on Linux, macOS, and Windows from the same codebase
- **Network**: Local network only (mDNS/Bonjour discovery, no internet required)
- **License**: Open source (specific license TBD)
<!-- GSD:project-end -->

<!-- GSD:stack-start source:research/STACK.md -->
## Technology Stack

## Recommended Stack
### Core Technologies
| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| **C++17** | C++17 standard | Implementation language | All serious protocol libraries in this space (UxPlay, libupnp, OpenSSL) are C/C++. Wrapping them in any other language adds friction. Qt, GStreamer, and OpenSSL all have first-class C++ APIs. The commercial reference (AirServer) is C++ + Qt. |
| **Qt 6.8 LTS** | 6.8.x (LTS until 2029-10-08) | GUI framework + UI rendering | AirServer itself is built with Qt (Qt Core, Quick, Network, Quick Layouts, Multimedia). Qt 6.8 LTS is the right choice for a greenfield project — avoids the LGPL commercial issue of Qt 5, has native Windows/macOS/Linux support, and the LTS commitment avoids churn. Qt 6.10 exists but is not yet LTS. |
| **GStreamer 1.26.x** | 1.26.5 (current stable) | Audio/video decoding pipeline | UxPlay uses GStreamer for all media output. GStreamer is the only cross-platform pipeline library that provides H.264/H.265 hardware decode via VAAPI (Linux/Intel), D3D11 (Windows), and VideoToolbox (macOS) with the same pipeline API. Version 1.26 is the current stable series (1.28.0 dropped in January 2026 but too new for stable distribution packaging). |
| **OpenSSL** | 3.x (≥3.0.0) | TLS + AirPlay FairPlay crypto | AirPlay 2 mirroring requires TLS and FairPlay SRP authentication. OpenSSL 3.x is GPL-compatible and available everywhere. OpenSSL 1.1.1 reached EOL September 2023 — use 3.x. |
| **CMake** | ≥3.28 | Build system | Standard for cross-platform C++ projects in 2026. Integrates with vcpkg for dependency management. Supports presets for per-platform configuration. All core dependencies ship CMake find-modules. |
### Protocol Libraries
| Library | Version | Protocol | Notes |
|---------|---------|----------|-------|
| **UxPlay** (embedded / forked) | 1.73.6 | AirPlay 2 mirroring | The only actively maintained, GPL-licensed AirPlay mirroring receiver for Linux/macOS/Windows. Depends on GStreamer + OpenSSL + libplist + Avahi/mDNSResponder. Embed as a submodule rather than link as a shared lib — it is an application, not a library, but its core logic is separable. |
| **libplist** | ≥2.6 (2.7.0 released 2025-05) | Apple Property List parsing | Required by UxPlay. Cross-platform. Part of the libimobiledevice family. |
| **openscreen (libcast)** | tip-of-tree | Google Cast streaming | Google's own open-source Cast protocol implementation. The `cast/streaming` module is standalone and handles Cast Streaming sessions (sender + receiver). **Critical caveat: see Cast Authentication note below.** |
| **pupnp / libupnp** | ≥1.14.x | DLNA/UPnP DMR | The Portable UPnP SDK. C library with active maintenance (GitHub: pupnp/pupnp). Implements UPnP AV Media Renderer (DMR) needed for DLNA push. Platinum UPnP SDK is an alternative but last released 2020 — avoid. |
| **MiracleCast / lazycast approach** | N/A | Miracast | **No viable cross-platform library exists.** See Miracast note below. |
### Service Discovery
| Library | Platform | Purpose | Notes |
|---------|----------|---------|-------|
| **Avahi** | Linux | mDNS/DNS-SD advertisement | Used by UxPlay on Linux. Required to advertise `_airplay._tcp` and `_raop._tcp`. LGPL license. |
| **mDNSResponder (Bonjour)** | macOS + Windows | mDNS/DNS-SD advertisement | Apple's own implementation; built into macOS. On Windows, the Bonjour installer must be bundled or the user must have iTunes installed. |
| **mjansson/mdns** (header-only) | All platforms | mDNS fallback / SSDP for DLNA | Single-file, public-domain, C99, no dependencies. Useful for DLNA SSDP advertisement and as a lightweight fallback if Avahi/Bonjour unavailable. |
### Supporting Libraries
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| **libssl / libcrypto (OpenSSL)** | 3.x | Crypto primitives | Always — AirPlay FairPlay, Cast TLS, general cert handling |
| **gst-plugins-base** | 1.26.x | GStreamer core plugins | Always — provides `videoconvert`, `audioconvert`, `autovideosink` |
| **gst-plugins-good** | 1.26.x | RTP, RTSP, codec plugins | Always — `rtph264depay`, `rtpjitterbuffer`, `opusparse` |
| **gst-plugins-bad** | 1.26.x | Hardware decode plugins | Always on desktop — `vaapidecodebin` (Linux), `d3d11h264dec` (Windows) |
| **gst-libav** | 1.26.x | FFmpeg codec fallback | Always — software H.264/H.265/AAC decode when hardware unavailable |
| **Qt QML qml6glsink** | Ships with GStreamer | Render GStreamer video into Qt QML | Use `qml6glsink` to get zero-copy video frames into the Qt scene graph. This is the correct integration pattern; `QMediaPlayer` is too opaque for custom pipelines. |
| **protobuf** | 3.x or 4.x | Google Cast CASTV2 protocol | Cast uses protobuf over TLS for control messages. Use `libprotobuf` matching openscreen's requirement. |
| **spdlog** | ≥1.13 | Structured logging | Header-only C++ logging. Not required but saves time. |
### Development Tools
| Tool | Purpose | Notes |
|------|---------|-------|
| **vcpkg** | C++ dependency management | Manifest mode (`vcpkg.json`) ensures reproducible builds across platforms. Better for open-source projects than Conan due to simpler setup and GitHub Actions integration. |
| **Ninja** | Fast parallel build | Pair with CMake for fast incremental builds. `cmake -G Ninja` |
| **clang-format** | Code formatting | Enforce consistent style across contributors |
| **GStreamer gst-inspect-1.0** | Pipeline debugging | Verify plugins available at runtime |
| **GitHub Actions** | CI/CD | Matrix builds for Linux/macOS/Windows in one workflow |
## Critical Protocol Notes
### AirPlay — HIGH confidence, viable
### Google Cast — MEDIUM confidence, legally/technically constrained
### Miracast — LOW confidence, practically broken on desktop Linux/Windows as a receiver
- **v1:** Skip Wi-Fi Direct Miracast entirely. Too fragile.
- **v1.5:** Implement Miracast over Infrastructure (MS-MICE) for Windows — this is achievable and covers modern Windows 10/11 sources.
- **v2:** Reconsider Wi-Fi Direct Miracast if the kernel/wpa_supplicant P2P API situation improves.
### DLNA — HIGH confidence, viable
## Installation
# Linux (Ubuntu/Debian)
# macOS (Homebrew)
# Windows (MSYS2 MinGW-64)
# vcpkg (cross-platform, for non-system deps)
## Alternatives Considered
| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| Qt 6.8 LTS | Electron / Tauri | Never for this use case — Electron can't sink GStreamer video into the renderer without a C++ native module, negating any benefit. Adds 200MB+ to binary size. |
| Qt 6.8 LTS | wxWidgets | If you specifically need LGPL without Qt's GPL/commercial distinction. Missing QML and has weaker multimedia integration. |
| GStreamer 1.26 | FFmpeg (libav) directly | If you don't need a pipeline abstraction and are writing a simple player, not a multi-source receiver. GStreamer's dynamic pipeline model is essential here for switching between protocols. |
| GStreamer 1.26 | VLC libvlc | libvlc has no pipeline injection API — you can't feed raw RTP/RTSP demux output into it from AirPlay. GStreamer is the only option that allows protocol libraries to inject encoded frames. |
| pupnp / libupnp | Platinum UPnP SDK | Platinum's last release was July 2020 and it requires SCons on Linux. Dead for new projects. |
| pupnp / libupnp | libnpupnp | Viable future upgrade path (fewer historical bugs), but documentation is sparse and ecosystem is smaller than pupnp. |
| UxPlay (embedded) | shairplay | shairplay is unmaintained and targets AirPlay 1 only. UxPlay supports AirPlay 2 mirroring. |
| vcpkg | Conan 2 | Conan is faster for large enterprise dependency graphs. For this project vcpkg manifest mode is simpler and better supported in GitHub Actions. |
| openscreen (libcast) | CastV2 reverse-engineering | Building your own CASTV2 protobuf parser from scratch takes months. Openscreen gives you the streaming layer for free, even if authentication is the unsolved piece. |
| C++17 | Rust | Rust is a reasonable choice but all reference implementations (UxPlay, pupnp, openscreen) are C/C++. Wrapping them via FFI is more work than the type-safety benefit. Revisit for v2. |
## What NOT to Use
| Avoid | Why | Use Instead |
|-------|-----|-------------|
| **Platinum UPnP SDK** | Last released July 2020. Requires Visual Studio 2010 on Windows and SCons on Linux. Effectively abandoned. | `pupnp / libupnp` (GitHub: pupnp/pupnp) |
| **OpenSSL 1.1.1** | Reached End of Life September 2023. Security vulnerabilities will not receive patches. | OpenSSL 3.x |
| **Qt 5.x** | Qt 5.15 LTS commercial support ended. Many Linux distros shipping Qt 6 by default. Qt 5 multimedia backend is weaker. | Qt 6.8 LTS |
| **WirelessDisplay (WirelessPresentation/WirelessDisplay)** | Java-heavy (80% Java), last release January 2022, no OSS license visible, Windows + Android only (no macOS). | Build protocol modules from reference libraries directly |
| **Wi-Fi Direct Miracast (MiracleCast)** | Development explicitly stalled by maintainer. wpa_supplicant P2P API is not stable across distros. | Miracast over Infrastructure (MS-MICE) on Windows for v1 |
| **RPiPlay (original FD-/RPiPlay)** | Pi-specific OpenMAX backend. Not portable to x86 Linux/macOS/Windows. | UxPlay (the FDH2 fork) which is the active cross-platform version |
| **Node.js / Electron for core** | Cannot inject raw A/V frames from C protocol libraries into Chromium's renderer without a native addon that defeats the purpose. High memory overhead. | Qt + GStreamer |
| **AirServer SDK (commercial)** | Proprietary, licensed per-seat. Contradicts the free/open-source core value of the project. | UxPlay + openscreen + pupnp as open-source building blocks |
| **Qt QMediaPlayer for protocol video output** | Qt Multimedia's `QMediaPlayer` is a black box — it cannot accept raw encoded H.264 frames from a protocol library's callback. | `qml6glsink` + custom GStreamer pipeline |
## Stack Patterns by Variant
- Use Avahi for all mDNS
- Use GStreamer `autovideosink` (no QML overhead)
- Skip Miracast entirely
- This gets AirPlay + DLNA working fastest
- Use the official GStreamer MSVC installer (not MSYS2 packages) for the release build
- Bundle Bonjour (mDNSResponder) — it cannot be assumed present unless iTunes is installed
- Implement Miracast over Infrastructure (MS-MICE) as the Windows-specific Miracast path
- Bonjour is built-in (no bundling needed)
- Use `vtdec` (VideoToolbox) GStreamer element for H.264/H.265 hardware decode
- Qt 6.8 builds natively on Apple Silicon and Intel
- Drop in the openscreen `cast/streaming` receiver module
- The GStreamer pipeline hookup is the same as AirPlay — the receiver callback delivers H.264 frames
## Version Compatibility
| Package | Compatible With | Notes |
|---------|-----------------|-------|
| Qt 6.8.x | GStreamer 1.26.x via qml6glsink | `qml6glsink` ships in `gst-plugins-good`. Both must use the same OpenGL context. |
| UxPlay 1.73.x | GStreamer ≥1.20 | UxPlay tests against GStreamer 1.22+ in practice. Use 1.26.x to match system packages. |
| UxPlay 1.73.x | OpenSSL ≥1.1.1 | Prefers 3.x for GPL compatibility. Do not mix OpenSSL 1.x and 3.x in the same binary. |
| pupnp/libupnp 1.14.x | Linux glibc ≥2.17, Windows ≥Vista, macOS ≥10.9 | No known conflicts with Qt or GStreamer. |
| openscreen (libcast) | Chromium build toolchain (GN + Ninja) | Does NOT use CMake. Requires a separate build step or vendoring. Integration with a CMake project requires building openscreen first and linking the resulting static libs. |
| protobuf 3.x vs 4.x | openscreen requires protobuf | Check openscreen's `DEPS` file for exact version. Protobuf 4.x (formerly 3.22+) has an API break from 3.21.x. Pin the version. |
## Sources
- [UxPlay GitHub (FDH2/UxPlay)](https://github.com/FDH2/UxPlay) — Version 1.73.6, dependency chain, platform support confirmed HIGH confidence
- [GStreamer Releases Page](https://gstreamer.freedesktop.org/releases/) — 1.26.x current stable, 1.28.0 released January 2026 HIGH confidence
- [Qt 6.8 LTS Release Blog](https://www.qt.io/blog/qt-6.8-released) — LTS status, platform support HIGH confidence
- [AirServer Built with Qt (Qt.io)](https://www.qt.io/development/airserver-universal-screen-mirroring-receiver-built-with-qt) — Confirmed commercial reference uses Qt Core, Quick, Network, Layouts, Multimedia MEDIUM confidence
- [GStreamer qml6glsink documentation](https://gstreamer.freedesktop.org/documentation/qml6/qml6glsink.html) — Integration pattern for Qt 6 + GStreamer HIGH confidence
- [Chromecast Device Authentication (Tristan Penman, 2025-03-22)](https://tristanpenman.com/blog/posts/2025/03/22/chromecast-device-authentication/) — Certificate chain requirement confirmed HIGH confidence
- [Shanocast / Making a Chromecast receiver](https://xakcop.com/post/shanocast/) — Authentication bypass approach documented MEDIUM confidence
- [openscreen libcast README](https://chromium.googlesource.com/openscreen/+/HEAD/cast/README.md) — Standalone streaming module confirmed HIGH confidence
- [MiracleCast stalled development (maintainer statement)](https://github.com/albfan/miraclecast) — P2P API blocker confirmed HIGH confidence
- [MS-MICE Miracast over Infrastructure (Microsoft Docs)](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-mice/9598ca72-d937-466c-95f6-70401bb10bdb) — Protocol spec is public HIGH confidence
- [pupnp/libupnp GitHub](https://github.com/pupnp/pupnp) — Active maintenance confirmed MEDIUM confidence
- [Platinum UPnP SDK GitHub](https://github.com/plutinosoft/Platinum) — Last release July 2020 confirmed HIGH confidence (reason to avoid)
- [libplist 2.7.0 release (2025-05)](https://libimobiledevice.org/news/2025/05/14/libplist-2.7.0-release/) — Active maintenance confirmed HIGH confidence
- [OpenSSL 1.1.1 EOL](https://www.openssl.org/blog/blog/2023/09/11/eol-111/) — EOL September 2023 HIGH confidence
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

Conventions not yet established. Will populate as patterns emerge during development.
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

Architecture not yet mapped. Follow existing patterns found in the codebase.
<!-- GSD:architecture-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd:quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd:debug` for investigation and bug fixing
- `/gsd:execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd:profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
