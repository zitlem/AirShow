# Feature Research

**Domain:** Cross-platform screen mirroring sender app (Flutter companion to AirShow receiver)
**Researched:** 2026-03-30
**Confidence:** MEDIUM — competitive analysis from live apps, protocol mechanics from official specs; Flutter-specific screen capture implementation details have some LOW-confidence gaps due to platform API churn

---

## Context: Scope of This Research

This is the **companion sender app** for the existing AirShow receiver. It is NOT a general-purpose app that speaks AirPlay or Cast natively — it speaks a **custom AirShow protocol** over a local network, discovered via mDNS. The receiver side already handles AirPlay/Cast/DLNA/Miracast from third-party senders. This app handles the "mirror FROM this device TO an AirShow receiver" direction.

Existing receiver features (already shipped, NOT in scope here):
- Multi-protocol receiver (AirPlay, Cast, Miracast, DLNA)
- mDNS discovery and advertisement
- Security controls, fullscreen UI
- GStreamer decode pipeline

---

## Feature Landscape

### Table Stakes (Users Expect These)

Features every screen mirroring sender app must have. Missing one = product feels broken.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Auto-discover AirShow receivers via mDNS** | Users expect zero-config LAN discovery (AirPlay, Cast, Chromecast all do this). Manual IP entry is unacceptable as the default flow. | MEDIUM | Use `nsd` Flutter package (v4.1.0, supports Android/iOS/macOS/Windows). Discovers `_airshow._tcp` service type. Receiver side must also advertise this new service type — prerequisite work on the receiver. |
| **Device picker list UI** | Users need to see which receivers are available before connecting. All competitors (AirServer Connect, LetsView, ApowerMirror) show a scrollable list of discovered receivers. | LOW | Show display name + IP. Refresh/rescan button. Handle empty state ("No AirShow receivers found on this network"). |
| **One-tap connect and start mirroring** | Mirroring should start in 3 taps or fewer from app launch. AirServer Connect starts mirroring automatically after QR scan. LetsView bills itself as "one-click casting." | MEDIUM | After selecting a receiver, immediately request screen capture permission and begin streaming. Don't require extra confirmation dialogs beyond OS-required permission prompts. |
| **Screen capture permission request** | Every platform requires explicit user permission before an app can capture the screen. Users expect a clear explanation of why it is needed. | LOW | Android: `MediaProjection` API, requires foreground service + `FOREGROUND_SERVICE_MEDIA_PROJECTION` permission (Android 14+). iOS: ReplayKit broadcast extension (separate process, 50 MB memory limit, ~15 fps cap). macOS/Windows/Linux: OS-level screen recording permission prompts. |
| **Stop mirroring control** | Users must be able to stop cleanly. Competitors surface "Stop Mirroring" prominently. Android requires a persistent foreground notification with a stop action while mirroring is active. | LOW | In-app stop button. Android: foreground notification with "Stop" action. iOS: system-level broadcast stop from Control Center — app must respond to `broadcastFinished()`. |
| **Connection status indicator** | User must know: disconnected / connecting / mirroring / error. AirServer Connect uses a state label; LetsView uses color-coded icons. | LOW | Three states minimum: idle, connecting, mirroring. Show receiver name while connected. Surface error messages (receiver unreachable, permission denied, connection lost). |
| **QR code fallback for complex networks** | On corporate/school networks where mDNS is blocked by VLAN segmentation, QR code connection is the de-facto fallback. AirServer Connect, LetsView, Kingshiper, and Mirroring360 all provide QR codes on the receiver side. | MEDIUM | Receiver displays a QR code encoding its IP + port + auth token. Sender app has a "Scan QR Code" button that bypasses mDNS. Requires camera permission on mobile. Desktop sender: allow manual IP + port entry instead. |
| **Auto-reconnect on brief disconnect** | Wi-Fi dropouts are common. Users expect the app to silently reconnect within seconds rather than forcing a manual restart. | MEDIUM | Implement reconnect backoff (1s, 2s, 5s). Show "Reconnecting..." state in the status indicator. Give up after ~30s and return to device picker. |

---

### Differentiators (Competitive Advantage)

