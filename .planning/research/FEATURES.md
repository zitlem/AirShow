# Feature Research

**Domain:** Cross-platform screen mirroring receiver (AirServer alternative)
**Researched:** 2026-03-28
**Confidence:** MEDIUM-HIGH (competitor analysis verified via official sites; protocol details from official specs and open-source implementations)

---

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist. Missing these = product feels incomplete or broken.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| AirPlay screen mirroring reception | iOS/macOS users expect any AirPlay-capable display receiver to "just work" | HIGH | AirPlay 2 TLS/certificate auth adds complexity; UxPlay implements AirPlay 1 which covers screen mirroring; AirPlay 2 adds multi-room audio (out of scope for v1) |
| Google Cast screen mirroring reception | Android/Chrome users expect Cast to work on any receiver app | HIGH | Google Cast uses TLS with device certificates; open-source shanocast/openscreen exist but certificate chain is the key challenge |
| Miracast reception | Windows and Android devices use Miracast natively; users expect it to work | HIGH | Requires Wi-Fi Direct (P2P) — the most complex of the three protocols; MiracleCast and GNOME Network Displays exist as Linux references |
| DLNA media push (DMR) | Smart-TV-style "push media" use case; users with DLNA clients expect to push video/audio | MEDIUM | DLNA/UPnP DMR is for media push, NOT live screen mirroring; clearly distinct from AirPlay/Cast/Miracast; implementation via UPnP MediaRenderer profile |
| Audio playback from mirrored device | Users casting video or audio expect to hear it from the receiver | MEDIUM | Audio sync with video is a known pain point; A/V sync must be explicitly managed |
| Mute/unmute audio control | Users want to silence the receiver without stopping the mirror | LOW | Simple UI toggle; must persist across reconnects |
| Auto-discovery via mDNS/Bonjour | Sender devices discover receivers via mDNS — no manual IP entry expected | MEDIUM | AirPlay uses `_airplay._tcp.local`; Google Cast uses `_googlecast._tcp.local`; Miracast uses Wi-Fi Direct P2P discovery (different stack); all must be advertised simultaneously |
| Receiver name display on screen | Users see the receiver's name in their device's casting list | LOW | Name shown in AirPlay/Cast picker on sender device; customizable name is a user comfort feature |
| Fullscreen display mode | Users expect cast content to fill the screen like an Apple TV | LOW | Standard window management; must handle aspect ratio and letterboxing |
| Connection status indication | Users need visual feedback that a device is connected or waiting | LOW | On-screen overlay or status bar element showing protocol and source device name |
| Works on same local network, no internet | Privacy expectation; users do not want cloud routing | LOW | mDNS is link-local by design; no cloud infrastructure needed or wanted |
| Run on Linux, macOS, Windows | "Cross-platform" is the core promise; single protocol support is not enough | HIGH | Single codebase; different platform APIs for audio, rendering, and Wi-Fi Direct |

---

### Differentiators (Competitive Advantage)

Features that set AirShow apart. Not required, but create value over existing options.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| All four protocols in one app (AirPlay + Cast + Miracast + DLNA) | No existing free/open-source tool covers all four; UxPlay is AirPlay-only; FCast is Cast-centric | HIGH | This is the primary market gap AirShow fills; the "one app" promise for heterogeneous device rooms |
| Completely free, no license key, no paywalls | AirServer costs $20+/device; Reflector 4 is paid; LonelyScreen is paid; all alternatives have friction | LOW (product decision, not technical) | Open-source license removes all license management complexity; builds trust with Linux community |
| Linux support | AirServer/Reflector don't support Linux at all; Linux users are chronically underserved for receiver software | MEDIUM | Requires careful platform abstraction; GStreamer is the natural Linux media backend |
| Customizable receiver name | Users in multi-receiver environments (classrooms, offices) need named receivers that appear correctly in device pickers | LOW | Name is broadcast via mDNS service record; trivially configurable in settings |
| Multi-device simultaneous display | AirServer and Reflector support this; it is differentiating vs. simpler tools like LonelyScreen | HIGH | Requires tiling/split-screen layout in the receiver window; complex state management for N concurrent streams |
| Connection approval prompt (allow/deny) | Security-conscious users and IT admins need to control who can cast | LOW | Simple modal dialog on incoming connection; prevents rogue casts in shared spaces |
| PIN-based pairing | Additional security layer: only devices that know the PIN can connect | MEDIUM | Must be implemented per-protocol (AirPlay PIN, Miracast PIN); not all protocols support PINs natively |

