# Phase 1: Foundation - Research

**Researched:** 2026-03-28
**Domain:** CMake cross-platform build system, Qt6 Quick/QML fullscreen window, GStreamer 1.26 media pipeline, qml6glsink video rendering, hardware H.264 decode with fallback
**Confidence:** HIGH — core stack verified against official documentation, package availability confirmed on Linux dev machine, architecture patterns verified against GStreamer + Qt6 upstream examples

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Use CMake as the build system with find_package for Qt6 and GStreamer dependencies
- **D-02:** On Linux, depend on system-installed Qt6 and GStreamer packages. On macOS and Windows, bundle dependencies (Homebrew Qt6 + GStreamer framework on macOS; vcpkg or pre-built binaries on Windows)
- **D-03:** C++17 standard — matches research recommendation and AirServer's confirmed stack
- **D-04:** Use `qml6glsink` as the GStreamer video sink — renders directly into a QML item via shared GL context
- **D-05:** Use `appsrc` injection point for feeding protocol data into the pipeline. For Phase 1, use `videotestsrc` and `audiotestsrc` to validate the pipeline
- **D-06:** Single shared GStreamer pipeline architecture — all future protocols will converge on one pipeline, not per-protocol pipelines
- **D-07:** Use GStreamer `autoaudiosink` which auto-selects the correct platform backend (PipeWire/PulseAudio on Linux, CoreAudio on macOS, WASAPI on Windows)
- **D-08:** Mute toggle implemented by setting the audio sink volume to 0 (not by disconnecting the audio branch)
- **D-09:** Qt Quick/QML for the receiver window — required for qml6glsink integration and provides GPU-accelerated rendering
- **D-10:** Application launches fullscreen by default on the primary display
- **D-11:** Use GStreamer `decodebin` with rank-based decoder selection. Log which decoder (vaapih264dec, nvh264dec, vtdec, d3d11h264dec, avdec_h264) is selected
- **D-12:** If hardware decode fails or is unavailable, fall back to software `avdec_h264` and log a warning — do not crash or refuse to play

### Claude's Discretion

- CMake module structure and directory layout
- Specific CI/CD pipeline configuration (if any in Phase 1)
- Test infrastructure setup choices
- Exact GStreamer pipeline element chain beyond the decisions above

### Deferred Ideas (OUT OF SCOPE)

None — discussion stayed within phase scope
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| FOUND-01 | Application builds and runs on Linux, macOS, and Windows from a single codebase | CMake 3.31 + pkg-config + FindGStreamer.cmake confirmed available on Linux; macOS/Windows dependency strategy documented in Standard Stack |
| FOUND-02 | Application renders video frames from GStreamer pipeline in a Qt fullscreen window | qml6glsink confirmed as correct integration path; `GstGLVideoItem` QML item and `widget` property pattern documented in Code Examples |
| FOUND-03 | Application plays audio from mirrored device through system speakers | `autoaudiosink` selects correct platform backend automatically; no platform-specific code needed |
| FOUND-04 | User can mute/unmute audio with a toggle control | Volume-to-0 pattern on audio sink documented; avoids sync disruption vs. disconnecting branch |
| FOUND-05 | Application detects and uses hardware H.264 decoder when available, falls back to software gracefully | `decodebin` rank-based selection documented; decoder name retrieval pattern and fallback to `avdec_h264` documented in Code Examples |
</phase_requirements>

---

## Summary

Phase 1 establishes the foundational binary: a CMake-driven cross-platform C++17 project that opens a Qt Quick fullscreen window, hosts a GStreamer pipeline rendering a test video via `qml6glsink` into the QML scene graph, plays test audio via `autoaudiosink`, provides a mute toggle, and logs whether hardware or software H.264 decode is active. No protocol code, no network code — this phase exists solely to prove the media pipeline, windowing integration, and build system work on all three platforms before any protocol complexity is added.

The integration approach is well-proven by the commercial reference (AirServer, which uses Qt + GStreamer) and by the open-source ecosystem (UxPlay). The `qml6glsink` element ships in `gst-plugins-good` and is available as a system package (`gstreamer1.0-qt6`) on Ubuntu. The key build concern on Linux is that GStreamer dev headers (`libgstreamer1.0-dev`, `libgstreamer-plugins-base1.0-dev`) must be installed separately from the runtime packages — the system currently has runtime GStreamer 1.26.x installed but not the dev headers. The system's Qt version is 6.9.2, which is newer than the recommended 6.8 LTS; this is acceptable and compatible.

