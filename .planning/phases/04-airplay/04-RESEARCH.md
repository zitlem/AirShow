# Phase 4: AirPlay - Research

**Researched:** 2026-03-28
**Domain:** UxPlay RAOP/AirPlay 2 mirroring integration — extracting UxPlay's `lib/` as a CMake static library, wiring `raop_callbacks_t` into `AirPlayHandler`, feeding H.264 video and AAC/ALAC audio into the existing `MediaPipeline` via `appsrc`, and wiring session lifecycle to `ConnectionBridge`.
**Confidence:** HIGH — UxPlay source structure verified against GitHub; callback signatures confirmed from `lib/raop.h` and `lib/stream.h`; appsrc injection pattern confirmed from `renderers/video_renderer.c` and `renderers/audio_renderer.c`.

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**UxPlay Integration**
- D-01: Embed UxPlay 1.73.x as a Git submodule and extract its core server logic (RAOP server, mirroring handler, FairPlay auth from the `lib/` directory) into a linkable library target
- D-02: Create `AirPlayHandler : ProtocolHandler` in `src/protocol/AirPlayHandler.h` that wraps UxPlay's RAOP server and implements `start()`, `stop()`, `setMediaPipeline()`
- D-03: UxPlay's GStreamer rendering code is replaced — instead, decoded A/V frames are fed into MyAirShow's shared `MediaPipeline` via `appsrc` injection (Phase 1 D-05)
- D-04: UxPlay's own service advertisement code is bypassed — MyAirShow's `DiscoveryManager` (Phase 2) already handles `_airplay._tcp` and `_raop._tcp` advertisement

**AirPlay Authentication**
- D-05: Use UxPlay's built-in FairPlay SRP authentication implementation — no custom crypto needed
- D-06: OpenSSL 3.x (already linked in Phase 1) provides the underlying crypto primitives for FairPlay
- D-07: libplist (dependency of UxPlay) is added via vcpkg or system package for Apple property list parsing

**Session Lifecycle**
- D-08: Single-session model for v1 — one AirPlay mirroring session at a time
- D-09: When a new device connects while one is active, the existing session is replaced (UxPlay default behavior)
- D-10: Session events (connect, disconnect, device name, protocol) are routed to `ConnectionBridge` to update the HUD overlay (Phase 3)
- D-11: Clean teardown on disconnect — stop pipeline input, clear connection state, return to idle screen

**A/V Synchronization**
- D-12: Use GStreamer's RTP-based clock synchronization — `rtpjitterbuffer` and `rtph264depay` handle timestamp-based A/V sync
- D-13: GStreamer's pipeline clock is the master sync reference for both video and audio streams
- D-14: AirPlay sends H.264 video via RTP and AAC/ALAC audio via a separate RTP stream — both are demuxed and fed into the shared pipeline with their RTP timestamps preserved

### Claude's Discretion
- Exact UxPlay source files to extract vs exclude (renderer, CLI entry point, etc.)
- CMake integration approach for UxPlay as a submodule library target
- Internal threading model for the RAOP server (UxPlay uses its own event loop)
- GStreamer element chain between UxPlay output and `appsrc` injection
- Whether to use `appsrc` for both audio and video or split into separate injection points
- Error handling and reconnection behavior details

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| AIRP-01 | User can mirror their iPhone/iPad screen to MyAirShow via AirPlay | UxPlay RAOP server handles iOS AirPlay 2 mirroring via `raop_t`; `video_process` callback delivers H.264 NAL units; `conn_init`/`conn_destroy` callbacks manage session lifecycle |
| AIRP-02 | User can mirror their macOS screen to MyAirShow via AirPlay | Same RAOP code path handles macOS screen mirroring; macOS uses identical AirPlay 2 protocol; no platform-specific deviation needed in the handler |
| AIRP-03 | AirPlay mirroring includes synchronized audio and video | `audio_process` and `video_process` both receive NTP timestamps (`ntp_time_local`, `ntp_time_remote`) from `raop_ntp_t`; shared GStreamer pipeline clock + synchronized `appsrc` PTS values achieve A/V sync |
| AIRP-04 | AirPlay connection maintains stable A/V sync during extended sessions | GStreamer pipeline clock as master; `rtpjitterbuffer`-style buffering in UxPlay's `raop_buffer.c`; preserving NTP-derived PTS across both audio and video branches prevents drift |
</phase_requirements>

---

## Summary

Phase 4 is the first live protocol in the MyAirShow stack. The foundation (GStreamer pipeline, Qt window, discovery advertisement, protocol handler interface) is already in place. This phase's job is to wire UxPlay's RAOP server — specifically the C library in its `lib/` directory — into a clean `AirPlayHandler` class that satisfies the `ProtocolHandler` interface and injects encoded A/V frames into the existing `MediaPipeline` via `appsrc`.

UxPlay's `lib/` compiles as a static library named `airplay` (confirmed from `lib/CMakeLists.txt`). The top-level `uxplay.cpp` application and the `renderers/` directory are NOT needed — they implement UxPlay's own GStreamer pipeline and CLI, which are explicitly replaced per D-03. The two files to exclude are: `uxplay.cpp` (CLI entry point) and the entire `renderers/` subtree. The target is to use `lib/` only.

The key integration points are: (1) `raop_init()` / `raop_init2()` / `raop_start_httpd()` to spin up the RAOP HTTP server on port 7000; (2) `raop_callbacks_t` to register the `video_process`, `audio_process`, `conn_init`, `conn_destroy`, and `conn_teardown` callbacks; and (3) `gst_app_src_push_buffer()` inside those callbacks to feed encoded frames into the shared pipeline.

