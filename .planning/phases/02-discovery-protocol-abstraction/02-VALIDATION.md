---
phase: 2
slug: discovery-protocol-abstraction
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest + GTest (established in Phase 1) + avahi-browse / dns-sd CLI probes |
| **Config file** | CMakeLists.txt / tests/CMakeLists.txt |
| **Quick run command** | `ctest --test-dir build/linux-debug --output-on-failure -R quick` |
| **Full suite command** | `ctest --test-dir build/linux-debug --output-on-failure` |
| **Estimated runtime** | ~15 seconds |

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
| 02-01-01 | 01 | 1 | DISC-01 | integration | `ctest --test-dir build/linux-debug -R test_airplay_mdns` | ❌ W0 | ⬜ pending |
| 02-01-02 | 01 | 1 | DISC-02 | integration | `ctest --test-dir build/linux-debug -R test_cast_mdns` | ❌ W0 | ⬜ pending |
| 02-01-03 | 01 | 1 | DISC-03 | integration | `ctest --test-dir build/linux-debug -R test_dlna_ssdp` | ❌ W0 | ⬜ pending |
| 02-02-01 | 02 | 2 | DISC-04 | integration | `ctest --test-dir build/linux-debug -R test_receiver_name` | ❌ W0 | ⬜ pending |
| 02-02-02 | 02 | 2 | DISC-05 | unit | `ctest --test-dir build/linux-debug -R test_firewall` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_discovery.cpp` — test stubs for mDNS/SSDP discovery
- [ ] Test fixtures for Avahi client mocking or live probing

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| MyAirShow appears in AirPlay picker on iOS | DISC-01 | Requires real iOS device on same network | Open Control Center > Screen Mirroring, look for MyAirShow |
| MyAirShow appears in Cast menu on Android | DISC-02 | Requires real Android device on same network | Open Settings > Connected devices > Cast, look for MyAirShow |
| MyAirShow appears in DLNA controller app | DISC-03 | Requires DLNA controller app on same network | Open BubbleUPnP or similar, check Renderers list |
| Name change propagates to device pickers | DISC-04 | Requires physical sender devices to verify | Change name, re-check pickers within 10 seconds |
| Windows firewall auto-configured | DISC-05 | Requires Windows machine | Fresh install, verify discovery works without manual port config |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
