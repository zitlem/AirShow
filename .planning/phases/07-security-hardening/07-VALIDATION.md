---
phase: 7
slug: security-hardening
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-30
---

# Phase 7 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GTest (already in project) |
| **Quick run command** | `cd build/linux-debug && ctest -R test_security --output-on-failure` |
| **Full suite command** | `cd build/linux-debug && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Approval dialog appears on new device connect | SEC-01 | Requires real protocol connection + GUI | 1. Start MyAirShow 2. Connect from phone 3. See dialog |
| PIN pairing blocks unauthorized device | SEC-02 | Requires real device PIN entry | 1. Enable PIN 2. Connect without PIN 3. Verify rejected |
| VPN traffic rejected | SEC-03 | Requires active VPN connection | 1. Start VPN 2. Attempt connect from VPN IP 3. Verify rejected |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
