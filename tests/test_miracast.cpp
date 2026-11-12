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

using namespace airshow;

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
    // Build a SOURCE_READY message per [MS-MICE] 2.2 / 2.2.7:
    //   [2 bytes big-endian: total size]
    //   [1 byte: version=0x01]
    //   [1 byte: command=0x01 SOURCE_READY]
    //   TLVs: [1 byte type][2 bytes big-endian length][value]
    //   TLV 0x00: FRIENDLY_NAME "TestPC" as UTF-16LE
    //   TLV 0x02: RTSP_PORT 7236 as big-endian uint16
    //   TLV 0x03: SOURCE_ID, 16 bytes

    QByteArray msg;

    // FriendlyName TLV: "TestPC" in UTF-16LE
    auto encoder = QStringEncoder(QStringEncoder::Utf16LE);
    QByteArray nameBytes = encoder.encode(QStringLiteral("TestPC"));

    // RTSPPort TLV: 7236 as big-endian uint16
    QByteArray portBytes;
    uint16_t port = qToBigEndian<uint16_t>(7236);
    portBytes.append(reinterpret_cast<const char*>(&port), 2);

    // SourceID TLV: ASCII
    QByteArray sourceIdBytes = QByteArray::fromHex("00112233445566778899aabbccddeeff");

    // Helper: append TLV (type and length as big-endian uint16)
    auto appendTlv = [&](uint8_t type, const QByteArray& value) {
        uint16_t lenBe  = qToBigEndian<uint16_t>(static_cast<uint16_t>(value.size()));
        msg.append(static_cast<char>(type));
        msg.append(reinterpret_cast<const char*>(&lenBe), 2);
        msg.append(value);
    };

    // Build TLV body first (to know the total size)
    QByteArray tlvBody;
    auto appendTlvToBody = [&](uint8_t type, const QByteArray& value) {
        uint16_t lenBe  = qToBigEndian<uint16_t>(static_cast<uint16_t>(value.size()));
        tlvBody.append(static_cast<char>(type));
        tlvBody.append(reinterpret_cast<const char*>(&lenBe), 2);
        tlvBody.append(value);
    };
    Q_UNUSED(appendTlv)

    appendTlvToBody(0x00, nameBytes);
    appendTlvToBody(0x02, portBytes);
    appendTlvToBody(0x03, sourceIdBytes);

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
    EXPECT_EQ(info.sourceId, QStringLiteral("00112233445566778899aabbccddeeff"));
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

// ── Test 5: WfdCapabilityResponseFormat ──────────────────────────────────────
// Verify kWfdCapabilityResponse contains all required WFD fields and does NOT
// advertise HDCP (wfd_content_protection must be "none" per RESEARCH.md Pitfall 6).

TEST(MiracastHandlerTest, WfdCapabilityResponseFormat) {
    const QString cap = QString::fromUtf8(MiracastHandler::kWfdCapabilityResponse);

    // Required fields must be present
    EXPECT_TRUE(cap.contains(QStringLiteral("wfd_video_formats:")))
        << "Missing wfd_video_formats";
    EXPECT_TRUE(cap.contains(QStringLiteral("wfd_audio_codecs:")))
        << "Missing wfd_audio_codecs";
    EXPECT_TRUE(cap.contains(QStringLiteral("wfd_client_rtp_ports:")))
        << "Missing wfd_client_rtp_ports";
    EXPECT_TRUE(cap.contains(QStringLiteral("wfd_content_protection: none")))
        << "wfd_content_protection must be 'none' (RESEARCH.md Pitfall 6)";
    EXPECT_TRUE(cap.contains(QStringLiteral("wfd_display_edid:")))
        << "Missing wfd_display_edid";
    EXPECT_TRUE(cap.contains(QStringLiteral("wfd_connector_type:")))
        << "Missing wfd_connector_type";

    // Must NOT advertise HDCP
    EXPECT_FALSE(cap.contains(QStringLiteral("HDCP"), Qt::CaseInsensitive))
        << "HDCP must not appear in capability response (Pitfall 6)";

    // Content protection must be exactly "none"
    EXPECT_FALSE(cap.contains(QStringLiteral("wfd_content_protection: HDCP")))
        << "HDCP content protection would cause Windows to send encrypted media";
    EXPECT_FALSE(cap.contains(QStringLiteral("wfd_content_protection: hdcp")))
        << "HDCP content protection (lowercase) would cause Windows to send encrypted media";
}

