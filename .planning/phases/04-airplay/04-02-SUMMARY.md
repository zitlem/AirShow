---
phase: 04-airplay
plan: 02
subsystem: protocol
tags: [uxplay, raop, airplay, gstreamer, appsrc, openssl, ed25519, mdns]

requires:
  - phase: 04-01
    provides: [airplay-static-lib, appsrc-pipeline, txt-record-update, uxplay-submodule]
  - phase: 03-display-receiver-ui
    provides: [ConnectionBridge, ProtocolHandler, MediaPipeline]

provides:
  - AirPlayHandler class implementing ProtocolHandler — full RAOP server lifecycle
  - H.264 video and AAC/ALAC audio injection into MediaPipeline via appsrc
  - Ed25519 public key extraction from PEM keyfile for mDNS TXT record pk field
  - Thread-safe ConnectionBridge updates via QMetaObject::invokeMethod (QueuedConnection)

affects: [main.cpp, Phase 04 Plan 03, any code instantiating AirPlayHandler]

tech-stack:
  added: []
  patterns:
    - "File-scope C trampolines bridge raop_callbacks_t (C API) to C++ instance methods — avoids anonymous struct type leakage into public header"
    - "void* storage for GstElement* and opaque struct pointers in header — cast to correct type in .cpp where headers are included"
    - "LANGUAGES C added to project() for UxPlay lib/ C sources to compile under CMake"
    - "QMetaObject::invokeMethod with QueuedConnection for cross-thread Qt signal emission from RAOP callbacks"

key-files:
  created:
    - src/protocol/AirPlayHandler.h
    - src/protocol/AirPlayHandler.cpp
  modified:
    - CMakeLists.txt (added AirPlayHandler.cpp to sources; added LANGUAGES C to project())

key-decisions:
  - "File-scope C trampoline functions (not static class methods) used for raop_callbacks_t — allows exact signature match with UxPlay's C callback types without including stream.h/raop.h in the public header"
  - "readPublicKeyFromKeyfile() uses OpenSSL EVP_PKEY to read PEM key (not 64-byte binary) — actual UxPlay crypto.c writes PEM via PEM_write_bio_PrivateKey; plan spec was incorrect"
  - "LANGUAGES C added to project() — required so CMake compiles UxPlay lib/ C source files; previously only mocs_compilation.cpp.o was built causing empty libairplay.a"
  - "void* used for GstElement* and audio/video decode struct pointers in header — GstElement typedef conflicts with forward decl; stream.h types are anonymous structs (cannot be forward-declared)"
  - "raop_set_dnssd() not called — MyAirShow's DiscoveryManager already advertises _airplay._tcp and _raop._tcp; calling it would cause duplicate mDNS entries (Pitfall 7)"
  - "m_audioCapsSet flag prevents setAudioCaps() re-invocation after first audio_get_format callback (Pitfall 6)"
  - "m_basetimeSet captures basetime once per session on first media frame — whichever arrives first (video or audio) wins (Pitfall 4)"

patterns-established:
  - "Protocol handler C callback pattern: file-scope trampolines with cls=this cast, public instance methods for the implementation logic"
  - "OpenSSL PEM key read pattern: PEM_read_PrivateKey + EVP_PKEY_get_raw_public_key for Ed25519 public key extraction"

requirements-completed: [AIRP-01, AIRP-02, AIRP-03, AIRP-04]

duration: ~15min
completed: 2026-03-28
---

# Phase 4 Plan 2: AirPlayHandler — RAOP Server, Frame Injection, Session Management

**AirPlayHandler wraps UxPlay's RAOP server with C-to-C++ callback trampolines, pushes H.264/AAC frames into MediaPipeline via appsrc with NTP-derived PTS, and updates the mDNS pk TXT record from the PEM-encoded Ed25519 keyfile**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-03-28
- **Completed:** 2026-03-28
- **Tasks:** 2
- **Files modified:** 3 (AirPlayHandler.h created, AirPlayHandler.cpp created, CMakeLists.txt modified)

## Accomplishments

- Full `ProtocolHandler` implementation wrapping UxPlay's RAOP server lifecycle (`raop_init`, `raop_init2`, `raop_start_httpd`, `raop_stop_httpd`, `raop_destroy`)
- All 7 `raop_callbacks_t` callbacks registered: `video_process`, `audio_process`, `conn_init`, `conn_destroy`, `conn_teardown`, `audio_get_format`, `report_client_request`
- H.264 video and AAC/ALAC audio frames injected into GStreamer appsrc with NTP-to-pipeline basetime PTS normalization
- Ed25519 public key extracted from PEM keyfile via OpenSSL EVP_PKEY and hex-encoded (64-char lowercase) for mDNS TXT record update
- Thread-safe `ConnectionBridge::setConnected()` invocation via `QMetaObject::invokeMethod(Qt::QueuedConnection)` from RAOP callback threads
- Build verified: `cmake --build` exits 0 with all 67 targets compiled and linked

## Task Commits

1. **Task 1: AirPlayHandler header** - `b0bf5d2` (feat)
2. **Task 2: AirPlayHandler implementation** - `c2f10dd` (feat)