Features that set AirShow Companion apart. None are required for launch, but they compound the "free and open" value proposition.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **Adjustable stream quality (bitrate + resolution presets)** | Paid competitors (ApowerMirror paid tier, AirServer Connect) lock quality settings behind upsells. AirShow is free — exposing bitrate/resolution gives power users control competitors charge for. | MEDIUM | Low (720p/2 Mbps), Medium (1080p/4 Mbps), High (1080p/8 Mbps) presets. Advanced: manual slider. Store preference per-receiver. Receiver-side AirShowHandler must accept quality negotiation in the handshake — this is a protocol design decision. |
| **Desktop sender (Windows/macOS/Linux)** | Competitors focus on mobile-to-TV. AirShow targets computer-to-computer mirroring (e.g., laptop to desktop). No free tool does this cleanly without USB cables or remote desktop software. | HIGH | Flutter desktop screen capture via `screen_capturer` package. Windows: DirectX Desktop Duplication API. macOS: ScreenCaptureKit (macOS 12.3+) or CGDisplayStream. Linux: PipeWire `xdg-desktop-portal`. Each platform needs a separate capture adapter. |
| **Multi-display source selection** | Power users with multiple monitors want to choose which display to cast. Most competitors cast the primary display only. | MEDIUM | Enumerate displays at connection time. Show display picker before mirroring starts. Desktop only (mobile has one screen). Depends on "Desktop sender" feature being implemented first. |
| **Audio mirroring on desktop platforms** | AirServer Connect has no audio on Android (documented limitation — no system audio capture API). AirShow can support audio on platforms where the API exists (macOS, Windows, Linux). | HIGH | macOS: AVAudioEngine tap or loopback virtual device. Windows: WASAPI loopback capture. Linux: PipeWire source sink. Android: no system audio capture without root — document this limitation clearly. iOS: ReplayKit captures app audio only (not all system audio). Encode as Opus (low latency) or AAC. |
| **Low-latency mode toggle** | Power users (presenters, gamers) want sub-100ms latency. Standard streaming buffers 200–500ms for smoothness. Competitive differentiation for presentation use cases. | HIGH | Reduce encoder B-frames, disable or minimize jitter buffer on receiver side, tune RTP clock. Expose as a toggle, not the default (trades smoothness for latency). Receiver side must explicitly support the mode. |
| **Receiver health display (latency, bitrate, packet loss)** | No competitor surfaces this to users. Useful for diagnosing "why is my mirror laggy?" without guessing. | MEDIUM | Receiver sends RTCP-style feedback or a sidecar stats message over the AirShow protocol. Display as a collapsible panel in the sender app. |
| **Stream to multiple receivers simultaneously** | ApowerMirror supports 4 simultaneous mirrors as a paid feature. AirShow can offer this free. | HIGH | Each receiver gets its own TCP/UDP stream. CPU/GPU encode load multiplies per-receiver. Only useful for classroom/presentation scenarios. Defer to v2. |

---

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| **Cloud relay for remote mirroring over the internet** | "I want to mirror to a colleague's computer over the internet" | Contradicts the "local network only" core constraint. Adds server infrastructure costs, privacy concerns, and legal risk. Every cloud relay either costs money (defeats the free mission) or becomes a DDoS target. | Document clearly: AirShow is LAN-only by design. For internet use, direct users to RustDesk or similar remote desktop tools. |
| **Reverse control (control sender device from receiver)** | LetsView and ApowerMirror offer this as a premium feature | Requires a second bidirectional input injection channel. On iOS, impossible without a jailbreak. On Android, requires `INJECT_EVENTS` system permission (not grantable to Play Store apps without device owner). Large attack surface for a mirroring tool. | Out of scope permanently. Document why if requested. |
| **Built-in screen recorder on the sender** | Users conflate "mirroring" with "recording" | Adds storage permission, background file write, codec complexity, and consent questions. Scope creep that delays the core mirroring use case. | The receiver can optionally save the mirrored stream — a separate receiver-side feature. |
| **Whiteboard/annotation overlay on the stream** | LetsView ships this as a drawing tool over mirrored content | Requires a second compositing layer before encoding, adding latency. Scope and complexity far exceed the value for a basic mirroring app. | Suggest OS-native annotation tools (macOS Markup, Windows Snip and Sketch) on the receiving end. |
| **Account or login system** | Users familiar with commercial apps may expect cloud accounts | Contradicts the "no internet required" and "no paywalls" constraints. Accounts imply a backend service, which requires maintenance and creates a fragile dependency on the AirShow project staying funded/maintained. | Zero accounts, zero login. Discovery is mDNS-local. Security is handled by PIN/QR pairing at connection time. |
| **Native AirPlay/Miracast/Cast sender** | "Can the app speak AirPlay natively to send to any AirPlay receiver?" | Apple does not publish the AirPlay sender protocol. Reverse-engineering violates Apple's ToS and DMCA. Cast sender requires Google device certification. Miracast sender requires Wi-Fi Direct kernel support that is unreliable on desktop. | The custom AirShow protocol is the correct path — full control over quality, latency, and features without legal risk. |

