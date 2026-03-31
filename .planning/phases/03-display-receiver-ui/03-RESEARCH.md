# Phase 3: Display & Receiver UI - Research

**Researched:** 2026-03-28
**Domain:** Qt6 QML UI — aspect-ratio video rendering, animated overlays, C++ context property bridges
**Confidence:** HIGH

## Summary

Phase 3 adds no protocol logic. It transforms the minimal `main.qml` (a bare GstGLQt6VideoItem with a mute button) into a polished receiver experience: correct aspect-ratio display, a connection-status HUD that auto-hides, and an idle/waiting screen with a subtle pulse animation.

`GstGLQt6VideoItem` exposes a `forceAspectRatio` boolean property (default `true`) that internally calls `gst_video_sink_center_rect()` to calculate letterbox/pillarbox bars within the item's bounds. Because the property defaults to `true` and the root window background is already `"black"`, letterboxing is essentially free — the video just needs to fill the parent (current `anchors.fill: parent`) and `forceAspectRatio` left at its default. No wrapper container is required. This is the letterboxing mechanism for DISP-01.

The HUD overlay (DISP-02) and idle screen (DISP-03) are purely QML: `Item` overlays using `visible`/`opacity` driven by a `connectionBridge` context property (a new `QObject` modelled after the existing `AudioBridge` pattern). The C++ side emits `NOTIFY` signals when state changes; QML binds to them declaratively. Animations use `NumberAnimation` on `opacity` (200-300 ms, `Easing.InOutQuad`) for fade, and a `SequentialAnimation` with `loops: Animation.Infinite` for the idle pulse. A `Timer` (3 s, single-shot, restarted on each state change) triggers HUD hide. All three features can live in `main.qml` or be split into focused component files — the CMake module already supports listing multiple `QML_FILES`.

**Primary recommendation:** Add `ConnectionBridge` C++ QObject, expose it as context property `connectionBridge`, then implement all three visual elements entirely in QML using the established AudioBridge pattern.

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Letterbox with black bars — preserve the source aspect ratio, never stretch or crop
- **D-02:** Implement letterboxing by setting the item's `fillMode` to preserve aspect ratio (or wrapping in a container that centres it with black margins)
- **D-03:** The window background remains black (`color: "black"` already set in main.qml), providing natural letterbox bars
- **D-04:** Semi-transparent overlay showing device name and protocol in use (e.g., "iPhone 15 via AirPlay")
- **D-05:** HUD auto-hides after 3 seconds of no state change, reappears on new connection or disconnection
- **D-06:** Position at the top of the screen, horizontally centred, with subtle fade-in/fade-out animation
- **D-07:** Show protocol icon (small) next to device name — use simple text/emoji indicators, not image assets
- **D-08:** Dark background (black) with app name "AirShow", receiver name (from AppSettings), and "Waiting for connection..." text
- **D-09:** Idle screen is visible when no mirroring session is active. Hides when content starts, reappears when session ends
- **D-10:** Receiver name updates live if changed in settings (bind to AppSettings.receiverName)
- **D-11:** Minimal animation — subtle opacity pulse on the "Waiting..." text to show the app is alive
- **D-12:** Dark/translucent overlays (semi-transparent black background, ~70% opacity) with white text
- **D-13:** Font: system default sans-serif, clean and readable at TV-viewing distance
- **D-14:** Subtle opacity/fade animations (200-300 ms duration), no bouncing or sliding
- **D-15:** No heavy theming or custom design system — keep it minimal and TV-receiver-like

### Claude's Discretion
- Exact QML component structure and file organisation
- Whether to split into multiple QML files or keep in main.qml
- Exact animation timing and easing curves
- Whether mute button styling needs updating to match new visual style

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DISP-01 | Mirrored content displays fullscreen with correct aspect ratio (letterboxed if needed) | `GstGLQt6VideoItem.forceAspectRatio` defaults to `true`; black window background provides bars |
| DISP-02 | Application shows connection status (waiting/connected/device name/protocol) | `ConnectionBridge` QObject context property; QML `Text` overlay with `Timer`-driven auto-hide |
| DISP-03 | Application shows an idle/waiting screen when no device is connected | Same `ConnectionBridge.connected` bool; idle `Item` overlay with `SequentialAnimation` pulse |
</phase_requirements>

