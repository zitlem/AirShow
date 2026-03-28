# Architecture Research

**Domain:** Cross-platform screen mirroring receiver (AirPlay, Google Cast, Miracast, DLNA)
**Researched:** 2026-03-28
**Confidence:** HIGH (based on open-source reference implementations: UxPlay, RPiPlay, MiracleCast, shanocast, openscreen)

## Standard Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        UI / Display Layer                        │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │          Receiver Window  (fullscreen, cross-platform)    │   │
│  │          Video Sink   ←   Audio Sink                     │   │
│  └──────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────┤
│                     Media Pipeline Layer                         │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Decode (H.264/H.265/AAC/ALAC)  →  Sync  →  Render      │   │
│  │  Hardware accel if available; software fallback          │   │
│  └──────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────┤
│                   Protocol Abstraction Layer                      │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │     Unified Session Interface  (start / stop / status)   │   │
│  └──────────────────────────────────────────────────────────┘   │
├───────────────┬──────────────┬──────────────┬───────────────────┤
│  AirPlay 2    │ Google Cast  │  Miracast     │  DLNA/DMR         │
│  Protocol     │  Protocol    │  Protocol     │  Protocol         │
│  Handler      │  Handler     │  Handler      │  Handler          │
│               │              │               │                   │
│  mDNS advert  │  mDNS +SSDP  │  Wi-Fi Direct │  SSDP / UPnP     │
│  RTSP/HTTP    │  TLS+protobuf│  RTSP+H.264   │  HTTP + SOAP      │
│  RTP (H264)   │  WebRTC/Cast │  RTP over TCP │  HTTP media pull  │
│  AES decrypt  │  TLS encrypt │  HDCP (opt.)  │  UPnP AVTransport │
└───────────────┴──────────────┴──────────────┴───────────────────┘
         ↑               ↑              ↑               ↑
┌─────────────────────────────────────────────────────────────────┐
│                     Discovery / Advertisement Layer              │
│  ┌───────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │  mDNS / Avahi │  │  SSDP / DIAL │  │  Wi-Fi P2P (wpa_sup) │  │
│  │  (Bonjour)    │  │  UPnP        │  │  (Miracast only)     │  │
│  └───────────────┘  └──────────────┘  └──────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Typical Implementation |
|-----------|----------------|------------------------|
| Discovery / Advertisement | Advertise receiver presence on LAN; listen for sender announcements | mDNS via Avahi/mdns-sd, SSDP multicast on 239.255.255.250:1900 |
| AirPlay Protocol Handler | RTSP + HTTP combined server; pairing/auth (Ed25519/SRP6a); AES-CTR/CBC decryption; dispatch to RTP receivers | lib/raop.c style (UxPlay/RPiPlay lineage) |
| Google Cast Protocol Handler | mDNS advertisement; TLS socket on port 8009; protobuf message framing; DIAL launch/control; CastV2 mirroring | openscreen / node-castv2 style |
| Miracast Protocol Handler | Wi-Fi Direct (P2P) negotiation via wpa_supplicant; RTSP capability exchange; RTP/H.264 stream receive | miraclecast / GNOME Network Displays style |
| DLNA/DMR Handler | UPnP device advertisement; UPnP AVTransport service; HTTP media pull from sender URL | gupnp / coherence / node-upnp style |
| Protocol Abstraction Layer | Unified session lifecycle API so the media pipeline does not care which protocol delivered the stream | Interface / plugin registry pattern |
| Media Pipeline | Receive raw encoded frames; jitter buffer + NTP sync; hardware-accelerated decode (H.264, H.265, AAC, ALAC); push to sinks | GStreamer (preferred: cross-platform, plugin-agnostic hardware accel) |
| Receiver Window / Display | Fullscreen render window; embed video sink; audio sink; minimal HUD (protocol name, connected device) | GStreamer video sink + native windowing (gtkglsink / d3d11videosink / metal) |
| Audio Engine | Receive audio stream (separate from video in AirPlay); sync with video timestamps; mute toggle | GStreamer audio pipeline with ALAC/AAC decoder |
| Session Manager | Track active session state; arbitrate if two protocols attempt simultaneous sessions | State machine per protocol, one active session at a time |
| Configuration / Persistence | Device name, audio device selection, display selection, startup behaviour | Simple key-value config file (TOML/JSON) |

