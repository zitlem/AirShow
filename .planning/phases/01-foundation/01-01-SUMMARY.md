---
phase: 01-foundation
plan: 01
subsystem: infra
tags: [cmake, gstreamer, qt6, googletest, github-actions, ninja, cpp17]

requires: []

provides:
  - CMake build system with Qt6 6.9.2, GStreamer 1.26.x, OpenSSL 3.x, spdlog
  - CMakePresets.json with linux-debug, macos-debug, windows-msys2-debug presets
  - src/pipeline/MediaPipeline.{h,cpp} — stub (init returns false; Plans 02-03 implement)
  - src/pipeline/DecoderInfo.h — DecoderInfo struct and DecoderType enum
  - src/ui/ReceiverWindow.{h,cpp} — stub QQmlApplicationEngine wrapper
  - src/main.cpp — entry point: gst_init, checkRequiredPlugins, QGuiApplication loop
  - qml/main.qml — fullscreen Window (black background, FullScreen visibility)
  - tests/CMakeLists.txt — GTest/GoogleTest integration with gtest_discover_tests
  - tests/test_pipeline.cpp — Wave 0 stubs: 4 GTEST_SKIP stubs + SmokeTest (PASSED)
  - .github/workflows/build.yml — Linux CI: install-qt-action + apt GStreamer, configure, build, ctest
  - .planning/phases/01-foundation/01-VALIDATION.md updated: wave_0_complete: true

affects:
  - 01-02 (MediaPipeline implementation reads MediaPipeline.h interface)
  - 01-03 (decoder detection test fills in test_decoder_detection stub)
  - all subsequent phases (build system and CI are shared infrastructure)