---

## Project Constraints (from CLAUDE.md)

- C++17 + Qt 6.8 LTS + GStreamer 1.26.x — no version changes
- Must remain cross-platform: Linux, macOS, Windows
- No commercial libraries, no internet dependencies
- Established integration pattern: `setContextProperty` + `Q_PROPERTY` + `NOTIFY` (AudioBridge pattern)
- GSD workflow enforcement: all edits via `/gsd:execute-phase`
- `pkg-config` for GStreamer detection (not `FindGStreamer.cmake`)

---

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt6 Quick | 6.8 LTS | QML scene graph, animations, Item hierarchy | Already linked; all UI lives here |
| Qt6 Qml | 6.8 LTS | QQmlContext, context properties, engine | Already in ReceiverWindow.cpp |
| GStreamer qml6glsink | 1.26.x | Renders video into QML scene via GL | Already used (videoItem) |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Qt6 Quick Controls | 6.8 LTS | `Button` (mute button already uses it) | Already imported in main.qml |

No new library dependencies are required for this phase.

**Version verification:** No new packages. All dependencies already linked in `CMakeLists.txt`.

---

## Architecture Patterns

### Recommended Project Structure
```
qml/
├── main.qml             # Root Window — modified primary target
├── HudOverlay.qml       # (discretionary) connection status overlay component
└── IdleScreen.qml       # (discretionary) waiting screen component
src/ui/
├── ConnectionBridge.h   # New QObject — mirrors AudioBridge pattern
├── ConnectionBridge.cpp
├── ReceiverWindow.h     # Modified: expose connectionBridge context property
├── ReceiverWindow.cpp
├── AudioBridge.h        # Unchanged
└── AudioBridge.cpp      # Unchanged
```

Splitting into component files is the Qt-recommended approach for maintainability. Each `.qml` file is auto-registered as a component by its capital-first filename within the same `qt_add_qml_module`. The `CMakeLists.txt` `QML_FILES` list must be updated to include them.

### Pattern 1: ConnectionBridge QObject (mirrors AudioBridge)
**What:** A `QObject` with `Q_PROPERTY` fields for connection state, exposed to QML as a context property named `connectionBridge`.
**When to use:** Any time C++ protocol logic (Phase 4+) needs to push state to the QML layer. Phase 3 stubs the bridge with static/hardcoded values; real protocol integration comes in later phases.

```cpp
// Source: mirrors src/ui/AudioBridge.h pattern
class ConnectionBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected    READ isConnected  NOTIFY connectedChanged)
    Q_PROPERTY(QString deviceName READ deviceName  NOTIFY deviceNameChanged)
    Q_PROPERTY(QString protocol  READ protocol     NOTIFY protocolChanged)
public:
    explicit ConnectionBridge(QObject* parent = nullptr);
    bool    isConnected() const  { return m_connected; }
    QString deviceName()  const  { return m_deviceName; }
    QString protocol()    const  { return m_protocol; }
    // Phase 4 calls these to push new state:
    void setConnected(bool c, const QString& device = {}, const QString& proto = {});
signals:
    void connectedChanged(bool);
    void deviceNameChanged(const QString&);
    void protocolChanged(const QString&);
private:
    bool    m_connected  = false;
    QString m_deviceName;
    QString m_protocol;
};
```

Expose in `ReceiverWindow::load()`:
```cpp
// Source: mirrors ReceiverWindow.cpp audioBridge pattern
auto* connBridge = new ConnectionBridge(&m_engine);
m_engine.rootContext()->setContextProperty("connectionBridge", connBridge);
```

