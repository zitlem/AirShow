---
phase: 01-foundation
plan: 02
subsystem: media-pipeline
tags: [gstreamer, qt-quick, qml, audio-bridge, pipeline, mute]
dependency_graph:
  requires: [01-01]
  provides: [MediaPipeline::init, AudioBridge, ReceiverWindow::load, fullscreen-qml-window]
  affects: [01-03]
tech_stack:
  added: [glupload, GstGLQt6VideoItem, AudioBridge Q_PROPERTY, qml6glsink-preload-pattern]
  patterns: [fakesink-fallback-headless, glupload-bridge, qml6glsink-preload-before-engine-load]
key_files:
  created:
    - src/ui/AudioBridge.h
    - src/ui/AudioBridge.cpp
  modified:
    - src/pipeline/MediaPipeline.h
    - src/pipeline/MediaPipeline.cpp
    - src/ui/ReceiverWindow.cpp
    - qml/main.qml
    - src/main.cpp
    - CMakeLists.txt
    - tests/test_pipeline.cpp
decisions:
  - glupload inserted between videoconvert and qml6glsink to bridge video/x-raw to video/x-raw(memory:GLMemory)
  - fakesink used for video branch when qmlVideoItem is nullptr (headless/test mode) because qml6glsink requires QGuiApplication
  - glib.h (g_warning/g_info) used in MediaPipeline.cpp instead of QDebug so file compiles in test target without Qt headers
  - AudioBridge parented to m_engine (not ReceiverWindow) since ReceiverWindow is not a QObject
metrics:
  duration: 6m
  completed: 2026-03-28
  tasks: 2
  files_created: 2
  files_modified: 7
---

# Phase 01 Plan 02: Two-Branch GStreamer Pipeline and QML Window Summary

**One-liner:** Two-branch GStreamer pipeline (videotestsrc+audiotestsrc via glupload+qml6glsink) wired into a fullscreen Qt Quick window with an AudioBridge mute toggle.

## What Was Built

### MediaPipeline::init() signature and element names

```cpp
bool MediaPipeline::init(void* qmlVideoItem);
```

- When `qmlVideoItem` is non-null (production): `videotestsrc ! videoconvert ! glupload ! qml6glsink` + `audiotestsrc ! audioconvert ! autoaudiosink`
- When `qmlVideoItem` is null (headless/test): `videotestsrc ! videoconvert ! fakesink` + `audiotestsrc ! audioconvert ! autoaudiosink`
- `m_audioSink` stores the `autoaudiosink` element pointer for volume control
- Returns `true` on `GST_STATE_CHANGE_ASYNC` or `GST_STATE_CHANGE_SUCCESS`; `false` on `GST_STATE_CHANGE_FAILURE`

GStreamer element names used (for Plan 03 to extend):
- `"videosrc"` — videotestsrc
- `"videoconvert"` — videoconvert
- `"glupload"` — glupload (GL only)
- `"videosink"` — qml6glsink or fakesink
- `"audiosrc"` — audiotestsrc
- `"audioconvert"` — audioconvert
- `"audiosink"` — autoaudiosink

### AudioBridge interface

```cpp
class AudioBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
public:
    explicit AudioBridge(MediaPipeline& pipeline, QObject* parent = nullptr);
    bool isMuted() const;
    void setMuted(bool muted);
signals:
    void mutedChanged(bool muted);
};
```

- `setMuted()` guards against no-op calls (`if (m_pipeline.isMuted() == muted) return`)
- Emits `mutedChanged` only on actual state change
- Delegates to `MediaPipeline::setMuted()` which calls `g_object_set(m_audioSink, "volume", ...)` (D-08)

### ReceiverWindow::load() contract

Critical order (must not be changed):

1. `gst_element_factory_make("qml6glsink", nullptr)` — registers `GstGLQt6VideoItem` QML type as a side-effect
2. `m_engine.rootContext()->setContextProperty("audioBridge", audioBridge)` — exposes QObject to QML
3. `m_engine.load(QUrl("qrc:/qt/qml/MyAirShow/qml/main.qml"))` — loads QML with type registered
4. `rootObject->findChild<QObject*>("videoItem")` — retrieves `GstGLQt6VideoItem` by `objectName`
5. `m_pipeline.init(videoItem)` — passes the QML item to the pipeline

If `qml6glsink` preload is omitted, `GstGLQt6VideoItem` is not registered and QML loading fails silently.

### qml/main.qml: objectName used by findChild

