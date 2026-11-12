#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <QHostAddress>
#include <QTimer>
#include <QString>
#include <string>

// Forward declarations — avoid pulling in Qt network headers in the interface
class QTcpServer;
class QTcpSocket;

namespace airshow {

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
    State       state() const { return m_state; }
    void        setMediaPipeline(MediaPipeline* pipeline) override;

    // Security integration (Phase 7 Plan 02 pattern). Call before start().
    // If null, all local connections are admitted (backward compatible).
    void        setSecurityManager(SecurityManager* sm);

    // Store QML video output item for initMiracastPipeline().
    // Called from main.cpp before any Miracast connection arrives.
    // Same pattern as MediaPipeline::setQmlVideoItem() used by Cast.
    void        setQmlVideoItem(void* item);

    // WFD M3 capability response body (public so tests can verify exact format).
    // Source: lazycast d2.py (verified against WFD spec v2.1 field definitions).
    // IMPORTANT: wfd_content_protection MUST be "none" — see RESEARCH.md Pitfall 6.
    static constexpr const char* kWfdCapabilityResponse =
        "wfd_video_formats: "
        "00 00 02 10 "
        "0001FEFF 3FFFFFFF 00000FFF "
        "00 0000 0000 00 none none\r\n"
        "wfd_audio_codecs: "
        "LPCM 00000003 00, AAC 00000001 00\r\n"
        "wfd_client_rtp_ports: "
        "RTP/AVP/UDP;unicast 1028 0 mode=play\r\n"
        "wfd_content_protection: none\r\n"
        "wfd_display_edid: none\r\n"
        "wfd_connector_type: 05\r\n";

    // Build a RTSP/1.0 response string for a given CSeq, status code, and optional body.
    // Made public static for unit testing without friend declarations
    // (same pattern as DlnaHandler::parseTimeString and CastSession::buildSdpFromOffer).
    static QString buildRtspResponse(int cseq, int statusCode, const QString& body = {});

    // MS-MICE message commands ([MS-MICE] 2.2)
    static constexpr uint8_t kMiceSourceReady    = 0x01;
    static constexpr uint8_t kMiceStopProjection = 0x02;

    // Parse a MS-MICE SOURCE_READY binary message. Exposed as public static for unit testing.
    // Fills out: friendlyName, rtspPort, sourceId (hex of the 16-byte Source ID).
    // Returns true if message is a valid SOURCE_READY.
    struct SourceReadyInfo {
        QString friendlyName;
        uint16_t rtspPort = 0;
        QString sourceId;
    };
    static bool parseSourceReady(const QByteArray& data, SourceReadyInfo& out);

    // If buf starts with one complete MS-MICE message, return its length in bytes
    // (from the 2-byte Size header); return 0 if more data is needed, -1 if invalid.
    static int miceMessageLength(const QByteArray& buf);

    // Extract the URL from a "wfd_presentation_URL: <url0> <url1>" line in an
    // RTSP text/parameters body (WFD M4). Returns empty if absent or "none".
    static QString parsePresentationUrl(const QByteArray& body);

    // Extract the method from a "wfd_trigger_method: <SETUP|PLAY|PAUSE|TEARDOWN>"
    // line (WFD M5 and later triggers), upper-cased. Returns empty if absent.
    static QString parseTriggerMethod(const QByteArray& body);

    // Extract the session id from an RTSP "Session: <id>[;timeout=N]" header value.
    static QString parseSessionId(const QString& sessionHeader);

private slots:
    void onMiceConnection();
    void onMiceData();
    void onRtspConnected();
    void onRtspData();
    void onRtspDisconnected();

private:
    void parseMiceMessage(const QByteArray& data);
    void handleSourceReady(const QByteArray& data);
    void connectToSource();
    void sendSetup();
    void sendPlay();
    // WFD M13: ask the source for an IDR (key) frame so decoding can (re)start.
    void sendIdrRequest();
    // Periodic stream health check while streaming: requests an IDR while no
    // frame has decoded yet (with backoff), or after packet loss / decode errors.
    void checkStreamHealth();
    QString streamUri() const;
    // Returns the CSeq used for the request.
    int  sendRtspRequest(const QString& method, const QString& uri,
                         const QString& extraHeaders = {}, const QString& body = {});
    void teardown();

    // Parse one complete RTSP message from m_rtspBuffer.
    // Returns true and fills out/body/cseq/method/statusCode if a complete message
    // (headers + optional Content-Length body) is present; removes consumed bytes.
    struct RtspMessage {
        QString method;       // e.g., "OPTIONS", "GET_PARAMETER" — empty for responses
        QString uri;          // for requests
        int     statusCode{0};// 0 for requests
        int     cseq{0};
        QString contentType;
        int     contentLength{0};
        QString session;      // raw Session header value, if any
        QByteArray body;
        bool    isRequest{false};
    };
    bool parseNextRtspMessage(RtspMessage& msg);
    void handleRtspRequest(const RtspMessage& msg);
    void handleRtspResponse(const RtspMessage& msg);

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
    void*             m_qmlVideoItem   = nullptr;   // stored for initMiracastPipeline()

    QString           m_presentationUrl;            // wfd_presentation_URL from M4
    QString           m_sessionId;                  // Session id from the M6 SETUP response
    int               m_m2Cseq         = -1;        // CSeq of our M2 OPTIONS
    int               m_setupCseq      = -1;        // CSeq of our M6 SETUP
    int               m_playCseq       = -1;        // CSeq of our M7 PLAY
    QTimer            m_healthTimer;                // drives checkStreamHealth()
    uint64_t          m_lastStreamErrors = 0;
    // IDR backoff while no frame has decoded (checkStreamHealth, 1 tick = 1 s)
    static constexpr int kMaxIdrIntervalSec = 16;
    static constexpr int kNoVideoWarnSec    = 10;
    int               m_healthTicks    = 0;
    int               m_idrInterval    = 1;
    int               m_nextIdrTick    = 1;

    // Accumulation buffers (TCP is a byte stream; messages may be split or coalesced)
    QByteArray        m_rtspBuffer;
    QByteArray        m_miceBuffer;
};

} // namespace airshow