**Primary recommendation:** Set up CMake with pkg-config GStreamer detection, create a minimal Qt Quick application with a single QML window, integrate `qml6glsink` using the `widget` property pattern to connect a C++-constructed GStreamer pipeline to the QML `GstGLVideoItem`, and use `decodebin` with a post-instantiation callback to log the selected decoder element name.

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C++17 | C++17 standard | Implementation language | All protocol libraries (UxPlay, libupnp, OpenSSL) are C/C++; Qt, GStreamer, OpenSSL all have first-class C++ APIs; confirmed AirServer stack |
| Qt 6.9.2 (system) | 6.9.2 (system); 6.8 LTS is minimum target | GUI framework + QML window | Required for qml6glsink integration; system package on Linux dev machine is 6.9.2 — compatible with 6.8+ API |
| GStreamer 1.26.x | 1.26.6 (runtime installed), 1.26.5 (plugins) | Audio/video pipeline | Only cross-platform pipeline library with H.264 hardware decode (VAAPI/D3D11/VideoToolbox) and qml6glsink |
| CMake | 3.31.6 (system) | Build system | System CMake exceeds ≥3.28 requirement; Qt6's FindGStreamer.cmake is available at `/usr/lib/x86_64-linux-gnu/cmake/Qt6/FindGStreamer.cmake` |
| OpenSSL | 3.5.3 (libssl-dev installed) | Crypto (future phases) | Dev package already installed; link against it now to avoid a header-change task later |

### Supporting

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| gst-plugins-base | 1.26.6 (installed) | Core GStreamer plugins: videoconvert, audioconvert, appsrc, appsink | Always — provides the elements used in Phase 1 pipeline |
| gst-plugins-good | 1.26.5 (installed) | qml6glsink, rtph264depay, rtpjitterbuffer | Always — qml6glsink lives here |
| gstreamer1.0-qt6 | 1.26.5 (available, not yet installed) | Provides libgstqml6.so — the qml6glsink plugin | Must install for Phase 1 to work |
| gstreamer1.0-libav | 1.26.6 (installed) | FFmpeg-based software codec fallback: avdec_h264, avdec_aac | Always — provides software fallback H.264/AAC decode |
| gst-plugins-bad | 1.26.5 (installed) | VAAPI, D3D11 hardware decode on Linux/Windows | Installed; hardware decode elements (vaapih264dec) live here |
| spdlog | 1.15.3 (libspdlog-dev available) | Structured logging for decoder selection, pipeline events | Phase 1 logger; lightweight header-only option |
| Ninja | 6.9.2 (system) | Fast parallel build (ninja version shown as 6.9.2 — this is actually the Qt version shown by the shell; ninja binary is installed) | Pair with CMake for fast builds |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| qml6glsink | Qt Multimedia QMediaPlayer | QMediaPlayer is opaque — cannot accept raw frames from protocol callbacks. Not viable for future phases. |
| autoaudiosink | Platform-specific sink (pulsesink, wasapisink, osxaudiosink) | autoaudiosink abstracts the platform correctly. Platform-specific sinks only needed if fine-grained audio device selection is required (Phase 3 UI). |
| decodebin | Explicit pipeline string | Explicit pipeline is fragile across platforms (element names differ). `decodebin` with rank control is the portable approach. |
| pkg-config for GStreamer | Find via Qt6's FindGStreamer.cmake | Qt6's FindGStreamer.cmake uses pkg-config internally. The Qt6 CMake module path is the canonical way to get GStreamer::App, GStreamer::Video etc. as imported targets. |

**Installation (Linux — dev packages not yet installed):**
```bash
sudo apt install \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-qt6 \
  libspdlog-dev
```

**Already installed on Linux dev machine:** cmake 3.31, ninja, Qt 6.9.2 (base + declarative), libssl-dev 3.5.3, gstreamer1.0-libav, gstreamer1.0-plugins-base/good/bad (runtime), gstreamer1.0-gl

**macOS (Homebrew):**
```bash
brew install gstreamer gst-plugins-base gst-plugins-good \
  gst-plugins-bad gst-libav qt@6 cmake ninja spdlog
```

**Windows (MSYS2 MinGW-64 or vcpkg):**
```bash
# MSYS2 approach
pacman -S mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base \
  mingw-w64-x86_64-gst-plugins-good mingw-w64-x86_64-gst-plugins-bad \
  mingw-w64-x86_64-gst-libav mingw-w64-x86_64-qt6-base \
  mingw-w64-x86_64-qt6-declarative mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja

# NOTE: On Windows, gstreamer1.0-qt6 equivalent (gstqml6.dll) must be
# present in the GStreamer plugin path. The official GStreamer MSVC installer
# includes it; MSYS2 packages it in mingw-w64-x86_64-gst-plugins-good.
```

**Version verification (confirmed 2026-03-28 on Linux dev machine):**
- `cmake --version` → 3.31.6
- `pkg-config --modversion Qt6Core` → 6.9.2
- `apt-cache show libgstreamer1.0-dev` → 1.26.6-1
- `apt-cache show gstreamer1.0-qt6` → 1.26.5-1ubuntu2 (available, not yet installed)
- `apt-cache show libssl-dev` → 3.5.3-1ubuntu3 (installed)

