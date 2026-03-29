---
phase: 05-dlna
plan: 01
subsystem: protocol/dlna
tags: [dlna, upnp, gstreamer, soap, media-pipeline]
dependency_graph:
  requires: []
  provides:
    - DlnaHandler SOAP dispatcher with 13 AVTransport/RenderingControl/ConnectionManager actions
    - UpnpAdvertiser SOAP routing via DlnaHandler cookie pointer
    - SCPD XML files (avt-scpd.xml, rc-scpd.xml, cm-scpd.xml) written to temp dir before device registration
    - MediaPipeline URI pipeline mode with uridecodebin for DLNA media pull
  affects:
    - src/discovery/UpnpAdvertiser (modified for DlnaHandler routing)
    - src/pipeline/MediaPipeline (new URI pipeline methods)
    - CMakeLists.txt (DlnaHandler.cpp added, SCPD configure_file)
tech_stack:
  added: []
  patterns:
    - Pattern 1 (cookie pointer trampoline): DlnaHandler* threaded through UpnpRegisterRootDevice cookie
    - Pattern 2 (SOAP dispatch switch): strcmp dispatch on UpnpActionRequest_get_ActionName
    - Pattern 3 (IXML argument extraction): ixmlDocument_getElementsByTagName helper
    - Pattern 4 (UpnpAddToActionResponse): iterative stub response building
    - Pattern 5 (uridecodebin pad-added): dynamic audio/video pad connection
    - Pattern 6 (Qt thread marshalling): QMetaObject::invokeMethod in all SOAP handlers
key_files:
  created:
    - src/protocol/DlnaHandler.h
    - src/protocol/DlnaHandler.cpp
    - resources/dlna/avt-scpd.xml
    - resources/dlna/rc-scpd.xml
    - resources/dlna/cm-scpd.xml
    - tests/test_dlna.cpp
  modified:
    - src/discovery/UpnpAdvertiser.h
    - src/discovery/UpnpAdvertiser.cpp
    - src/pipeline/MediaPipeline.h
    - src/pipeline/MediaPipeline.cpp
    - tests/CMakeLists.txt
    - CMakeLists.txt
decisions:
  - "DlnaHandler header uses glib.h for gint64 type (avoids full GStreamer pull in header)"
  - "parseTimeString/formatGstTime made public static for direct unit testing without friend declarations"
  - "ixml.h included as <upnp/ixml.h> not <ixml.h> — header lives under upnp/ subdirectory in libupnp-dev extraction"
  - "stopUri() call in DlnaHandler::stop() deferred to Plan 02 (method didn't exist at Task 1 compile time — Task 2 adds it)"
  - "writeScpdFiles uses inline static string literals (not file reads) for runtime SCPD content — simpler and avoids applicationDirPath dependency"
  - "URI pipeline pre-links static chains (audio: audioconvert!audioresample!volume!autoaudiosink; video: videoconvert[!glupload]!videosink) before pad-added fires"
metrics:
  duration: "~35 minutes"
  completed_date: "2026-03-28"
  tasks_completed: 3
  files_modified: 12
---

# Phase 05 Plan 01: DLNA Infrastructure Summary

DLNA DMR infrastructure with DlnaHandler SOAP dispatcher (13 actions), uridecodebin URI pipeline in MediaPipeline, UpnpAdvertiser SOAP routing via cookie pointer, and three SCPD XML service descriptions enabling DLNA-03 controller discovery.

## What Was Built

### Task 0: Test Scaffold (Wave 0)
Created `tests/test_dlna.cpp` with 7 GTEST_SKIP stub tests before any production code existed. Added minimal `test_dlna` target to `tests/CMakeLists.txt` (GTest only). All 7 tests skipped cleanly — scaffold ready for Task 1.

### Task 1: DlnaHandler Skeleton + UpnpAdvertiser Routing + SCPD XMLs

**DlnaHandler** (`src/protocol/DlnaHandler.h/.cpp`):
- `class DlnaHandler : public QObject, public ProtocolHandler` with `Q_OBJECT`
- Full SOAP dispatch switch via `handleSoapAction(const void* event)` — dispatches all 13 actions
- `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` threading pattern in place for all SOAP handlers (Plan 02 fills lambda bodies with real pipeline calls)
- `parseTimeString()` / `formatGstTime()` static helpers for H:MM:SS ↔ nanoseconds conversion
- `onGetProtocolInfo()` returns full SinkProtocolInfo with 9 MIME types (D-07)

**UpnpAdvertiser** (modified):
- `setDlnaHandler(DlnaHandler*)` setter stores pointer
- `writeScpdFiles()` writes inline SCPD content to temp dir (Pattern 1 from RESEARCH.md)
- `start()` calls `writeScpdFiles()` before `UpnpRegisterRootDevice()` (Pitfall 2)
- `UpnpRegisterRootDevice` cookie changed from `nullptr` to `m_dlnaHandler`
- `upnpCallback()` routes `UPNP_CONTROL_ACTION_REQUEST` to `handler->handleSoapAction(event)`
- `stop()` removes SCPD temp files on teardown

