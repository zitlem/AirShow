---
phase: 6
slug: google-cast
status: draft
nyquist_compliant: false
wave_0_complete: false
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
| 06-01-00 | 01 | 0 | CAST-01 | scaffold | `ctest -R test_cast` | creates it | pending |
| 06-01-01 | 01 | 1 | CAST-01 | unit | `ctest -R test_cast` | W0 | pending |
| 06-02-01 | 02 | 2 | CAST-01, CAST-02 | integration | `ctest -R test_cast` | W0 | pending |
| 06-03-01 | 03 | 3 | CAST-01, CAST-02, CAST-03 | e2e | `ctest -R test_cast` | W0 | pending |

---

## Wave 0 Requirements

- [ ] `tests/test_cast.cpp` — stubs for CAST-01, CAST-02, CAST-03
- [ ] test_cast target in CMakeLists.txt — links GTest, Qt6::Core, protobuf

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Android discovers and casts to MyAirShow | CAST-01 | Requires real Android device | 1. Start MyAirShow 2. Open Cast menu on Android 3. Select LVMS |
| Chrome tab cast works | CAST-02 | Requires Chrome browser | 1. Open Chrome 2. Cast tab 3. Select LVMS |
| Audio plays in sync with video | CAST-03 | Requires real media stream | 1. Cast content 2. Verify A/V sync |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
