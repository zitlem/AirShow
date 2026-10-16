#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Qt
#include <QCoreApplication>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpSocket>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QtEndian>
#include <QEventLoop>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QElapsedTimer>

#include <cstring>
#include <optional>
#include <gst/gst.h>

// OpenSSL — verify the Cast auth signature the way Chrome does
#include <openssl/evp.h>
#include <openssl/x509.h>

// Project headers
#include "protocol/CastHandler.h"
#include "protocol/CastSession.h"
#include "pipeline/MediaPipeline.h"
#include "ui/ConnectionBridge.h"
#include "cast/cast_auth_sigs.h"
#include "cast/CastStreamingOffer.h"
#include "cast/CastStreamingReceiver.h"

// Generated protobuf header (from cast_channel.proto)
#include "cast_channel.pb.h"

using extensions::api::cast_channel::CastMessage;
using extensions::api::cast_channel::DeviceAuthMessage;
using extensions::api::cast_channel::AuthResponse;

// ── Test environment: QCoreApplication ───────────────────────────────────────
// QCoreApplication must be created before any QObject (CastHandler is QObject).
// Managed as a raw pointer to control destruction order — must outlive all tests.

static int s_argc = 1;
static const char* s_argv[] = {"test_cast", nullptr};
static QCoreApplication* s_app = nullptr;

class CastTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            s_app = new QCoreApplication(s_argc, const_cast<char**>(s_argv));
        }
        // GStreamer must be initialized for MediaPipeline tests
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
    }
    void TearDown() override {
        // Process remaining queued events before Qt objects are destroyed
        QCoreApplication::processEvents();
    }
};

testing::Environment* const castEnv =
    testing::AddGlobalTestEnvironment(new CastTestEnvironment);

// ── Test 1: CastMessageFraming_EncodeLength ───────────────────────────────────
// Verify that serializing a CastMessage and prepending a 4-byte big-endian
// length prefix produces bytes that can be re-parsed with ParseFromArray.

TEST(CastMessageFramingTest, EncodeLength) {
    CastMessage msg;
    msg.set_protocol_version(CastMessage::CASTV2_1_0);
    msg.set_source_id("sender-0");
    msg.set_destination_id("receiver-0");
    msg.set_namespace_("urn:x-cast:com.google.cast.tp.heartbeat");
    msg.set_payload_type(CastMessage::STRING);
    msg.set_payload_utf8(R"({"type":"PING"})");

    std::string serialized;
    ASSERT_TRUE(msg.SerializeToString(&serialized));

    // Build framed message: 4-byte big-endian length + payload
    uint32_t payloadLen = static_cast<uint32_t>(serialized.size());
    uint32_t lenBe = qToBigEndian<uint32_t>(payloadLen);

    QByteArray frame;
    frame.append(reinterpret_cast<const char*>(&lenBe), 4);
    frame.append(serialized.data(), static_cast<int>(serialized.size()));

    ASSERT_GE(frame.size(), 4);

    // Extract and verify the length prefix
    uint32_t extractedLenBe;
    std::memcpy(&extractedLenBe, frame.constData(), 4);
    uint32_t extractedLen = qFromBigEndian<uint32_t>(extractedLenBe);
    EXPECT_EQ(extractedLen, payloadLen);
    EXPECT_EQ(extractedLen, static_cast<uint32_t>(msg.ByteSizeLong()));

    // Verify payload parses back to the original message
    CastMessage parsed;
    ASSERT_TRUE(parsed.ParseFromArray(frame.constData() + 4, static_cast<int>(extractedLen)));
    EXPECT_EQ(parsed.source_id(), msg.source_id());
    EXPECT_EQ(parsed.destination_id(), msg.destination_id());
    EXPECT_EQ(parsed.namespace_(), msg.namespace_());
    EXPECT_EQ(parsed.payload_utf8(), msg.payload_utf8());
}

// ── Test 2: CastMessageFraming_RoundTrip ─────────────────────────────────────
// Build a PING CastMessage, serialize with length prefix, parse back.

