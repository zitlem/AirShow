---
phase: 06-google-cast
plan: 01
subsystem: protocol
tags: [castv2, protobuf, tls, qsslserver, openssl, cast-auth, gstreamer, webrtc-stub]

requires:
  - phase: 05-dlna
    provides: MediaPipeline.initUriPipeline/setUri/playUri for media LOAD handling
  - phase: 03-display-receiver-ui
    provides: ConnectionBridge.setConnected() for Cast session lifecycle events
  - phase: 02-discovery-protocol-abstraction
    provides: ProtocolHandler interface that CastHandler implements

provides:
  - CastHandler: QObject+ProtocolHandler, QSslServer TLS on port 8009, self-signed RSA-2048 cert
  - CastSession: CASTV2 state machine with 6 namespace handlers (deviceauth, connection, heartbeat, receiver, media, webrtc stub)
  - cast_channel.proto: authoritative CASTV2 protobuf definition (CastMessage, DeviceAuthMessage, AuthChallenge, AuthResponse, AuthError)
  - cast_auth_sigs.h: 795x256-byte precomputed RSA-2048 signature table with getCastAuthSignature() lookup
  - test_cast: 7 unit tests for framing, auth structure, signature rotation, handler lifecycle

affects: [06-google-cast-02, 06-google-cast-03, protocol-manager, discovery-manager]

tech-stack:
  added:
    - libprotobuf 3.21.12 (system; vendored headers at /tmp/protobuf-dev/usr)
    - protoc 3.21.12 (via /tmp/protoc-wrapper.sh — wrapper needed for LD_LIBRARY_PATH)
    - Qt6::Network (QSslServer/QSslSocket for TLS)
    - gstreamer-webrtc-1.0 + gstreamer-sdp-1.0 (optional pkg_check_modules, for Plan 02)
  patterns:
    - QSslServer + pendingConnectionAvailable signal for TLS accept (no manual threading)
    - Accumulation buffer state machine for TCP framing (ReadState::READING_HEADER/READING_PAYLOAD)
    - Precomputed signature bypass for Cast device auth: (unix_time/172800)%795 index lookup
    - protobuf_generate_cpp before add_subdirectory(tests) with CACHE INTERNAL proto vars

key-files:
  created:
    - src/cast/cast_channel.proto
    - src/cast/cast_auth_sigs.h
    - src/protocol/CastHandler.h
    - src/protocol/CastHandler.cpp
    - src/protocol/CastSession.h
    - src/protocol/CastSession.cpp
    - tests/test_cast.cpp
  modified:
    - CMakeLists.txt (Qt6::Network, Protobuf, protobuf_generate_cpp, GST_WEBRTC optional)
    - tests/CMakeLists.txt (test_cast target with CAST_PROTO_SRCS cache variable)

key-decisions:
  - "libprotobuf-dev not system-installed; vendored to /tmp/protobuf-dev via apt-get download + dpkg-deb; protoc wrapped in /tmp/protoc-wrapper.sh for LD_LIBRARY_PATH"
  - "QStringLiteral cannot accept const char* variables; replaced with QLatin1StringView for kAppIdChromeMirror/kAppIdDefaultMedia comparisons"
  - "std::pair<QSslCertificate,QSslKey> uses explicit default constructor in C++17; must use std::make_pair on failure path"
  - "cast_auth_sigs.h uses CAST_SIG_ROW macro producing exactly 256 bytes (2 index + 254 sequence bytes 0x01..0xFE)"
  - "getCastAuthSignature() moved below kCastAuthSignatures definition to avoid extern+static conflict"
  - "test_cast QCoreApplication managed as raw pointer with processEvents() in TearDown to fix teardown segfault"

patterns-established:
  - "Pattern: QSslServer accept loop via pendingConnectionAvailable signal (no blocking accept)"
  - "Pattern: CastSession TCP framing uses accumulation buffer m_buffer with ReadState enum — never blocking socket reads"
  - "Pattern: Auth bypass uses (QDateTime::currentSecsSinceEpoch() / 172800) % 795 index into precomputed table"

requirements-completed: [CAST-01, CAST-02]

duration: 10min
completed: 2026-03-29
---

# Phase 6 Plan 01: Google Cast CASTV2 Protocol Layer Summary

**CASTV2 protocol layer: QSslServer TLS on port 8009 with OpenSSL 3.x self-signed cert, length-prefixed protobuf framing, 6-namespace dispatcher, precomputed RSA-2048 auth bypass, and 7 passing unit tests**

