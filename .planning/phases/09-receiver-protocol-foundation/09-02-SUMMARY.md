---
phase: 09-receiver-protocol-foundation
plan: 02
subsystem: protocol
tags: [airshow-protocol, mdns, avahi, flutter, sender-scaffold, tcp, gstreamer, windows-firewall]

# Dependency graph
requires:
  - phase: 09-01
    provides: AirShowHandler.h/cpp — TCP server on port 7400 with JSON handshake and binary framing
  - phase: 02-discovery
    provides: DiscoveryManager advertise pattern and ServiceAdvertiser interface
  - phase: 08-miracast
    provides: MiracastHandler wiring pattern (addHandler after setSecurityManager)
provides:
  - _airshow._tcp mDNS advertisement via DiscoveryManager (kAirShowPort=7400, ver/fn TXT records)
  - AirShowHandler wired into main.cpp ProtocolManager — starts alongside all other handlers
  - TCP 7400 added to Windows firewall rules (WindowsFirewall::registerRules)
  - Flutter sender project scaffold (sender/) targeting Android, iOS, macOS, Windows
  - sender/lib/main.dart placeholder screen with AirShowSenderApp widget
affects: [10-android-sender, 11-ios-sender, 12-macos-sender, 13-windows-sender, 14-web-interface]

# Tech tracking
tech-stack:
  added:
    - Flutter 3.41.6 (downloaded to ~/flutter — no snap/sudo required)
  patterns:
    - "AirShowHandler wiring: same pattern as MiracastHandler — make_unique, setSecurityManager, addHandler"
    - "Flutter project at repo root sender/ directory — platforms: android, ios, macos, windows"
    - "Port constant in DiscoveryManager.cpp + advertise block after existing protocol blocks"

key-files:
  created:
    - sender/pubspec.yaml
    - sender/lib/main.dart
    - sender/android/ (full platform scaffold)
    - sender/ios/ (full platform scaffold)
    - sender/macos/ (full platform scaffold)
    - sender/windows/ (full platform scaffold)
    - sender/test/widget_test.dart
  modified:
    - src/discovery/DiscoveryManager.cpp
    - src/main.cpp
    - src/platform/WindowsFirewall.cpp
    - CMakeLists.txt

key-decisions:
  - "AirShowHandler.cpp was missing from CMakeLists.txt airshow target — added (Rule 1 auto-fix)"
  - "Flutter installed to ~/flutter via direct tarball download (no sudo required)"
  - "Flutter 3.41.6 stable used (plan specified 3.41.5, 3.41.6 was the stable release available)"

patterns-established:
  - "AirShow mDNS TXT records: ver=1 (protocol version), fn=name (friendly name)"
  - "Flutter sender project lives at repo root sender/ — tracked in git, flutter .gitignore handles build artifacts"

requirements-completed: [RECV-01, RECV-02, RECV-03]

# Metrics
duration: 35min
completed: 2026-03-31
---

# Phase 09 Plan 02: AirShow Wiring and Flutter Sender Scaffold Summary

**AirShowHandler wired into main.cpp ProtocolManager, _airshow._tcp advertised via mDNS on port 7400, Windows firewall port 7400 added, and Flutter 3.41.6 sender project scaffold created for Android/iOS/macOS/Windows**

## Performance

- **Duration:** ~35 min
- **Started:** 2026-03-31T16:10:00Z
- **Completed:** 2026-03-31T16:45:00Z
- **Tasks:** 2 auto (+ 1 auto-approved checkpoint)
- **Files modified:** 4 C++ files + 113 Flutter scaffold files

## Accomplishments

- _airshow._tcp mDNS advertisement live: port 7400, TXT records `ver=1` and `fn=<receiverName>`
- AirShowHandler instantiated in main.cpp following the MiracastHandler wiring pattern
- Windows firewall rule "AirShow Protocol" TCP 7400 added to registerRules()
- Firewall error message in main.cpp updated to include TCP 7400 (AirShow)
- Flutter 3.41.6 project created at sender/ with Android, iOS, macOS, Windows platform targets
- sender/lib/main.dart shows AirShow Sender placeholder screen (counter app replaced)
- flutter analyze exits 0 with no issues

## Task Commits

Each task was committed atomically:

