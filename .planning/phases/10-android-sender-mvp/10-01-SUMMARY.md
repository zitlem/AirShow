---
phase: 10-android-sender-mvp
plan: 01
subsystem: ui
tags: [flutter, dart, bloc, cubit, mdns, multicast_dns, equatable, flutter_bloc, method_channel, event_channel]

requires:
  - phase: 09-receiver-protocol-foundation
    provides: AirShowHandler on port 7400 with _airshow._tcp mDNS advertisement and Flutter sender scaffold

provides:
  - DiscoveryCubit with idle->searching->found/timeout state machine backed by MdnsService
  - SessionCubit with idle->connecting->mirroring->stopping->idle lifecycle driven by native EventChannel events
  - AirShowChannel bridge defining MethodChannel('com.airshow/capture') + EventChannel('com.airshow/capture_events') contracts
  - ReceiverListScreen with discovered receiver list + manual IP entry fallback
  - MirroringScreen with Stop button and back-navigation on disconnect
  - 8 unit tests validating all state transition behaviors

affects: [10-android-sender-mvp/10-02, 11-ios-sender, 12-macos-sender, 13-windows-sender]

tech-stack:
  added:
    - flutter_bloc 9.1.1 (BLoC/Cubit state management)
    - bloc 9.2.0 (core BLoC library)
    - equatable 2.0.8 (value equality for state classes)
    - multicast_dns 0.3.3 (mDNS service discovery)
    - flutter_multicast_lock 1.2.0 (Android WiFi multicast lock for mDNS on physical devices)
    - flutter_foreground_task 9.2.2 (foreground service with mediaProjection type)
    - permission_handler 12.0.1 (runtime permission requests)
  patterns:
    - Sealed class hierarchy for BLoC states (DiscoveryState, SessionState)
    - Injectable dependencies for testability (MdnsService, AirShowChannel injected via constructor)
    - stream.take(N).toList() pattern for capturing N state emissions in tests
    - unawaited() for async cubit methods in tests where events drive state transitions
    - BlocConsumer for MirroringScreen (listener for navigation, builder for UI)
    - BlocListener wrapper in ReceiverListScreen for SessionConnecting navigation

key-files:
  created:
    - sender/lib/discovery/discovery_state.dart
    - sender/lib/discovery/discovery_cubit.dart
    - sender/lib/discovery/mdns_service.dart
    - sender/lib/session/session_state.dart
    - sender/lib/session/airshow_channel.dart
    - sender/lib/session/session_cubit.dart
    - sender/lib/app.dart
    - sender/lib/ui/receiver_list_screen.dart
    - sender/lib/ui/mirroring_screen.dart
    - sender/test/discovery_cubit_test.dart
    - sender/test/session_cubit_test.dart
  modified:
    - sender/pubspec.yaml (7 new dependencies added)
    - sender/lib/main.dart (now imports app.dart, 4 lines total)
    - sender/test/widget_test.dart (imports from app.dart, smoke test)

key-decisions:
  - "Test pattern: stream.take(N).toList() captures exactly N state emissions; avoids race with listen+cancel approach"
  - "unawaited() used in session tests when CONNECTED/ERROR events must be emitted after startMirroring() starts"
  - "MirroringScreen uses BlocConsumer: listener handles navigation back on SessionIdle, builder renders current state UI"
  - "ReceiverListScreen uses BlocListener wrapper to navigate to MirroringScreen on SessionConnecting (both manual connect and list tap)"

patterns-established:
  - "Pattern 1: BLoC stream testing — use stream.take(N).toList() before calling the method that emits states"
  - "Pattern 2: Sealed states with Equatable — each state class overrides props for BLoC equality checks"
  - "Pattern 3: Injectable service pattern — cubits receive service/channel in constructor, enabling mock injection in tests"
  - "Pattern 4: Native event bridge — SessionCubit subscribes to AirShowChannel.sessionEvents in constructor; cancels in close()"

requirements-completed: [DISC-06, DISC-07]

duration: 20min
completed: 2026-04-01
---

# Phase 10 Plan 01: Flutter Dart Layer Summary

**Flutter BLoC state machines for mDNS discovery + session lifecycle, AirShowChannel bridge with MethodChannel/EventChannel contracts, and two UI screens with 8 passing unit tests**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-04-01T16:08:00Z
- **Completed:** 2026-04-01T16:28:07Z
- **Tasks:** 2
- **Files modified:** 14

## Accomplishments

