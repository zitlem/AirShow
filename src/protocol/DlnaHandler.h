#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <string>
#include <mutex>

// gint64 forward: include gst types via glib primitives to avoid full GStreamer pull in header
#include <glib.h>

namespace myairshow {

class MediaPipeline;
class ConnectionBridge;

// DLNA Digital Media Renderer handler (D-01).
// Implements ProtocolHandler and handles SOAP action dispatch for:
//   - AVTransport:1 (SetAVTransportURI, Play, Stop, Pause, Seek, GetTransportInfo, etc.)
//   - RenderingControl:1 (SetVolume, GetVolume, SetMute, GetMute)
//   - ConnectionManager:1 (GetProtocolInfo)
//
// Threading: libupnp invokes handleSoapAction() from its own thread pool.
// All GStreamer state changes and Qt signal emissions are marshalled via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection) — never called directly
// from the libupnp callback (Pattern 6 from RESEARCH.md).
class DlnaHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    explicit DlnaHandler(ConnectionBridge* connectionBridge);
    ~DlnaHandler() override;

    // ProtocolHandler interface
    bool start() override;
    void stop() override;
    std::string name() const override { return "dlna"; }
    bool isRunning() const override { return m_running; }
    void setMediaPipeline(MediaPipeline* pipeline) override;

    // Called by UpnpAdvertiser::upnpCallback trampoline (D-02).
    // event is cast to const UpnpActionRequest* internally.
    int handleSoapAction(const void* event);

    // Exposed for testing: parse "H:MM:SS" or "H+:MM:SS" to GStreamer nanoseconds
    static gint64 parseTimeString(const std::string& t);
    // Exposed for testing: format nanoseconds to "H:MM:SS"
    static std::string formatGstTime(gint64 ns);

private:
    // Per-action handlers — each populates result IXML_Document
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
    std::string       m_currentMetadata;
    std::mutex        m_stateMutex;
};

} // namespace myairshow