## Performance

- **Duration:** 10 min
- **Started:** 2026-03-29T04:10:46Z
- **Completed:** 2026-03-29T04:21:18Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments

- CastHandler implements ProtocolHandler with QSslServer listening on port 8009 using a runtime-generated self-signed RSA-2048/SHA-256 certificate via OpenSSL 3.x EVP API; single-session model (D-14) replaces active session on new connection
- CastSession implements full CASTV2 framing (4-byte big-endian uint32 length prefix + protobuf payload via non-blocking accumulation buffer state machine) with namespace dispatch for all 6 cast namespaces; auth bypass sends precomputed signature from 795-entry table indexed by (unix_time/172800)%795; heartbeat PONG; receiver LAUNCH/GET_STATUS/STOP; media GET_STATUS/LOAD (calls initUriPipeline+setUri+playUri matching DLNA pattern); webrtc stub logs OFFER received
- All 7 unit tests pass: CastMessage serialization round-trip, length-prefix encoding, AuthResponse protobuf structure, signature index rotation across 48h windows, CastHandler start/stop lifecycle, cert generation idempotency, signature table constants

## Task Commits

1. **Task 1: Protobuf codegen, auth signatures, CastHandler TLS server, CastSession CASTV2 framing** - `6ffeafe` (feat)
2. **Task 2: Unit tests for CASTV2 framing, namespace dispatch, auth structure** - `50b370f` (test)

## Files Created/Modified

- `src/cast/cast_channel.proto` - CASTV2 proto2 definition (CastMessage, DeviceAuthMessage, AuthChallenge, AuthResponse, AuthError)
- `src/cast/cast_auth_sigs.h` - 795x256-byte precomputed RSA signature table; getCastAuthSignature() lookup; kCastAuthPeerCert placeholder
- `src/protocol/CastHandler.h` - QObject+ProtocolHandler interface; QSslServer member; generateSelfSignedCert() private method
- `src/protocol/CastHandler.cpp` - TLS server setup, cert generation via OpenSSL 3.x EVP_PKEY_CTX, connection accept loop
- `src/protocol/CastSession.h` - Per-connection session state machine; ReadState enum; 6 namespace handler declarations
- `src/protocol/CastSession.cpp` - CASTV2 framing state machine; all 6 namespace handlers; sendMessage/makeJsonMsg/buildReceiverStatus helpers
- `tests/test_cast.cpp` - 7 unit tests covering framing, auth, signature rotation, handler lifecycle
- `CMakeLists.txt` - Added Qt6::Network, find_package(Protobuf), protobuf_generate_cpp, GST_WEBRTC optional, CACHE vars
- `tests/CMakeLists.txt` - Added test_cast target with CAST_PROTO_SRCS reference

## Decisions Made

- **protobuf vendoring:** libprotobuf-dev not system-installed; used `apt-get download` + `dpkg-deb -x` to extract headers and static libs to /tmp/protobuf-dev. protoc binary needs libprotoc.so.32 from a separate runtime package (libprotoc32t64). Wrapped in /tmp/protoc-wrapper.sh with LD_LIBRARY_PATH.
- **protoc CMake integration:** Used `-DProtobuf_PROTOC_EXECUTABLE=/tmp/protoc-wrapper.sh` to route CMake's protobuf_generate_cpp through the wrapper script.
- **QLatin1StringView vs QStringLiteral:** QStringLiteral macro requires a string literal at compile time; const char* file-scope variables cannot be passed to it. Used QLatin1StringView for runtime comparisons.
- **std::pair explicit constructor:** GCC 15 (C++17 strict mode) treats `return {}` as calling explicit default constructor of pair — must use `std::make_pair(QSslCertificate{}, QSslKey{})` on failure paths.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] libprotobuf-dev and protoc not system-installed**
- **Found during:** Task 1 (CMakeLists.txt update)
- **Issue:** libprotobuf-dev package not installed; no system protoc binary; required for protobuf_generate_cpp
- **Fix:** Downloaded packages with `apt-get download`, extracted with `dpkg-deb -x` to /tmp/protobuf-dev. Created /tmp/protoc-wrapper.sh to set LD_LIBRARY_PATH. Configured CMake with -DProtobuf_PROTOC_EXECUTABLE pointing to wrapper.
- **Files modified:** CMakeLists.txt (CMAKE_PREFIX_PATH addition)
- **Verification:** `protoc --version` returns `libprotoc 3.21.12`; `ninja myairshow` succeeds with proto codegen
- **Committed in:** 6ffeafe (part of Task 1 commit)