---

## Feature Dependencies

```
[mDNS Auto-Discovery (_airshow._tcp)]
    └──required for──> [Device Picker List UI]
                           └──required for──> [One-Tap Connect]
                                                  └──required for──> [Screen Capture Permission Request]
                                                                         └──required for──> [Start Mirroring / Stream]
                                                                                                └──required for──> [Stop Mirroring Control]
                                                                                                └──required for──> [Connection Status Indicator]
                                                                                                └──required for──> [Auto-Reconnect]

[QR Code Fallback]
    └──alternative path to──> [One-Tap Connect]  (bypasses mDNS when VLAN blocks it)

[Desktop Sender (screen_capturer integration)]
    └──enables──> [Multi-Display Source Selection]
    └──enables──> [Audio Toggle on macOS/Windows/Linux]

[Adjustable Stream Quality]
    └──depends on──> [Receiver-side AirShowHandler quality negotiation field in handshake]

[Low-Latency Mode]
    └──depends on──> [Receiver-side jitter buffer tuning]  (receiver must be updated)

[Audio Toggle]
    └──not available on──> [Android sender]  (no system audio capture API without root)
    └──iOS ReplayKit captures app audio only (not full system audio)
```

### Dependency Notes

- **mDNS requires receiver to advertise `_airshow._tcp`**: The existing receiver's mDNS advertisement code must be extended with a new service type. This is a prerequisite receiver-side change before the sender app can find anything on the network.
- **Screen capture on iOS requires a Broadcast Upload Extension**: iOS ReplayKit runs the capture in a separate process. The Flutter app must coordinate with the extension via App Groups shared container. This is architecturally unlike any other platform and must be treated as a separate scope of work — it cannot be done with a standard Flutter plugin alone.
- **Quality negotiation and low-latency mode require protocol design decisions now**: The custom AirShow protocol handshake must include capability negotiation fields from the start. These cannot be bolted on later without breaking backward compatibility between sender and receiver versions.
- **Audio on desktop is three separate implementations**: macOS (AVAudioEngine/CoreAudio), Windows (WASAPI loopback), and Linux (PipeWire) each have entirely different APIs. A clean platform-channel abstraction layer is needed or audio becomes three independent maintenance burdens.

---

## MVP Definition

### Launch With (v1 — validates the concept)

- [ ] **mDNS discovery of `_airshow._tcp` receivers** — core discovery mechanic; without it the app is useless on a standard home network. Requires receiver-side advertisement change as a prerequisite.
- [ ] **Device picker UI** — scrollable list of discovered receivers with name and IP, refresh/rescan button, empty state message.
- [ ] **QR code fallback connection** — essential for corporate/school networks; this is industry-standard and needed from day one, not a later addition.
- [ ] **Screen capture + H.264 encode + stream to receiver** — the core mirroring. Android (MediaProjection) and iOS (ReplayKit) are the highest-value platforms. macOS as the third target due to ScreenCaptureKit stability.
- [ ] **Connection status indicator** — idle / connecting / mirroring / error states with receiver name shown while connected.
- [ ] **Stop mirroring** — in-app button plus Android foreground notification with "Stop" action.
- [ ] **Auto-reconnect (basic)** — retry up to 3 times on disconnect before returning to device picker.

### Add After Validation (v1.x)

- [ ] **Adjustable stream quality presets** — add once basic streaming is confirmed stable; requires quality negotiation field to be designed into the AirShow protocol handshake from the start.
- [ ] **Desktop sender (macOS first, then Windows)** — macOS is easier (ScreenCaptureKit is stable); Windows (DirectX duplication) second. Linux defer to v2.
- [ ] **Audio toggle on macOS/Windows** — add after video-only mirroring is reliable; audio sync adds a separate complexity that should not block the video milestone.
- [ ] **Multi-display source selection (desktop)** — add once the desktop sender is working and validated.

### Future Consideration (v2+)