---

### Anti-Features (Commonly Requested, Often Problematic)

Features that seem good but should be deliberately avoided or deferred.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Session recording / screen capture | Users want to capture presentations or tutorials delivered over mirror | Adds FFmpeg/libav dependency, increases binary size significantly, creates legal questions about recording consent, and is entirely out of scope for a receiver — use OBS or OS-level recording instead | Document that users should use OBS or OS screen recorder to capture the receiver window |
| Streaming FROM the computer to other devices (sender mode) | Users conflate sender and receiver; some want both directions | Completely different product category; doubles scope, adds Chromecast sender SDK, AirPlay sender complexity; destroys "receiver only" positioning clarity | Out of scope for v1; could be a v2 product or separate tool |
| Remote/internet mirroring (over internet, not LAN) | Remote work users want to cast over VPN or internet | Requires relay infrastructure (TURN server), dramatically increases latency, creates privacy/security surface, and contradicts the "local network only" constraint that keeps the product simple and free to operate | Not planned; direct users to solutions like Moonlight/Parsec for remote use cases |
| Mobile app receiver (iOS/Android) | Some users want to cast TO a phone rather than a computer | Entirely different platform target; app store distribution complexity; Apple restrictions on AirPlay receiver APIs on iOS | Desktop only; this is a deliberate constraint |
| Cloud sync / account system | Enterprise IT wants central management | Adds server infrastructure, auth complexity, privacy risk; contradicts open-source/free positioning | Use mDNS-based discovery; no accounts needed |
| Annotation / drawing over mirrored content | Education users request this | High UI complexity; out of scope for a lean receiver; distracts from the core "just display it" mission | Defer to v2+ if education use case is validated |
| Recording with device frame overlays | Reflector 4 differentiates on this | Adds significant UI complexity and recording pipeline; scope-creep for MVP | Recording is explicitly out of scope for v1 per PROJECT.md |
| DRM-protected content mirroring (Netflix, etc.) | Users try to mirror streaming apps | DRM (Widevine, FairPlay) explicitly blocks screen mirroring; implementation is legally fraught and technically blocked at the protocol level | Document this as a known limitation; not a bug |

---

## Feature Dependencies

```
[mDNS/Bonjour advertisement]
    └──required-by──> [AirPlay reception]
    └──required-by──> [Google Cast reception]

[Wi-Fi Direct / P2P stack]
    └──required-by──> [Miracast reception]

[UPnP/SSDP discovery]
    └──required-by──> [DLNA media push reception]

[AirPlay reception]
    └──required-by──> [Audio playback from AirPlay]
    └──enables──>     [AirPlay PIN pairing]

[Google Cast reception]
    └──required-by──> [Audio playback from Cast]

[Miracast reception]
    └──required-by──> [Audio playback from Miracast]

[Audio playback]
    └──requires──>    [A/V sync management]
    └──enables──>     [Mute toggle]

[Fullscreen display mode]
    └──enhances──>    [Multi-device simultaneous display]
                          └──conflicts-with──> [Simple single-window layout]

[Connection approval prompt]
    └──enhances──>    [PIN-based pairing]

[Receiver name advertisement]
    └──requires──>    [mDNS/Bonjour advertisement]
    └──enhances──>    [Connection status display]
```

### Dependency Notes