```qml
GstGLQt6VideoItem {
    id: videoItem
    objectName: "videoItem"   // <-- matched by findChild<QObject*>("videoItem")
    anchors.fill: parent
}
```

The `objectName: "videoItem"` string must remain stable — it is the coupling point between the QML item and `ReceiverWindow::load()`.

### White-box accessor for tests

```cpp
GstElement* gstPipeline() const { return m_pipeline; }
```

Allows tests to call `gst_element_get_state()` on the real pipeline pointer without crashing on null.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] qml6glsink requires QGuiApplication; headless test fails with GST_STATE_CHANGE_FAILURE**
- **Found during:** Task 1 (TDD GREEN phase)
- **Issue:** `qml6glsink` emits "Could not retrieve QGuiApplication instance" and returns `GST_STATE_CHANGE_FAILURE` when no Qt app context exists (unit tests)
- **Fix:** Added conditional sink selection — when `qmlVideoItem` is `nullptr`, use `fakesink` instead of `qml6glsink`. Audio branch always uses `autoaudiosink`.
- **Files modified:** `src/pipeline/MediaPipeline.cpp`
- **Commit:** a64049c

**2. [Rule 1 - Bug] videoconvert caps incompatible with qml6glsink without glupload**
- **Found during:** Task 1 (TDD GREEN phase)
- **Issue:** `videoconvert` outputs `video/x-raw` but `qml6glsink` requires `video/x-raw(memory:GLMemory)`. Link fails with "caps are incompatible / no common format".
- **Fix:** Inserted `glupload` element between `videoconvert` and `qml6glsink` in the GL branch.
- **Files modified:** `src/pipeline/MediaPipeline.cpp`
- **Commit:** a64049c

**3. [Rule 1 - Bug] MediaPipeline.cpp used QDebug which fails to compile in test target (no Qt headers)**
- **Found during:** Task 1 first build attempt
- **Issue:** Test target does not link Qt; `#include <QDebug>` causes compile error.
- **Fix:** Replaced `QDebug` with `#include <glib.h>` and `g_warning()`/`g_info()` (GLib logging, already a transitive dependency via GStreamer).
- **Files modified:** `src/pipeline/MediaPipeline.cpp`
- **Commit:** a64049c

**4. [Rule 1 - Bug] ReceiverWindow is not a QObject; cannot pass `this` as AudioBridge parent**
- **Found during:** Task 2 build
- **Issue:** `AudioBridge(MediaPipeline&, QObject*)` — `ReceiverWindow*` is not a `QObject*`.
- **Fix:** Parent `AudioBridge` to `m_engine` (`QQmlApplicationEngine` is a `QObject`), ensuring its lifetime is tied to the engine.
- **Files modified:** `src/ui/ReceiverWindow.cpp`
- **Commit:** 6f31aad

## Decisions Made

| Decision | Rationale |
|----------|-----------|
| glupload inserted in GL branch | qml6glsink sink pad accepts only GLMemory; glupload is the standard bridge element |
| fakesink for headless/test mode | Allows pipeline audio branch to be tested without a display or Qt context |
| glib.h instead of QDebug in pipeline | Pipeline code must be linkable from both Qt executable and pure GStreamer test binary |
| AudioBridge parented to m_engine | Only QObjects can be QObject parents; m_engine is the correct lifetime anchor |
| qml6glsink preload before engine.load() | Required to register GstGLQt6VideoItem QML type before QML parsing |

## Pipeline Extension Points for Plan 03

Plan 03 (decoder detection) will extend MediaPipeline by:
- Implementing `initDecoderPipeline()` (currently stub returning `false`)
- Using `static void onElementAdded(GstBin*, GstElement*, gpointer)` already declared in header
- Connecting via `g_signal_connect(decodebin, "element-added", G_CALLBACK(MediaPipeline::onElementAdded), this)`
- `m_decoderPipeline` member already declared for the second pipeline instance

## Self-Check: PASSED

- FOUND: src/ui/AudioBridge.h
- FOUND: src/ui/AudioBridge.cpp
- FOUND: src/pipeline/MediaPipeline.h
- FOUND: src/pipeline/MediaPipeline.cpp
- FOUND: qml/main.qml
- FOUND: .planning/phases/01-foundation/01-02-SUMMARY.md
- FOUND commit: a64049c (MediaPipeline implementation)
- FOUND commit: 6f31aad (ReceiverWindow, AudioBridge, QML window)
