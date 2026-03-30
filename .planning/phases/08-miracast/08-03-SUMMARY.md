---
phase: 08-miracast
plan: 03
subsystem: protocol
tags: [miracast, ms-mice, gstreamer, mpeg-ts, rtp, wfd]

# Dependency graph
requires:
  - phase: 08-miracast plan 01
    provides: MiracastHandler class with full MS-MICE + WFD RTSP state machine
  - phase: 08-miracast plan 02
    provides: MediaPipeline::initMiracastPipeline + stopMiracast, DiscoveryManager _display._tcp
  - phase: 07-security-hardening
    provides: SecurityManager for connection approval and RFC1918 filtering
  - phase: 06-google-cast
    provides: CastHandler wiring pattern used as blueprint for MiracastHandler wiring

provides:
  - MiracastHandler wired into main.cpp application lifecycle (creation, SecurityManager, qmlVideoItem, ProtocolManager registration)
  - GStreamer plugin checks for rtpmp2tdepay, tsparse, tsdemux (fatal) and vaapidecodebin, aacparse (non-fatal)
  - Integration tests 10-12: plugin availability, handler start/stop lifecycle, pipeline start/stop/restart cycle

affects: [complete — this is the final plan of phase 08-miracast]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Fatal vs non-fatal Miracast plugin checks follow same pattern as Cast plugin checks (Phase 6 decision)"
    - "MiracastHandler wired after CastHandler in main.cpp — consistent ordering with protocol registration"
    - "setQmlVideoItem(nullptr) at wiring time — real pointer set asynchronously by ReceiverWindow after sceneGraphInitialized"

key-files:
  created: []
  modified:
    - src/main.cpp
    - tests/test_miracast.cpp

key-decisions:
  - "Fatal Miracast plugin checks: rtpmp2tdepay, tsparse, tsdemux — all required for MPEG-TS demux pipeline; app exits if missing"
  - "Non-fatal Miracast plugin checks: vaapidecodebin (hardware decode, avdec_h264 fallback), aacparse (audio optional — some streams are LPCM only)"
  - "setQmlVideoItem(nullptr) called in main.cpp at wiring time — consistent with pipeline.setQmlVideoItem(nullptr) deferred pattern"

patterns-established:
  - "Miracast plugin checks use gst_registry_check_feature_version consistent with existing Cast checks in checkRequiredPlugins()"

requirements-completed: [MIRA-01, MIRA-02, MIRA-03]

# Metrics
duration: 2min
completed: 2026-03-30
---

# Phase 8 Plan 3: Miracast Integration (main.cpp Wiring + Tests) Summary

**MiracastHandler wired into main.cpp alongside AirPlay/DLNA/Cast, with fatal/non-fatal GStreamer plugin checks and 12 passing integration tests covering plugin availability, handler lifecycle, and pipeline restart cycle**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-30T19:06:49Z
- **Completed:** 2026-03-30T19:08:50Z
- **Tasks:** 2 auto (+ 1 human-verify checkpoint, auto-approved in auto mode)
- **Files modified:** 2

## Accomplishments

- MiracastHandler created, SecurityManager-wired, qmlVideoItem-set, and registered with ProtocolManager in main.cpp following the exact CastHandler pattern
- GStreamer plugin checks added: rtpmp2tdepay/tsparse/tsdemux as fatal (app exits if missing); vaapidecodebin and aacparse as non-fatal (warnings logged, fallbacks used)
- Integration tests 10-12 added and all 12 test_miracast tests pass: plugin availability verification, TCP server start/stop/restart lifecycle, and MPEG-TS pipeline create/play/stop cycle repeated twice

## Task Commits

Each task was committed atomically:

1. **Task 1: main.cpp wiring + GStreamer plugin checks** - `57076ca` (feat)
2. **Task 2: Integration tests + plugin availability verification** - `5e96718` (test)
3. **Task 3: End-to-end Miracast verification (human-verify)** - auto-approved (auto_advance=true)

## Files Created/Modified

- `src/main.cpp` - Added MiracastHandler include, fatal/non-fatal plugin checks, and full handler wiring block after CastHandler
- `tests/test_miracast.cpp` - Added tests 10 (RequiredGStreamerPluginsAvailable), 11 (MiracastHandlerStartStopLifecycle), 12 (MiracastPipelineStartStopCycle); all 12 tests pass

## Decisions Made

- Fatal Miracast plugin checks cover rtpmp2tdepay, tsparse, tsdemux — these form the required MPEG-TS demux pipeline; no fallback path exists for these
- vaapidecodebin check is non-fatal: avdec_h264 software fallback provides correct behavior (confirmed in test output: "vaapidecodebin not available, falling back to avdec_h264")
- aacparse check is non-fatal: some Miracast streams carry LPCM audio only; missing aacparse still allows those streams to work
- setQmlVideoItem(nullptr) in main.cpp mirrors pipeline.setQmlVideoItem(nullptr) deferred-pointer pattern — real pointer set by ReceiverWindow after QML scene graph initializes

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None — build succeeded on first attempt, all 12 tests passed on first run.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 08-miracast is now complete. All three plans executed:
- Plan 01: MiracastHandler core implementation (MS-MICE + WFD RTSP state machine)
- Plan 02: MediaPipeline Miracast pipeline, DiscoveryManager _display._tcp advertisement
- Plan 03: main.cpp wiring, plugin checks, integration tests (this plan)

The Miracast implementation is ready for human end-to-end verification with a Windows 10/11 device. The checkpoint:human-verify task (Task 3) was auto-approved in auto mode.

---
*Phase: 08-miracast*
*Completed: 2026-03-30*

## Self-Check: PASSED

- `src/main.cpp`: FOUND (modified with MiracastHandler wiring and plugin checks)
- `tests/test_miracast.cpp`: FOUND (modified with tests 10-12)
- Commit `57076ca`: FOUND
- Commit `5e96718`: FOUND
- All 12 tests: PASSED (verified by test run output showing `[  PASSED  ] 12 tests`)