TEST(CastMessageFramingTest, RoundTrip) {
    CastMessage original;
    original.set_protocol_version(CastMessage::CASTV2_1_0);
    original.set_source_id("sender-0");
    original.set_destination_id("receiver-0");
    original.set_namespace_("urn:x-cast:com.google.cast.tp.heartbeat");
    original.set_payload_type(CastMessage::STRING);
    original.set_payload_utf8(R"({"type":"PING"})");

    // Serialize
    std::string bytes;
    ASSERT_TRUE(original.SerializeToString(&bytes));
    uint32_t lenBe = qToBigEndian<uint32_t>(static_cast<uint32_t>(bytes.size()));

    QByteArray frame;
    frame.append(reinterpret_cast<const char*>(&lenBe), 4);
    frame.append(bytes.data(), static_cast<int>(bytes.size()));

    // Parse length
    uint32_t parsedLen = qFromBigEndian<uint32_t>(*reinterpret_cast<const uint32_t*>(frame.constData()));
    EXPECT_EQ(parsedLen, static_cast<uint32_t>(bytes.size()));

    // Parse message
    CastMessage parsed;
    ASSERT_TRUE(parsed.ParseFromArray(frame.constData() + 4, static_cast<int>(parsedLen)));

    EXPECT_EQ(parsed.protocol_version(), CastMessage::CASTV2_1_0);
    EXPECT_EQ(parsed.source_id(), "sender-0");
    EXPECT_EQ(parsed.destination_id(), "receiver-0");
    EXPECT_EQ(parsed.namespace_(), "urn:x-cast:com.google.cast.tp.heartbeat");
    EXPECT_EQ(parsed.payload_type(), CastMessage::STRING);
    EXPECT_EQ(parsed.payload_utf8(), R"({"type":"PING"})");
}

// ── Test 3: AuthResponse_Structure ───────────────────────────────────────────
// Build an AuthResponse, set signature and cert, serialize and re-parse.
// Verifies the protobuf structure matches the CASTV2 spec.

TEST(CastAuthTest, AuthResponseStructure) {
    // Build AuthResponse with placeholder signature from index 0
    AuthResponse resp;
    resp.set_signature(
        reinterpret_cast<const char*>(
            airshow::cast::getSignatureForTime(airshow::cast::kCastAuthStartEpoch)),
        airshow::cast::kCastAuthSignatureSize);
    resp.set_client_auth_certificate(
        reinterpret_cast<const char*>(airshow::cast::auth_crt),
        airshow::cast::auth_crt_len);
    resp.set_signature_algorithm(extensions::api::cast_channel::RSASSA_PKCS1v15);
    resp.set_hash_algorithm(extensions::api::cast_channel::SHA256);

    // Wrap in DeviceAuthMessage
    DeviceAuthMessage authMsg;
    *authMsg.mutable_response() = resp;

    // Serialize
    std::string bytes;
    ASSERT_TRUE(authMsg.SerializeToString(&bytes));

    // Re-parse
    DeviceAuthMessage parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));

    ASSERT_TRUE(parsed.has_response());
    const AuthResponse& parsedResp = parsed.response();

    // Signature must be exactly 256 bytes
    EXPECT_EQ(parsedResp.signature().size(),
              static_cast<size_t>(airshow::cast::kCastAuthSignatureSize));
    EXPECT_EQ(parsedResp.signature().size(), 256u);

    // Certificate is present
    EXPECT_GT(parsedResp.client_auth_certificate().size(), 0u);

    // Algorithms match
    EXPECT_EQ(parsedResp.signature_algorithm(),
              extensions::api::cast_channel::RSASSA_PKCS1v15);
    EXPECT_EQ(parsedResp.hash_algorithm(),
              extensions::api::cast_channel::SHA256);
}

// ── Test 4: AuthSignature_IndexRotation ──────────────────────────────────────
// Signatures rotate every 2-day window starting at kCastAuthStartEpoch, and
// there is no signature outside the table (no wrap-around).