### Pattern 2: Aspect Ratio — forceAspectRatio (GstGLQt6VideoItem)
**What:** `GstGLQt6VideoItem` has a built-in `forceAspectRatio` bool property (default `true`) that calls `gst_video_sink_center_rect()` internally, producing letterbox/pillarbox bars within the item's allocated rect.
**When to use:** Always. Default is correct. The item fills the window (`anchors.fill: parent`); the black `Window.color` provides the bar colour. No extra wrapper needed.

> Note: D-02 in CONTEXT.md mentions "setting the item's `fillMode`" — this is the Qt Multimedia `VideoOutput` property. `GstGLQt6VideoItem` does NOT have `fillMode`. The equivalent is `forceAspectRatio`. The decision intent is satisfied by `forceAspectRatio: true` (already the default).

```qml
// Source: GStreamer gstreamer/subprojects/gst-plugins-good/ext/qt6/qt6glitem.h
GstGLQt6VideoItem {
    id: videoItem
    objectName: "videoItem"
    anchors.fill: parent
    forceAspectRatio: true   // default; explicit for clarity
}
```

### Pattern 3: HUD Overlay with Auto-Hide Timer
**What:** An `Item` anchored to the top-centre of the window. A `Timer` (3 s, single-shot) starts when connection state changes; on `triggered` it fades the HUD out with `NumberAnimation`. State changes restart the timer and fade in.

```qml
// Source: Qt documentation — NumberAnimation, Timer, opacity binding
Item {
    id: hudOverlay
    visible: connectionBridge.connected
    opacity: 0
    anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 20 }

    // Protocol emoji prefix + device name text (D-07)
    Text {
        text: {
            var icon = connectionBridge.protocol === "AirPlay"  ? "📱 " :
                       connectionBridge.protocol === "Cast"     ? "🎭 " :
                       connectionBridge.protocol === "Miracast" ? "💻 " : ""
            return icon + connectionBridge.deviceName + " via " + connectionBridge.protocol
        }
        color: "white"
        font.pixelSize: 22
        font.family: "sans-serif"
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -10
        color: "#B3000000"   // ~70% opaque black (D-12)
        radius: 8
        z: -1
    }

    NumberAnimation {
        id: fadeIn
        target: hudOverlay; property: "opacity"
        to: 1.0; duration: 250; easing.type: Easing.InOutQuad
    }
    NumberAnimation {
        id: fadeOut
        target: hudOverlay; property: "opacity"
        to: 0.0; duration: 250; easing.type: Easing.InOutQuad
    }

    Timer {
        id: hudHideTimer
        interval: 3000; repeat: false
        onTriggered: fadeOut.start()
    }

    Connections {
        target: connectionBridge
        function onConnectedChanged(c) {
            if (c) {
                hudOverlay.opacity = 0
                fadeIn.start()
                hudHideTimer.restart()
            }
        }
    }
}
```

### Pattern 4: Idle Screen with Opacity Pulse
**What:** A full-window overlay visible when `!connectionBridge.connected`. Contains app name, receiver name bound to `AppSettings`, and a "Waiting for connection..." text with infinite opacity pulse.

```qml
// Source: Qt documentation — SequentialAnimation, loops: Animation.Infinite
Item {
    id: idleScreen
    anchors.fill: parent
    visible: !connectionBridge.connected

    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            text: "AirShow"
            color: "white"
            font.pixelSize: 48
            font.family: "sans-serif"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: appSettings.receiverName   // live binding to AppSettings (D-10)
            color: "#CCCCCC"
            font.pixelSize: 28
            font.family: "sans-serif"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            id: waitingText
            text: "Waiting for connection..."
            color: "white"
            font.pixelSize: 22
            font.family: "sans-serif"
            horizontalAlignment: Text.AlignHCenter
            anchors.horizontalCenter: parent.horizontalCenter

            SequentialAnimation on opacity {
                running: idleScreen.visible
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 1200; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 1200; easing.type: Easing.InOutSine }
            }
        }
    }
}
```