## Recommended Project Structure

```
src/
├── discovery/              # LAN advertisement and peer detection
│   ├── mdns.rs/ts          # mDNS / Bonjour / Avahi wrapper
│   ├── ssdp.rs/ts          # SSDP multicast for Cast + DLNA
│   └── wifi_direct.rs/ts   # Wi-Fi P2P for Miracast
│
├── protocols/              # One module per protocol
│   ├── mod.rs/index.ts     # Protocol trait / interface definition
│   ├── airplay/            # RTSP+HTTP server, RTP receivers, AES decrypt
│   ├── cast/               # TLS socket, protobuf framing, DIAL
│   ├── miracast/           # RTSP sink, wpa_supplicant interaction
│   └── dlna/               # UPnP AVTransport DMR
│
├── pipeline/               # Media decode + render (GStreamer)
│   ├── video.rs/ts         # H.264/H.265 decode pipeline, video sink
│   ├── audio.rs/ts         # AAC/ALAC decode pipeline, audio sink
│   └── sync.rs/ts          # NTP timestamp mapping, A/V sync
│
├── session/                # Active session state machine
│   └── manager.rs/ts       # Start/stop sessions, arbitrate conflicts
│
├── ui/                     # Receiver window and minimal HUD
│   ├── window.rs/ts        # Fullscreen window lifecycle
│   └── overlay.rs/ts       # Protocol name, device name HUD
│
├── config/                 # Persistent settings
│   └── settings.rs/ts      # Load/save TOML or JSON config
│
└── main.rs/index.ts        # Bootstrap: init discovery + session manager + UI
```

### Structure Rationale

- **protocols/:** Each protocol is fully self-contained. They expose a common trait/interface; the session manager does not import protocol internals.
- **pipeline/:** GStreamer is the only cross-platform media backend that provides hardware-accelerated H.264 decode, an audio pipeline, and native video sinks (gtkglsink on Linux, metal on macOS, d3d11videosink on Windows) from one codebase. Keep it isolated so it can be swapped.
- **discovery/:** Separated from protocol handlers because mDNS is shared between AirPlay and Cast; SSDP is shared between Cast and DLNA.
- **session/:** Central arbitration prevents two protocols from fighting over the video sink simultaneously.

## Architectural Patterns

### Pattern 1: Protocol Plugin / Strategy

**What:** Each protocol handler implements a common `ProtocolHandler` interface with `advertise()`, `start_session()`, `stop_session()` methods. A plugin registry holds all active handlers; the session manager calls the interface, not concrete types.

**When to use:** Any time a new protocol needs to be added without touching the media pipeline or session manager. This is the correct architecture for MyAirShow from day one.

**Trade-offs:** Adds interface indirection but prevents the alternative (a giant switch statement throughout the codebase). The overhead is negligible.

**Example:**
```typescript
interface ProtocolHandler {
  readonly name: string;           // "airplay" | "cast" | "miracast" | "dlna"
  advertise(): Promise<void>;      // start LAN advertisement
  stopAdvertising(): Promise<void>;
  on(event: "session_start", cb: (session: StreamSession) => void): void;
  on(event: "session_end",   cb: () => void): void;
}

// Session manager iterates handlers, doesn't import AirPlay or Cast directly
class SessionManager {
  constructor(private handlers: ProtocolHandler[], private pipeline: MediaPipeline) {
    for (const h of handlers) {
      h.on("session_start", (s) => this.activate(h.name, s));
      h.on("session_end",   ()  => this.deactivate(h.name));
    }
  }
}
```

### Pattern 2: GStreamer appsrc Ingestion

**What:** Each protocol handler pushes raw encoded bytes (H.264 NAL units, AAC frames) into a GStreamer `appsrc` element. GStreamer's pipeline handles decoding, synchronisation, and platform-specific rendering. The protocol handler never knows what GPU or display backend is in use.

**When to use:** Always — this is the established pattern from UxPlay, RPiPlay, and libcast. It separates network I/O from media rendering cleanly.

**Trade-offs:** Requires GStreamer to be installed on the target machine (it is on most Linux distros; installer bundle on macOS/Windows). The alternative — embedding FFmpeg + native rendering — is significantly more work with no advantage.