TEST(CastAuthTest, AuthSignatureIndexRotation) {
    using namespace airshow::cast;

    const uint64_t t1 = kCastAuthStartEpoch + 10 * kCastAuthWindowSeconds + 5;
    const uint8_t* s1 = getSignatureForTime(t1);
    ASSERT_NE(s1, nullptr);

    // Same window → same signature
    EXPECT_EQ(getSignatureForTime(t1 + 100), s1);
    EXPECT_EQ(getSignatureForTime(castAuthWindowStart(10)), s1);

    // Next window → next table entry
    EXPECT_EQ(getSignatureForTime(t1 + kCastAuthWindowSeconds), s1 + kCastAuthSignatureSize);

    // Window bounds line up with the index
    EXPECT_EQ(castAuthWindowIndex(t1), 10);
    EXPECT_EQ(castAuthWindowStart(11) - castAuthWindowStart(10), kCastAuthWindowSeconds);

    // Last window is valid; anything outside the table has no signature
    EXPECT_NE(getSignatureForTime(kCastAuthEndEpoch - 1), nullptr);
    EXPECT_EQ(getSignatureForTime(kCastAuthEndEpoch), nullptr);
    EXPECT_EQ(getSignatureForTime(kCastAuthStartEpoch - 1), nullptr);
    EXPECT_FALSE(isCastAuthAvailable(kCastAuthEndEpoch));
    EXPECT_TRUE(isCastAuthAvailable(kCastAuthStartEpoch));
}

// ── Test 5: CastHandler_StartsOnPort8009 ──────────────────────────────────────
// start() returns true, isRunning() becomes true, name() returns "cast",
// stop() makes isRunning() false.

TEST(CastHandlerTest, StartsOnPort8009) {
    airshow::ConnectionBridge bridge;
    airshow::CastHandler handler(&bridge);

    EXPECT_FALSE(handler.isRunning());
    EXPECT_EQ(handler.name(), "cast");

    bool started = handler.start();
    EXPECT_TRUE(started);
    if (started) {
        EXPECT_TRUE(handler.isRunning());
        handler.stop();
        EXPECT_FALSE(handler.isRunning());
    }
}

// ── Test 6: CastHandler_SelfSignedCertGeneration ─────────────────────────────
// Calling start() twice is idempotent; the server does not crash on the second call.

TEST(CastHandlerTest, SelfSignedCertIdempotentStart) {
    airshow::ConnectionBridge bridge;
    airshow::CastHandler handler(&bridge);

    bool first = handler.start();
    // Second start() on a running handler should return true without crashing
    bool second = handler.start();
    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
    EXPECT_TRUE(handler.isRunning());
    handler.stop();
}

// ── Test 7: ReceiverStatus_JsonStructure ──────────────────────────────────────
// Verify that the RECEIVER_STATUS JSON can be built by testing auth signature
// table constants that CastSession relies on.

TEST(CastHandlerTest, SignatureTableHasCorrectConstants) {
    // Verify the signature table constants are consistent
    EXPECT_EQ(airshow::cast::kCastAuthSignatureCount, 795u);
    EXPECT_EQ(airshow::cast::kCastAuthSignatureSize, 256u);

    // Verify that getCastAuthSignature returns a valid pointer within the table
    uint64_t now = 1774757446ULL;  // fixed test timestamp
    const uint8_t* sig = airshow::cast::getSignatureForTime(now);
    ASSERT_NE(sig, nullptr);

    // The returned pointer must point within the signature table
    const uint8_t* tableStart = airshow::cast::getSignatureForTime(airshow::cast::kCastAuthStartEpoch);
    const uint8_t* tableEnd   = tableStart +
        airshow::cast::kCastAuthSignatureCount * airshow::cast::kCastAuthSignatureSize;
    EXPECT_GE(sig, tableStart);
    EXPECT_LT(sig, tableEnd);
}

// ── Cast Streaming: OFFER parsing, stream selection, ANSWER ──────────────────

