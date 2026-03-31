# Phase 5: DLNA - Research

**Researched:** 2026-03-28
**Domain:** UPnP/DLNA Digital Media Renderer (DMR) with libupnp (pupnp) + GStreamer uridecodebin
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Create `DlnaHandler : ProtocolHandler` in `src/protocol/DlnaHandler.h` — follows established AirPlayHandler pattern. Owns SOAP action dispatch, GStreamer pipeline lifecycle for DLNA sessions, and session state.
- **D-02:** `UpnpAdvertiser` stays focused on SSDP discovery. Route SOAP action events from `UpnpAdvertiser::upnpCallback` to `DlnaHandler` for processing (replace the current 501 stub).
- **D-03:** `DlnaHandler` is registered with `ProtocolManager` via `addHandler()` like AirPlayHandler.
- **D-04:** Use GStreamer `uridecodebin` for DLNA media playback. DLNA controllers send a media URI via SetAVTransportURI; `uridecodebin` handles HTTP fetch, container demux, format detection, and codec selection automatically.
- **D-05:** `uridecodebin` replaces `appsrc` for DLNA — unlike AirPlay (which pushes raw frames), DLNA provides a URL that GStreamer fetches directly. The pipeline switches from appsrc-based to URI-based when a DLNA session is active.
- **D-06:** Use the same `autoaudiosink` and video sink chain from the shared pipeline for output.
- **D-07:** Advertise broad format support in SinkProtocolInfo — `uridecodebin` handles format detection via GStreamer's plugin registry, so no extra code per format:
  - Video: `video/mp4`, `video/mpeg`, `video/x-matroska`, `video/avi`
  - Audio: `audio/mpeg` (MP3), `audio/mp4` (AAC), `audio/flac`, `audio/wav`, `audio/x-ms-wma`
  - Transport: `http-get:*:mime:*` DLNA protocol info format
- **D-08:** Implement full AVTransport:1 action set: SetAVTransportURI, Play, Stop, Pause, Seek, GetTransportInfo, GetPositionInfo, GetMediaInfo.
- **D-09:** Implement RenderingControl:1 actions: SetVolume, GetVolume, SetMute, GetMute.
- **D-10:** Implement ConnectionManager:1 actions: GetProtocolInfo.
- **D-11:** Create SCPD XML files: `avt-scpd.xml`, `rc-scpd.xml`, `cm-scpd.xml`. Serve via libupnp's built-in HTTP server.
- **D-12:** Single-session model: one DLNA playback at a time; new SetAVTransportURI replaces current playback.
- **D-13:** Session events routed to `ConnectionBridge` for HUD display — shows "DLNA" as protocol and controller name if available from metadata.
- **D-14:** Clean teardown: Stop pipeline, clear connection state, return to idle screen on Stop or controller disconnect.

### Claude's Discretion

- Internal threading model for SOAP action handling (libupnp callback thread vs Qt event loop)
- Exact GStreamer pipeline element chain for uridecodebin output
- SCPD XML content details (state variable defaults, allowed value ranges)
- UPnP eventing implementation (GENA subscription for LastChange notifications)
- Error handling for unsupported codecs or unreachable media URLs
- Whether to use playbin vs custom uridecodebin pipeline

### Deferred Ideas (OUT OF SCOPE)

None — discussion stayed within phase scope

</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| DLNA-01 | User can push video files from a DLNA controller to AirShow for playback | D-04 (uridecodebin), D-08 (AVTransport), D-05 (URI pull model) |
| DLNA-02 | User can push audio files from a DLNA controller to AirShow for playback | D-04 (uridecodebin handles audio containers), D-08 (AVTransport), D-07 (SinkProtocolInfo audio MIME types) |
| DLNA-03 | AirShow appears as a DLNA Media Renderer (DMR) in DLNA controller apps | D-11 (SCPD XMLs must exist and be served), D-08/D-09/D-10 (all three required services implemented) |
</phase_requirements>

---

## Summary

Phase 5 replaces the 501-stub SOAP callback in `UpnpAdvertiser` with real DLNA Digital Media Renderer logic. The core work is: (1) routing libupnp SOAP callbacks to a new `DlnaHandler`; (2) implementing AVTransport:1, RenderingControl:1, and ConnectionManager:1 SOAP action handlers; (3) creating and serving the three required SCPD XML files; and (4) adding a `MediaPipeline::initUriPipeline()` mode that uses `uridecodebin` instead of `appsrc` to play HTTP URIs provided by DLNA controllers.

The DLNA protocol is a "pull" model: the controller sends a URL, the renderer fetches and plays it. This is fundamentally simpler than AirPlay's push model — there is no raw frame injection, no crypto, and no NTP sync work. GStreamer's `uridecodebin` element handles all the complexity of HTTP download, container demuxing, codec detection, and hardware decoder selection with a single `uri` property set.

The primary engineering challenge is threading: libupnp calls its callback from an internal thread pool, not from Qt's main thread. All GStreamer state changes and Qt signal emissions must be marshalled via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` or mutexed C++ state. The SCPD XML files are also mandatory — a DMR that lacks them will be rejected by strict controllers (BubbleUPnP validates them before sending any actions).

**Primary recommendation:** Implement DlnaHandler as a thin dispatcher (no Qt inheritance needed) with a file-scope C trampoline for the libupnp callback (matching the AirPlayHandler pattern). Add `initUriPipeline(void* qmlVideoItem)` to MediaPipeline for URI-based playback. Serve SCPD files by placing them in the same temp directory as the runtime device XML — libupnp's built-in HTTP server serves any file placed under its registered root.

---

## Standard Stack

### Core (Already in Project)