- **mDNS advertisement must precede all AirPlay/Cast features:** Without correct mDNS service records, sender devices cannot discover the receiver at all. This is the lowest-level dependency in the whole system.
- **Miracast is independent of mDNS:** It uses Wi-Fi Direct P2P, a separate discovery and connection stack entirely. This is the most architecturally isolated protocol — it cannot share the mDNS foundation of AirPlay and Cast.
- **DLNA uses SSDP (UPnP), not mDNS:** DLNA discovery is separate from Bonjour. The two stacks can coexist but must both be running for full protocol coverage.
- **Multi-device display conflicts with simple layout:** A single-stream fullscreen layout is straightforward; supporting N simultaneous streams requires a tiling/compositor layer, which adds state complexity and is a separate implementation milestone.
- **A/V sync is a hard sub-requirement of audio:** Users will report audio problems before video problems. The audio pipeline needs explicit sync management from the start — it cannot be bolted on later.

---

## MVP Definition

### Launch With (v1)

Minimum viable product — what's needed to validate the concept.

- [ ] AirPlay screen mirroring reception (iOS/macOS → computer) — the most common use case; highest validation value
- [ ] Google Cast screen mirroring reception (Android/Chrome → computer) — second most common; covers non-Apple users
- [ ] mDNS/Bonjour advertisement for both protocols — prerequisite for discovery; no discovery = nothing works
- [ ] Audio playback with mute toggle — silence on mirrored audio is a fatal usability bug
- [ ] Fullscreen receiver window with connection status — core display experience
- [ ] Runs on Linux, macOS, Windows — the cross-platform promise is the product's reason to exist
- [ ] Customizable receiver name — needed so users can distinguish this receiver in their device pickers

### Add After Validation (v1.x)

Features to add once the AirPlay + Cast core is stable and validated.

- [ ] Miracast reception — adds Windows/Android coverage; architecturally separate from AirPlay/Cast; add once mDNS stack is proven
- [ ] DLNA media push (DMR) — covers smart-TV push use case; add when core mirroring is stable
- [ ] Connection approval prompt (allow/deny) — security feature; important for shared/professional use
- [ ] PIN-based pairing — add after approval prompt is in place

### Future Consideration (v2+)

Features to defer until product-market fit is established.

- [ ] Multi-device simultaneous display (picture-in-picture / tiling) — high complexity; validates if education/meeting room use case is real
- [ ] Annotation / drawing overlay — only if education market is confirmed as primary audience
- [ ] Remote/internet mirroring — would require fundamental infrastructure changes; revisit only if users strongly demand it

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| AirPlay reception | HIGH | HIGH | P1 |
| Google Cast reception | HIGH | HIGH | P1 |
| mDNS advertisement | HIGH | MEDIUM | P1 |
| Audio playback + mute | HIGH | MEDIUM | P1 |
| Fullscreen receiver window | HIGH | LOW | P1 |
| Cross-platform (Linux/macOS/Windows) | HIGH | HIGH | P1 |
| Receiver name customization | MEDIUM | LOW | P1 |
| Miracast reception | MEDIUM | HIGH | P2 |
| DLNA DMR reception | MEDIUM | MEDIUM | P2 |
| Connection approval prompt | MEDIUM | LOW | P2 |
| PIN-based pairing | MEDIUM | MEDIUM | P2 |
| Multi-device simultaneous display | MEDIUM | HIGH | P3 |
| Annotation overlay | LOW | HIGH | P3 |

**Priority key:**
- P1: Must have for launch
- P2: Should have, add when possible
- P3: Nice to have, future consideration

---

## Competitor Feature Analysis

| Feature | AirServer | Reflector 4 | LonelyScreen | UxPlay (OSS) | AirShow |
|---------|-----------|-------------|--------------|--------------|-----------|
| AirPlay | Yes | Yes | Yes | Yes | Yes (target) |
| Google Cast | Yes | Yes | No | No | Yes (target) |
| Miracast | Yes | Yes (Windows only) | No | No | Yes (target) |
| DLNA | No | No | No | No | Yes (target) |
| Linux support | No (embedded only) | No | No | Yes | Yes (target) |
| Free | No ($20+) | No ($15+) | No | Yes | Yes |
| Open source | No | No | No | Yes | Yes |
| Multi-device simultaneous | Yes | Yes | No | No | v1.x |
| Recording | Yes | Yes | No | No | Out of scope v1 |
| Connection approval | Yes | Yes | No | No | v1.x |
| PIN pairing | Yes | Partial | No | No | v1.x |
| Receiver name customization | Yes | Yes | No | Yes | Yes (target) |