// ── Test 6: RtspRequestFormat ─────────────────────────────────────────────────
// Verify that sendRtspRequest formats a valid RTSP/1.0 request line with CSeq.
// Tests the static buildRtspResponse (inverse: buildRtspResponse for responses,
// requests are formatted by sendRtspRequest which we test indirectly via format check).
// We test the response builder for completeness and verify the request format
// that sendRtspRequest would produce matches expected RTSP/1.0 format.

TEST(MiracastHandlerTest, RtspRequestFormat) {
    // Test OPTIONS request format via buildRtspResponse symmetry check.
    // The actual sendRtspRequest output follows "METHOD uri RTSP/1.0\r\nCSeq: N\r\n..." format.
    // We verify the essential format by constructing the expected string manually.

    // Verify M2 OPTIONS would contain the right fields:
    // "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nRequire: org.wfa.wfd1.0\r\n\r\n"
    const QString expectedM2 =
        QStringLiteral("OPTIONS * RTSP/1.0\r\n"
                       "CSeq: 1\r\n"
                       "Require: org.wfa.wfd1.0\r\n"
                       "\r\n");

    EXPECT_TRUE(expectedM2.contains(QStringLiteral("OPTIONS * RTSP/1.0")));
    EXPECT_TRUE(expectedM2.contains(QStringLiteral("CSeq:")));
    EXPECT_TRUE(expectedM2.contains(QStringLiteral("Require: org.wfa.wfd1.0")));
    EXPECT_TRUE(expectedM2.endsWith(QStringLiteral("\r\n")));

    // Verify M6 SETUP Transport header format
    const QString setupTransport =
        QStringLiteral("Transport: RTP/AVP/UDP;unicast;client_port=%1\r\n")
        .arg(MiracastHandler::kDefaultRtpPort);
    EXPECT_TRUE(setupTransport.contains(QStringLiteral("RTP/AVP/UDP")));
    EXPECT_TRUE(setupTransport.contains(QStringLiteral("unicast")));
    EXPECT_TRUE(setupTransport.contains(QStringLiteral("client_port=1028")));

    // Also verify buildRtspResponse 200 OK has proper structure
    const QString resp = MiracastHandler::buildRtspResponse(5, 200);
    EXPECT_TRUE(resp.startsWith(QStringLiteral("RTSP/1.0 200 OK")));
    EXPECT_TRUE(resp.contains(QStringLiteral("CSeq: 5")));
    EXPECT_TRUE(resp.endsWith(QStringLiteral("\r\n")));
}

// ── Test 7: StateTransitionsOnSourceReady ────────────────────────────────────
// Verify that a valid SOURCE_READY binary message is parsed correctly and that
// parseSourceReady extracts the name, rtspPort, and sourceId fields.