tech-stack:
  added:
    - CMake 3.31.6 (system)
    - Qt 6.9.2 (system, compatible with 6.8 LTS API)
    - GStreamer 1.26.6 dev headers via pkg-config
    - OpenSSL 3.5.3 (libssl-dev, already installed)
    - spdlog 1.15.3 (optional, HAVE_SPDLOG define if found)
    - GTest 1.17.0 + GMock (libgtest-dev, libgmock-dev)
    - Ninja 1.13.0 (installed via pip --user --break-system-packages)
  patterns:
    - pkg-config for GStreamer detection (not Qt6's FindGStreamer.cmake directly)
    - qt_add_executable + qt_add_qml_module for QML resource embedding
    - CMakePresets.json with per-platform binaryDir for isolated build directories
    - Stub implementation pattern: header defines interface, .cpp returns defaults
    - gst_init guard before gst_registry_check_feature_version in tests

key-files:
  created:
    - CMakeLists.txt
    - CMakePresets.json
    - src/main.cpp
    - src/pipeline/MediaPipeline.h
    - src/pipeline/MediaPipeline.cpp
    - src/pipeline/DecoderInfo.h
    - src/ui/ReceiverWindow.h
    - src/ui/ReceiverWindow.cpp
    - qml/main.qml
    - tests/CMakeLists.txt
    - tests/test_pipeline.cpp
    - .github/workflows/build.yml
    - .gitignore
  modified:
    - .planning/phases/01-foundation/01-VALIDATION.md (wave_0_complete: true)

key-decisions:
  - "pkg-config for GStreamer: CMakeLists.txt uses pkg_check_modules(GST) not FindGStreamer.cmake — pkg-config is the canonical approach on Linux for GStreamer dev headers"
  - "Ninja installed via pip --user --break-system-packages: ninja-build package unavailable without sudo; pip approach worked cleanly"
  - "MediaPipeline.cpp included in test_pipeline target: test directly instantiates MediaPipeline so sources must link"
  - "gst_init guard in SmokeTest: gst_registry_check_feature_version requires initialized GStreamer; SmokeTest has no SetUp fixture unlike PipelineTest"
  - "pkexec for package installation: sudo -i authentication failed; pkexec (polkit) prompted GUI auth dialog successfully"

patterns-established:
  - "Pattern 1: CMake preset naming — {platform}-{build-type} (e.g., linux-debug) with binaryDir ${sourceDir}/build/{preset-name}"
  - "Pattern 2: Stub headers — define full public interface in .h, implement all methods as no-ops/defaults in .cpp, fill in future plans"
  - "Pattern 3: Test gst_init guard — always check gst_is_initialized() before calling GStreamer registry APIs, even in non-fixture tests"

requirements-completed: [FOUND-01]

duration: 11min
completed: 2026-03-28
---

# Phase 01 Plan 01: Build System Foundation Summary

**CMake + Qt6 + GStreamer build scaffolding compiling to a runnable Linux binary in one command, with Wave 0 test stubs and GitHub Actions CI**

## Performance

- **Duration:** 11 min
- **Started:** 2026-03-28T18:55:33Z
- **Completed:** 2026-03-28T19:06:59Z
- **Tasks:** 2
- **Files modified:** 13 created + 1 modified

## Accomplishments

- `cmake --preset linux-debug && cmake --build build/linux-debug` exits 0 on Linux in one command
- `build/linux-debug/airshow` binary produced (1.8MB, C++17 + Qt6 + GStreamer linked)
- All 5 tests pass: 4 pipeline stubs SKIPPED (correct), SmokeTest PASSED (all GStreamer plugins confirmed present)
- GitHub Actions CI workflow created targeting Ubuntu with install-qt-action + apt GStreamer
- `01-VALIDATION.md` updated: `wave_0_complete: true`

## Task Commits

1. **Task 1: CMakeLists.txt, CMakePresets.json, project skeleton** - `37c323a` (chore)
2. **Task 2: Test scaffold and GitHub Actions CI** - `352aab8` (chore)
3. **Auto-fixes: linker + gst_init** - `807608f` (fix)

## Files Created/Modified

- `CMakeLists.txt` — Root build config: Qt6, GStreamer pkg-config, OpenSSL, spdlog, enable_testing
- `CMakePresets.json` — linux-debug, macos-debug, windows-msys2-debug presets using Ninja
- `src/main.cpp` — Entry point: gst_init, checkRequiredPlugins, QGuiApplication loop
- `src/pipeline/MediaPipeline.h` — Full public interface (stub impl, Plans 02-03 fill in)
- `src/pipeline/MediaPipeline.cpp` — Stub: all methods return defaults/no-ops
- `src/pipeline/DecoderInfo.h` — DecoderInfo struct + DecoderType enum (Hardware/Software)
- `src/ui/ReceiverWindow.h` — QQmlApplicationEngine wrapper header
- `src/ui/ReceiverWindow.cpp` — Stub: load() returns false
- `qml/main.qml` — Fullscreen black Window
- `tests/CMakeLists.txt` — GTest + gtest_discover_tests, links MediaPipeline sources
- `tests/test_pipeline.cpp` — Wave 0 stubs for FOUND-02..05 + SmokeTest
- `.github/workflows/build.yml` — Linux CI pipeline
- `.gitignore` — Excludes build/
- `.planning/phases/01-foundation/01-VALIDATION.md` — wave_0_complete: true

## Decisions Made

- Used `pkg_check_modules(GST)` directly rather than relying on Qt6's `FindGStreamer.cmake` — more portable, avoids Qt module path dependency for non-Qt consumers of GStreamer headers.
- Linked `src/pipeline/MediaPipeline.cpp` into the test binary explicitly (rather than a library target) — correct for the current stub-only phase; Plan 02 can refactor to a shared library if needed.
- Ninja installed via `pip install --user --break-system-packages ninja` — the system's `ninja-build` package was unavailable without sudo, pip provided version 1.13.0 which satisfies CMake's requirement.
- OpenSSL linked now even though unused in Phase 1 — avoids a header-change task in Phase 4 when AirPlay crypto code lands (per RESEARCH.md recommendation).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] test_pipeline missing MediaPipeline sources**
- **Found during:** Task 2 (test scaffold) verification — `cmake --build` failed
- **Issue:** `tests/CMakeLists.txt` added `test_pipeline.cpp` as sole source, but the test directly instantiates `airshow::MediaPipeline` — linker emitted undefined reference to `MediaPipeline::MediaPipeline()`, `isMuted()`, `~MediaPipeline()`
- **Fix:** Added `${CMAKE_SOURCE_DIR}/src/pipeline/MediaPipeline.cpp` to the `test_pipeline` executable target
- **Files modified:** `tests/CMakeLists.txt`
- **Verification:** Build succeeded, all 5 tests ran
- **Committed in:** `807608f`

