#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <QHostAddress>
#include <QString>
#include <string>

// Forward declarations — avoid pulling in Qt network headers in the interface
class QTcpServer;
class QTcpSocket;

namespace myairshow {

class ConnectionBridge;
class MediaPipeline;
class SecurityManager;

// Miracast over Infrastructure (MS-MICE) protocol handler.
//
// Implements the receiver (sink) side of the MS-MICE protocol as described
// in [MS-MICE] revision 6.0. The receiver listens on TCP port 7250 for a
// SOURCE_READY binary TLV message from the Windows source, then connects
// to the source's RTSP server on port 7236 to negotiate the WFD session
// via the M1-M7 message exchange.
//
// Port 7250: MS-MICE binary TLV control channel (QTcpServer listening)
// Port 7236: WFD RTSP signaling (QTcpSocket connecting to source)
// Port 1028: RTP/UDP media receive (default, negotiated in M6 SETUP)
//
// Thread model: all I/O runs on the Qt event loop thread — no manual threading.
// Single-session model consistent with CastHandler (D-14).
class MiracastHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    // MS-MICE + WFD RTSP state machine (RESEARCH.md Pattern 3)
    enum class State {
        Idle,
        WaitingSourceReady,   // TCP 7250 listener active, waiting for source connection
        ConnectingToSource,   // TCP connecting to source:7236
        NegotiatingM1,        // awaiting M1 OPTIONS from source
        NegotiatingM2,        // sent M2 OPTIONS, waiting source 200 OK
        NegotiatingM3,        // awaiting M3 GET_PARAMETER from source
        NegotiatingM4,        // sent M3 response, awaiting M4 SET_PARAMETER from source
        NegotiatingM5,        // sent M4 response, awaiting M5 SET_PARAMETER trigger
        SendingSetup,         // sent SETUP (M6), awaiting source 200 OK
        SendingPlay,          // sent PLAY (M7), awaiting source 200 OK
        Streaming,            // RTP media flowing
        TearingDown           // TEARDOWN in progress
    };

    static constexpr uint16_t kMicePort    = 7250;  // MS-MICE control channel
    static constexpr uint16_t kRtspPort    = 7236;  // WFD RTSP server on source
    static constexpr int      kDefaultRtpPort = 1028;  // UDP port for RTP media (lazycast default)

    explicit MiracastHandler(ConnectionBridge* connectionBridge, QObject* parent = nullptr);
    ~MiracastHandler() override;

    // ProtocolHandler interface
    bool        start() override;
    void        stop() override;
    std::string name() const override { return "miracast"; }
    bool        isRunning() const override { return m_running; }
    void        setMediaPipeline(MediaPipeline* pipeline) override;

    // Security integration (Phase 7 Plan 02 pattern). Call before start().
    // If null, all local connections are admitted (backward compatible).
    void        setSecurityManager(SecurityManager* sm);

    // Build a RTSP/1.0 response string for a given CSeq, status code, and optional body.
    // Made public static for unit testing without friend declarations
    // (same pattern as DlnaHandler::parseTimeString and CastSession::buildSdpFromOffer).
    static QString buildRtspResponse(int cseq, int statusCode, const QString& body = {});

    // Parse a MS-MICE SOURCE_READY binary message. Exposed as public static for unit testing.
    // Fills out: friendlyName, rtspPort, sourceId. Returns true if message is valid.
    struct SourceReadyInfo {
        QString friendlyName;
        uint16_t rtspPort = 0;
        QString sourceId;
    };
    static bool parseSourceReady(const QByteArray& data, SourceReadyInfo& out);

private slots:
    void onMiceConnection();
    void onMiceData();
    void onRtspConnected();
    void onRtspData();
    void onRtspDisconnected();

private:
    void parseMiceMessage(const QByteArray& data);
    void sendRtspRequest(const QString& method, const QString& uri, const QString& body = {});

    // MS-MICE TCP listener (port 7250) — receives SOURCE_READY from Windows source
    QTcpServer*       m_miceServer     = nullptr;
    // Accepted connection from the source (MS-MICE channel)
    QTcpSocket*       m_miceClient     = nullptr;
    // RTSP client connection to source:7236
    QTcpSocket*       m_rtspSocket     = nullptr;

    State             m_state          = State::Idle;
    bool              m_running        = false;
    MediaPipeline*    m_pipeline       = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    SecurityManager*  m_securityManager  = nullptr;

    int               m_cseq           = 1;        // RTSP CSeq counter
    uint16_t          m_rtspPort       = kRtspPort; // RTSP port from SOURCE_READY
    QString           m_sourceName;                 // Friendly name from SOURCE_READY
    QHostAddress      m_sourceAddr;                 // IP of the Windows source
    QString           m_sourceId;                   // SourceID TLV from SOURCE_READY
    int               m_udpPort        = kDefaultRtpPort; // negotiated RTP receive port

    // Accumulation buffer for RTSP data (non-blocking async parsing)
    QByteArray        m_rtspBuffer;
};

} // namespace myairshow