- Complete Dart-side BLoC architecture: DiscoveryCubit (idle->searching->found/timeout) + SessionCubit (idle->connecting->mirroring->stopping->idle)
- AirShowChannel defines the exact contract Plan 02 (Kotlin native plugin) must implement: MethodChannel for startCapture/stopCapture, EventChannel for CONNECTED/DISCONNECTED/ERROR events
- ReceiverListScreen with automatic mDNS discovery list + manual IP/port entry fallback for networks where mDNS is blocked
- MirroringScreen with live state display (connecting spinner, mirroring active, stop button) and automatic back-navigation on disconnect
- 8 unit tests covering all documented state transitions — tests validate logic without a device

## Task Commits

1. **Task 1: BLoC state machines, mDNS service, and channel bridge** - `2f1ec8b` (feat)
2. **Task 2: UI screens and app entry point wiring** - `44efbb1` (feat)

## Files Created/Modified

- `sender/pubspec.yaml` - Added 7 dependencies: flutter_bloc, bloc, equatable, multicast_dns, flutter_multicast_lock, flutter_foreground_task, permission_handler
- `sender/lib/discovery/discovery_state.dart` - ReceiverInfo value object + DiscoveryState sealed class hierarchy
- `sender/lib/discovery/mdns_service.dart` - MdnsService wrapping MDnsClient + FlutterMulticastLock for Android
- `sender/lib/discovery/discovery_cubit.dart` - DiscoveryCubit with startDiscovery() method
- `sender/lib/session/session_state.dart` - SessionState sealed class hierarchy
- `sender/lib/session/airshow_channel.dart` - MethodChannel + EventChannel bridge definition
- `sender/lib/session/session_cubit.dart` - SessionCubit with event subscription, startMirroring(), stopMirroring()
- `sender/lib/app.dart` - AirShowSenderApp with MultiBlocProvider and MaterialApp
- `sender/lib/main.dart` - Updated to import app.dart (4 lines)
- `sender/lib/ui/receiver_list_screen.dart` - Discovery-state-driven list + manual IP entry TextField
- `sender/lib/ui/mirroring_screen.dart` - Session-state-driven screen with Stop button
- `sender/test/discovery_cubit_test.dart` - 4 tests: initial state, found, timeout, exception
- `sender/test/session_cubit_test.dart` - 4 tests: initial state, CONNECTED event, stopMirroring, ERROR event
- `sender/test/widget_test.dart` - Updated smoke test importing from app.dart

## Decisions Made

- `stream.take(N).toList()` pattern for BLoC tests instead of `listen + cancel` — avoids race condition where second emit is missed before cancel
- `unawaited()` in session tests so the async `startMirroring` call doesn't block before the native event fires
- `MirroringScreen` uses `BlocConsumer` (not `BlocBuilder`) so navigation logic in `listener` doesn't depend on rebuild cycle
- `ReceiverListScreen` uses a `BlocListener` wrapper for session navigation so both list-tap and manual-connect paths go through the same navigation trigger

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed test race condition in stream.listen + cancel pattern**

- **Found during:** Task 1 (running tests)
- **Issue:** Tests using `listen` + `await cubit.startDiscovery()` + `cancel` only captured 1 state instead of 2. The second emit happened synchronously after `await` resolved but before the stream listener propagated it.
- **Fix:** Switched to `stream.take(2).toList()` pattern — futures that complete only after exactly N states have been emitted, eliminating the race
- **Files modified:** sender/test/discovery_cubit_test.dart, sender/test/session_cubit_test.dart
- **Verification:** All 8 tests pass
- **Committed in:** 2f1ec8b (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (Rule 1 - Bug)
**Impact on plan:** Test infrastructure fix only. No production code changed.

## Issues Encountered

- Initial test implementation used `listen` + `cancel` pattern which had a timing gap. Resolved by switching to `stream.take(N).toList()` — a more idiomatic and reliable Dart stream testing pattern.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 02 (Kotlin native plugin) can now implement against the exact AirShowChannel contract
- `MethodChannel('com.airshow/capture')` requires: `startCapture({host, port})` method, `stopCapture()` method
- `EventChannel('com.airshow/capture_events')` must emit maps: `{'type': 'CONNECTED'}`, `{'type': 'DISCONNECTED', 'reason': ...}`, `{'type': 'ERROR', 'message': ...}`
- Android foreground service with `FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION` required for Plan 02
- No blockers for Plan 02

---
*Phase: 10-android-sender-mvp*
*Completed: 2026-04-01*