---

## Architecture Patterns

### Recommended Project Structure

```
AirShow/
├── CMakeLists.txt              # Root: find Qt6, GStreamer; add subdirs
├── CMakePresets.json           # Per-platform presets (linux-debug, macos-debug, windows-debug)
├── vcpkg.json                  # Optional: for Windows deps not in MSYS2
├── src/
│   ├── main.cpp                # Bootstrap: QGuiApplication, load QML, init pipeline
│   ├── pipeline/
│   │   ├── MediaPipeline.h     # GStreamer pipeline wrapper: init, play, pause, stop
│   │   ├── MediaPipeline.cpp   # Pipeline construction, appsrc, decoder detection
│   │   └── DecoderInfo.h       # Struct: decoder name, hw/sw flag, element type
│   └── ui/
│       ├── ReceiverWindow.h    # QML engine holder, fullscreen window management
│       └── ReceiverWindow.cpp  # Loads QML, retrieves GstGLVideoItem, sets widget property
├── qml/
│   ├── main.qml                # Root QML: Window {flags: fullscreen}, VideoItem
│   └── VideoItem.qml           # GstGLVideoItem host (or inline in main.qml)
└── tests/
    ├── CMakeLists.txt          # CTest integration
    └── test_pipeline.cpp       # Pipeline smoke tests
```

**Rationale:**
- `src/pipeline/` is isolated — protocol handlers (Phases 4–8) will call `MediaPipeline::pushVideoBuffer()` and `pushAudioBuffer()` without knowing anything about the Qt UI.
- `src/ui/` owns the QML engine; MediaPipeline does not import Qt. This enforces the separation documented in ARCHITECTURE.md.
- `qml/` kept separate from `src/` so QML can be edited without touching C++.

### Pattern 1: CMake with pkg-config GStreamer detection

**What:** Use CMake's `find_package(PkgConfig)` to locate GStreamer libraries as imported targets. Qt6's toolchain includes `FindGStreamer.cmake` that wraps this into `GStreamer::App`, `GStreamer::Video`, etc.

**When to use:** Always on Linux and macOS (system packages). On Windows with MSYS2, pkg-config is available and works the same way.

**Example:**
```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.28)
project(AirShow LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 — must come before GStreamer to get Qt6's FindGStreamer.cmake on the path
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml)
qt_standard_project_setup()

# GStreamer via pkg-config (Qt6's FindGStreamer.cmake uses this internally)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
  gstreamer-1.0>=1.20
  gstreamer-video-1.0>=1.20
  gstreamer-app-1.0>=1.20
  gstreamer-audio-1.0>=1.20
)

# Main executable
qt_add_executable(airshow src/main.cpp src/pipeline/MediaPipeline.cpp src/ui/ReceiverWindow.cpp)
qt_add_qml_module(airshow URI AirShow VERSION 1.0 QML_FILES qml/main.qml)

target_link_libraries(airshow PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml
  PkgConfig::GST
)
```

### Pattern 2: qml6glsink integration — loading the plugin and binding to QML item

**What:** `qml6glsink` renders GStreamer video into a QML scene graph item (`GstGLVideoItem`). The plugin must be loaded before the QML file is parsed (so it can register the `GstGLVideoItem` type), and then a C++ call sets the QML item as the sink's `widget` property.

**When to use:** Always — this is the locked decision (D-04) and the pattern used by AirServer.

**Example (C++ side):**
```cpp
// Source: GStreamer qml6glsink docs + official GStreamer qmlsink example pattern
// https://gstreamer.freedesktop.org/documentation/qml6/qml6glsink.html

#include <gst/gst.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

void setupPipeline(QQmlApplicationEngine &engine) {
    // Step 1: Load the qml6glsink plugin BEFORE parsing QML
    // This registers the GstGLVideoItem QML type
    GstElement *preload = gst_element_factory_make("qml6glsink", nullptr);
    // preload can be unreffed immediately; its only job is type registration
    if (preload) gst_object_unref(preload);

    // Step 2: Load QML (GstGLVideoItem is now registered)
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    // Step 3: Retrieve the video item from the QML scene
    QObject *rootObject = engine.rootObjects().first();
    QObject *videoItem = rootObject->findChild<QObject*>("videoItem");

    // Step 4: Build the pipeline with qml6glsink
    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    GstElement *src      = gst_element_factory_make("videotestsrc", "src");
    GstElement *sink     = gst_element_factory_make("qml6glsink", "sink");

    // Step 5: Set the QML item as the sink's widget
    g_object_set(sink, "widget", videoItem, nullptr);

    gst_bin_add_many(GST_BIN(pipeline), src, sink, nullptr);
    gst_element_link(src, sink);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}
```

