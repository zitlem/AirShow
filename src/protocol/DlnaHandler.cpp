#include "protocol/DlnaHandler.h"
#include "ui/ConnectionBridge.h"
#include "pipeline/MediaPipeline.h"

// libupnp headers
#include <upnp/upnp.h>
#include <upnp/upnptools.h>
#include <upnp/UpnpActionRequest.h>
#include <upnp/UpnpString.h>
#include <upnp/ixml.h>

// GStreamer
#include <gst/gst.h>

// Qt
#include <QMetaObject>

#include <cstdio>
#include <cstring>
#include <string>

namespace myairshow {

// ── File-scope helper ──────────────────────────────────────────────────────────

// Extract a named argument value from a SOAP action request XML document.
// Pattern 3 from RESEARCH.md — verified against libupnp sample_util.c.
static std::string getArgValue(IXML_Document* actionDoc, const char* argName) {
    if (!actionDoc || !argName) return {};
    IXML_NodeList* nodeList = ixmlDocument_getElementsByTagName(actionDoc, argName);
    if (!nodeList) return {};
    IXML_Node* node = ixmlNodeList_item(nodeList, 0);
    ixmlNodeList_free(nodeList);
    if (!node) return {};
    IXML_Node* textNode = ixmlNode_getFirstChild(node);
    if (!textNode) return {};
    const DOMString val = ixmlNode_getNodeValue(textNode);
    return val ? std::string(val) : std::string{};
}

// ── DlnaHandler ───────────────────────────────────────────────────────────────

DlnaHandler::DlnaHandler(ConnectionBridge* connectionBridge)
    : m_connectionBridge(connectionBridge)
    , m_running(false)
{}

DlnaHandler::~DlnaHandler() {
    stop();
}

bool DlnaHandler::start() {
    // DlnaHandler does not own its own libupnp listener — UpnpAdvertiser does.
    // start() just marks us as ready to receive SOAP callbacks via handleSoapAction.
    m_running = true;
    return true;
}

void DlnaHandler::stop() {
    if (!m_running) return;

    // Stop URI pipeline if a DLNA session is active
    QMetaObject::invokeMethod(this, [this]() {
        if (m_pipeline) {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            // Plan 02 will call: if (m_transportState != TransportState::STOPPED) m_pipeline->stopUri();
            m_transportState = TransportState::STOPPED;
            m_currentUri.clear();
            m_currentMetadata.clear();
        }
        // Clear connection bridge state
        if (m_connectionBridge) {
            m_connectionBridge->setConnected(false);
        }
    }, Qt::QueuedConnection);

    m_running = false;
}

void DlnaHandler::setMediaPipeline(MediaPipeline* pipeline) {
    m_pipeline = pipeline;
}

// ── SOAP dispatch ──────────────────────────────────────────────────────────────

int DlnaHandler::handleSoapAction(const void* event) {
    // Pattern 2 from RESEARCH.md — verified against libupnp tv_device sample.
    const auto* req = static_cast<const UpnpActionRequest*>(event);

    const char* actionName =
        UpnpString_get_String(UpnpActionRequest_get_ActionName(req));
    // serviceId available for future multi-service disambiguation if needed
    // const char* serviceId =
    //     UpnpString_get_String(UpnpActionRequest_get_ServiceID(req));

    IXML_Document* result = nullptr;
    int errCode = UPNP_E_SUCCESS;

    if (strcmp(actionName, "SetAVTransportURI") == 0) {
        errCode = onSetAVTransportURI(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "Play") == 0) {
        errCode = onPlay(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "Stop") == 0) {
        errCode = onStop(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "Pause") == 0) {
        errCode = onPause(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "Seek") == 0) {
        errCode = onSeek(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetTransportInfo") == 0) {
        errCode = onGetTransportInfo(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetPositionInfo") == 0) {
        errCode = onGetPositionInfo(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetMediaInfo") == 0) {
        errCode = onGetMediaInfo(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "SetVolume") == 0) {
        errCode = onSetVolume(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetVolume") == 0) {
        errCode = onGetVolume(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "SetMute") == 0) {
        errCode = onSetMute(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetMute") == 0) {
        errCode = onGetMute(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetProtocolInfo") == 0) {
        errCode = onGetProtocolInfo(req, reinterpret_cast<void**>(&result));
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

// ── Transport state string ─────────────────────────────────────────────────────

const char* DlnaHandler::transportStateString() const {
    switch (m_transportState) {
        case TransportState::STOPPED:      return "STOPPED";
        case TransportState::PLAYING:      return "PLAYING";
        case TransportState::PAUSED:       return "PAUSED_PLAYBACK";
        case TransportState::TRANSITIONING: return "TRANSITIONING";
    }
    return "STOPPED";
}

// ── Time parsing helpers ───────────────────────────────────────────────────────

// static
gint64 DlnaHandler::parseTimeString(const std::string& t) {
    int h = 0, m = 0, s = 0;
    if (sscanf(t.c_str(), "%d:%d:%d", &h, &m, &s) != 3) return 0;
    return (gint64)(h * 3600 + m * 60 + s) * GST_SECOND;
}

// static
std::string DlnaHandler::formatGstTime(gint64 ns) {
    if (ns < 0) return "0:00:00";
    gint64 totalSec = ns / GST_SECOND;
    int h = (int)(totalSec / 3600);
    int m = (int)((totalSec % 3600) / 60);
    int s = (int)(totalSec % 60);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    return buf;
}

// ── AVTransport action stubs ───────────────────────────────────────────────────
// These stubs return valid empty SOAP responses. Plan 02 replaces them with
// real pipeline calls. The QMetaObject::invokeMethod pattern MUST be present
// now so Plan 02 only fills in the lambda bodies.

int DlnaHandler::onSetAVTransportURI(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string uri      = getArgValue(actionDoc, "CurrentURI");
    std::string metadata = getArgValue(actionDoc, "CurrentURIMetaData");

    // CRITICAL threading rule (Pitfall 1 from RESEARCH.md): queue onto Qt main thread
    QMetaObject::invokeMethod(this, [this, uri, metadata]() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentUri      = uri;
        m_currentMetadata = metadata;
        m_transportState  = TransportState::STOPPED;
        // Plan 02 will: m_pipeline->stopUri(); m_pipeline->setUri(uri);
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SetAVTransportURIResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onPlay(const void* /*req*/, void** result) {
    QMetaObject::invokeMethod(this, [this]() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_transportState = TransportState::PLAYING;
        // Plan 02 will: m_pipeline->playUri(); m_connectionBridge->setConnected(true, ..., "DLNA");
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "PlayResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onStop(const void* /*req*/, void** result) {
    QMetaObject::invokeMethod(this, [this]() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_transportState = TransportState::STOPPED;
        // Plan 02 will: m_pipeline->stopUri(); m_connectionBridge->setConnected(false);
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "StopResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onPause(const void* /*req*/, void** result) {
    QMetaObject::invokeMethod(this, [this]() {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_transportState = TransportState::PAUSED;
        // Plan 02 will: m_pipeline->pauseUri();
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "PauseResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onSeek(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string unit   = getArgValue(actionDoc, "Unit");
    std::string target = getArgValue(actionDoc, "Target");

    QMetaObject::invokeMethod(this, [this, unit, target]() {
        // Plan 02 will: if (unit == "REL_TIME") m_pipeline->seekUri(parseTimeString(target));
        (void)unit; (void)target;
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SeekResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetTransportInfo(const void* /*req*/, void** result) {
    const char* state = transportStateString();
    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetTransportInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "CurrentTransportState", state);
    UpnpAddToActionResponse(doc, "GetTransportInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "CurrentTransportStatus", "OK");
    UpnpAddToActionResponse(doc, "GetTransportInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1",
        "CurrentSpeed", "1");
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetPositionInfo(const void* /*req*/, void** result) {
    // Plan 02 will query m_pipeline->queryPosition() / queryDuration()
    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "Track", "0");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "TrackDuration", "0:00:00");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "TrackMetaData", "");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "TrackURI", "");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "RelTime", "0:00:00");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "AbsTime", "NOT_IMPLEMENTED");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "RelCount", "2147483647");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "AbsCount", "2147483647");
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetMediaInfo(const void* /*req*/, void** result) {
    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "NrTracks", "0");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "MediaDuration", "0:00:00");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "CurrentURI", m_currentUri.c_str());
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "CurrentURIMetaData", m_currentMetadata.c_str());
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "NextURI", "");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "NextURIMetaData", "");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "PlayMedium", "NETWORK");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "RecordMedium", "NOT_IMPLEMENTED");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        "urn:schemas-upnp-org:service:AVTransport:1", "WriteStatus", "NOT_IMPLEMENTED");
    return UPNP_E_SUCCESS;
}

// ── RenderingControl action stubs ──────────────────────────────────────────────

int DlnaHandler::onSetVolume(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string volStr = getArgValue(actionDoc, "DesiredVolume");

    QMetaObject::invokeMethod(this, [this, volStr]() {
        // Plan 02 will: int vol = std::stoi(volStr); m_pipeline->setVolume(vol / 100.0);
        (void)volStr;
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SetVolumeResponse",
        "urn:schemas-upnp-org:service:RenderingControl:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetVolume(const void* /*req*/, void** result) {
    // Plan 02 will: query m_pipeline->getVolume() and convert 0.0-1.0 to 0-100
    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetVolumeResponse",
        "urn:schemas-upnp-org:service:RenderingControl:1",
        "CurrentVolume", "100");
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onSetMute(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string muteStr = getArgValue(actionDoc, "DesiredMute");

    QMetaObject::invokeMethod(this, [this, muteStr]() {
        // Plan 02 will: bool muted = (muteStr == "1" || muteStr == "true");
        //               m_pipeline->setMuted(muted);
        (void)muteStr;
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SetMuteResponse",
        "urn:schemas-upnp-org:service:RenderingControl:1", nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetMute(const void* /*req*/, void** result) {
    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetMuteResponse",
        "urn:schemas-upnp-org:service:RenderingControl:1",
        "CurrentMute", "0");
    return UPNP_E_SUCCESS;
}

// ── ConnectionManager action stubs ────────────────────────────────────────────

int DlnaHandler::onGetProtocolInfo(const void* /*req*/, void** result) {
    // D-07: Broad format support list for SinkProtocolInfo
    static const char* kSinkProtocolInfo =
        "http-get:*:video/mp4:*,"
        "http-get:*:video/mpeg:*,"
        "http-get:*:video/x-matroska:*,"
        "http-get:*:video/avi:*,"
        "http-get:*:audio/mpeg:*,"
        "http-get:*:audio/mp4:*,"
        "http-get:*:audio/flac:*,"
        "http-get:*:audio/wav:*,"
        "http-get:*:audio/x-ms-wma:*";

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetProtocolInfoResponse",
        "urn:schemas-upnp-org:service:ConnectionManager:1",
        "Source", "");
    UpnpAddToActionResponse(doc, "GetProtocolInfoResponse",
        "urn:schemas-upnp-org:service:ConnectionManager:1",
        "Sink", kSinkProtocolInfo);
    return UPNP_E_SUCCESS;
}

} // namespace myairshow
