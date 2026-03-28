---
phase: 02-discovery-protocol-abstraction
plan: 01
subsystem: protocol-discovery-settings
tags: [interfaces, tdd, wave-0, protocol-abstraction, service-discovery, settings]
dependency_graph:
  requires: []
  provides:
    - src/protocol/ProtocolHandler.h
    - src/protocol/ProtocolManager.h
    - src/protocol/ProtocolManager.cpp
    - src/discovery/ServiceAdvertiser.h
    - src/settings/AppSettings.h
    - src/settings/AppSettings.cpp
    - tests/test_discovery.cpp
  affects:
    - Plans 02-02 and 02-03 (execute against these contracts)
    - Phase 4+ (ProtocolHandler implementations)
tech_stack:
  added: []
  patterns:
    - Pure abstract C++ interface pattern for ProtocolHandler (virtual = 0)
    - Factory method pattern for ServiceAdvertiser::create() cross-platform backend
    - GTEST_SKIP Wave 0 stub pattern for tests without hardware dependencies
key_files:
  created:
    - src/protocol/ProtocolHandler.h
    - src/protocol/ProtocolManager.h
    - src/protocol/ProtocolManager.cpp
    - src/discovery/ServiceAdvertiser.h
    - src/settings/AppSettings.h
    - src/settings/AppSettings.cpp
    - tests/test_discovery.cpp
  modified:
    - tests/CMakeLists.txt
    - CMakeLists.txt
decisions:
  - "ProtocolHandler uses pure virtual interface (no base state) — AirPlay/Cast/DLNA handlers own all protocol-specific state"
  - "ServiceAdvertiser::create() factory defers platform selection to Plan 02 (AvahiAdvertiser) — no #ifdefs in the header"
  - "AppSettings uses QSettings default constructor (NativeFormat) — no explicit path needed"
  - "ProtocolManager.cpp uses glib.h g_warning (not QDebug) for consistency with MediaPipeline.cpp pattern"
  - "test_discovery target has no GStreamer dependency — discovery phase does not touch GStreamer"
metrics:
  duration: 2m
  completed_date: 2026-03-28
  tasks_completed: 2
  files_changed: 9
---

# Phase 02 Plan 01: Wave 0 Test Scaffold + Protocol/Discovery/Settings Interface Contracts Summary

**One-liner:** C++ pure-virtual interfaces for ProtocolHandler, ProtocolManager, ServiceAdvertiser, and AppSettings with Wave 0 GTEST_SKIP stubs enabling parallel execution of Plans 02 and 03.

---

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Wave 0 test scaffold for discovery | b71c04f | tests/test_discovery.cpp, tests/CMakeLists.txt |
| 2 | Protocol and discovery interface contracts | 643a605 | src/protocol/ProtocolHandler.h, ProtocolManager.h/.cpp, src/discovery/ServiceAdvertiser.h, src/settings/AppSettings.h/.cpp, CMakeLists.txt |

---

## What Was Built

### Task 1: Wave 0 Test Scaffold

Created `tests/test_discovery.cpp` with 5 `GTEST_SKIP` stubs in a `DiscoveryTest` fixture. Added a new `test_discovery` CMake target with no GStreamer dependency (discovery classes do not touch GStreamer in Phase 2). All 5 tests run as SKIPPED — exercisable immediately with ctest -R DiscoveryTest.

Tests created:
- `test_airplay_mdns` — skipped: "Avahi backend not yet implemented — Plan 02"
- `test_cast_mdns` — skipped: "Avahi backend not yet implemented — Plan 02"
- `test_dlna_ssdp` — skipped: "UpnpAdvertiser not yet implemented — Plan 03"
- `test_receiver_name` — skipped: "AppSettings not yet wired — Plan 02"
- `test_firewall` — skipped: "WindowsFirewall is Windows-only — Plan 03"

### Task 2: Interface Contracts

**ProtocolHandler.h** (D-06): Pure abstract base class with `start()`, `stop()`, `name()`, `isRunning()`, `setMediaPipeline(MediaPipeline*)`. Forward-declares `MediaPipeline` — no MediaPipeline.h include in the header.

**ProtocolManager.h/.cpp** (D-08): Owns a `vector<unique_ptr<ProtocolHandler>>`. On `addHandler()` calls `setMediaPipeline()` immediately. `startAll()` returns false if any handler fails and logs via `g_warning`. `stopAll()` only calls `stop()` on running handlers (guards double-stop). Destructor calls `stopAll()` for RAII cleanup.

**ServiceAdvertiser.h**: Abstract mDNS/DNS-SD interface with `advertise()`, `rename()`, `stop()`. Includes a `TxtRecord` struct and a `static create()` factory that will return the correct backend per platform (implemented in Plan 02).

**AppSettings.h/.cpp** (D-09, D-10): QSettings wrapper using NativeFormat. `receiverName()` returns stored name or `QSysInfo::machineHostName()` as default. `isFirstLaunch()` / `setFirstLaunchComplete()` support the Windows firewall first-launch path (D-12).

---

## Verification Results

```
cmake --build build/linux-debug   → Success (no errors)
ctest --output-on-failure         → 100% passed, 0 failed out of 10
  PipelineTest.*                  → 4 PASSED + 1 PASSED (smoke)
  DiscoveryTest.*                 → 5 SKIPPED
```

---

## Deviations from Plan

None — plan executed exactly as written.

---

## Known Stubs

The following stubs are intentional and tracked for future plans:

| File | Stub | Reason | Resolved in |
|------|------|--------|-------------|
| src/discovery/ServiceAdvertiser.h | `static create()` declared but not implemented | Factory body requires AvahiAdvertiser which is Plan 02 | Plan 02-02 |
| tests/test_discovery.cpp | All 5 tests are GTEST_SKIP | Avahi/UPnP implementations don't exist yet | Plans 02-02, 02-03 |

These stubs do not prevent the plan's goal — the goal was to define contracts and create the test scaffold. The stubs are the expected output.

---

## Self-Check: PASSED