Receiver name binding requires `AppSettings` to be exposed as a context property. It currently is not — `AppSettings` is a plain C++ class (not a `QObject`). Two options:
1. Wrap `AppSettings` in a new `QObject` bridge `SettingsBridge` (with `Q_PROPERTY receiverName` and `NOTIFY nameChanged`).
2. Expose `receiverName` as a string via `setContextProperty("receiverName", ...)` and update it manually on change.

**Recommendation:** Add a `SettingsBridge` QObject following the same bridge pattern. This is the only clean way to get live `nameChanged` → QML binding. Alternatively, add `receiverName` as a context property and call `setContextProperty` again when the name changes (less idiomatic but simpler for Phase 3).

### Pattern 5: Multiple QML Files in qt_add_qml_module
**What:** Add component `.qml` files to the same `qt_add_qml_module` call so the build system auto-registers them.

```cmake
# Source: Qt6 official docs — qt_add_qml_module QML_FILES
qt_add_qml_module(airshow
  URI AirShow
  VERSION 1.0
  QML_FILES
    qml/main.qml
    qml/HudOverlay.qml
    qml/IdleScreen.qml
)
```

Component files must start with a capital letter to be used as QML components by name without an explicit import.

### Anti-Patterns to Avoid
- **Using `fillMode` on GstGLQt6VideoItem:** This property does not exist on `GstGLQt6VideoItem`. It exists on Qt Multimedia's `VideoOutput`. Use `forceAspectRatio: true` instead.
- **Calling `setContextProperty` after `engine.load()`:** Context properties must be set before `engine.load()` or the initial bindings will not resolve. If state can change after load, emit NOTIFY signals — do not call `setContextProperty` again for value changes.
- **Animating `visible` directly:** `visible` is a bool, not animatable. Animate `opacity` instead, and set `visible: opacity > 0` or manage visibility separately so the overlay does not intercept mouse events when invisible.
- **`loops: Animation.Infinite` running when overlay is hidden:** Wastes CPU/GPU. Use `running: idleScreen.visible` to stop the animation when the idle screen is hidden.
- **Inline all UI in main.qml without separation:** For this phase (3 distinct visual concerns), splitting into components makes each concern testable/readable independently.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Aspect ratio letterboxing | Custom calculation of content rect with black bars | `forceAspectRatio: true` on GstGLQt6VideoItem | Built into the GStreamer Qt6 plugin; handles all source resolutions correctly |
| Fade-in/out animations | `Timer` + manual opacity steps | `NumberAnimation` on `opacity` | Qt scene graph GPU-composited; correct easing; one-liner |
| Infinite pulse animation | Manual `Timer` toggling opacity | `SequentialAnimation { loops: Animation.Infinite }` | Declarative, zero CPU overhead, restarts cleanly |
| Auto-hide timer reset | `QTimer` in C++ pushing state | QML `Timer` with `restart()` | Keep display logic in QML; C++ bridge only pushes connection events |
| Receiver name live update | Polling AppSettings on a timer | `Q_PROPERTY receiverName` with `NOTIFY nameChanged` on a bridge QObject | Reactive binding; no polling; Qt property system guarantees notification |

**Key insight:** Qt Quick's declarative binding system means display state should live in QML properties, not in C++ logic. The C++ bridge emits signals; QML reacts. Never push UI state by calling `setContextProperty` after load.

---

## Common Pitfalls

### Pitfall 1: fillMode Does Not Exist on GstGLQt6VideoItem
**What goes wrong:** Developer sets `fillMode: VideoOutput.PreserveAspectFit` — QML logs a binding error, property is silently ignored, video stretches to fill the item.
**Why it happens:** Qt Multimedia's `VideoOutput` has `fillMode`; GStreamer's `GstGLQt6VideoItem` does not.
**How to avoid:** Use `forceAspectRatio: true` (the default). Confirm the property name against the GStreamer source: `qt6glitem.h` exposes `forceAspectRatio`, `acceptEvents`, `itemInitialized` — no `fillMode`.
**Warning signs:** QML console warning "Cannot assign to non-existent property 'fillMode'".

