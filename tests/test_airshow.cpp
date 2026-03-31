#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Qt
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <gst/gst.h>

// Project headers
#include "protocol/AirShowHandler.h"
#include "pipeline/MediaPipeline.h"
#include "ui/ConnectionBridge.h"

using namespace airshow;

// ── Test environment: QCoreApplication ───────────────────────────────────────
// QCoreApplication must be created before any QObject (AirShowHandler is QObject).

static int s_argc = 1;
static const char* s_argv[] = {"test_airshow", nullptr};
static QCoreApplication* s_app = nullptr;

class AirShowTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            s_app = new QCoreApplication(s_argc, const_cast<char**>(s_argv));
        }
        // GStreamer must be initialized for MediaPipeline pipeline tests
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
    }
    void TearDown() override {
        // Process remaining queued events before Qt objects are destroyed
        QCoreApplication::processEvents();
    }
};

testing::Environment* const airshowEnv =
    testing::AddGlobalTestEnvironment(new AirShowTestEnvironment);

// ── Test 1: AirShowHandlerConformsToInterface ────────────────────────────────
// Verify that AirShowHandler implements the ProtocolHandler interface correctly.

TEST(AirShowHandlerTest, ConformsToInterface) {
    // Instantiate with nullptr ConnectionBridge — testing the interface, not networking
    AirShowHandler handler(nullptr);

    // name() must return "airshow"
    EXPECT_EQ(handler.name(), "airshow");

    // Not running before start()
    EXPECT_FALSE(handler.isRunning());

    // start() must succeed (TCP listen on 7400)
    EXPECT_TRUE(handler.start());

    // isRunning() must be true after start()
    EXPECT_TRUE(handler.isRunning());

    // stop() must set isRunning() to false
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

// ── Test 2: ParseFrameHeader ──────────────────────────────────────────────────
// Construct a valid 16-byte frame header and verify the static parser
// extracts type, flags, length, and pts correctly.

TEST(AirShowHandlerTest, ParseFrameHeader) {
    // Build a 16-byte header:
    //   type   = 0x01 (VIDEO_NAL)
    //   flags  = 0x01 (keyframe)
    //   length = 128 (big-endian uint32 at bytes 2-5)
    //   pts    = 33333333 ns (big-endian int64 at bytes 6-13)
    //   reserved = 0x0000 (bytes 14-15)

    const uint32_t expectedLength = 128;
    const int64_t  expectedPts    = 33333333LL;

    QByteArray header(AirShowHandler::kFrameHeaderSize, '\0');
    header[0] = static_cast<char>(AirShowHandler::kTypeVideoNal);   // type
    header[1] = static_cast<char>(AirShowHandler::kFlagKeyframe);   // flags

    // length: big-endian uint32 at offset 2
    quint32 lenBe = qToBigEndian<quint32>(expectedLength);
    memcpy(header.data() + 2, &lenBe, 4);

    // pts: big-endian int64 at offset 6
    qint64 ptsBe = qToBigEndian<qint64>(static_cast<qint64>(expectedPts));
    memcpy(header.data() + 6, &ptsBe, 8);

    // reserved bytes 14-15 are already zero

    AirShowHandler::FrameHeader parsed;
    ASSERT_TRUE(AirShowHandler::parseFrameHeader(header, parsed));

    EXPECT_EQ(parsed.type,   static_cast<uint8_t>(AirShowHandler::kTypeVideoNal));
    EXPECT_EQ(parsed.flags,  static_cast<uint8_t>(AirShowHandler::kFlagKeyframe));
    EXPECT_EQ(parsed.length, expectedLength);
    EXPECT_EQ(parsed.pts,    static_cast<int64_t>(expectedPts));

    // Verify that a buffer shorter than kFrameHeaderSize returns false
    QByteArray shortData(AirShowHandler::kFrameHeaderSize - 1, '\0');
    AirShowHandler::FrameHeader dummy;
    EXPECT_FALSE(AirShowHandler::parseFrameHeader(shortData, dummy));
}

// ── Test 3: HandshakeJsonRoundTrip ───────────────────────────────────────────
// Start an AirShowHandler, connect a QTcpSocket, send a HELLO JSON, and verify
// that the response is a valid HELLO_ACK JSON with the expected negotiated fields.

TEST(AirShowHandlerTest, HandshakeJsonRoundTrip) {
    AirShowHandler handler(nullptr);
    ASSERT_TRUE(handler.start());

    QTcpSocket client;
    client.connectToHost(QStringLiteral("127.0.0.1"), AirShowHandler::kAirShowPort);

    // Wait up to 2 seconds for connection
    bool connected = client.waitForConnected(2000);
    if (!connected) {
        handler.stop();
        GTEST_SKIP() << "Could not connect to AirShowHandler on localhost:"
                     << AirShowHandler::kAirShowPort
                     << " — " << client.errorString().toStdString();
    }

    // Build HELLO JSON
    QJsonObject hello;
    hello[QStringLiteral("type")]          = QStringLiteral("HELLO");
    hello[QStringLiteral("version")]       = 1;
    hello[QStringLiteral("deviceName")]    = QStringLiteral("TestDevice");
    hello[QStringLiteral("codec")]         = QStringLiteral("h264");
    hello[QStringLiteral("maxResolution")] = QStringLiteral("1920x1080");
    hello[QStringLiteral("targetBitrate")] = 4000000;
    hello[QStringLiteral("fps")]           = 30;

    QByteArray helloBytes = QJsonDocument(hello).toJson(QJsonDocument::Compact);
    helloBytes.append('\n');
    client.write(helloBytes);
    client.flush();

    // Process events until we have a response line (up to 3 seconds)
    QByteArray responseBuffer;
    for (int i = 0; i < 300 && !responseBuffer.contains('\n'); ++i) {
        QCoreApplication::processEvents();
        if (client.waitForReadyRead(10)) {
            responseBuffer += client.readAll();
        }
    }

    ASSERT_TRUE(responseBuffer.contains('\n'))
        << "No newline-terminated response received from AirShowHandler";

    // Extract first line
    QByteArray responseLine = responseBuffer.left(responseBuffer.indexOf('\n'));

    // Parse as JSON
    QJsonParseError parseError;
    QJsonDocument responseDoc = QJsonDocument::fromJson(responseLine, &parseError);
    ASSERT_EQ(parseError.error, QJsonParseError::NoError)
        << "Failed to parse HELLO_ACK JSON: " << parseError.errorString().toStdString();
    ASSERT_TRUE(responseDoc.isObject()) << "Response is not a JSON object";

    QJsonObject ack = responseDoc.object();

    // Verify HELLO_ACK fields
    EXPECT_EQ(ack[QStringLiteral("type")].toString(), QStringLiteral("HELLO_ACK"));
    EXPECT_EQ(ack[QStringLiteral("version")].toInt(), 1);
    EXPECT_EQ(ack[QStringLiteral("codec")].toString(), QStringLiteral("h264"));
    EXPECT_EQ(ack[QStringLiteral("acceptedResolution")].toString(),
              QStringLiteral("1920x1080"));
    EXPECT_EQ(ack[QStringLiteral("acceptedBitrate")].toInt(), 4000000);
    EXPECT_EQ(ack[QStringLiteral("acceptedFps")].toInt(), 30);

    client.disconnectFromHost();
    handler.stop();
}