| Library | Version | Purpose | Status |
|---------|---------|---------|--------|
| **pupnp / libupnp** | 1.14.24 (vendored in `/tmp/libupnp-dev`) | SOAP action dispatching, built-in HTTP server for SCPD files | Already linked via `PkgConfig::UPNP` |
| **GStreamer** | 1.26.6 (system) | Media decoding via `uridecodebin` + video/audio sink chain | Already linked |
| **Qt 6.8 LTS** | 6.8.x | Thread marshalling via `QMetaObject::invokeMethod`, `QObject` signals | Already linked |

### New Usage Patterns (No New Dependencies)

| Element | Purpose | Notes |
|---------|---------|-------|
| `uridecodebin` | URI-based media playback | Ships with `gst-plugins-base`. Handles HTTP fetch + demux + decode internally. |
| `UpnpActionRequest_get_ActionName()` | Extract SOAP action name in callback | Header: `<upnp/upnp.h>` → `<upnp/UpnpActionRequest.h>` |
| `UpnpActionRequest_get_ActionRequest()` | Get IXML_Document of incoming arguments | Part of existing libupnp includes |
| `UpnpAddToActionResponse()` | Build SOAP response IXML_Document | `<upnp/upnptools.h>` — already included in `UpnpAdvertiser.cpp` |
| `UpnpActionRequest_set_ActionResult()` | Set response on the event struct | Called after `UpnpAddToActionResponse` builds result |
| `UpnpActionRequest_set_ErrCode()` | Return UPnP error code (e.g., 501, 718) | Used for unsupported or out-of-range actions |

**No new packages to install.** All required libraries are already present.

---

## Architecture Patterns

### Recommended Project Structure (New Files)

```
src/
├── protocol/
│   ├── DlnaHandler.h          # NEW: ProtocolHandler implementation for DLNA
│   └── DlnaHandler.cpp        # NEW: SOAP dispatch, pipeline lifecycle
├── pipeline/
│   └── MediaPipeline.h/.cpp   # MODIFY: add initUriPipeline(), playUri(), seek(), getPosition()
resources/dlna/
├── MediaRenderer.xml          # EXISTS (no change needed)
├── avt-scpd.xml               # NEW: AVTransport:1 service description
├── rc-scpd.xml                # NEW: RenderingControl:1 service description
└── cm-scpd.xml                # NEW: ConnectionManager:1 service description
tests/
└── test_dlna.cpp              # NEW: DlnaHandler unit tests
```

### Pattern 1: File-Scope C Trampoline for libupnp Callback

libupnp's `UpnpRegisterRootDevice` requires a C function pointer (`Upnp_FunPtr`). `UpnpAdvertiser` already uses a `static` class method. For Phase 5, `DlnaHandler` owns the SOAP action logic but cannot be passed directly to `UpnpRegisterRootDevice`. The solution is to thread a `DlnaHandler*` through the cookie parameter.

**Problem:** `UpnpRegisterRootDevice` is called in `UpnpAdvertiser::start()` with `cookie = nullptr`. Phase 5 must change that to `cookie = dlnaHandler`.

**Correct approach:** `UpnpAdvertiser` receives a `DlnaHandler*` in its constructor (or via a `setDlnaHandler()` setter). `upnpCallback` casts the cookie back to `DlnaHandler*` and calls `handler->handleSoapAction(eventType, event)`.

```cpp
// UpnpAdvertiser.cpp (modified)
// static
int UpnpAdvertiser::upnpCallback(Upnp_EventType_e eventType,
                                 const void* event,
                                 void* cookie) {
    if (eventType == UPNP_CONTROL_ACTION_REQUEST && cookie) {
        auto* handler = static_cast<airshow::DlnaHandler*>(cookie);
        return handler->handleSoapAction(event);
    }
    return UPNP_E_SUCCESS;
}
```

Pass `dlnaHandler` as cookie when registering:
```cpp
ret = UpnpRegisterRootDevice(
    m_runtimeXmlPath.c_str(),
    &UpnpAdvertiser::upnpCallback,
    m_dlnaHandler,   // cookie — was nullptr in Phase 2
    &m_deviceHandle
);
```

**Confidence:** HIGH — matches file-scope trampoline pattern used for UxPlay callbacks in AirPlayHandler (per `STATE.md` decision: "File-scope C trampolines for raop_callbacks_t").

### Pattern 2: SOAP Action Name Dispatch

```cpp
// DlnaHandler.cpp — inside handleSoapAction()
int DlnaHandler::handleSoapAction(const void* event) {
    const auto* req = static_cast<const UpnpActionRequest*>(event);

    const char* actionName =
        UpnpString_get_String(UpnpActionRequest_get_ActionName(req));
    const char* serviceId =
        UpnpString_get_String(UpnpActionRequest_get_ServiceID(req));

    IXML_Document* result = nullptr;
    int errCode = UPNP_E_SUCCESS;

    if (strcmp(actionName, "SetAVTransportURI") == 0) {
        errCode = onSetAVTransportURI(req, &result);
    } else if (strcmp(actionName, "Play") == 0) {
        errCode = onPlay(req, &result);
    } else if (strcmp(actionName, "Stop") == 0) {
        errCode = onStop(req, &result);
    } else if (strcmp(actionName, "Pause") == 0) {
        errCode = onPause(req, &result);
    } else if (strcmp(actionName, "Seek") == 0) {
        errCode = onSeek(req, &result);
    } else if (strcmp(actionName, "GetTransportInfo") == 0) {
        errCode = onGetTransportInfo(req, &result);
    } else if (strcmp(actionName, "GetPositionInfo") == 0) {
        errCode = onGetPositionInfo(req, &result);
    } else if (strcmp(actionName, "GetMediaInfo") == 0) {
        errCode = onGetMediaInfo(req, &result);
    } else if (strcmp(actionName, "SetVolume") == 0) {
        errCode = onSetVolume(req, &result);
    } else if (strcmp(actionName, "GetVolume") == 0) {
        errCode = onGetVolume(req, &result);
    } else if (strcmp(actionName, "SetMute") == 0) {
        errCode = onSetMute(req, &result);
    } else if (strcmp(actionName, "GetMute") == 0) {
        errCode = onGetMute(req, &result);
    } else if (strcmp(actionName, "GetProtocolInfo") == 0) {
        errCode = onGetProtocolInfo(req, &result);
    } else {
        // Unknown action — return 401 Invalid Action
        UpnpActionRequest_set_ErrCode(
            const_cast<UpnpActionRequest*>(req), 401);
        return UPNP_E_SUCCESS;  // libupnp expects UPNP_E_SUCCESS even for error responses
    }

    if (result) {
        UpnpActionRequest_set_ActionResult(
            const_cast<UpnpActionRequest*>(req), result);
    }
    if (errCode != UPNP_E_SUCCESS) {
        UpnpActionRequest_set_ErrCode(
            const_cast<UpnpActionRequest*>(req), errCode);
    }
    return UPNP_E_SUCCESS;  // Always return UPNP_E_SUCCESS; error is in ErrCode field
}
```