### Pitfall 2: Context Properties Set After engine.load() Are Ignored for Initial Bindings
**What goes wrong:** QML uses `connectionBridge.connected` in a binding but the binding evaluates before the property is set, leaving the UI in the wrong initial state.
**Why it happens:** `QQmlContext` evaluates bindings at component creation time (during `engine.load()`). Properties set after load do not trigger initial binding resolution.
**How to avoid:** Call all `setContextProperty(...)` calls before `m_engine.load(...)`. This is the existing pattern in `ReceiverWindow.cpp` (see audioBridge).
**Warning signs:** Idle screen not visible on startup even though `connected = false`.

### Pitfall 3: Animated Overlay Blocking Mouse/Keyboard Input When Invisible
**What goes wrong:** Invisible overlay (`opacity: 0`) still catches mouse events, blocking the mute button beneath it.
**Why it happens:** `opacity: 0` does not remove an item from event handling. Only `visible: false` does.
**How to avoid:** Set `visible: opacity > 0` (or `visible: false` after fade-out completes). Can use `onStopped` of `fadeOut` animation: `hudOverlay.visible = false`. Alternatively set `enabled: false` when opacity is 0.
**Warning signs:** Mute button click stops working when HUD fades out.

### Pitfall 4: SequentialAnimation Continues Running When Item Is Hidden
**What goes wrong:** Idle screen pulse animation keeps running after a connection is established (item becomes invisible but animation was not stopped), wasting GPU/CPU.
**Why it happens:** `loops: Animation.Infinite` has no automatic stop condition.
**How to avoid:** Add `running: idleScreen.visible` to the `SequentialAnimation`. The animation stops when `visible` becomes `false` and restarts when it becomes `true` again.
**Warning signs:** Higher than expected CPU usage during active mirroring.

### Pitfall 5: AppSettings Is Not a QObject — Cannot Bind Directly
**What goes wrong:** Developer tries to expose `AppSettings` directly as a context property; QML cannot bind to its methods because it lacks `Q_PROPERTY`/`NOTIFY`.
**Why it happens:** `AppSettings` is a plain C++ class with no Qt object model. QML can only bind to `Q_PROPERTY` on `QObject`-derived classes.
**How to avoid:** Create a thin `SettingsBridge : QObject` that wraps the relevant properties. Or pass `receiverName` as a plain string and re-set it via `setContextProperty` on `AppSettings::nameChanged` — but this requires wiring the signal in C++ and is less clean.
**Warning signs:** QML shows "undefined" for `appSettings.receiverName`.

### Pitfall 6: New QML Files Not Listed in qt_add_qml_module
**What goes wrong:** Component `HudOverlay.qml` exists on disk but QML engine cannot find the type; reports "HudOverlay is not a type".
**Why it happens:** Qt6's `qt_add_qml_module` requires all QML files to be explicitly listed in `QML_FILES`. Files not listed are not embedded in the resource or registered as types.
**How to avoid:** Update `CMakeLists.txt` `qt_add_qml_module` `QML_FILES` list whenever a new `.qml` file is added.
**Warning signs:** CMake build succeeds but runtime error "qrc:/.../HudOverlay.qml: File not found".

---

## Code Examples

### Minimal ConnectionBridge Header
```cpp
// src/ui/ConnectionBridge.h
// Source: AudioBridge.h pattern (src/ui/AudioBridge.h)
#pragma once
#include <QObject>
#include <QString>

namespace airshow {

class ConnectionBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    connected   READ isConnected  NOTIFY connectedChanged)
    Q_PROPERTY(QString deviceName  READ deviceName   NOTIFY deviceNameChanged)
    Q_PROPERTY(QString protocol    READ protocol     NOTIFY protocolChanged)
public:
    explicit ConnectionBridge(QObject* parent = nullptr);

    bool    isConnected() const { return m_connected; }
    QString deviceName()  const { return m_deviceName; }
    QString protocol()    const { return m_protocol; }

    // Called by protocol handlers in Phase 4+. Safe to call from C++.
    void setConnected(bool connected,
                      const QString& deviceName = {},
                      const QString& protocol   = {});

signals:
    void connectedChanged(bool connected);
    void deviceNameChanged(const QString& name);
    void protocolChanged(const QString& proto);

private:
    bool    m_connected  = false;
    QString m_deviceName;
    QString m_protocol;
};

} // namespace airshow
```