**Gap analysis:** The combination of (AirPlay + Cast + Miracast + DLNA) + Linux + free + open source is unoccupied by any existing product. That is the differentiation.

---

## Protocol-Specific Feature Notes

### AirPlay
- AirPlay 1 (screen mirroring): Uses RAOP + additional mirroring stream over TCP/UDP; implemented by UxPlay and others; MEDIUM confidence this is achievable
- AirPlay 2 (multi-room audio): Different protocol; requires additional TLS certificate infrastructure; NOT needed for screen mirroring use case — AirPlay 1 mirroring is what iOS devices use for screen mirror
- FairPlay DRM: Blocks mirroring of DRM-protected content at the protocol level; document as known limitation

### Google Cast
- Uses TLS with device certificate signed by Google CA; open-source receivers (shanocast) exist and work without full Google cert by disabling authentication on the sender side — this is a known workaround; MEDIUM confidence
- Sender apps (Android Chrome, Chrome browser) are permissive about receiver cert validation in practice for local network receivers

### Miracast
- Wi-Fi Direct is NOT regular Wi-Fi; requires driver/kernel support; most challenging protocol on Linux
- MiracleCast and GNOME Network Displays are reference implementations; both are experimental quality
- May need to scope Miracast to Windows/macOS first and treat Linux Miracast as v2 work

### DLNA
- DLNA DMR (Digital Media Renderer) = media push, NOT live screen mirroring
- DLNA does not support live screen cast; it pushes media files from a DLNA server to a renderer
- Appropriate for "push video/audio file" use case; clearly documented distinction needed

---

## Sources

- [AirServer Overview](https://www.airserver.com/Overview) — official feature list
- [AirServer Features Analysis 2026](https://appmus.com/software/airserver)
- [Reflector 4 Features - Mac and Windows](https://www.airsquirrels.com/reflector/features/mac-and-windows) — official feature list
- [Introducing Reflector 4](https://blog.airsquirrels.com/introducing-new-reflector-4-most-powerful-screen-mirroring-streaming-receiver-yet)
- [LonelyScreen vs AirServer comparison](https://appmus.com/vs/lonelyscreen-vs-airserver)
- [AirServer Alternatives - AlternativeTo](https://alternativeto.net/software/airserver/)
- [UxPlay - AirPlay Unix mirroring server](https://github.com/antimof/UxPlay)
- [Shanocast - Google Cast receiver OSS](https://github.com/rgerganov/shanocast)
- [MiracleCast - Miracast implementation](https://github.com/albfan/miraclecast)
- [OpenWFD - Open Source WiFi-Display](https://www.freedesktop.org/wiki/Software/openwfd/)
- [GNOME Network Displays - Miracast for GNOME](https://github.com/benzea/gnome-network-displays)
- [AirPlay Service Discovery spec](https://openairplay.github.io/airplay-spec/service_discovery.html)
- [Screen Mirroring Protocols Guide 2025](https://www.airdroid.com/screen-mirror/screen-mirroring-protocols/)
- [DLNA Wikipedia](https://en.wikipedia.org/wiki/DLNA)
- [Reflector vs AirServer comparison](https://www.airsquirrels.com/reflector/resources/reflector-vs-airserver)
- [Screen Mirroring Security - Kingshiper](https://www.kingshiper.com/screen-mirroring/is-screen-mirroring-safe.html)
- [Building Cross-Platform Wireless Display Solutions 2025](https://www.blog.brightcoding.dev/2025/12/31/the-ultimate-developer-guide-building-cross-platform-wireless-display-solutions-with-airplay-miracast-google-cast-sdks/)

---
*Feature research for: Cross-platform screen mirroring receiver (AirShow)*
*Researched: 2026-03-28*