**Example:**
```c
// Protocol handler side (simplified from UxPlay pattern)
GstElement *appsrc = gst_bin_get_by_name(pipeline, "video_appsrc");
GstBuffer *buf = gst_buffer_new_allocate(NULL, nalu_size, NULL);
gst_buffer_fill(buf, 0, nalu_data, nalu_size);
GST_BUFFER_PTS(buf) = rtp_timestamp_to_gst_time(ts);
gst_app_src_push_buffer(GST_APP_SRC(appsrc), buf);
```

### Pattern 3: Single Active Session with Queuing

**What:** Only one protocol session renders at a time. When a second device tries to connect while a session is active, the new request is either rejected or queued based on user configuration. Session state is a simple enum: `Idle → Connecting → Active → Disconnecting → Idle`.

**When to use:** This is the correct default for a TV-like display receiver. Attempting to render two simultaneous streams adds enormous complexity with no clear benefit for v1.

**Trade-offs:** Prevents split-screen scenarios (out of scope for v1 anyway). Simple to implement and debug.

## Data Flow

### Screen Mirroring Session (AirPlay example — all protocols follow the same shape)

```
[iOS Device]                        [MyAirShow Receiver]
     │                                      │
     │── mDNS browse ─────────────────────> │ (Discovery: mDNS advertisement running)
     │<─ mDNS response (name, IP, port) ─── │
     │                                      │
     │── TCP connect (RTSP port 7100) ────> │ AirPlay Handler: HTTP/RTSP server
     │── RTSP OPTIONS ───────────────────>  │
     │<─ RTSP 200 OK ──────────────────────  │
     │── RTSP SETUP (video) ──────────────> │ Handler negotiates RTP parameters
     │── RTSP SETUP (audio) ──────────────> │
     │── RTSP RECORD ─────────────────────> │ Session starts → notify SessionManager
     │                                      │
     │   (encrypted RTP video, TCP) ──────> │ raop_rtp_mirror: receive + decrypt AES
     │   (encrypted RTP audio, UDP) ──────> │ raop_rtp: receive + decrypt AES
     │                                      │
     │                          Buffer + NTP sync (raop_buffer / raop_ntp)
     │                                      │
     │                          appsrc push to GStreamer video pipeline
     │                          appsrc push to GStreamer audio pipeline
     │                                      │
     │                          GStreamer: H.264 decode (hardware if available)
     │                          GStreamer: AAC decode
     │                                      │
     │                          videosink → Receiver Window (fullscreen)
     │                          audiosink → System audio output
     │                                      │
     │── RTSP TEARDOWN ──────────────────>  │ Session ends → SessionManager → Idle
```

### Google Cast Data Flow (differences from AirPlay)

```
[Android / Chrome]                 [MyAirShow Receiver]
     │── SSDP / mDNS browse ──────────────> │ Cast Handler: mDNS + SSDP advertisement
     │── TLS connect port 8009 ──────────>  │ Cast Handler: TLS server
     │── protobuf CONNECT message ────────> │
     │── LAUNCH (mirroring app) ──────────> │
     │<─ RECEIVER_STATUS ─────────────────  │
     │── OFFER (WebRTC SDP) ──────────────> │ Cast mirroring: negotiate codec
     │<─ ANSWER ──────────────────────────  │
     │   (RTP/SRTP video over UDP) ───────> │ Decrypt SRTP → appsrc → GStreamer
     │   (RTP/SRTP audio over UDP) ───────> │
```

### Miracast Data Flow (differs: transport-level is Wi-Fi Direct)

```
[Windows / Android]                [MyAirShow Receiver]
     │── P2P probe (Wi-Fi Direct) ────────> │ Miracast Handler: wpa_supplicant P2P
     │── P2P group negotiate ─────────────> │ (receiver acts as P2P Group Owner)
     │── TCP connect (RTSP port 7236) ────> │ RTSP capability exchange
     │── RTSP SETUP / PLAY ──────────────>  │
     │   (RTP H.264 over TCP) ────────────> │ → appsrc → GStreamer
```

### DLNA Data Flow (pull model — different from others)