### ReceiverWindow::load() Integration Point
```cpp
// src/ui/ReceiverWindow.cpp — additions to load()
// Source: existing ReceiverWindow.cpp audioBridge pattern
auto* connBridge = new ConnectionBridge(&m_engine);
m_engine.rootContext()->setContextProperty("connectionBridge", connBridge);

// (optional) SettingsBridge for live receiverName binding:
auto* settingsBridge = new SettingsBridge(m_settings, &m_engine);
m_engine.rootContext()->setContextProperty("appSettings", settingsBridge);

// THEN load QML (all context properties must exist before load):
m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/AirShow/qml/main.qml")));
```

### GstGLQt6VideoItem with explicit forceAspectRatio
```qml
// qml/main.qml — DISP-01
// Source: GStreamer qt6glitem.h Q_PROPERTY forceAspectRatio
GstGLQt6VideoItem {
    id: videoItem
    objectName: "videoItem"
    anchors.fill: parent
    forceAspectRatio: true   // explicit for documentation; already the default
}
// Window color: "black" (unchanged) provides letterbox bar colour
```

### CMakeLists.txt QML_FILES Update
```cmake
# CMakeLists.txt — add new QML component files
# Source: Qt6 docs — qt_add_qml_module QML_FILES
qt_add_qml_module(airshow
  URI AirShow
  VERSION 1.0
  QML_FILES
    qml/main.qml
    qml/HudOverlay.qml
    qml/IdleScreen.qml
)
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Qt5 `qmlglsink` / `GstQtVideoItem` | Qt6 `qml6glsink` / `GstGLQt6VideoItem` | GStreamer 1.21.3 (2022) | `forceAspectRatio` property name same; `fillMode` was never on GStreamer items |
| `setContextProperty` for all C++/QML bridges | Qt6 prefers `QML_ELEMENT` + singleton | Qt 6.2 (2021) | Project already uses `setContextProperty` idiom; acceptable for this project size; no migration needed in Phase 3 |

**Deprecated/outdated:**
- `GstQtVideoItem` (Qt5): replaced by `GstGLQt6VideoItem` for Qt6 — do not use.
- `qmlglsink` (Qt5): replaced by `qml6glsink` — project already uses correct Qt6 variant.

---

## Runtime State Inventory

> Phase 3 is a greenfield UI addition with no renames, refactors, or data migrations.

Not applicable — omitted per instructions (greenfield phase).

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt6 Quick | QML scene graph | ✓ | 6.8 LTS (linked) | — |
| Qt6 Qml | Context properties | ✓ | 6.8 LTS (linked) | — |
| GStreamer qml6glsink | Video rendering | ✓ | 1.26.x (in build) | — |
| GTest / GMock | Phase tests | ✓ | present (test_pipeline builds) | — |

**Missing dependencies with no fallback:** None.

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Google Test (GTest + GMock) |
| Config file | `tests/CMakeLists.txt` |
| Quick run command | `ctest --test-dir build/linux-debug -R test_display -V` |
| Full suite command | `ctest --test-dir build/linux-debug --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DISP-01 | forceAspectRatio defaults to true on videoItem | unit | `ctest --test-dir build/linux-debug -R test_display_aspect_ratio -V` | ❌ Wave 0 |
| DISP-02 | ConnectionBridge starts disconnected; setConnected() emits signals | unit | `ctest --test-dir build/linux-debug -R test_connection_bridge -V` | ❌ Wave 0 |
| DISP-03 | ConnectionBridge connected=false on construction (idle state) | unit | `ctest --test-dir build/linux-debug -R test_connection_bridge -V` | ❌ Wave 0 |

