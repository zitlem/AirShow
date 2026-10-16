#include "cast/CastStreamingReceiver.h"

#include <QDebug>
#include <QTimer>
#include <QUdpSocket>

#include <openssl/evp.h>

#include <algorithm>
#include <chrono>

namespace airshow::cast {

namespace {

constexpr int     kMaxFramesAhead       = 120;   // openscreen kMaxUnackedFrames
constexpr auto    kNackInterval         = std::chrono::milliseconds(30);
constexpr auto    kReportInterval       = std::chrono::milliseconds(500);
constexpr auto    kTickInterval         = std::chrono::milliseconds(10);
constexpr size_t  kRtpMinSize           = 18;
constexpr uint8_t kRtcpSenderReport     = 200;
constexpr uint8_t kRtcpReceiverReport   = 201;
constexpr uint8_t kRtcpPayloadFeedback  = 206;
constexpr uint8_t kRtcpExtendedReport   = 207;
constexpr uint32_t kCastFourCC  = 0x43415354;  // "CAST"
constexpr uint32_t kCst2FourCC  = 0x43535432;  // "CST2"

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }

void put8(QByteArray& b, uint8_t v) { b.append(static_cast<char>(v)); }
void put16(QByteArray& b, uint16_t v) { put8(b, uint8_t(v >> 8)); put8(b, uint8_t(v)); }
void put32(QByteArray& b, uint32_t v) { put16(b, uint16_t(v >> 16)); put16(b, uint16_t(v)); }
void put64(QByteArray& b, uint64_t v) { put32(b, uint32_t(v >> 32)); put32(b, uint32_t(v)); }

// Expand a truncated 8-bit frame ID to the value nearest `reference`
// (window [reference-128, reference+127]) — openscreen ExpandedValueBase::Expand.
int64_t expandFrameId(int64_t reference, uint8_t truncated) {
    const int64_t max = reference + 127;
    int64_t result = (max & ~int64_t{0xFF}) | truncated;
    if (result > max) result -= 256;
    return result;
}

// RTP vs RTCP on the shared port (openscreen packet_util.cc).
bool looksLikeRtp(const uint8_t* d, size_t n) {
    if (n < kRtpMinSize || d[0] != 0x80) return false;
    const int pt = d[1] & 0x7F;
    return (pt >= 96 && pt <= 104) || pt == 127;
}
bool looksLikeRtcp(const uint8_t* d, size_t n) {
    return n >= 8 && (d[0] >> 5) == 0b100 && d[1] >= 200 && d[1] <= 207;
}

// Patch a packet's length field: 32-bit words after the 4-byte header.
void finishRtcpPacket(QByteArray& b, int start) {
    const int words = (b.size() - start) / 4 - 1;
    b[start + 2] = static_cast<char>(words >> 8);
    b[start + 3] = static_cast<char>(words & 0xFF);
}

} // namespace

CastStreamingReceiver::CastStreamingReceiver(QObject* parent) : QObject(parent) {}

CastStreamingReceiver::~CastStreamingReceiver() { stop(); }