**Confidence:** HIGH — verified against libupnp tv_device sample code pattern.

### Pattern 3: Reading SOAP Input Arguments from IXML_Document

```cpp
// Extract a named argument from the action request XML
// Source: BelledonneCommunications/libupnp sample_util.c pattern
static std::string getArgValue(IXML_Document* actionDoc,
                               const char* argName) {
    IXML_NodeList* nodeList =
        ixmlDocument_getElementsByTagName(actionDoc, argName);
    if (!nodeList) return {};
    IXML_Node* node = ixmlNodeList_item(nodeList, 0);
    ixmlNodeList_free(nodeList);
    if (!node) return {};
    IXML_Node* textNode = ixmlNode_getFirstChild(node);
    if (!textNode) return {};
    const DOMString val = ixmlNode_getNodeValue(textNode);
    return val ? std::string(val) : std::string{};
}

// Usage in SetAVTransportURI handler:
int DlnaHandler::onSetAVTransportURI(const UpnpActionRequest* req,
                                     IXML_Document** result) {
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(req);
    std::string uri = getArgValue(actionDoc, "CurrentURI");
    // uri now holds the media URL from the controller
    // ...
    UpnpAddToActionResponse(result, "SetAVTransportURIResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}
```

**Confidence:** HIGH — verified pattern from libupnp sample_util.c.

### Pattern 4: Building SOAP Responses with UpnpAddToActionResponse

```cpp
// GetTransportInfo response (returns current transport state)
int DlnaHandler::onGetTransportInfo(const UpnpActionRequest* /*req*/,
                                    IXML_Document** result) {
    const char* state = transportStateString();  // "PLAYING", "PAUSED_PLAYBACK", "STOPPED"
    *result = nullptr;
    UpnpAddToActionResponse(result, "GetTransportInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "CurrentTransportState", state);
    UpnpAddToActionResponse(result, "GetTransportInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "CurrentTransportStatus", "OK");
    UpnpAddToActionResponse(result, "GetTransportInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "CurrentSpeed", "1");
    return UPNP_E_SUCCESS;
}
```

`UpnpAddToActionResponse` signature:
```c
int UpnpAddToActionResponse(
    IXML_Document** response,  // built up iteratively; pass nullptr on first call
    const char* actionName,    // "GetTransportInfoResponse" (note: Response suffix)
    const char* serviceType,   // "urn:schemas-upnp-org:service:AVTransport:1"
    const char* argName,       // output parameter name
    const char* argValue       // output parameter value as string
);
```

**Confidence:** HIGH — verified against pupnp tv_device.c sample and upnptools.h header.

### Pattern 5: GStreamer URI Pipeline (uridecodebin)

DLNA is a pull model. GStreamer `uridecodebin` handles HTTP fetch, demux, and decode in one element. MediaPipeline needs a new method:

```cpp
// MediaPipeline.h — new method
bool initUriPipeline(void* qmlVideoItem);  // builds uridecodebin pipeline
void setUri(const std::string& uri);       // sets uri property and prepares pipeline
void play();                               // transitions to PLAYING
void pause();                              // transitions to PAUSED
void stopPlayback();                       // transitions to NULL, clears URI
gint64 getPosition() const;               // gst_element_query_position result in nanoseconds
gint64 getDuration() const;               // gst_element_query_duration result in nanoseconds
void seekTo(gint64 positionNs);           // gst_element_seek_simple
```

Pipeline structure:
```
uridecodebin (uri=<controller URL>)
    |-- video pad --> videoconvert ! glupload ! qml6glsink  (or fakesink headless)
    \-- audio pad --> audioconvert ! audioresample ! autoaudiosink
```

`uridecodebin` emits `pad-added` for each decoded stream. The `pad-added` callback (identical pattern to existing decodebin pad callbacks in `initAppsrcPipeline`) connects audio/video pads to the appropriate sinks.

Key `uridecodebin` properties:
- `uri` — set before transitioning to PAUSED/PLAYING
- `buffer-duration` — default is fine for local-network HTTP streaming
- `connection-speed` — can hint at available bandwidth (leave at default)

**Important:** Set `uri` BEFORE setting pipeline to `GST_STATE_PAUSED`. Changing `uri` on a running pipeline requires stopping to NULL first, changing uri, then re-starting.