**2. [Rule 1 - Bug] QStringLiteral cannot accept const char* variables**
- **Found during:** Task 1 (CastSession.cpp compile)
- **Issue:** Used `QStringLiteral(kAppIdChromeMirror)` where kAppIdChromeMirror is a `const char*`; QStringLiteral requires a string literal
- **Fix:** Replaced with `QLatin1StringView(kAppIdChromeMirror)` for runtime const char* comparisons
- **Files modified:** src/protocol/CastSession.cpp
- **Committed in:** 6ffeafe

**3. [Rule 1 - Bug] std::pair explicit default constructor in C++17**
- **Found during:** Task 1 (CastHandler.cpp compile)
- **Issue:** `return {}` inside generateSelfSignedCert() fails with GCC 15 — explicit default constructor cannot be called via brace initialization
- **Fix:** Changed all `return {}` failure paths to `return std::make_pair(QSslCertificate{}, QSslKey{})`
- **Files modified:** src/protocol/CastHandler.cpp
- **Committed in:** 6ffeafe

**4. [Rule 1 - Bug] cast_auth_sigs.h extern/static conflict**
- **Found during:** Task 1 (compile error in CastSession.cpp)
- **Issue:** getCastAuthSignature() used `extern const uint8_t kCastAuthSignatures[]` inside function body, conflicting with the `static` table definition in the same header
- **Fix:** Rewrote header to declare the static table first, then define getCastAuthSignature() after the table. Eliminated the extern declaration entirely.
- **Files modified:** src/cast/cast_auth_sigs.h
- **Committed in:** 6ffeafe

**5. [Rule 1 - Bug] test_cast teardown segfault**
- **Found during:** Task 2 (test execution)
- **Issue:** Static QCoreApplication in SetUp caused segfault during teardown from static destruction order
- **Fix:** Changed to raw pointer (s_app) with QCoreApplication::processEvents() in TearDown for clean event loop drain
- **Files modified:** tests/test_cast.cpp
- **Committed in:** 50b370f

---

**Total deviations:** 5 auto-fixed (3 bugs, 2 blocking)
**Impact on plan:** All auto-fixes required for compilation and correct test teardown. No scope creep.

## Issues Encountered

- **gst_webrtc_1.0 optional:** gstreamer-webrtc-1.0 pkg-config module may not be present on all systems; made GST_WEBRTC optional via `pkg_check_modules(GST_WEBRTC IMPORTED_TARGET ...)` without REQUIRED, with conditional `target_link_libraries` using `if(GST_WEBRTC_FOUND)`.

## Known Stubs

- **cast_auth_sigs.h `kCastAuthSignatures` table:** All 795 entries are deterministic placeholder data (2-byte index + 0x01..0xFE sequence). Chrome will reject Cast authentication until real signatures are extracted from AirReceiver APK. Code structure is correct; only binary data needs replacing.
- **cast_auth_sigs.h `kCastAuthPeerCert`:** Placeholder DER prefix only. Must be replaced with the actual ~800-1200 byte DER certificate from AirReceiver APK that the signatures were computed against.
- **CastSession::onWebrtc():** Stub that logs "Plan 02 will implement" without sending a response. Plan 02 implements the WebRTC SDP offer/answer and GStreamer webrtcbin pipeline.

These stubs are intentional and documented — Plan 02 (WebRTC media pipeline) will replace the webrtc stub. The auth signatures require out-of-band APK extraction.

## Next Phase Readiness

- CastHandler and CastSession are ready to be wired into ProtocolManager (same pattern as DlnaHandler)
- Plan 02 can implement onWebrtc() using the webrtcbin pipeline pattern from RESEARCH.md Pattern 5
- The auth signature stubs are non-blocking for Plan 02 development (Chrome will fail auth but the code path is exercised)
- The libprotobuf vendor path (/tmp/protobuf-dev) is non-persistent; build system must pass PKG_CONFIG_PATH and CMAKE_PREFIX_PATH consistently

## Self-Check: PASSED

All 9 required files exist on disk. Both task commits (6ffeafe, 50b370f) verified in git history.

---
*Phase: 06-google-cast*
*Completed: 2026-03-29*
