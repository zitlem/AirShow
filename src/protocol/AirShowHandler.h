#pragma once
#include "protocol/ProtocolHandler.h"
#include <QObject>
#include <QByteArray>

// Forward declarations — avoid pulling in Qt network headers in the interface
class QTcpServer;
class QTcpSocket;

namespace airshow {

class ConnectionBridge;
class MediaPipeline;
class SecurityManager;

// AirShow custom protocol handler.
//
// Implements the receiver (sink) side of the AirShow protocol.
// The handler listens on TCP port 7400 for sender connections from companion
// Flutter sender apps (Phases 10-13). The protocol has two phases:
//
//   1. JSON handshake: sender sends HELLO JSON (newline-terminated), receiver
//      responds with HELLO_ACK JSON including accepted codec, resolution, bitrate, fps.
//   2. Binary streaming: sender sends 16-byte framed VIDEO_NAL payloads which are
//      pushed directly into MediaPipeline::videoAppsrc() via gst_app_src_push_buffer.
//
// Frame header layout (16 bytes, all fields big-endian):
//   Byte 0:    type    (0x01=VIDEO_NAL, 0x02=AUDIO, 0x03=KEEPALIVE)
//   Byte 1:    flags   (0x01=keyframe, 0x02=end_of_AU)
//   Bytes 2-5: length  (uint32 payload byte count, not including header)
//   Bytes 6-13: pts    (int64 nanoseconds, GstClockTime)
//   Bytes 14-15: reserved (zero)
//
// Thread model: all I/O runs on the Qt event loop thread — no manual threading.
// Single-session model: only one sender connection is accepted at a time.
class AirShowHandler : public QObject, public ProtocolHandler {
    Q_OBJECT
public:
    // Connection/streaming state machine
    enum class State { Idle, Handshake, Streaming };

    // Frame types for 16-byte binary header
    static constexpr uint8_t  kTypeVideoNal  = 0x01;
    static constexpr uint8_t  kTypeAudio     = 0x02;
    static constexpr uint8_t  kTypeKeepalive = 0x03;
    static constexpr uint8_t  kFlagKeyframe  = 0x01;
    static constexpr uint8_t  kFlagEndOfAU   = 0x02;
    static constexpr uint16_t kAirShowPort   = 7400;
    static constexpr int      kFrameHeaderSize = 16;

    // Parsed representation of one 16-byte binary frame header
    struct FrameHeader {
        uint8_t  type;
        uint8_t  flags;
        uint32_t length;   // payload byte count (not including header)
        int64_t  pts;      // nanoseconds (GstClockTime unit)
    };

    explicit AirShowHandler(ConnectionBridge* bridge, QObject* parent = nullptr);
    ~AirShowHandler() override;

    // ProtocolHandler interface
    bool        start() override;
    void        stop() override;
    std::string name() const override { return "airshow"; }
    bool        isRunning() const override { return m_running; }
    void        setMediaPipeline(MediaPipeline* pipeline) override;

    // Security integration. Call before start(). If null, all local connections
    // are admitted (backward compatible).
    void        setSecurityManager(SecurityManager* sm);

    // Public for unit testing — parses 16-byte header from raw bytes.
    // Returns false if data is shorter than kFrameHeaderSize.
    static bool parseFrameHeader(const QByteArray& data, FrameHeader& out);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleHandshake();
    void handleStreamingData();
    void processFrame(const QByteArray& frameData);
    void disconnectClient();

    QTcpServer*       m_server           = nullptr;
    QTcpSocket*       m_client           = nullptr;
    ConnectionBridge* m_connectionBridge;
    MediaPipeline*    m_pipeline          = nullptr;
    SecurityManager*  m_security          = nullptr;
    State             m_state             = State::Idle;
    bool              m_running           = false;
    bool              m_audioCapSet       = false;  // true after PCM caps set on audioAppsrc
    QByteArray        m_readBuffer;
    QString           m_clientDeviceName;
};

} // namespace airshow
