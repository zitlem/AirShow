---
phase: 1
slug: foundation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest (CMake's built-in test runner) + GStreamer pipeline probes |
| **Config file** | CMakeLists.txt (enable_testing()) |
| **Quick run command** | `ctest --test-dir build/linux-debug --output-on-failure -R quick` |
| **Full suite command** | `ctest --test-dir build/linux-debug --output-on-failure` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build/linux-debug && ctest --test-dir build/linux-debug --output-on-failure -R quick`
- **After every plan wave:** Run `ctest --test-dir build/linux-debug --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 1 | FOUND-01 | build | `cmake --build build/linux-debug` | ❌ W0 | ⬜ pending |
| 01-02-01 | 02 | 1 | FOUND-02 | integration | `ctest --test-dir build/linux-debug -R test_video_pipeline` | ❌ W0 | ⬜ pending |
| 01-02-02 | 02 | 1 | FOUND-03 | integration | `ctest --test-dir build/linux-debug -R test_audio_pipeline` | ❌ W0 | ⬜ pending |
| 01-02-03 | 02 | 1 | FOUND-04 | integration | `ctest --test-dir build/linux-debug -R test_mute_toggle` | ❌ W0 | ⬜ pending |
| 01-03-01 | 03 | 2 | FOUND-05 | integration | `ctest --test-dir build/linux-debug -R test_decoder_detection` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `CMakeLists.txt` — enable_testing() and add_test() stubs
- [ ] `tests/` directory created with test runner setup

*Wave 0 is part of the build system plan (Plan 01). After Plan 01 Task 2 smoke test passes, set `wave_0_complete: true` in this file's frontmatter.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Fullscreen window renders visible moving video | FOUND-02 | Visual verification of GStreamer test pattern display | Launch app, confirm videotestsrc pattern is visible and animated |
| Audio plays through speakers | FOUND-03 | Audio output verification | Launch app, confirm audiotestsrc tone is audible |
| Mute toggle silences audio | FOUND-04 | Audio state change verification | Click mute, confirm silence; click unmute, confirm audio resumes |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
