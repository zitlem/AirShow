---
phase: 04-airplay
plan: 01
subsystem: protocol-foundation
tags: [uxplay, gstreamer, appsrc, discovery, mdns, cmake]
dependency_graph:
  requires: [03-display-receiver-ui]
  provides: [airplay-static-lib, appsrc-pipeline, txt-record-update, test-airplay-scaffold]
  affects: [src/pipeline/MediaPipeline, src/discovery/ServiceAdvertiser, tests]
tech_stack:
  added: [UxPlay v1.73.5, libplist-2.0 v2.6.0, avahi-compat-libdns_sd v0.8, llhttp, playfair]
  patterns: [appsrc-injection, vendor-pkg-config, static-submodule-library]
key_files:
  created:
    - vendor/uxplay (submodule v1.73.5)
    - vendor/libplist/ (vendored headers + .so)
    - vendor/avahi/include/avahi-compat-libdns_sd/ (vendored dns_sd.h)
    - vendor/avahi/lib/x86_64-linux-gnu/libdns_sd.* (vendored avahi-compat)
    - vendor/avahi/lib/x86_64-linux-gnu/pkgconfig/avahi-compat-libdns_sd.pc
    - tests/test_airplay.cpp
  modified:
    - CMakeLists.txt
    - CMakePresets.json
    - src/pipeline/MediaPipeline.h
    - src/pipeline/MediaPipeline.cpp
    - src/discovery/ServiceAdvertiser.h
    - src/discovery/AvahiAdvertiser.h
    - src/discovery/AvahiAdvertiser.cpp
    - src/discovery/DiscoveryManager.h
    - src/discovery/DiscoveryManager.cpp
    - tests/CMakeLists.txt
decisions:
  - "UxPlay lib/ requires llhttp and playfair subdirectories added before lib/ itself — mirrors root UxPlay CMakeLists.txt order"
  - "libplist-2.0, avahi-compat-libdns_sd vendored via deb extraction — sudo unavailable; vendor/libplist/ and vendor/avahi/ with fixed .pc prefix paths"
  - "avahi-compat-libdns_sd vendored into existing vendor/avahi/ tree — avoids new top-level vendor directory"
  - "initAppsrcPipeline() starts pipeline in GST_STATE_PAUSED — AirPlayHandler transitions to PLAYING on first frame"
  - "decodebin used for audio in appsrc pipeline — handles AAC and ALAC codec negotiation automatically"
  - "test_airplay scaffold links GTest only (no GStreamer) — consistent with test isolation pattern from Phases 2-3"
metrics:
  duration: "~7 minutes"
  completed: "2026-03-28"
  tasks_completed: 2
  files_modified: 10
  files_created: 3
---

# Phase 4 Plan 1: AirPlay Foundation Summary

UxPlay v1.73.5 embedded as a git submodule with `airplay` static library linked into myairshow; GStreamer `appsrc`-based pipeline added to `MediaPipeline` for H.264/AAC injection; `ServiceAdvertiser.updateTxtRecord()` API added for post-start TXT record updates; `test_airplay` scaffold compiles and passes.

## What Was Built

### Task 1: UxPlay Submodule + CMake Integration + appsrc Pipeline

**UxPlay as Git Submodule:**
- `vendor/uxplay` added at v1.73.5 (v1.73.6 tag not yet published on GitHub)
- `CMakeLists.txt` adds `llhttp`, `playfair`, then `vendor/uxplay/lib` in that order — mirrors UxPlay's own root CMakeLists.txt to ensure playfair/llhttp static libs are available when `airplay` links against them
- `OPENSSL_FOUND=TRUE` set before `add_subdirectory` to prevent UxPlay from running `find_package(OpenSSL 1.1.1)` redundantly

**Dependency Vendoring (no sudo available):**
- `libplist-2.0` v2.6.0 extracted from `.deb` to `vendor/libplist/` with fixed `.pc` prefix
- `avahi-compat-libdns_sd` extracted from `.deb` to `vendor/avahi/` (headers, static lib, shared lib, `.pc`) — required by UxPlay for `dns_sd.h`
- `CMakePresets.json` linux-debug `PKG_CONFIG_PATH` updated to include `vendor/libplist/lib/.../pkgconfig`

**MediaPipeline appsrc Extension:**
- `initAppsrcPipeline(void* qmlVideoItem)` creates a two-branch pipeline:
  - Video: `appsrc(video_appsrc)` → `h264parse` → `vaapih264dec`/`avdec_h264` fallback → `videoconvert` → `glupload` → `qml6glsink` (or `fakesink`)
  - Audio: `appsrc(audio_appsrc)` → `decodebin` (dynamic pad) → `audioconvert` → `audioresample` → `autoaudiosink`
