# Stack Research

**Domain:** Cross-platform wireless display receiver (AirPlay + Google Cast + Miracast + DLNA)
**Researched:** 2026-03-28
**Confidence:** MEDIUM — Core stack is HIGH confidence; Google Cast and Miracast receiver feasibility have critical caveats (see notes)

---

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

---

## Critical Protocol Notes

### AirPlay — HIGH confidence, viable

UxPlay 1.73.6 works on Linux, macOS, and Windows (via MSYS2/MinGW-64). GPL v3. Its internals (the `lib/` subfolder containing `raop`, `mdns`, `playfair`) are the embeddable core. The dependency chain is well-understood:

```
UxPlay core → OpenSSL 3.x + libplist 2.x + GStreamer 1.x + Avahi/mDNS
```

The correct integration approach is to embed UxPlay's C library layer (not the top-level GStreamer application binary) and hook its video/audio callbacks into MyAirShow's own GStreamer pipeline.

### Google Cast — MEDIUM confidence, legally/technically constrained

**The fundamental problem:** Google Cast's device authentication requires a certificate chain signed by Google. Chrome sends a challenge; the receiver must prove it holds a Google-issued device cert. Independent receivers cannot obtain such certs legitimately.

The only practical workarounds documented in the open-source community are:
1. **Shanocast approach** — Extract pre-computed challenge signatures from AirReceiver APK. Works but is legally grey and requires periodic updates as signatures expire.
2. **Use openscreen's standalone_receiver** demo as a base — same underlying problem with authentication.

**Recommendation:** Implement Cast as Phase 2/3 after AirPlay + DLNA ship. Design the Cast module as a plugin so it can be swapped when/if the authentication situation changes. Flag this as the highest-risk protocol in the project.

### Miracast — LOW confidence, practically broken on desktop Linux/Windows as a receiver

Miracast uses Wi-Fi Direct (P2P) at the Wi-Fi layer. The **open-source MiracleCast project is explicitly stalled** — its developer documented that development is halted "until network managers provide a P2P API." There is no stable Linux userspace Wi-Fi P2P stack for an application-level receiver.

On Windows, Miracast over Infrastructure (MS-MICE, TCP port 7250) is a better target: it works over standard LAN/Wi-Fi without Wi-Fi Direct, and is what Surface Hub and Windows PCs use. The MS-MICE protocol is publicly documented by Microsoft.

**Recommendation:**
- **v1:** Skip Wi-Fi Direct Miracast entirely. Too fragile.
- **v1.5:** Implement Miracast over Infrastructure (MS-MICE) for Windows — this is achievable and covers modern Windows 10/11 sources.
- **v2:** Reconsider Wi-Fi Direct Miracast if the kernel/wpa_supplicant P2P API situation improves.

### DLNA — HIGH confidence, viable

pupnp/libupnp implements UPnP AV DMR (Digital Media Renderer). DLNA push from phones/smart TVs sends HTTP URLs; the receiver fetches the URL and plays it via GStreamer. This is the simplest protocol in the stack. libnpupnp is a modern rewrite with fewer bugs but limited documentation — use pupnp for v1, migrate later.

---

## Installation

```bash
# Linux (Ubuntu/Debian)
sudo apt install \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev gstreamer1.0-libav \
  libssl-dev libplist-dev libavahi-client-dev \
  libupnp-dev libprotobuf-dev protobuf-compiler \
  qt6-base-dev qt6-declarative-dev cmake ninja-build

# macOS (Homebrew)
brew install gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-libav openssl libplist \
  protobuf qt cmake ninja

# Windows (MSYS2 MinGW-64)
pacman -S \
  mingw-w64-x86_64-gstreamer \
  mingw-w64-x86_64-gst-plugins-base \
  mingw-w64-x86_64-gst-plugins-good \
  mingw-w64-x86_64-gst-plugins-bad \
  mingw-w64-x86_64-gst-libav \
  mingw-w64-x86_64-openssl \
  mingw-w64-x86_64-protobuf \
  mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja

# vcpkg (cross-platform, for non-system deps)
vcpkg install spdlog mdns
```

---

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

---

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

---

## Stack Patterns by Variant

**If targeting Linux only (v0 prototype):**
- Use Avahi for all mDNS
- Use GStreamer `autovideosink` (no QML overhead)
- Skip Miracast entirely
- This gets AirPlay + DLNA working fastest

**If targeting Windows first:**
- Use the official GStreamer MSVC installer (not MSYS2 packages) for the release build
- Bundle Bonjour (mDNSResponder) — it cannot be assumed present unless iTunes is installed
- Implement Miracast over Infrastructure (MS-MICE) as the Windows-specific Miracast path

**If targeting macOS:**
- Bonjour is built-in (no bundling needed)
- Use `vtdec` (VideoToolbox) GStreamer element for H.264/H.265 hardware decode
- Qt 6.8 builds natively on Apple Silicon and Intel

**If Google Cast authentication is resolved:**
- Drop in the openscreen `cast/streaming` receiver module
- The GStreamer pipeline hookup is the same as AirPlay — the receiver callback delivers H.264 frames

---

## Version Compatibility

| Package | Compatible With | Notes |
|---------|-----------------|-------|
| Qt 6.8.x | GStreamer 1.26.x via qml6glsink | `qml6glsink` ships in `gst-plugins-good`. Both must use the same OpenGL context. |
| UxPlay 1.73.x | GStreamer ≥1.20 | UxPlay tests against GStreamer 1.22+ in practice. Use 1.26.x to match system packages. |
| UxPlay 1.73.x | OpenSSL ≥1.1.1 | Prefers 3.x for GPL compatibility. Do not mix OpenSSL 1.x and 3.x in the same binary. |
| pupnp/libupnp 1.14.x | Linux glibc ≥2.17, Windows ≥Vista, macOS ≥10.9 | No known conflicts with Qt or GStreamer. |
| openscreen (libcast) | Chromium build toolchain (GN + Ninja) | Does NOT use CMake. Requires a separate build step or vendoring. Integration with a CMake project requires building openscreen first and linking the resulting static libs. |
| protobuf 3.x vs 4.x | openscreen requires protobuf | Check openscreen's `DEPS` file for exact version. Protobuf 4.x (formerly 3.22+) has an API break from 3.21.x. Pin the version. |

---

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

---

*Stack research for: cross-platform wireless display receiver (AirPlay + Cast + Miracast + DLNA)*
*Researched: 2026-03-28*
