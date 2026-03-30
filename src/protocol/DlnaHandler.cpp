#include "protocol/DlnaHandler.h"
#include "ui/ConnectionBridge.h"
#include "pipeline/MediaPipeline.h"
#include "security/SecurityManager.h"

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
#include <QHostAddress>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

namespace myairshow {

// ── File-scope service type constants ─────────────────────────────────────────

static constexpr const char* kAvtServiceType =
    "urn:schemas-upnp-org:service:AVTransport:1";
static constexpr const char* kRcServiceType =
    "urn:schemas-upnp-org:service:RenderingControl:1";
static constexpr const char* kCmServiceType =
    "urn:schemas-upnp-org:service:ConnectionManager:1";

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
            bool wasPlaying = false;
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                wasPlaying = (m_transportState != TransportState::STOPPED);
                m_transportState = TransportState::STOPPED;
                m_currentUri.clear();
                m_currentMetadata.clear();
            }
            if (wasPlaying) {
                m_pipeline->stopUri();
            }
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

void DlnaHandler::setSecurityManager(SecurityManager* sm) {
    m_securityManager = sm;
}

// ── SOAP dispatch ──────────────────────────────────────────────────────────────

int DlnaHandler::handleSoapAction(const void* event) {
    // Pattern 2 from RESEARCH.md — verified against libupnp tv_device sample.
    const auto* req = static_cast<const UpnpActionRequest*>(event);

    // Phase 7 (SEC-03): Reject SOAP actions from non-RFC1918 source IPs (D-08).
    // UpnpActionRequest_get_CtrlPtIPAddr returns a struct sockaddr_storage*.
    const struct sockaddr_storage* peerSockAddr = UpnpActionRequest_get_CtrlPtIPAddr(req);
    if (peerSockAddr) {
        QHostAddress clientAddr;
        if (peerSockAddr->ss_family == AF_INET) {
            const auto* sin = reinterpret_cast<const struct sockaddr_in*>(peerSockAddr);
            clientAddr = QHostAddress(ntohl(sin->sin_addr.s_addr));
        } else if (peerSockAddr->ss_family == AF_INET6) {
            const auto* sin6 = reinterpret_cast<const struct sockaddr_in6*>(peerSockAddr);
            clientAddr = QHostAddress(sin6->sin6_addr.s6_addr);
        }
        if (!clientAddr.isNull() && !SecurityManager::isLocalNetwork(clientAddr)) {
            // Reject non-local source — return 401 Invalid Action (D-08)
            UpnpActionRequest_set_ErrCode(
                const_cast<UpnpActionRequest*>(req), 401);
            return UPNP_E_SUCCESS;
        }

        // Phase 7 (SEC-01): Require SecurityManager approval for unrecognized devices.
        // handleSoapAction runs on libupnp's thread pool — safe to use synchronous
        // checkConnection (QSemaphore blocking, not Qt main thread).
        if (m_securityManager && !clientAddr.isNull()) {
            QString deviceId = clientAddr.toString();
            bool allowed = m_securityManager->checkConnection(
                QStringLiteral("DLNA Controller"), QStringLiteral("DLNA"), deviceId);
            if (!allowed) {
                UpnpActionRequest_set_ErrCode(
                    const_cast<UpnpActionRequest*>(req), 401);
                return UPNP_E_SUCCESS;
            }
        }
    }

    const char* actionName =
        UpnpString_get_String(UpnpActionRequest_get_ActionName(req));

    IXML_Document* result = nullptr;
    int errCode = UPNP_E_SUCCESS;

    // AVTransport actions
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
    // Spec-required minimal AVTransport actions
    } else if (strcmp(actionName, "GetDeviceCapabilities") == 0) {
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "GetDeviceCapabilitiesResponse",
            kAvtServiceType, "PlayMedia", "NETWORK");
        UpnpAddToActionResponse(doc, "GetDeviceCapabilitiesResponse",
            kAvtServiceType, "RecMedia", "NOT_IMPLEMENTED");
        UpnpAddToActionResponse(doc, "GetDeviceCapabilitiesResponse",
            kAvtServiceType, "RecQualityModes", "NOT_IMPLEMENTED");
    } else if (strcmp(actionName, "GetTransportSettings") == 0) {
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "GetTransportSettingsResponse",
            kAvtServiceType, "PlayMode", "NORMAL");
        UpnpAddToActionResponse(doc, "GetTransportSettingsResponse",
            kAvtServiceType, "RecQualityMode", "NOT_IMPLEMENTED");
    } else if (strcmp(actionName, "GetCurrentTransportActions") == 0) {
        TransportState state;
        std::string uri;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            state = m_transportState;
            uri   = m_currentUri;
        }
        const char* actions = "Play,Stop,Pause,Seek";
        if (state == TransportState::STOPPED && uri.empty()) {
            actions = "";
        }
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "GetCurrentTransportActionsResponse",
            kAvtServiceType, "Actions", actions);
    // RenderingControl actions
    } else if (strcmp(actionName, "SetVolume") == 0) {
        errCode = onSetVolume(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetVolume") == 0) {
        errCode = onGetVolume(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "SetMute") == 0) {
        errCode = onSetMute(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetMute") == 0) {
        errCode = onGetMute(req, reinterpret_cast<void**>(&result));
    // Spec-required minimal RenderingControl actions
    } else if (strcmp(actionName, "ListPresets") == 0) {
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "ListPresetsResponse",
            kRcServiceType, "CurrentPresetNameList", "FactoryDefaults");
    } else if (strcmp(actionName, "SelectPreset") == 0) {
        // Accept any preset name — no-op renderer
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "SelectPresetResponse",
            kRcServiceType, nullptr, nullptr);
    // ConnectionManager actions
    } else if (strcmp(actionName, "GetProtocolInfo") == 0) {
        errCode = onGetProtocolInfo(req, reinterpret_cast<void**>(&result));
    } else if (strcmp(actionName, "GetCurrentConnectionIDs") == 0) {
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "GetCurrentConnectionIDsResponse",
            kCmServiceType, "ConnectionIDs", "0");
    } else if (strcmp(actionName, "GetCurrentConnectionInfo") == 0) {
        auto** doc = reinterpret_cast<IXML_Document**>(&result);
        *doc = nullptr;
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "RcsID", "0");
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "AVTransportID", "0");
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "ProtocolInfo", "");
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "PeerConnectionManager", "");
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "PeerConnectionID", "-1");
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "Direction", "Input");
        UpnpAddToActionResponse(doc, "GetCurrentConnectionInfoResponse",
            kCmServiceType, "Status", "OK");
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