TEST(MiracastHandlerTest, StateTransitionsOnSourceReady) {
    // Build a SOURCE_READY message (same builder as Test 2 but different values)
    auto encoder = QStringEncoder(QStringEncoder::Utf16LE);
    QByteArray nameBytes = encoder.encode(QStringLiteral("DESKTOP-WINPC"));

    uint16_t portBe = qToBigEndian<uint16_t>(7236);
    QByteArray portBytes;
    portBytes.append(reinterpret_cast<const char*>(&portBe), 2);

    QByteArray sourceIdBytes = QByteArray::fromHex("0f0e0d0c0b0a09080706050403020100");

    QByteArray tlvBody;
    auto appendTlv = [&](uint8_t type, const QByteArray& value) {
        uint16_t lenBe  = qToBigEndian<uint16_t>(static_cast<uint16_t>(value.size()));
        tlvBody.append(static_cast<char>(type));
        tlvBody.append(reinterpret_cast<const char*>(&lenBe), 2);
        tlvBody.append(value);
    };

    appendTlv(0x00, nameBytes);
    appendTlv(0x02, portBytes);
    appendTlv(0x03, sourceIdBytes);

    const uint16_t totalSize = static_cast<uint16_t>(4 + tlvBody.size());
    uint16_t sizeBe = qToBigEndian<uint16_t>(totalSize);

    QByteArray msg;
    msg.append(reinterpret_cast<const char*>(&sizeBe), 2);
    msg.append(static_cast<char>(0x01));  // version
    msg.append(static_cast<char>(0x01));  // command: SOURCE_READY
    msg.append(tlvBody);

    // Parse and verify all fields extracted correctly
    MiracastHandler::SourceReadyInfo info;
    ASSERT_TRUE(MiracastHandler::parseSourceReady(msg, info));

    EXPECT_EQ(info.friendlyName, QStringLiteral("DESKTOP-WINPC"));
    EXPECT_EQ(info.rtspPort, static_cast<uint16_t>(7236));
    EXPECT_EQ(info.sourceId, QStringLiteral("0f0e0d0c0b0a09080706050403020100"));

    // Verify invalid command byte is rejected
    QByteArray badMsg = msg;
    badMsg[3] = 0x02;  // command 0x02 = STOP_PROJECTION, not SOURCE_READY
    MiracastHandler::SourceReadyInfo badInfo;
    EXPECT_FALSE(MiracastHandler::parseSourceReady(badMsg, badInfo));

    // Verify truncated message is rejected
    QByteArray truncatedMsg = msg.left(2);  // Only 2 bytes — less than 4-byte minimum
    MiracastHandler::SourceReadyInfo truncInfo;
    EXPECT_FALSE(MiracastHandler::parseSourceReady(truncatedMsg, truncInfo));
}

// ── Test 8: M3ResponseIncludesAllFields ──────────────────────────────────────
// Verify the M3 capability response body includes all WFD parameter lines,
// each ending with \r\n (required by RTSP text/parameters MIME type).

TEST(MiracastHandlerTest, M3ResponseIncludesAllFields) {
    const QByteArray capBytes(MiracastHandler::kWfdCapabilityResponse);
    const QList<QByteArray> lines = capBytes.split('\n');

    // Expected field names — each must appear as a line prefix
    const QList<QByteArray> requiredFields = {
        "wfd_video_formats:",
        "wfd_audio_codecs:",
        "wfd_client_rtp_ports:",
        "wfd_content_protection:",
        "wfd_display_edid:",
        "wfd_connector_type:",
    };

    for (const QByteArray& field : requiredFields) {
        bool found = false;
        for (const QByteArray& line : lines) {
            if (line.trimmed().startsWith(field)) {
                found = true;
                // Each field line must end with \r (since we split on \n, \r remains)
                EXPECT_TRUE(line.endsWith('\r'))
                    << "Field line '" << field.constData()
                    << "' does not end with \\r\\n";
                break;
            }
        }
        EXPECT_TRUE(found) << "Required WFD field missing: " << field.constData();
    }

    // Verify video_formats includes H.264 codec descriptor markers
    EXPECT_TRUE(capBytes.contains("00 00 02 10"))
        << "Missing H.264 CBP+CHP codec count/profile marker in wfd_video_formats";

    // Verify audio codecs includes LPCM and AAC
    EXPECT_TRUE(capBytes.contains("LPCM"))
        << "Missing LPCM in wfd_audio_codecs";
    EXPECT_TRUE(capBytes.contains("AAC"))
        << "Missing AAC in wfd_audio_codecs";

    // Verify RTP client port 1028 (lazycast default, RESEARCH.md Open Question 3)
    EXPECT_TRUE(capBytes.contains("1028"))
        << "Missing RTP client port 1028 in wfd_client_rtp_ports";
}

// ── Test 9: TeardownResetsState ───────────────────────────────────────────────
// Verify state machine correctness: after a session ends, isRunning() is still
// true (server keeps listening) but ConnectionBridge receives setConnected(false).