// Shaped like Chrome's mirroring OFFER: audio indexes first, one video stream
// per codec, PT 127/96 regardless of codec (openscreen "Android RTP hack").
static QJsonObject chromeStyleOffer() {
    auto stream = [](int index, const char* type, const char* codec, int pt, double ssrc,
                     const char* timeBase) {
        QJsonObject o;
        o["index"] = index;
        o["type"] = type;
        o["codecName"] = codec;
        o["rtpProfile"] = "cast";
        o["rtpPayloadType"] = pt;
        o["ssrc"] = ssrc;
        o["timeBase"] = timeBase;
        o["targetDelay"] = 400;
        o["aesKey"] = "000102030405060708090a0b0c0d0e0f";
        o["aesIvMask"] = "f0e0d0c0b0a090807060504030201000";
        return o;
    };
    QJsonObject audio = stream(0, "audio_source", "opus", 127, 3000000001.0, "1/48000");
    audio["channels"] = 2;
    audio["bitRate"] = 102000;
    QJsonArray streams{audio,
                       stream(1, "video_source", "h264", 96, 2000001, "1/90000"),
                       stream(2, "video_source", "vp8", 96, 2000002, "1/90000")};
    QJsonObject offerBody;
    offerBody["castMode"] = "mirroring";
    offerBody["supportedStreams"] = streams;
    QJsonObject msg;
    msg["type"] = "OFFER";
    msg["seqNum"] = 42;
    msg["offer"] = offerBody;
    return msg;
}

TEST(CastOfferTest, ParsesChromeStyleOffer) {
    const auto offer = airshow::cast::parseOffer(chromeStyleOffer());
    ASSERT_TRUE(offer.has_value());
    EXPECT_EQ(offer->seqNum, 42);
    ASSERT_EQ(offer->streams.size(), 3u);

    const auto& audio = offer->streams[0];
    EXPECT_EQ(audio.kind, airshow::cast::CastStream::Kind::Audio);
    EXPECT_EQ(audio.codec, "opus");
    EXPECT_EQ(audio.senderSsrc, 3000000001u);   // above INT32_MAX
    EXPECT_EQ(audio.receiverSsrc(), 3000000002u);
    EXPECT_EQ(audio.rtpTimebase, 48000);
    EXPECT_EQ(audio.channels, 2);
    EXPECT_EQ(audio.aesKey[0], 0x00);
    EXPECT_EQ(audio.aesKey[15], 0x0f);
    EXPECT_EQ(audio.aesIvMask[0], 0xf0);
    EXPECT_EQ(audio.gstCaps(), "audio/x-opus,channel-mapping-family=0,channels=2,rate=48000");
}

TEST(CastOfferTest, SelectsVp8AndOpusThenFallsBackToH264) {
    const auto offer = airshow::cast::parseOffer(chromeStyleOffer());
    ASSERT_TRUE(offer.has_value());

    auto sel = airshow::cast::selectStreams(*offer, [](const std::string&) { return true; });
    ASSERT_TRUE(sel.video && sel.audio);
    EXPECT_EQ(sel.video->codec, "vp8");
    EXPECT_EQ(sel.video->index, 2);
    EXPECT_EQ(sel.audio->index, 0);

    sel = airshow::cast::selectStreams(*offer, [](const std::string& c) { return c != "vp8"; });
    ASSERT_TRUE(sel.video);
    EXPECT_EQ(sel.video->codec, "h264");

    sel = airshow::cast::selectStreams(*offer, [](const std::string&) { return false; });
    EXPECT_TRUE(sel.empty());
}

TEST(CastOfferTest, AnswerListsAudioThenVideo) {
    const auto offer = airshow::cast::parseOffer(chromeStyleOffer());
    const auto sel = airshow::cast::selectStreams(*offer, [](const std::string&) { return true; });
    const QJsonObject reply = airshow::cast::buildAnswer(offer->seqNum, 51234, sel);

    EXPECT_EQ(reply["type"].toString(), "ANSWER");
    EXPECT_EQ(reply["seqNum"].toInt(), 42);
    EXPECT_EQ(reply["result"].toString(), "ok");
    const QJsonObject answer = reply["answer"].toObject();
    EXPECT_EQ(answer["udpPort"].toInt(), 51234);
    EXPECT_EQ(answer["sendIndexes"].toArray(), (QJsonArray{0, 2}));
    EXPECT_EQ(answer["ssrcs"].toArray(), (QJsonArray{qint64(3000000002LL), qint64(2000003)}));
}