**QML side:**
```qml
// qml/main.qml
import QtQuick 2.0
import QtQuick.Window 2.0
import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0   // Registered by qml6glsink preload

Window {
    id: root
    visibility: Window.FullScreen
    color: "black"

    GstGLQt6VideoItem {
        id: videoItem
        objectName: "videoItem"    // Must match findChild() call in C++
        anchors.fill: parent
    }
}
```

### Pattern 3: Hardware decoder detection via decodebin element-added signal

**What:** `decodebin` autoplugs decoders at runtime based on rank. Hook the `element-added` signal to inspect which decoder was selected, then log it. Supports D-11 (log decoder) and D-12 (no crash on missing hardware decode).

**When to use:** Always — decodebin is the locked approach (D-11).

**Example:**
```cpp
// Source: GStreamer decodebin documentation + rank system documentation
// https://gstreamer.freedesktop.org/documentation/playback/decodebin3.html

static void onElementAdded(GstBin* /*bin*/, GstElement* element, gpointer /*userData*/) {
    GstElementFactory *factory = gst_element_get_factory(element);
    if (!factory) return;

    const gchar *name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
    const gchar *klass = gst_element_factory_get_klass(factory);

    // Check if this is a video decoder
    if (g_strstr_len(klass, -1, "Decoder/Video") != nullptr) {
        const bool isHardware =
            g_str_has_prefix(name, "vaapi")   ||  // Linux VAAPI
            g_str_has_prefix(name, "nv")       ||  // NVIDIA NVDEC
            g_str_has_prefix(name, "vtdec")    ||  // macOS VideoToolbox
            g_str_has_prefix(name, "d3d11")    ||  // Windows D3D11
            g_str_has_prefix(name, "mfh264dec");   // Windows Media Foundation

        if (isHardware) {
            g_message("Hardware H.264 decoder selected: %s", name);
        } else {
            g_warning("Software H.264 decoder selected: %s (hardware unavailable)", name);
        }
    }
}

// In pipeline construction:
GstElement *decodebin = gst_element_factory_make("decodebin", "decoder");
g_signal_connect(decodebin, "element-added", G_CALLBACK(onElementAdded), nullptr);
```

### Pattern 4: Audio mute via volume property (D-08)

**What:** The `autoaudiosink` volume is controlled by setting the `volume` property to 0.0 (mute) or 1.0 (unmute). This keeps the audio branch running and maintains A/V sync — critical per PITFALLS.md.

**Example:**
```cpp
// Source: GStreamer autoaudiosink documentation
GstElement *audioSink = gst_bin_get_by_name(GST_BIN(pipeline), "audiosink");
// Mute:
g_object_set(audioSink, "volume", 0.0, nullptr);
// Unmute:
g_object_set(audioSink, "volume", 1.0, nullptr);

// Expose to QML via Q_PROPERTY on a C++ QObject bridge:
// Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
```

### Anti-Patterns to Avoid

- **Calling `gst_element_factory_make("qml6glsink")` after loading QML:** The `GstGLVideoItem` type will not be registered, causing a QML import error at runtime. Always preload before `engine.load()`.
- **Using `sync=false` on the video sink:** Disables clock-based frame presentation, causing audio/video desync. Never do this in production — only acceptable in a debugging session when checking that frames decode at all.
- **Creating a GStreamer pipeline per protocol handler:** Violates D-06. One pipeline, multiple appsrc elements (one for video, one for audio). Protocol handlers push buffers; they do not own pipelines.
- **Passing the GStreamer pipeline pointer into the QML file:** QML must not know about GStreamer. All GStreamer state lives in C++; QML only hosts the `GstGLVideoItem` and calls Qt signals/slots for UI events (mute toggle).
- **Linking against GStreamer at compile time without checking plugin availability at runtime:** Use `gst_registry_check_feature_version("qml6glsink", 1, 20, 0)` at startup to give a clear error message if the plugin is missing rather than a cryptic pipeline failure.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Cross-platform audio output | Platform-specific audio write code | `autoaudiosink` | Handles PipeWire/PulseAudio (Linux), CoreAudio (macOS), WASAPI (Windows) automatically |
| H.264 decoder selection | Custom decoder negotiation code | `decodebin` with rank system | GStreamer's rank system selects the highest-priority available decoder; falling back is automatic |
| GL texture sharing between GStreamer and Qt | Custom EGL/GLX context sharing | `qml6glsink` | Zero-copy GPU texture sharing via shared OpenGL context — complex to implement correctly |
| Video frame timestamping | Custom PTS calculation | GStreamer buffer PTS + pipeline clock | Getting frame presentation timing right without the pipeline clock leads to drift |
| Fullscreen window on all platforms | Per-platform window API | `Window { visibility: Window.FullScreen }` in QML | Qt handles the platform-specific fullscreen API (Wayland, X11, Win32, Cocoa) |