// ── AVTransport actions ────────────────────────────────────────────────────────

int DlnaHandler::onSetAVTransportURI(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string uri      = getArgValue(actionDoc, "CurrentURI");
    std::string metadata = getArgValue(actionDoc, "CurrentURIMetaData");

    // Store URI and state under mutex before queuing pipeline work
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_currentUri      = uri;
        m_currentMetadata = metadata;
        m_transportState  = TransportState::STOPPED;
    }

    // CRITICAL threading rule (Pitfall 1 from RESEARCH.md): queue onto Qt main thread.
    // setUri() stops any current playback (GST_STATE_NULL) then sets the new URI
    // and prerolls (GST_STATE_PAUSED). Single-session model (D-12).
    QMetaObject::invokeMethod(this, [this, uri]() {
        if (m_pipeline) {
            m_pipeline->setUri(uri);
        }
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SetAVTransportURIResponse",
        kAvtServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onPlay(const void* /*req*/, void** result) {
    // Queue pipeline and HUD updates on main thread (Pitfall 1 from RESEARCH.md)
    QMetaObject::invokeMethod(this, [this]() {
        if (m_pipeline) {
            m_pipeline->playUri();
        }
        if (m_connectionBridge) {
            m_connectionBridge->setConnected(true, "DLNA Controller", "DLNA");
        }
    }, Qt::QueuedConnection);

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_transportState = TransportState::PLAYING;
    }

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "PlayResponse",
        kAvtServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onStop(const void* /*req*/, void** result) {
    // Queue pipeline and HUD updates on main thread (Pitfall 1 from RESEARCH.md)
    QMetaObject::invokeMethod(this, [this]() {
        if (m_pipeline) {
            m_pipeline->stopUri();
        }
        if (m_connectionBridge) {
            m_connectionBridge->setConnected(false);
        }
    }, Qt::QueuedConnection);

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_transportState = TransportState::STOPPED;
    }

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "StopResponse",
        kAvtServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onPause(const void* /*req*/, void** result) {
    // Queue pipeline pause on main thread (Pitfall 1 from RESEARCH.md)
    QMetaObject::invokeMethod(this, [this]() {
        if (m_pipeline) {
            m_pipeline->pauseUri();
        }
    }, Qt::QueuedConnection);

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_transportState = TransportState::PAUSED;
    }

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "PauseResponse",
        kAvtServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onSeek(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string unit   = getArgValue(actionDoc, "Unit");
    std::string target = getArgValue(actionDoc, "Target");

    // Only REL_TIME seeks supported (Pitfall 5 from RESEARCH.md)
    if (unit != "REL_TIME") {
        UpnpActionRequest_set_ErrCode(
            const_cast<UpnpActionRequest*>(actionReq), 710);  // Seek mode not supported
        return UPNP_E_SUCCESS;
    }

    gint64 posNs = parseTimeString(target);

    // Queue seek on main thread (Pitfall 1 from RESEARCH.md)
    QMetaObject::invokeMethod(this, [this, posNs]() {
        if (m_pipeline) {
            m_pipeline->seekUri(posNs);
        }
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SeekResponse",
        kAvtServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetTransportInfo(const void* /*req*/, void** result) {
    const char* state;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        state = transportStateString();
    }
    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetTransportInfoResponse",
        kAvtServiceType, "CurrentTransportState", state);
    UpnpAddToActionResponse(doc, "GetTransportInfoResponse",
        kAvtServiceType, "CurrentTransportStatus", "OK");
    UpnpAddToActionResponse(doc, "GetTransportInfoResponse",
        kAvtServiceType, "CurrentSpeed", "1");
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetPositionInfo(const void* /*req*/, void** result) {
    // Query position and duration from pipeline (Pitfall 6: duration may be -1 for streams)
    gint64 pos = (m_pipeline) ? m_pipeline->queryPosition() : -1;
    gint64 dur = (m_pipeline) ? m_pipeline->queryDuration() : -1;

    std::string posStr = (pos >= 0) ? formatGstTime(pos) : "0:00:00";
    std::string durStr = (dur >= 0) ? formatGstTime(dur) : "NOT_IMPLEMENTED";

    std::string trackUri;
    std::string trackMeta;
    bool stopped = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        trackUri  = m_currentUri;
        trackMeta = m_currentMetadata;
        stopped   = (m_transportState == TransportState::STOPPED);
    }

    const char* trackNum = (stopped && trackUri.empty()) ? "0" : "1";

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "Track", trackNum);
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "TrackDuration", durStr.c_str());
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "TrackMetaData", trackMeta.c_str());
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "TrackURI", trackUri.c_str());
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "RelTime", posStr.c_str());
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "AbsTime", "NOT_IMPLEMENTED");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "RelCount", "2147483647");
    UpnpAddToActionResponse(doc, "GetPositionInfoResponse",
        kAvtServiceType, "AbsCount", "2147483647");
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetMediaInfo(const void* /*req*/, void** result) {
    gint64 dur = (m_pipeline) ? m_pipeline->queryDuration() : -1;
    std::string durStr = (dur >= 0) ? formatGstTime(dur) : "NOT_IMPLEMENTED";

    std::string currentUri;
    std::string currentMeta;
    bool hasUri = false;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        currentUri  = m_currentUri;
        currentMeta = m_currentMetadata;
        hasUri      = !m_currentUri.empty();
    }

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "NrTracks", hasUri ? "1" : "0");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "MediaDuration", durStr.c_str());
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "CurrentURI", currentUri.c_str());
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "CurrentURIMetaData", currentMeta.c_str());
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "NextURI", "");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "NextURIMetaData", "");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "PlayMedium", "NETWORK");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "RecordMedium", "NOT_IMPLEMENTED");
    UpnpAddToActionResponse(doc, "GetMediaInfoResponse",
        kAvtServiceType, "WriteStatus", "NOT_IMPLEMENTED");
    return UPNP_E_SUCCESS;
}