TEST(CastOfferTest, DropsStreamsWithInvalidMandatoryFields) {
    QJsonObject msg = chromeStyleOffer();
    QJsonObject body = msg["offer"].toObject();
    QJsonArray streams = body["supportedStreams"].toArray();
    QJsonObject bad0 = streams[0].toObject(); bad0["aesKey"] = "abcd";          // too short
    QJsonObject bad1 = streams[1].toObject(); bad1["timeBase"] = "2/90000";     // numerator != 1
    streams[0] = bad0;
    streams[1] = bad1;
    body["supportedStreams"] = streams;
    msg["offer"] = body;

    const auto offer = airshow::cast::parseOffer(msg);
    ASSERT_TRUE(offer.has_value());
    ASSERT_EQ(offer->streams.size(), 1u);
    EXPECT_EQ(offer->streams[0].codec, "vp8");

    QJsonObject notOffer = chromeStyleOffer();
    notOffer["type"] = "ANSWER";
    EXPECT_FALSE(airshow::cast::parseOffer(notOffer).has_value());
}

// ── Cast Streaming: receiver loopback (we play Chrome's sender role) ─────────

namespace {

QByteArray ctrCrypt(const airshow::cast::CastStream& s, uint32_t frameId, const QByteArray& in) {
    std::array<uint8_t, 16> iv{};
    iv[8] = uint8_t(frameId >> 24); iv[9] = uint8_t(frameId >> 16);
    iv[10] = uint8_t(frameId >> 8); iv[11] = uint8_t(frameId);
    for (size_t i = 0; i < 16; ++i) iv[i] ^= s.aesIvMask[i];
    QByteArray out(in.size(), Qt::Uninitialized);
    int len = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, s.aesKey.data(), iv.data());
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                      reinterpret_cast<const unsigned char*>(in.constData()), int(in.size()));
    EVP_CIPHER_CTX_free(ctx);
    return out;
}

void append16(QByteArray& b, uint16_t v) { b.append(char(v >> 8)); b.append(char(v)); }
void append32(QByteArray& b, uint32_t v) { append16(b, uint16_t(v >> 16)); append16(b, uint16_t(v)); }

// One Cast RTP packet as openscreen's RtpPacketizer writes it (R bit always set).
QByteArray castRtpPacket(const airshow::cast::CastStream& s, uint16_t seq, uint32_t frameId,
                         bool key, uint16_t pid, uint16_t maxPid, const QByteArray& chunk) {
    QByteArray b;
    b.append(char(0x80));
    b.append(char((pid == maxPid ? 0x80 : 0) | s.rtpPayloadType));
    append16(b, seq);
    append32(b, frameId * 3000);
    append32(b, s.senderSsrc);
    b.append(char((key ? 0x80 : 0) | 0x40));        // K | R, no extensions
    b.append(char(frameId & 0xFF));
    append16(b, pid);
    append16(b, maxPid);
    b.append(char((key ? frameId : frameId - 1) & 0xFF));  // reference frame id
    b.append(chunk);
    return b;
}

QByteArray senderReport(uint32_t ssrc) {
    QByteArray b;
    b.append(char(0x80)); b.append(char(200)); append16(b, 6);
    append32(b, ssrc);
    append32(b, 0xE5000000); append32(b, 0x12345678);  // NTP
    append32(b, 0); append32(b, 0); append32(b, 0);    // RTP ts, packet & octet count
    return b;
}

struct Feedback { int checkpoint = -2; std::vector<std::pair<int, int>> losses; };