```
[Phone / Smart TV app]             [MyAirShow Receiver]
     │── SSDP M-SEARCH ──────────────────> │ DLNA Handler: SSDP advertisement
     │<─ SSDP NOTIFY (device description)─ │
     │── UPnP Browse device ─────────────> │
     │── AVTransport SetAVTransportURI ──> │ Handler receives media URL
     │── AVTransport Play ───────────────> │ Handler fetches URL → appsrc / filesrc
     │                          GStreamer decodes and renders
```

### Key Data Flows Summary

1. **Discovery → Handler activation:** mDNS/SSDP/P2P runs continuously in background; when a sender connects, the relevant handler creates a session object and notifies SessionManager.
2. **Handler → Pipeline:** All protocol handlers converge at the same interface: push encoded video/audio bytes with a PTS timestamp into GStreamer `appsrc` elements.
3. **Pipeline → Display:** GStreamer manages decode, sync, and sink selection. The UI layer only creates the window and passes the native window handle to GStreamer's video sink.
4. **SessionManager arbitration:** SessionManager receives "session start" events, checks if idle, starts pipeline if so (or rejects if occupied).

## Suggested Build Order

Dependencies between components determine the correct construction sequence:

```
1. Media Pipeline (GStreamer appsrc → decode → sink)
      ↓ must exist before any protocol can render
2. Receiver Window (fullscreen window + embed GStreamer video sink)
      ↓ must exist to validate the pipeline shows anything
3. Discovery Layer (mDNS + SSDP advertisement)
      ↓ prerequisite for any protocol to be found
4. AirPlay Protocol Handler    ← first protocol (best-documented, most traffic)
      ↓ validates end-to-end: discovery → session → pipeline → display
5. Session Manager
      ↓ needed before second protocol to prevent conflicts
6. Google Cast Protocol Handler
7. DLNA/DMR Protocol Handler   ← simpler (pull model, no encryption)
8. Miracast Protocol Handler   ← most complex (Wi-Fi Direct negotiation)
9. Configuration / Persistence
10. UI polish (HUD, device name display, settings screen)
```

**Rationale:** Pipeline and Window first because they are the integration target for every subsequent component. Without them you cannot confirm any protocol works. AirPlay before Cast because its protocol is more thoroughly reverse-engineered and has mature open-source reference implementations (UxPlay, RPiPlay). Miracast last because Wi-Fi Direct P2P adds OS-level complexity (wpa_supplicant on Linux, native WFD stack on Windows/macOS) that is orthogonal to the core architecture.

## Anti-Patterns

### Anti-Pattern 1: Per-Protocol Media Pipeline

**What people do:** Build a separate GStreamer (or FFmpeg) pipeline inside each protocol handler — one for AirPlay, one for Cast, one for Miracast.

**Why it's wrong:** Three separate pipelines fight over the audio device and video sink. A/V sync is implemented three times. Adding a second display output requires changes in four places. Hardware decoder initialization is duplicated.

**Do this instead:** One shared pipeline with `appsrc` injection points. All protocols push bytes into the same pipeline. The pipeline is started/stopped by the SessionManager, not by protocol handlers.

### Anti-Pattern 2: Blocking Network I/O in Protocol Handler

**What people do:** Protocol handlers (especially the RTSP server) use synchronous/blocking I/O, stalling the entire process while waiting for the next packet.

**Why it's wrong:** Causes A/V glitches when packet arrival is delayed. On Windows/macOS, blocking the main thread triggers OS "app not responding" states.

**Do this instead:** Dedicated thread (or async task) per RTP receiver. UxPlay's architecture is instructive: `raop_rtp_mirror` runs on its own thread that only blocks on socket reads, then pushes to the GStreamer pipeline on a separate thread boundary.

### Anti-Pattern 3: Coupling Discovery to Protocol Handler

**What people do:** AirPlay handler code also runs the mDNS advertisement. Cast handler code also runs SSDP.

**Why it's wrong:** mDNS is shared between AirPlay and Cast (both use `_googlecast._tcp` and `_airplay._tcp`). SSDP is shared between Cast and DLNA. When you need to change the device name, you touch four different places.

**Do this instead:** Discovery layer is a shared service. Protocol handlers tell the discovery service what TXT records and service types to advertise. Discovery layer owns all multicast sockets.