- [ ] **Low-latency mode** — requires receiver-side jitter buffer work; defer until presenter use cases create real demand.
- [ ] **Stream to multiple receivers simultaneously** — high GPU/CPU cost; only valuable for classroom/presentation scenarios; validate that use case before building.
- [ ] **Receiver health/stats display** — add when debugging becomes a user-reported pain point, not before.
- [ ] **Linux sender screen capture** — PipeWire `xdg-desktop-portal` is the correct path but portal availability varies across distributions; defer until Linux desktop environment stabilizes.
- [ ] **Audio on Android** — no viable API without root; only reconsider if Android opens a system audio capture API.

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| mDNS auto-discovery | HIGH | MEDIUM | P1 |
| Device picker list UI | HIGH | LOW | P1 |
| One-tap connect + start mirroring | HIGH | MEDIUM | P1 |
| Screen capture: Android (MediaProjection) | HIGH | HIGH | P1 |
| Screen capture: iOS (ReplayKit extension) | HIGH | HIGH | P1 |
| Screen capture: macOS (ScreenCaptureKit) | HIGH | MEDIUM | P1 |
| Stop mirroring + foreground notification | HIGH | LOW | P1 |
| Connection status indicator | HIGH | LOW | P1 |
| QR code fallback | MEDIUM | MEDIUM | P1 |
| Auto-reconnect | MEDIUM | MEDIUM | P1 |
| Adjustable stream quality presets | MEDIUM | MEDIUM | P2 |
| Desktop sender: Windows | MEDIUM | HIGH | P2 |
| Audio toggle: macOS/Windows | MEDIUM | HIGH | P2 |
| Multi-display selection (desktop) | LOW | MEDIUM | P2 |
| Low-latency mode | LOW | HIGH | P3 |
| Multi-receiver simultaneous streaming | LOW | HIGH | P3 |
| Receiver stats/health display | LOW | MEDIUM | P3 |

**Priority key:**
- P1: Must have for launch
- P2: Should have, add when core is stable
- P3: Nice to have, future consideration

---

## Competitor Feature Analysis

| Feature | AirServer Connect | LetsView | ApowerMirror | scrcpy | AirShow Companion (plan) |
|---------|-------------------|----------|--------------|--------|--------------------------|
| **Discovery method** | QR code (primary) | mDNS auto + PIN code | mDNS auto + USB | ADB (USB or TCP/IP) | mDNS primary + QR fallback |
| **Android sender** | Yes (via Cast to AirServer) | Yes | Yes | Yes (Android only) | Yes (MediaProjection) |
| **iOS sender** | Yes (via AirPlay to AirServer) | Yes | Yes | No | Yes (ReplayKit extension) |
| **macOS sender** | No dedicated app | Yes | Yes | No | Yes (ScreenCaptureKit) |
| **Windows sender** | No dedicated app | Yes | Yes | No | Yes (DirectX duplication) |
| **Linux sender** | No | No | No | Yes (USB/ADB only) | Yes (PipeWire, v2) |
| **Audio support** | No audio on Android (documented) | Yes (app audio) | Yes (paid tier) | Yes (via sndcpy companion) | macOS/Windows yes; Android no |
| **Stream quality settings** | No | Basic | Yes (paid) | Yes (via CLI flags) | Yes (free, presets) |
| **Multi-display selection** | No | No | No | Yes (via CLI) | Yes (desktop, v1.x) |
| **Protocol** | Cast / AirPlay (3rd-party protocol) | Proprietary | Proprietary | H.264 over ADB socket | Custom AirShow protocol |
| **Reverse control** | No | Yes (paid) | Yes (paid) | Yes (core feature) | Never (security/legal) |
| **Price** | Free (limited) / paid features | Free (with ads) / paid | Freemium | Free, open source | Free, open source |
| **Open source** | No | No | No | Yes (GPLv2) | Yes |

**Key competitive observations:**

1. No competitor offers a clean free desktop-to-desktop mirroring sender. scrcpy mirrors desktop TO Android (inverted direction) over USB. The desktop sender is a genuine unoccupied space.
2. Android system audio capture is a universal limitation — every competitor either omits it or requires payment for a platform that does support it (macOS/Windows). Do not attempt to solve it on Android; document it.
3. QR code pairing is industry-standard and expected from day one, not a "nice to have." Every major competitor ships it.
4. iOS sender via ReplayKit is the most complex platform to implement due to the broadcast extension architecture running in a separate process. Plan for it to take more time than the Android implementation.

---

## Platform-Specific Screen Capture Notes

These are implementation constraints that affect feature scope per platform.

