---
phase: 3
slug: display-receiver-ui
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest + GTest (established in Phase 1) |
| **Config file** | CMakeLists.txt / tests/CMakeLists.txt |
| **Quick run command** | `ctest --test-dir build/linux-debug --output-on-failure -R test_display` |
| **Full suite command** | `ctest --test-dir build/linux-debug --output-on-failure` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build/linux-debug && ctest --test-dir build/linux-debug --output-on-failure -R test_display`
- **After every plan wave:** Run `ctest --test-dir build/linux-debug --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | DISP-02, DISP-03 | unit | `ctest --test-dir build/linux-debug -R test_connection_bridge` | ❌ W0 | ⬜ pending |
| 03-01-02 | 01 | 1 | DISP-02, DISP-03 | build | `cmake --build build/linux-debug --target test_display` | ❌ W0 | ⬜ pending |
| 03-02-01 | 02 | 2 | DISP-02, DISP-03 | unit | `ctest --test-dir build/linux-debug -R test_connection_bridge` | ❌ W0 | ⬜ pending |
| 03-03-01 | 03 | 3 | DISP-01, DISP-02, DISP-03 | build | `cmake --build build/linux-debug` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_display.cpp` — test stubs for ConnectionBridge and SettingsBridge
- [ ] Test fixtures for bridge state verification

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Letterboxing visible with mismatched aspect ratio | DISP-01 | Visual verification of black bars | Launch app, verify videotestsrc letterboxed on non-16:9 display |
| HUD appears on simulated connect | DISP-02 | Visual verification of overlay | Trigger setConnected(true), verify overlay shows and fades after 3s |
| Idle screen visible on launch | DISP-03 | Visual verification of idle state | Launch app, verify app name + receiver name + "Waiting..." visible |
| Mute button visible and styled | DISP-01 | Visual verification of button | Verify mute button is visible and matches overlay style |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
