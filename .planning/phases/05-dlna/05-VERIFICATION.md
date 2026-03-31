---
phase: 05-dlna
verified: 2026-03-29T01:38:43Z
status: human_needed
score: 9/9 automated must-haves verified
re_verification: false
human_verification:
  - test: "Launch AirShow, then open BubbleUPnP (Android), VLC (desktop), or foobar2000 with UPnP plugin. Look for 'AirShow' in the renderer/player list."
    expected: "AirShow appears as a DLNA Media Renderer (DLNA-03)"
    why_human: "SSDP/mDNS discovery and UPnP device description serving requires a live running process and a real network — not testable with static analysis"
  - test: "Select AirShow as the renderer in a DLNA controller app, then push an MP4 or MKV video file."
    expected: "Video plays on the AirShow window with audio through speakers; HUD shows 'DLNA Controller' and 'DLNA' protocol (DLNA-01)"
    why_human: "End-to-end DLNA push requires a live GStreamer uridecodebin pipeline receiving an HTTP URI from a real controller — not testable without a running app"
  - test: "Select AirShow as the renderer, then push an MP3 or FLAC audio file."
    expected: "Audio plays through the receiver's speakers without video (DLNA-02)"
    why_human: "Audio-only DLNA push requires runtime codec negotiation via uridecodebin and real audio output — not verifiable statically"
  - test: "During active DLNA video playback, test Pause, Resume (Play), Seek (REL_TIME), and Stop from the controller app."
    expected: "Each transport action reflects correctly: pause freezes frame, seek jumps position, stop returns to idle screen and clears HUD"
    why_human: "Transport control flow (SOAP → GStreamer state transition → UI update) can only be observed end-to-end with a running process"
  - test: "While DLNA video is playing, push a second video file from the controller."
    expected: "First playback stops and second begins (single-session model)"
    why_human: "SetAVTransportURI single-session replacement requires runtime pipeline teardown and re-init — not testable without live execution"
---

# Phase 5: DLNA Verification Report

**Phase Goal:** Users with DLNA controller apps can push video and audio files to AirShow for playback
**Verified:** 2026-03-29T01:38:43Z
**Status:** human_needed — all automated checks passed; 5 items require real-device verification
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

Success criteria from ROADMAP.md Phase 5:

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A DLNA controller app can see AirShow listed as a Media Renderer | ? HUMAN | UpnpAdvertiser wired, SCPD XMLs written, device XML present — live network test required |
| 2 | Pushing a video file from the controller causes it to play on the receiver with video and audio | ? HUMAN | Full pipeline chain implemented and wired; runtime execution required |
| 3 | Pushing an audio file from the controller causes it to play through the receiver's speakers | ? HUMAN | Audio branch in uridecodebin pipeline implemented; runtime execution required |

Additional must-have truths from PLAN frontmatter (Plans 01-03):

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 4 | DlnaHandler.name() returns 'dlna' and start() returns true | ✓ VERIFIED | DlnaHandlerTest.NameReturnsDlna and StartReturnsTrueAndIsRunning pass (9/9 tests) |
| 5 | DlnaHandler.stop() transitions isRunning() from true to false | ✓ VERIFIED | DlnaHandlerTest.StopMakesNotRunning passes |
| 6 | parseTimeString round-trips correctly through formatGstTime | ✓ VERIFIED | DlnaHandlerTest.ParseTimeStringBasic, ParseTimeStringZero, FormatGstTimeRoundTrip all pass |
| 7 | SCPD XML files exist and contain required UPnP action declarations | ✓ VERIFIED | resources/dlna/avt-scpd.xml contains SetAVTransportURI; rc-scpd.xml contains SetVolume; cm-scpd.xml contains GetProtocolInfo |
| 8 | DlnaHandler is registered with ProtocolManager and starts on application launch | ✓ VERIFIED | main.cpp lines 83-89: make_unique<DlnaHandler>, setDlnaHandler, addHandler; ordering confirmed |
| 9 | UpnpAdvertiser receives DlnaHandler pointer before start() so SOAP actions route correctly | ✓ VERIFIED | setDlnaHandler() called at line 87; upnpAdvertiser.start() at line 113 — correct ordering |

**Automated Score:** 6/6 programmatically verifiable truths confirmed
**Pending Human:** 3/3 ROADMAP success criteria require live device testing

### Required Artifacts

