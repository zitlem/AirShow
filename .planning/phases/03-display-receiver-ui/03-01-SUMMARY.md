---
phase: 03-display-receiver-ui
plan: 01
subsystem: ui-bridges
tags: [connection-bridge, settings-bridge, qobject, qml, tdd, interface-contract]
dependency_graph:
  requires: []
  provides:
    - ConnectionBridge.h — QObject bridge contract for connection state
    - SettingsBridge.h — QObject bridge contract for settings receiverName
    - tests/test_display.cpp — GTest stubs for DISP-01/02/03 (RED state)
    - tests/CMakeLists.txt — test_display target, Qt6::Core only
  affects:
    - src/ui/ReceiverWindow.cpp — will use setContextProperty("connectionBridge") and setContextProperty("appSettings")
    - Phase 3 Plan 02 — implements ConnectionBridge.cpp and SettingsBridge.cpp against these headers
tech_stack:
  added: []
  patterns:
    - QObject bridge pattern (Q_PROPERTY + Q_OBJECT + signals) matching AudioBridge.h
    - SettingsBridge takes AppSettings by reference (not owned) — same ownership model as AudioBridge/MediaPipeline
    - test_display links Qt6::Core only — no GStreamer dependency (mirrors test_discovery pattern)
key_files:
  created:
    - src/ui/ConnectionBridge.h
    - src/ui/SettingsBridge.h
    - tests/test_display.cpp
  modified:
    - tests/CMakeLists.txt
decisions:
  - ConnectionBridge.setConnected() declared in header only — .cpp implementation deferred to Plan 02 (expected RED link failure)
  - SettingsBridge reads receiverName at startup only; NOTIFY signal is forward-compatible hook for Phase 7 settings panel
  - test_display target links Qt6::Core only (no GStreamer) to stay consistent with test_discovery isolation pattern
metrics:
  duration: "3m"
  completed: "2026-03-28"
  tasks: 2
  files: 4
---

# Phase 3 Plan 01: Interface Contracts and Test Scaffold Summary

**One-liner:** ConnectionBridge and SettingsBridge QObject headers define the QML-to-C++ contract; test_display scaffold confirms RED state until Plan 02 supplies the .cpp implementations.

## What Was Built

Two header-only interface contracts (`ConnectionBridge.h`, `SettingsBridge.h`) following the existing `AudioBridge.h` canonical pattern, plus a GTest scaffold (`test_display.cpp`) covering DISP-01/02/03 requirements. The `test_display` CMake target is wired with Qt6::Core only — no GStreamer dependency.

### ConnectionBridge.h

- `Q_PROPERTY(bool connected)` / `Q_PROPERTY(QString deviceName)` / `Q_PROPERTY(QString protocol)` — QML binds directly
- `setConnected(bool, QString, QString)` — single mutation point Phase 4 protocol handlers will call
- Starts in idle state: `m_connected = false`, `m_deviceName = {}`, `m_protocol = {}`
- Inline getters in header; `setConnected()` declared only (implemented in Plan 02)

### SettingsBridge.h

- `Q_PROPERTY(QString receiverName)` — delegates to `m_settings.receiverName()`
- Constructor takes `AppSettings&` by reference (ownership stays in `ReceiverWindow`)
- `receiverNameChanged` signal is forward-compatible; Phase 7 will call it from the settings panel

### test_display.cpp

Five test stubs:
1. `test_display_aspect_ratio` — `GTEST_SKIP()` (visual only)
2. `test_connection_bridge_initial_state` — asserts `isConnected()==false`, empty strings
3. `test_connection_bridge_set_connected` — asserts state after `setConnected(true, "iPhone 15", "AirPlay")`
4. `test_connection_bridge_set_disconnected` — asserts state clears after `setConnected(false)`
5. `test_settings_bridge_receiver_name` — writes QSettings, constructs `AppSettings`, wraps in `SettingsBridge`, asserts `receiverName()`

### tests/CMakeLists.txt

Appended `test_display` target linking `GTest::gtest_main`, `GTest::gmock`, `Qt6::Core`. No `PkgConfig::GST` or GStreamer dependency.

## Commits

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | ConnectionBridge.h and SettingsBridge.h | f551b7c | src/ui/ConnectionBridge.h, src/ui/SettingsBridge.h |
| 2 | test_display scaffold and CMakeLists target | 05e21f2 | tests/test_display.cpp, tests/CMakeLists.txt |

## Deviations from Plan

None — plan executed exactly as written.

## Known Stubs

- `ConnectionBridge::setConnected()` is declared but has no implementation (`.cpp` does not exist yet). This is the intentional RED state — Plan 02 will implement it.
- `SettingsBridge` constructor and `receiverName()` similarly have no `.cpp`. Plan 02 resolves both.

These stubs do NOT block Plan 01's goal (interface contracts + test scaffold). They are the explicit design of this wave.

## Self-Check: PASSED

- [x] `src/ui/ConnectionBridge.h` exists with 3 Q_PROPERTYs and `setConnected()` signature
- [x] `src/ui/SettingsBridge.h` exists with 1 Q_PROPERTY and `AppSettings&` constructor
- [x] `tests/test_display.cpp` exists with 5 test stubs (62 lines)
- [x] `tests/CMakeLists.txt` contains `test_display` target with no GStreamer dependency
- [x] Commits f551b7c and 05e21f2 present in `git log`