**Primary recommendation:** Embed UxPlay `lib/` as a CMake submodule target `airplay` (static), register a `raop_callbacks_t` struct in `AirPlayHandler`, and push each `video_decode_struct.data` payload directly into a video `appsrc` element with PTS derived from `ntp_time_local`. Use the same pattern for audio with `audio_decode_struct`. Wire `conn_init`/`conn_destroy` to `ConnectionBridge::setConnected()`.

---

## Standard Stack

### Core (all already in project)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| UxPlay | 1.73.6 | RAOP server, FairPlay SRP auth, AirPlay 2 mirroring | The only active GPL AirPlay 2 receiver. Phase decision D-01 |
| libplist | ≥2.6 (2.7.0 latest) | Apple Property List parsing | UxPlay hard dependency; handles AirPlay SETUP plist bodies |
| GStreamer appsrc | 1.26.x (gstreamer-app-1.0) | Frame injection from UxPlay callbacks into pipeline | Already linked (gstreamer-app-1.0 in CMakeLists.txt) |
| OpenSSL 3.x | Already linked | FairPlay SRP / AES-CTR crypto | Already linked in Phase 1; UxPlay `lib/` links against OpenSSL::Crypto |
| Avahi (Linux) | vendor/avahi/ (vendored) | mDNS — already handles AirPlay advertisement | Already in project; `DiscoveryManager` uses it. No new dependency |

### New Dependencies This Phase
| Library | Version | Purpose | Installation |
|---------|---------|---------|--------------|
| libplist | ≥2.6 | Apple plist parsing for AirPlay SETUP negotiation | `apt install libplist-dev` (Linux) / `brew install libplist` (macOS) / vcpkg on Windows |
| libplist-2.0 | pkg-config name `libplist-2.0` | CMake detection | `pkg_check_modules(PLIST REQUIRED IMPORTED_TARGET libplist-2.0)` |

**Installation (Linux):**
```bash
sudo apt install libplist-dev
```

**UxPlay submodule add:**
```bash
git submodule add https://github.com/FDH2/UxPlay.git vendor/uxplay
git submodule update --init --recursive vendor/uxplay
```

**Version verification:**
UxPlay 1.73.6 is the current stable tag. `libplist` 2.7.0 was released 2025-05. Both verified against GitHub and libimobiledevice.org as of 2026-03-28.

---

## Architecture Patterns

### Recommended Project Structure (additions this phase)

```
src/
├── protocol/
│   ├── ProtocolHandler.h      # Existing — AirPlayHandler implements this
│   ├── ProtocolManager.h/cpp  # Existing — registers AirPlayHandler
│   ├── AirPlayHandler.h       # NEW: wraps raop_t, implements ProtocolHandler
│   └── AirPlayHandler.cpp     # NEW: raop_callbacks_t wiring + appsrc injection
vendor/
└── uxplay/                    # NEW: git submodule (FDH2/UxPlay)
    └── lib/                   # Target: airplay (static library)
        ├── CMakeLists.txt
        ├── raop.c/h
        ├── raop_rtp_mirror.c/h
        ├── raop_ntp.c/h
        ├── raop_buffer.c/h
        ├── pairing.c/h
        ├── crypto.c/h
        ├── playfair/
        ├── llhttp/
        └── ...
```

### Pattern 1: UxPlay lib/ CMake Integration

**What:** Add `vendor/uxplay` as a submodule. In the root `CMakeLists.txt`, add the UxPlay `lib/` subdirectory after disabling UxPlay's own service-discovery and renderer builds. Link `airplay` (static) into `myairshow`.

**When to use:** Phase 4 build setup. This is the locked approach (D-01).

**Key insight:** UxPlay's root `CMakeLists.txt` calls `add_subdirectory(lib)` to build the `airplay` static target, then `add_subdirectory(renderers)` for its own GStreamer output. We want only `lib/`, not `renderers/`. The safest approach is to call `add_subdirectory(vendor/uxplay/lib)` directly rather than `add_subdirectory(vendor/uxplay)`, avoiding renderers entirely.

```cmake
# In root CMakeLists.txt — add after existing find_package calls

# libplist — required by UxPlay lib/
pkg_check_modules(PLIST REQUIRED IMPORTED_TARGET libplist-2.0)

# UxPlay lib/ — builds the 'airplay' static library target
# Include lib/ directly to avoid pulling in uxplay's renderers/ and uxplay.cpp
add_subdirectory(vendor/uxplay/lib)

# Link airplay into the main target
target_link_libraries(myairshow PRIVATE airplay PkgConfig::PLIST)
target_include_directories(myairshow PRIVATE vendor/uxplay/lib)
```

**Caveat:** UxPlay `lib/CMakeLists.txt` uses `aux_source_directory(.)` which includes ALL `.c` files. It also calls its own `find_package(OpenSSL)` and links `playfair` and `llhttp` (which it adds with `add_subdirectory`). Adding `vendor/uxplay/lib` will implicitly require `vendor/uxplay/lib/playfair` and `vendor/uxplay/lib/llhttp` subdirectories — these are already in the submodule.

**DNS-SD collision:** UxPlay's `lib/dnssd.c` contains DNS-SD code for Avahi. It will compile into the `airplay` target but will NOT be called since `DiscoveryManager` owns all mDNS operations (D-04). No conflict at link time, but `raop_init2()` must be called without triggering UxPlay's built-in DNS-SD registration (pass `NULL` for the dnssd context or use the `nohold` parameter appropriately).

