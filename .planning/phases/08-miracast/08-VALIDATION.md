---
phase: 8
slug: miracast
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-30
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | GTest |
| **Quick run command** | `cd build/linux-debug && ctest -R test_miracast --output-on-failure` |
| **Full suite command** | `cd build/linux-debug && ctest --output-on-failure` |
| **Estimated runtime** | ~5 seconds |

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Windows discovers MyAirShow in Connect menu | MIRA-01 | Requires Windows device | 1. Start MyAirShow 2. Win+K on Windows 3. See LVMS |
| Windows mirrors screen to receiver | MIRA-01 | Requires real Miracast session | 1. Select LVMS 2. Verify screen mirrors |
| Audio from Miracast source plays | MIRA-03 | Requires real media | 1. Play audio on Windows 2. Verify on receiver |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify
- [ ] Sampling continuity
- [ ] Wave 0 covers MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