```cpp
// Usage pattern in DlnaHandler::onSetAVTransportURI
void DlnaHandler::onSetAVTransportURI(...) {
    // 1. Stop current playback if any
    QMetaObject::invokeMethod(this, [this, uri]() {
        m_pipeline->stopPlayback();
        m_pipeline->setUri(uri);
    }, Qt::QueuedConnection);
}

// onPlay — transitions pipeline to PLAYING
void DlnaHandler::onPlay(...) {
    QMetaObject::invokeMethod(this, [this]() {
        m_pipeline->play();
        m_connectionBridge->setConnected(true, "DLNA Controller", "DLNA");
    }, Qt::QueuedConnection);
}
```

**Confidence:** HIGH — uridecodebin is the standard GStreamer URI-based decode element (gst-plugins-base 1.26.6 confirmed present). Pattern is directly analogous to existing `initAppsrcPipeline` pad-added usage in `MediaPipeline.cpp`.

### Pattern 6: Threading — libupnp Callback to Qt/GStreamer

libupnp invokes `upnpCallback` from its own internal thread pool, not from Qt's main thread. GStreamer state changes must happen on the correct thread.

**Rule:** Never call `gst_element_set_state()` directly from within the libupnp callback. Use `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to queue work onto Qt's main event loop.

`DlnaHandler` must inherit from `QObject` to use `invokeMethod` with `Qt::QueuedConnection` and lambda capture.

```cpp
// DlnaHandler.h
class DlnaHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
    // ...
};
```

The `Q_OBJECT` macro is needed. Add `DlnaHandler.cpp` to `qt_add_executable` source list in `CMakeLists.txt` (same as AirPlayHandler).

**Confidence:** HIGH — QMetaObject::invokeMethod with Qt::QueuedConnection is documented as thread-safe. This is the established cross-thread signalling pattern in the existing codebase (ConnectionBridge uses it implicitly via Qt signals).

### Pattern 7: SCPD Files — Content and Serving

MediaRenderer.xml already declares the three SCPD URLs:
- `/avt-scpd.xml`
- `/rc-scpd.xml`
- `/cm-scpd.xml`

libupnp's built-in HTTP server serves files from the directory containing the registered device XML. Since `UpnpAdvertiser` already writes `airshow_dlna.xml` to `QDir::tempPath()`, placing the three SCPD files in the **same directory** (`QDir::tempPath()`) before calling `UpnpRegisterRootDevice` makes them automatically available.

Alternatively, copy them at startup to the same temp path as the runtime XML.

**SCPD file structure (minimal but valid):**

```xml
<?xml version="1.0" encoding="utf-8"?>
<scpd xmlns="urn:schemas-upnp-org:service-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <actionList>
    <action>
      <name>SetAVTransportURI</name>
      <argumentList>
        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>
        <argument><name>CurrentURI</name><direction>in</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>
        <argument><name>CurrentURIMetaData</name><direction>in</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>
      </argumentList>
    </action>
    <!-- ... more actions ... -->
  </actionList>
  <serviceStateTable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>
    <!-- ... -->
  </serviceStateTable>
</scpd>
```

**Required SCPD actions per service (from UPnP spec):**

**avt-scpd.xml (AVTransport:1):** SetAVTransportURI, Play, Stop, Pause, Seek, GetTransportInfo, GetPositionInfo, GetMediaInfo, GetDeviceCapabilities, GetTransportSettings, GetCurrentTransportActions

**rc-scpd.xml (RenderingControl:1):** SetVolume, GetVolume, SetMute, GetMute, ListPresets, SelectPreset

**cm-scpd.xml (ConnectionManager:1):** GetProtocolInfo, GetCurrentConnectionInfo, GetCurrentConnectionIDs

**Important:** ListPresets/SelectPreset (RenderingControl) and GetCurrentConnectionInfo/GetCurrentConnectionIDs (ConnectionManager) can return minimal/empty responses — controllers call them on discovery but do not depend on them for basic playback.

**Confidence:** HIGH — verified against WDTV Live SCPD reference (airpnp repo) and UPnP AV specification.

### Pattern 8: Serving SCPD Files via libupnp HTTP Server

libupnp's HTTP server serves files from the directory it is initialized with. The device XML is registered via `UpnpRegisterRootDevice(path, ...)`. The server root is implicitly the directory containing that XML file.

Since `UpnpAdvertiser::writeRuntimeXml()` writes to `QDir::tempPath() + "/airshow_dlna.xml"`, place SCPD files at:
- `QDir::tempPath() + "/avt-scpd.xml"`
- `QDir::tempPath() + "/rc-scpd.xml"`
- `QDir::tempPath() + "/cm-scpd.xml"`

Copy these from `resources/dlna/` at build time (same `configure_file` pattern used for `MediaRenderer.xml` in `CMakeLists.txt`), then copy from `CMAKE_BINARY_DIR` to `QDir::tempPath()` at runtime startup, or write them inline from static strings in `UpnpAdvertiser::start()`.

**Simplest approach:** Write SCPD content from static C++ string literals in `UpnpAdvertiser::start()` alongside `writeRuntimeXml()` — no file copy required, no dependency on build output directory at runtime.

**Confidence:** MEDIUM — libupnp documentation on web server root is sparse but the temp-dir approach is confirmed by UxPlay and gupnp device implementations. Verified via `UpnpSetWebServerRootDir` documentation noting files must be under the registered root.

### Anti-Patterns to Avoid

- **Calling gst_element_set_state() directly from upnpCallback thread** — crashes or deadlocks Qt event loop. Always use `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- **Returning non-UPNP_E_SUCCESS from upnpCallback** — libupnp sends a transport error instead of a SOAP error. Always return `UPNP_E_SUCCESS`; use `UpnpActionRequest_set_ErrCode()` to convey SOAP-level errors.
- **Passing the ActionResult to set_ActionResult before calling UpnpAddToActionResponse** — `UpnpAddToActionResponse` takes a `IXML_Document**` and builds it; pass nullptr on first call per action, then assign result to the event struct afterward.
- **Omitting SCPD XML files** — BubbleUPnP, foobar2000, and Kodi all fetch SCPD files before sending actions; a missing SCPD causes the controller to skip or reject the renderer.
- **Changing uridecodebin uri without stopping the pipeline first** — must set state to GST_STATE_NULL, set the new uri, then start PAUSED → PLAYING. Changing uri mid-stream causes element confusion.
- **Using QMediaPlayer instead of uridecodebin** — `QMediaPlayer` cannot share the pipeline with the existing qml6glsink video item. Stick to `uridecodebin` feeding the same sink chain.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| HTTP media fetch from controller URL | Custom HTTP downloader | `uridecodebin` (sets `uri` property) | uridecodebin handles HTTP, HTTPS, local file, and network redirects automatically |
| Container demux for MP4/MKV/AVI | Custom demuxer | `uridecodebin` internal autoplugging | GStreamer's plugin registry selects the right demuxer at runtime |
| Audio/video codec selection | Per-codec dispatch logic | `uridecodebin` + GStreamer plugin system | Hardware decoder selection (vaapi, d3d11, vtdec) is automatic |
| SOAP XML response construction | Manual XML string building | `UpnpAddToActionResponse()` + iXML API | libupnp builds well-formed SOAP envelopes; hand-built strings cause namespace errors |
| SOAP XML argument parsing | Custom XML parser | `ixmlDocument_getElementsByTagName()` | Already in libupnp; thread-safe; handles encoding correctly |
| UPnP error responses | Custom HTTP 500 logic | `UpnpActionRequest_set_ErrCode()` | libupnp translates ErrCode to proper UPnP fault envelopes |