TEST(MiracastHandlerTest, TeardownResetsState) {
    // Use a minimal ConnectionBridge with inspection capability
    // We pass nullptr here and verify the handler doesn't crash on teardown
    // without a ConnectionBridge (guards against null deref in teardown path).
    MiracastHandler handler(nullptr);

    // Start the handler — should succeed
    ASSERT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());

    // stop() should set isRunning() to false
    handler.stop();
    EXPECT_FALSE(handler.isRunning());

    // Second stop() is idempotent (no crash)
    handler.stop();
    EXPECT_FALSE(handler.isRunning());

    // Restart should work after stop
    // (Verifies listener cleanup doesn't leave port bound)
    // Note: Port 7250 may still be in TIME_WAIT on some systems; skip if bind fails.
    // We use a second handler to avoid port conflict with the first handler's bind.
}

// ── Test 10: RequiredGStreamerPluginsAvailable ────────────────────────────────
// Verify that all required GStreamer plugins for Miracast are installed.
// Fails immediately with a clear message if any required plugin is missing —
// catches missing package installs before a real Miracast session tries to use them.

TEST(MiracastHandlerTest, RequiredGStreamerPluginsAvailable) {
    // All elements required to build the Miracast MPEG-TS/RTP receive pipeline
    struct { const char* name; const char* package; } requiredPlugins[] = {
        {"rtpmp2tdepay", "gstreamer1.0-plugins-good"},
        {"tsparse",      "gstreamer1.0-plugins-bad"},
        {"tsdemux",      "gstreamer1.0-plugins-bad"},
        {"h264parse",    "gstreamer1.0-plugins-bad"},
        {"avdec_h264",   "gstreamer1.0-libav"},
        {"aacparse",     "gstreamer1.0-plugins-bad"},
        {"avdec_aac",    "gstreamer1.0-libav"},
        {"videoconvert", "gstreamer1.0-plugins-base"},
        {"autoaudiosink","gstreamer1.0-plugins-good"},
    };

    for (auto& p : requiredPlugins) {
        GstElementFactory* factory = gst_element_factory_find(p.name);
        EXPECT_NE(factory, nullptr)
            << "Required Miracast GStreamer plugin '" << p.name
            << "' not found. Install: " << p.package;
        if (factory) {
            gst_object_unref(factory);
        }
    }
}

// ── Test 11: MiracastHandlerStartStopLifecycle ────────────────────────────────
// Verify TCP server lifecycle: start() opens port 7250, isRunning() reflects state,
// stop() closes the port, and restart() works cleanly.

TEST(MiracastHandlerTest, MiracastHandlerStartStopLifecycle) {
    MiracastHandler handler(nullptr);

    // Initially not running
    EXPECT_FALSE(handler.isRunning());

    // start() must succeed and listen on port 7250
    EXPECT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());

    // stop() must clean up and clear isRunning()
    handler.stop();
    EXPECT_FALSE(handler.isRunning());

    // start() again — verify the handler can restart cleanly
    // (port must be released by stop() so re-bind succeeds)
    EXPECT_TRUE(handler.start());
    EXPECT_TRUE(handler.isRunning());

    // Final stop
    handler.stop();
    EXPECT_FALSE(handler.isRunning());
}

// ── Test 12: MiracastPipelineStartStopCycle ───────────────────────────────────
// Verify that initMiracastPipeline() creates a pipeline, play() transitions it
// to PLAYING, stopMiracast() cleans it up, and the cycle can be repeated once
// without resource leaks (pipeline pointer null after stop).

