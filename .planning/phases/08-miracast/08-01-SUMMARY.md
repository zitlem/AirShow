---
phase: 08-miracast
plan: 01
subsystem: miracast
tags: [miracast, ms-mice, gstreamer, mdns, mpeg-ts]
dependency_graph:
  requires:
    - src/protocol/ProtocolHandler.h
    - src/pipeline/MediaPipeline.h
    - src/discovery/DiscoveryManager.cpp
    - src/ui/ConnectionBridge.h
    - src/security/SecurityManager.h
  provides:
    - MiracastHandler (QObject+ProtocolHandler, TCP 7250 listener, binary TLV parser, RTSP stubs)
    - MediaPipeline::initMiracastPipeline() (MPEG-TS/RTP receive pipeline with tsdemux dynamic pads)
    - _display._tcp mDNS advertisement (DiscoveryManager)
    - test_miracast (4 unit tests covering interface, parsing, pipeline, RTSP response)
  affects:
    - src/pipeline/MediaPipeline.h (new public methods + private member)
    - src/discovery/DiscoveryManager.cpp (new _display._tcp advertisement)
    - CMakeLists.txt (MiracastHandler.cpp added to myairshow target)
    - tests/CMakeLists.txt (test_miracast target added)
tech_stack:
  added: []
  patterns:
    - QObject+ProtocolHandler dual inheritance (same as CastHandler)
    - Static public methods for unit testability (same as DlnaHandler::parseTimeString)
    - MS-MICE binary TLV parser with big-endian uint16 fields and UTF-16LE FriendlyName
    - GStreamer tsdemux dynamic pad-added callback (same as initUriPipeline Phase 5)
    - vaapidecodebin -> avdec_h264 software fallback (same as AirPlay Phase 4)
    - Separate m_miracastPipeline member (same pattern as m_uriPipeline, m_webrtcPipeline)
key_files:
  created:
    - src/protocol/MiracastHandler.h
    - src/protocol/MiracastHandler.cpp
    - tests/test_miracast.cpp
  modified:
    - src/pipeline/MediaPipeline.h
    - src/pipeline/MediaPipeline.cpp
    - src/discovery/DiscoveryManager.cpp
    - CMakeLists.txt
    - tests/CMakeLists.txt
decisions:
  - MiracastHandler binary TLV parser exposed as public static parseSourceReady() for testability without friend declarations
  - buildRtspResponse() also public static — same pattern as CastSession::buildSdpFromOffer (Phase 6)
  - _display._tcp advertisement uses VerMgmt=0x0202 and VerMin=0x0100 from MS-MICE spec revision 6.0
  - vaapidecodebin used as primary hardware decoder in initMiracastPipeline(); avdec_h264 software fallback (dev machine lacks VAAPI)
  - m_miracastPipeline is a separate pipeline (not m_pipeline) consistent with m_uriPipeline and m_webrtcPipeline patterns
metrics:
  duration: "~15 minutes"
  completed: "2026-03-30"
  tasks_completed: 2
  files_changed: 8
---

# Phase 8 Plan 1: Miracast Skeleton Summary

MiracastHandler TCP listener (MS-MICE binary TLV, port 7250), MPEG-TS/RTP GStreamer pipeline (udpsrc+tsdemux dynamic pads), and _display._tcp mDNS advertisement for Windows "Connect" discovery.

## Tasks Completed

| Task | Description | Commit | Status |
|------|-------------|--------|--------|
| 1 | MiracastHandler skeleton + initMiracastPipeline() + _display._tcp | e16c622 | Done |
| 2 | test_miracast scaffold (4 tests) | f62e06c | Done |

## Key Artifacts

### src/protocol/MiracastHandler.h / .cpp

- `class MiracastHandler : public QObject, public ProtocolHandler` — dual inheritance per CastHandler pattern
- `enum class State { Idle, WaitingSourceReady, ConnectingToSource, NegotiatingM1 ... TearingDown }` — 12-state MS-MICE+WFD machine
- TCP listener on `kMicePort = 7250` for MS-MICE SOURCE_READY binary messages
- `parseSourceReady()` — static public method parsing binary TLV: FriendlyName (UTF-16LE), RTSPPort (big-endian uint16), SourceID (ASCII)
- `buildRtspResponse()` — static public method building RTSP/1.0 responses with CSeq + Content-Length
- RTSP methods (`sendRtspRequest`, `onRtspConnected`, `onRtspData`) are stubs that log "not yet implemented (Plan 02)"
- Security integration via `setSecurityManager()` consistent with CastHandler

