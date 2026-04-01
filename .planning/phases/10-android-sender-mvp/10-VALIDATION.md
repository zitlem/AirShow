---
phase: 10
slug: android-sender-mvp
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-31
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Flutter test (built-in) — `flutter_test` SDK package |
| **Config file** | `sender/analysis_options.yaml` (existing) |
| **Quick run command** | `cd sender && ~/flutter/bin/flutter test test/` |
| **Full suite command** | `cd sender && ~/flutter/bin/flutter test --coverage && ~/flutter/bin/flutter analyze` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `cd sender && ~/flutter/bin/flutter test test/`
- **After every plan wave:** Run `cd sender && ~/flutter/bin/flutter test --coverage && ~/flutter/bin/flutter analyze`
- **Before `/gsd:verify-work`:** Full suite must be green + manual device smoke test
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 10-01-01 | 01 | 0 | DISC-06 | unit | `flutter test test/discovery_cubit_test.dart` | ❌ W0 | ⬜ pending |
| 10-01-02 | 01 | 0 | DISC-07 | unit | `flutter test test/session_cubit_test.dart` | ❌ W0 | ⬜ pending |
| 10-02-xx | 02 | 1 | DISC-06 | unit | `flutter test test/discovery_cubit_test.dart` | ❌ W0 | ⬜ pending |
| 10-02-xx | 02 | 1 | DISC-07 | unit | `flutter test test/session_cubit_test.dart` | ❌ W0 | ⬜ pending |
| 10-03-xx | 03 | 2 | SEND-01 | manual | Run apk on device + observe receiver display | — | ⬜ pending |
| 10-03-xx | 03 | 2 | SEND-05 | manual | Run apk on device + listen on receiver speakers | — | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `sender/test/discovery_cubit_test.dart` — stubs for DISC-06 state machine
- [ ] `sender/test/session_cubit_test.dart` — stubs for DISC-07 + session lifecycle
- [ ] Android SDK installation: `~/android-sdk/cmdline-tools/latest/bin/sdkmanager`
- [ ] `sender/pubspec.yaml` — add all Phase 10 dependencies

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Screen appears on receiver | SEND-01 | MediaProjection requires real device | Run APK, tap receiver, observe display |
| Audio plays on receiver | SEND-05 | AudioPlaybackCapture requires real device | Run APK, play audio, listen on receiver |
| Stop button ends mirroring | SEND-01 | Notification action requires device | Tap Stop in notification, verify app returns to list |
| Manual IP entry connects | DISC-07 | Requires network and running receiver | Enter receiver IP, verify connection |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
