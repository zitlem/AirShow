# Pitfalls Research

**Domain:** Cross-platform screen mirroring receiver (AirPlay, Google Cast, Miracast, DLNA) + v2.0 Flutter sender app
**Researched:** 2026-03-28 (receiver) / 2026-03-30 (sender append)
**Confidence:** HIGH (critical pitfalls verified against open-source projects and official documentation; Miracast/DLNA claims MEDIUM due to sparse recent sources)

---

## Critical Pitfalls

### Pitfall 1: Apple Protocol Fragility — Unofficial Implementations Break on iOS Updates

**What goes wrong:**
Apple changes AirPlay handshake and pairing details in iOS/iPadOS security patches without notice. Third-party receivers built on reverse-engineered protocol knowledge stop working — sometimes for all users, sometimes only for users who updated. iOS 18.4 and iPadOS 26 both introduced pairing changes that broke multiple commercial and open-source AirPlay receivers. AirPlay protocol details are not published, so there is "no assurance that [any open-source AirPlay receiver] will continue to work in future."

**Why it happens:**
The protocol is proprietary and undocumented. All third-party implementations (UxPlay, RPiPlay, shairport-sync, Reflection, AirServer itself historically) rely on reverse engineering. Apple occasionally ships security patches that alter the pairing or authentication flow as a side effect. Receivers have no advance notice and no official changelog to check.

**How to avoid:**
- Isolate the AirPlay protocol layer behind a clean interface so it can be replaced/patched without rewiring the rest of the app.
- Monitor the UxPlay and shairport-sync GitHub issue trackers as an early-warning system — they are the most active open-source receivers and will surface breakage within hours.
- Pin to a known-good version of the underlying RAOP/AirPlay library; do not auto-upgrade to untested upstream changes.
- Write automated integration tests using a real iOS device so breakage is caught in CI rather than via user reports.

**Warning signs:**
- Sudden spike in "not showing up in AirPlay menu" or "connection refused" issues after an iOS release.
- shairport-sync or UxPlay GitHub showing open issues titled "broken after iOS X.Y".
- Apple releases a "Rapid Security Response" — these are the most likely to alter authentication flows without full release notes.

**Phase to address:**
AirPlay implementation phase. Architecture must enforce this boundary before any protocol code is written.

---

### Pitfall 2: Miracast Requires a Dedicated Wi-Fi Adapter (Kills Internet)

**What goes wrong:**
Standard Miracast uses Wi-Fi Direct (P2P), which creates a separate ad hoc wireless network. This means the computer's Wi-Fi adapter is consumed by the Miracast session, cutting off the device's internet connection for the duration. Users discover this after shipping and consider it broken behavior.

**Why it happens:**
Wi-Fi Direct is specified at the hardware/driver level. Most consumer PCs have one Wi-Fi adapter. When wpa_supplicant negotiates the P2P group, the same interface that was connected to the home router is repurposed. The alternative — "Miracast over Infrastructure" (using the existing network rather than P2P) — requires explicit Windows 10+ policy configuration and is not universally supported on the sender side.

**How to avoid:**
- Default to "Miracast over Infrastructure" where available (Windows 10 version 2004+ senders support this).
- Document clearly that standard Miracast requires a dedicated Wi-Fi adapter or a USB Wi-Fi dongle, or that internet will be interrupted.
- Consider making Miracast optional or a later phase — evaluate whether the use case justifies the complexity vs. the reliable network-based protocols (AirPlay, Cast, DLNA).
- If implementing, test on systems with exactly one Wi-Fi adapter, not developer machines that often have two.

**Warning signs:**
- "Miracast disconnects when I open a browser" bug reports.
- CI/test environment works (multiple interfaces) but user machines fail.
- Linux wpa_supplicant logs showing P2P group formation consuming the primary interface.

**Phase to address:**
Miracast implementation phase. Must be evaluated in architecture phase to decide whether to include at all for v1.

---

### Pitfall 3: mDNS/Bonjour Discovery Fails Silently When Receiver Is Invisible

**What goes wrong:**
The receiver never appears in the sender's AirPlay, Cast, or Miracast menu. The user sees no error — the receiver is just absent. This is the single most common user-facing issue across all protocols. UDP port 5353 blocked, avahi-daemon not running, OS firewall prompting at the wrong time, or multicast being filtered by the router all produce the same invisible-receiver symptom.

**Why it happens:**
Each protocol uses a different discovery mechanism — AirPlay uses mDNS/Bonjour (UDP 5353), Google Cast uses mDNS and SSDP (UDP 1900), Miracast uses Wi-Fi Direct probe requests. None of these are TCP traffic that firewalls commonly "allow all outbound." OS-level firewalls (Windows Defender, macOS Application Firewall) prompt the user on first run; if they dismiss or deny, the receiver becomes invisible with zero indication of why. The prompt appears early in the process and is easy to miss.

**How to avoid:**
- Request firewall exceptions at install time with clear user-facing dialogs — do not rely on the OS's default firewall prompt.
- On Windows, use the Windows Firewall API to register rules programmatically during installation for all required ports (UDP 5353, UDP 1900, TCP 7000, TCP 7100, TCP 8008, TCP 8009, TCP 7236, UDP 32768–61000 range for RTP).
- On macOS, include an entitlements plist for the necessary network capabilities and register Bonjour services via the standard NSNetService/dns-sd APIs so the firewall exception is pre-authorized.
- On Linux, detect the active firewall tool (ufw, firewalld, iptables) at startup and emit a specific actionable error message if ports are blocked, rather than silently degrading.
- Show a "receiver not visible? Check firewall" diagnostic in the UI.

**Warning signs:**
- "Not showing in AirPlay menu" is the first bug report category for any new install.
- Testing works on developer machine (firewall off) but fails for users (firewall on).
- avahi-daemon systemctl status shows stopped.

**Phase to address:**
Core infrastructure / first working milestone. Discovery must work before any mirroring feature is useful.

---

### Pitfall 4: Audio/Video Sync Drift With Naive Pipeline Design

**What goes wrong:**
Video and audio arrive on separate network streams, are decoded by independent pipeline stages, and drift apart over time. The result is lips out of sync with speech, or a growing desync that worsens over a long session. AirPlay mandates a ~2 second audio buffer latency by design; naively applying the same delay to video (or no delay to audio) produces persistent offset from frame 1.

**Why it happens:**
AirPlay sources set a latency of 2.0–2.25 seconds for audio, while AirPlay 2 can use ~0.5 seconds. The NTP clock synchronization used for AV sync (AirPlay sends NTP requests to the client on port 7010) requires the receiver to maintain a high-resolution local clock reference and map it to both audio and video presentation timestamps. Any pipeline that uses GStreamer's `sync=false` or decoupled `appsrc` sinks without a shared clock will drift.

