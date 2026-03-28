# Phase 4: AirPlay - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 04-airplay
**Areas discussed:** UxPlay integration strategy, AirPlay authentication, Session lifecycle, A/V sync approach
**Mode:** --auto (all selections made automatically using recommended defaults)

---

## UxPlay Integration Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Embed UxPlay core as library | Extract lib/ into linkable target, submodule | ✓ |
| Fork and heavily modify | Full fork with deep changes to UxPlay codebase | |
| Run as subprocess | Launch UxPlay as separate process, IPC for frames | |

**User's choice:** [auto] Embed UxPlay core as library (recommended default)
**Notes:** Embedding gives direct access to decoded frames for appsrc injection. Matches STATE.md blocker note about "UxPlay lib/ subfolder embedding approach." Subprocess adds latency and complexity; heavy fork creates maintenance burden.

---

## AirPlay Authentication

| Option | Description | Selected |
|--------|-------------|----------|
| Use UxPlay's built-in FairPlay | Reuse existing SRP auth from UxPlay | ✓ |
| Custom FairPlay implementation | Re-implement from scratch | |
| Skip auth (development only) | No auth for testing, add later | |

**User's choice:** [auto] Use UxPlay's built-in FairPlay implementation (recommended default)
**Notes:** UxPlay already handles FairPlay SRP authentication for AirPlay 2. OpenSSL 3.x already linked. No reason to re-implement.

---

## Session Lifecycle

| Option | Description | Selected |
|--------|-------------|----------|
| Single-session with clean teardown | One session at a time, replace on new connect | ✓ |
| Queue-based sessions | Queue incoming, switch on disconnect | |
| Multi-session (concurrent) | Support multiple simultaneous mirrors | |

**User's choice:** [auto] Single-session model with clean teardown (recommended default)
**Notes:** V1 scope — one mirror at a time is standard for receivers like AirServer in basic mode. Multi-session is a v2 feature (ADV-01, ADV-02).

---

## A/V Sync Approach

| Option | Description | Selected |
|--------|-------------|----------|
| GStreamer RTP clock sync | Use rtpjitterbuffer + pipeline clock | ✓ |
| Custom NTP-based sync | Implement NTP time sync with sender | |
| Audio-follows-video adaptive | Dynamic sync correction based on drift detection | |

**User's choice:** [auto] GStreamer RTP clock synchronization (recommended default)
**Notes:** GStreamer's RTP infrastructure handles timestamp-based sync natively. UxPlay's output is already GStreamer-compatible. This is the proven approach.

---

## Claude's Discretion

- Exact UxPlay source files to extract vs exclude
- CMake integration for UxPlay as submodule library
- Internal threading model for RAOP server
- GStreamer element chain for appsrc injection
- Error handling and reconnection details

## Deferred Ideas

None — discussion stayed within phase scope
