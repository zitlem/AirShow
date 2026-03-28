---
phase: 4
slug: airplay
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 4 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Google Test (CMake `FetchContent` or system) — pattern from existing `test_foundation`, `test_discovery`, `test_display` targets |
| **Config file** | None — CMake `enable_testing()` + `add_test()` pattern used in prior phases |
| **Quick run command** | `ctest -R test_airplay --output-on-failure` |
| **Full suite command** | `ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `ctest -R test_airplay --output-on-failure`
- **After every plan wave:** Run `ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 04-01-01 | 01 | 0 | AIRP-01 | unit | `ctest -R test_airplay -V` | ❌ W0 | ⬜ pending |
| 04-01-02 | 01 | 0 | AIRP-03 | unit | `ctest -R test_airplay -V` | ❌ W0 | ⬜ pending |
| 04-02-01 | 02 | 1 | AIRP-01 | unit | `ctest -R test_airplay -V` | ❌ W0 | ⬜ pending |
| 04-02-02 | 02 | 1 | AIRP-03 | unit | `ctest -R test_airplay -V` | ❌ W0 | ⬜ pending |
| 04-03-01 | 03 | 2 | AIRP-04 | unit (simulated) | `ctest -R test_airplay -V` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_airplay.cpp` — unit tests for AirPlayHandler (start/stop, mock callbacks, appsrc push)
- [ ] Mock `raop_t` or test harness that fires callbacks without a real iOS device
- [ ] `checkRequiredPlugins()` additions: `h264parse`, `avdec_aac`

*Existing CMake test pattern from prior phases applies — no new framework needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| iPhone/iPad mirroring to MyAirShow | AIRP-01 | Requires real iOS device and AirPlay protocol handshake | 1. Open Control Center on iPhone/iPad 2. Tap Screen Mirroring 3. Select MyAirShow 4. Verify screen appears on receiver within 3 seconds |
| macOS mirroring to MyAirShow | AIRP-02 | Requires real Mac and AirPlay connection | 1. Open System Settings > Displays 2. Select MyAirShow as AirPlay display 3. Verify desktop mirrors to receiver |
| 30-minute session stability | AIRP-04 | Duration testing with real device | 1. Start AirPlay mirror from iPhone 2. Leave running for 30 minutes 3. Verify no A/V drift and no connection drops |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