### Anti-Pattern 4: Miracast Implemented First

**What people do:** Tackle Miracast early because it is listed as a requirement.

**Why it's wrong:** Miracast requires Wi-Fi Direct (P2P), which requires OS-level wpa_supplicant control on Linux, the Windows WFD API on Windows, and has no mature cross-platform library. Building this before the media pipeline is validated wastes time on networking plumbing before you know the core render path works.

**Do this instead:** Build Miracast last, after the pipeline + window + at least one other protocol is proven end-to-end.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| mDNS / Bonjour | Avahi daemon on Linux; native Bonjour on macOS; mdns4 / Bonjour SDK on Windows | Platform-specific; abstract behind a thin wrapper |
| wpa_supplicant (Miracast) | D-Bus control interface for Wi-Fi Direct P2P negotiation | Linux only; Windows uses native WFD stack; macOS lacks P2P support entirely |
| GStreamer | Shared library; dynamically loaded plugins | Must be present on target OS; bundle with installer for macOS/Windows |
| OpenSSL / libssl | AES decryption for AirPlay; TLS for Cast | Link statically for portable binaries |
| System audio output | GStreamer audiosink autoselects (PulseAudio/PipeWire on Linux; CoreAudio on macOS; WASAPI on Windows) | No manual plumbing required if using GStreamer |

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| Protocol Handler → Session Manager | Event callbacks / message queue | Handler emits "session_start" with encoded stream metadata; manager decides whether to accept |
| Protocol Handler → Media Pipeline | `appsrc` push (GStreamer API) | Only encoded bytes + PTS cross this boundary; handlers have no knowledge of decoder |
| Session Manager → Receiver Window | Signal/event: show/hide, display protocol name | Window does not know which protocol is active |
| Discovery Layer → Protocol Handlers | Service registry: handlers register their mDNS service records at startup | Discovery layer does not import protocol-specific code |
| Config → All components | Read at startup; reload on change | Pass config struct at construction time; no global config singleton |

## Scaling Considerations

This is a local-network desktop application; traditional web-service scaling is not applicable. The relevant scaling axes are:

| Concern | Single session (v1) | Multi-session (future) | Notes |
|---------|---------------------|------------------------|-------|
| CPU (decode) | Hardware-accelerated H.264 trivially handles 1080p on any modern machine | Two simultaneous streams require two decoder instances; hardware decode handles it | GStreamer plugin is hardware-agnostic |
| Memory | ~50 MB for GStreamer + protocol state is typical (UxPlay baseline) | Linear growth per session | Not a concern for desktop |
| Network | Single unicast stream; receiver is passive | Multiple streams would require multiple sockets | Protocol handlers are already per-connection |
| Latency | Target < 100 ms glass-to-glass; achievable with low-latency GStreamer settings | Unaffected by multi-session if pipelines are separate | UxPlay achieves ~65–80 ms on modern hardware |

## Sources

- UxPlay architecture (DeepWiki): https://deepwiki.com/antimof/UxPlay
- UxPlay source (FDH2 fork, active): https://github.com/FDH2/UxPlay
- RPiPlay (AirPlay mirroring server): https://github.com/FD-/RPiPlay
- AirPlay internal documentation: https://air-display.github.io/airplay-internal/
- Google Cast protocol (oakbits): https://oakbits.com/google-cast-protocol-discovery-and-connection.html
- openscreen (Google's open Cast library): https://chromium.googlesource.com/openscreen/
- shanocast (open Cast receiver): https://github.com/rgerganov/shanocast
- MiracleCast (Wi-Fi Direct / Miracast): https://github.com/albfan/miraclecast
- GNOME Network Displays (Miracast): https://github.com/benzea/gnome-network-displays
- Cross-platform receiver developer guide (brightcoding, Dec 2025): https://www.blog.brightcoding.dev/2025/12/31/the-ultimate-developer-guide-building-cross-platform-wireless-display-solutions-with-airplay-miracast-google-cast-sdks/
- GStreamer cross-platform features: https://gstreamer.freedesktop.org/features/
- DLNA/DMR specification context: https://en.wikipedia.org/wiki/DLNA

---
*Architecture research for: MyAirShow — cross-platform screen mirroring receiver*
*Researched: 2026-03-28*