- Pipeline starts in `GST_STATE_PAUSED`; AirPlayHandler will transition to `GST_STATE_PLAYING`
- `setAudioCaps(const char*)` updates audio appsrc caps for AAC/ALAC when codec is known
- Existing `init()` method untouched — test-source mode preserved

### Task 2: ServiceAdvertiser TXT Update API + test_airplay Scaffold

**ServiceAdvertiser.updateTxtRecord():**
- Pure virtual method added to `ServiceAdvertiser` interface
- `AvahiAdvertiser::updateTxtRecord()` finds matching service type, updates (or adds) the TXT key/value, then resets and re-registers the entry group via existing `createServices()` path
- `DiscoveryManager::updateTxtRecord()` delegates to `m_advertiser->updateTxtRecord()`
- Thread-safe: holds `avahi_threaded_poll_lock()` during update

**test_airplay scaffold:**
- `tests/test_airplay.cpp` with two placeholder `EXPECT_TRUE(true)` tests
- `test_airplay` target in `tests/CMakeLists.txt` — links `GTest::GTest GTest::Main` only (no GStreamer dependency)
- `ctest -R test_airplay` passes: 2/2 tests pass

## Verification Results

```
cmake --build build/linux-debug     → exit 0 (all targets built)
vendor/uxplay/lib/raop.h exists     → confirmed
ctest -R test_airplay               → 2/2 PASSED
grep initAppsrcPipeline MediaPipeline.h → 2 matches
grep updateTxtRecord ServiceAdvertiser.h → 1 match
```

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] libplist not system-installed; sudo unavailable**
- **Found during:** Task 1, Step 1 (cmake configure)
- **Issue:** `pkg_check_modules(PLIST REQUIRED IMPORTED_TARGET libplist-2.0)` fails — libplist-dev not installed and sudo auth fails
- **Fix:** Downloaded `libplist-dev` and `libplist-2.0-4` .debs, extracted to `vendor/libplist/`, updated `CMakePresets.json` PKG_CONFIG_PATH, fixed `.pc` prefix
- **Files modified:** `vendor/libplist/`, `CMakePresets.json`
- **Commit:** 695ae5c

**2. [Rule 3 - Blocking] avahi-compat-libdns_sd not vendored; required by UxPlay**
- **Found during:** Task 1, cmake configure of `vendor/uxplay/lib`
- **Issue:** UxPlay `lib/CMakeLists.txt` calls `pkg_search_module(AVAHI_DNSSD avahi-compat-libdns_sd)` and links against `dns_sd` — not in existing vendor/avahi tree
- **Fix:** Downloaded `libavahi-compat-libdnssd-dev` and `libavahi-compat-libdnssd1` .debs (already in project directory), extracted to `vendor/avahi/`, added `.pc` with fixed prefix
- **Files modified:** `vendor/avahi/` (new files), CMakePresets.json already had avahi path
- **Commit:** 695ae5c

**3. [Rule 3 - Blocking] UxPlay lib/ links playfair/llhttp but subdirs not built**
- **Found during:** Task 1, link step
- **Issue:** `add_subdirectory(vendor/uxplay/lib)` alone: `airplay` target links `-lplayfair -lllhttp` but those targets don't exist in the CMake graph
- **Fix:** Added `add_subdirectory(vendor/uxplay/lib/llhttp)` and `add_subdirectory(vendor/uxplay/lib/playfair)` before `add_subdirectory(vendor/uxplay/lib)` — mirrors order in UxPlay root CMakeLists.txt
- **Files modified:** `CMakeLists.txt`
- **Commit:** 695ae5c

**4. [Rule 2 - Missing functionality] UxPlay uses v1.73.5 tag (v1.73.6 not yet tagged)**
- **Found during:** Task 1, git tag inspection
- **Issue:** PLAN.md specifies v1.73.6 but that tag does not exist on GitHub; latest stable tag is v1.73.5
- **Fix:** Used v1.73.5 (last stable release); the HEAD commit is pre-release 1.73.6 work. v1.73.5 is functionally equivalent for this foundation task
- **No file change** — submodule pinned to v1.73.5 tag

## Known Stubs

None — no data flow to UI in this plan. All additions are build infrastructure and API surface.

## Commits

| Hash | Message |
|------|---------|
| 695ae5c | feat(04-01): UxPlay submodule + CMake integration + appsrc pipeline |
| 8cf0d4c | feat(04-01): ServiceAdvertiser TXT record update API + test_airplay scaffold |
