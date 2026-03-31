---
phase: 04-airplay
verified: 2026-03-28T23:55:00Z
status: human_needed
score: 4/4 must-have artifact truths verified; 4/4 success criteria require human
re_verification: false
human_verification:
  - test: "On iPhone or iPad, open Control Center > Screen Mirroring and look for 'AirShow'. Tap to connect."
    expected: "Mirrored screen appears in the receiver window within 3 seconds (AIRP-01)"
    why_human: "Requires a physical iOS/macOS device on the same LAN; cannot simulate AirPlay protocol handshake programmatically"
  - test: "On a Mac, open Control Center > Screen Mirroring and select 'AirShow'."
    expected: "Mac desktop mirrors to receiver window (AIRP-02)"
    why_human: "Requires macOS hardware and real RAOP negotiation over the network"
  - test: "Start a screen mirror from iPhone/iPad with audio playing. Observe audio and video on the receiver."
    expected: "Audio plays through receiver speakers in sync with video; no persistent drift after 5 minutes (AIRP-03)"
    why_human: "A/V sync quality requires perceptual judgment by a human observer"
  - test: "Leave a mirroring session running for 30 minutes. Observe for drift or dropped connection."
    expected: "Session stable at 30 minutes; no A/V sync drift requiring reconnect (AIRP-04)"
    why_human: "Requires sustained hardware session; cannot be verified from static code analysis"
---

# Phase 4: AirPlay Verification Report

**Phase Goal:** iPhone, iPad, and Mac users can mirror their screen to AirShow via AirPlay with stable, synchronized audio and video
**Verified:** 2026-03-28T23:55:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | iPhone/iPad can select AirShow and mirrored screen appears within 3 seconds | ? NEEDS HUMAN | RAOP server wired, UxPlay starts on launch, mDNS advertises `_airplay._tcp` — cannot execute without Apple hardware |
| 2 | Mac can select AirShow and mirror its desktop to the receiver | ? NEEDS HUMAN | Same code path as iPhone/iPad; Mac uses same RAOP stack |
| 3 | Audio plays through receiver speakers in sync with video, no persistent drift after 5 minutes | ? NEEDS HUMAN | NTP-to-pipeline basetime normalization implemented in `onVideoFrame`/`onAudioFrame`; sync quality requires perceptual observation |
| 4 | 30-minute session with no A/V sync drift and no dropped-connection recovery needed | ? NEEDS HUMAN | `m_basetimeSet` captures basetime once per session; clock source is `gst_element_get_base_time`; stability requires real session observation |