**Key insight:** DLNA's pull model means the renderer does almost nothing except tell GStreamer a URL and let it play. The complexity budget goes to SOAP dispatch and SCPD correctness, not media handling.

---

## Common Pitfalls

### Pitfall 1: libupnp Callback Thread — GStreamer State Change Crash

**What goes wrong:** `DlnaHandler::handleSoapAction` is called by libupnp on an internal thread. If it calls `gst_element_set_state(m_pipeline, GST_STATE_PLAYING)` directly, GStreamer may detect cross-thread access and crash or deadlock, because the Qt/GStreamer pipeline was created on the main thread.

**Why it happens:** libupnp uses a thread pool for HTTP and callback dispatch. GStreamer's state machine is not thread-unsafe per se, but Qt's event loop objects (used in the pipeline through qml6glsink) are strictly single-threaded.

**How to avoid:** Queue all GStreamer state changes onto Qt's main thread:
```cpp
QMetaObject::invokeMethod(this, [this]() {
    gst_element_set_state(m_pipeline->gstPipeline(), GST_STATE_PLAYING);
}, Qt::QueuedConnection);
```
`DlnaHandler` must inherit `QObject` for this to compile. This is the correct design and requires no mutex.

**Warning signs:** Segfault or assert in `gst_element_sync_state_with_parent` or `gst_object_unref` when Play is called shortly after SetAVTransportURI.

### Pitfall 2: SCPD Files Not Found — Controller Silently Skips Renderer

**What goes wrong:** BubbleUPnP or Kodi discovers the renderer via SSDP, fetches `MediaRenderer.xml`, tries to fetch `/avt-scpd.xml` and gets a 404. The controller considers the device incomplete and does not display it or rejects actions.

**Why it happens:** The SCPD URLs are declared in `MediaRenderer.xml` but the files are not written to disk before `UpnpRegisterRootDevice` is called. libupnp's HTTP server will return 404 for any file not present in its web root.

**How to avoid:** Write all three SCPD files to `QDir::tempPath()` before calling `UpnpRegisterRootDevice`. The simplest approach is writing them from inline static strings in `UpnpAdvertiser::start()` immediately after `writeRuntimeXml()`.

**Warning signs:** Controller can discover the renderer but shows it as grayed out or with a warning icon. Wireshark shows HTTP GET `/avt-scpd.xml` → 404.

### Pitfall 3: UpnpAddToActionResponse on Already-Populated Document

**What goes wrong:** `UpnpAddToActionResponse` is called multiple times for a multi-argument response, but each call is passed a fresh `nullptr` instead of the document built by the previous call. Result: only the last argument appears in the response.

**Why it happens:** The function signature takes `IXML_Document**` and builds the document if `*response == nullptr`, otherwise appends. Must thread the same pointer across multiple calls.

**How to avoid:**
```cpp
IXML_Document* result = nullptr;
UpnpAddToActionResponse(&result, "GetTransportInfoResponse", svcType, "CurrentTransportState", state);
UpnpAddToActionResponse(&result, "GetTransportInfoResponse", svcType, "CurrentTransportStatus", "OK");
UpnpAddToActionResponse(&result, "GetTransportInfoResponse", svcType, "CurrentSpeed", "1");
// Now set result on the event
UpnpActionRequest_set_ActionResult(const_cast<UpnpActionRequest*>(req), result);
```

**Warning signs:** Controller receives a response with only one out-argument instead of the full set. BubbleUPnP shows "Transport state: " with empty value.

### Pitfall 4: uridecodebin URI Change Without Pipeline Reset

**What goes wrong:** SetAVTransportURI is called while media is already playing. The new URI is set via `g_object_set(uridecodebin, "uri", ...)` without stopping the pipeline first. GStreamer internally tries to re-autoplugin mid-stream, often resulting in a hang or error.

**Why it happens:** `uridecodebin` creates internal source and demux elements dynamically based on the URI at pipeline preroll time. Changing the URI on a running pipeline does not tear down and rebuild those internals.