### Pattern 2: raop_callbacks_t Registration

**What:** Allocate a `raop_callbacks_t` struct in `AirPlayHandler`, fill in function pointers, pass to `raop_init()`. The callbacks deliver encoded A/V frames.

**Key callback signatures (verified from `lib/raop.h` and `lib/stream.h`):**

```c
// video_decode_struct (from lib/stream.h):
typedef struct {
    bool is_h265;
    int nal_count;
    unsigned char *data;
    int data_len;
    uint64_t ntp_time_local;   // nanoseconds, local clock
    uint64_t ntp_time_remote;  // nanoseconds, remote (sender) clock
} video_decode_struct;

// audio_decode_struct (from lib/stream.h):
typedef struct {
    unsigned char *data;
    unsigned char ct;          // codec type: 0x20=ALAC, 0x8c-0x8e=AAC-ELD
    int data_len;
    int sync_status;
    uint64_t ntp_time_local;
    uint64_t ntp_time_remote;
    uint32_t rtp_time;
    unsigned short seqnum;
} audio_decode_struct;

// Callback registration (raop_callbacks_t members used by AirPlayHandler):
void video_process(void *cls, raop_ntp_t *ntp, video_decode_struct *data);
void audio_process(void *cls, raop_ntp_t *ntp, audio_decode_struct *data);
void conn_init(void *cls);       // new connection started
void conn_destroy(void *cls);    // connection fully torn down
void conn_teardown(void *cls, bool *teardown_96, bool *teardown_110);
void audio_get_format(void *cls, unsigned char *ct, unsigned short *spf,
                      bool *usingScreen, bool *isMedia, uint64_t *audioFormat);
void report_client_request(void *cls, char *deviceid, char *model,
                            char *name, bool *admit);  // for device name + admission
```

**Example (C++ wrapper pattern):**
```cpp
// Source: verified from UxPlay renderers/video_renderer.c pattern
static void videoProcessCallback(void* cls, raop_ntp_t* ntp,
                                  video_decode_struct* data) {
    auto* handler = static_cast<AirPlayHandler*>(cls);
    handler->onVideoFrame(data);
}

void AirPlayHandler::onVideoFrame(video_decode_struct* data) {
    if (!m_pipeline || !m_videoAppsrc) return;

    GstBuffer* buf = gst_buffer_new_allocate(nullptr, data->data_len, nullptr);
    gst_buffer_fill(buf, 0, data->data, data->data_len);

    // Normalize PTS relative to pipeline base time
    uint64_t pts = data->ntp_time_local;
    if (pts >= m_basetime) pts -= m_basetime;
    GST_BUFFER_PTS(buf) = pts;

    gst_app_src_push_buffer(GST_APP_SRC(m_videoAppsrc), buf);
}
```

### Pattern 3: appsrc GStreamer Pipeline for AirPlay

**What:** The existing `MediaPipeline` currently uses `videotestsrc` and `audiotestsrc`. Phase 4 replaces those source elements with `appsrc` elements that receive data from UxPlay callbacks. The downstream decode chain (`h264parse ! [decoder] ! videoconvert ! glupload ! qml6glsink`) is constructed in `MediaPipeline` and exposed via named `appsrc` elements.

