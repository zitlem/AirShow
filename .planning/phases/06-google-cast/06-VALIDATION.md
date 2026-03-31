---
phase: 6
slug: google-cast
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-03-29
---

# Phase 6 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GTest (already in project from Phase 1) |
| **Config file** | `CMakeLists.txt` (test targets defined there) |
| **Quick run command** | `cd build/linux-debug && ctest -R test_cast --output-on-failure` |
| **Full suite command** | `cd build/linux-debug && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build/linux-debug && ctest -R test_cast --output-on-failure`
- **After every plan wave:** Run `cd build/linux-debug && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 06-01-T1 | 01 | 1 | CAST-01, CAST-02 | build | `ninja airshow` | creates src | pending |
| 06-01-T2 | 01 | 1 | CAST-01, CAST-02 | unit | `ninja test_cast && ctest -R test_cast` | creates tests | pending |
| 06-02-T1 | 02 | 2 | CAST-01, CAST-02, CAST-03 | build | `ninja airshow` | exists | pending |
| 06-02-T2 | 02 | 2 | CAST-01, CAST-02 | unit | `ninja test_cast && ctest -R test_cast` | exists | pending |
| 06-03-T1 | 03 | 3 | CAST-01, CAST-02, CAST-03 | integration | `ninja airshow && ctest -R test_cast` | exists | pending |
| 06-03-T2 | 03 | 3 | CAST-01, CAST-02, CAST-03 | human-verify | `./airshow --help` | exists | pending |

---

## Wave 0 Requirements

- [x] `tests/test_cast.cpp` — created by Plan 01 Task 2 with stub tests
- [x] test_cast target in CMakeLists.txt — created by Plan 01 Task 2

*Wave 0 is absorbed into Plan 01 Task 2 (Wave 1). Test scaffold is created before any subsequent plan runs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Android discovers and casts to AirShow | CAST-01 | Requires real Android device | 1. Start AirShow 2. Open Cast menu on Android 3. Select LVMS |
| Chrome tab cast works | CAST-02 | Requires Chrome browser | 1. Open Chrome 2. Cast tab 3. Select LVMS |
| Audio plays in sync with video | CAST-03 | Requires real media stream | 1. Cast content 2. Verify A/V sync |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 10s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved 2026-03-29