**How to avoid:** On every `SetAVTransportURI`, always:
1. `gst_element_set_state(pipeline, GST_STATE_NULL)` — stops and destroys internal uridecodebin elements
2. `g_object_set(uridecodebin, "uri", newUri, nullptr)`
3. `gst_element_set_state(pipeline, GST_STATE_PAUSED)` — re-prerolls with new URI (buffers, does not play)
4. When `Play` arrives: `gst_element_set_state(pipeline, GST_STATE_PLAYING)`

**Warning signs:** Second video plays from the beginning momentarily then freezes; or error message "no pads could be decoded" in GStreamer debug output.

### Pitfall 5: Seek Position Format (REL_TIME vs ABS_TIME vs X_DLNA_REL_BYTE)

**What goes wrong:** DLNA controller sends `Seek` with `Unit=REL_TIME` and `Target=0:02:30` (HH:MM:SS format). Handler attempts to use the string as a nanosecond value and passes garbage to `gst_element_seek_simple`.

**Why it happens:** UPnP AVTransport Seek target for REL_TIME is formatted as `H+:MM:SS` (hours, minutes, seconds as a string), not as raw milliseconds or nanoseconds.

**How to avoid:** Parse the time string before passing to GStreamer:
```cpp
// Parse "H:MM:SS" to nanoseconds
gint64 parseTimeString(const std::string& t) {
    int h=0, m=0, s=0;
    sscanf(t.c_str(), "%d:%d:%d", &h, &m, &s);
    return (gint64)(h * 3600 + m * 60 + s) * GST_SECOND;
}
// Then: gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
//           GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, posNs)
```

**Warning signs:** After Seek, playback jumps to an unexpected position. GStreamer `gst_element_seek_simple` with a very large or zero nanosecond value.

### Pitfall 6: GetPositionInfo Returns Zero Duration

**What goes wrong:** Controller calls `GetPositionInfo` before uridecodebin has prerolled. `gst_element_query_duration` returns FALSE or -1, and the response sends `TrackDuration = "0:00:00"`. Some controllers interpret this as "not supported" and disable seek UI.

**Why it happens:** Duration is not known until the source has been fetched and the demuxer has read the container header. This can take 500ms–3s for remote HTTP content.

**How to avoid:** Return `"NOT_IMPLEMENTED"` for TrackDuration if duration query fails (or returns -1). Strict UPnP compliance allows this. BubbleUPnP handles it gracefully; foobar2000 also accepts it.
```cpp
gint64 dur = -1;
gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur);
std::string durStr = (dur > 0) ? formatGstTime(dur) : "NOT_IMPLEMENTED";
```

**Warning signs:** Seek slider never appears in controller app even after video is visibly playing.

### Pitfall 7: VPN Interface Binding (Pre-existing, Now Relevant to DLNA Actions)

`STATE.md` documents: "Note: if VPN is active, this may bind to the wrong interface (Pitfall 4)." SSDP discovery already works (Phase 2). DLNA action SOAP requests come over TCP to the same interface libupnp is bound to. If VPN is active, action requests from the controller on the LAN may never reach the handler. This is a pre-existing constraint — no new work needed, but document for user-facing troubleshooting.

---

## Code Examples

### Setting Up DlnaHandler Constructor

```cpp
// DlnaHandler.h
#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <string>
#include <mutex>

// Forward declarations
struct Upnp_Action_Request;  // or include <upnp/upnp.h> in .cpp only

namespace airshow {

class MediaPipeline;
class ConnectionBridge;

class DlnaHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    DlnaHandler(ConnectionBridge* connectionBridge);
    ~DlnaHandler() override;

    // ProtocolHandler interface
    bool start() override;
    void stop() override;
    std::string name() const override { return "dlna"; }
    bool isRunning() const override { return m_running; }
    void setMediaPipeline(MediaPipeline* pipeline) override;

    // Called by UpnpAdvertiser::upnpCallback trampoline
    // Returns UPNP_E_SUCCESS always; errors go through ErrCode.
    int handleSoapAction(const void* event);

private:
    // Per-action handlers — each returns UPNP_E_SUCCESS or a UPnP error code
    int onSetAVTransportURI(const void* req, void** result);
    int onPlay(const void* req, void** result);
    int onStop(const void* req, void** result);
    int onPause(const void* req, void** result);
    int onSeek(const void* req, void** result);
    int onGetTransportInfo(const void* req, void** result);
    int onGetPositionInfo(const void* req, void** result);
    int onGetMediaInfo(const void* req, void** result);
    int onSetVolume(const void* req, void** result);
    int onGetVolume(const void* req, void** result);
    int onSetMute(const void* req, void** result);
    int onGetMute(const void* req, void** result);
    int onGetProtocolInfo(const void* req, void** result);

    enum class TransportState { STOPPED, PLAYING, PAUSED, TRANSITIONING };
    const char* transportStateString() const;

    MediaPipeline*    m_pipeline         = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    bool              m_running          = false;
    TransportState    m_transportState   = TransportState::STOPPED;
    std::string       m_currentUri;
    std::mutex        m_stateMutex;  // protects m_transportState, m_currentUri
};

} // namespace airshow
```

Note: libupnp headers (`<upnp/upnp.h>`) are included in `.cpp` only to avoid polluting the header namespace — same pattern as AirPlayHandler with UxPlay types.

### MediaPipeline URI Pipeline Addition