## Files Created/Modified

- `src/protocol/AirPlayHandler.h` — Class declaration: ProtocolHandler interface, public callback methods for C trampolines, void* storage for GstElement and decode struct pointers
- `src/protocol/AirPlayHandler.cpp` — Full implementation: RAOP lifecycle, 7 C trampolines, frame injection with PTS, session management, OpenSSL PEM key reading
- `CMakeLists.txt` — Added `src/protocol/AirPlayHandler.cpp` to `qt_add_executable` source list; changed `LANGUAGES CXX` to `LANGUAGES CXX C`

## Decisions Made

- **File-scope C trampolines** used instead of static class methods for `raop_callbacks_t` — the callback structs require exact C function pointer types; using file-scope functions avoids the need to include UxPlay's `stream.h` (anonymous struct typedefs) in the public header
- **`readPublicKeyFromKeyfile()` uses OpenSSL PEM** — the plan spec described a 64-byte binary keyfile format, but UxPlay's `crypto.c` actually writes a PEM private key via `PEM_write_bio_PrivateKey`. Fixed to use `PEM_read_PrivateKey` + `EVP_PKEY_get_raw_public_key`
- **`LANGUAGES C` added to `project()`** — UxPlay lib/ contains C source files compiled via `aux_source_directory`; without `LANGUAGES C`, CMake's `LANGUAGES CXX`-only mode skips C compilation and produces an empty `libairplay.a`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Keyfile format is PEM, not 64-byte binary**
- **Found during:** Task 2 (implementing `readPublicKeyFromKeyfile()`)
- **Issue:** Plan spec stated "64-byte binary file containing Ed25519 seed + public key". Actual inspection of `vendor/uxplay/lib/crypto.c` shows `PEM_write_bio_PrivateKey` is called — the file is a standard PEM private key, not raw bytes
- **Fix:** Implemented `readPublicKeyFromKeyfile()` using `PEM_read_PrivateKey` + `EVP_PKEY_get_raw_public_key` to extract the 32-byte Ed25519 public key, then hex-encoded to 64-char lowercase string
- **Files modified:** `src/protocol/AirPlayHandler.cpp`
- **Verification:** OpenSSL API successfully reads the PEM key format that UxPlay generates
- **Committed in:** c2f10dd (Task 2 commit)

**2. [Rule 3 - Blocking] libairplay.a was empty — UxPlay C sources not compiled**
- **Found during:** Task 2 (link step after successful compile)
- **Issue:** CMake project was declared with `LANGUAGES CXX` only. UxPlay `lib/CMakeLists.txt` uses `aux_source_directory(. play_src)` to gather `.c` files, but without `LANGUAGES C` in the parent project, CMake skips C compilation — producing `libairplay.a` with only `mocs_compilation.cpp.o` (2KB). The link step failed with `undefined reference to raop_init`
- **Fix:** Changed `project(MyAirShow LANGUAGES CXX)` to `project(MyAirShow LANGUAGES CXX C)` in `CMakeLists.txt`
- **Files modified:** `CMakeLists.txt`
- **Verification:** After reconfigure + rebuild, all 67 targets compiled; binary links successfully
- **Committed in:** c2f10dd (Task 2 commit)

**3. [Rule 1 - Bug] GstElement forward declaration conflicts with GStreamer typedef**
- **Found during:** Task 2 (first compile attempt)
- **Issue:** `struct GstElement;` forward declaration in header conflicts with GStreamer's `typedef struct _GstElement GstElement`. Similarly, `video_decode_struct` and `audio_decode_struct` are anonymous typedef structs in stream.h (cannot be forward-declared as named structs)
- **Fix:** Stored `GstElement*` as `void*` in the header; cast to `GstElement*` in the .cpp. Used file-scope trampolines instead of static class methods so UxPlay struct types never appear in the header
- **Files modified:** `src/protocol/AirPlayHandler.h`, `src/protocol/AirPlayHandler.cpp`
- **Verification:** Compile succeeds with no type conflicts
- **Committed in:** c2f10dd (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs, 1 Rule 3 blocking)
**Impact on plan:** All fixes were necessary for correctness and compilation. No scope creep — the fix for PEM vs. binary format is a pure correctness fix for the Ed25519 key extraction path.

## Issues Encountered

The three deviations above were the only issues. All resolved automatically during execution.

## Known Stubs

None — AirPlayHandler is fully implemented. The only stub is that `setMediaPipeline()` calls `initAppsrcPipeline(nullptr)` (no QML video item); `ReceiverWindow` will call `initAppsrcPipeline(videoItem)` in Plan 03 to wire the live video output.

## Next Phase Readiness

- `AirPlayHandler` is ready to be instantiated in `main.cpp` (Plan 03)
- `ProtocolManager::registerHandler()` exists and will accept an `AirPlayHandler` instance
- `DiscoveryManager::updateTxtRecord()` is already wired (Plan 01)
- `ConnectionBridge::setConnected()` is already wired (Phase 03)
- The RAOP server can be started with `handler.start()` after `setMediaPipeline()` is called

---
*Phase: 04-airplay*
*Completed: 2026-03-28*