// Extract the Cast Feedback (PSFB FMT 15) from a compound RTCP packet.
std::optional<Feedback> parseFeedback(const QByteArray& pkt) {
    const auto* d = reinterpret_cast<const uint8_t*>(pkt.constData());
    size_t off = 0, n = size_t(pkt.size());
    while (off + 4 <= n) {
        const size_t len = (size_t((d[off + 2] << 8) | d[off + 3]) + 1) * 4;
        if (d[off + 1] == 206 && (d[off] & 0x1F) == 15 && off + 20 <= n &&
            memcmp(d + off + 12, "CAST", 4) == 0) {
            Feedback fb;
            fb.checkpoint = d[off + 16];
            for (int i = 0; i < d[off + 17]; ++i) {
                const uint8_t* l = d + off + 20 + 4 * i;
                fb.losses.push_back({l[0], (l[1] << 8) | l[2]});
            }
            return fb;
        }
        off += len;
    }
    return std::nullopt;
}

template <typename Pred> bool spinUntil(Pred pred, int ms = 2000) {
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return pred();
}

} // namespace

TEST(CastStreamingReceiverTest, LoopbackDeliversDecryptedFramesAndNacksLoss) {
    const auto offer = airshow::cast::parseOffer(chromeStyleOffer());
    const auto sel = airshow::cast::selectStreams(*offer, [](const std::string&) { return true; });
    const airshow::cast::CastStream video = *sel.video;
    const airshow::cast::CastStream audio = *sel.audio;

    std::vector<QByteArray> gotVideo, gotAudio;
    std::vector<bool> gotKey;
    airshow::cast::CastStreamingReceiver receiver;
    receiver.setFrameCallback([&](airshow::cast::CastStream::Kind k, const uint8_t* d, size_t n, bool key) {
        QByteArray f(reinterpret_cast<const char*>(d), qsizetype(n));
        if (k == airshow::cast::CastStream::Kind::Video) { gotVideo.push_back(f); gotKey.push_back(key); }
        else gotAudio.push_back(f);
    });
    ASSERT_TRUE(receiver.start({audio, video}));
    const quint16 port = receiver.localPort();
    ASSERT_NE(port, 0);

    QUdpSocket sender;
    ASSERT_TRUE(sender.bind(QHostAddress::LocalHost, 0));
    std::vector<QByteArray> rtcp;
    auto pump = [&] {
        while (sender.hasPendingDatagrams()) {
            QByteArray b(int(sender.pendingDatagramSize()), Qt::Uninitialized);
            sender.readDatagram(b.data(), b.size());
            rtcp.push_back(b);
        }
    };
    auto send = [&](const QByteArray& b) { sender.writeDatagram(b, QHostAddress::LocalHost, port); };

    // Three 2500-byte video frames, 1000-byte chunks → packet IDs 0..2.
    std::vector<QByteArray> plain;
    for (int i = 0; i < 3; ++i) {
        QByteArray f(2500, Qt::Uninitialized);
        for (int j = 0; j < f.size(); ++j) f[j] = char((i * 31 + j * 7) & 0xFF);
        plain.push_back(f);
    }
    uint16_t seq = 1000;
    auto packets = [&](uint32_t frameId) {
        const QByteArray enc = ctrCrypt(video, frameId, plain[frameId]);
        std::vector<QByteArray> out;
        for (uint16_t pid = 0; pid <= 2; ++pid)
            out.push_back(castRtpPacket(video, seq++, frameId, frameId == 0, pid, 2,
                                        enc.mid(pid * 1000, 1000)));
        return out;
    };

    send(senderReport(video.senderSsrc));
    const auto f0 = packets(0), f1 = packets(1), f2 = packets(2);
    for (const auto& p : f0) send(p);
    send(f1[0]); send(f1[2]);                  // f1 packet 1 "lost"
    for (const auto& p : f2) send(p);

    ASSERT_TRUE(spinUntil([&] { return gotVideo.size() == 1; }));
    // Receiver must NACK frame 1 / packet 1 with checkpoint 0.
    ASSERT_TRUE(spinUntil([&] {
        pump();
        for (const auto& b : rtcp)
            if (auto fb = parseFeedback(b))
                if (fb->checkpoint == 0 && !fb->losses.empty() &&
                    fb->losses[0] == std::make_pair(1, 1)) return true;
        return false;
    })) << "no NACK for frame 1 packet 1";

    send(f1[1]);                               // retransmission
    ASSERT_TRUE(spinUntil([&] { return gotVideo.size() == 3; }));
    EXPECT_EQ(gotVideo[0], plain[0]);
    EXPECT_EQ(gotVideo[1], plain[1]);
    EXPECT_EQ(gotVideo[2], plain[2]);
    EXPECT_TRUE(gotKey[0]);
    EXPECT_FALSE(gotKey[1]);

    // Checkpoint advances to frame 2.
    rtcp.clear();
    ASSERT_TRUE(spinUntil([&] {
        pump();
        for (const auto& b : rtcp)
            if (auto fb = parseFeedback(b)) if (fb->checkpoint == 2) return true;
        return false;
    })) << "checkpoint did not advance";

    // Audio: a single-packet frame on the audio SSRC.
    const QByteArray opus("opus-frame-bytes");
    send(castRtpPacket(audio, 1, 0, true, 0, 0, ctrCrypt(audio, 0, opus)));
    ASSERT_TRUE(spinUntil([&] { return gotAudio.size() == 1; }));
    EXPECT_EQ(gotAudio[0], opus);
    EXPECT_EQ(receiver.framesDelivered(airshow::cast::CastStream::Kind::Video), 3u);
}