```cpp
// MediaPipeline.h additions
bool initUriPipeline(void* qmlVideoItem);  // uridecodebin-based pipeline
void setUri(const std::string& uri);       // g_object_set uri property
void playUri();                            // GST_STATE_PLAYING
void pauseUri();                           // GST_STATE_PAUSED
void stopUri();                            // GST_STATE_NULL
gint64 queryPosition() const;             // ns, or -1 if not available
gint64 queryDuration() const;             // ns, or -1 if not available
void seekUri(gint64 positionNs);           // gst_element_seek_simple

private:
GstElement* m_uriDecodebin  = nullptr;    // kept for uri property access
```

### SCPD File Writing (Inline Static Strings)

```cpp
// In UpnpAdvertiser::start(), after writeRuntimeXml():
static const char* kAvtScpd = R"xml(
<?xml version="1.0" encoding="utf-8"?>
<scpd xmlns="urn:schemas-upnp-org:service-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <actionList>
    <action><name>SetAVTransportURI</name><argumentList>
      <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>
      <argument><name>CurrentURI</name><direction>in</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>
      <argument><name>CurrentURIMetaData</name><direction>in</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>
    </argumentList></action>
    <action><name>Play</name><argumentList>
      <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>
      <argument><name>Speed</name><direction>in</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>
    </argumentList></action>
    <!-- Stop, Pause, Seek, GetTransportInfo, GetPositionInfo, GetMediaInfo,
         GetDeviceCapabilities, GetTransportSettings, GetCurrentTransportActions -->
  </actionList>
  <serviceStateTable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>TransportPlaySpeed</name><dataType>string</dataType><allowedValueList><allowedValue>1</allowedValue></allowedValueList></stateVariable>
    <stateVariable sendEvents="yes"><name>LastChange</name><dataType>string</dataType></stateVariable>
  </serviceStateTable>
</scpd>)xml";

writeScpdFile(QDir::tempPath().toStdString() + "/avt-scpd.xml", kAvtScpd);
// ... similarly for rc-scpd.xml and cm-scpd.xml
```

### GStreamer Seek Pattern

```cpp
// Source: GStreamer documentation — gst_element_seek_simple
void MediaPipeline::seekUri(gint64 positionNs) {
    if (!m_pipeline || positionNs < 0) return;
    gst_element_seek_simple(
        m_pipeline,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        positionNs
    );
}
```

---

## State of the Art

| Old Approach | Current Approach | Impact |
|--------------|-----------------|--------|
| DLNA-only GStreamer pipeline per handler | Shared `MediaPipeline` with URI mode | Consistent sink chain; one audio device |
| Write SCPD files to build output dir | Write inline from C++ strings to temp dir | No build path dependency at runtime |
| libupnp direct state mutation in callback | QMetaObject::invokeMethod queued to main thread | Eliminates threading crashes |

**Deprecated/outdated:**
- `Platinum UPnP SDK`: last released 2020, requires SCons — do not use (already documented in CLAUDE.md)
- `libnpupnp`: modern C++ rewrite of pupnp, cleaner API, but sparse documentation — fine for future; use pupnp 1.14.24 for v1

---

## Open Questions