bool CastStreamingReceiver::start(const std::vector<CastStream>& streams, uint16_t port) {
    stop();
    if (streams.empty()) return false;

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::AnyIPv4, port)) {
        qWarning("CastStreamingReceiver: UDP bind failed: %s", qPrintable(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    // A 1080p key frame is a few hundred packets arriving back to back.
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
    connect(m_socket, &QUdpSocket::readyRead, this, &CastStreamingReceiver::onReadyRead);

    for (const CastStream& c : streams) {
        auto s = std::make_unique<StreamState>();
        s->config = c;
        m_streams.push_back(std::move(s));
    }

    m_tick = new QTimer(this);
    m_tick->setTimerType(Qt::PreciseTimer);
    connect(m_tick, &QTimer::timeout, this, &CastStreamingReceiver::onTick);
    m_tick->start(kTickInterval);

    qDebug("CastStreamingReceiver: listening on UDP %u for %zu stream(s)",
           localPort(), m_streams.size());
    return true;
}

void CastStreamingReceiver::stop() {
    if (m_tick) { m_tick->stop(); delete m_tick; m_tick = nullptr; }
    if (m_socket) { m_socket->close(); delete m_socket; m_socket = nullptr; }
    m_streams.clear();
    m_senderAddress.clear();
    m_senderPort = 0;
}

uint16_t CastStreamingReceiver::localPort() const {
    return m_socket ? m_socket->localPort() : 0;
}

uint64_t CastStreamingReceiver::framesDelivered(CastStream::Kind kind) const {
    uint64_t n = 0;
    for (const auto& s : m_streams)
        if (s->config.kind == kind) n += s->delivered;
    return n;
}

void CastStreamingReceiver::requestKeyFrame() {
    for (auto& s : m_streams) {
        if (s->config.kind == CastStream::Kind::Video && !s->pliPending) {
            s->pliPending = true;
            sendRtcp(*s);
        }
    }
}

CastStreamingReceiver::StreamState* CastStreamingReceiver::streamBySenderSsrc(uint32_t ssrc) {
    for (auto& s : m_streams)
        if (s->config.senderSsrc == ssrc) return s.get();
    return nullptr;
}

// ── Receive ──────────────────────────────────────────────────────────────────

void CastStreamingReceiver::onReadyRead() {
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const qint64 pending = m_socket->pendingDatagramSize();
        m_datagram.resize(pending > 0 ? pending : 65536);
        QHostAddress from;
        quint16 fromPort = 0;
        const qint64 n = m_socket->readDatagram(m_datagram.data(), m_datagram.size(), &from, &fromPort);
        if (n <= 0) continue;
        if (!m_expectedSender.isNull() && !from.isEqual(m_expectedSender, QHostAddress::TolerantConversion))
            continue;

        const auto* d = reinterpret_cast<const uint8_t*>(m_datagram.constData());
        const size_t size = static_cast<size_t>(n);
        if (looksLikeRtp(d, size)) {
            StreamState* s = streamBySenderSsrc(be32(d + 8));
            if (!s) continue;
            // RTCP goes back to wherever the media comes from.
            m_senderAddress = from;
            m_senderPort    = fromPort;
            handleRtp(*s, d, size);
        } else if (looksLikeRtcp(d, size)) {
            handleRtcp(d, size, from, fromPort);
        }
    }
}

void CastStreamingReceiver::handleRtp(StreamState& s, const uint8_t* d, size_t n) {
    // Receive statistics for the RR's extended highest sequence number.
    const uint16_t seq = be16(d + 2);
    if (!s.haveSeq) {
        s.haveSeq = true;
        s.maxSeq  = seq;
    } else if (uint16_t(seq - s.maxSeq) < 0x8000) {
        if (seq < s.maxSeq) ++s.seqCycles;
        s.maxSeq = seq;
    }

    // Cast header (byte 12 on): |K|R| ext count | frame id | packet id | max packet id | [ref id] | ext...
    const bool    keyFrame    = d[12] & 0x80;
    const bool    hasRefId    = d[12] & 0x40;
    const int     extCount    = d[12] & 0x3F;
    const uint8_t frameId8    = d[13];
    const int     packetId    = be16(d + 14);
    const int     maxPacketId = be16(d + 16);
    if (maxPacketId == 0xFFFF || packetId > maxPacketId) return;

    size_t off = kRtpMinSize + (hasRefId ? 1 : 0);
    for (int i = 0; i < extCount; ++i) {
        if (off + 2 > n) return;
        const int extSize = be16(d + off) & 0x3FF;  // type = top 6 bits (1 = playout delay)
        off += 2 + static_cast<size_t>(extSize);
    }
    if (off > n) return;

    const int64_t frameId = expandFrameId(s.highestFrameId, frameId8);
    if (frameId <= s.checkpoint || frameId > s.checkpoint + kMaxFramesAhead) return;
    s.highestFrameId = std::max(s.highestFrameId, frameId);

    PendingFrame& f = s.frames[frameId];
    if (f.maxPacketId < 0) {
        f.maxPacketId = maxPacketId;
        f.chunks.resize(static_cast<size_t>(maxPacketId) + 1);
        f.have.resize(static_cast<size_t>(maxPacketId) + 1, false);
    } else if (f.maxPacketId != maxPacketId) {
        return;  // inconsistent with earlier packets of this frame
    }
    if (packetId == 0) f.keyFrame = keyFrame;
    if (f.have[static_cast<size_t>(packetId)]) return;  // retransmitted duplicate
    f.have[static_cast<size_t>(packetId)] = true;
    f.chunks[static_cast<size_t>(packetId)] =
        QByteArray(reinterpret_cast<const char*>(d + off), static_cast<qsizetype>(n - off));
    ++f.received;

    if (f.complete() && frameId == s.checkpoint + 1) deliverReadyFrames(s);
}

void CastStreamingReceiver::handleRtcp(const uint8_t* d, size_t n,
                                       const QHostAddress& from, quint16 fromPort) {
    // Compound packet: walk each sub-packet by its length field.
    size_t off = 0;
    while (off + 4 <= n) {
        const size_t len = (size_t(be16(d + off + 2)) + 1) * 4;
        if ((d[off] >> 6) != 2 || off + len > n) break;
        if (d[off + 1] == kRtcpSenderReport && len >= 28) {
            if (StreamState* s = streamBySenderSsrc(be32(d + off + 4))) {
                // The sender's first SR precedes its first RTP packet.
                m_senderAddress    = from;
                m_senderPort       = fromPort;
                s->haveSr          = true;
                s->lastSrNtpMiddle = static_cast<uint32_t>(be64(d + off + 8) >> 16);
                s->lastSrArrival   = Clock::now();
                sendRtcp(*s);  // lets the sender measure RTT right away
            }
        }
        off += len;
    }
}

// ── Frame delivery ───────────────────────────────────────────────────────────

void CastStreamingReceiver::deliverReadyFrames(StreamState& s) {
    bool advanced = false;
    for (auto it = s.frames.find(s.checkpoint + 1);
         it != s.frames.end() && it->first == s.checkpoint + 1 && it->second.complete();
         it = s.frames.find(s.checkpoint + 1)) {
        deliver(s, it->first, it->second);
        if (it->second.keyFrame) s.pliPending = false;
        s.checkpoint = it->first;
        s.frames.erase(it);
        advanced = true;
    }
    if (advanced) {
        s.stalledSince = {};
        sendRtcp(s);  // advancing the checkpoint is what lets the sender continue
    }
}

void CastStreamingReceiver::deliver(StreamState& s, int64_t frameId, const PendingFrame& frame) {
    QByteArray data;
    for (const QByteArray& chunk : frame.chunks) data.append(chunk);

    // AES-128-CTR over the whole frame; IV = mask XOR [0×8 | BE32(frame id) | 0×4].
    std::array<uint8_t, 16> iv{};
    const uint32_t id32 = static_cast<uint32_t>(frameId);
    iv[8]  = uint8_t(id32 >> 24);
    iv[9]  = uint8_t(id32 >> 16);
    iv[10] = uint8_t(id32 >> 8);
    iv[11] = uint8_t(id32);
    for (size_t i = 0; i < iv.size(); ++i) iv[i] ^= s.config.aesIvMask[i];

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outLen = 0;
    const bool ok = ctx &&
        EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, s.config.aesKey.data(), iv.data()) == 1 &&
        EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(data.data()), &outLen,
                          reinterpret_cast<const unsigned char*>(data.constData()),
                          static_cast<int>(data.size())) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        qWarning("CastStreamingReceiver: AES-CTR decrypt failed for frame %lld",
                 static_cast<long long>(frameId));
        return;
    }

    if (s.delivered++ == 0) {
        qDebug("CastStreamingReceiver: first %s frame (%s, %lld bytes)",
               s.config.kind == CastStream::Kind::Video ? "video" : "audio",
               s.config.codec.c_str(), static_cast<long long>(data.size()));
    }
    if (m_frameCallback) {
        m_frameCallback(s.config.kind, reinterpret_cast<const uint8_t*>(data.constData()),
                        static_cast<size_t>(data.size()), frame.keyFrame);
    }
}