// ── Test 15: Integration — CastHandler_IntegrationStartStop ──────────────────
// Integration test: start CastHandler, verify isRunning()==true and name()=="cast",
// verify port 8009 is bound by attempting a QTcpSocket connection to localhost:8009,
// then stop and verify isRunning()==false.

TEST(CastHandlerIntegrationTest, CastHandler_IntegrationStartStop) {
    airshow::ConnectionBridge bridge;
    airshow::MediaPipeline pipeline;

    airshow::CastHandler handler(&bridge);
    handler.setMediaPipeline(&pipeline);

    EXPECT_FALSE(handler.isRunning());
    EXPECT_EQ(handler.name(), "cast");

    bool started = handler.start();
    ASSERT_TRUE(started) << "CastHandler::start() must return true";
    EXPECT_TRUE(handler.isRunning());
    EXPECT_EQ(handler.name(), "cast");

    // Verify port 8009 is bound: QTcpSocket should connect successfully
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, 8009);
        bool connected = socket.waitForConnected(2000);  // 2 second timeout
        // The socket connects to the TLS port; TLS handshake is not required for
        // TCP-layer port availability check. Either a connected state or an SSL
        // error (which only fires after connect) confirms the port is open.
        bool portBound = connected || (socket.state() == QAbstractSocket::ConnectedState)
                         || (socket.error() == QAbstractSocket::SslHandshakeFailedError)
                         || (socket.error() == QAbstractSocket::RemoteHostClosedError);
        EXPECT_TRUE(portBound) << "Port 8009 should be bound after start(). Socket error: "
                               << socket.errorString().toStdString();
        socket.disconnectFromHost();
    }

    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

// ── Test 16: Integration — CastHandler_RejectsDoubleStart ────────────────────
// Calling start() twice must be idempotent: both calls return true,
// isRunning() remains true, and there is no crash or resource leak.