1. **GENA eventing (LastChange notifications)**
   - What we know: AVTransport spec defines `LastChange` as the sole evented state variable. Controllers may SUBSCRIBE to receive state change events.
   - What's unclear: Whether BubbleUPnP/foobar2000 require GENA subscriptions for basic playback or just for displaying current position.
   - Recommendation: Omit GENA eventing for v1 (D-Claude's Discretion). Controllers that don't get LastChange events will poll GetPositionInfo instead. Note in plan that `eventSubURL` is registered in `MediaRenderer.xml` but SUBSCRIBE responses return empty subscriber lists if not implemented.

2. **playbin vs uridecodebin for URI pipeline**
   - What we know: D-04 specifies `uridecodebin`. `playbin` wraps `uridecodebin` internally and adds audio/video sink management. `uridecodebin` requires manual pad-added handling but allows sharing the existing qml6glsink.
   - What's unclear: Whether `playbin` can be told to use an existing `qml6glsink` widget that is already part of another pipeline element.
   - Recommendation: Use `uridecodebin` as specified in D-04. It requires ~20 extra lines for pad-added handling but avoids pipeline ownership conflicts. `playbin`'s video-sink property could work but risks conflicts with the Phase 1/4 pipeline. Verify with `gst-inspect-1.0 playbin` for `video-sink` property at plan time.

3. **Controller device name for ConnectionBridge HUD**
   - What we know: The DLNA controller does not send its display name in SOAP action parameters. `CurrentURIMetaData` (DIDL-Lite XML) may contain title/artist metadata.
   - What's unclear: Whether BubbleUPnP sends metadata reliably; some controllers omit it.
   - Recommendation: Show `"DLNA"` as the device name if metadata is absent; or extract the URI hostname (e.g., `192.168.1.5`) as a fallback identifier. D-13 says "controller name if available from metadata" — implement with a simple fallback.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| libupnp (pupnp) | SOAP dispatch, HTTP server | ✓ | 1.14.24 (in `/tmp/libupnp-dev`) | — |
| GStreamer 1.26 | uridecodebin pipeline | ✓ | 1.26.6 | — |
| uridecodebin element | URI media playback | ✓ (gst-plugins-base) | 1.26.6 | — |
| Qt 6.8 | Thread marshalling, QObject | ✓ | 6.8.x | — |
| CMake | Build | ✓ | 3.31.6 | — |
| gst-plugins-good/bad/libav | Format support for MP4, MKV, AAC, etc. | Assumed ✓ (installed in Phase 1/4) | 1.26.x | Software decode fallback already in place |

**No missing dependencies.** All required components were installed during Phases 1–4.

---

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Google Test (GTest) via CMake `find_package(GTest REQUIRED)` |
| Config file | `tests/CMakeLists.txt` |
| Quick run command | `ctest --test-dir build/linux-debug -R test_dlna -V` |
| Full suite command | `ctest --test-dir build/linux-debug -V` |

### Phase Requirements to Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| DLNA-01 | Video pushback: SetAVTransportURI + Play dispatches to pipeline | unit | `ctest --test-dir build/linux-debug -R DlnaHandlerTest -V` | ❌ Wave 0 |
| DLNA-02 | Audio pushback: same pipeline path, audio MIME in SinkProtocolInfo | unit | same | ❌ Wave 0 |
| DLNA-03 | DlnaHandler registers as "dlna" protocol, start/stop lifecycle | unit | same | ❌ Wave 0 |

**Note:** End-to-end "real controller pushes media and it plays" testing is manual only — requires a DLNA controller app (BubbleUPnP, foobar2000) on the same LAN. Automated tests cover SOAP dispatch logic and handler lifecycle only.

### Sampling Rate

- **Per task commit:** `ctest --test-dir build/linux-debug -R test_dlna -V`
- **Per wave merge:** `ctest --test-dir build/linux-debug -V`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/test_dlna.cpp` — covers DLNA-01, DLNA-02, DLNA-03 (DlnaHandler instantiation, name(), start()/stop() lifecycle, SOAP dispatch stub)
- [ ] Add `test_dlna` target to `tests/CMakeLists.txt` — link DlnaHandler.cpp + ConnectionBridge.cpp + PkgConfig::UPNP + PkgConfig::GST + Qt6::Core

**Test pattern** (mirrors test_airplay.cpp):
```cpp
TEST(DlnaHandlerTest, CanInstantiate) {
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    EXPECT_EQ(handler.name(), "dlna");
    EXPECT_FALSE(handler.isRunning());
}
TEST(DlnaHandlerTest, StopWithoutStart) {
    airshow::ConnectionBridge bridge;
    airshow::DlnaHandler handler(&bridge);
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}
```

---

## Project Constraints (from CLAUDE.md)

| Constraint | Impact on Phase 5 |
|------------|-------------------|
| C++17 | Use `std::string`, `std::mutex`, structured bindings — no C++20 features |
| Qt 6.8 LTS | `QObject` + `Q_OBJECT` macro required for `QMetaObject::invokeMethod` with lambdas |
| GStreamer 1.26.x | `uridecodebin` available; `gst_element_seek_simple` API stable since 1.0 |
| pupnp / libupnp ≥1.14 | `UpnpActionRequest_get_ActionName` accessor API; `UpnpAddToActionResponse` for responses |
| OpenSSL 3.x | Not used by DLNA itself; already linked — no change |
| CMake ≥3.28 | `qt_add_executable` adds DlnaHandler.cpp; `configure_file` copies SCPD resources |
| GSD Workflow Enforcement | Work starts via GSD entry points; no direct edits outside GSD workflow |
| No freemium/ads/license keys | No dependencies require paid licenses; pupnp is BSD-licensed, GStreamer is LGPL/GPL |
| Local network only | DLNA is inherently LAN-local via SSDP; no internet calls needed |

---

## Sources

### Primary (HIGH confidence)

- `src/discovery/UpnpAdvertiser.h/.cpp` — Existing SSDP infrastructure; SOAP stub to be extended
- `src/protocol/AirPlayHandler.h/.cpp` — Reference pattern for ProtocolHandler + trampoline + QObject
- `src/pipeline/MediaPipeline.cpp` — Existing pad-added and pipeline patterns to follow for uridecodebin
- `resources/dlna/MediaRenderer.xml` — Declares SCPD URLs that must be satisfied
- [pupnp/pupnp GitHub](https://github.com/pupnp/pupnp) — libupnp 1.14.24 API reference
- [BelledonneCommunications/libupnp tv_device.c](https://github.com/BelledonneCommunications/libupnp/blob/master/upnp/sample/common/tv_device.c) — SOAP action handler pattern confirmed
- [BelledonneCommunications/libupnp ActionRequest.h](https://github.com/BelledonneCommunications/libupnp/blob/master/upnp/inc/ActionRequest.h) — UpnpActionRequest accessor API confirmed
- [UPnP AVTransport:1 Service Template](https://upnp.org/specs/av/UPnP-av-AVTransport-v1-Service.pdf) — Official action/state variable spec
- [airpnp AVTransport SCPD reference](https://github.com/provegard/airpnp/blob/master/itest/wdtvlive/MediaRenderer_AVTransport/scpd.xml) — Real-world SCPD XML example

### Secondary (MEDIUM confidence)

- GStreamer uridecodebin — `gst-inspect-1.0 uridecodebin` on the development system; documentation at gstreamer.freedesktop.org (403 at time of research, but API is stable and well-known from Phase 1/4 work)
- [DLNA PITFALLS.md — CallStranger / SSCP subscription vulnerability](../.planning/research/PITFALLS.md) — Existing pitfall documented; SUBSCRIBE callbacks should reject non-LAN IPs

### Tertiary (LOW confidence)

- GENA eventing behavior of BubbleUPnP — no primary source; based on common knowledge from DLNA DMR implementations. Mark as requiring manual validation during phase verification.

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all libraries confirmed present at known versions; no new dependencies
- SOAP action dispatch: HIGH — verified against libupnp sample code and accessor API headers
- uridecodebin pipeline: HIGH — element present on system; pattern is analogous to existing decodebin usage in MediaPipeline.cpp
- SCPD XML correctness: MEDIUM — structure verified against WDTV Live real-world example and UPnP spec; exact field names validated against AVTransport:1 PDF
- Threading model: HIGH — QMetaObject::invokeMethod Qt::QueuedConnection is documented thread-safe

**Research date:** 2026-03-28
**Valid until:** 2026-06-28 (stable libraries; pupnp and GStreamer 1.26 APIs will not change within 90 days)