1. **Task 1: Add _airshow._tcp mDNS advertisement and wire AirShowHandler in main.cpp** - `81ef537` (feat)
2. **Task 2: Create Flutter sender project scaffold** - `c347588` (feat)
3. **Task 3: Verify end-to-end AirShow protocol flow** - auto-approved (checkpoint:human-verify, auto_advance=true)

## Files Created/Modified

- `src/discovery/DiscoveryManager.cpp` - Added kAirShowPort=7400 constant and _airshow._tcp advertisement block
- `src/main.cpp` - Added AirShowHandler.h include and handler wiring block after MiracastHandler
- `src/platform/WindowsFirewall.cpp` - Added "AirShow Protocol" TCP 7400 firewall rule
- `CMakeLists.txt` - Added src/protocol/AirShowHandler.cpp to airshow target (auto-fix)
- `sender/pubspec.yaml` - Flutter project manifest (name: airshow_sender, org: com.airshow)
- `sender/lib/main.dart` - AirShowSenderApp with placeholder screen
- `sender/test/widget_test.dart` - Updated test to reference AirShowSenderApp
- `sender/android/` - Full Android platform scaffold
- `sender/ios/` - Full iOS platform scaffold
- `sender/macos/` - Full macOS platform scaffold
- `sender/windows/` - Full Windows platform scaffold

## Decisions Made

- **Flutter version:** 3.41.6 stable used instead of 3.41.5 from plan — 3.41.6 was the current stable release. Minor patch difference, no behavioral difference.
- **Flutter install method:** Direct tarball download to ~/flutter (no sudo) — snap install required authentication which was not available.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] AirShowHandler.cpp missing from CMakeLists.txt airshow target**
- **Found during:** Task 1 (build verification)
- **Issue:** AirShowHandler was created in Plan 01 but only added to the test target's CMakeLists.txt, not the main airshow executable target. Linker error: undefined reference to `AirShowHandler::AirShowHandler` and `setSecurityManager`.
- **Fix:** Added `src/protocol/AirShowHandler.cpp` to the `qt_add_executable(airshow ...)` source list in CMakeLists.txt
- **Files modified:** CMakeLists.txt
- **Verification:** Rebuild succeeded; airshow binary linked correctly
- **Committed in:** 81ef537 (Task 1 commit)

**2. [Rule 1 - Bug] widget_test.dart referenced removed MyApp class**
- **Found during:** Task 2 (flutter analyze)
- **Issue:** Default Flutter scaffold test referenced `MyApp` which was replaced by `AirShowSenderApp` in main.dart. flutter analyze reported: `The name 'MyApp' isn't a class`
- **Fix:** Updated test/widget_test.dart to use `AirShowSenderApp` and updated test assertions to match placeholder text
- **Files modified:** sender/test/widget_test.dart
- **Verification:** flutter analyze exits 0 with no issues
- **Committed in:** c347588 (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (Rule 1 - linking bug, Rule 1 - test class reference)
**Impact on plan:** Both fixes required for build to succeed and flutter analyze to pass. No scope creep.

## Issues Encountered

- Flutter SDK not installed as snap (requires sudo). Resolved by downloading Flutter 3.41.6 tarball directly to ~/flutter — no root access needed. This is a standard Flutter installation method.

## Known Stubs

- `sender/lib/main.dart` displays "Discovery & mirroring coming soon" — intentional placeholder. Phase 10 (Android sender) will replace with real mDNS discovery and screen capture. This does not block this plan's goal (scaffold creation).

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- AirShowHandler starts on application launch alongside AirPlay, Cast, Miracast, and DLNA handlers
- _airshow._tcp advertised: `avahi-browse -t _airshow._tcp` will show the receiver on port 7400
- Flutter sender scaffold ready for Phase 10 (Android sender): add multicast_dns for discovery, MethodChannel for screen capture
- Flutter SDK at ~/flutter — Phase 10 execution agent should add to PATH: `export PATH="$PATH:/home/sanya/flutter/bin"`
- Pre-existing test failure: `PipelineTest.test_video_pipeline` — NOT caused by this plan (confirmed pre-existing via git stash check)

---
*Phase: 09-receiver-protocol-foundation*
*Completed: 2026-03-31*