**Score:** 4/4 must-have artifact truths VERIFIED (automated); 4/4 ROADMAP success criteria awaiting human verification

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `vendor/uxplay/lib/raop.h` | UxPlay v1.73.x submodule | VERIFIED | File exists; submodule pinned to v1.73.5 |
| `CMakeLists.txt` | UxPlay lib/ integration, libplist, AirPlayHandler.cpp | VERIFIED | `add_subdirectory(vendor/uxplay/lib)`, `pkg_check_modules(PLIST`, `airplay` in `target_link_libraries`, `AirPlayHandler.cpp` in `qt_add_executable`, `LANGUAGES CXX C` |
| `src/pipeline/MediaPipeline.h` | `initAppsrcPipeline()`, `videoAppsrc()`, `audioAppsrc()`, `setAudioCaps()` | VERIFIED | All 4 methods present and declared; private members `m_videoAppsrc` and `m_audioAppsrc` present |
| `src/pipeline/MediaPipeline.cpp` | appsrc pipeline with h264parse, decoder fallback chain, audio decodebin | VERIFIED | Two `appsrc` elements created (`video_appsrc`, `audio_appsrc`); `h264parse` in video branch; `vaapih264dec`→`avdec_h264` fallback; `video/x-h264,stream-format=byte-stream,alignment=nal` caps string present |
| `src/discovery/ServiceAdvertiser.h` | `updateTxtRecord()` pure virtual method | VERIFIED | Present at line 43; full signature matches plan spec |
| `src/discovery/AvahiAdvertiser.cpp` | Implementation of `updateTxtRecord()` | VERIFIED | Lines 65–109 implement full body: finds service type, updates or adds TXT key/value, resets group, re-registers via `createServices()`; thread-safe via `avahi_threaded_poll_lock` |
| `src/discovery/DiscoveryManager.h` | Public `updateTxtRecord()` and `deviceId()` methods | VERIFIED | Both public methods present with correct signatures |
| `src/discovery/DiscoveryManager.cpp` | Delegates to `m_advertiser->updateTxtRecord()` | VERIFIED | Line 123: `return m_advertiser->updateTxtRecord(serviceType, key, value)` |
| `src/protocol/AirPlayHandler.h` | `class AirPlayHandler : public ProtocolHandler`; all callbacks, `readPublicKeyFromKeyfile()` | VERIFIED | Full class declaration present; file-scope trampoline pattern used (void* storage for GstElement* to avoid header dependency) |
| `src/protocol/AirPlayHandler.cpp` | RAOP lifecycle, 7 callbacks, appsrc frame injection, PTS normalization, pk TXT update | VERIFIED | All 13 critical patterns confirmed present (see spot-check below) |
| `src/main.cpp` | AirPlayHandler registered with ProtocolManager, `startAll()`/`stopAll()`, plugin checks | VERIFIED | `#include "protocol/AirPlayHandler.h"`, `make_unique<AirPlayHandler>`, `addHandler`, `startAll`, `stopAll`, `h264parse` and `avdec_aac` in `checkRequiredPlugins` |
| `src/ui/ReceiverWindow.h` | `connectionBridge()` public accessor | VERIFIED | Line 20: `ConnectionBridge* connectionBridge() { return m_connectionBridge; }` |
| `src/protocol/ProtocolManager.cpp` | `addHandler()` calls `setMediaPipeline(m_pipeline)` | VERIFIED | Line 15: `handler->setMediaPipeline(m_pipeline);` — critical wiring for non-null pipeline |
| `tests/test_airplay.cpp` | Real unit tests: CanInstantiate, StopWithoutStart, SetMediaPipelineStoresPointer | VERIFIED | All 3 real tests present; replaced placeholder tests from Plan 01 scaffold |
| `tests/CMakeLists.txt` | `test_airplay` target with full dependency chain | VERIFIED | Compiled with AirPlayHandler.cpp, ConnectionBridge.cpp, MediaPipeline.cpp, DiscoveryManager.cpp, ServiceAdvertiser.cpp, AvahiAdvertiser.cpp, PkgConfig::AVAHI on Linux |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `CMakeLists.txt` | `vendor/uxplay/lib` | `add_subdirectory` | WIRED | `add_subdirectory(vendor/uxplay/lib/llhttp)`, `add_subdirectory(vendor/uxplay/lib/playfair)`, `add_subdirectory(vendor/uxplay/lib)` — correct order mirrors UxPlay root |
| `CMakeLists.txt` | `libplist-2.0` | `pkg_check_modules` | WIRED | `pkg_check_modules(PLIST REQUIRED IMPORTED_TARGET libplist-2.0)` |
| `AirPlayHandler.cpp` | `raop_callbacks_t` | callback registration in `start()` | WIRED | All 7 callbacks registered: `video_process`, `audio_process`, `conn_init`, `conn_destroy`, `conn_teardown`, `audio_get_format`, `report_client_request` |
| `AirPlayHandler.cpp` | `gst_app_src_push_buffer` | `onVideoFrame` and `onAudioFrame` | WIRED | Both `onVideoFrame` and `onAudioFrame` call `gst_app_src_push_buffer(GST_APP_SRC(...), buf)` with PTS normalization |
| `AirPlayHandler.cpp` | `ConnectionBridge::setConnected` | `QMetaObject::invokeMethod` in `onConnectionInit`/`onConnectionDestroy` | WIRED | Both connect (true + device name) and disconnect (false) events wired with `Qt::QueuedConnection` |
| `AirPlayHandler.cpp` | `DiscoveryManager::updateTxtRecord` | pk TXT record update in `start()` | WIRED | Updates `_airplay._tcp` and `_raop._tcp` after `raop_init2` generates keyfile |
| `src/main.cpp` | `ProtocolManager::addHandler` | `std::make_unique<AirPlayHandler>` | WIRED | `protocolManager.addHandler(std::move(airplay))` |
| `ProtocolManager::addHandler` | `ProtocolHandler::setMediaPipeline` | internal call in `addHandler()` | WIRED | `handler->setMediaPipeline(m_pipeline)` at line 15 of ProtocolManager.cpp — ensures non-null pipeline for all frame injection |
| `src/main.cpp` | `ProtocolManager::startAll` | function call | WIRED | `if (!protocolManager.startAll())` present |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| `AirPlayHandler.cpp` | `data->data` (video frames) | `raop_cb_video_process` callback from UxPlay RAOP server receiving live AirPlay data | Yes — live H.264 NAL units from iOS sender | FLOWING |
| `AirPlayHandler.cpp` | `data->data` (audio frames) | `raop_cb_audio_process` callback from UxPlay RAOP server | Yes — live AAC/ALAC audio frames from iOS sender | FLOWING |
| `AirPlayHandler.cpp` | PEM keyfile → `pk` hex string | `readPublicKeyFromKeyfile()` using OpenSSL `PEM_read_PrivateKey` + `EVP_PKEY_get_raw_public_key` on file written by `raop_init2` | Yes — real Ed25519 key from UxPlay crypto.c (deviation from plan: PEM format, not 64-byte binary) | FLOWING |
| `MediaPipeline.cpp` | `m_videoAppsrc`, `m_audioAppsrc` | `gst_element_factory_make("appsrc", ...)` stored as members, returned by accessors | Yes — real GstElement pointers cached by `initAppsrcPipeline()` | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| AirPlayHandler instantiates without crash | `test_airplay --gtest_filter=AirPlayHandlerTest.CanInstantiate` | PASSED (0 ms) | PASS |
| `stop()` without `start()` is safe | `test_airplay --gtest_filter=AirPlayHandlerTest.StopWithoutStart` | PASSED (0 ms) | PASS |
| `setMediaPipeline(nullptr)` does not crash | `test_airplay --gtest_filter=AirPlayHandlerTest.SetMediaPipelineStoresPointer` | PASSED (0 ms) | PASS |
| Full test suite (all 3 AirPlay tests) | `./build/linux-debug/tests/test_airplay` | 3/3 PASSED (0 ms total) | PASS |
| Binary contains AirPlay symbols | `nm airshow \| grep raop_init\|AirPlayHandler` | 126 matching symbols | PASS |
| RAOP server start on device with Apple hardware | Requires running app with iOS/macOS device | N/A | SKIP — route to human |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|------------|------------|-------------|--------|----------|
| AIRP-01 | 04-01, 04-02, 04-03 | User can mirror iPhone/iPad screen to AirShow via AirPlay | HUMAN NEEDED | RAOP server wired, UxPlay linked, mDNS advertising `_airplay._tcp`; confirmed by automated checks; end-to-end requires hardware |
| AIRP-02 | 04-01, 04-02, 04-03 | User can mirror macOS screen to AirShow via AirPlay | HUMAN NEEDED | Same code path as AIRP-01; Mac uses identical RAOP protocol stack |
| AIRP-03 | 04-01, 04-02, 04-03 | AirPlay mirroring includes synchronized audio and video | HUMAN NEEDED | NTP-to-basetime PTS normalization implemented in `onVideoFrame`/`onAudioFrame`; `m_basetimeSet` captures basetime on first frame; sync quality is perceptual |
| AIRP-04 | 04-01, 04-02, 04-03 | AirPlay connection maintains stable A/V sync during extended sessions | HUMAN NEEDED | Pipeline clock is GStreamer's monotonic clock; basetime captured once per session; 30-minute stability requires live observation |

