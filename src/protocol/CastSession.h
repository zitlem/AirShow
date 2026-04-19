#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <cstdint>
#include <string>

// Forward declarations
class QSslSocket;

// Protobuf forward declaration (from cast_channel.pb.h generated at build time)
namespace extensions::api::cast_channel {
class CastMessage;
}

namespace airshow {

class ConnectionBridge;
class MediaPipeline;
class SecurityManager;

// Per-connection CASTV2 session state machine (D-06, D-07, D-13).
//
// Lifecycle:
//   1. CastHandler creates CastSession on TLS connection accept.
//   2. CastSession owns the QSslSocket for the duration of the session.
//   3. When socket disconnects or CLOSE message is received, CastSession
//      emits finished() and schedules self-destruction.
//
// Framing: every CASTV2 message is a 4-byte big-endian uint32 length prefix
// followed by a serialized CastMessage protobuf (Pattern 1 from RESEARCH.md).
//
// Namespace dispatch routes messages to per-namespace handler methods:
//   deviceauth  ->  onDeviceAuth()   — auth challenge/response bypass
//   connection  ->  onConnection()   — CONNECT/CLOSE virtual connections
//   heartbeat   ->  onHeartbeat()    — PING/PONG keepalive
//   receiver    ->  onReceiver()     — LAUNCH/GET_STATUS/STOP
//   media       ->  onMedia()        — GET_STATUS/LOAD (uridecodebin)
//   webrtc      ->  onWebrtc()       — stub (Plan 02 will implement)
class CastSession : public QObject {
    Q_OBJECT
public:
    explicit CastSession(QSslSocket* socket,
                         ConnectionBridge* connectionBridge,
                         MediaPipeline* pipeline,
                         SecurityManager* securityManager = nullptr,
                         QObject* parent = nullptr);
    ~CastSession() override;

    // Phase 6: Translate Cast OFFER JSON to a standard SDP string for webrtcbin.
    // Exposed as public static for unit testing in test_cast.cpp.
    // offerJson: the top-level JSON object of the OFFER message (containing "offer" sub-object).
    static std::string buildSdpFromOffer(const QJsonObject& offerJson);

    // Returns true once the sender has sent CONNECT to our transportId — i.e.
    // the session is an active cast, not just a discovery probe (DeviceAuth only).
    bool isActive() const { return m_didConnect; }

signals:
    // Emitted when the session ends (socket disconnected or CLOSE received).
    // CastHandler connects this to its onSessionFinished slot.
    void finished();

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    // TCP framing state machine
    enum class ReadState { READING_HEADER, READING_PAYLOAD };

    // Dispatch a fully assembled CastMessage to the appropriate handler.
    void dispatchMessage(const extensions::api::cast_channel::CastMessage& msg);

    // Per-namespace handlers
    void onDeviceAuth(const extensions::api::cast_channel::CastMessage& msg);
    void onConnection(const extensions::api::cast_channel::CastMessage& msg);
    void onHeartbeat(const extensions::api::cast_channel::CastMessage& msg);
    void onReceiver(const extensions::api::cast_channel::CastMessage& msg);
    void onMedia(const extensions::api::cast_channel::CastMessage& msg);
    void onWebrtc(const extensions::api::cast_channel::CastMessage& msg);

    // Send a serialized CastMessage with 4-byte big-endian length prefix.
    void sendMessage(const extensions::api::cast_channel::CastMessage& msg);

    // Build a CastMessage with STRING payload from a QJsonObject.
    // src/dst are the source_id/destination_id fields.
    extensions::api::cast_channel::CastMessage makeJsonMsg(
        const QString& src,
        const QString& dst,
        const QString& ns,
        const QByteArray& jsonPayload);

    // Build a RECEIVER_STATUS JSON payload for the given state.
    QByteArray buildReceiverStatus(int requestId) const;

    QSslSocket*       m_socket           = nullptr;
    ConnectionBridge* m_connectionBridge = nullptr;
    MediaPipeline*    m_pipeline         = nullptr;
    SecurityManager*  m_securityManager  = nullptr;

    // Set to true once SecurityManager approval has been granted for this session.
    // Approval is requested lazily on the first CONNECT or LAUNCH message (not on
    // raw TCP connection, which is too early — many devices probe port 8009 during
    // discovery without sending any Cast protocol messages).
    bool m_approved = false;

    // Set to true when this session calls setConnected(true) on the ConnectionBridge.
    // Only sessions that set connected=true should reset it to false on teardown.
    // Probe sessions (auth-only, no CONNECT to transportId) never set this, so they
    // cannot accidentally wipe another protocol's connected state.
    bool m_didConnect = false;

    // TCP accumulation buffer (never blocking — state machine per Pitfall 6)
    QByteArray m_buffer;
    ReadState  m_readState    = ReadState::READING_HEADER;
    uint32_t   m_expectedLen  = 0;

    // Application state
    QString    m_transportId;   // unique per session, assigned at construction
    bool       m_appLaunched   = false;
    QString    m_launchedAppId;
    QString    m_senderName;    // populated on CONNECT message
};

} // namespace airshow