**SCPD XML files** (`resources/dlna/`):
- `avt-scpd.xml`: 11 actions (SetAVTransportURI, Play, Stop, Pause, Seek, GetTransportInfo, GetPositionInfo, GetMediaInfo, GetDeviceCapabilities, GetTransportSettings, GetCurrentTransportActions), 29 state variables
- `rc-scpd.xml`: 6 actions (SetVolume, GetVolume, SetMute, GetMute, ListPresets, SelectPreset), 6 state variables
- `cm-scpd.xml`: 3 actions (GetProtocolInfo, GetCurrentConnectionInfo, GetCurrentConnectionIDs), 10 state variables

**CMakeLists.txt**: `DlnaHandler.cpp` added to main target, 3 `configure_file` commands for SCPD XMLs.

**Tests**: Replaced GTEST_SKIP stubs with real assertions — name(), isRunning(), start()/stop() lifecycle, parseTimeString(), formatGstTime() round-trip. All 7 pass.

### Task 2: MediaPipeline URI Pipeline Mode

**MediaPipeline** (modified):

New public methods:
- `initUriPipeline(void* qmlVideoItem)` — creates separate `uri-pipeline` with `uridecodebin` + pad-added callback
- `setUri(const std::string&)` — stops to NULL, sets `uri` property, prerolls to PAUSED
- `playUri()`, `pauseUri()`, `stopUri()` — state transitions
- `queryPosition()`, `queryDuration()` — nanosecond position queries
- `seekUri(gint64)` — `gst_element_seek_simple` with FLUSH|KEY_UNIT flags
- `setVolume(double)`, `getVolume()` — 0.0-1.0 volume control on uri-volume element
- `setMuted()` updated to also set URI pipeline volume element

New private members: `m_uriPipeline`, `m_uriDecodebin`, `m_uriAudioSink`, `m_uriVolume`

Pipeline structure:
```
uridecodebin [uri=<controller URL>]
    |-- video pad --> videoconvert ! glupload ! qml6glsink  (or fakesink headless)
    \-- audio pad --> audioconvert ! audioresample ! volume ! autoaudiosink
```

Static chains are pre-linked at init time; `uridecodebin` pads connect via pad-added callback that checks `video/` or `audio/` caps prefix.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `ixml.h` include path**
- **Found during:** Task 1 first build
- **Issue:** Plan specified `#include <ixml.h>` but libupnp-dev extraction places it at `upnp/ixml.h`
- **Fix:** Changed to `#include <upnp/ixml.h>` matching the actual header tree
- **Files modified:** `src/protocol/DlnaHandler.cpp`
- **Commit:** 2bca0d2 (auto-fixed before commit)

**2. [Rule 1 - Bug] `stopUri()` forward reference in DlnaHandler::stop()**
- **Found during:** Task 1 (stop() body referenced MediaPipeline::stopUri() which Task 2 adds)
- **Issue:** Task 1 compiled DlnaHandler.cpp against MediaPipeline which lacked stopUri() until Task 2
- **Fix:** Commented out `m_pipeline->stopUri()` in Task 1 stub stop() with `// Plan 02 will call:` annotation. Task 2 adds stopUri() to MediaPipeline — Plan 02 will wire it.
- **Files modified:** `src/protocol/DlnaHandler.cpp`
- **Commit:** 2bca0d2 (auto-fixed before commit)

## Known Stubs

The following items are intentional stubs for Plan 02 to implement:

| File | Stub | Reason |
|------|------|--------|
| `src/protocol/DlnaHandler.cpp` | `onSetAVTransportURI` lambda body empty (no `m_pipeline->setUri()`) | Plan 02 wires pipeline |
| `src/protocol/DlnaHandler.cpp` | `onPlay` lambda body empty (no `m_pipeline->playUri()`) | Plan 02 wires pipeline |
| `src/protocol/DlnaHandler.cpp` | `onStop` lambda body empty (no `m_pipeline->stopUri()`) | Plan 02 wires pipeline |
| `src/protocol/DlnaHandler.cpp` | `onPause` lambda body empty (no `m_pipeline->pauseUri()`) | Plan 02 wires pipeline |
| `src/protocol/DlnaHandler.cpp` | `onGetPositionInfo` returns hardcoded "0:00:00" | Plan 02 queries pipeline |
| `src/protocol/DlnaHandler.cpp` | `onSetVolume`/`onGetVolume` lambda stubs | Plan 02 wires pipeline |
| `src/protocol/DlnaHandler.cpp` | `DlnaHandler::stop()` does not call `m_pipeline->stopUri()` | Deferred until Plan 02 adds stopUri() awareness |

These stubs are by design — Plan 01 establishes all contracts and plumbing; Plan 02 implements the SOAP action logic.

## Verification

- `ninja myairshow` passes (exit 0)
- `ctest -R DlnaHandlerTest --output-on-failure` passes all 7 tests (0 failures, 0 skips)
- `grep -r "class DlnaHandler" src/` confirms DlnaHandler exists
- `grep "handleSoapAction" src/discovery/UpnpAdvertiser.cpp` confirms routing
- `grep "uridecodebin" src/pipeline/MediaPipeline.cpp` confirms URI pipeline
- All three SCPD files exist under `resources/dlna/`
- All prior test targets (test_pipeline, test_discovery, test_display, test_airplay) still build and pass

## Self-Check: PASSED