All 4 requirements are mapped to Phase 4 in REQUIREMENTS.md (Traceability table, lines 107–110). No orphaned requirements — AIRP-01 through AIRP-04 are the only Phase 4 requirements and all three plans claim them.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `AirPlayHandler.cpp` | 326–371 | Multiple `return {}` | Info | These are early-return error paths in `readPublicKeyFromKeyfile()`, each preceded by `g_warning()`. Not stubs — correct defensive error handling. |
| `ServiceAdvertiser.h` | 37 | Word "placeholder" | Info | In a comment describing the pk TXT field's initial state before the real key is known. Not a code placeholder. |

No blockers or warnings found. All `return {}` patterns are guarded error returns in OpenSSL key-reading code, not stub implementations. No hardcoded empty data flows to rendering paths.

### Human Verification Required

#### 1. iOS/iPadOS Screen Mirroring (AIRP-01)

**Test:** Build and run `./build/linux-debug/airshow`. On an iPhone or iPad connected to the same LAN, open Control Center, tap Screen Mirroring, and look for "AirShow" (or the configured receiver name).
**Expected:** AirShow appears in the AirPlay device picker. Tapping it initiates mirroring and the phone's screen appears in the receiver window within 3 seconds.
**Why human:** Requires a physical iOS/iPadOS device; the AirPlay handshake (FairPlay SRP, Ed25519 pk exchange) cannot be simulated from static analysis.

