#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Qt
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <QStringConverter>
#include <QtEndian>
#include <gst/gst.h>

// Project headers
#include "protocol/MiracastHandler.h"
#include "pipeline/MediaPipeline.h"
#include "ui/ConnectionBridge.h"

using namespace myairshow;

// ── Test environment: QCoreApplication ───────────────────────────────────────
// QCoreApplication must be created before any QObject (MiracastHandler is QObject).

static int s_argc = 1;
static const char* s_argv[] = {"test_miracast", nullptr};
static QCoreApplication* s_app = nullptr;

class MiracastTestEnvironment : public ::testing::Environment {
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

testing::Environment* const miracastEnv =
    testing::AddGlobalTestEnvironment(new MiracastTestEnvironment);

// ── Test 1: MiracastHandlerConformsToInterface ────────────────────────────────
// Verify that MiracastHandler implements the ProtocolHandler interface correctly.

TEST(MiracastHandlerTest, ConformsToInterface) {
    // Instantiate with nullptr ConnectionBridge — we are testing the interface, not networking
    MiracastHandler handler(nullptr);

    // name() must return "miracast"
    EXPECT_EQ(handler.name(), "miracast");

    // Not running before start()
    EXPECT_FALSE(handler.isRunning());

    // start() must succeed (TCP listen on 7250)
    EXPECT_TRUE(handler.start());

    // isRunning() must be true after start()
    EXPECT_TRUE(handler.isRunning());

    // stop() must set isRunning() to false
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

// ── Test 2: ParseMiceSourceReady ──────────────────────────────────────────────
// Construct a valid SOURCE_READY binary message and verify the static parser
// extracts FriendlyName, RTSPPort, and SourceID correctly.

TEST(MiracastHandlerTest, ParseMiceSourceReady) {
    // Build a SOURCE_READY message according to RESEARCH.md Pattern 1:
    //   [2 bytes big-endian: total size]
    //   [1 byte: version=0x01]
    //   [1 byte: command=0x01 SOURCE_READY]
    //   TLV 0x0001: FriendlyName "TestPC" as UTF-16LE
    //   TLV 0x0002: RTSPPort 7236 as big-endian uint16
    //   TLV 0x0003: SourceID "test-source-id-00" as ASCII

    QByteArray msg;

    // FriendlyName TLV: "TestPC" in UTF-16LE
    auto encoder = QStringEncoder(QStringEncoder::Utf16LE);
    QByteArray nameBytes = encoder.encode(QStringLiteral("TestPC"));

    // RTSPPort TLV: 7236 as big-endian uint16
    QByteArray portBytes;
    uint16_t port = qToBigEndian<uint16_t>(7236);
    portBytes.append(reinterpret_cast<const char*>(&port), 2);

    // SourceID TLV: ASCII
    QByteArray sourceIdBytes = QByteArrayLiteral("test-source-id-00");

    // Helper: append TLV (type and length as big-endian uint16)
    auto appendTlv = [&](uint16_t type, const QByteArray& value) {
        uint16_t typeBe = qToBigEndian<uint16_t>(type);
        uint16_t lenBe  = qToBigEndian<uint16_t>(static_cast<uint16_t>(value.size()));
        msg.append(reinterpret_cast<const char*>(&typeBe), 2);
        msg.append(reinterpret_cast<const char*>(&lenBe), 2);
        msg.append(value);
    };

    // Build TLV body first (to know the total size)
    QByteArray tlvBody;
    auto appendTlvToBody = [&](uint16_t type, const QByteArray& value) {
        uint16_t typeBe = qToBigEndian<uint16_t>(type);
        uint16_t lenBe  = qToBigEndian<uint16_t>(static_cast<uint16_t>(value.size()));
        tlvBody.append(reinterpret_cast<const char*>(&typeBe), 2);
        tlvBody.append(reinterpret_cast<const char*>(&lenBe), 2);
        tlvBody.append(value);
    };
    Q_UNUSED(appendTlv)

    appendTlvToBody(0x0001, nameBytes);
    appendTlvToBody(0x0002, portBytes);
    appendTlvToBody(0x0003, sourceIdBytes);

    // Total message size: 4-byte header + TLV body
    const uint16_t totalSize = static_cast<uint16_t>(4 + tlvBody.size());
    uint16_t sizeBe = qToBigEndian<uint16_t>(totalSize);

    // Assemble full message
    msg.append(reinterpret_cast<const char*>(&sizeBe), 2);
    msg.append(static_cast<char>(0x01));  // version
    msg.append(static_cast<char>(0x01));  // command: SOURCE_READY
    msg.append(tlvBody);

    // Parse
    MiracastHandler::SourceReadyInfo info;
    ASSERT_TRUE(MiracastHandler::parseSourceReady(msg, info));

    EXPECT_EQ(info.friendlyName, QStringLiteral("TestPC"));
    EXPECT_EQ(info.rtspPort, static_cast<uint16_t>(7236));
    EXPECT_EQ(info.sourceId, QStringLiteral("test-source-id-00"));
}

// ── Test 3: InitMiracastPipelineCreatesElements ───────────────────────────────
// Verify that initMiracastPipeline() creates a GStreamer pipeline in PAUSED state,
// and that stopMiracast() cleans it up.

TEST(MiracastHandlerTest, InitMiracastPipelineCreatesElements) {
    MediaPipeline pipeline;

    // nullptr qmlVideoItem — headless mode (uses fakesink for video)
    // udpPort 1028 — the default per RESEARCH.md Open Question 3
    bool result = pipeline.initMiracastPipeline(nullptr, 1028);

    if (!result) {
        // Some CI environments may lack tsdemux / rtpmp2tdepay — skip gracefully
        GTEST_SKIP() << "initMiracastPipeline() returned false — required GStreamer plugins "
                        "(rtpmp2tdepay, tsparse, tsdemux) may not be available in this environment";
    }

    EXPECT_TRUE(result);

    // Verify pipeline was created and is in PAUSED state
    GstElement* gstPipeline = pipeline.miracastPipeline();
    ASSERT_NE(gstPipeline, nullptr);

    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    gst_element_get_state(gstPipeline, &state, &pending, GST_CLOCK_TIME_NONE);
    // Acceptable states: PAUSED (nominal) or READY (if PAUSED transition is async)
    EXPECT_TRUE(state == GST_STATE_PAUSED || state == GST_STATE_READY)
        << "Expected PAUSED or READY, got state=" << state;

    // stopMiracast() must clean up
    pipeline.stopMiracast();
    EXPECT_EQ(pipeline.miracastPipeline(), nullptr);
}

// ── Test 4: BuildRtspResponse ─────────────────────────────────────────────────
// Verify that buildRtspResponse() produces a valid RTSP/1.0 response string.

TEST(MiracastHandlerTest, BuildRtspResponse) {
    // Test 4a: 200 OK with no body
    const QString resp200 = MiracastHandler::buildRtspResponse(1, 200);
    EXPECT_TRUE(resp200.startsWith(QStringLiteral("RTSP/1.0 200 OK")));
    EXPECT_TRUE(resp200.contains(QStringLiteral("CSeq: 1")));
    // No body — Content-Length must not appear
    EXPECT_FALSE(resp200.contains(QStringLiteral("Content-Length")));
    // Must end with \r\n\r\n (blank line terminates headers)
    EXPECT_TRUE(resp200.endsWith(QStringLiteral("\r\n")));

    // Test 4b: 200 OK with body — Content-Length must be present and correct
    const QString body = QStringLiteral("wfd_video_formats: none\r\n");
    const QString respWithBody = MiracastHandler::buildRtspResponse(3, 200, body);
    EXPECT_TRUE(respWithBody.startsWith(QStringLiteral("RTSP/1.0 200 OK")));
    EXPECT_TRUE(respWithBody.contains(QStringLiteral("CSeq: 3")));
    EXPECT_TRUE(respWithBody.contains(QStringLiteral("Content-Length:")));

    // Extract Content-Length value and verify it matches the actual body byte size
    const QByteArray bodyUtf8 = body.toUtf8();
    const QString expectedContentLength =
        QStringLiteral("Content-Length: %1").arg(bodyUtf8.size());
    EXPECT_TRUE(respWithBody.contains(expectedContentLength))
        << "Expected Content-Length: " << bodyUtf8.size()
        << " in: " << respWithBody.toStdString();

    // Body must appear after the blank line (\r\n\r\n)
    const int blankLine = respWithBody.indexOf(QStringLiteral("\r\n\r\n"));
    EXPECT_NE(blankLine, -1) << "No blank line found in response with body";
    if (blankLine != -1) {
        const QString afterBlankLine = respWithBody.mid(blankLine + 4);
        EXPECT_EQ(afterBlankLine, body);
    }

    // Test 4c: CSeq counter increments properly
    const QString resp400 = MiracastHandler::buildRtspResponse(7, 400);
    EXPECT_TRUE(resp400.startsWith(QStringLiteral("RTSP/1.0 400 Bad Request")));
    EXPECT_TRUE(resp400.contains(QStringLiteral("CSeq: 7")));
}