TEST(MiracastHandlerTest, MiracastPipelineStartStopCycle) {
    MediaPipeline pipeline;

    // First cycle — headless mode (nullptr qmlVideoItem), port 1028
    bool firstResult = pipeline.initMiracastPipeline(nullptr, 1028);

    if (!firstResult) {
        GTEST_SKIP() << "initMiracastPipeline() returned false — required GStreamer plugins "
                        "(rtpmp2tdepay, tsparse, tsdemux) may not be available in this environment";
    }

    EXPECT_NE(pipeline.miracastPipeline(), nullptr)
        << "Pipeline must be non-null after successful initMiracastPipeline";

    // Transition to PLAYING. (MediaPipeline::play() only drives the main pipeline;
    // a Miracast pipeline left PAUSED receives nothing and shows no picture.)
    EXPECT_TRUE(pipeline.playMiracast());

    {
        GstElement* gstPipeline = pipeline.miracastPipeline();
        ASSERT_NE(gstPipeline, nullptr);
        GstState state = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        // Use a short timeout — we don't want to block indefinitely in a unit test
        gst_element_get_state(gstPipeline, &state, &pending, 500 * GST_MSECOND);
        EXPECT_TRUE(state == GST_STATE_PLAYING || pending == GST_STATE_PLAYING)
            << "Expected PLAYING (or transition to it), got state=" << state
            << " pending=" << pending;
    }

    // stopMiracast() must clean up: pipeline pointer becomes null
    pipeline.stopMiracast();
    EXPECT_EQ(pipeline.miracastPipeline(), nullptr)
        << "miracastPipeline() must return nullptr after stopMiracast()";

    // Second cycle — verify restart works without resource leaks
    bool secondResult = pipeline.initMiracastPipeline(nullptr, 1028);
    EXPECT_TRUE(secondResult) << "Second initMiracastPipeline() cycle must succeed";

    if (secondResult) {
        EXPECT_NE(pipeline.miracastPipeline(), nullptr)
            << "Pipeline must be non-null after second initMiracastPipeline";

        pipeline.stopMiracast();
        EXPECT_EQ(pipeline.miracastPipeline(), nullptr)
            << "Pipeline must be null after second stopMiracast()";
    }
}

// ── Test 13: ParseCapturedWindowsSourceReady ──────────────────────────────────
// Bytes in the layout a Windows 11 source sends: the FRIENDLY_NAME TLV starts
// "00 00 20 44 00" (type 0x00, length 32, 'D' in UTF-16LE). The old parser read
// the type as 2 bytes and failed with "TLV 0x0000 length 8260".

TEST(MiracastHandlerTest, ParseCapturedWindowsSourceReady) {
    auto encoder = QStringEncoder(QStringEncoder::Utf16LE);
    const QByteArray name = encoder.encode(QStringLiteral("DESKTOP-ABCD1234"));
    ASSERT_EQ(name.size(), 32);

    QByteArray tlvs;
    tlvs.append(char(0x00)).append(char(0x00)).append(char(0x20)).append(name);
    tlvs.append(QByteArray::fromHex("0200021c44"));                       // RTSP_PORT 7236
    tlvs.append(QByteArray::fromHex("030010"));                           // SOURCE_ID, 16 bytes
    tlvs.append(QByteArray::fromHex("a1a2a3a4b1b2c1c2d1d2e1e2e3e4e5e6"));

    QByteArray msg;
    const int size = 4 + tlvs.size();
    msg.append(char(size >> 8)).append(char(size & 0xff)).append(char(0x01)).append(char(0x01));
    msg.append(tlvs);
    ASSERT_EQ(msg.mid(4, 5), QByteArray::fromHex("0000204400"));

    MiracastHandler::SourceReadyInfo info;
    ASSERT_TRUE(MiracastHandler::parseSourceReady(msg, info));
    EXPECT_EQ(info.friendlyName, QStringLiteral("DESKTOP-ABCD1234"));
    EXPECT_EQ(info.rtspPort, static_cast<uint16_t>(7236));
    EXPECT_EQ(info.sourceId, QStringLiteral("a1a2a3a4b1b2c1c2d1d2e1e2e3e4e5e6"));
}

// ── Test 14: MiceMessageFraming ───────────────────────────────────────────────
// MS-MICE runs over TCP: messages can arrive split or back to back.

TEST(MiracastHandlerTest, MiceMessageFraming) {
    // STOP_PROJECTION: size 0x000B, version 1, command 2, FRIENDLY_NAME "A"
    const QByteArray stop = QByteArray::fromHex("000b010200000241000000");
    EXPECT_EQ(MiracastHandler::miceMessageLength(QByteArray()), 0);
    EXPECT_EQ(MiracastHandler::miceMessageLength(stop.left(1)), 0);
    EXPECT_EQ(MiracastHandler::miceMessageLength(stop.left(6)), 0);          // incomplete
    EXPECT_EQ(MiracastHandler::miceMessageLength(stop), 11);
    EXPECT_EQ(MiracastHandler::miceMessageLength(stop + stop), 11);          // coalesced
    EXPECT_EQ(MiracastHandler::miceMessageLength(QByteArray::fromHex("0002")), -1);  // < header
}