// ── RenderingControl actions ───────────────────────────────────────────────────

int DlnaHandler::onSetVolume(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string volStr = getArgValue(actionDoc, "DesiredVolume");

    // Parse and clamp the volume, converting UPnP 0-100 to GStreamer 0.0-1.0
    int rawVol = 100;
    try {
        rawVol = std::stoi(volStr);
    } catch (...) {
        rawVol = 100;
    }
    int clamped = std::max(0, std::min(100, rawVol));
    double vol = clamped / 100.0;

    // Queue volume change on main thread (Pitfall 1 from RESEARCH.md)
    QMetaObject::invokeMethod(this, [this, vol]() {
        if (m_pipeline) {
            m_pipeline->setVolume(vol);
        }
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SetVolumeResponse",
        kRcServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetVolume(const void* /*req*/, void** result) {
    double vol = (m_pipeline) ? m_pipeline->getVolume() : 1.0;
    int upnpVol = static_cast<int>(vol * 100.0 + 0.5);
    upnpVol = std::max(0, std::min(100, upnpVol));

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetVolumeResponse",
        kRcServiceType, "CurrentVolume", std::to_string(upnpVol).c_str());
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onSetMute(const void* req, void** result) {
    auto* actionReq = static_cast<const UpnpActionRequest*>(req);
    IXML_Document* actionDoc = UpnpActionRequest_get_ActionRequest(actionReq);
    std::string muteStr = getArgValue(actionDoc, "DesiredMute");

    bool mute = (muteStr == "1" || muteStr == "true" || muteStr == "True");

    // Queue mute change on main thread (Pitfall 1 from RESEARCH.md)
    QMetaObject::invokeMethod(this, [this, mute]() {
        if (m_pipeline) {
            m_pipeline->setMuted(mute);
        }
    }, Qt::QueuedConnection);

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "SetMuteResponse",
        kRcServiceType, nullptr, nullptr);
    return UPNP_E_SUCCESS;
}

int DlnaHandler::onGetMute(const void* /*req*/, void** result) {
    bool muted = (m_pipeline) ? m_pipeline->isMuted() : false;

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetMuteResponse",
        kRcServiceType, "CurrentMute", muted ? "1" : "0");
    return UPNP_E_SUCCESS;
}

// ── ConnectionManager actions ──────────────────────────────────────────────────

int DlnaHandler::onGetProtocolInfo(const void* /*req*/, void** result) {
    // D-07: Broad format support list for SinkProtocolInfo
    static const char* kSinkProtocolInfo =
        "http-get:*:video/mp4:*,"
        "http-get:*:video/mpeg:*,"
        "http-get:*:video/x-matroska:*,"
        "http-get:*:video/avi:*,"
        "http-get:*:video/x-msvideo:*,"
        "http-get:*:audio/mpeg:*,"
        "http-get:*:audio/mp4:*,"
        "http-get:*:audio/flac:*,"
        "http-get:*:audio/wav:*,"
        "http-get:*:audio/x-wav:*,"
        "http-get:*:audio/x-ms-wma:*,"
        "http-get:*:audio/L16:*,"
        "http-get:*:video/x-flv:*,"
        "http-get:*:video/3gpp:*";

    auto** doc = reinterpret_cast<IXML_Document**>(result);
    *doc = nullptr;
    UpnpAddToActionResponse(doc, "GetProtocolInfoResponse",
        kCmServiceType, "Source", "");
    UpnpAddToActionResponse(doc, "GetProtocolInfoResponse",
        kCmServiceType, "Sink", kSinkProtocolInfo);
    return UPNP_E_SUCCESS;
}

} // namespace myairshow