**Key insight:** GStreamer's pluggable element system makes hardware decode fallback automatic — `decodebin` tries higher-ranked hardware decoders first and falls back to `avdec_h264` (gst-libav) if none are available. The only task is to log which path was taken, not to implement the fallback logic.

---

## Common Pitfalls

### Pitfall 1: gstreamer1.0-qt6 package not installed (qml6glsink unavailable)

**What goes wrong:** `gst_element_factory_make("qml6glsink", nullptr)` returns NULL. The pipeline fails silently or crashes. The QML import of `org.freedesktop.gstreamer.Qt6GLVideoItem` fails with "module not found."

**Why it happens:** The GStreamer Qt6 plugin (`libgstqml6.so`) ships in a separate package (`gstreamer1.0-qt6`) that is not automatically installed with `gstreamer1.0-plugins-good`. On the current Linux dev machine it is available in apt but NOT yet installed.

**How to avoid:** Wave 0 setup task must install `gstreamer1.0-qt6`. Add a startup check: `if (!gst_registry_check_feature_version("qml6glsink", 1, 20, 0)) { qFatal("qml6glsink not available — install gstreamer1.0-qt6"); }`.

**Warning signs:** `(gst-plugin-scanner:PID): GLib-GObject-WARNING: Plugin 'qml6' not found` in stderr.

### Pitfall 2: Qt ABI mismatch between system Qt and GStreamer Qt6 plugin

**What goes wrong:** The `gstreamer1.0-qt6` package depends on `qt6-base-private-abi = 6.9.2` (confirmed via `apt-cache show`). If the build uses a different Qt version (e.g., a manually installed Qt 6.8 alongside the system 6.9.2), the GStreamer Qt6 plugin will refuse to load.

**Why it happens:** GStreamer's Qt6 plugin uses Qt private APIs (for GL context sharing) and is ABI-pinned to a specific Qt patch version. The system package locks to `= 6.9.2`.

**How to avoid:** On Linux, use the system Qt exclusively (6.9.2). Do not mix system GStreamer with a manually installed Qt 6.8. On macOS and Windows where you control both, build both against the same Qt version.

**Warning signs:** `GLib-GObject-WARNING: unable to find plugin 'qml6'` combined with `CRITICAL: GStreamer plugin 'qml6': Qt version mismatch`.

### Pitfall 3: GStreamer dev headers missing (build fails immediately)

**What goes wrong:** CMake's `pkg_check_modules(GST REQUIRED gstreamer-1.0)` fails because the pkg-config entry for `gstreamer-1.0` is absent. The system has GStreamer 1.26.x runtime installed but not the dev headers.

**Why it happens:** On Ubuntu/Debian, runtime (`libgstreamer1.0-0`) and dev (`libgstreamer1.0-dev`) are separate packages. Runtime is installed by default as a desktop dependency; dev package must be added explicitly.

**How to avoid:** Wave 0 install step. Required packages: `libgstreamer1.0-dev`, `libgstreamer-plugins-base1.0-dev`, `libgstreamer-plugins-bad1.0-dev`.

**Warning signs:** `CMake Error: could not find pkg-config module 'gstreamer-1.0'`.

### Pitfall 4: qml6glsink preload must happen before QML engine load

**What goes wrong:** `GstGLQt6VideoItem` type is not registered when the QML file is parsed. The `import org.freedesktop.gstreamer.Qt6GLVideoItem 1.0` line fails at runtime with "module not found," even though the plugin is installed.

**Why it happens:** `qml6glsink` registers the `GstGLVideoItem` QML type only when the plugin is instantiated (even as a throw-away element). QML type registration is a one-time side effect of plugin initialization.

**How to avoid:** In `main()`, call `gst_element_factory_make("qml6glsink", nullptr)` and immediately unref the result BEFORE calling `engine.load()`. This is the canonical pattern from the official GStreamer example.

### Pitfall 5: decodebin does not produce H.264 output for videotestsrc

**What goes wrong:** Phase 1 uses `videotestsrc` which produces raw video, not H.264. Routing `videotestsrc` through `decodebin` is a no-op or unnecessary. The decoder detection signal will not fire for test sources.

**Why it happens:** `decodebin` is designed to decode compressed formats. `videotestsrc` produces uncompressed video — `decodebin` passes it through.

**How to avoid:** For Phase 1 (test pipeline validation), use `videotestsrc ! videoconvert ! qml6glsink` directly. Decoder detection should be implemented as a separate task that exercises `decodebin` by encoding and decoding a test H.264 stream: `videotestsrc ! x264enc ! decodebin ! videoconvert ! qml6glsink`.

**Warning signs:** `element-added` callback fires but no decoder name matches the expected hardware decoder patterns.

