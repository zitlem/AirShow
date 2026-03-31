---
phase: 9
slug: receiver-protocol-foundation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-31
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | CTest + Qt Test (existing) |
| **Config file** | `CMakeLists.txt` (test targets) |
| **Quick run command** | `cd build && ctest --output-on-failure -R airshow` |
| **Full suite command** | `cd build && ctest --output-on-failure` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd build && ctest --output-on-failure -R airshow`
- **After every plan wave:** Run `cd build && ctest --output-on-failure`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 1 | RECV-01 | unit | `ctest -R airshow_handler` | ❌ W0 | ⬜ pending |
| 09-01-02 | 01 | 1 | RECV-01 | integration | `echo '{"type":"handshake","version":1}' | nc localhost 7400` | ❌ W0 | ⬜ pending |
| 09-02-01 | 02 | 1 | RECV-02 | integration | `avahi-browse -t _airshow._tcp` | ✅ | ⬜ pending |
| 09-03-01 | 03 | 2 | RECV-03 | unit | `ctest -R quality_negotiation` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_airshow_handler.cpp` — stubs for RECV-01 handshake and connection acceptance
- [ ] `tests/test_quality_negotiation.cpp` — stubs for RECV-03 negotiation fields
- [ ] Test targets added to CMakeLists.txt

*Existing Qt Test infrastructure covers framework setup.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| NAL units appear on receiver display | RECV-01 (SC-5) | Requires visual confirmation of video rendering | Connect sender, push H.264 NAL units, verify video appears in Qt window |
| Flutter analyze passes | RECV-01 (SC-4) | Requires Flutter SDK installed | `cd sender && flutter analyze` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