| Artifact | Status | Details |
|----------|--------|---------|
| `src/protocol/DlnaHandler.h` | ✓ VERIFIED | `class DlnaHandler : public QObject, public ProtocolHandler` with Q_OBJECT, handleSoapAction, parseTimeString, formatGstTime public static |
| `src/protocol/DlnaHandler.cpp` | ✓ VERIFIED | Full SOAP dispatch (19 actions), all 13 on* methods implemented with real pipeline/bridge calls, 8 QMetaObject::invokeMethod(Qt::QueuedConnection) calls |
| `src/pipeline/MediaPipeline.h` | ✓ VERIFIED | initUriPipeline, setUri, playUri, pauseUri, stopUri, queryPosition, queryDuration, seekUri, setVolume, getVolume all declared; m_uriPipeline, m_uriDecodebin, m_uriAudioSink, m_uriVolume private members present |
| `src/pipeline/MediaPipeline.cpp` | ✓ VERIFIED | gst_element_factory_make("uridecodebin"), pad-added callback, GST_STATE_NULL in setUri, gst_element_seek_simple, setVolume/getVolume implemented |
| `resources/dlna/avt-scpd.xml` | ✓ VERIFIED | SetAVTransportURI confirmed present |
| `resources/dlna/rc-scpd.xml` | ✓ VERIFIED | SetVolume confirmed present |
| `resources/dlna/cm-scpd.xml` | ✓ VERIFIED | GetProtocolInfo confirmed present |
| `src/main.cpp` | ✓ VERIFIED | `#include "protocol/DlnaHandler.h"`, make_unique<airshow::DlnaHandler>, setDlnaHandler, addHandler present; ordering: setDlnaHandler (line 87) before start() (line 113) confirmed |
| `tests/test_dlna.cpp` | ✓ VERIFIED | 9 real tests (no GTEST_SKIP stubs): name, not-running-initially, start-returns-true, stop-makes-not-running, parse-time-basic, parse-time-zero, format-gst-time-round-trip, set-pipeline-nocrash, start-stop-with-pipeline |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/discovery/UpnpAdvertiser.cpp` | `src/protocol/DlnaHandler.h` | `static_cast<airshow::DlnaHandler*>(cookie)` in upnpCallback | ✓ WIRED | Line 249: `auto* handler = static_cast<airshow::DlnaHandler*>(cookie); return handler->handleSoapAction(event);` |
| `src/pipeline/MediaPipeline.cpp` | uridecodebin GStreamer element | `gst_element_factory_make("uridecodebin", "urisrc")` | ✓ WIRED | Line 445 confirmed |
| `src/protocol/DlnaHandler.cpp` | `src/pipeline/MediaPipeline.h` | `m_pipeline->setUri()`, `playUri()`, `stopUri()`, `pauseUri()`, `seekUri()`, `queryPosition()`, `queryDuration()` | ✓ WIRED | All 7 pipeline call sites confirmed at lines 281, 296, 319, 342, 376, 406, 407 |
| `src/protocol/DlnaHandler.cpp` | `src/ui/ConnectionBridge.h` | `m_connectionBridge->setConnected(true, "DLNA Controller", "DLNA")` in onPlay; `setConnected(false)` in onStop and stop() | ✓ WIRED | Lines 90, 299, 322 confirmed |
| `src/protocol/DlnaHandler.cpp` | `src/pipeline/MediaPipeline.h` | `m_pipeline->setVolume()` and `m_pipeline->setMuted()` | ✓ WIRED | Lines 502, 535 confirmed |
| `src/main.cpp` | `src/protocol/DlnaHandler.h` | `make_unique<airshow::DlnaHandler>` + `protocolManager.addHandler` | ✓ WIRED | Lines 83, 89 confirmed |
| `src/main.cpp` | `src/discovery/UpnpAdvertiser.h` | `upnpAdvertiser.setDlnaHandler(dlnaRawPtr)` | ✓ WIRED | Line 87 confirmed; before start() on line 113 |

### Data-Flow Trace (Level 4)

The DLNA pipeline is pull-based (uridecodebin fetches the URI provided by the controller), not push-based. The data source is an HTTP URI provided at runtime by the DLNA controller via SetAVTransportURI SOAP action. Static data-flow trace is not applicable here — the pipeline is correctly constructed and wired; whether real data flows depends on a live controller and network, which is a human verification item.

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `DlnaHandler::onSetAVTransportURI` | `m_currentUri` | SOAP arg `CurrentURI` extracted via `getArgValue()` | Yes — stored under mutex, passed to `m_pipeline->setUri()` | ✓ FLOWING |
| `MediaPipeline::setUri` | `m_uriDecodebin` uri property | `g_object_set(m_uriDecodebin, "uri", uri.c_str(), nullptr)` | Yes — passed to GStreamer uridecodebin element | ✓ FLOWING |
| `DlnaHandler::onGetPositionInfo` | `pos`, `dur` | `m_pipeline->queryPosition()`, `m_pipeline->queryDuration()` | Yes — real GStreamer queries; returns `NOT_IMPLEMENTED` correctly when pipeline not loaded (-1) | ✓ FLOWING |
| `DlnaHandler::onGetVolume` | `vol` | `m_pipeline->getVolume()` | Yes — queries live GStreamer volume element | ✓ FLOWING |

### Behavioral Spot-Checks

The application requires a running Qt event loop and GStreamer pipeline to exercise DLNA flows. The test binary (test_dlna) provides unit-level coverage; end-to-end spot-checks are deferred to human verification.

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| test_dlna builds and all 9 tests pass | `ctest --test-dir build/linux-debug -R DlnaHandlerTest` | 9/9 passed, 0 failed | ✓ PASS |
| airshow binary exists (clean build) | `ls build/linux-debug/AirShow` | Binary present, ninja reported no work to do | ✓ PASS |
| SCPD XMLs contain required action names | grep for SetAVTransportURI, SetVolume, GetProtocolInfo | All 3 found | ✓ PASS |
| Full SOAP pipeline (DLNA controller → receiver) | Requires live app + controller | Not runnable statically | ? SKIP (human) |

### Requirements Coverage

All requirement IDs declared across Plans 01-03 are accounted for:

| Requirement | Source Plan(s) | Description | Status | Evidence |
|-------------|---------------|-------------|--------|----------|
| DLNA-01 | 05-02, 05-03 | User can push video files from a DLNA controller to AirShow for playback | ? HUMAN | Full implementation present (SetAVTransportURI → setUri, Play → playUri, uridecodebin pipeline with video branch); live controller test needed |
| DLNA-02 | 05-02, 05-03 | User can push audio files from a DLNA controller to AirShow for playback | ? HUMAN | SinkProtocolInfo lists 14 MIME types (audio/mpeg, audio/mp4, audio/flac, etc.); uridecodebin audio branch wired; live controller test needed |
| DLNA-03 | 05-01, 05-03 | AirShow appears as a DLNA Media Renderer (DMR) in DLNA controller apps | ? HUMAN | UpnpAdvertiser with MediaRenderer.xml, SCPD XMLs, and DlnaHandler SOAP routing all wired; SSDP discovery requires live network test |

**No orphaned requirements found.** REQUIREMENTS.md maps DLNA-01, DLNA-02, DLNA-03 to Phase 5. All three are claimed by plan frontmatter and have implementation evidence. All three require human verification for final confirmation.

### Anti-Patterns Found

No blockers or warnings found.

Scan covered: `src/protocol/DlnaHandler.h`, `src/protocol/DlnaHandler.cpp`, `src/pipeline/MediaPipeline.cpp`, `src/discovery/UpnpAdvertiser.cpp`, `src/main.cpp`, `tests/test_dlna.cpp`

- No TODO/FIXME/HACK/PLACEHOLDER comments in DLNA-related files
- No empty lambda bodies (all 8 QMetaObject::invokeMethod lambdas contain real pipeline or bridge calls)
- No `return null` or `return {}` stubs in SOAP action handlers
- No hardcoded empty data flowing to user-visible output
- The "Plan 02 will:" annotations noted in Plan 01 SUMMARY were confirmed resolved — DlnaHandler.cpp has no such comments remaining

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | — | — | None found |

### Human Verification Required

#### 1. DLNA Controller Discovery (DLNA-03)

**Test:** Launch AirShow, then open BubbleUPnP (Android), VLC (View > Playlist > UPnP), or foobar2000 with UPnP plugin on the same network.
**Expected:** "AirShow" (or the configured receiver name) appears in the renderer list as a Media Renderer.
**Why human:** SSDP advertisement, mDNS discovery, and UPnP device description serving require a live running process and a real network. The SCPD XMLs are verified present and the wiring is confirmed — only the runtime serving can confirm DLNA-03.

#### 2. Video Push Playback (DLNA-01)

**Test:** Select AirShow as the renderer, then push an MP4 or MKV video file from the controller. Observe the receiver window and speakers.
**Expected:** Video appears on the receiver window with audio through speakers. HUD overlay shows "DLNA Controller" and "DLNA" protocol label.
**Why human:** The GStreamer uridecodebin pipeline is fully wired but can only be exercised with a real HTTP URI from a controller app. Visual output and audio output cannot be verified statically.

#### 3. Audio Push Playback (DLNA-02)

**Test:** Select AirShow as the renderer, then push an MP3 or FLAC audio file.
**Expected:** Audio plays through the receiver's speakers with no video output (audio-only session).
**Why human:** Audio-only DLNA push requires uridecodebin to select only the audio branch — this depends on runtime pad negotiation and OS audio output.

#### 4. Transport Controls

**Test:** During active video playback, use Pause, Play/Resume, Seek (REL_TIME), Volume, and Stop from the controller.
**Expected:** Pause freezes playback. Resume continues. Seek jumps to the new position (REL_TIME only — other modes return UPnP error 710). Volume adjusts. Stop clears the HUD and returns to idle screen.
**Why human:** Transport state transitions (SOAP → QMetaObject::invokeMethod → GStreamer state change → Qt UI update) require a live Qt event loop to complete.

#### 5. Single-Session Model

**Test:** While video is playing, push a second video file from the controller without stopping the first.
**Expected:** First playback stops automatically (GST_STATE_NULL), second begins (uridecodebin reconfigured with new URI and set to PAUSED then PLAYING).
**Why human:** Single-session URI replacement depends on GStreamer pipeline teardown and reinitialization behavior at runtime.

### Gaps Summary

No gaps. All programmatically verifiable must-haves are confirmed. The three ROADMAP success criteria (DLNA-01, DLNA-02, DLNA-03) cannot be verified without a running process and a real DLNA controller app on the local network — this is expected for a protocol implementation at this stage. The implementation is complete and all wiring is confirmed.

---

_Verified: 2026-03-29T01:38:43Z_
_Verifier: Claude (gsd-verifier)_