### Pitfall 6: CMake presets needed for cross-platform Qt6 discovery

**What goes wrong:** On macOS, `find_package(Qt6)` fails because the Homebrew Qt6 install path (`/opt/homebrew/opt/qt@6`) is not in CMake's default search paths. On Windows with MSYS2, the Qt6 install is in a non-standard prefix.

**How to avoid:** Use `CMakePresets.json` with per-platform `cacheVariables`:
```json
{
  "configurePresets": [
    {
      "name": "macos-debug",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "/opt/homebrew/opt/qt@6;/opt/homebrew"
      }
    },
    {
      "name": "windows-msys2-debug",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": "C:/msys64/mingw64"
      }
    }
  ]
}
```

---

## Code Examples

Verified patterns from official sources:

### Minimal Phase 1 pipeline (test sources)
```cpp
// Source: GStreamer documentation patterns
// Pipeline: videotestsrc -> videoconvert -> qml6glsink
//           audiotestsrc -> audioconvert -> autoaudiosink
// (Two branches, one pipeline)

GstElement *pipeline      = gst_pipeline_new("airshow-pipeline");
GstElement *videoSrc      = gst_element_factory_make("videotestsrc",  "videosrc");
GstElement *videoConvert  = gst_element_factory_make("videoconvert",  "videoconvert");
GstElement *videoSink     = gst_element_factory_make("qml6glsink",    "videosink");
GstElement *audioSrc      = gst_element_factory_make("audiotestsrc",  "audiosrc");
GstElement *audioConvert  = gst_element_factory_make("audioconvert",  "audioconvert");
GstElement *audioSink     = gst_element_factory_make("autoaudiosink", "audiosink");

// Set the QML video item on the sink (must happen before PLAYING state)
g_object_set(videoSink, "widget", videoQmlItem, nullptr);

gst_bin_add_many(GST_BIN(pipeline),
    videoSrc, videoConvert, videoSink,
    audioSrc, audioConvert, audioSink,
    nullptr);

gst_element_link_many(videoSrc, videoConvert, videoSink, nullptr);
gst_element_link_many(audioSrc, audioConvert, audioSink, nullptr);

gst_element_set_state(pipeline, GST_STATE_PLAYING);
```

### Startup plugin availability check
```cpp
// Source: GStreamer registry API documentation
// Call this in main() before engine.load()
void checkRequiredPlugins() {
    struct { const char* name; const char* pkg; } required[] = {
        {"qml6glsink",   "gstreamer1.0-qt6"},
        {"videotestsrc", "gstreamer1.0-plugins-base"},
        {"audiotestsrc", "gstreamer1.0-plugins-base"},
        {"autoaudiosink","gstreamer1.0-plugins-good"},
        {"avdec_h264",   "gstreamer1.0-libav"},
    };
    for (auto& p : required) {
        if (!gst_registry_check_feature_version(
                gst_registry_get(), p.name, 1, 20, 0)) {
            qFatal("Missing GStreamer plugin '%s'. "
                   "Install package: %s", p.name, p.pkg);
        }
    }
}
```

