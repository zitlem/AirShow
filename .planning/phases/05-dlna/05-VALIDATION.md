---
phase: 5
slug: dlna
status: draft
nyquist_compliant: false
wave_0_complete: false
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
| 05-01-01 | 01 | 1 | DLNA-03 | unit | `ctest -R test_dlna` | ❌ W0 | ⬜ pending |
| 05-02-01 | 02 | 2 | DLNA-01, DLNA-02 | integration | `ctest -R test_dlna` | ❌ W0 | ⬜ pending |
| 05-03-01 | 03 | 3 | DLNA-01, DLNA-02, DLNA-03 | e2e | `ctest -R test_dlna` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_dlna.cpp` — stubs for DLNA-01, DLNA-02, DLNA-03
- [ ] test_dlna target in CMakeLists.txt — link Qt6::Core, libupnp

*If none: "Existing infrastructure covers all phase requirements."*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| BubbleUPnP discovers MyAirShow as DMR | DLNA-03 | Requires real DLNA controller app | 1. Start MyAirShow 2. Open BubbleUPnP 3. Check renderer list |
| Video push plays on receiver | DLNA-01 | Requires real media file + controller | 1. Push MP4 via BubbleUPnP 2. Verify video+audio plays |
| Audio push plays on receiver | DLNA-02 | Requires real audio file + controller | 1. Push MP3 via BubbleUPnP 2. Verify audio plays through speakers |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