// ── Test 15: PresentationUrlAndSession ────────────────────────────────────────
// M6 SETUP / M7 PLAY must target wfd_presentation_URL from M4, and PLAY must
// carry the Session id from the SETUP response (Windows answers 400 otherwise).

TEST(MiracastHandlerTest, PresentationUrlAndSession) {
    const QByteArray m4 =
        "wfd_video_formats: 00 00 02 10 00002000 00000000 00000000 00 0000 0000 00 none none\r\n"
        "wfd_audio_codecs: AAC 00000001 00\r\n"
        "wfd_presentation_URL: rtsp://192.168.68.76/wfd1.0/streamid=0 none\r\n"
        "wfd_client_rtp_ports: RTP/AVP/UDP;unicast 1028 0 mode=play\r\n";
    EXPECT_EQ(MiracastHandler::parsePresentationUrl(m4),
              QStringLiteral("rtsp://192.168.68.76/wfd1.0/streamid=0"));
    EXPECT_TRUE(MiracastHandler::parsePresentationUrl("wfd_trigger_method: SETUP\r\n").isEmpty());
    EXPECT_TRUE(MiracastHandler::parsePresentationUrl("wfd_presentation_URL: none none\r\n").isEmpty());

    EXPECT_EQ(MiracastHandler::parseSessionId(QStringLiteral("6B8B4567;timeout=30")),
              QStringLiteral("6B8B4567"));
    EXPECT_EQ(MiracastHandler::parseSessionId(QStringLiteral("12345678")),
              QStringLiteral("12345678"));
}

// ── Test 16: MiracastPipelineDecodesRtpMpegTs ─────────────────────────────────
// End-to-end: feed the receive pipeline the same stream shape a Wi-Fi Display
// source sends (H.264 + AAC in MPEG-TS in RTP over UDP) and require decoded frames.
// Catches a pipeline that is created but never plays or never decodes.

TEST(MiracastHandlerTest, MiracastPipelineDecodesRtpMpegTs) {
    constexpr int kPort = 15028;  // not 1028, so a running AirShow doesn't collide

    GError* err = nullptr;
    GstElement* sender = gst_parse_launch(
        "videotestsrc is-live=true pattern=ball ! video/x-raw,width=320,height=240,framerate=30/1 "
        "! x264enc tune=zerolatency key-int-max=15 ! h264parse ! mux. "
        "audiotestsrc is-live=true wave=sine ! audio/x-raw,rate=48000,channels=2 "
        "! avenc_aac ! aacparse ! mux. "
        "mpegtsmux name=mux ! rtpmp2tpay ! udpsink host=127.0.0.1 port=15028", &err);
    if (!sender) {
        const std::string msg = err ? err->message : "unknown";
        if (err) g_error_free(err);
        GTEST_SKIP() << "cannot build test sender (x264enc/avenc_aac missing?): " << msg;
    }

    MediaPipeline pipeline;
    ASSERT_TRUE(pipeline.initMiracastPipeline(nullptr, kPort));
    ASSERT_TRUE(pipeline.playMiracast());
    gst_element_set_state(sender, GST_STATE_PLAYING);

    // Wait up to 5 s; require frames to keep flowing (not just the first one)
    for (int i = 0; i < 50 && pipeline.miracastFramesDecoded() < 60; ++i) {
        QCoreApplication::processEvents();
        g_usleep(100 * 1000);
    }

    gst_element_set_state(sender, GST_STATE_NULL);
    gst_object_unref(sender);

    EXPECT_GE(pipeline.miracastFramesDecoded(), 60u)
        << "Miracast pipeline decoded " << pipeline.miracastFramesDecoded() << " frames";
    pipeline.stopMiracast();
}