### Android
- API: `MediaProjection` (Android 5.0+, stable)
- Requires a foreground service with `mediaProjection` foreground service type declared in the manifest (Android 14+)
- System shows a mandatory permission dialog ("This app will capture everything displayed on your screen")
- **No system-wide audio capture without root** — this is a hard platform limitation, not a missing library
- Flutter integration: `media_projection_creator` package + custom platform channel for encoding pipeline
- **Confidence: HIGH** — stable API, well-documented, widely used in commercial apps

### iOS
- API: ReplayKit broadcast extension (runs as a separate extension process, not the main Flutter app)
- Memory limit: 50 MB for the extension process — a hard constraint that limits buffering and resolution choices
- Frame rate: capped at approximately 15 fps by ReplayKit, variable; not guaranteed
- App audio is captured but not system-wide audio from unrelated apps
- Flutter coordination: App Groups shared container between main app and extension; `flutter_replay_kit_launcher` package for triggering the system picker
- Screen recording permission must be enabled by user in iOS Settings > Privacy & Security
- **Confidence: MEDIUM** — API is stable but the 50 MB and 15 fps limits are hard constraints that affect quality claims in marketing

### macOS
- API: `ScreenCaptureKit` (macOS 12.3+); `CGDisplayStream` fallback for older macOS
- Screen Recording permission required in System Settings > Privacy & Security
- Hardware H.264 encode via VideoToolbox (same GPU pipeline already used by the receiver)
- Flutter integration: `screen_capturer` package, but its maturity on macOS is LOW; may need a custom platform channel via FFI
- **Confidence: MEDIUM** — ScreenCaptureKit itself is high-confidence; the Flutter ecosystem around it is lower

### Windows
- API: DirectX Desktop Duplication API (Windows 8+) — the standard used by OBS, Teams screen share, Discord
- Hardware H.264 encode via MediaFoundation, NVENC (NVIDIA), or Intel QuickSync
- Flutter integration: `screen_capturer` package wraps the native API, or custom FFI
- **Confidence: MEDIUM** — DirectX duplication is stable and well-understood; Flutter desktop plugin ecosystem is still maturing as of early 2026

### Linux
- API: PipeWire `xdg-desktop-portal` (Wayland); X11 XCB for legacy X11 sessions
- PipeWire portal availability varies: good in Flatpak environments, inconsistent in bare installs depending on distro and compositor
- **Confidence: LOW** — defer to v2; too many environment variables (Wayland vs X11, portal version, distro packaging) to guarantee a reliable experience across the Linux user base

---

## Sources

- [AirDroid: 4 Major Screen Mirroring Protocols](https://www.airdroid.com/screen-mirror/screen-mirroring-protocols/) — protocol mechanics, codec specs MEDIUM confidence
- [AirServer Connect: How to screen mirror using the sender app](https://support.airserver.com/support/solutions/articles/43000537120) — QR code flow, competitor feature set HIGH confidence
- [LetsView: Free Screen Mirroring](https://letsview.com/screen-mirroring) — competitor feature set MEDIUM confidence
- [ApowerMirror Review 2025 - AirDroid](https://www.airdroid.com/screen-mirror/apowermirror-review/) — competitor analysis MEDIUM confidence
- [scrcpy GitHub (Genymobile/scrcpy)](https://github.com/Genymobile/scrcpy) — architecture: ADB + H.264 + server-on-device HIGH confidence
- [NSD Flutter package (pub.dev v4.1.0)](https://pub.dev/packages/nsd) — Android/iOS/macOS/Windows support confirmed HIGH confidence
- [Apple Developer: ReplayKit limitations](https://developer.apple.com/documentation/ReplayKit) — 50 MB memory cap, ~15 fps cap HIGH confidence
- [Android MediaProjection API developer docs](https://developer.android.com/media/grow/media-projection) — permission requirements, foreground service type HIGH confidence
- [Kingshiper: QR code and PIN code connection flows](https://www.kingshiper.com/support/287.html) — industry-standard fallback patterns MEDIUM confidence
- [AirDroid: Best Screen Mirroring Apps 2025](https://deskin.io/resource/blog/best-screen-mirroring-app) — competitor overview MEDIUM confidence
- [Mirroring360: QR and Meeting ID fallback](https://www.mirroring360.com/mirroring-assist) — fallback connection patterns MEDIUM confidence
- [Android foreground notification requirements (developer.android.com)](https://developer.android.com/develop/ui/views/notifications) — foreground service notification requirements HIGH confidence

---

*Feature research for: AirShow Companion Sender App (Flutter cross-platform sender)*
*Researched: 2026-03-30*