// If checkpoint+1 can't be completed (lost beyond retransmission) while newer
// frames keep arriving, jump ahead: audio to the next complete frame, video to
// the next complete key frame (requesting one with PLI if none is buffered).
void CastStreamingReceiver::skipStalledFrames(StreamState& s, Clock::time_point now) {
    const auto next = s.frames.find(s.checkpoint + 1);
    const bool blocked = !s.frames.empty() &&
                         (next == s.frames.end() || !next->second.complete()) &&
                         s.highestFrameId > s.checkpoint + 1;
    if (!blocked) {
        s.stalledSince = {};
        return;
    }
    if (s.stalledSince == Clock::time_point{}) {
        s.stalledSince = now;
        return;
    }
    const auto limit = std::chrono::milliseconds(std::max(s.config.targetDelayMs, 250));
    if (now - s.stalledSince < limit) return;

    const bool video = s.config.kind == CastStream::Kind::Video;
    for (auto it = s.frames.begin(); it != s.frames.end(); ++it) {
        if (it->second.complete() && (!video || it->second.keyFrame)) {
            qDebug("CastStreamingReceiver: skipping %s frames %lld..%lld (lost)",
                   video ? "video" : "audio", static_cast<long long>(s.checkpoint + 1),
                   static_cast<long long>(it->first - 1));
            s.frames.erase(s.frames.begin(), it);
            s.checkpoint = s.frames.begin()->first - 1;
            deliverReadyFrames(s);
            return;
        }
    }
    if (video && !s.pliPending) {
        s.pliPending = true;
        sendRtcp(s);
    }
}

// ── RTCP ─────────────────────────────────────────────────────────────────────

void CastStreamingReceiver::onTick() {
    const auto now = Clock::now();
    for (auto& s : m_streams) {
        skipStalledFrames(*s, now);
        if (now >= s->nextRtcp) sendRtcp(*s);
    }
}

