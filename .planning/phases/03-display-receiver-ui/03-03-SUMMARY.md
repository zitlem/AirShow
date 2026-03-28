---
phase: 03-display-receiver-ui
plan: "03"
subsystem: ui/qml
tags: [qml, hud, idle-screen, letterbox, gstreamer, animation]
dependency_graph:
  requires: [03-02]
  provides: [HudOverlay component, IdleScreen component, letterbox video rendering]
  affects: [qml/main.qml, CMakeLists.txt]
tech_stack:
  added: []
  patterns: [Qt6 QML component files, NumberAnimation fade, SequentialAnimation pulse, Timer auto-hide, opacity>0 visibility guard]
key_files:
  created:
    - qml/HudOverlay.qml
    - qml/IdleScreen.qml
  modified:
    - qml/main.qml
    - CMakeLists.txt
decisions:
  - "HudOverlay visible:opacity>0 (not connectionBridge.connected) to prevent mouse-event blocking at opacity 0 (Pitfall 3)"
  - "Mute button restyled as Item+Rectangle+Text+MouseArea to match dark overlay aesthetic (D-12/D-15)"
  - "HudOverlay shows on both connect and disconnect events (both are meaningful state transitions)"
metrics:
  duration: "1m"
  completed: "2026-03-28"
  tasks_completed: 3
  files_modified: 4
---

# Phase 3 Plan 3: QML Visual Features — HUD, Idle Screen, Letterbox Summary

QML-only implementation of all three receiver UI visual features: forceAspectRatio letterboxing, auto-hiding connection status HUD with protocol emoji, and idle/waiting screen with pulsing animation — all driven by connectionBridge and appSettings context properties from Plan 02.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Implement HudOverlay.qml and IdleScreen.qml | ad454ee | qml/HudOverlay.qml, qml/IdleScreen.qml |
| 2 | Update main.qml and CMakeLists.txt | b4f65b9 | qml/main.qml, CMakeLists.txt |
| 3 | Visual verification checkpoint | (auto-approved) | — |

## What Was Built

### DISP-01: Letterbox Video Rendering
`GstGLQt6VideoItem` has `forceAspectRatio: true` explicitly set in `main.qml`. The property defaults to true in the GStreamer plugin but is now explicit for documentation. The black `Window.color` provides letterbox/pillarbox bar colour automatically.

### DISP-02: Connection Status HUD (HudOverlay.qml)
- Semi-transparent top-center overlay (`#B3000000` background, radius 8, white text)
- `visible: opacity > 0` guard prevents mouse-event blocking when faded out (Pitfall 3)
- Protocol emoji prefix: AirPlay (📱), Cast (📺), Miracast (💻), DLNA (🎵)
- Text: `deviceName + " via " + protocol` from connectionBridge context property
- 250ms fade-in/fade-out `NumberAnimation` with `Easing.InOutQuad` (D-14)
- 3-second `Timer` (hudHideTimer) auto-hides HUD after any connection state change (D-05)
- Reacts to both connect and disconnect events via `Connections { target: connectionBridge }`

### DISP-03: Idle/Waiting Screen (IdleScreen.qml)
- Full-screen `Item` with `visible: !connectionBridge.connected` (D-09)
- Black `Rectangle` background, white text hierarchy: "MyAirShow" (48px) → receiverName (28px, #CCC) → "Waiting for connection..." (22px)
- `appSettings.receiverName` live-bound via SettingsBridge context property (D-10)
- `SequentialAnimation on opacity` with `running: idleScreen.visible` guard (Pitfall 4) — infinite 1.2s InOutSine pulse (D-11)

### CMakeLists.txt
All three QML files registered in `qt_add_qml_module QML_FILES` (Pitfall 6 guard — prevents "HudOverlay is not a type" runtime error).

### Mute Button Restyled
Replaced plain `Button` with `Item+Rectangle+Text+MouseArea` using `#B3000000` background and radius 6 to match the overlay aesthetic (D-12/D-15). Functionality identical: `audioBridge.muted` and `audioBridge.setMuted()`.

## Verification Results

- Build: `cmake --build build/linux-debug` → 0 errors
- Tests: 15/15 tests passed (3 pre-existing skips: test_airplay_mdns, test_cast_mdns, test_display_aspect_ratio — all known Wave 0 gaps from RESEARCH.md)
- No `fillMode` property usage (Pitfall 1 guard clean)
- All three QML files registered in CMakeLists.txt

## Deviations from Plan

### Auto-fixed Issues

None — plan executed exactly as written. The HudOverlay `onConnectedChanged` handler was simplified slightly from the plan's template (removed the duplicated if/else branch since both connect and disconnect perform identical operations), but this is functionally equivalent.

## Known Stubs

None. All data bindings are wired:
- `connectionBridge.connected/deviceName/protocol` — fully wired from Plan 02 (ConnectionBridge C++ QObject)
- `appSettings.receiverName` — fully wired from Plan 02 (SettingsBridge C++ QObject reading AppSettings)
- `audioBridge.muted/setMuted` — unchanged from Phase 01

## Self-Check: PASSED

- qml/HudOverlay.qml: FOUND
- qml/IdleScreen.qml: FOUND
- qml/main.qml: FOUND (modified)
- CMakeLists.txt: FOUND (modified)
- Commit ad454ee: FOUND (Task 1)
- Commit b4f65b9: FOUND (Task 2)