**Recommended pipeline strings (verified from UxPlay's video_renderer.c and audio_renderer.c):**

```
Video branch:
  appsrc name=video_appsrc stream-type=0 format=time is-live=true
  ! h264parse
  ! [vaapih264dec | avdec_h264]
  ! videoconvert
  ! glupload
  ! qml6glsink

Audio branch:
  appsrc name=audio_appsrc stream-type=0 format=time is-live=true
  ! [avdec_aac | avdec_alac]   (codec negotiated at session setup via audio_get_format)
  ! audioconvert
  ! audioresample
  ! autoaudiosink
```

**appsrc caps (must be set before pushing data):**

```cpp
// Video appsrc caps — H.264 bytestream (AirPlay always sends Annex B NAL units):
GstCaps* videoCaps = gst_caps_from_string(
    "video/x-h264, stream-format=byte-stream, alignment=nal");
g_object_set(m_videoAppsrc, "caps", videoCaps, nullptr);
gst_caps_unref(videoCaps);

// Audio appsrc caps (for AAC-ELD, most common in AirPlay screen mirroring):
GstCaps* audioCaps = gst_caps_from_string(
    "audio/mpeg, mpegversion=4, stream-format=raw, channels=2, rate=44100");
g_object_set(m_audioAppsrc, "caps", audioCaps, nullptr);
gst_caps_unref(audioCaps);
```

**ALAC audio caps (less common, used in AirPlay audio streaming):**
```cpp
GstCaps* alacCaps = gst_caps_from_string(
    "audio/x-alac, channels=2, rate=44100, samplesize=16");
```

**Codec discrimination:** The `audio_get_format` callback fires before audio data begins. Use the `ct` (codec type) byte to determine caps:
- `ct == 0x20` → ALAC → `audio/x-alac`
- `ct >= 0x80 && ct <= 0x8e` → AAC-ELD → `audio/mpeg, mpegversion=4`

### Pattern 4: raop_init / raop_start_httpd Lifecycle

**What:** UxPlay's RAOP server is started by calling `raop_init()` then `raop_init2()` then `raop_start_httpd()`. The server runs its own HTTP event loop (via `httpd.c`) in a dedicated thread. `AirPlayHandler::start()` calls these three functions.

**Function signatures (verified from `lib/raop.h`):**

```c
// Step 1: create raop instance with callback table
raop_t *raop_init(raop_callbacks_t *callbacks);

// Step 2: configure with device id (MAC address) and optional keyfile
int raop_init2(raop_t *raop, int nohold, const char *device_id, const char *keyfile);

// Step 3: start the HTTP server (binds to port 7000)
int raop_start_httpd(raop_t *raop, unsigned short *port);

// Shutdown:
void raop_stop_httpd(raop_t *raop);
void raop_destroy(raop_t *raop);
```

**Port coordination:** `DiscoveryManager` already advertises `_airplay._tcp` and `_raop._tcp` on port 7000 (`kAirPlayPort = 7000`). `raop_start_httpd` must bind to port 7000. Pass `&port` where `port = 7000` before the call; the function writes the actual bound port back.

**Device ID:** Use the MAC address already obtained by `DiscoveryManager::readMacAddress()` — the same value that appears in the `deviceid` TXT record. This must match or iOS will reject the pairing.

**Public key (`pk`) TXT record:** The `DiscoveryManager` currently uses a 128-character zero placeholder. The real `pk` value is UxPlay's Ed25519 public key — generated by UxPlay's `pairing.c` on first run. UxPlay stores it via its `keyfile` parameter to `raop_init2()`. This phase must:
1. On first run, let UxPlay generate and save the keypair to a file (e.g., `<QStandardPaths::AppDataLocation>/airplay.key`)
2. Read back the public key from that file (or from UxPlay's pairing API)
3. Update the `pk` TXT record via `DiscoveryManager` before or immediately after starting the RAOP server

### Anti-Patterns to Avoid

- **Calling `add_subdirectory(vendor/uxplay)` (root):** Pulls in `renderers/` and `uxplay.cpp`, causing link conflicts with `main()` and unused GStreamer pipeline code. Use `add_subdirectory(vendor/uxplay/lib)` instead.
- **Using `sync=false` on video appsrc or sinks:** Disables the shared pipeline clock reference and causes permanent A/V drift. Only acceptable as a debugging aid.
- **Calling UxPlay's DNS-SD functions (`dnssd_init`, `dnssd_register_*`):** `DiscoveryManager` already owns all mDNS advertisement. Calling UxPlay's dnssd would create duplicate/conflicting mDNS registrations. Set the dnssd pointer to `NULL` when calling `raop_init2`.
- **Creating a separate GStreamer pipeline in AirPlayHandler:** Violates the shared pipeline architecture (D-03 and ARCHITECTURE.md Anti-Pattern 1). All frames go through `MediaPipeline`'s single pipeline.
- **Pushing frames on the GLib main loop thread:** UxPlay's callbacks fire on its internal HTTP/RTP worker threads. `gst_app_src_push_buffer` is thread-safe; do NOT marshal frames back to the Qt main thread before pushing — this adds latency and risks deadlock.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| AirPlay FairPlay SRP auth | Custom SRP6a implementation | UxPlay `lib/srp.c` + `lib/pairing.c` | SRP6a over Ed25519 with Apple-specific twists; wrong constants = auth failure every time |
| AES-CTR/CBC frame decryption | Custom AES decrypt on RTP payloads | UxPlay `lib/crypto.c` | AirPlay uses a non-standard key schedule; getting IV offsets wrong produces silent garbage frames |
| Apple plist parsing | Custom XML/binary plist parser | `libplist` | SETUP negotiation sends binary plist; binary plist format has edge cases (UIDs, dates) that libplist handles correctly |
| NTP clock synchronization | Polling `clock_gettime` directly | UxPlay `lib/raop_ntp.c` | AirPlay NTP requires both sending NTP queries on port 7010 AND receiving replies to compute clock offset; hand-rolling this is PITFALLS.md Pitfall 4 |
| HTTP request parsing | Custom HTTP parser for RTSP-like requests | UxPlay `lib/llhttp/` | AirPlay uses HTTP/1.1 + RTSP hybrid; llhttp handles it; standard HTTP parsers reject RTSP verbs |
| Buffer jitter management | Custom ring buffer for RTP reorder | UxPlay `lib/raop_buffer.c` | RTP reorder + AirPlay-specific flush semantics; wrong flush = audio artifacts on every seek |

**Key insight:** UxPlay's `lib/` is not just a protocol library — it encodes 10+ years of Apple reverse-engineering fixes for auth edge cases, device compatibility quirks, and iOS version-specific behavior. The only correct approach is to use it verbatim and build MyAirShow's integration around its callback interface.

---

## Common Pitfalls

### Pitfall 1: pk TXT Record Mismatch Causes iOS to Reject Receiver

**What goes wrong:** The `_airplay._tcp` and `_raop._tcp` mDNS advertisements currently use a 128-character zero placeholder for the `pk` field. iOS 14+ validates that the `pk` field in the TXT record matches the Ed25519 public key used during the AirPlay pairing handshake. A mismatch causes iOS to silently fail to connect (the receiver appears in the AirPlay menu but the spinner runs forever then fails).

**Why it happens:** Phase 2 correctly used a placeholder knowing this phase would replace it. The `pk` is generated by UxPlay's `pairing.c` on first run and stored in a keyfile. The same keypair must be used for both the TXT record and the live auth handshake.

**How to avoid:**
1. Call `raop_init2()` with a persistent keyfile path (e.g., `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/airplay.key"`)
2. After `raop_init()` succeeds, read the generated public key from the keyfile (hex-encoded, 64 bytes = 128 hex chars)
3. Call `DiscoveryManager::updateTxtRecord("_airplay._tcp", "pk", <real_pk>)` before any iOS device attempts to connect
4. `DiscoveryManager` must expose a method to update TXT records post-start (currently it does not — this must be added)

**Warning signs:** iPhone AirPlay menu shows the receiver, spinner runs, then "Could not connect" or the device disappears from the menu. Wireshark shows SETUP request reaching the server but getting a 500 or no response.

### Pitfall 2: AirPlay Port 7000 Already Bound by Discovery or Another Process

**What goes wrong:** `raop_start_httpd()` tries to bind TCP port 7000. If something else already has it (unlikely but possible), it returns a different port. The mDNS advertisement already says port 7000. The receiver is invisible or connections are refused.

**How to avoid:** Call `raop_start_httpd(&port)` where `port` is initialized to 7000. Log a fatal error if the returned port differs from 7000 (do not silently accept a different port without also updating the mDNS advertisement). Since `DiscoveryManager` starts before `AirPlayHandler`, ensure no other component binds 7000 before `AirPlayHandler::start()` is called.

### Pitfall 3: appsrc Caps Not Set Before First Push → Pipeline Negotiation Failure

**What goes wrong:** Pushing a buffer into `appsrc` before setting caps causes GStreamer to try to auto-detect the stream format. H.264 Annex B detection via GstTypeFinder is unreliable for short NAL units. The pipeline enters an error state and the first ~10 frames are lost (visible as a black flash at session start).

**How to avoid:** Set caps on the `appsrc` element during `MediaPipeline::init()` (Phase 1's `init()` needs to be extended to accept a mode: `TestSources` vs `AppsrcSources`). Caps must be set before `gst_element_set_state(pipeline, GST_STATE_PLAYING)`. For video: `video/x-h264, stream-format=byte-stream, alignment=nal`. For audio: determined by `audio_get_format` callback which fires at SETUP time, before the first audio frame.

### Pitfall 4: A/V Sync Lost Because Video and Audio appsrc Have Different Base Times

**What goes wrong:** The video `appsrc` and audio `appsrc` are pushed from different UxPlay worker threads. Each push has a PTS derived from `ntp_time_local`. If the base time used to normalize PTS is not identical for both streams, audio will run ahead of or behind video from the very first frame.

**How to avoid:** Capture the pipeline base time ONCE when the first frame arrives (whichever stream arrives first): `m_basetime = gst_element_get_base_time(m_pipeline)`. Store it as a member of `AirPlayHandler`. Both `onVideoFrame()` and `onAudioFrame()` use the same `m_basetime` for PTS normalization. This is the exact pattern UxPlay's own renderer uses (`gst_video_pipeline_base_time` / `gst_audio_pipeline_base_time` are both captured at `gst_element_set_state → PLAYING` transition, not per-stream).

### Pitfall 5: UxPlay lib/ CMakeLists.txt Calls find_package(OpenSSL 1.1.1) — Fails with OpenSSL 3.x System Install

**What goes wrong:** `vendor/uxplay/lib/CMakeLists.txt` specifies `find_package(OpenSSL 1.1.1 REQUIRED)` with an exact version requirement (1.1.1). On systems with OpenSSL 3.x, CMake's `find_package` may fail the version check depending on the CMake version and how OpenSSL exports its version.

**How to avoid:** Before running `add_subdirectory(vendor/uxplay/lib)`, set a CMake variable to override or patch the version requirement. Alternatively, apply a minimal patch to `vendor/uxplay/lib/CMakeLists.txt` to change `find_package(OpenSSL 1.1.1)` to `find_package(OpenSSL REQUIRED)` (no version pin, using the already-found OpenSSL 3.x target from the root CMakeLists). Since we already call `find_package(OpenSSL REQUIRED)` in the root CMakeLists.txt, the OpenSSL targets are already resolved — we can set `OPENSSL_FOUND=TRUE` and `OPENSSL_VERSION` before the subdirectory is added so UxPlay's find_package becomes a no-op.

**Practical approach:**
```cmake
# Root CMakeLists.txt — BEFORE add_subdirectory(vendor/uxplay/lib)
find_package(OpenSSL REQUIRED)
set(OPENSSL_FOUND TRUE)         # suppress re-find in uxplay/lib
set(OPENSSL_VERSION "3.0.0")    # satisfy version check (UxPlay only needs >=1.1.1)
```

### Pitfall 6: ALAC Audio Requires Different appsrc Caps Than AAC-ELD

**What goes wrong:** Both ALAC and AAC-ELD are valid AirPlay audio formats. If caps are hardcoded to AAC, ALAC streams either produce silence or pipeline errors.

**How to avoid:** Do not set audio appsrc caps at pipeline init. Set them dynamically in the `audio_get_format` callback when the codec type (`ct` byte) is known. Call `gst_app_src_set_caps()` before pushing the first audio buffer. This requires putting the audio `appsrc` into `GST_APP_STREAM_TYPE_STREAM` mode with `format=time`.

### Pitfall 7: UxPlay's dnssd.c Registers Its Own mDNS Services

**What goes wrong:** `lib/dnssd.c` contains Avahi-based mDNS code. If `raop_init2()` is called in a way that activates DNS-SD registration, it will attempt to register `_airplay._tcp` a second time, causing Avahi to rename it (appending `#2`) or conflict with the existing registration from `DiscoveryManager`.

**How to avoid:** Pass `NULL` for the `dnssd_t` parameter if available, or ensure `raop_init2` does not receive configuration that triggers dnssd registration. Review `uxplay.cpp` to determine whether dnssd initialization is required for `raop_init2` to work or is only needed for the advertisement path that MyAirShow replaces.

---

## Code Examples

Verified patterns from UxPlay source (GitHub: FDH2/UxPlay):

### AirPlayHandler Skeleton

```cpp
// Source: verified callback signatures from lib/raop.h + lib/stream.h
// Pattern: verified from renderers/video_renderer.c + renderers/audio_renderer.c

class AirPlayHandler : public ProtocolHandler {
public:
    AirPlayHandler(ConnectionBridge* connectionBridge,
                   const std::string& deviceId,
                   const std::string& keyfilePath);

    bool start() override {
        raop_callbacks_t callbacks{};
        callbacks.cls             = this;
        callbacks.video_process   = &AirPlayHandler::sVideoProcess;
        callbacks.audio_process   = &AirPlayHandler::sAudioProcess;
        callbacks.conn_init       = &AirPlayHandler::sConnInit;
        callbacks.conn_destroy    = &AirPlayHandler::sConnDestroy;
        callbacks.audio_get_format = &AirPlayHandler::sAudioGetFormat;
        callbacks.report_client_request = &AirPlayHandler::sReportClientRequest;

        m_raop = raop_init(&callbacks);
        if (!m_raop) return false;

        unsigned short port = 7000;
        raop_init2(m_raop, 0, m_deviceId.c_str(), m_keyfilePath.c_str());
        if (raop_start_httpd(m_raop, &port) < 0) return false;

        m_running = true;
        return true;
    }

    void stop() override {
        if (m_raop) {
            raop_stop_httpd(m_raop);
            raop_destroy(m_raop);
            m_raop = nullptr;
        }
        m_running = false;
    }

private:
    static void sVideoProcess(void* cls, raop_ntp_t* ntp, video_decode_struct* data) {
        static_cast<AirPlayHandler*>(cls)->onVideoFrame(data);
    }
    static void sAudioProcess(void* cls, raop_ntp_t* ntp, audio_decode_struct* data) {
        static_cast<AirPlayHandler*>(cls)->onAudioFrame(data);
    }
    static void sConnInit(void* cls) {
        auto* h = static_cast<AirPlayHandler*>(cls);
        // Capture pipeline base time on first connection
        if (h->m_pipeline) {
            h->m_basetime = gst_element_get_base_time(h->m_pipeline->gstPipeline());
        }
        // Update ConnectionBridge on Qt main thread via queued signal
        QMetaObject::invokeMethod(h->m_connectionBridge,
            [h]() { h->m_connectionBridge->setConnected(true, h->m_deviceName, "AirPlay"); },
            Qt::QueuedConnection);
    }
    static void sConnDestroy(void* cls) {
        auto* h = static_cast<AirPlayHandler*>(cls);
        QMetaObject::invokeMethod(h->m_connectionBridge,
            [h]() { h->m_connectionBridge->setConnected(false); },
            Qt::QueuedConnection);
    }

    void onVideoFrame(video_decode_struct* data);
    void onAudioFrame(audio_decode_struct* data);

    raop_t*           m_raop           = nullptr;
    GstElement*       m_videoAppsrc    = nullptr;
    GstElement*       m_audioAppsrc    = nullptr;
    MediaPipeline*    m_pipeline       = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    uint64_t          m_basetime       = 0;
    std::string       m_deviceId;
    std::string       m_keyfilePath;
    std::string       m_deviceName;
    bool              m_running        = false;
};
```

### appsrc Video Frame Push

```cpp
// Source: pattern from UxPlay renderers/video_renderer.c
void AirPlayHandler::onVideoFrame(video_decode_struct* data) {
    if (!m_videoAppsrc || data->data_len <= 0) return;

    GstBuffer* buf = gst_buffer_new_allocate(nullptr, data->data_len, nullptr);
    gst_buffer_fill(buf, 0, data->data, data->data_len);

    uint64_t pts = data->ntp_time_local;
    if (pts >= m_basetime) {
        GST_BUFFER_PTS(buf) = pts - m_basetime;
    } else {
        GST_BUFFER_PTS(buf) = GST_CLOCK_TIME_NONE;
    }

    if (gst_app_src_push_buffer(GST_APP_SRC(m_videoAppsrc), buf) != GST_FLOW_OK) {
        g_warning("AirPlayHandler: video appsrc push failed");
    }
}
```

### MediaPipeline appsrc Mode Init

```cpp
// New init method for Phase 4 — replaces videotestsrc with appsrc
bool MediaPipeline::initAppsrcPipeline(void* qmlVideoItem) {
    // Video: appsrc ! h264parse ! [decoder] ! videoconvert ! glupload ! qml6glsink
    // Audio: appsrc ! [aac/alac decoder] ! audioconvert ! audioresample ! autoaudiosink

    GstElement* pipeline   = gst_pipeline_new("myairshow-pipeline");
    GstElement* videoSrc   = gst_element_factory_make("appsrc",      "video_appsrc");
    GstElement* h264parse  = gst_element_factory_make("h264parse",   "h264parse");
    // ... decoder selection (hardware first, avdec_h264 fallback) ...
    GstElement* audioSrc   = gst_element_factory_make("appsrc",      "audio_appsrc");

    // Set video appsrc caps
    GstCaps* vcaps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=nal");
    g_object_set(videoSrc, "caps", vcaps,
                            "stream-type", 0,   // GST_APP_STREAM_TYPE_STREAM
                            "format", GST_FORMAT_TIME,
                            "is-live", TRUE, nullptr);
    gst_caps_unref(vcaps);

    // Audio caps set later via audio_get_format callback
    g_object_set(audioSrc, "stream-type", 0,
                            "format", GST_FORMAT_TIME,
                            "is-live", TRUE, nullptr);
    // ...
}
```

### ConnectionBridge Thread-Safe Update

```cpp
// UxPlay callbacks fire on worker threads, not the Qt main thread.
// QMetaObject::invokeMethod with Qt::QueuedConnection is the correct pattern.
// Source: Qt6 documentation on thread affinity of QObject signals.
static void sConnInit(void* cls) {
    auto* h = static_cast<AirPlayHandler*>(cls);
    QMetaObject::invokeMethod(h->m_connectionBridge,
        [h]() {
            h->m_connectionBridge->setConnected(
                true,
                QString::fromStdString(h->m_deviceName),
                QStringLiteral("AirPlay"));
        },
        Qt::QueuedConnection);
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| AirPlay 1 (shairplay, shairport) | AirPlay 2 (UxPlay, shairport-sync 4.x) | iOS 14+ | AirPlay 1 receivers no longer appear in iOS screen mirroring menu on iOS 14+ |
| SRP auth bypass / no pairing | Ed25519 keypair + SRP6a pairing | iOS 14, hardened in iOS 17 | Non-paired receivers require FairPlay SRP; UxPlay 1.73 handles this |
| Separate audio and video pipelines per session | Single shared GStreamer pipeline, appsrc injection | UxPlay >= 1.65 architectural guidance | Eliminates duplicate decoder instances, enables shared clock A/V sync |
| Hard-coded AAC audio | Runtime codec detection (ct byte in audio_get_format) | UxPlay 1.70+ | ALAC is used in some AirPlay configurations; hard-coding AAC causes silence |
| OpenSSL 1.1.x | OpenSSL 3.x | OpenSSL 1.1.1 EOL Sep 2023 | Must not mix 1.x and 3.x in same binary; libplist and UxPlay must all use same OpenSSL |

**Deprecated / outdated:**
- `shairplay`: AirPlay 1 only, unmaintained — use UxPlay
- RPiPlay (FD-/RPiPlay original): Pi-specific OpenMAX backend — use UxPlay (FDH2 fork)
- UxPlay < 1.65: Does not support the current iOS 17/18 pairing flow — use 1.73.6

---

## Open Questions

1. **UxPlay dnssd.c in lib/ — does `raop_init2` require a non-NULL dnssd_t?**
   - What we know: `lib/dnssd.c` is compiled into the `airplay` static target. The function `raop_init2` takes `nohold` and `device_id` and `keyfile` but its interaction with dnssd is unclear from headers alone.
   - What's unclear: Whether `NULL` can be safely passed for any dnssd parameter, or whether `raop_init2` internally calls dnssd functions unconditionally.
   - Recommendation: Read `lib/raop.c` source in the submodule after checkout to verify. If dnssd is called unconditionally, add a stub dnssd_init that does nothing, or set a compile flag to skip it.

2. **Public key extraction from UxPlay keyfile format**
   - What we know: `raop_init2` accepts a `keyfile` path. UxPlay writes the keypair to this file on first run. The `pk` TXT record must be the hex-encoded Ed25519 public key (64 bytes = 128 hex chars).
   - What's unclear: The exact keyfile format (binary? PEM? custom?) and whether UxPlay exposes an API to read back the public key after `raop_init`.
   - Recommendation: After adding the submodule, check `lib/pairing.c` for the write path. If no API exists to read back the public key, parse the keyfile directly after `raop_init2` succeeds.

3. **MediaPipeline refactor scope for appsrc mode**
   - What we know: `MediaPipeline` currently only has `init()` (videotestsrc) and `initDecoderPipeline()`. Phase 4 needs an `initAppsrcPipeline()` method that builds the appsrc-based pipeline and returns named element pointers.
   - What's unclear: Whether `initAppsrcPipeline()` should be a new method or replace `init()`, and how to handle the test target (which tests `videotestsrc` and cannot depend on UxPlay).
   - Recommendation: Add `initAppsrcPipeline()` as a new method. Keep `init()` for tests. `AirPlayHandler::setMediaPipeline()` calls `initAppsrcPipeline()` then retrieves the `appsrc` element handles via `gst_bin_get_by_name()`.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|-------------|-----------|---------|----------|
| GStreamer 1.26.x | appsrc pipeline | Already verified in project | 1.20+ (confirmed from CMakeLists.txt check) | — (required) |
| OpenSSL 3.x | UxPlay FairPlay, already linked | Already in project | 3.x (from Phase 1) | — (required) |
| libplist-dev | UxPlay lib/ link | Needs install | 2.6–2.7 | No fallback — required by UxPlay |
| gst-plugins-good (h264parse) | Video decode pipeline | Likely present | 1.20+ | Must be verified at startup |
| gst-libav (avdec_h264, avdec_aac) | Software fallback decode | Likely present | 1.20+ | Must be verified at startup |
| Git submodule support | UxPlay vendor embed | ✓ | git ≥ 2.x | — |

**Missing dependencies to verify before planning:**
- `libplist-dev` — not yet in project; must be added to `vcpkg.json` or system install instructions
- `h264parse` plugin — part of `gst-plugins-bad` or `gst-plugins-good` depending on distribution; must be added to `checkRequiredPlugins()` in `main.cpp`
- `avdec_aac` — part of `gst-libav`; must be added to plugin check

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test (CMake `FetchContent` or system) — pattern from existing `test_foundation`, `test_discovery`, `test_display` targets |
| Config file | None — CMake `enable_testing()` + `add_test()` pattern used in prior phases |
| Quick run command | `ctest -R test_airplay --output-on-failure` |
| Full suite command | `ctest --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| AIRP-01 | AirPlayHandler::start() returns true; RAOP server binds port 7000 | unit | `ctest -R test_airplay -V` | ❌ Wave 0 |
| AIRP-02 | Same RAOP code path; macOS-specific coverage in CI matrix | unit (CI) | `ctest -R test_airplay -V` | ❌ Wave 0 |
| AIRP-03 | video_process + audio_process callbacks both push buffers; PTS values are non-zero and close to each other (< 200ms delta) | unit | `ctest -R test_airplay -V` | ❌ Wave 0 |
| AIRP-04 | Simulated 60-second stream; PTS delta between video and audio stays < 50ms after 1000 frames | unit (simulated) | `ctest -R test_airplay -V` | ❌ Wave 0 |

**Note on live device testing:** AIRP-01 (iPhone/iPad actual mirroring) and AIRP-02 (Mac mirroring) require a real Apple device and cannot be automated in CI. The automated tests verify the plumbing (RAOP server starts, appsrc accepts buffers, PTS math is correct). Manual integration testing with a real iOS device is the gate for AIRP-01/02 verification.

### Wave 0 Gaps
- [ ] `tests/test_airplay.cpp` — unit tests for AirPlayHandler (start/stop, mock callbacks, appsrc push)
- [ ] Mock `raop_t` or test harness that fires callbacks without a real iOS device
- [ ] `checkRequiredPlugins()` additions: `h264parse`, `avdec_aac`

*(No new test framework needed — existing CMake test pattern from prior phases applies)*

---

## Sources

### Primary (HIGH confidence)
- `FDH2/UxPlay GitHub — lib/raop.h` — `raop_callbacks_t` struct, `raop_init`, `raop_init2`, `raop_start_httpd` signatures verified
- `FDH2/UxPlay GitHub — lib/stream.h` — `video_decode_struct`, `audio_decode_struct` field definitions verified
- `FDH2/UxPlay GitHub — lib/CMakeLists.txt` — CMake target name `airplay`, `add_library(airplay STATIC)` confirmed
- `FDH2/UxPlay GitHub — renderers/video_renderer.c` — `gst_app_src_push_buffer` pattern, PTS normalization from `ntp_time_local`, `appsrc` pipeline string verified
- `FDH2/UxPlay GitHub — renderers/audio_renderer.c` — `g_string_append` pipeline construction, AAC/ALAC codec detection via `ct` byte verified
- Existing MyAirShow codebase — `ProtocolHandler.h`, `MediaPipeline.h/cpp`, `ConnectionBridge.h/cpp`, `DiscoveryManager.cpp` — all interfaces and integration points confirmed by direct read

### Secondary (MEDIUM confidence)
- `.planning/research/STACK.md` — UxPlay dependency chain, OpenSSL 3.x requirement, libplist version
- `.planning/research/ARCHITECTURE.md` — appsrc injection pattern, shared pipeline architecture, data flow diagram
- `.planning/research/PITFALLS.md` — A/V sync drift (NTP), AirPlay protocol fragility on iOS updates, DRM handling

### Tertiary (LOW confidence)
- WebFetch of UxPlay `uxplay.cpp` — renderer abstraction description; implementation details incomplete from WebFetch (full source not shown)

---

## Project Constraints (from CLAUDE.md)

| Constraint | Directive |
|------------|-----------|
| Language | C++17 — no other language for core |
| GUI | Qt 6.8 LTS — no Electron, no wxWidgets |
| Video pipeline | GStreamer 1.26.x — use `qml6glsink`; do NOT use `QMediaPlayer` for protocol video output |
| Crypto | OpenSSL 3.x — do NOT use 1.1.1 |
| Build | CMake ≥ 3.28 + vcpkg manifest mode |
| AirPlay library | UxPlay (FDH2 fork) embedded — do NOT use shairplay or RPiPlay original |
| Cost | Completely free, open source — no commercial SDKs |
| Discovery | DiscoveryManager owns all mDNS — AirPlayHandler must NOT call UxPlay's own dnssd functions |
| Pipeline | Single shared MediaPipeline — AirPlayHandler injects via appsrc, does NOT create its own GStreamer pipeline |
| Thread safety | UxPlay callbacks fire on worker threads; ConnectionBridge updates MUST use `Qt::QueuedConnection` |

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — UxPlay source structure, static library target, and callback signatures verified directly from GitHub source
- Architecture patterns: HIGH — appsrc injection pattern confirmed from UxPlay's own renderer code; CMake integration approach derived from lib/CMakeLists.txt analysis
- Pitfalls: HIGH for pk mismatch (documented UxPlay behavior), appsrc caps, and A/V base time; MEDIUM for dnssd collision (requires submodule checkout to fully verify)

**Research date:** 2026-03-28
**Valid until:** 2026-06-28 (stable stack; UxPlay RAOP auth may change within 30 days of a major iOS release — monitor FDH2/UxPlay issues after any iOS update)