uint64_t CastStreamingReceiver::nextNtpTimestamp() {
    using namespace std::chrono;
    const auto sinceEpoch = system_clock::now().time_since_epoch();
    const uint64_t secs   = static_cast<uint64_t>(duration_cast<seconds>(sinceEpoch).count());
    const uint64_t micros = static_cast<uint64_t>(duration_cast<microseconds>(sinceEpoch).count() % 1000000);
    uint64_t ntp = ((secs + 2208988800ULL) << 32) | ((micros << 32) / 1000000);
    // The sender drops RTCP whose RRTR time doesn't increase.
    if (ntp <= m_lastNtp) ntp = m_lastNtp + 1;
    m_lastNtp = ntp;
    return ntp;
}

void CastStreamingReceiver::sendRtcp(StreamState& s) {
    if (!m_socket || m_senderPort == 0) return;  // sender endpoint not known yet
    const auto now = Clock::now();
    const uint32_t rx = s.config.receiverSsrc();
    const uint32_t tx = s.config.senderSsrc;
    QByteArray b;

    // Receiver Report — with a report block once an SR has arrived (LSR/DLSR → RTT).
    int start = b.size();
    put8(b, s.haveSr ? 0x81 : 0x80); put8(b, kRtcpReceiverReport); put16(b, 0);
    put32(b, rx);
    if (s.haveSr) {
        put32(b, tx);
        put32(b, 0);                                       // fraction lost | cumulative lost
        put32(b, (s.seqCycles << 16) | s.maxSeq);          // extended highest sequence number
        put32(b, 0);                                       // jitter
        put32(b, s.lastSrNtpMiddle);                       // LSR
        const auto sinceSr = std::chrono::duration_cast<std::chrono::microseconds>(now - s.lastSrArrival);
        put32(b, static_cast<uint32_t>((uint64_t(sinceSr.count()) << 16) / 1000000));  // DLSR, 1/65536 s
    }
    finishRtcpPacket(b, start);

    // XR with a Receiver Reference Time Report block.
    start = b.size();
    put8(b, 0x80); put8(b, kRtcpExtendedReport); put16(b, 0);
    put32(b, rx);
    put8(b, 4); put8(b, 0); put16(b, 2);
    put64(b, nextNtpTimestamp());
    finishRtcpPacket(b, start);

    // Picture Loss Indication while a key frame is wanted.
    if (s.pliPending) {
        start = b.size();
        put8(b, 0x81); put8(b, kRtcpPayloadFeedback); put16(b, 0);
        put32(b, rx); put32(b, tx);
        finishRtcpPacket(b, start);
    }

    // Cast Feedback: checkpoint, NACKs for missing packets, ACKs for frames beyond it.
    struct Loss { uint8_t frame; uint16_t packet; uint8_t bits; };
    std::vector<Loss> losses;
    for (int64_t id = s.checkpoint + 1; id < s.highestFrameId && losses.size() < 255; ++id) {
        const auto it = s.frames.find(id);
        if (it == s.frames.end()) {
            losses.push_back({uint8_t(id), 0xFFFF, 0});  // whole frame missing
            continue;
        }
        const PendingFrame& f = it->second;
        for (int pid = 0; pid <= f.maxPacketId && losses.size() < 255; ++pid) {
            if (f.have[size_t(pid)]) continue;
            uint8_t bits = 0;
            for (int i = 1; i <= 8 && pid + i <= f.maxPacketId; ++i)
                if (!f.have[size_t(pid + i)]) bits |= uint8_t(1 << (i - 1));
            losses.push_back({uint8_t(id), uint16_t(pid), bits});
            pid += 8;  // the bit vector covers the next 8 packet IDs
        }
    }
    uint8_t acks[2] = {0, 0};  // bit b of octet k ACKs frame checkpoint + 2 + 8k + b
    for (const auto& [id, f] : s.frames) {
        const int64_t bit = id - (s.checkpoint + 2);
        if (bit >= 0 && bit < 16 && f.complete()) acks[bit / 8] |= uint8_t(1 << (bit % 8));
    }

    start = b.size();
    put8(b, 0x8F); put8(b, kRtcpPayloadFeedback); put16(b, 0);
    put32(b, rx); put32(b, tx);
    put32(b, kCastFourCC);
    put8(b, uint8_t(s.checkpoint));                  // -1 (nothing yet) goes out as 0xFF
    put8(b, uint8_t(losses.size()));
    put16(b, uint16_t(std::max(s.config.targetDelayMs, 1)));
    for (const Loss& l : losses) { put8(b, l.frame); put16(b, l.packet); put8(b, l.bits); }
    put32(b, kCst2FourCC);
    put8(b, s.feedbackCount++);
    put8(b, 2);
    put8(b, acks[0]); put8(b, acks[1]);
    finishRtcpPacket(b, start);

    m_socket->writeDatagram(b, m_senderAddress, m_senderPort);
    s.nextRtcp = now + (losses.empty() ? kReportInterval : kNackInterval);
}

} // namespace airshow::cast