### src/pipeline/MediaPipeline.cpp: initMiracastPipeline()

- New `m_miracastPipeline` member (separate from m_pipeline, m_uriPipeline, m_webrtcPipeline)
- Pipeline: `udpsrc(port=N, caps=MP2T/90kHz) ! rtpmp2tdepay ! tsparse ! tsdemux`
- Video branch (pad-added): `queue ! h264parse ! vaapidecodebin/avdec_h264 ! videoconvert [! glupload] ! qml6glsink/fakesink`
- Audio branch (pad-added): `queue ! aacparse ! avdec_aac ! audioconvert ! audioresample ! autoaudiosink`
- `MiracastPadHelper` struct for the tsdemux pad-added callback (same named-struct pattern as PadAddedHelper in Phase 1/5)
- Starts in `GST_STATE_PAUSED`; Plan 02 PLAY message transitions to PLAYING
- `stopMiracast()` sets to NULL state and unrefs; `stop()` updated to clean up m_miracastPipeline

### src/discovery/DiscoveryManager.cpp

- Added `_display._tcp` advertisement on port 7250 with TXT records `VerMgmt=0x0202` and `VerMin=0x0100`
- Per RESEARCH.md Pitfall 1: without this Windows "Connect" shows no devices in wireless display list

### tests/test_miracast.cpp

- **Test 1 ConformsToInterface**: name()/"miracast", isRunning() before/after start()/stop()
- **Test 2 ParseMiceSourceReady**: constructs valid SOURCE_READY binary message, verifies FriendlyName="TestPC", RTSPPort=7236, SourceID="test-source-id-00"
- **Test 3 InitMiracastPipelineCreatesElements**: verifies GST_STATE_PAUSED pipeline, stopMiracast() nulls the pipeline pointer
- **Test 4 BuildRtspResponse**: verifies RTSP/1.0 format, CSeq header, Content-Length with body, body placement after \r\n\r\n

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| parseSourceReady() public static | Testable without friend declaration — same pattern as DlnaHandler::parseTimeString (Phase 5) |
| buildRtspResponse() public static | Same justification; Plan 02 fills in the full RTSP state machine |
| _display._tcp TXT uses MS-MICE spec values | VerMgmt/VerMin from [MS-MICE] rev 6.0 are required for Windows to recognize the receiver |
| vaapidecodebin primary + avdec_h264 fallback | Consistent with AirPlay Phase 4 D-12; dev machine lacks VAAPI so fallback activates |
| Separate m_miracastPipeline | Consistent with m_uriPipeline (Phase 5) and m_webrtcPipeline (Phase 6) isolation pattern |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed -Wformat warning for qsizetype in parseMiceMessage()**
- **Found during:** Task 1 compilation
- **Issue:** `qWarning` format string used `%d` for `data.size()` which is `qsizetype` (long long) — produces -Wformat warning
- **Fix:** Changed to `%lld` with `static_cast<long long>(data.size())`
- **Files modified:** `src/protocol/MiracastHandler.cpp`
- **Commit:** e16c622 (included in Task 1 commit after fix)

None beyond the format warning fix.

## Known Stubs

| Stub | File | Reason |
|------|------|--------|
| `sendRtspRequest()` | MiracastHandler.cpp | RTSP client to source:7236 — intentional stub, implemented in Plan 02 |
| `onRtspConnected/Data/Disconnected()` | MiracastHandler.cpp | RTSP state machine — intentional stub, implemented in Plan 02 |
| `parseMiceMessage()` stops at ConnectingToSource state | MiracastHandler.cpp | Does not initiate TCP connection to source — intentional, Plan 02 wires this up |

These stubs are intentional — Plan 02 (MS-MICE RTSP M1-M7 negotiation) fills them in.

## Verification Results

```
cmake --build build --target myairshow  -> success (no errors)
cmake --build build --target test_miracast -> success
./build/tests/test_miracast -> 4/4 PASSED
```

## Self-Check: PASSED