### CMakePresets.json structure
```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "linux-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "macos-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/macos-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "/opt/homebrew/opt/qt@6;/opt/homebrew"
      }
    },
    {
      "name": "windows-msys2-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/windows-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "C:/msys64/mingw64"
      }
    }
  ]
}
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| qmlglsink (Qt5) | qml6glsink (Qt6) | GStreamer 1.21.3 (2022) | New element name and QML import path; old Qt5 element does not work with Qt6 |
| autovideosink (any backend) | qml6glsink (QML-integrated) | Qt6 era | autovideosink opens its own window; qml6glsink renders inside QML scene graph with shared GL context |
| Qt5 multimedia backend | Qt6 Multimedia (GStreamer backend on Linux) | Qt 6.4 | Qt6 Multimedia now uses GStreamer as its backend on Linux — but QMediaPlayer is still too opaque for custom pipelines |
| GStreamer 1.22 (previous LTS) | GStreamer 1.26 (current stable) | January 2025 | 1.28.0 was released Jan 2026 but is too new for stable distro packaging; 1.26.x is the safe choice |

**Deprecated/outdated:**
- `qmlglsink`: Qt5-only, use `qml6glsink` for Qt6 projects
- `OpenSSL 1.1.1`: EOL September 2023; system already has 3.5.3
- `Qt QMediaPlayer for custom pipeline output`: Cannot accept raw encoded frames; use `qml6glsink` directly

---

## Open Questions

1. **Windows: GStreamer MSVC installer vs. MSYS2 for release builds**
   - What we know: MSYS2 packages work for development. The official GStreamer MSVC installer bundles `gstqml6.dll` and is the standard for Windows release distribution.
   - What's unclear: Whether `gstqml6.dll` in the MSYS2 `mingw-w64-x86_64-gst-plugins-good` package correctly locates the Qt6 DLLs at runtime on a clean Windows machine.
   - Recommendation: In Phase 1, develop and test with MSYS2. Document that Windows release packaging requires the official MSVC installer or that `GST_PLUGIN_PATH` must point to the MSYS2 plugin directory.

2. **macOS: GStreamer Homebrew vs. official framework for qml6glsink Qt ABI matching**
   - What we know: The `gstreamer1.0-qt6` ABI-pin issue on Linux (pinned to Qt 6.9.2) is a Debian packaging concern. On macOS with Homebrew, `brew install gstreamer` and `brew install qt@6` should build against the same Qt.
   - What's unclear: Whether Homebrew's `gst-plugins-good` includes the qml6glsink plugin built against the Homebrew Qt6.
   - Recommendation: Verify in Phase 1 macOS testing by running `gst-inspect-1.0 qml6glsink` after installing both.

3. **Qt version: 6.8 LTS (project target) vs. 6.9.2 (system installed)**
   - What we know: The system has Qt 6.9.2; the `gstreamer1.0-qt6` package is compiled against Qt 6.9.2. The project's stated requirement is Qt 6.8 LTS.
   - What's unclear: Whether the CI matrix should test against Qt 6.8 specifically, or whether 6.9.2 is acceptable as the minimum.
   - Recommendation: Use 6.9.2 as the development baseline (it's what's installed and what GStreamer's Qt plugin is compiled against). Document Qt 6.8+ as the minimum requirement in README. The APIs used in Phase 1 exist in both 6.8 and 6.9.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| CMake | Build system (D-01) | Yes | 3.31.6 | — |
| Ninja | Fast builds | Yes | installed | make (slower) |
| Qt6 Core/Quick/Qml | QML window (D-09) | Yes | 6.9.2 | — |
| libgstreamer1.0-dev | Build: GStreamer headers | No | available in apt 1.26.6 | Must install |
| libgstreamer-plugins-base1.0-dev | Build: appsrc, videoconvert headers | No | available in apt 1.26.6 | Must install |
| libgstreamer-plugins-bad1.0-dev | Build: VAAPI/hardware decode headers | No | available in apt 1.26.5 | Must install |
| gstreamer1.0-qt6 | Runtime: qml6glsink plugin | No | available in apt 1.26.5 | Must install |
| libssl-dev | Build: OpenSSL headers (future phases) | Yes | 3.5.3 | — |
| spdlog (libspdlog-dev) | Logging | No | available in apt 1.15.3 | std::cerr (no package needed) |
| GStreamer runtime plugins-base/good/bad/libav | Runtime: codec and sink elements | Yes | 1.26.5–1.26.6 | — |
| git | Version control | Yes | 2.51.0 | — |
| clang-format | Code formatting | No | not installed | Install or skip for Phase 1 |
| vcpkg | Windows dependency management | No | not installed | MSYS2 packages (Linux/Windows dev) |

**Missing dependencies with no fallback (must install before building):**
- `libgstreamer1.0-dev` — GStreamer C headers are required for compilation
- `libgstreamer-plugins-base1.0-dev` — appsrc, videoconvert headers
- `gstreamer1.0-qt6` — provides `libgstqml6.so` (qml6glsink plugin)

**Missing dependencies with fallback:**
- `libspdlog-dev` — fallback to `g_message()`/`g_warning()` (GLib logging) or `qDebug()` if spdlog is not installed; spdlog is a quality-of-life improvement only
- `libgstreamer-plugins-bad1.0-dev` — only needed if code references VAAPI or D3D11 header APIs directly; for Phase 1 decoder detection, the element is referenced by name string, so this header can be deferred

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | CTest (bundled with CMake 3.31.6) + GoogleTest 1.17.0 (available in apt) |
| Config file | `tests/CMakeLists.txt` — does not exist yet (Wave 0 gap) |
| Quick run command | `cd build/linux-debug && ctest --output-on-failure -R smoke` |
| Full suite command | `cd build/linux-debug && ctest --output-on-failure` |

### Phase Requirements to Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| FOUND-01 | `cmake --build` succeeds on Linux without manual steps | smoke/build | `cmake --preset linux-debug && cmake --build build/linux-debug` | No — Wave 0 |
| FOUND-02 | GStreamer pipeline renders videotestsrc frames (qml6glsink receives buffers) | integration | `ctest -R test_video_pipeline` (checks pipeline enters PLAYING state) | No — Wave 0 |
| FOUND-03 | audiotestsrc plays through autoaudiosink (pipeline reaches PLAYING state) | integration | `ctest -R test_audio_pipeline` | No — Wave 0 |
| FOUND-04 | Mute toggle sets volume to 0; unmute restores to 1.0 | unit | `ctest -R test_mute_toggle` | No — Wave 0 |
| FOUND-05 | decodebin selects a decoder and logs its name; pipeline does not crash on a machine without VAAPI | integration | `ctest -R test_decoder_detection` | No — Wave 0 |

**Note on FOUND-01 (macOS/Windows):** Automated cross-platform build verification requires CI (GitHub Actions matrix). Phase 1 Wave 0 should include a minimal `.github/workflows/build.yml` targeting Linux initially, with macOS and Windows jobs as stretch goals. Manual verification on macOS/Windows is acceptable for Phase 1 if CI is not set up in this phase.

**Note on FOUND-02 (visual verification):** True "visible moving frames" requires a human to observe the window. The automated test verifies pipeline state (`GST_STATE_PLAYING`) and bus message absence of errors — not pixel content.

### Sampling Rate
- **Per task commit:** `cmake --build build/linux-debug && ctest --output-on-failure -R smoke -C Debug`
- **Per wave merge:** `cmake --build build/linux-debug && ctest --output-on-failure`
- **Phase gate:** Full suite green + manual fullscreen window observation before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/CMakeLists.txt` — CTest setup with GoogleTest
- [ ] `tests/test_pipeline.cpp` — smoke tests for FOUND-02, FOUND-03, FOUND-04, FOUND-05
- [ ] GTest install: `sudo apt install libgtest-dev` (available 1.17.0)
- [ ] CI stub: `.github/workflows/build.yml` — Linux build matrix job