> QML visual rendering (idle screen visibility, HUD fade timing, text content) cannot be automated with GTest without a headless Qt QML test harness (QQuickTest). Given project use of GTest, visual tests are manual-only. Unit tests cover the C++ bridge logic that drives the QML state.

### Sampling Rate
- **Per task commit:** `ctest --test-dir build/linux-debug -R test_display -V`
- **Per wave merge:** `ctest --test-dir build/linux-debug --output-on-failure`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/test_display.cpp` — covers DISP-01 (forceAspectRatio property exists/default), DISP-02/03 (ConnectionBridge state machine)
- [ ] `tests/CMakeLists.txt` — add `test_display` target (mirrors `test_discovery` structure; links Qt6::Core, no GStreamer dependency)

---

## Open Questions

1. **SettingsBridge vs direct string context property for receiverName**
   - What we know: `AppSettings` is not a `QObject`; live binding requires `Q_PROPERTY`+`NOTIFY`.
   - What's unclear: Whether `AppSettings::nameChanged` signal exists (it does not — AppSettings has no signals at all; it's a plain class with no Qt object model). A Phase 2 decision note says "caller must then call `discoveryManager->rename()`" — there is no signal to connect to.
   - Recommendation: Create a thin `SettingsBridge : QObject` with a `receiverName` Q_PROPERTY. Phase 3 only reads the name; the bridge reads from `AppSettings` and re-emits on manual trigger. Or: expose `receiverName` as a plain `QVariant` context property (set once at startup, updated manually). For Phase 3 the name does not change during a session, so initial binding is sufficient — defer the live-update problem to Phase 7 (settings panel).

2. **Mute button visual style**
   - What we know: D-15 says no heavy theming; Claude has discretion on whether mute button needs style update.
   - What's unclear: Whether the plain `Button` from Qt Quick Controls is visually consistent with the new dark overlay aesthetic.
   - Recommendation: Restyle the mute button to match the overlay style (dark semi-transparent background, white text, same opacity, same radius) so it does not look out of place against the new HUD.

---

## Sources

### Primary (HIGH confidence)
- `github.com/GStreamer/gstreamer` — `subprojects/gst-plugins-good/ext/qt6/qt6glitem.h` — confirmed `forceAspectRatio` Q_PROPERTY (default true), `acceptEvents`, `itemInitialized`; no `fillMode`
- `github.com/GStreamer/gstreamer` — `subprojects/gst-plugins-good/ext/qt6/qt6glitem.cc` — confirmed `gst_video_sink_center_rect()` for aspect ratio; forceAspectRatio=true default
- `doc.qt.io/qt-6/qml-qtquick-numberanimation.html` — NumberAnimation syntax, easing types
- `doc.qt.io/qt-6/qml-qtquick-sequentialanimation.html` — SequentialAnimation, `loops: Animation.Infinite`
- `doc.qt.io/qt-6/qtqml-cppintegration-contextproperties.html` — setContextProperty pattern
- `doc.qt.io/qt-6/qt-add-qml-module.html` — QML_FILES multi-file registration

### Secondary (MEDIUM confidence)
- WebSearch cross-verified: `GstGLQt6VideoItem` has no `fillMode`; `forceAspectRatio` is the equivalent
- Qt Forum topic/159182 — confirmed GstGLQt6VideoItem property set via source code inspection
- WebSearch cross-verified: `loops: Animation.Infinite` + `running: item.visible` pattern for idle pulse

### Tertiary (LOW confidence)
- None — all critical claims verified against official source code or Qt documentation.

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all dependencies already linked; no new packages needed
- Architecture: HIGH — forceAspectRatio confirmed from GStreamer source; bridge pattern confirmed from existing codebase
- Pitfalls: HIGH — fillMode/forceAspectRatio confusion confirmed from source; other pitfalls from Qt documentation
- Animation patterns: HIGH — direct Qt documentation

**Research date:** 2026-03-28
**Valid until:** 2026-09-28 (stable APIs; GStreamer and Qt6 LTS)