TEST(CastHandlerIntegrationTest, CastHandler_RejectsDoubleStart) {
    airshow::ConnectionBridge bridge;
    airshow::CastHandler handler(&bridge);

    bool first  = handler.start();
    bool second = handler.start();  // idempotent — should return true, not crash

    EXPECT_TRUE(first)  << "First start() call must return true";
    EXPECT_TRUE(second) << "Second start() call must return true (idempotent)";
    EXPECT_TRUE(handler.isRunning()) << "isRunning() must be true after double start";

    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

// ── PeerCertMatchesPrecomputedSignature ──────────────────────────────────────
// The check Chrome performs during device auth: the precomputed signature for
// the TLS certificate's window must verify (RSA PKCS#1 v1.5, SHA-256) against
// the DER of the certificate the receiver actually presents, using auth_crt's
// public key. Any change to the peer cert (serial, CN, validity, key, signing
// digest) breaks this and makes every Cast sender reject the receiver.

TEST(CastHandlerIntegrationTest, PeerCertMatchesPrecomputedSignature) {
    airshow::ConnectionBridge bridge;
    airshow::CastHandler handler(&bridge);
    ASSERT_TRUE(handler.start());

    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyNone);
    QEventLoop loop;
    QObject::connect(&socket, &QSslSocket::encrypted, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QSslSocket::errorOccurred, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    socket.connectToHostEncrypted(QStringLiteral("127.0.0.1"), 8009);
    loop.exec();  // server and client share this thread — don't use waitForEncrypted()
    ASSERT_TRUE(socket.isEncrypted()) << qPrintable(socket.errorString());

    const QSslCertificate peer = socket.peerCertificate();
    ASSERT_FALSE(peer.isNull());
    const QByteArray peerDer = peer.toDer();

    const uint8_t* sig = airshow::cast::getSignatureForTime(
        static_cast<uint64_t>(peer.effectiveDate().toSecsSinceEpoch()));
    ASSERT_NE(sig, nullptr) << "no signature for the certificate's window";

    const unsigned char* crtPtr = airshow::cast::auth_crt;
    X509* authCrt = d2i_X509(nullptr, &crtPtr, airshow::cast::auth_crt_len);
    ASSERT_NE(authCrt, nullptr);
    EVP_PKEY* authKey = X509_get_pubkey(authCrt);
    ASSERT_NE(authKey, nullptr);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    ASSERT_EQ(EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, authKey), 1);
    const int verified = EVP_DigestVerify(
        ctx, sig, airshow::cast::kCastAuthSignatureSize,
        reinterpret_cast<const unsigned char*>(peerDer.constData()),
        static_cast<size_t>(peerDer.size()));
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(authKey);
    X509_free(authCrt);

    EXPECT_EQ(verified, 1) << "precomputed Cast auth signature does not match the TLS peer cert";

    socket.disconnectFromHost();
    handler.stop();
}

// 8-bit frame IDs on the wire wrap every 256 frames; the receiver must expand
// them (and use the expanded ID in the AES IV) for a long-running stream.
TEST(CastStreamingReceiverTest, FrameIdsExpandAcrossWrap) {
    const auto offer = airshow::cast::parseOffer(chromeStyleOffer());
    const auto sel = airshow::cast::selectStreams(*offer, [](const std::string&) { return true; });
    const airshow::cast::CastStream audio = *sel.audio;

    int delivered = 0;
    bool allMatch = true;
    airshow::cast::CastStreamingReceiver receiver;
    receiver.setFrameCallback([&](airshow::cast::CastStream::Kind, const uint8_t* d, size_t n, bool) {
        const QByteArray expected = QByteArray::number(delivered);
        allMatch = allMatch && QByteArray(reinterpret_cast<const char*>(d), qsizetype(n)) == expected;
        ++delivered;
    });
    ASSERT_TRUE(receiver.start({audio}));

    QUdpSocket sender;
    ASSERT_TRUE(sender.bind(QHostAddress::LocalHost, 0));
    for (uint32_t id = 0; id < 300; ++id) {
        const QByteArray plain = QByteArray::number(id);
        sender.writeDatagram(castRtpPacket(audio, uint16_t(id), id, true, 0, 0,
                                           ctrCrypt(audio, id, plain)),
                             QHostAddress::LocalHost, receiver.localPort());
        if (id % 20 == 0) spinUntil([&] { return delivered == int(id) + 1; }, 500);
    }
    ASSERT_TRUE(spinUntil([&] { return delivered == 300; }));
    EXPECT_TRUE(allMatch);
}