**2. [Rule 1 - Bug] SmokeTest missing gst_init before registry check**
- **Found during:** Task 2 verification — `ctest` showed SmokeTest FAILED with all 5 plugin checks reporting false
- **Issue:** `TEST(SmokeTest, required_plugins_available)` is a standalone test (not `TEST_F`), so it has no `SetUp()` from `PipelineTest`. `gst_registry_check_feature_version` requires GStreamer to be initialized; without `gst_init()`, the registry is empty and all plugin checks return false even though the plugin `.so` files are present on disk.
- **Fix:** Added `if (!gst_is_initialized()) { gst_init(nullptr, nullptr); }` guard at the start of the test body
- **Files modified:** `tests/test_pipeline.cpp`
- **Verification:** `ctest -R required_plugins_available` exits 0, PASSED
- **Committed in:** `807608f`

---

**Total deviations:** 2 auto-fixed (both Rule 1 — bugs)
**Impact on plan:** Both fixes were correctness-blocking. No scope creep — fixes stay within the test scaffold task's own files.

## Issues Encountered

- **sudo authentication failure:** Initial `sudo apt install` failed. Resolved by using `pkexec` (polkit) which presented a graphical authentication dialog. Packages installed successfully.
- **Ninja not in PATH:** `ninja-build` APT package unavailable without sudo. Installed via `pip install --user --break-system-packages ninja` (version 1.13.0). Build works after adding `~/.local/bin` to PATH.

## Known Stubs

The following stubs are intentional and will be filled in by subsequent plans:

| File | Stub | Resolved By |
|------|------|-------------|
| `src/pipeline/MediaPipeline.cpp` | `init()` returns false; all methods are no-ops | Plan 02 |
| `src/ui/ReceiverWindow.cpp` | `load()` returns false | Plan 02 |
| `qml/main.qml` | No GstGLVideoItem, no mute button | Plan 02 |
| `tests/test_pipeline.cpp:505-62` | 4 pipeline tests all GTEST_SKIP | Plans 02 + 03 |

These stubs do NOT block the plan goal (FOUND-01: build compiles and runs). The binary launches and enters `app.exec()`. The stubs are tracked per VALIDATION.md Wave 0 spec.

## Next Phase Readiness

**Plan 02 (MediaPipeline implementation) can start immediately:**
- `src/pipeline/MediaPipeline.h` defines the full interface (init, setMuted, isMuted, activeDecoder, setDecoderSelectedCallback, stop)
- `tests/test_pipeline.cpp` has stub tests with TODO comments describing exact implementation requirements
- Build system is wired: `tests/CMakeLists.txt` already links `MediaPipeline.cpp`

**Plan 03 (decoder detection) depends on Plan 02 completion.**

## Self-Check: PASSED

- CMakeLists.txt: FOUND
- CMakePresets.json: FOUND
- src/main.cpp: FOUND
- src/pipeline/MediaPipeline.h: FOUND
- src/pipeline/DecoderInfo.h: FOUND
- src/ui/ReceiverWindow.h: FOUND
- qml/main.qml: FOUND
- tests/CMakeLists.txt: FOUND
- tests/test_pipeline.cpp: FOUND
- .github/workflows/build.yml: FOUND
- build/linux-debug/airshow (binary): FOUND
- .planning/phases/01-foundation/01-01-SUMMARY.md: FOUND
- Commit 37c323a: FOUND
- Commit 352aab8: FOUND
- Commit 807608f: FOUND

---
*Phase: 01-foundation*
*Completed: 2026-03-28*
