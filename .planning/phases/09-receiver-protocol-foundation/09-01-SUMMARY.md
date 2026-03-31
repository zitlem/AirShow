---
phase: 09-receiver-protocol-foundation
plan: 01
subsystem: protocol
tags: [tcp, json-handshake, binary-framing, gstreamer, appsrc, qt-network, airshow-protocol]

# Dependency graph
requires:
  - phase: 08-miracast
    provides: MiracastHandler pattern for TCP server, binary framing, and appsrc injection
  - phase: 07-security
    provides: SecurityManager integration pattern (setSecurityManager)
  - phase: 04-airplay
    provides: gst_app_src_push_buffer pattern for NAL unit injection
provides:
  - AirShowHandler: TCP server on port 7400 accepting companion sender connections
  - JSON handshake: HELLO/HELLO_ACK with codec, resolution, bitrate, fps negotiation
  - 16-byte binary frame parsing (type + flags + uint32 length + int64 PTS)
  - VIDEO_NAL push to MediaPipeline::videoAppsrc() via gst_app_src_push_buffer
  - GTest scaffold (3 tests): ConformsToInterface, ParseFrameHeader, HandshakeJsonRoundTrip
affects: [10-android-sender, 11-ios-sender, 12-macos-sender, 13-windows-sender, 14-web-interface]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "AirShow protocol: newline-terminated JSON handshake followed by 16-byte binary frame streaming"
    - "Static parseFrameHeader() public for unit testing without network (same as MiracastHandler::parseSourceReady)"
    - "Single-session model: new connection drops existing client (consistent with CastHandler D-14)"
    - "initAppsrcPipeline() guard: only called if videoAppsrc() is null (avoids double-init)"

key-files:
  created:
    - src/protocol/AirShowHandler.h
    - src/protocol/AirShowHandler.cpp
    - tests/test_airshow.cpp
  modified:
    - tests/CMakeLists.txt

key-decisions:
  - "Newline-terminated JSON for handshake (vs length-prefix): simplest for Flutter TCP client and matches Open Question 3 recommendation from RESEARCH.md"
  - "Echo-back negotiation for HELLO_ACK: receiver accepts sender's requested quality unchanged in v1; quality arbitration deferred to Phase 10"
  - "16-byte header layout: type(1) + flags(1) + length(4) + pts(8) + reserved(2) — all big-endian per cross-platform convention"

patterns-established:
  - "AirShow protocol: HELLO JSON -> HELLO_ACK JSON -> 16-byte binary frames (type/flags/length/pts)"
  - "parseFrameHeader() is static public for test access without network setup"

requirements-completed: [RECV-01, RECV-03]

# Metrics
duration: 82min
completed: 2026-03-31
---

# Phase 09 Plan 01: AirShow Protocol Handler Summary

**AirShowHandler: TCP receiver on port 7400 with newline-terminated JSON handshake (HELLO/HELLO_ACK with codec/resolution/bitrate/fps negotiation) and 16-byte binary frame parsing pushing VIDEO_NAL buffers to GStreamer appsrc**

## Performance

- **Duration:** 82 min
- **Started:** 2026-03-31T14:36:19Z
- **Completed:** 2026-03-31T15:58:51Z
- **Tasks:** 2 (TDD: 1 RED + 1 GREEN)
- **Files modified:** 4

## Accomplishments

- TCP server listening on port 7400 accepting companion Flutter sender connections
- Newline-terminated JSON handshake: HELLO/HELLO_ACK with codec, resolution, bitrate, fps negotiation
- 16-byte binary frame parsing (big-endian): type(1B) + flags(1B) + length(4B uint32) + pts(8B int64) + reserved(2B)
- VIDEO_NAL payloads pushed to MediaPipeline::videoAppsrc() via gst_app_src_push_buffer
- All 3 unit/integration tests pass: ConformsToInterface, ParseFrameHeader, HandshakeJsonRoundTrip

## Task Commits

Each task was committed atomically:

1. **Task 1: Create test scaffold and AirShowHandler header (TDD RED)** - `5a6c05b` (test)
2. **Task 2: Implement AirShowHandler.cpp — TCP server, JSON handshake, binary framing, appsrc injection (TDD GREEN)** - `573ebb1` (feat)

_Note: TDD tasks use two commits (test → feat). RED phase stub compiled but all 3 tests failed. GREEN phase: all 3 tests pass._

## Files Created/Modified

- `src/protocol/AirShowHandler.h` - Full class declaration: State enum, FrameHeader struct, constants (kAirShowPort=7400, kFrameHeaderSize=16), static parseFrameHeader(), ProtocolHandler interface
- `src/protocol/AirShowHandler.cpp` - Implementation: TCP server start/stop, JSON handshake, binary frame streaming loop, GStreamer appsrc injection, ConnectionBridge updates
- `tests/test_airshow.cpp` - 3 GTest tests with AirShowTestEnvironment (QCoreApplication + gst_init), mirrors test_miracast.cpp pattern
- `tests/CMakeLists.txt` - Added test_airshow executable target with correct source list and link libraries

## Decisions Made

- **Newline-terminated JSON for handshake:** Simplest for Flutter TCP client, matches Open Question 3 from RESEARCH.md. Avoids the complexity of length-prefix framing in the handshake phase.
- **Echo-back quality negotiation:** HELLO_ACK echoes sender's requested codec/resolution/bitrate/fps unchanged. Quality arbitration deferred to Phase 10 (Android sender) when real negotiation scenarios are known.
- **16-byte header layout:** type(1) + flags(1) + length(4) + pts(8) + reserved(2), all big-endian. Consistent with network byte order convention and cross-platform safe.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed format specifier warning for qsizetype**
- **Found during:** Task 2 (compile)
- **Issue:** `%d` format specifier used for `qsizetype` (which is `long long` on 64-bit Linux), causing a compiler warning
- **Fix:** Changed to `%lld` with `static_cast<long long>()`
- **Files modified:** src/protocol/AirShowHandler.cpp
- **Verification:** Rebuild produced no warnings
- **Committed in:** 573ebb1 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (Rule 1 - format specifier warning)
**Impact on plan:** Trivial fix. No scope change.

## Issues Encountered

None — plan executed exactly as specified. Both TDD phases (RED then GREEN) worked as expected.

## Known Stubs

None — all protocol functionality is fully wired. Audio frame type (0x02) logs "not yet implemented" intentionally per plan (audio streaming deferred to Phase 10). This is not a stub blocking the plan's goal (RECV-01 video streaming is complete).

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- AirShowHandler is fully functional on the receiver side: listens on port 7400, completes JSON handshake, parses binary frames, injects VIDEO_NAL into GStreamer appsrc
- Phase 10 (Android sender) can connect to port 7400, send HELLO JSON, and receive HELLO_ACK, then stream 16-byte framed H.264 NALs
- Port 7400 conflict concern from STATE.md (SIP systems): no conflict observed on this network; remains a deployment note for users
- Audio streaming (kTypeAudio = 0x02) is handled without crashing but does nothing — Phase 10 will wire audio appsrc

---
*Phase: 09-receiver-protocol-foundation*
*Completed: 2026-03-31*