---

## Project Constraints (from CLAUDE.md)

**Directives extracted from `CLAUDE.md` that the planner must honor:**

- **Cost:** Must be completely free — no freemium, no ads, no license keys. All chosen libraries must be open-source (GPL/LGPL/MIT/BSD). Qt 6 open-source edition is LGPL; GStreamer is LGPL; OpenSSL is Apache 2.0. All compatible.
- **Cross-platform:** Must work on Linux, macOS, and Windows from the same codebase. CMake + Ninja + presets is the chosen approach.
- **Network:** Local network only. Phase 1 has no network code — this constraint applies to future phases.
- **GSD Workflow:** All file changes must go through a GSD command (`/gsd:execute-phase`). No direct repo edits outside the workflow.
- **Conventions:** Not yet established — Phase 1 establishes them. The planner should document any conventions established (file naming, CMake target naming, GStreamer element naming) in `CLAUDE.md` at phase completion.

---

## Sources

### Primary (HIGH confidence)
- [GStreamer qml6glsink documentation](https://gstreamer.freedesktop.org/documentation/qml6/qml6glsink.html) — Integration pattern, QML item name, widget property
- Qt6's `FindGStreamer.cmake` at `/usr/lib/x86_64-linux-gnu/cmake/Qt6/FindGStreamer.cmake` — CMake imported target names (GStreamer::App, GStreamer::Video)
- `apt-cache show` on Linux dev machine (2026-03-28) — all version numbers and package availability
- [GStreamer decodebin3 documentation](https://gstreamer.freedesktop.org/documentation/playback/decodebin3.html) — Rank-based decoder selection
- [GStreamer element rank system](https://developer.ridgerun.com/wiki/index.php?title=GStreamer_modify_the_elements_rank) — GST_RANK_PRIMARY/SECONDARY/MARGINAL/NONE values

### Secondary (MEDIUM confidence)
- [ystreet00 blog: New Qt6 QML OpenGL elements](http://ystreet00.blogspot.com/2023/05/new-qt6-qml-opengl-elements.html) — Confirms qml6glsink operates same as Qt5 predecessor; references official examples
- [GStreamer Discourse: Running qmlsink in Qt6](https://discourse.gstreamer.org/t/running-the-standard-example-qmlsink-in-qt6/1921) — Confirms Windows needs GST_PLUGIN_PATH; debug/release ABI mismatch issue documented
- [Qt Forum: Qt6 + GStreamer + CMake](https://forum.qt.io/topic/161751/qt6-gstreamer-cmake) — pkg-config approach confirmed by community

### Tertiary (LOW confidence)
- [GStreamer Discourse: qml6glsink missing on Windows](https://discourse.gstreamer.org/t/qml6glsink-missing-for-gstreamer-1-22-9-on-windows/892) — Windows qml6glsink availability concerns (GStreamer 1.22, may be resolved in 1.26)

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — versions confirmed via `apt-cache` and `pkg-config` on dev machine
- Architecture: HIGH — based on official GStreamer example patterns and locked decisions in CONTEXT.md
- Pitfalls: HIGH — Pitfall 1 (missing package) verified by direct apt probe; others from PITFALLS.md (HIGH confidence source)
- Environment availability: HIGH — directly probed on the Linux dev machine

**Research date:** 2026-03-28
**Valid until:** 2026-06-28 (stable libraries; GStreamer 1.26 series will remain current for several months)