#### 2. macOS Screen Mirroring (AIRP-02)

**Test:** On a Mac on the same LAN, open Control Center > Screen Mirroring and select "AirShow".
**Expected:** Mac desktop mirrors to the receiver window.
**Why human:** Requires macOS hardware; RAOP stack is the same but macOS AirPlay sender has different negotiation behavior.

#### 3. Audio/Video Sync Quality (AIRP-03)

**Test:** Mirror from iPhone/iPad with a video playing that has a clearly audible audio track (e.g., a ticking clock or music video). Observe the receiver for 5+ minutes.
**Expected:** Audio is perceptibly in sync with video; no persistent drift observable after 5 minutes.
**Why human:** A/V sync quality is a perceptual judgment; code analysis confirms PTS normalization is implemented but cannot verify the quality of clock synchronization at runtime.

#### 4. 30-Minute Session Stability (AIRP-04)

**Test:** Run a mirroring session for 30 minutes without disconnecting. Observe for A/V drift accumulation or connection drops requiring reconnect.
**Expected:** Session stable at 30 minutes; audio and video remain in sync; no reconnect needed.
**Why human:** Requires sustained hardware session; cannot be verified from code inspection.

### Gaps Summary

No automated gaps. All code artifacts exist, are substantive, and are wired. The binary compiles (6.2 MB), links 126 AirPlay-related symbols, and the test suite passes 3/3. The only items that cannot be verified programmatically are the four ROADMAP success criteria, which all require live hardware testing with Apple devices.

The implementation is structurally complete for end-to-end AirPlay mirroring:
- UxPlay v1.73.5 embedded as `libairplay.a`, linked into `airshow`
- `MediaPipeline::initAppsrcPipeline()` creates a two-branch GStreamer pipeline (H.264 video + AAC/ALAC audio via appsrc)
- `AirPlayHandler` wraps the RAOP server with 7 C-to-C++ callback trampolines
- Video and audio frames injected into appsrc with NTP-derived PTS normalization
- Ed25519 public key extracted from PEM keyfile (OpenSSL) and published to mDNS TXT records
- HUD updates wired thread-safely via `QMetaObject::invokeMethod(Qt::QueuedConnection)`
- `ProtocolManager::addHandler()` ensures `setMediaPipeline()` is called before `start()`
- Clean teardown on `stop()` and disconnect

---

_Verified: 2026-03-28T23:55:00Z_
_Verifier: Claude (gsd-verifier)_
