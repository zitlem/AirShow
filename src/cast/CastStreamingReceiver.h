#pragma once
#include "cast/CastStreamingOffer.h"

#include <QHostAddress>
#include <QObject>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class QTimer;
class QUdpSocket;

namespace airshow::cast {

// Receives one Google Cast Streaming session (Chrome tab / desktop mirroring).
//
// Wire format per openscreen cast/streaming/impl: every stream shares one UDP
// port. RTP packets carry an 18–19 byte Cast header (key-frame flag, 8-bit frame
// ID, packet ID / max packet ID, optional extensions) followed by a chunk of an
// AES-128-CTR encrypted frame — no codec-specific payload framing. Frames are
// reassembled, decrypted (IV = aesIvMask XOR [0×8 | BE32(frame id) | 0×4]) and
// delivered in frame-ID order.
//
// The sender stops after ~66 ms of unacknowledged media, so the receiver must
// keep sending RTCP: RR (+ LSR/DLSR once a Sender Report arrives), XR/RRTR,
// optional PLI, and Cast Feedback (checkpoint frame, NACKs, ACK bit vector) —
// immediately when the checkpoint advances or an SR arrives, every 30 ms while
// NACKs are outstanding, otherwise every 500 ms.
//
// Runs on the thread that owns it (Qt event loop); frames are delivered there.
class CastStreamingReceiver : public QObject {
    Q_OBJECT
public:
    using FrameCallback = std::function<void(CastStream::Kind kind, const uint8_t* data,
                                             size_t size, bool keyFrame)>;

    explicit CastStreamingReceiver(QObject* parent = nullptr);
    ~CastStreamingReceiver() override;

    // Bind a UDP port (0 = any free port) and start receiving the given streams.
    bool start(const std::vector<CastStream>& streams, uint16_t port = 0);
    void stop();
    uint16_t localPort() const;

    // Only accept media from this address (the Cast control connection's peer).
    void setExpectedSender(const QHostAddress& address) { m_expectedSender = address; }

    void setFrameCallback(FrameCallback cb) { m_frameCallback = std::move(cb); }

    // Ask the sender for a key frame on the video stream (PLI).
    void requestKeyFrame();

    uint64_t framesDelivered(CastStream::Kind kind) const;

private slots:
    void onReadyRead();
    void onTick();

private:
    using Clock = std::chrono::steady_clock;

    struct PendingFrame {
        int                     maxPacketId = -1;
        std::vector<QByteArray> chunks;
        std::vector<bool>       have;
        int                     received = 0;
        bool                    keyFrame = false;
        bool complete() const { return maxPacketId >= 0 && received == maxPacketId + 1; }
    };

    struct StreamState {
        CastStream config;
        int64_t    highestFrameId = 0;    // reference for expanding 8-bit frame IDs
        int64_t    checkpoint     = -1;   // all frames <= this have been delivered
        std::map<int64_t, PendingFrame> frames;
        Clock::time_point stalledSince{};  // checkpoint+1 incomplete while later frames exist

        bool              haveSr = false;
        uint32_t          lastSrNtpMiddle = 0;
        Clock::time_point lastSrArrival{};

        bool     haveSeq = false;
        uint16_t maxSeq = 0;
        uint32_t seqCycles = 0;

        bool              pliPending = false;
        uint8_t           feedbackCount = 0;
        Clock::time_point nextRtcp{};
        uint64_t          delivered = 0;
    };

    StreamState* streamBySenderSsrc(uint32_t ssrc);
    void handleRtp(StreamState& s, const uint8_t* data, size_t size);
    void handleRtcp(const uint8_t* data, size_t size, const QHostAddress& from, quint16 fromPort);
    void deliverReadyFrames(StreamState& s);
    void skipStalledFrames(StreamState& s, Clock::time_point now);
    void deliver(StreamState& s, int64_t frameId, const PendingFrame& frame);
    void sendRtcp(StreamState& s);
    uint64_t nextNtpTimestamp();

    QUdpSocket*   m_socket = nullptr;
    QTimer*       m_tick   = nullptr;
    QHostAddress  m_expectedSender;
    QHostAddress  m_senderAddress;   // learned from the first valid packet
    quint16       m_senderPort = 0;
    std::vector<std::unique_ptr<StreamState>> m_streams;
    FrameCallback m_frameCallback;
    uint64_t      m_lastNtp = 0;
    QByteArray    m_datagram;
};

} // namespace airshow::cast