**How to avoid:**
- Use a single GStreamer pipeline clock shared between the audio and video branches, not two independent pipelines.
- Do not set `sync=false` on video sink in production — this disables clock-based presentation and trades sync correctness for reduced frame drops.
- Implement the AirPlay NTP clock recovery (port 7010) correctly from day one; do not stub it out as a later task.
- Add an integration test that plays a sync-marked video (clapperboard equivalent) and measures audio/video offset — must be under 50ms.

**Warning signs:**
- "Audio and video not in sync" reported within the first minutes of testing any new build.
- Using `sync=false` in the GStreamer pipeline as a quick fix for dropped frames.
- Dropped-frame complaints only appear during long sessions (>10 min), suggesting clock drift rather than throughput problem.

**Phase to address:**
AirPlay implementation phase and general media pipeline design phase. The shared-clock architecture must be established before any protocol-specific decoding work.

---

### Pitfall 5: Hardware H.264/H.265 Decoding Assumed but Not Available

**What goes wrong:**
The app ships using GStreamer's `autovideosink` and `decodebin`, which selects the "best available" decoder at runtime. On developer machines with discrete GPUs and full driver stacks, this silently picks hardware decoding (vaapi, nvdec, v4l2). On end-user machines — especially Linux users with open-source drivers, ARM boards, or older integrated graphics — it falls back to software decoding, producing unacceptable latency and CPU usage. HEVC/H.265 hardware decoding is even less reliably available.

**Why it happens:**
GStreamer's automatic decoder selection is opaque. The `decodebin` element negotiates caps at runtime with no easy way for the application to know which decoder was selected or to surface degraded performance to the user. The difference in CPU usage between `avdec_h264` (software) and `vaapih264dec` (hardware) can be 10x. On Raspberry Pi 5, GPU H.264 support was removed entirely; software decoding on the Pi 5 is inadequate for 1080p mirroring.

**How to avoid:**
- Log the actual decoder element selected at startup and surface it in a diagnostic/about screen.
- Provide explicit fallback behavior: attempt hardware decoder, detect sustained high CPU usage or frame drops, warn the user and optionally switch to software.
- Test the full matrix: Linux (vaapi, nvdec, v4l2, software), macOS (VideoToolbox), Windows (MF/DXVA). Each has a different best-path decoder name.
- For H.265/HEVC: treat hardware decoding as a bonus, not a requirement. AirPlay mirroring primarily sends H.264; H.265 is used for higher resolution streams but has patchy hardware support.
- Document minimum hardware requirements honestly based on software-decode benchmarks.

**Warning signs:**
- All CI tests pass but users on cheap laptops report "extremely laggy mirroring."
- CPU pegged at 100% on a single core during mirroring while GPU metrics show 0% utilization.
- GStreamer pipeline logs showing `avdec_h264` on a machine that should be using vaapi.

**Phase to address:**
Media pipeline architecture phase. Must be designed to be hardware-path-explicit rather than relying on auto-selection.

---

### Pitfall 6: DRM-Protected Content Silently Breaks Mirroring

**What goes wrong:**
When users try to mirror DRM-protected content from Netflix, Apple TV+, or Disney+ via AirPlay mirroring, the video stream is either blank, replaced with a black frame, or the sender refuses to start mirroring entirely. Users blame the receiver app. This is not a bug — it is by design — but the experience is confusing and generates support noise.

**Why it happens:**
AirPlay uses FairPlay encryption for DRM content. FairPlay Streaming "has not been made available for licensing on 3rd party platforms." The sender device detects that the receiver is not an Apple-certified device and either refuses the session or sends an encrypted stream the receiver cannot decrypt. There is no way to implement DRM decryption in an open-source receiver without violating Apple's licensing terms.

**How to avoid:**
- Document clearly in README and in-app UI that DRM-protected content (streaming apps) cannot be mirrored — only screen mirroring from the iOS/macOS screen itself works.
- Show an informative error message when the sender signals a DRM refusal rather than silently showing a black window.
- Test with both DRM and non-DRM content during QA to ensure the error path is handled and not confused with a real bug.

**Warning signs:**
- "Works with YouTube but not Netflix" bug reports.
- Black window with no error message.
- Sender-side "This content cannot be AirPlayed" system alert appearing but receiver showing as if connecting.

**Phase to address:**
AirPlay implementation phase. Error handling for DRM refusal must be part of the initial protocol implementation, not deferred.

---

### Pitfall 7: Single Protocol Abstraction Breaks Under Multi-Protocol Load

**What goes wrong:**
The first protocol (e.g., AirPlay) is implemented directly against the UI layer. When Google Cast is added, the differences in session lifecycle, stream format, and discovery model require either duplicating logic or retrofitting an abstraction. By the third protocol, the codebase has branched into protocol-specific silos, making cross-protocol features (audio mute, window resizing, status display) require changes in three places.

**Why it happens:**
Protocols differ fundamentally: AirPlay is push-based with H.264+AAC streams; Google Cast uses a receiver-hosted HTTP server model; Miracast is Wi-Fi Direct + RTP; DLNA is UPnP + HTTP media push. Treating them uniformly from the start feels premature — there is a temptation to ship the first protocol "working" and design the abstraction later.

**How to avoid:**
- Define a `MirroringSession` interface before writing any protocol code: `start()`, `stop()`, `getVideoStream()`, `getAudioStream()`, `onDisconnect()`.
- Define a `ReceiverAdvertiser` interface: `advertise(name, capabilities)`, `stop()`.
- Implement AirPlay against these interfaces. Adding Google Cast then becomes filling in a second implementation rather than a refactor.
- The UI layer must never directly reference protocol-specific types.

**Warning signs:**
- Protocol-specific constants or type names (e.g., `AirPlaySession`) appear in UI code.
- Adding a mute toggle requires changes to more than two files.
- The video renderer pipeline is duplicated per protocol.

**Phase to address:**
Architecture phase, before any protocol implementation. This is the single most important structural decision.

---

## v2.0 Companion Sender — Critical Pitfalls

*Added 2026-03-30. Covers Flutter-based cross-platform sender app that captures and streams device screen to an AirShow receiver.*

---

### Sender Pitfall 1: Android MediaProjection — Consent Cannot Be Cached or Reused (API 34+)

**What goes wrong:**
The app caches the `Intent` returned by `MediaProjectionManager.createScreenCaptureIntent()` and reuses it across sessions (or across app restarts). On Android 14 (API 34) and above this throws a `SecurityException` immediately. Developers who test only on Android 13 or below ship code that crashes on the majority of current devices.

