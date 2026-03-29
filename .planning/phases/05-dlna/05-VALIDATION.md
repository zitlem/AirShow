---
phase: 5
slug: dlna
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-03-28
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GTest (already in project from Phase 1) |
| **Config file** | `CMakeLists.txt` (test targets defined there) |
| **Quick run command** | `cd build && ctest --test-dir . -R test_dlna --output-on-failure` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest --test-dir . -R test_dlna --output-on-failure`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 05-01-00 | 01 | 0 | DLNA-03 | scaffold | `ctest -R test_dlna` | creates it | ⬜ pending |
| 05-01-01 | 01 | 1 | DLNA-03 | unit | `ctest -R test_dlna` | ✅ W0 | ⬜ pending |
| 05-01-02 | 01 | 1 | DLNA-03 | unit | `ctest -R test_dlna` | ✅ W0 | ⬜ pending |
| 05-02-01 | 02 | 2 | DLNA-01, DLNA-02 | integration | `ctest -R test_dlna` | ✅ W0 | ⬜ pending |
| 05-03-01 | 03 | 3 | DLNA-01, DLNA-02, DLNA-03 | e2e | `ctest -R test_dlna` | ✅ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `tests/test_dlna.cpp` — created by Plan 05-01 Task 0 with GTEST_SKIP stubs
- [x] test_dlna target in CMakeLists.txt — links GTest, builds and runs (all skipped) before production code

*Wave 0 is Task 0 of Plan 05-01. It creates the test scaffold with stub bodies that skip, ensuring the test target exists and passes before any production DLNA code is written. Task 1 then replaces stubs with real assertions.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| BubbleUPnP discovers MyAirShow as DMR | DLNA-03 | Requires real DLNA controller app | 1. Start MyAirShow 2. Open BubbleUPnP 3. Check renderer list |
| Video push plays on receiver | DLNA-01 | Requires real media file + controller | 1. Push MP4 via BubbleUPnP 2. Verify video+audio plays |
| Audio push plays on receiver | DLNA-02 | Requires real audio file + controller | 1. Push MP3 via BubbleUPnP 2. Verify audio plays through speakers |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 10s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved
