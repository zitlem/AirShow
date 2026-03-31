---
phase: 02-discovery-protocol-abstraction
plan: 02
subsystem: discovery
tags: [avahi, mdns, airplay, googlecast, linux, service-advertisement]
dependency_graph:
  requires: [02-01]
  provides: [avahi-advertiser, discovery-manager, airplay-mdns, cast-mdns]
  affects: [src/main.cpp, CMakeLists.txt]
tech_stack:
  added: [libavahi-client, libavahi-common]
  patterns: [AvahiThreadedPoll, factory-method, QSettings-persistence]
key_files:
  created:
    - src/discovery/AvahiAdvertiser.h
    - src/discovery/AvahiAdvertiser.cpp
    - src/discovery/ServiceAdvertiser.cpp
    - src/discovery/DiscoveryManager.h
    - src/discovery/DiscoveryManager.cpp
    - vendor/avahi/ (vendored headers + symlinks + .pc files)
  modified:
    - src/discovery/ServiceAdvertiser.h (added #include <cstdint>)
    - src/discovery/UpnpAdvertiser.h (fixed Upnp_FunPtr callback type)
    - src/discovery/UpnpAdvertiser.cpp (fixed callback parameter type)
    - CMakeLists.txt (added Avahi pkg_check_modules + source files)
    - CMakePresets.json (added PKG_CONFIG_PATH for vendored Avahi + libupnp)
    - src/main.cpp (instantiate AppSettings + DiscoveryManager before exec())
    - tests/test_discovery.cpp (promote test_receiver_name from SKIP to real test)
    - tests/CMakeLists.txt (link Qt6::Core + AppSettings.cpp into test_discovery)
decisions:
  - "Vendored Avahi dev headers under vendor/avahi/ using deb extraction — avahi-client-dev not installed system-wide and sudo unavailable"
  - "CMakePresets.json environment.PKG_CONFIG_PATH used to inject vendor paths — avoids cmake cache pollution and works cross-invocation"
  - "UpnpAdvertiser callback type fixed to Upnp_EventType_e — int was a type error from parallel agent leaving a build break"
metrics:
  duration: "~15 minutes"
  completed: "2026-03-28"
  tasks: 2
  files: 13
---

# Phase 02 Plan 02: Avahi mDNS Backend + DiscoveryManager Summary

Avahi Linux mDNS backend advertising _airplay._tcp, _raop._tcp, and _googlecast._tcp services using exact TXT record values from RESEARCH.md, wired into application startup so AirShow appears in AirPlay and Cast device pickers on the LAN.

## Tasks Completed

| # | Name | Commit | Key Files |
|---|------|--------|-----------|
| 1 | AvahiAdvertiser Linux mDNS backend | 4e49c52 | AvahiAdvertiser.h, AvahiAdvertiser.cpp, ServiceAdvertiser.cpp |
| 2 | DiscoveryManager + CMake + main.cpp wiring | 9b1551e | DiscoveryManager.h, DiscoveryManager.cpp, CMakeLists.txt, src/main.cpp, tests/ |

## What Was Built

**AvahiAdvertiser** (`src/discovery/AvahiAdvertiser.h/.cpp`):
- Implements `ServiceAdvertiser` interface using `AvahiThreadedPoll` (non-blocking, separate thread from Qt event loop)
- Thread-safe: all entry group operations performed under `avahi_threaded_poll_lock()`
- `AVAHI_ENTRY_GROUP_COLLISION` handled via `avahi_alternative_service_name()` — two AirShow instances on LAN will self-rename
- `AVAHI_CLIENT_FAILURE` logs actionable `g_critical` message: "Start avahi-daemon with: sudo systemctl start avahi-daemon"
- Multiple services accumulated before commit — single `avahi_entry_group_commit()` registers all records atomically

**ServiceAdvertiser::create()** (`src/discovery/ServiceAdvertiser.cpp`):
- Factory returning `AvahiAdvertiser` on Linux, `nullptr` (stub) on macOS/Windows

**DiscoveryManager** (`src/discovery/DiscoveryManager.h/.cpp`):
- Registers `_airplay._tcp` (port 7000), `_raop._tcp` (port 7000, name `<MAC>@<Name>`), `_googlecast._tcp` (port 8009)
- AirPlay TXT: `features=0x5A7FFFF7,0x1E`, `model=AppleTV3,2`, `srcvers=220.68`, 128-char zero placeholder `pk`, persistent `pi` UUID
- RAOP TXT: Full 15-field record matching UxPlay reference values
- Cast TXT: `id` as UUID without hyphens, `ve=02`, `fn=<ReceiverName>`, `ca=5`
- `readMacAddress()`: reads first non-loopback NIC via `SIOCGIFHWADDR` ioctl
- `getOrCreateUuid()`: stable identity across restarts via `QSettings`

**Application wiring** (`src/main.cpp`):
- `setOrganizationName("AirShow")` + `setApplicationName("AirShow")` before `QGuiApplication` construction
- `AppSettings` + `DiscoveryManager` instantiated before `app.exec()`
- Non-fatal `qWarning` if `discovery.start()` returns false (no crash on Linux without avahi-daemon)

**Tests** (`tests/test_discovery.cpp`):
- `test_receiver_name`: PASSED — tests default hostname, setReceiverName persistence, QSettings cleanup
- `test_airplay_mdns`, `test_cast_mdns`: SKIPPED — integration tests requiring real avahi-daemon + LAN device
- All other tests: 0 failures (pipeline tests still pass)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed missing `#include <cstdint>` in ServiceAdvertiser.h**
- **Found during:** Task 1 first build attempt
- **Issue:** `uint16_t` in `advertise()` signature was treated as `int` by GCC 15 without the include, causing `AvahiAdvertiser::advertise` to not match the base class signature and report as "does not override"
- **Fix:** Added `#include <cstdint>` to ServiceAdvertiser.h
- **Files modified:** `src/discovery/ServiceAdvertiser.h`
- **Commit:** 4e49c52

**2. [Rule 1 - Bug] Fixed UpnpAdvertiser callback type mismatch**
- **Found during:** Task 1 build attempt (pre-existing break from parallel agent 02-03)
- **Issue:** `upnpCallback` declared as `int(int, ...)` but `Upnp_FunPtr` requires `int(Upnp_EventType_e, ...)`
- **Fix:** Changed callback signature in both UpnpAdvertiser.h and UpnpAdvertiser.cpp to use `Upnp_EventType_e`
- **Files modified:** `src/discovery/UpnpAdvertiser.h`, `src/discovery/UpnpAdvertiser.cpp`
- **Commit:** 9b1551e

**3. [Rule 3 - Blocking] Vendored Avahi dev headers to unblock build**
- **Found during:** Task 1 setup — `libavahi-client-dev` not installed; `sudo` not available
- **Fix:** Downloaded `.deb` via `apt-get download`, extracted with `dpkg-deb -x`, created vendor directory with headers, symlinks to system `.so.3.x.x` files, and `.pc` files with corrected paths. Updated `CMakePresets.json` to inject `PKG_CONFIG_PATH` pointing to vendor directory.
- **Files modified:** `vendor/avahi/` (new), `CMakePresets.json`
- **Commit:** 4e49c52

## Build Verification

```
cmake --build build/linux-debug  # exit 0
ctest --test-dir build/linux-debug --output-on-failure
# 100% tests passed, 0 tests failed out of 10
# test_receiver_name: PASSED
# test_airplay_mdns: SKIPPED (requires LAN device)
# test_cast_mdns: SKIPPED (requires LAN device)
# test_dlna_ssdp: SKIPPED (Plan 03)
# test_firewall: SKIPPED (Windows-only)
```

## Known Stubs

None that block the plan goal. The following are intentional Phase 2 placeholders:

- `pk` TXT field in `_airplay._tcp` and `_raop._tcp`: 128 hex zeros — Phase 4 will replace with real FairPlay public key
- `ServiceAdvertiser::create()` returns `nullptr` on macOS/Windows — Phase 2 macOS/Windows port (future plan)
- `test_airplay_mdns` / `test_cast_mdns`: SKIPPED — verification requires real iOS/Android device on LAN (manual verification)

## Self-Check: PASSED

Created files verified:
- FOUND: src/discovery/AvahiAdvertiser.h
- FOUND: src/discovery/AvahiAdvertiser.cpp
- FOUND: src/discovery/ServiceAdvertiser.cpp
- FOUND: src/discovery/DiscoveryManager.h
- FOUND: src/discovery/DiscoveryManager.cpp

Commits verified:
- FOUND: 4e49c52 (feat(02-02): implement AvahiAdvertiser Linux mDNS backend)
- FOUND: 9b1551e (feat(02-02): add DiscoveryManager, wire into main.cpp, fill test_receiver_name)

Build: exit 0
Tests: 100% passed, 0 failed