**Why it happens:**
Android 14 changed the consent model: each `MediaProjection` instance is single-use, and each `createVirtualDisplay()` call consumes the token. The old pattern of persisting the result `Intent` for reuse was common in pre-Android-14 code and is still found in many tutorials and Stack Overflow answers written before the 2023 behavior change. The flutter-webrtc package historically had this exact bug (GitHub issue #1521).

**How to avoid:**
- Always call `createScreenCaptureIntent()` fresh before each new capture session — never persist the result across sessions.
- Follow the mandatory API 34+ lifecycle: `createScreenCaptureIntent()` → user grants → start foreground service of type `mediaProjection` → retrieve token → call `createVirtualDisplay()` once per token.
- Declare in `AndroidManifest.xml`: `android.permission.FOREGROUND_SERVICE_MEDIA_PROJECTION` and foreground service type `mediaProjection`.
- Test on a physical or emulated Android 14+ device before any release, not just the developer's Android 12 device.

**Warning signs:**
- `SecurityException: Media projections require a foreground service` crash log on Android 14+ devices.
- Screen sharing works for the first session but crashes on reconnect.
- `flutter-webrtc` dependency version older than the fix for issue #1521 and #1813.

**Phase to address:**
Android screen capture phase (sender MVP). Must be correct from the first implementation — the wrong architecture here requires rewriting the capture lifecycle.

---

### Sender Pitfall 2: iOS ReplayKit Broadcast Extension — 50 MB Hard Memory Limit

**What goes wrong:**
The Broadcast Upload Extension (the out-of-process component required for iOS system-wide screen recording) is killed by the OS when it exceeds 50 MB of memory. On iPad (large resolution frames) and on any device during launch of another app during broadcast, ReplayKit itself can spike to 8 concurrent CMSampleBuffer references in the extension process, pushing total usage over the limit. The broadcast silently stops and the user sees no error — the countdown timer on "Start Broadcast" simply disappears.

**Why it happens:**
iOS extensions have hard memory budgets that are stricter than the main app. ReplayKit delivers raw BGRA frames at full display resolution. A single 2560×1600 iPad frame at 32bpp = ~16 MB. With buffering this exhausts 50 MB in milliseconds unless the extension immediately downscales or encodes to H.264 before buffering. Most developers assume the extension can hold a few frames for encoding; it cannot.

**How to avoid:**
- Downscale incoming `CMSampleBuffer` frames inside the extension before any other processing. Targeting 720p for streaming (not recording) is acceptable and keeps buffers manageable.
- Use VideoToolbox H.264 encoding directly inside the extension — encode immediately, do not accumulate raw frames.
- Prefer H.264 over VP8/VP9 in the extension — the Twilio iOS SDK explicitly documents H.264 as the memory-safe choice in broadcast extensions.
- Use an App Group (`group.com.yourapp`) to pass encoded NAL units from the extension to the main app via a shared memory region or `Darwin` notifications + `CMMemoryPool`. Do not use XPC with large payloads — XPC serialization contributes to peak memory.
- Test specifically on a 12.9" iPad Pro with at least one other app launching during broadcast.

**Warning signs:**
- `EXC_RESOURCE RESOURCE_TYPE_MEMORY (limit=50MB)` crash in broadcast extension crash logs.
- "Start Broadcast" countdown completes on screen but broadcast immediately stops (no user-visible error).
- Broadcast works reliably on iPhone but crashes on iPad.

**Phase to address:**
iOS screen capture phase (sender MVP). The extension architecture must encode immediately — a later refactor to add encoding to an already-shipping raw-buffer extension means redesigning IPC and the extension memory model from scratch.

---

### Sender Pitfall 3: macOS TCC Screen Recording — Signing Identity Determines Permission Persistence

**What goes wrong:**
During development, the app is rebuilt frequently. On macOS, TCC (Transparency, Consent and Control) tracks screen recording permission by code-signing identity hash. Every fresh build with an ad-hoc signature (`codesign -s -`) produces a new identity, and TCC treats it as a new, unknown app — resetting the permission. Developers end up constantly re-granting permission in System Settings and cannot reproduce user-facing permission prompts. When the app ships with a real Developer ID, permission persists — but if the release cert changes between releases (e.g., a certificate renewal), all existing users lose their stored permission.

**Why it happens:**
TCC binds grants to the `TeamIdentifier` entitlement (from the code-signing certificate's Team ID). Ad-hoc builds have no Team ID, so each build is distinct. This is documented behavior but is easy to miss because macOS prompts for permission on first launch, and developers interpret the prompt as "working."

**How to avoid:**
- Sign all builds (including debug builds) with a stable Developer ID certificate from the start.
- Use `ScreenCaptureKit` (not the deprecated `CGDisplayStream`) — Apple will add additional consent alerts for legacy APIs in future macOS versions.
- Test the permission grant flow in a sandboxed clean VM, not on the developer's machine where permission was already granted.
- Include `NSScreenCaptureUsageDescription` in `Info.plist` with a clear, honest reason string — vague descriptions increase user rejection rates.
- Never rely on `CGPreflightScreenCaptureAccess()` as a gate without also calling `CGRequestScreenCaptureAccess()` — the preflight return value does not match permission state on first launch.

**Warning signs:**
- "Works on my machine" but users report no permission prompt (permission was never requested correctly).
- macOS 15 Sequoia or later showing repeated permission re-prompts — this is a known issue with apps that rebuild or update certificates without migrating TCC entries.
- `CGWindowListCreateImage` returning nil with no error code.

**Phase to address:**
macOS screen capture phase (sender MVP). Signing strategy must be established before the first macOS screen capture test, not at release time.

---

### Sender Pitfall 4: Windows Screen Capture — Secure Desktop and Elevation Boundaries

**What goes wrong:**
On Windows, the `Windows.Graphics.Capture` API (the modern DXGI-based approach) cannot capture content from the UAC secure desktop, any window running at a higher elevation (UAC prompt, elevated processes), or protected content rendered via Protected Media Path (PMP). Attempts to capture these surfaces produce blank frames with no error, which users report as "screen sharing is showing a black screen."

A second common failure: when the sender app is built as a standard Win32 app, certain GPU-accelerated frames from DirectX 12 applications are not delivered to the `FrameArrived` callback correctly — the callback fires only as many times as the declared buffer count (typically 2), then stops.

**Why it happens:**
Windows isolates elevated and secure desktop contexts at the kernel level. The `Windows.Graphics.Capture` API explicitly documents that it cannot capture content from a higher integrity level than the capturing process. DirectX 12 and the capture API have a known interop bug with `D3D11On12CreateDevice`.

**How to avoid:**
- Use `Windows.Graphics.Capture` (WinRT API, requires Windows 10 build 1803+) as the primary capture path — it covers the broadest set of surfaces and shows the user a capture consent indicator (yellow border).
- Accept that UAC dialogs and elevated windows cannot be captured — document this clearly rather than attempting workarounds that require admin elevation (which creates a worse UX problem).
- For DirectX 12 application capture, fall back to DXGI `OutputDuplication` API as a secondary path — it handles the DX12 interop case that `Windows.Graphics.Capture` misses.
- The consent indicator (yellow border) cannot be removed for desktop apps — do not attempt to suppress it. UWP apps can suppress it but Flutter builds as a Win32 desktop app.

**Warning signs:**
- Black frames captured when a UAC-elevated window is frontmost.
- Frame capture freezes after exactly N frames where N equals the declared buffer count (DX12 interop bug).
- Users asking "how to remove the yellow border" — the border is expected and intentional.

**Phase to address:**
Windows screen capture phase (sender MVP). The dual-path architecture (WinRT primary + DXGI fallback) should be decided before implementation, not retrofitted when specific apps fail.

---

### Sender Pitfall 5: H.264 Encoding — Color Format Mismatch Causes Corrupted or Tinted Frames

**What goes wrong:**
Android's `MediaCodec` H.264 encoder input color format varies by device manufacturer and SoC. Qualcomm Adreno chips prefer `COLOR_FormatYUV420SemiPlanar` (NV12), Nvidia Tegra prefers `COLOR_FormatYUV420Planar` (I420), and some TI OMAP chips require a vendor-specific `TI_PackedSemiPlanar` format. Feeding the wrong format produces corrupted output with green/pink tints, inverted chroma, or encoder exceptions. This issue does not affect the developer's device but affects a subset of user devices.

**Why it happens:**
The Android CDD requires encoders to support `COLOR_FormatYUV420Flexible` as a safe universal input, but many hardware encoders from 2018–2022 devices only reliably accept their native format. The flexible format is a caps declaration, not a guarantee of correct behavior. Additionally, `MediaCodecInfo.getCapabilitiesForType()` can take up to 5 seconds on some vendor implementations, making capability-querying at startup impractical.

**How to avoid:**
- Use the `Surface` input API (`MediaCodec.createInputSurface()`) instead of direct buffer input. The encoder internally handles color format conversion when using a Surface as input. This is the recommended approach for `MediaProjection` → `VirtualDisplay` → encoder pipeline.
- Never feed raw pixel buffers from `VirtualDisplay` directly into the encoder buffer input unless you have verified the color format matches at runtime.
- Set explicit encoder parameters: H.264 High Profile (not Baseline), level 4.1, keyframe interval ≤ 2 seconds.
- On the receiver side, ensure GStreamer's `rtph264depay` can handle both Annex B and AVCC NAL unit format — Android encoders output AVCC (length-prefixed) not Annex B (start-code prefixed), which surprises many GStreamer pipelines.

**Warning signs:**
- Green/pink tint on captured frames only on specific device models.
- Encoder crashes with `IllegalStateException` at `queueInputBuffer` on specific SoCs.
- Receiver's GStreamer pipeline logs showing `h264parse` errors about invalid start codes.

**Phase to address:**
Android encoding phase (sender MVP). Surface input API must be the first-choice architecture — retrofitting from buffer input to Surface input later requires redesigning the MediaProjection VirtualDisplay attachment.

---

### Sender Pitfall 6: Flutter Platform Channels Are Too Slow for Per-Frame Video Data

**What goes wrong:**
The developer routes encoded H.264 NAL units (or worse, raw pixel frames) through a Flutter `MethodChannel` or `EventChannel` to send them to the Dart networking layer. At 30fps × 1080p, even compressed H.264 data is ~1–4 MB/s with per-frame invocations. The `MethodChannel` serialization overhead, combined with thread-switching between the platform and Flutter UI thread, causes frame queuing to back up within seconds and the sender lags visibly behind the screen.

**Why it happens:**
Flutter's default `MethodChannel` was designed for infrequent method calls (UI events, settings reads), not high-frequency data streaming. Scheduling replies on the UI thread from the platform thread is documented as "massively slow" when payload size grows. Until Flutter 3.32, the platform thread and UI thread were separate — message scheduling between them added latency that scaled with payload. As of Flutter 3.32 (stable), iOS and Android merge these threads by default, which reduces but does not eliminate the overhead.

**How to avoid:**
- Do NOT pass video frame data through `MethodChannel` or `EventChannel`. Use one of these alternatives:
  1. **FFI + shared memory**: Allocate a native shared memory buffer; pass only a pointer/offset through a lightweight FFI call. The Dart side reads directly from native memory without copy.
  2. **Native socket**: Encode on the platform side and send directly over a native UDP/TCP socket. Dart only handles metadata (session state, timing). This is the cleanest architecture for a sender app.
  3. **`flutter_webrtc` plugin**: If using WebRTC as the transport, let the WebRTC native layer handle encoding and network sending entirely — Dart code only controls session setup.
- If platform channel is unavoidable for any data path, use `BasicMessageChannel` with `BinaryCodec` (not `StandardMessageCodec`) to minimize serialization overhead.

**Warning signs:**
- Dart `EventChannel` listener receiving frames with increasing lag from wall clock.
- `PlatformException` timeouts or dropped messages under load.
- Native profiler shows platform thread blocked waiting for Flutter UI thread to process a message.

**Phase to address:**
Flutter architecture phase (before any sender data path is implemented). The data flow architecture — native-handles-media, Dart-handles-control — must be established in the design phase and never reversed.

---

### Sender Pitfall 7: mDNS Discovery Broken by Access Point Client Isolation

**What goes wrong:**
The sender app discovers no AirShow receivers on the network, even though the receiver is running and confirmed visible on the same subnet. The failure is silent — the discovery UI shows "searching..." indefinitely. This happens when the Wi-Fi access point has "client isolation" (also called "AP isolation" or "wireless isolation") enabled, which is a common default on guest networks, corporate Wi-Fi, and some consumer routers.

**Why it happens:**
mDNS uses link-local multicast (`224.0.0.251:5353`). Client isolation prevents stations on the same AP from communicating directly at Layer 2, which silently drops all multicast traffic between devices. The devices are on the same IP subnet but cannot exchange multicast packets. This is identical to the failure mode that affects AirPlay, Chromecast, and Matter — it is not specific to AirShow.

**How to avoid:**
- Test the sender on both a home router (usually no isolation) and a corporate/guest network (usually has isolation) — the receiver will be invisible on the latter.
- Add a fallback discovery mechanism: allow the user to manually enter the receiver's IP address and port when mDNS fails.
- In the sender UI, show a specific "Check that your phone and computer are on the same Wi-Fi network, and that the network allows device-to-device communication" message after 10 seconds of no discovery — not a generic spinner.
- Consider implementing DNS-SD unicast fallback (RFC 6762 §11) for networks where multicast is blocked but unicast is not.

**Warning signs:**
- Sender finds receivers in the developer's home but never on office or hotel Wi-Fi.
- `dig _airshow._tcp.local` from the device returns no results but the receiver is definitely running.
- Users with enterprise Wi-Fi or Ubiquiti UniFi gear with "Block LAN to WLAN multicast and broadcast data" enabled.

**Phase to address:**
Sender discovery phase. The manual IP fallback must ship alongside mDNS discovery — it is not a "nice to have" for a real-world product.

---

### Sender Pitfall 8: iOS App Extension Cannot Directly Call Main App Network Stack

**What goes wrong:**
The developer writes all networking code in the main app and assumes the Broadcast Upload Extension can call into it. It cannot. The extension runs in a completely separate process with no shared memory, no shared sockets, and no ability to call functions in the main app directly. All communication between the extension (which receives screen frames) and the main app (which manages the network connection to the receiver) must go through an approved IPC mechanism.

**Why it happens:**
iOS extensions are designed for isolation and security. Extension processes have separate address spaces, separate entitlements, and shorter lifetimes than the host app. Developers accustomed to Android's service model (where the app and service share a process or communicate easily via `Binder`) underestimate how different the iOS extension model is. The XPC calls used for IPC are asynchronous and have latency — naive use adds per-frame round-trips to the extension-to-app data path.

**How to avoid:**
- Use an App Group container (`group.com.yourapp.airshow`) with `CFMessagePort` or Darwin notification + shared `CFData` in shared user defaults to pass encoded H.264 NAL units from the extension to the main app.
- Alternatively, have the extension open its own network socket to the receiver directly (the extension can make outbound network connections) — this eliminates the main-app IPC entirely and is the cleanest architecture.
- Keep the main app running in the foreground during screen broadcast (show the AirShow sender UI) — if the main app is backgrounded and then suspended, the extension loses its IPC target.
- Avoid using `NSXPCConnection` with large serialized payloads — as confirmed by the memory spike crash reports, XPC deserialization contributes to the 50 MB extension memory limit being hit.

**Warning signs:**
- Broadcast starts but the receiver receives no data.
- Extension crash logs showing `NSXPCDecoder` memory pressure.
- Main app shows "connecting" while extension is running — main app was not informed of extension state changes.

**Phase to address:**
iOS screen capture phase (sender MVP). The IPC architecture between extension and main app must be designed and tested before any networking code is written — retrofitting the extension-to-app data path is one of the most disruptive sender refactors possible.

---

### Sender Pitfall 9: H.264 Keyframe Interval and NAL Unit Format Mismatch with Receiver

**What goes wrong:**
The sender emits H.264 with infrequent IDR (keyframe) intervals (e.g., 10–30 seconds as the encoder default). When the receiver connects mid-stream, or when a single packet is lost, it cannot decode any frames until the next IDR arrives. The receiver shows a frozen or corrupted display for up to 30 seconds with no feedback. Additionally, Android encoders output AVCC-format NAL units (4-byte length prefix) while GStreamer's `rtph264depay` expects Annex B (3/4-byte start codes `00 00 00 01`) — failing to convert produces parser errors with no useful diagnostics.

**Why it happens:**
Encoder default IDR intervals are optimized for file recording (fewer IDRs = better compression), not live streaming (every IDR is a recovery point). The AVCC vs. Annex B difference is a perennial gotcha in H.264 interop: Android MediaCodec, Apple VideoToolbox, and browser WebRTC all use AVCC internally; RTP packetization and GStreamer pipelines use Annex B. The conversion is one line of code but developers unfamiliar with H.264 framing do not know to look for it.

**How to avoid:**
- Set IDR interval to 1–2 seconds (30–60 frames at 30fps) on the encoder side. This is a property on both Android `MediaFormat` (`KEY_I_FRAME_INTERVAL`) and iOS `VideoToolbox` (`kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration`).
- Convert AVCC to Annex B before RTP packetization: replace the 4-byte NALU length prefix with `00 00 00 01` start codes. This is ~10 lines of C++ and must happen before the data reaches GStreamer.
- Use H.264 Baseline or High Profile, Level 4.1. Do not use High Profile Level 5.0+ — older Android decoder chips do not support it.
- On the receiver side, add `h264parse ! avdec_h264` as an explicit fallback parser so GStreamer does not silently drop malformed NAL units.

**Warning signs:**
- Receiver shows a still/frozen frame for the first several seconds after connection.
- GStreamer logs showing `h264parse: Invalid NALU start code` or `rtph264pay: Could not find start code`.
- Receivers connect and disconnect repeatedly in a loop — symptom of the receiver failing to decode and timing out.

**Phase to address:**
Sender encoding phase and receiver AirShowHandler integration phase. The IDR interval and NAL format conversion must be in the initial implementation spec — they cannot be left as "later optimization."

---

### Sender Pitfall 10: Flutter Desktop Screen Capture Lacks a First-Party API

**What goes wrong:**
Flutter does not provide a built-in screen capture API for desktop platforms (Windows, macOS, Linux). Third-party packages (`screen_capturer`, `desktop_screen_recorder`) exist but are community-maintained with inconsistent update cadence and no Flutter team support commitment. Building a real-time sender on Linux with these packages hits gaps: no audio capture, limited framerate control, no way to capture a specific window (only full-screen), and no hardware encoding pipeline integration.

**Why it happens:**
Flutter's desktop support is still maturing as of 2026. The integration testing framework itself does not support screenshot capture on desktop, indicating that screen capture was not a priority use case for the Flutter team. The packages that exist were built for screenshot/recording use cases, not for continuous low-latency capture feeding a network stream.

**How to avoid:**
- On desktop platforms (Windows, macOS, Linux), implement screen capture entirely in the platform-native layer (C++ or Swift/Obj-C/Win32) and expose only session control to Flutter via platform channel. The native layer owns the capture loop, encoding, and network send.
- Do not try to pull frames into Flutter/Dart for processing — the frame data never leaves the native layer.
- On Linux, use PipeWire (via `xdg-desktop-portal`) for Wayland-compatible screen capture. GStreamer has a `pipewiresrc` element that integrates this natively. Do not use `XCopyArea`/X11 capture as a primary path — it does not work under Wayland.
- On Windows, use `Windows.Graphics.Capture` WinRT API (available from the existing C++ receiver codebase).
- On macOS, use `ScreenCaptureKit` (macOS 12.3+).

**Warning signs:**
- "Screen capture works on X11 but not Wayland" reports — Wayland forbids direct framebuffer access.
- Frame rate dropping to 5–10fps under moderate system load when using a polling-based Flutter package.
- Package version pinned to an old version because the newer version broke the API (community maintenance instability).

**Phase to address:**
Desktop sender architecture phase. The decision to keep capture in native code (not Flutter/Dart) must be made before any desktop capture work begins.

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| `sync=false` on GStreamer video sink | Eliminates dropped frames immediately | Permanent audio/video desync | Never in production; acceptable only in early decoding spike |
| Hardcode GStreamer pipeline string | Gets video on screen fast | Breaks on systems without vaapi; no fallback | Only in a single-platform prototype |
| Implement AirPlay directly in UI event loop | Simplest first milestone | Blocks UI during network operations; unextractable later | Never — async from day one |
| Skip mDNS conflict resolution (duplicate receiver name) | Avoids edge-case complexity | Two instances of the app on the same LAN collide | Defer until multi-instance support is a requirement |
| Use only legacy AirPlay (not AirPlay 2) | Known, documented protocol | Newer iOS features (multi-room audio, spatial audio) unavailable; may be deprecated | Acceptable for v1 screen mirroring — screen mirroring does not require AirPlay 2 |
| Bundle FFmpeg instead of GStreamer | Simpler static binary distribution | Missing GStreamer plugin ecosystem; no pipeline composability for future protocols | Acceptable only if GStreamer integration complexity blocks shipping |
| Route video frames through Flutter MethodChannel | Simpler architecture on paper | Backpressure and frame lag at any practical frame rate | Never for video data — only for session control messages |
| Reuse Android MediaProjection token across sessions | Avoids re-prompting user | SecurityException crash on Android 14+; all modern devices affected | Never — re-consent is required by design |
| Raw buffer input to Android MediaCodec | Works on developer device | Color format mismatch crashes on ~20% of real device models | Never — use Surface input API |
| Skip IDR keyframe tuning, use encoder default | No extra work | Receiver stalls for up to 30 seconds on connect or packet loss | Never for a live streaming sender |

---

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| Avahi (mDNS on Linux) | Assume avahi-daemon is running; crash or hang when it isn't | Check at startup; emit actionable error: "Install avahi-daemon and ensure it is running" |
| Bonjour (mDNS on macOS/Windows) | Use Avahi directly on macOS | Use `dns-sd` / `DNSServiceRegister` API on macOS; Bonjour SDK on Windows; Avahi only on Linux |
| GStreamer plugin loading | Link against GStreamer at compile time, assume plugins present | Use `gst_registry_check_feature_version()` at runtime; report missing plugins by name to user |
| NTP clock sync (AirPlay port 7010) | Ignore NTP; use system time for AV sync | Must implement AirPlay NTP reply correctly; it is the AV sync source, not system clock |
| Google Cast TLS | Use plain TCP for Cast protocol | Cast protocol requires TLS even on local network — use self-signed cert with proper ALPN |
| SSDP (DLNA/UPnP discovery) | Use unicast for SSDP | SSDP discovery uses multicast to `239.255.255.250:1900`; unicast SSDP is a different flow |
| Miracast RTSP | Assume standard RTSP library works | Miracast's RTSP is a modified subset — standard RTSP libraries may reject valid Miracast RTSP messages |
| Android MediaProjection + MediaCodec | Use buffer input API for encoder | Always use Surface input: `VirtualDisplay` → `MediaCodec.createInputSurface()` — avoids color format problems |
| iOS Broadcast Extension ↔ Main App | Call main app functions directly from extension | Extension is a separate process — use App Group shared container or open a direct socket from the extension |
| macOS TCC permissions | Grant permission in dev, ship without testing fresh grant | Sign all builds with stable Developer ID; test TCC flow on a VM where permission was never granted |
| Flutter MethodChannel for video data | Treat channels like a Unix pipe | Channels are for infrequent control messages — video data must stay in native layer (FFI or direct socket) |

---

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Software H.264 decoding on main thread | UI freezes during mirroring; CPU 100% | Decode in dedicated thread; use hardware decoder where available | Immediately on 1080p@60fps streams |
| Re-creating GStreamer pipeline per session | 2–5 second black screen on reconnect | Build pipeline once at startup; manage with state machine (PAUSED ↔ PLAYING) | Every connection attempt |
| Allocating new frame buffers per decoded frame | Memory pressure, GC pauses (in managed runtimes) | Use buffer pools; reuse allocations across frames | After ~30 seconds at 60fps |
| mDNS re-announcement storm | Competing discovery beacons, sender gets confused | Implement TTL-based re-announcement (every 1 hour for AirPlay), not constant broadcasts | Not a threshold issue — wrong from first packet |
| RTSP session keepalive ignored | Miracast sender tears down after 60 seconds | Respond to RTSP OPTIONS keepalive within 30 seconds | Exactly 60 seconds into any Miracast session |
| Audio resampling in software for rate mismatch | CPU spike and audible glitches | Negotiate sample rate at session setup; use GStreamer `audioresample` only as last resort | Variable; worst on Windows with WASAPI at non-standard rates |
| Passing H.264 frames through Flutter MethodChannel | Dart event queue saturates within seconds; visible sender lag | Keep media data in native layer; Dart only controls session lifecycle | At any frame rate above ~5fps |
| iOS broadcast extension accumulating raw CMSampleBuffers | Extension process killed by OS; broadcast terminates | Encode immediately inside extension; never buffer more than 1–2 raw frames | When device memory pressure spikes (app launch, etc.) |
| TCP transport for live screen streaming | Latency spikes to seconds on any packet loss due to head-of-line blocking | Use UDP with application-layer loss tolerance; accept occasional corrupt frame over stalling | At ~2% packet loss on a typical Wi-Fi network |

---

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| Accepting AirPlay connections from any IP without pairing PIN | Any device on the LAN can mirror without consent | Implement AirPlay PIN pairing; show PIN on screen; allow user to configure "trusted devices" |
| Binding receiver to `0.0.0.0` (all interfaces) | Receiver accepts connections from internet-routed interfaces if user is on a corporate VPN | Default to binding only to LAN interface(s); detect and exclude VPN interfaces |
| Exposing UPnP/DLNA to internet-routed interfaces | CallStranger vulnerability (CVE-2020-12695): SSCP subscription can be weaponized for DDoS amplification | Bind SSDP/UPnP only to RFC1918 addresses; reject SUBSCRIBE requests with non-LAN callback URLs |
| Not validating AirPlay certificate pairing | AirBorne family of vulnerabilities (23 CVEs, 2025): unauthenticated RCE possible via malformed AirPlay packets | Implement all pairing/authentication steps from the AirPlay spec; do not skip authentication flows to simplify implementation |
| Storing device pairing tokens in plaintext | Leaked pairing token allows any device to auto-connect silently | Store tokens in OS keychain (Keychain on macOS, DPAPI/Credential Manager on Windows, libsecret on Linux) |
| Sender app capturing screen without explicit user consent | Privacy violation; app store rejection on iOS/Android | Always trigger the platform consent flow before first capture; never autostart capture in background |
| Android sender bypassing MediaProjection consent dialog | SecurityException on Android 14+; CVE-2025-32322 type bypass on older versions | Never cache or persist MediaProjection tokens; always prompt fresh per session |

---

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Receiver window appears before first frame is decoded | Black window with no feedback; user thinks app crashed | Show "Waiting for stream..." placeholder with device name; only go fullscreen on first keyframe |
| No feedback when discovery fails | User has no idea receiver is invisible; tries rebooting | Show persistent "visible on network as [Name]" status indicator; turn red with diagnostic link when mDNS is not working |
| Full-screen takeover without escape route | User cannot get back to desktop during presentation; panics | Always show a corner hint: "Press ESC to exit mirroring" |
| Receiver name collides with existing AirServer/Apple TV | User selects wrong device; confused why it "doesn't work" | Default name to `hostname (AirShow)`; allow rename in settings |
| No audio confirmation when stream starts | Silent black screen; user thinks mirroring is broken | Play a brief audio cue (or show a visible "Connected" banner) when stream is first received |
| Abrupt disconnect with no recovery | 1-second Wi-Fi hiccup kills session permanently | Implement automatic reconnection with 10-second grace window before declaring disconnect |
| Sender shows "searching..." indefinitely on AP-isolated network | User cannot diagnose why no receivers appear | After 10 seconds, show "No receivers found — check your Wi-Fi allows device communication, or enter IP manually" |
| iOS broadcast consent dialog appears without context | Users dismiss the system broadcast picker without selecting | Show in-app instructions before triggering the system picker: "Tap 'AirShow Sender' then tap 'Start Broadcast'" |

---

## "Looks Done But Isn't" Checklist

- [ ] **AirPlay discovery:** Test on a network where the macOS Bonjour proxy is active — iOS devices may see duplicate entries or stale entries from the proxy.
- [ ] **Google Cast:** Verify receiver appears in Chrome browser's Cast menu, not just Android — they use different discovery paths.
- [ ] **Audio mute:** Mute must suppress audio at the receiver, not tell the sender to stop sending — sender-side mute breaks sync on unmute.
- [ ] **Reconnect after sleep/wake:** Suspend the host machine, resume, and verify the receiver re-advertises within 5 seconds and accepts new connections.
- [ ] **Multiple simultaneous senders:** Second device attempting to connect while a session is active should receive a polite rejection, not crash the receiver.
- [ ] **Long session stability:** Run a 30-minute mirroring session and verify no memory leak, no AV drift accumulation, no RTSP keepalive timeout.
- [ ] **Firewall prompt (Windows):** Uninstall, reinstall, and verify the Windows Defender firewall prompt appears and that accepting it is sufficient — no manual rule creation needed.
- [ ] **DRM content:** Mirror the Netflix app from iOS — receiver should show a clear "DRM content cannot be mirrored" message, not a black window.
- [ ] **4K/60fps streams:** Test iPhone 14+ in screen mirroring at full resolution; verify decoder can sustain frame rate without backpressure starving the network buffer.
- [ ] **Android sender on API 34+:** Verify screen capture starts fresh each session and does not throw SecurityException on reconnect after initial permission grant.
- [ ] **iOS sender on iPad Pro 12.9":** Broadcast extension must not exceed 50 MB — test with a competing app launching mid-broadcast.
- [ ] **AP-isolated network:** Test sender discovery on a corporate or guest Wi-Fi — manual IP entry must work when mDNS is silently blocked.
- [ ] **AVCC to Annex B conversion:** Verify receiver GStreamer pipeline logs no `h264parse` errors when receiving frames from the Android sender.
- [ ] **macOS sender TCC after rebuild:** Grant permission, rebuild the app, verify permission persists (requires stable code-signing identity).
- [ ] **Windows sender on DX12 app:** Capture a DirectX 12 game or app — verify frames are not limited to the declared buffer count.

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| iOS update breaks AirPlay pairing | MEDIUM | Monitor shairport-sync/UxPlay issues for the delta; patch the RAOP authentication handshake; release hotfix within 48h of breakage reports |
| Miracast Wi-Fi conflict discovered post-ship | LOW | Document workaround (dedicated USB Wi-Fi adapter); implement "Miracast over Infrastructure" as a follow-up feature |
| Audio/video sync drift caused by missing NTP implementation | HIGH | Requires rearchitecting the clock source for both audio and video pipelines; do not defer NTP |
| GStreamer pipeline crashes on unsupported hardware decoder | LOW | Implement try/catch around pipeline construction; fall back to `avdec_h264` automatically; log the failure |
| mDNS firewall silently blocks discovery | LOW | Add a diagnostic startup check; surface specific port requirements in UI; release a patch with explicit firewall rule registration |
| Multi-protocol abstraction missing, protocol silos created | HIGH | Full refactor of protocol layer — estimated 2–4 weeks depending on protocol count; only gets harder with each new protocol added |
| Android sender uses cached MediaProjection token | MEDIUM | Rewrite capture lifecycle to always request fresh consent; regression-test on Android 14 emulator before each release |
| iOS extension exceeds 50 MB, broadcast silently stops | HIGH | Redesign extension to encode immediately (VideoToolbox H.264) and never buffer raw frames; requires complete extension rewrite |
| Flutter MethodChannel data path for video frames | HIGH | Migrate media data path to FFI shared memory or native socket; requires rewriting data flow across Dart/native boundary |
| AVCC/Annex B mismatch causes receiver parser errors | LOW | Add 10-line NAL unit reframing function before RTP packetization; no architecture change needed |

---

## Pitfall-to-Phase Mapping

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Apple protocol fragility | Architecture phase: isolate protocol layer | Protocol layer has a defined interface; AirPlay code does not leak into UI code |
| Miracast Wi-Fi conflict | Architecture phase: decide scope of Miracast in v1 | Decision recorded; if included, "over infrastructure" mode is default |
| mDNS discovery failure | Core infrastructure milestone | Receiver appears in AirPlay menu on a test iOS device on a network with Windows Defender active |
| Audio/video sync drift | Media pipeline design phase | AV offset < 50ms measured on a 5-minute session |
| Hardware decoder fragility | Media pipeline design phase | Explicit decoder selection and fallback implemented; tested on a machine without vaapi |
| DRM content handling | AirPlay implementation phase | DRM refusal produces a user-visible error message, not a black window |
| Single-protocol abstraction missing | Architecture phase (before any protocol code) | All protocol implementations satisfy the same `MirroringSession`/`ReceiverAdvertiser` interfaces |
| Firewall blocking ports | Core infrastructure milestone | Works on a fresh Windows install with default firewall without manual rule creation |
| Security: unauthenticated connections | AirPlay/Cast implementation phase | Pairing PIN is enforced by default; auto-accept is opt-in |
| Android MediaProjection consent caching | Sender Android capture phase | Session reconnect on Android 14 device does not throw SecurityException |
| iOS extension 50 MB memory limit | Sender iOS capture phase | 30-minute broadcast on iPad Pro 12.9" without extension crash |
| macOS TCC permission binding to signing identity | Sender macOS capture phase (pre-implementation) | Rebuild app, verify TCC permission persists without re-grant |
| Windows UAC secure desktop / DX12 capture | Sender Windows capture phase | Dual-path (WinRT + DXGI) implemented; DX12 app capture verified |
| H.264 color format mismatch (AVCC vs Annex B) | Sender encoding phase | Receiver GStreamer pipeline logs no h264parse errors with Android sender frames |
| Flutter MethodChannel video data throughput | Sender Flutter architecture phase | Dart layer handles only session control; media bytes never cross platform channel |
| mDNS blocked by AP isolation | Sender discovery phase | Manual IP fallback verified on a network with client isolation enabled |
| iOS extension ↔ main app IPC architecture | Sender iOS capture phase | Extension and main app communicate without XPC large-payload crashes |
| IDR interval and NAL unit framing | Sender encoding phase | Receiver can connect mid-stream and display first frame within 2 seconds |
| Flutter desktop screen capture API gaps | Sender desktop architecture phase | Linux capture uses PipeWire via native layer; no dependency on community Flutter packages |

---

## Sources

**Receiver (v1) sources:**
- [UxPlay GitHub — known issues and dependency documentation](https://github.com/antimof/UxPlay) — HIGH confidence
- [RPiPlay GitHub — open source AirPlay mirroring server](https://github.com/FD-/RPiPlay) — HIGH confidence
- [AirPlay issues after iPadOS/iOS 26 update — Prowise](https://service.prowise.com/hc/en-gb/articles/29653730908178-AirPlay-issues-resolved-by-update-to-iOS-iPadOS-macOS-26-2) — HIGH confidence
- [AirBorne: 23 AirPlay CVEs including RCE — Oligo Security 2025](https://www.oligo.security/blog/critical-vulnerabilities-in-airplay-protocol-affecting-multiple-apple-devices) — HIGH confidence
- [Unofficial AirPlay Protocol Specification — openairplay.github.io](https://openairplay.github.io/airplay-spec/) — MEDIUM confidence (community, not Apple)
- [AirPlay 2 internals — emanuelecozzi.net](https://emanuelecozzi.net/docs/airplay2) — MEDIUM confidence
- [Miracast implementation — MiracleCast / albfan](https://github.com/albfan/miraclecast) — MEDIUM confidence
- [Miracast on Linux — linuxvox.com](https://linuxvox.com/blog/miracast-linux/) — MEDIUM confidence
- [Miracast over Infrastructure — Microsoft Learn](https://learn.microsoft.com/en-us/surface-hub/miracast-over-infrastructure) — HIGH confidence
- [Google Cast Web Receiver troubleshooting — Google Developers](https://developers.google.com/cast/docs/android_tv_receiver/troubleshooting) — HIGH confidence
- [DLNA incompatibility list — Audirvana community](https://community.audirvana.com/t/a-dlna-in-compatibility-list/23835) — MEDIUM confidence
- [CallStranger SSCP vulnerability](https://www.belkin.com/support-article/?articleNum=159410) — MEDIUM confidence
- [FairPlay not licensed for third-party platforms — Apple Developer](https://developer.apple.com/streaming/fps/) — HIGH confidence
- [RPiPlay firewall port requirements — GitHub issue #91](https://github.com/FD-/RPiPlay/issues/91) — HIGH confidence
- [AirPlay update breaks third-party apps — Squirrels/Medium](https://medium.com/@Squirrels/airplay-overhaul-breaks-third-party-streaming-apps-in-tvos-10-2-this-is-why-9aa565ac74b7) — MEDIUM confidence

**Sender (v2.0) sources:**
- [Android MediaProjection behavior changes API 34 — Android Developers](https://developer.android.com/about/versions/14/behavior-changes-14#media-projection-consent) — HIGH confidence
- [Android foreground service types required — Android Developers](https://developer.android.com/about/versions/14/changes/fgs-types-required) — HIGH confidence
- [flutter-webrtc Android 14 MediaProjection crash — GitHub issue #1813](https://github.com/flutter-webrtc/flutter-webrtc/issues/1813) — HIGH confidence
- [flutter-webrtc Android SDK 34 getDisplayMedia — GitHub issue #1521](https://github.com/flutter-webrtc/flutter-webrtc/issues/1521) — HIGH confidence
- [CVE-2025-32322 MediaProjection bypass — ZeroPath](https://zeropath.com/blog/android-cve-2025-32322-mediaprojection-bypass) — HIGH confidence
- [iOS ReplayKit broadcast extension memory limits — Apple Developer Forums](https://developer.apple.com/forums/thread/651367) — HIGH confidence
- [Twilio iOS broadcast extension memory pitfalls — GitHub issue #16](https://github.com/twilio/twilio-video-ios/issues/16) — HIGH confidence
- [iOS broadcast extension crash — Apple Developer Forums](https://developer.apple.com/forums/thread/131210) — HIGH confidence
- [macOS TCC permission binding to signing identity — Michael Tsai blog](https://mjtsai.com/blog/2024/08/08/sequoia-screen-recording-prompts-and-the-persistent-content-capture-entitlement/) — HIGH confidence
- [Windows.Graphics.Capture UAC limitation — Microsoft Learn](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture) — HIGH confidence
- [Windows capture DX12 frame count bug — Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/971078/winrt-windows-graphics-capture-api-on-directx12) — MEDIUM confidence
- [Android MediaCodec color format device fragmentation — bigflake.com](https://bigflake.com/mediacodec/) — HIGH confidence
- [Android MediaCodec color format issue tracker](https://issuetracker.google.com/issues/36955844) — HIGH confidence
- [Flutter platform channel performance — Aaron Clarke / Flutter Medium](https://medium.com/flutter/improving-platform-channel-performance-in-flutter-e5b4e5df04af) — HIGH confidence
- [Flutter thread merge iOS/Android 3.32 — flutter/flutter issue #150525](https://github.com/flutter/flutter/issues/150525) — HIGH confidence
- [mDNS AP isolation and multicast filtering — 1Home documentation](https://www.1home.io/docs/en/server/matter-networking) — MEDIUM confidence
- [Flutter WebRTC screen sharing Android 14+ guide — Medium 2026](https://medium.com/@owinojumahjerome/flutter-webrtc-screen-sharing-on-android-14-the-missing-guide-4f45391055f3) — MEDIUM confidence
- [TCP head-of-line blocking latency — IT Hare](http://ithare.com/almost-zero-additional-latency-udp-over-tcp/) — MEDIUM confidence

---
*Pitfalls research for: cross-platform screen mirroring receiver (AirPlay, Google Cast, Miracast, DLNA) + v2.0 Flutter companion sender app*
*Researched: 2026-03-28 (receiver) / 2026-03-30 (sender append)*
