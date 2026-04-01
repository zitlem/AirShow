---
gsd_state_version: 1.0
milestone: v2.0
milestone_name: Companion Sender
status: executing
stopped_at: Completed 10-01-PLAN.md — Flutter Dart BLoC layer, discovery + session state machines, UI screens
last_updated: "2026-04-01T23:37:18.490Z"
last_activity: 2026-04-01
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 5
  completed_plans: 3
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-30)

**Core value:** Any device can mirror its screen to any computer, for free
**Current focus:** Phase 10 — android-sender-mvp

## Current Position

Phase: 10 (android-sender-mvp) — EXECUTING
Plan: 2 of 3
Status: Ready to execute
Last activity: 2026-04-01

Progress: [░░░░░░░░░░] 0% (v2.0 milestone)

## Performance Metrics

**Velocity (v1.0 reference):**

- Total plans completed (v1.0): 24
- Average duration: ~9 min/plan
- Total execution time: ~3.6 hours

**v2.0 — no plans completed yet**

## Accumulated Context

### Decisions

- v2.0 stack: Flutter 3.41.5 for sender app (only cross-platform option covering all 5 targets); receiver stack unchanged (C++17 + Qt 6.8 + GStreamer)
- Protocol transport: custom 16-byte binary TCP framing (type 1B + flags 1B + length 4B + PTS 8B); no third-party transport library
- Native-handles-media rule: Dart controls only session state; native plugin handles capture + encode + socket send (MethodChannel too slow for 30fps frame data)
- Phase order: Receiver first (hard dependency), then Android (highest value, most confident), iOS (dedicated phase — extension IPC is unique), macOS, Windows, web interface last
- mDNS package: `multicast_dns` 0.3.3 (flutter.dev) — covers all 5 platforms; `nsd` dropped for lacking Linux support
- Port 7400 for AirShow protocol; port 7401 for local web interface
- [Phase 09]: AirShow protocol: newline-terminated JSON handshake (HELLO/HELLO_ACK) followed by 16-byte binary frame streaming on TCP port 7400
- [Phase 09]: Echo-back quality negotiation in HELLO_ACK: receiver accepts sender-requested codec/resolution/bitrate/fps unchanged in v1; arbitration deferred to Phase 10
- [Phase 09]: AirShowHandler.cpp was missing from CMakeLists.txt airshow target — added in Plan 02 wiring
- [Phase 09]: Flutter 3.41.6 installed to ~/flutter via tarball (no sudo required); add to PATH for Phase 10+
- [Phase 10-android-sender-mvp]: stream.take(N).toList() pattern for BLoC stream testing avoids race condition with listen+cancel
- [Phase 10-android-sender-mvp]: AirShowChannel contract: MethodChannel('com.airshow/capture') + EventChannel('com.airshow/capture_events') — Plan 02 Kotlin must implement CONNECTED/DISCONNECTED/ERROR events

### Pending Todos

None yet.

### Blockers/Concerns

- Phase 11 (iOS): ReplayKit Broadcast Extension + direct outbound socket in Flutter context is sparsely documented — needs `/gsd:research-phase` before planning
- Phase 12 (macOS): TCC + code-signing identity interaction during CI/CD (GitHub Actions rebuilds may invalidate TCC permission) — needs signing decision before planning
- Port 7400 conflict: verify no conflict with SIP systems common on target networks before Phase 9 planning

## Session Continuity

Last session: 2026-04-01T23:37:18.487Z
Stopped at: Completed 10-01-PLAN.md — Flutter Dart BLoC layer, discovery + session state machines, UI screens
Resume file: None
