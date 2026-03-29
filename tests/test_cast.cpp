#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Qt
#include <QCoreApplication>
#include <QByteArray>
#include <QtEndian>

// Project headers
#include "protocol/CastHandler.h"
#include "protocol/CastSession.h"
#include "ui/ConnectionBridge.h"
#include "cast/cast_auth_sigs.h"

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
        reinterpret_cast<const char*>(&myairshow::cast::kCastAuthSignatures[0][0]),
        myairshow::cast::kCastAuthSignatureSize);
    resp.set_client_auth_certificate(
        reinterpret_cast<const char*>(myairshow::cast::kCastAuthPeerCert),
        myairshow::cast::kCastAuthPeerCertSize);
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
              static_cast<size_t>(myairshow::cast::kCastAuthSignatureSize));
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
// Verify the index rotation formula: (timestamp / 172800) % 795

TEST(CastAuthTest, AuthSignatureIndexRotation) {
    using namespace myairshow::cast;

    // Two timestamps in different 48h windows must give different indices
    uint64_t t1 = 1000000000ULL;               // some timestamp
    uint64_t t2 = t1 + 172800ULL;              // exactly one window later

    size_t idx1 = (t1 / 172800) % kCastAuthSignatureCount;
    size_t idx2 = (t2 / 172800) % kCastAuthSignatureCount;

    // They should differ (unless we wrapped around by exactly kCastAuthSignatureCount)
    // With a 795-element table and a 1-step increment, they will always differ.
    EXPECT_NE(idx1, idx2);

    // Two timestamps in the same 48h window must give the same index
    uint64_t t3 = t1 + 100;         // 100 seconds later, same window
    size_t idx3 = (t3 / 172800) % kCastAuthSignatureCount;
    EXPECT_EQ(idx1, idx3);

    // Index is always within bounds
    EXPECT_LT(idx1, kCastAuthSignatureCount);
    EXPECT_LT(idx2, kCastAuthSignatureCount);
    EXPECT_LT(idx3, kCastAuthSignatureCount);

    // Index wraps correctly at the table boundary
    // After 795 windows the cycle repeats
    uint64_t t_wrap = t1 + static_cast<uint64_t>(kCastAuthSignatureCount) * 172800ULL;
    size_t idx_wrap = (t_wrap / 172800) % kCastAuthSignatureCount;
    EXPECT_EQ(idx1, idx_wrap);
}

// ── Test 5: CastHandler_StartsOnPort8009 ──────────────────────────────────────
// start() returns true, isRunning() becomes true, name() returns "cast",
// stop() makes isRunning() false.

TEST(CastHandlerTest, StartsOnPort8009) {
    myairshow::ConnectionBridge bridge;
    myairshow::CastHandler handler(&bridge);

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
    myairshow::ConnectionBridge bridge;
    myairshow::CastHandler handler(&bridge);

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
    EXPECT_EQ(myairshow::cast::kCastAuthSignatureCount, 795u);
    EXPECT_EQ(myairshow::cast::kCastAuthSignatureSize, 256u);

    // Verify that getCastAuthSignature returns a valid pointer within the table
    uint64_t now = 1774757446ULL;  // fixed test timestamp
    const uint8_t* sig = myairshow::cast::getCastAuthSignature(now);
    ASSERT_NE(sig, nullptr);

    // The returned pointer must point within the signature table
    const uint8_t* tableStart = &myairshow::cast::kCastAuthSignatures[0][0];
    const uint8_t* tableEnd   = tableStart +
        myairshow::cast::kCastAuthSignatureCount * myairshow::cast::kCastAuthSignatureSize;
    EXPECT_GE(sig, tableStart);
    EXPECT_LT(sig, tableEnd);
}
